#include "eb_chan.h"
#include <stdlib.h>
#include <assert.h>
#include <libkern/OSAtomic.h>
#include <mach/mach.h>
#include <stdio.h>
#include "eb_assert.h"
#include "eb_port.h"

// TODO: update comments

//#define OSSpinLock int32_t
//#define OS_SPINLOCK_INIT 0
//
//#define OSSpinLockTry(l) ({                                \
//    OSAtomicCompareAndSwap32(false, true, l);              \
//})
//
//#define OSSpinLockLock(l) ({                                \
//    while (!OSAtomicCompareAndSwap32(false, true, l));      \
//    true;                                           \
//})
//
//#define OSSpinLockUnlock(l) ({                                  \
//    assert(OSAtomicCompareAndSwap32Barrier(true, false, l));    \
//})

typedef struct {
    OSSpinLock lock;
    size_t cap;
    size_t len;
    eb_port_t *ports;
} *port_list_t;

static inline void port_list_free(port_list_t l);

/* Creates a new empty list */
static inline port_list_t port_list_alloc(size_t cap) {
    assert(cap > 0);
    
    port_list_t result = malloc(sizeof(*result));
        eb_assert_or_recover(result, goto failed);
    
    result->lock = OS_SPINLOCK_INIT;
    result->cap = cap;
    result->len = 0;
    result->ports = malloc(cap * sizeof(*(result->ports)));
        eb_assert_or_recover(result->ports, goto failed);
    
    return result;
    failed: {
        port_list_free(result);
        return NULL;
    }
}

/* Releases every port in the list, and frees the list itself */
static inline void port_list_free(port_list_t l) {
    /* Intentionally allowing l==NULL */
    if (!l) {
        return;
    }
    
    /* Release each port in our list */
    for (size_t i = 0; i < l->len; i++) {
        eb_port_release(l->ports[i]);
    }
    
    free(l->ports);
    l->ports = NULL;
    
    free(l);
    l = NULL;
}

/* Add a port to the end of the list, expanding the buffer as necessary */
static inline void port_list_add(port_list_t l, const eb_port_t p) {
    assert(l);
    assert(p);
    
    OSSpinLockLock(&l->lock);
        /* Sanity-check that the list's length is less than its capacity */
        eb_assert_or_bail(l->len <= l->cap, "Sanity check failed");
        
        /* Expand the list's buffer if it's full */
        if (l->len == l->cap) {
            l->cap *= 2;
            l->ports = realloc(l->ports, l->cap * sizeof(*(l->ports)));
            // TODO: handle allocation failures better
            eb_assert_or_recover(l->ports, return);
        }
        
        l->ports[l->len] = eb_port_retain(p);
        l->len++;
    OSSpinLockUnlock(&l->lock);
}

/* Remove the first occurence of 'p' in the list. Returns whether a port was actually removed. */
static inline bool port_list_rm(port_list_t l, eb_port_t p) {
    assert(l);
    assert(p);
    
    bool result = false;
    OSSpinLockLock(&l->lock);
        /* Sanity-check that the list's length is less than its capacity */
        eb_assert_or_bail(l->len <= l->cap, "Sanity-check failed");
        
        /* Search for first occurence of the given port. If we find it, release it and move the last port in the list into the hole. */
        for (size_t i = 0; i < l->len; i++) {
            if (l->ports[i] == p) {
                /* Release the port */
                eb_port_release(l->ports[i]);
                /* Move the last element in the port list into the now-vacant spot */
                l->ports[i] = l->ports[l->len-1];
                /* Decrement the buffer length */
                l->len--;
                result = true;
                break;
            }
        }
    OSSpinLockUnlock(&l->lock);
    return result;
}

/* Signal the first port in the list that isn't 'ignore' */
// TODO: we may want to randomize the port that we pick out of the list?
static inline void port_list_signal_first(const port_list_t l, eb_port_t ignore) {
    assert(l);
    
    eb_port_t p = NULL;
    OSSpinLockLock(&l->lock);
        for (size_t i = 0; i < l->len; i++) {
            if (l->ports[i] != ignore) {
                p = eb_port_retain(l->ports[i]);
                break;
            }
        }
    OSSpinLockUnlock(&l->lock);
    
    if (p) {
        eb_port_signal(p);
        eb_port_release(p);
        p = NULL;
    }
}

