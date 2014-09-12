#include "eb_chan.h"
#include <stdlib.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "eb_assert.h"
#include "eb_port.h"
#include "eb_atomic.h"

// TODO: update comments
// TODO: standardize assertion indentation

#pragma mark - Types -
typedef struct {
    eb_spinlock lock;
    size_t cap;
    size_t len;
    eb_port *ports;
} *port_list;

static inline void port_list_free(port_list l);

/* Creates a new empty list */
static inline port_list port_list_alloc(size_t cap) {
    assert(cap > 0);
    
    port_list result = malloc(sizeof(*result));
        eb_assert_or_recover(result, goto failed);
    
    result->lock = 0;
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
static inline void port_list_free(port_list l) {
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
static inline void port_list_add(port_list l, const eb_port p) {
    assert(l);
    assert(p);
    
    eb_spinlock_lock(&l->lock);
        /* Sanity-check that the list's length is less than its capacity */
        eb_assert_or_bail(l->len <= l->cap, "Sanity check failed");
        
        /* Expand the list's buffer if it's full */
        if (l->len == l->cap) {
            l->cap *= 2;
            // TODO: bad idea to call realloc() inside of a spinlock, but how should we fix it?
            // having our ports stored in a statically-sized array would arbitrarily limit us
            // to a certain number of ports, and that would suck...
            l->ports = realloc(l->ports, l->cap * sizeof(*(l->ports)));
            // TODO: handle allocation failures better
            eb_assert_or_recover(l->ports, return);
        }
        
        l->ports[l->len] = eb_port_retain(p);
        l->len++;
    eb_spinlock_unlock(&l->lock);
}

/* Remove the first occurence of 'p' in the list. Returns whether a port was actually removed. */
static inline bool port_list_rm(port_list l, eb_port p) {
    assert(l);
    assert(p);
    
    bool result = false;
    eb_spinlock_lock(&l->lock);
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
    eb_spinlock_unlock(&l->lock);
    return result;
}

/* Signal the first port in the list that isn't 'ignore' */
// TODO: we may want to randomize the port that we pick out of the list?
static inline void port_list_signal_first(const port_list l, eb_port ignore) {
    assert(l);
    
    eb_port p = NULL;
    eb_spinlock_lock(&l->lock);
        for (size_t i = 0; i < l->len; i++) {
            if (l->ports[i] != ignore) {
                p = eb_port_retain(l->ports[i]);
                break;
            }
        }
    eb_spinlock_unlock(&l->lock);
    
    if (p) {
        eb_port_signal(p);
        eb_port_release(p);
        p = NULL;
    }
}

enum {
    /* Buffered/unbuffered channel states */
    chanstate_open,
    chanstate_closed,
    /* Unbuffered channel states */
    chanstate_send,
    chanstate_recv,
    chanstate_ack,
    chanstate_done,
    chanstate_cancelled
}; typedef int32_t chanstate;

struct eb_chan {
    unsigned int retain_count;
    eb_spinlock lock;
    chanstate state;
    
    port_list sends;
    port_list recvs;
    
    /* Buffered ivars */
    size_t buf_cap;
    size_t buf_len;
    size_t buf_idx;
    const void **buf;
    
    /* Unbuffered ivars */
    uintptr_t unbuf_id;
    eb_chan_op *unbuf_op;
    eb_port unbuf_port;
};

#pragma mark - Channel creation/lifecycle -
static inline void eb_chan_free(eb_chan c) {
    /* Intentionally allowing c==NULL so that this function can be called from eb_chan_create() */
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

eb_chan eb_chan_create(size_t buf_cap) {
    static const size_t k_init_buf_cap = 16;
    
    eb_chan c = malloc(sizeof(*c));
        eb_assert_or_recover(c, goto failed);
    memset(c, 0, sizeof(*c));
    
    c->retain_count = 1;
    c->lock = 0;
    c->state = chanstate_open;
    
    c->sends = port_list_alloc(k_init_buf_cap);
        eb_assert_or_recover(c->sends, goto failed);
    c->recvs = port_list_alloc(k_init_buf_cap);
        eb_assert_or_recover(c->recvs, goto failed);
    
    if (buf_cap) {
        /* ## Buffered */
        c->buf_cap = buf_cap;
        c->buf_len = 0;
        c->buf_idx = 0;
        c->buf = malloc(c->buf_cap * sizeof(*(c->buf)));
            eb_assert_or_recover(c->buf, goto failed);
    } else {
        /* ## Unbuffered */
        c->unbuf_id = 0;
        c->unbuf_op = NULL;
        c->unbuf_port = NULL;
    }
    
    /* Issue a memory barrier since we didn't have the lock acquired for our set up (and this channel could theoretically
       be passed to another thread without a barrier, and that'd be bad news...) */
    eb_atomic_barrier();
    
    return c;
    failed: {
        eb_chan_free(c);
        return NULL;
    }
}

eb_chan eb_chan_retain(eb_chan c) {
    assert(c);
    eb_atomic_add(&c->retain_count, 1);
    return c;
}

void eb_chan_release(eb_chan c) {
    assert(c);
    if (eb_atomic_add(&c->retain_count, -1) == 0) {
        eb_chan_free(c);
    }
}

#pragma mark - Channel closing -
void eb_chan_close(eb_chan c) {
    assert(c);
    
    bool done = false;
    while (!done) {
        eb_port signal_port = NULL;
        eb_spinlock_lock(&c->lock);
            chanstate state = c->state;
                eb_assert_or_bail(state != chanstate_closed, "Illegal close of already-closed channel.");
                eb_assert_or_bail(state != chanstate_send && state != chanstate_ack, "Illegal close of channel while send is in progress.");
            
            if (state == chanstate_open) {
                c->state = chanstate_closed;
                done = true;
            } else if (state == chanstate_recv) {
                if (c->unbuf_port) {
                    signal_port = eb_port_retain(c->unbuf_port);
                }
            }
        eb_spinlock_unlock(&c->lock);
        
        /* Wake up the recv */
        if (signal_port) {
            eb_port_signal(signal_port);
            eb_port_release(signal_port);
            signal_port = NULL;
        }
    }
    
    /* Wake up the sends/recvs. (It's illegal to have senders at this point, and we'll wind up aborting if we do...) */
    port_list_signal_first(c->sends, NULL);
    port_list_signal_first(c->recvs, NULL);
}

#pragma mark - Getters -
size_t eb_chan_get_buf_cap(eb_chan c) {
    assert(c);
    return c->buf_cap;
}

size_t eb_chan_get_buf_len(eb_chan c) {
    assert(c);
    
    /* buf_len is only valid if the channel's buffered */
    if (!c->buf_cap) {
        return 0;
    }
    
    size_t r = 0;
    eb_spinlock_lock(&c->lock);
        r = c->buf_len;
    eb_spinlock_unlock(&c->lock);
    return r;
}

#pragma mark - Multiple operations -
eb_chan_op eb_chan_send_op(eb_chan c, const void *val) {
    return (eb_chan_op){.chan = c, .send = true, .open = false, .val = val};
}

eb_chan_op eb_chan_recv_op(eb_chan c) {
    return (eb_chan_op){.chan = c, .send = false, .open = false, .val = NULL};
}

enum {
    op_result_complete,     /* The op completed and the caller should return */
    op_result_next,         /* The op couldn't make any progress and the caller should move on to the next op */
    op_result_next_clean,   /* Same as _next, but signifies that cleanup_op() must be called for the op */
    op_result_retry,        /* The channel's busy and we should try the op again */
}; typedef unsigned int op_result;

static inline op_result send_buf(uintptr_t id, const eb_chan_op *op, eb_port port, eb_nsecs timeout) {
        assert(op);
        assert(op->chan);
    
    eb_chan c = op->chan;
    op_result result = op_result_next;
    
    /* Verify that the channel is open */
    eb_assert_or_bail(c->state == chanstate_open, "Illegal send on non-open channel");
    
    if (c->buf_len < c->buf_cap) {
        /* It looks like our channel's in an acceptable state, so try to acquire the lock */
        if (eb_spinlock_try(&c->lock)) {
            bool signal_recv = false;
            if (c->buf_len < c->buf_cap) {
                /* ## Sending, buffered, channel open, buffer has space */
                /* Notify the channel's recvs if our buffer is going from empty to non-empty */
                signal_recv = (!c->buf_len);
                /* Add the value to the buffer */
                size_t idx = (c->buf_idx + c->buf_len) % c->buf_cap;
                c->buf[idx] = op->val;
                c->buf_len++;
                /* Set our flag signifying that we completed this op. */
                result = op_result_complete;
            }
            
            eb_spinlock_unlock(&c->lock);
            
            if (signal_recv) {
                port_list_signal_first(c->recvs, port);
            }
        } else {
            result = op_result_retry;
        }
    }
    
    return result;
}

static inline op_result recv_buf(uintptr_t id, eb_chan_op *op, eb_port port, eb_nsecs timeout) {
        assert(op);
        assert(op->chan);
    
    eb_chan c = op->chan;
    op_result result = op_result_next;
    
    chanstate state = c->state;
    /* Sanity-check the channel's state */
    eb_assert_or_bail(state == chanstate_open || state == chanstate_closed, "Invalid channel state");
    
    if (c->buf_len || state == chanstate_closed) {
        if (eb_spinlock_try(&c->lock)) {
            state = c->state;
            bool signal_send = false;
            if (c->buf_len) {
                /* ## Receiving, buffered, buffer non-empty */
                /* Notify the channel's sends if our buffer is going from full to not-full */
                signal_send = (c->buf_len == c->buf_cap);
                /* Set our op's state signifying that we received a value */
                op->open = true;
                op->val = c->buf[c->buf_idx];
                /* Update chan's buffer */
                c->buf_len--;
                c->buf_idx = (c->buf_idx + 1) % c->buf_cap;
                /* Set our flag signifying that we completed this op. */
                result = op_result_complete;
            } else if (state == chanstate_closed) {
                /* ## Receiving, buffered, buffer empty, channel closed */
                /* Set our op's state signifying that it completed because the channel's closed */
                op->open = false;
                op->val = NULL;
                /* We completed this op so set our return value! */
                result = op_result_complete;
            }
            
            eb_spinlock_unlock(&c->lock);
            
            if (signal_send) {
                port_list_signal_first(c->sends, port);
            }
        } else {
            result = op_result_retry;
        }
    }
    
    return result;
}

static inline op_result send_unbuf(uintptr_t id, eb_chan_op *op, eb_port port, eb_nsecs timeout) {
        assert(op);
        assert(op->chan);
    
    eb_chan c = op->chan;
    op_result result = op_result_next;
    
    if ((c->state == chanstate_open && timeout != eb_nsecs_zero) ||
        c->state == chanstate_closed ||
        (c->state == chanstate_send && c->unbuf_op == op) ||
        (c->state == chanstate_recv && c->unbuf_id != id) ||
        (c->state == chanstate_ack && c->unbuf_op == op)) {
        
        /* It looks like our channel's in an acceptable state, so try to acquire the lock */
        if (eb_spinlock_try(&c->lock)) {
            bool signal_recv = false;
            if (c->state == chanstate_open && timeout != eb_nsecs_zero) {
                c->state = chanstate_send;
                c->unbuf_id = id;
                c->unbuf_op = op;
                c->unbuf_port = port;
                signal_recv = true;
                result = op_result_next_clean;
            } else if (c->state == chanstate_closed) {
                /* Sending on a closed channel -- not allowed! */
                eb_assert_or_bail(c->state != chanstate_closed, "Illegal send on closed channel");
            } else if (c->state == chanstate_send && c->unbuf_op == op) {
                /* We own the send op that's in progress, so assign chan's unbuf_port */
                /* Verify that the unbuf_id matches our 'id' parameter. If this assertion fails, it means there's likely
                   one eb_chan_op being shared by multiple threads, which isn't allowed. */
                eb_assert_or_bail(c->unbuf_id == id, "Send id invalid");
                /* Assign the port */
                c->unbuf_port = port;
                result = op_result_next_clean;
            } else if (c->state == chanstate_recv && c->unbuf_id != id) {
                /* We verified (immediately above) that the recv isn't part of the same op pool (we can't do unbuffered
                   sends/recvs from the same _do() call) */
                
                /* Sanity check -- make sure the op is a recv */
                eb_assert_or_bail(!c->unbuf_op->send, "Op isn't a recv as expected");
                
                c->state = chanstate_ack;
                c->unbuf_op->open = true;
                c->unbuf_op->val = op->val;
                
                eb_port signal_port = (c->unbuf_port ? eb_port_retain(c->unbuf_port) : NULL);
                eb_spinlock_unlock(&c->lock);
                
                /* Wake up the recv */
                if (signal_port) {
                    eb_port_signal(signal_port);
                    eb_port_release(signal_port);
                    signal_port = NULL;
                }
                
                /* Wait until chan transitions to _done or _cancelled. */
                for (;;) {
                    chanstate state = *((volatile chanstate *)&c->state);
                    if (state == chanstate_done || state == chanstate_cancelled) {
                        eb_spinlock_lock(&c->lock);
                            if (c->state == chanstate_done) {
                                result = op_result_complete;
                            } else if (c->state == chanstate_cancelled && timeout != eb_nsecs_zero) {
                                /* We're resetting the channel's state to _open, so we should try the op again */
                                result = op_result_retry;
                            }
                            
                            if (c->state == chanstate_done || c->state == chanstate_cancelled) {
                                c->state = chanstate_open;
                                /* Wakeup a send since one of them can now proceed */
                                signal_recv = true;
                                /* We're intentionally bypassing our loop's unlock because we unlock the channel
                                   outside the encompassing if-statement. */
                                break;
                            }
                        eb_spinlock_unlock(&c->lock);
                    }
                }
            } else if (c->state == chanstate_ack && c->unbuf_op == op) {
                /* A recv acknowledged our send! */
                /* Verify that the unbuf_id matches our 'id' parameter. If this assertion fails, it means there's likely
                   one eb_chan_op being shared by multiple threads, which isn't allowed. */
                eb_assert_or_bail(c->unbuf_id == id, "unbuf_id invalid");
                /* A recv is polling for chan's state to change, so update it to signal that we're done sending! */
                c->state = chanstate_done;
                result = op_result_complete;
            }
            
            eb_spinlock_unlock(&c->lock);
            
            if (signal_recv) {
                port_list_signal_first(c->recvs, port);
            }
        } else {
            result = op_result_retry;
        }
    }
    
    return result;
}

static inline op_result recv_unbuf(uintptr_t id, eb_chan_op *op, eb_port port, eb_nsecs timeout) {
        assert(op);
        assert(op->chan);
    
    eb_chan c = op->chan;
    op_result result = op_result_next;
    
    if ((c->state == chanstate_open && timeout != eb_nsecs_zero) ||
        c->state == chanstate_closed ||
        (c->state == chanstate_send && c->unbuf_id != id) ||
        (c->state == chanstate_recv && c->unbuf_op == op) ||
        (c->state == chanstate_ack && c->unbuf_op == op)) {
        
        /* It looks like our channel's in an acceptable state, so try to acquire the lock */
        if (eb_spinlock_try(&c->lock)) {
            bool signal_send = false;
            if (c->state == chanstate_open && timeout != eb_nsecs_zero) {
                c->state = chanstate_recv;
                c->unbuf_id = id;
                c->unbuf_op = op;
                c->unbuf_port = port;
                signal_send = true;
                result = op_result_next_clean;
            } else if (c->state == chanstate_closed) {
                /* Set our op's state signifying that we're returning the value due to a closed channel */
                op->open = false;
                op->val = NULL;
                /* We completed this op so set our return value! */
                result = op_result_complete;
            } else if (c->state == chanstate_send && c->unbuf_id != id) {
                /* We verified (immediately above) that the send isn't part of the same op pool (we can't do unbuffered
                   sends/recvs from the same _do() call) */
                
                /* Sanity check -- make sure the op is a send */
                eb_assert_or_bail(c->unbuf_op->send, "Op isn't a send as expected");
                
                c->state = chanstate_ack;
                op->open = true;
                op->val = c->unbuf_op->val;
                
                eb_port signal_port = (c->unbuf_port ? eb_port_retain(c->unbuf_port) : NULL);
                eb_spinlock_unlock(&c->lock);
                
                /* Wake up the send */
                if (signal_port) {
                    eb_port_signal(signal_port);
                    eb_port_release(signal_port);
                    signal_port = NULL;
                }
                
                /* Wait until chan transitions to _done or _cancelled. */
                for (;;) {
                    chanstate state = *((volatile chanstate *)&c->state);
                    if (state == chanstate_done || state == chanstate_cancelled) {
                        eb_spinlock_lock(&c->lock);
                            if (c->state == chanstate_done) {
                                result = op_result_complete;
                            } else if (c->state == chanstate_cancelled && timeout != eb_nsecs_zero) {
                                /* We're resetting the channel's state to _open, so we should try the op again */
                                result = op_result_retry;
                            }
                            
                            if (c->state == chanstate_done || c->state == chanstate_cancelled) {
                                c->state = chanstate_open;
                                /* Wakeup a send since one of them can now proceed */
                                signal_send = true;
                                /* We're intentionally bypassing our loop's unlock because we unlock the channel
                                   outside the encompassing if-statement. */
                                break;
                            }
                        eb_spinlock_unlock(&c->lock);
                    }
                }
            } else if (c->state == chanstate_recv && c->unbuf_op == op) {
                /* We own the recv op that's in progress, so assign chan's unbuf_port */
                /* Verify that the _recv_id matches our 'id' parameter. If this assertion fails, it means there's likely
                   one eb_chan_op being shared by multiple threads, which isn't allowed. */
                eb_assert_or_bail(c->unbuf_id == id, "unbuf_id invalid");
                /* Assign the port */
                c->unbuf_port = port;
                result = op_result_next_clean;
            } else if (c->state == chanstate_ack && c->unbuf_op == op) {
                /* A send acknowledged our recv! */
                /* Verify that the unbuf_id matches our 'id' parameter. If this assertion fails, it means there's likely
                   one eb_chan_op being shared by multiple threads, which isn't allowed. */
                eb_assert_or_bail(c->unbuf_id == id, "unbuf_id invalid");
                /* A send is polling for chan's state to change, so update it to signal that we're done sending! */
                c->state = chanstate_done;
                result = op_result_complete;
            }
            
            eb_spinlock_unlock(&c->lock);
            
            if (signal_send) {
                port_list_signal_first(c->sends, port);
            }
        } else {
            result = op_result_retry;
        }
    }
    
    return result;
}

static inline void cleanup_op(const eb_chan_op *op, eb_port port) {
        assert(op);
    eb_chan c = op->chan;
    bool signal_send = false;
    bool signal_recv = false;
    eb_spinlock_lock(&c->lock);
        if (c->state == chanstate_send && c->unbuf_op == op) {
            /* 'op' was in the process of an unbuffered send on the channel, but no recv had arrived
               yet, so reset state to _open. */
            c->state = chanstate_open;
            signal_send = true;
        } else if (c->state == chanstate_recv && c->unbuf_op == op) {
            /* 'op' was in the process of an unbuffered recv on the channel, but no send had arrived
               yet, so reset state to _open. */
            c->state = chanstate_open;
            signal_recv = true;
        } else if (c->state == chanstate_ack && c->unbuf_op == op) {
            /* A counterpart acknowledged 'op' but, but 'op' isn't the one that completed in our select() call, so we're cancelling. */
            c->state = chanstate_cancelled;
        }
    eb_spinlock_unlock(&c->lock);
    
    if (signal_send) {
        port_list_signal_first(c->sends, port);
    }
    
    if (signal_recv) {
        port_list_signal_first(c->recvs, port);
    }
}

static inline op_result try_op(uintptr_t id, eb_chan_op *op, eb_port port, eb_nsecs timeout) {
        assert(op);
    eb_chan c = op->chan;
    if (c) {
        if (op->send) {
            /* ## Send */
            return (c->buf_cap ? send_buf(id, op, port, timeout) : send_unbuf(id, op, port, timeout));
        } else {
            /* ## Receive */
            return (c->buf_cap ? recv_buf(id, op, port, timeout) : recv_unbuf(id, op, port, timeout));
        }
    }
    return op_result_next;
}

eb_chan_op *eb_chan_do(eb_chan_op *const ops[], size_t nops, eb_nsecs timeout) {
    // TODO: randomize iteration by shuffling input array once (upon entry)
        assert(ops);
    eb_port port = NULL;
    eb_chan_op *result = NULL;
    uintptr_t id = (uintptr_t)&result;
    bool cleanup_ops[nops];
    memset(cleanup_ops, 0, sizeof(cleanup_ops));
    
    /* ## Shuffle our input array */
    /* No need to shuffle arrays that have 0 or 1 elements */
//    if (nops > 1) {
//        for (size_t i = 0; i < nops; i++) {
//            size_t ridx = random() % nops;
//            eb_chan_op *tmp = ops[ridx];
//            ops[ridx] = ops[i];
//            ops[i] = tmp;
//        }
//    }
    
    if (timeout == eb_nsecs_zero) {
        /* ## timeout == 0: try every op exactly once; if none of them can proceed, return NULL. */
        for (size_t i = 0; i < nops; i++) {
            eb_chan_op *op = ops[i];
            op_result r;
            while ((r = try_op(id, op, port, timeout)) == op_result_retry);
            
            /* Update cleanup_ops. Note that we don't need to consider _busy because we waited until
               the operation returned something other than _busy. */
            cleanup_ops[i] = (r == op_result_next_clean);
            
            /* If the op completed, we need to exit! */
            if (r == op_result_complete) {
                result = op;
                goto cleanup;
            }
        }
    } else {
        /* ## timeout != 0 */
        eb_nsecs start_time = (timeout != eb_nsecs_forever ? eb_time_now() : 0);
        for (;;) {
            /* ## Fast path: loop over our operations to see if one of them was able to send/receive. (If not,
               we'll enter the slow path where we put our thread to sleep until we're signalled.) */
            if (nops) {
                const size_t k_attempt_multiplier = 500;
                for (size_t i = 0; i < k_attempt_multiplier * nops; i++) {
                    size_t idx = (i % nops);
                    eb_chan_op *op = ops[idx];
                    op_result r = try_op(id, op, port, timeout);
                    
                    /* Update cleanup_ops. In the case of _busy, we can't update cleanup_ops, because its semantics imply
                       that no information could be garnered from the channel. */
                    if (r == op_result_next_clean) {
                        cleanup_ops[idx] = true;
                    } else if (r != op_result_retry) {
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
                    eb_chan_op *op = ops[i];
                    eb_chan c = op->chan;
                    if (c) {
                        port_list_add((op->send ? c->sends : c->recvs), port);
                    }
                }
            }
            
            /* Before we go to sleep, call try_op() for every op until we get a non-busy return value. This way we'll ensure
               that no op is actually able to be performed, and we'll also ensure that 'port' is registered as the 'unbuf_port'
               for the necessary channels. */
            for (size_t i = 0; i < nops; i++) {
                eb_chan_op *op = ops[i];
                op_result r;
                while ((r = try_op(id, op, port, timeout)) == op_result_retry);
                
                /* Update cleanup_ops. Note that we don't need to consider _busy because we waited until
                   the operation returned something other than _busy. */
                cleanup_ops[i] = (r == op_result_next_clean);
                
                /* If the op completed, we need to exit! */
                if (r == op_result_complete) {
                    result = op;
                    goto cleanup;
                }
            }
            
            eb_nsecs wait_timeout = eb_nsecs_forever;
            if (timeout != eb_nsecs_forever) {
                /* If we have a timeout, determine how much time has elapsed, because we may have timed-out. */
                eb_nsecs elapsed = eb_time_now() - start_time;
                /* Check if we timed-out */
                if (elapsed < timeout) {
                    wait_timeout = timeout - elapsed;
                } else {
                    goto cleanup;
                }
            }
            
            /* Put our thread to sleep until someone alerts us of an event */
            eb_port_wait(port, wait_timeout);
        }
    }
    
    /* Cleanup! */
    cleanup: {
        if (port) {
            for (size_t i = 0; i < nops; i++) {
                eb_chan_op *op = ops[i];
                eb_chan c = op->chan;
                if (c) {
                    port_list ports = (op->send ? c->sends : c->recvs);
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