enum {
    state_open,
    state_closed,
    state_send,
    state_recv,
    state_ack,
    state_done,
    state_cancelled
}; typedef int32_t state_t;

struct eb_chan {
    OSSpinLock lock;
    state_t state;
    
    port_list_t sends;
    port_list_t recvs;
    
    /* Buffered ivars */
    size_t buf_cap;
    size_t buf_len;
    const void **buf;
    
    /* Unbuffered ivars */
    uintptr_t unbuf_id;
    eb_chan_op_t *unbuf_op;
    eb_port_t unbuf_port;
};

/* Channel allocation/deallocation */
eb_chan_t eb_chan_alloc(size_t buf_cap) {
    static const size_t k_init_buf_cap = 16;
    
    eb_chan_t c = malloc(sizeof(*c));
        eb_assert_or_recover(c, goto failed);
    bzero(c, sizeof(*c));
    
    c->lock = OS_SPINLOCK_INIT;
    c->state = state_open;
    
    c->sends = port_list_alloc(k_init_buf_cap);
        eb_assert_or_recover(c->sends, goto failed);
    c->recvs = port_list_alloc(k_init_buf_cap);
        eb_assert_or_recover(c->recvs, goto failed);
    
    if (c->buf_cap) {
        /* ## Buffered */
        c->buf_cap = buf_cap;
        c->buf_len = 0;
        c->buf = malloc(c->buf_cap * sizeof(*(c->buf)));
            eb_assert_or_recover(c->buf, goto failed);
    } else {
        /* ## Unbuffered */
        c->unbuf_id = 0;
        c->unbuf_op = NULL;
        c->unbuf_port = NULL;
    }
    
    return c;
    failed: {
        eb_chan_free(c);
        return NULL;
    }
}

void eb_chan_free(eb_chan_t c) {
    /* Intentionally allowing c==NULL */
    if (!c) {
        return;
    }
    
    if (c->buf_cap) {
        /* ## Buffered */
        free(c->buf);
        c->buf = NULL;
    }
    
    port_list_free(c->recvs);
    c->recvs = NULL;
    
    port_list_free(c->sends);
    c->sends = NULL;
    
    free(c);
    c = NULL;
}

/* Close a channel */
void eb_chan_close(eb_chan_t c) {
    assert(c);
    
    bool done = false;
    while (!done) {
        eb_port_t wakeup_port = NULL;
        OSSpinLockLock(&c->lock);
            state_t state = c->state;
                eb_assert_or_bail(state != state_closed, "Illegal close of already-closed channel.");
                eb_assert_or_bail(state != state_send && state != state_ack, "Illegal close of channel while send is in progress.");
            
            if (state == state_open) {
                c->state = state_closed;
                done = true;
            } else if (state == state_recv) {
                if (c->unbuf_port) {
                    wakeup_port = eb_port_retain(c->unbuf_port);
                }
            }
        OSSpinLockUnlock(&c->lock);
        
        /* Wake up the recv */
        if (wakeup_port) {
            eb_port_signal(wakeup_port);
            eb_port_release(wakeup_port);
            wakeup_port = NULL;
        }
    }
    
    /* Wake up the first receiver. We don't bother to wake up senders here because it's illegal for the channel to have senders at this point anyway. */
    port_list_signal_first(c->recvs, NULL);
}

/* Getters */
size_t eb_chan_get_buf_cap(eb_chan_t c) {
    assert(c);
    return c->buf_cap;
}

size_t eb_chan_get_buf_len(eb_chan_t c) {
    assert(c);
    
    /* buf_len is only valid if the channel's buffered */
    if (!c->buf_cap) {
        return 0;
    }
    
    size_t r = 0;
    OSSpinLockLock(&c->lock);
        r = c->buf_len;
    OSSpinLockUnlock(&c->lock);
    return r;
}

/* Op-making convenience functions */
eb_chan_op_t eb_chan_send(eb_chan_t c, const void *val) {
    return (eb_chan_op_t){.chan = c, .send = true, .open = false, .val = val};
}

eb_chan_op_t eb_chan_recv(eb_chan_t c) {
    return (eb_chan_op_t){.chan = c, .send = false, .open = false, .val = NULL};
}

/* Performing operations on a channel */
enum {
    op_result_complete,     /* The op completed and the caller should return */
    op_result_next,         /* The op couldn't make any progress and the caller should move on to the next op */
    op_result_next_clean,   /* Same as _next, but signifies that cleanup_op() must be called for the op */
    op_result_busy,         /* The channel's busy and we should try the op again */
}; typedef unsigned int op_result_t;

static inline op_result_t send_buf(uintptr_t id, const eb_chan_op_t *op, eb_port_t port) {
    abort();
    return op_result_next;
    
//        assert(op);
//        assert(op->chan);
//    
//    eb_chan_t c = op->chan;
//    bool result = false;
//    if (SpinLock(&c->lock, port != NULL)) {
//        eb_assert_or_bail(chan->open, "Can't send on closed channel.");
//        port_list_t wakeup_ports = NULL;
//        if (c->buf_len < c->buf_cap) {
//            /* ## Sending, buffered, channel open, buffer has space */
//            /* Add the value to the buffer */
//            c->buf[c->buf_len] = op->val;
//            c->buf_len++;
//            /* Notify the channel's recvs if our buffer went from empty to non-empty */
//            if (c->buf_len == 1) {
//                /* Copy the channel's recvs so that we can signal them after we relinquish the lock, to notify
//                   them that we've added to the buffer. */
//                wakeup_ports = port_list_stack_copy(c->recvs);
//            }
//            /* Set our flag signifying that we completed this op. */
//            result = true;
//        }
//        SpinUnlock(&c->lock);
//        
//        /* Signal every port in wakeup_ports */
//        if (wakeup_ports) {
//            port_list_signal(wakeup_ports, port);
//            port_list_stack_free(wakeup_ports);
//            wakeup_ports = NULL;
//        }
//    }
//    
//    return result;
}

static inline op_result_t recv_buf(uintptr_t id, const eb_chan_op_t *op, eb_port_t port) {
    abort();
    return op_result_next;
    
//        assert(op);
//        assert(op->chan);
//    
//    eb_chan_t c = op->chan;
//    bool result = false;
//    if (SpinLock(&c->lock, port != NULL)) {
//        port_list_t wakeup_ports = NULL;
//        if (c->buf_len) {
//            /* ## Receiving, buffered, buffer non-empty */
//            /* Set our op's state signifying that we received a value */
//            op->open = true;
//            op->val = chan->buf[0];
//            /* Update chan's buffer */
//            c->buf_len--;
//            memmove(&c->buf[0], &c->buf[1], c->buf_len * sizeof(*(c->buf)));
//            /* Notify the channel's sends if our buffer went from full to not-full */
//            if (c->buf_len == c->buf_cap-1) {
//                /* Copy the channel's sends so that we can signal them after we relinquish the lock, to notify
//                   them that we've removed from the buffer. */
//                wakeup_ports = port_list_stack_copy(c->sends);
//            }
//            /* Set our flag signifying that we completed this op. */
//            result = true;
//        } else if (!chan->open) {
//            /* ## Receiving, buffered, buffer empty, channel closed */
//            /* Set our op's state signifying that it completed because the channel's closed */
//            op->open = false;
//            op->val = NULL;
//            /* Set our flag signifying that we completed this op. */
//            result = true;
//        }
//        SpinUnlock(&c->lock);
//        
//        /* Signal every port in wakeup_ports */
//        if (wakeup_ports) {
//            port_list_signal(wakeup_ports, port);
//            port_list_stack_free(wakeup_ports);
//            wakeup_ports = NULL;
//        }
//    }
//    
//    return result;
}

static inline op_result_t send_unbuf(uintptr_t id, eb_chan_op_t *op, eb_port_t port) {
        assert(op);
        assert(op->chan);
    
    eb_chan_t c = op->chan;
    op_result_t result = op_result_next;
    
    if (OSSpinLockTry(&c->lock)) {
        state_t state = c->state;
            /* Verify that the channel isn't closed */
            eb_assert_or_bail(state != state_closed, "Illegal send on closed channel");
        
        bool wakeup_send = false;
        bool wakeup_recv = false;
        if (state == state_open) {
            c->state = state_send;
            c->unbuf_id = id;
            c->unbuf_op = op;
            c->unbuf_port = port;
            wakeup_recv = true;
            result = op_result_next_clean;
        } else if (state == state_send) {
            if (c->unbuf_op == op) {
                /* We own the send op that's in progress, so assign chan's unbuf_port */
                /* Verify that the unbuf_id matches our 'id' parameter. If this assertion fails, it means there's likely
                   one eb_chan_op_t being shared by multiple threads, which isn't allowed. */
                eb_assert_or_bail(c->unbuf_id == id, "Send id invalid");
                /* Assign the port */
                c->unbuf_port = port;
                result = op_result_next_clean;
            }
        } else if (state == state_recv) {
            /* Verify that the recv isn't part of the same select() (we can't do unbuffered sends/recvs from the same select()) */
            if (c->unbuf_id != id) {
                    /* Sanity check -- make sure the op is a recv */
                    eb_assert_or_bail(!c->unbuf_op->send, "Op isn't a recv as expected");
                
                c->state = state_ack;
                c->unbuf_op->open = true;
                c->unbuf_op->val = op->val;
                
                eb_port_t wakeup_port = (c->unbuf_port ? eb_port_retain(c->unbuf_port) : NULL);
                OSSpinLockUnlock(&c->lock);
                
                /* Wake up the recv */
                if (wakeup_port) {
                    eb_port_signal(wakeup_port);
                    eb_port_release(wakeup_port);
                    wakeup_port = NULL;
                }
                
                /* Wait until chan transitions to _done or _cancelled. */
                for (;;) {
                    state_t state = *((volatile state_t *)&c->state);
                    if (state == state_done || state == state_cancelled) {
                        OSSpinLockLock(&c->lock);
                            if (c->state == state_done) {
                                result = op_result_complete;
                            }
                            
                            if (c->state == state_done || c->state == state_cancelled) {
                                c->state = state_open;
                                /* Wakeup a send since one of them can now proceed */
                                wakeup_send = true;
                                /* We're intentionally bypassing our loop's unlock because we unlock the channel
                                   outside the encompassing if-statement. */
                                break;
                            }
                        OSSpinLockUnlock(&c->lock);
                    }
                }
            }
        } else if (state == state_ack) {
            if (c->unbuf_op == op) {
                /* A recv acknowledged our send! */
                /* Verify that the unbuf_id matches our 'id' parameter. If this assertion fails, it means there's likely
                   one eb_chan_op_t being shared by multiple threads, which isn't allowed. */
                eb_assert_or_bail(c->unbuf_id == id, "unbuf_id invalid");
                /* A recv is polling for chan's state to change, so update it to signal that we're done sending! */
                c->state = state_done;
                result = op_result_complete;
            }
        }
        
        OSSpinLockUnlock(&c->lock);
        
        if (wakeup_send) {
            port_list_signal_first(c->sends, port);
        }
        
        if (wakeup_recv) {
            port_list_signal_first(c->recvs, port);
        }
    } else {
        result = op_result_busy;
    }
    
    return result;
}

static inline op_result_t recv_unbuf(uintptr_t id, eb_chan_op_t *op, eb_port_t port) {
        assert(op);
        assert(op->chan);
    
    eb_chan_t c = op->chan;
    op_result_t result = op_result_next;
    
    if (OSSpinLockTry(&c->lock)) {
        state_t state = c->state;
        bool wakeup_send = false;
        if (state == state_open) {
            c->state = state_recv;
            c->unbuf_id = id;
            c->unbuf_op = op;
            c->unbuf_port = port;
            wakeup_send = true;
            result = op_result_next_clean;
        } else if (state == state_closed) {
            /* Set our op's state signifying that we're returning the value due to a closed channel */
            op->open = false;
            op->val = NULL;
            /* We completed this op so set our return value! */
            result = op_result_complete;
        } else if (state == state_send) {
            /* Verify that the send isn't part of the same select() (we can't do unbuffered sends/recvs from the same select()) */
            if (c->unbuf_id != id) {
                    /* Sanity check -- make sure the op is a send */
                    eb_assert_or_bail(c->unbuf_op->send, "Op isn't a send as expected");
                
                c->state = state_ack;
                op->open = true;
                op->val = c->unbuf_op->val;
                
                eb_port_t wakeup_port = (c->unbuf_port ? eb_port_retain(c->unbuf_port) : NULL);
                OSSpinLockUnlock(&c->lock);
                
                /* Wake up the send */
                if (wakeup_port) {
                    eb_port_signal(wakeup_port);
                    eb_port_release(wakeup_port);
                    wakeup_port = NULL;
                }
                
                /* Wait until chan transitions to _done or _cancelled. */
                for (;;) {
                    state_t state = *((volatile state_t *)&c->state);
                    if (state == state_done || state == state_cancelled) {
                        OSSpinLockLock(&c->lock);
                            if (c->state == state_done) {
                                result = op_result_complete;
                            }
                            
                            if (c->state == state_done || c->state == state_cancelled) {
                                c->state = state_open;
                                /* Wakeup a send since one of them can now proceed */
                                wakeup_send = true;
                                /* We're intentionally bypassing our loop's unlock because we unlock the channel
                                   outside the encompassing if-statement. */
                                break;
                            }
                        OSSpinLockUnlock(&c->lock);
                    }
                }
            }
        } else if (state == state_recv) {
            if (c->unbuf_op == op) {
                /* We own the recv op that's in progress, so assign chan's unbuf_port */
                /* Verify that the _recv_id matches our 'id' parameter. If this assertion fails, it means there's likely
                   one eb_chan_op_t being shared by multiple threads, which isn't allowed. */
                eb_assert_or_bail(c->unbuf_id == id, "unbuf_id invalid");
                /* Assign the port */
                c->unbuf_port = port;
                result = op_result_next_clean;
            }
        } else if (state == state_ack) {
            if (c->unbuf_op == op) {
                /* A send acknowledged our recv! */
                /* Verify that the unbuf_id matches our 'id' parameter. If this assertion fails, it means there's likely
                   one eb_chan_op_t being shared by multiple threads, which isn't allowed. */
                eb_assert_or_bail(c->unbuf_id == id, "unbuf_id invalid");
                /* A send is polling for chan's state to change, so update it to signal that we're done sending! */
                c->state = state_done;
                result = op_result_complete;
            }
        }
        
        OSSpinLockUnlock(&c->lock);
        
        if (wakeup_send) {
            port_list_signal_first(c->sends, port);
        }
    } else {
        result = op_result_busy;
    }
    
    return result;
}

static inline void cleanup_op(const eb_chan_op_t *op, eb_port_t port) {
        assert(op);
    eb_chan_t c = op->chan;
    if (c && !c->buf_cap) {
        bool wakeup_send = false;
        bool wakeup_recv = false;
        OSSpinLockLock(&c->lock);
            state_t state = c->state;
            if (state == state_send) {
                if (c->unbuf_op == op) {
                    /* 'op' was in the process of an unbuffered send on the channel, but no recv had arrived
                       yet, so reset state to _open. */
                    c->state = state_open;
                    wakeup_send = true;
                }
            } else if (state == state_recv) {
                if (c->unbuf_op == op) {
                    /* 'op' was in the process of an unbuffered recv on the channel, but no send had arrived
                       yet, so reset state to _open. */
                    c->state = state_open;
                    wakeup_recv = true;
                }
            } else if (state == state_ack) {
                if (c->unbuf_op == op) {
                    /* A counterpart acknowledged 'op' but, but 'op' isn't the one that completed in our select() call, so we're cancelling. */
                    c->state = state_cancelled;
                }
            }
        OSSpinLockUnlock(&c->lock);
        
        if (wakeup_send) {
            port_list_signal_first(c->sends, port);
        }
        
        if (wakeup_recv) {
            port_list_signal_first(c->recvs, port);
        }
    }
}

static inline op_result_t try_op(uintptr_t id, eb_chan_op_t *op, eb_port_t port) {
        assert(op);
    eb_chan_t c = op->chan;
    if (c) {
        if (op->send) {
            /* ## Send */
            return (c->buf_cap ? send_buf(id, op, port) : send_unbuf(id, op, port));
        } else if (!op->send) {
            /* ## Receive */
            return (c->buf_cap ? recv_buf(id, op, port) : recv_unbuf(id, op, port));
        }
    }
    return op_result_next;
}

eb_chan_op_t *eb_chan_do(eb_chan_op_t *const ops[], size_t nops) {
        assert(ops);
    eb_port_t port = NULL;
    eb_chan_op_t *result = NULL;
    uintptr_t id = (uintptr_t)&result;
    bool cleanup_ops[nops];
    bzero(cleanup_ops, sizeof(cleanup_ops));
    
    for (;;) {
        /* ## Fast path: loop randomly over our operations to see if one of them was able to send/receive.
           If not, we'll enter the slow path where we put our thread to sleep until we're signalled. */
        if (nops) {
            static const size_t k_attempt_multiplier = 50;
            for (size_t i = 0; i < k_attempt_multiplier * nops; i++) {
                // TODO: not using random() here speeds this up a lot, so we should generate random bits more efficiently
                size_t idx = (i % nops);//(random() % nops);
                eb_chan_op_t *op = ops[idx];
                op_result_t r = try_op(id, op, port);
                
                /* Update cleanup_ops. In the case of _busy, we can't update cleanup_ops, because its semantics imply
                   that no information could be garnered from the channel. */
                if (r == op_result_next_clean) {
                    cleanup_ops[idx] = true;
                } else if (r != op_result_busy) {
                    cleanup_ops[idx] = false;
                }
                
                /* If the op completed, we need to exit! */
                if (r == op_result_complete) {
                    result = op;
                    goto cleanup;
                }
            }
        }
        
        /* ## Slow path: we weren't able to find an operation that could send/receive, so we'll create a
           port to receive notifications on and put this thread to sleep until someone wakes us up. */
        if (!port) {
            /* Create our port that we'll attach to channels so that we can be notified when events occur. */
            port = eb_port_create();
                eb_assert_or_recover(port, goto cleanup);
            
            /* Register our port for the appropriate notifications on every channel. */
            /* This adds 'port' to the channel's sends/recvs (depending on the op), which we clean up at the
               end of this function. */
            for (size_t i = 0; i < nops; i++) {
                eb_chan_op_t *op = ops[i];
                eb_chan_t c = op->chan;
                if (c) {
                    port_list_add((op->send ? c->sends : c->recvs), port);
                }
            }
        }
        
        // TODO: randomize iteration!
        for (size_t i = 0; i < nops; i++) {
            eb_chan_op_t *op = ops[i];
            op_result_t r;
            while ((r = try_op(id, op, port)) == op_result_busy);
            
            /* Update cleanup_ops. Note that we don't need to consider _busy because we waited until
               the operation returned something other than _busy. */
            cleanup_ops[i] = (r == op_result_next_clean);
            
            /* If the op completed, we need to exit! */
            if (r == op_result_complete) {
                result = op;
                goto cleanup;
            }
        }
        
        /* Put our thread to sleep until someone alerts us of an event */
        eb_port_wait(port);
    }
    
    /* Cleanup! */
    cleanup: {
        if (port) {
            for (size_t i = 0; i < nops; i++) {
                eb_chan_op_t *op = ops[i];
                eb_chan_t c = op->chan;
                if (c) {
                    port_list_t ports = (op->send ? c->sends : c->recvs);
                    port_list_rm(ports, port);
                    port_list_signal_first(ports, port);
                }
            }
        }
        
        for (size_t i = 0; i < nops; i++) {
            if (cleanup_ops[i]) {
                cleanup_op(ops[i], port);
            }
        }
        
        if (port) {
            eb_port_release(port);
            port = NULL;
        }
    }
    
    return result;
}

eb_chan_op_t *eb_chan_try(eb_chan_op_t *const ops[], size_t nops) {
    // TODO: randomize iteration!
    eb_chan_op_t *result = NULL;
    uintptr_t id = (uintptr_t)&result;
    bool cleanup_ops[nops];
    bzero(cleanup_ops, sizeof(cleanup_ops));
    
    for (size_t i = 0; i < nops; i++) {
        eb_chan_op_t *op = ops[i];
        op_result_t r;
        while ((r = try_op(id, op, NULL)) == op_result_busy);
        
        /* Update cleanup_ops. Note that we don't need to consider _busy because we waited until
           the operation returned something other than _busy. */
        cleanup_ops[i] = (r == op_result_next_clean);
        
        /* If the op completed, we need to exit! */
        if (r == op_result_complete) {
            result = op;
            break;
        }
    }
    
    for (size_t i = 0; i < nops; i++) {
        if (cleanup_ops[i]) {
            cleanup_op(ops[i], NULL);
        }
    }
    
    return result;
}
