#include "eb_chan.h"
#include <stdlib.h>
#include <assert.h>
#include <libkern/OSAtomic.h>
#include <mach/mach.h>
#include "eb_assert.h"
#include "eb_port.h"

typedef struct {
    size_t cap;
    size_t len;
    eb_port_t *ports;
} *eb_port_list_t;

#define OSSpinLock int32_t
#define OS_SPINLOCK_INIT 0

#define OSSpinLockTry(l) ({                                \
    OSAtomicCompareAndSwap32(false, true, l);              \
})

#define OSSpinLockLock(l) ({                                \
    while (!OSAtomicCompareAndSwap32(false, true, l));      \
})

#define SpinLock(l, block) ({                                                        \
    (block ? (OSSpinLockLock(l), true) : OSSpinLockTry(l));      \
})                                                                                   \

#define OSSpinLockUnlock(l) ({                                  \
    assert(OSAtomicCompareAndSwap32Barrier(true, false, l));    \
})

#define SpinUnlock OSSpinLockUnlock

#define CAS OSAtomicCompareAndSwap32
#define CASB OSAtomicCompareAndSwap32Barrier

static inline void port_list_free(eb_port_list_t l);

/* Creates a new empty list */
static inline eb_port_list_t port_list_alloc(size_t cap) {
    assert(cap > 0);
    
    eb_port_list_t result = malloc(sizeof(*result));
        eb_assert_or_recover(result, goto failed);
    
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

/* Creates a new list on the stack by copying every port into the new list, and retaining each port in the process */
#define port_list_stack_copy(l) ({                        \
    assert(l);                                            \
                                                          \
    eb_port_list_t r = alloca(sizeof(*(r)));              \
    r->cap = l->len;                                      \
    r->len = l->len;                                      \
    r->ports = alloca(r->len * sizeof(*(r->ports)));      \
                                                          \
    for (size_t i = 0; i < r->len; i++) {                 \
        r->ports[i] = eb_port_retain(l->ports[i]);        \
    }                                                     \
                                                          \
    r;                                                    \
})

static inline void port_list_stack_free(eb_port_list_t l) {
    assert(l);
    /* Release each port in our list */
    for (size_t i = 0; i < l->len; i++) {
        eb_port_release(l->ports[i]);
    }
    l->ports = NULL;
}

/* Releases every port in the list, and frees the list itself */
static inline void port_list_free(eb_port_list_t l) {
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
static inline void port_list_add(eb_port_list_t l, const eb_port_t p) {
    assert(l);
    assert(p);
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
}

/* Remove the first occurence of 'p' in the list. Returns whether a port was actually removed. */
static inline bool port_list_rm(eb_port_list_t l, eb_port_t p) {
    assert(l);
    assert(p);
    /* Sanity-check that the list's length is less than its capacity */
    eb_assert_or_bail(l->len <= l->cap, "Sanity-check failed");
    
    /* Search for first occurence of the given port. If we find it, release it and move all subsequent ports up one spot. */
    for (size_t i = 0; i < l->len; i++) {
        if (l->ports[i] == p) {
            /* Release the port */
            eb_port_release(l->ports[i]);
            /* Move the last element in the port list into the now-vacant spot */
            l->ports[i] = l->ports[l->len-1];
            /* Decrement the buffer length */
            l->len--;
            return true;
        }
    }
    return false;
}

/* Signal every port in the list */
static inline void port_list_signal(const eb_port_list_t l, const eb_port_t ignore) {
    assert(l);
    for (size_t i = 0; i < l->len; i++) {
        if (l->ports[i] != ignore) {
            eb_port_signal(l->ports[i]);
        }
    }
}

enum {
    unbuf_state_idle,
    unbuf_state_busy,
    unbuf_state_sendfast,
    unbuf_state_recvfast,
    unbuf_state_send,
    unbuf_state_recv,
    unbuf_state_done,
    unbuf_state_cancelled
}; typedef int32_t unbuf_state_t;

struct eb_chan {
    OSSpinLock lock;
    bool open;
    size_t buf_cap;
    
    eb_port_list_t sends;
    eb_port_list_t recvs;
    
    /* Buffered ivars */
    size_t buf_len;
    const void **buf;
    
    /* Unbuffered ivars */
    unbuf_state_t unbuf_state;
    uintptr_t unbuf_send_id;
    const eb_chan_op_t *unbuf_send_op;
};

/* Channel allocation/deallocation */
eb_chan_t eb_chan_alloc(size_t buf_cap) {
    static const size_t k_init_buf_cap = 16;
    
    eb_chan_t c = malloc(sizeof(*c));
        eb_assert_or_recover(c, goto failed);
    bzero(c, sizeof(*c));
    
    c->lock = OS_SPINLOCK_INIT;
    c->open = true;
    c->buf_cap = buf_cap;
    
    c->sends = port_list_alloc(k_init_buf_cap);
        eb_assert_or_recover(c->sends, goto failed);
    c->recvs = port_list_alloc(k_init_buf_cap);
        eb_assert_or_recover(c->recvs, goto failed);
    
    if (c->buf_cap) {
        /* ## Buffered */
        c->buf_len = 0;
        c->buf = malloc(c->buf_cap * sizeof(*(c->buf)));
            eb_assert_or_recover(c->buf, goto failed);
    } else {
        /* ## Unbuffered */
        c->unbuf_state = unbuf_state_idle;
        c->unbuf_send_id = 0;
        c->unbuf_send_op = NULL;
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
    
    /* Mark ourself as closed, and wake up senders/receivers so that they notice our new state and act appropriately. */
    eb_port_list_t sends = NULL;
    eb_port_list_t recvs = NULL;
    SpinLock(&c->lock, true);
        eb_assert_or_bail(c->open, "Can't close a channel that's already closed.");
        c->open = false;
        sends = port_list_stack_copy(c->sends);
        recvs = port_list_stack_copy(c->recvs);
    SpinUnlock(&c->lock);
    
    /* Wake up our senders/receivers so they notice our new closed state. */
    port_list_signal(sends, NULL);
    port_list_stack_free(sends);
    sends = NULL;
    
    port_list_signal(recvs, NULL);
    port_list_stack_free(recvs);
    recvs = NULL;
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
    SpinLock(&c->lock, true);
        r = c->buf_len;
    SpinUnlock(&c->lock);
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
typedef enum {
    op_result_ok,      /* The op succeeded */
    op_result_fail,    /* The op failed (eg, the buffer was full) and we should move on to the next op */
    op_result_busy,    /* The channel was busy so we need to try again (we neither succeeded nor failed) */
} op_result_t;

static inline op_result_t send_buf(uintptr_t id, const eb_chan_op_t *op, eb_port_t port) {
    abort();
    return op_result_fail;
    
//        assert(op);
//        assert(op->chan);
//    
//    eb_chan_t chan = op->chan;
//    bool result = false;
//    if (SpinLock(&chan->lock, port != NULL)) {
//        eb_assert_or_bail(chan->open, "Can't send on closed channel.");
//        eb_port_list_t wakeup_ports = NULL;
//        if (chan->buf_len < chan->buf_cap) {
//            /* ## Sending, buffered, channel open, buffer has space */
//            /* Add the value to the buffer */
//            chan->buf[chan->buf_len] = op->val;
//            chan->buf_len++;
//            /* Notify the channel's recvs if our buffer went from empty to non-empty */
//            if (chan->buf_len == 1) {
//                /* Copy the channel's recvs so that we can signal them after we relinquish the lock, to notify
//                   them that we've added to the buffer. */
//                wakeup_ports = port_list_stack_copy(chan->recvs);
//            }
//            /* Set our flag signifying that we completed this op. */
//            result = true;
//        }
//        SpinUnlock(&chan->lock);
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

static inline op_result_t send_unbuf(uintptr_t id, const eb_chan_op_t *op, eb_port_t port) {
        assert(op);
        assert(op->chan);
    
    eb_chan_t chan = op->chan;
    op_result_t result = op_result_fail;
    
    /* Attempt to gain control of the channel's unbuf_state */
    if (CAS(unbuf_state_idle, unbuf_state_busy, &chan->unbuf_state)) {
        /* ## Fast path */
        /* We successfully gained control of unbuf_state, so assign our fields. */
        chan->unbuf_send_id = id;
        chan->unbuf_send_op = op;
        /* Transition from _busy->_sendfast, which should always succeed because we have exclusive control over unbuf_state. */
        assert(CASB(unbuf_state_busy, unbuf_state_sendfast, &chan->unbuf_state));
        
        // TODO: do we want to wakeup recvs here? profile, and if not, only wakeup recvs when we enter the slow path.
        
        /* Give the receiver a chance to receive. */
        eb_port_list_t wakeup_ports = NULL;
        /* By default we fail and should move on to the next op */
        result = op_result_fail;
        for (size_t i = 0; i < 100; i++) {
            if (CAS(unbuf_state_recvfast, unbuf_state_busy, &chan->unbuf_state)) {
                wakeup_ports = port_list_stack_copy(chan->sends);
                assert(CAS(unbuf_state_busy, unbuf_state_idle, &chan->unbuf_state));
                /* A receiver got the value! */
                result = op_result_ok;
                break;
            }
        }
        
        /* ## Slow path */
        if (result != op_result_ok) {
            /* Attempt transition from the fast-path to the slow-path (_sendfast->_send) */
            if (CAS(unbuf_state_sendfast, unbuf_state_busy, &chan->unbuf_state)) {
                /* We're in the slow-path, so wakeup the recvs since one of them can proceed. */
                wakeup_ports = port_list_stack_copy(chan->recvs);
                assert(CAS(unbuf_state_busy, unbuf_state_send, &chan->unbuf_state));
            } else {
                /* We failed to transition _sendfast->_busy, so the receiver must be receiving the value, so wait for it to complete. */
                while (!CAS(unbuf_state_recvfast, unbuf_state_busy, &chan->unbuf_state));
                /* We're resetting unbuf_state to _idle, so wakeup the sends since one of them can proceed. */
                wakeup_ports = port_list_stack_copy(chan->sends);
                assert(CAS(unbuf_state_busy, unbuf_state_idle, &chan->unbuf_state));
                result = op_result_ok;
            }
        }
        
        if (wakeup_ports->len) {
            /* Signal every port in wakeup_ports */
            port_list_signal(wakeup_ports, port);
            port_list_stack_free(wakeup_ports);
            wakeup_ports = NULL;
        }
    } else if (CAS(unbuf_state_send, unbuf_state_busy, &chan->unbuf_state)) {
        assert(CAS(unbuf_state_busy, unbuf_state_send, &chan->unbuf_state));
        /* The channel's already sending so the caller should move on to the next op */
        result = op_result_fail;
    } else if (CAS(unbuf_state_recv, unbuf_state_busy, &chan->unbuf_state)) {
        /* Check the send op in progress to see if it's ours */
        if (chan->unbuf_send_op == op) {
            /* This is our send op! */
            /* Verify that the _send_id matches our 'id' parameter. If this assertion fails, it means there's likely
               some static eb_chan_op_t being shared by multiple threads, which isn't allowed. */
            eb_assert_or_bail(chan->unbuf_send_id == id, "Send id invalid");
            /* A receiver is currently polling for chan's unbuf_state to change, so update it to signal that we're done sending! */
            assert(CAS(unbuf_state_busy, unbuf_state_done, &chan->unbuf_state));
            result = op_result_ok;
        } else {
            /* The send op in progress isn't ours, so ignore it and reset unbuf_state back to _recv. */
            assert(CAS(unbuf_state_busy, unbuf_state_recv, &chan->unbuf_state));
            /* The channel's in the process of receiving should move on to the next op */
            result = op_result_fail;
        }
    } else {
        /* Otherwise, we should try the op again because it was in an intermittent state */
        result = op_result_busy;
    }
    
    return result;
}

static inline op_result_t recv_buf(uintptr_t id, eb_chan_op_t *op, eb_port_t port) {
    abort();
    return op_result_fail;
    
//        assert(op);
//        assert(op->chan);
//    
//    eb_chan_t chan = op->chan;
//    bool result = false;
//    if (SpinLock(&chan->lock, port != NULL)) {
//        eb_port_list_t wakeup_ports = NULL;
//        if (chan->buf_len) {
//            /* ## Receiving, buffered, buffer non-empty */
//            /* Set our op's state signifying that we received a value */
//            op->open = true;
//            op->val = chan->buf[0];
//            /* Update chan's buffer */
//            chan->buf_len--;
//            memmove(&chan->buf[0], &chan->buf[1], chan->buf_len * sizeof(*(chan->buf)));
//            /* Notify the channel's sends if our buffer went from full to not-full */
//            if (chan->buf_len == chan->buf_cap-1) {
//                /* Copy the channel's sends so that we can signal them after we relinquish the lock, to notify
//                   them that we've removed from the buffer. */
//                wakeup_ports = port_list_stack_copy(chan->sends);
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
//        SpinUnlock(&chan->lock);
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

static inline op_result_t recv_unbuf(uintptr_t id, eb_chan_op_t *op, eb_port_t port) {
        assert(op);
        assert(op->chan);
    
    eb_chan_t chan = op->chan;
    op_result_t result = op_result_fail;
    
    /* Attempt to gain control of the channel's unbuf_state */
    if (CAS(unbuf_state_sendfast, unbuf_state_busy, &chan->unbuf_state)) {
        op->open = true;
        op->val = chan->unbuf_send_op->val;
        assert(CAS(unbuf_state_busy, unbuf_state_recvfast, &chan->unbuf_state));
        result = op_result_ok;
    } else if (CAS(unbuf_state_send, unbuf_state_busy, &chan->unbuf_state)) {
        if (chan->unbuf_send_id != id) {
            op->open = true;
            op->val = chan->unbuf_send_op->val;
            eb_port_list_t wakeup_ports = port_list_stack_copy(chan->sends);
            assert(CAS(unbuf_state_busy, unbuf_state_recv, &chan->unbuf_state));
            
            /* Wakeup chan's sends so it updates its unbuf_state */
            if (wakeup_ports->len) {
                port_list_signal(wakeup_ports, port);
                port_list_stack_free(wakeup_ports);
                wakeup_ports = NULL;
            }
            
            /* Poll until chan's unbuf_state is either _done or _cancelled, and reset the state to _idle. */
            for (;;) {
                if (CAS(unbuf_state_done, unbuf_state_busy, &chan->unbuf_state)) {
                    wakeup_ports = port_list_stack_copy(chan->sends);
                    assert(CAS(unbuf_state_busy, unbuf_state_idle, &chan->unbuf_state));
                    result = op_result_ok;
                    break;
                } else if (CAS(unbuf_state_cancelled, unbuf_state_busy, &chan->unbuf_state)) {
                    wakeup_ports = port_list_stack_copy(chan->sends);
                    assert(CAS(unbuf_state_busy, unbuf_state_idle, &chan->unbuf_state));
                    result = op_result_fail;
                    break;
                }
            }
            
            /* Wakeup chan's sends because we reset unbuf_state to _idle, so one of them can now proceed. */
            if (wakeup_ports->len) {
                port_list_signal(wakeup_ports, port);
                port_list_stack_free(wakeup_ports);
                wakeup_ports = NULL;
            }
        } else {
            /* We can't receive from our own op */
            assert(CAS(unbuf_state_busy, unbuf_state_send, &chan->unbuf_state));
            /* Move on to the next op */
            result = op_result_fail;
        }
    } else {
        /* Otherwise, we should try the op again because it was in an intermittent state */
        result = op_result_busy;
    }
    
    return result;
}

static inline op_result_t cleanup_after_op(eb_port_t port, eb_chan_op_t *op) {
        assert(op);
    eb_chan_t chan = op->chan;
    op_result_t result = op_result_fail;
    
    if (chan && op->send && !chan->buf_cap) {
        if (CAS(unbuf_state_idle, unbuf_state_idle, &chan->unbuf_state)) {
            result = op_result_ok;
        } else if (CAS(unbuf_state_send, unbuf_state_busy, &chan->unbuf_state)) {
            if (chan->unbuf_send_op == op) {
                /* 'op' was in the process of an unbuffered send on the channel, but no receiver had arrived
                   yet, so reset unbuf_state to _idle. */
                eb_port_list_t wakeup_ports = port_list_stack_copy(chan->sends);
                assert(CAS(unbuf_state_busy, unbuf_state_idle, &chan->unbuf_state));
                
                /* Wakeup chan's sends since we just reset unbuf_state to _idle. */
                if (wakeup_ports->len) {
                    port_list_signal(wakeup_ports, port);
                    port_list_stack_free(wakeup_ports);
                    wakeup_ports = NULL;
                }
            } else {
                /* This isn't our op, reset unbuf_state back to _send */
                assert(CAS(unbuf_state_busy, unbuf_state_send, &chan->unbuf_state));
            }
            result = op_result_ok;
        } else if (CAS(unbuf_state_recv, unbuf_state_busy, &chan->unbuf_state)) {
            if (chan->unbuf_send_op == op) {
                /* 'op' was in the process of an unbuffered send on the channel, and a receiver is waiting on
                   the send, so set unbuf_state to _cancelled so that the receiver notices and stops waiting
                   on the sender. */
                assert(CAS(unbuf_state_busy, unbuf_state_cancelled, &chan->unbuf_state));
            } else {
                /* This isn't our op, reset unbuf_state back to _recv */
                assert(CAS(unbuf_state_busy, unbuf_state_recv, &chan->unbuf_state));
            }
            result = op_result_ok;
        } else {
            result = op_result_busy;
        }
    } else {
        result = op_result_ok;
    }
    
    return result;
}

static inline op_result_t try_op(uintptr_t id, eb_chan_op_t *op, eb_port_t port) {
        assert(op);
    eb_chan_t chan = op->chan;
    if (chan) {
        if (op->send) {
            /* ## Send */
            return (chan->buf_cap ? send_buf(id, op, port) : send_unbuf(id, op, port));
        } else if (!op->send) {
            /* ## Receive */
            return (chan->buf_cap ? recv_buf(id, op, port) : recv_unbuf(id, op, port));
        }
    }
    return op_result_fail;
}

eb_chan_op_t *eb_chan_do(eb_chan_op_t *const ops[], size_t nops) {
        assert(ops);
    eb_port_t port = NULL;
    eb_chan_op_t *result = NULL;
    /* ## Fast path: loop randomly over our operations to see if one of them was able to send/receive.
       If not, we'll enter the slow path where we put our thread to sleep until we're signalled. */
    if (nops) {
        static const size_t k_attempt_multiplier = 50;
        for (size_t i = 0; i < k_attempt_multiplier * nops; i++) {
            // TODO: not using random() here speeds this up a lot, so we should generate random bits more efficiently
//            result = try_op((uintptr_t)&result, ops[(random() % nops)], NULL);
            eb_chan_op_t *op = ops[(i % nops)];
            op_result_t r = try_op((uintptr_t)&result, op, NULL);
            /* If the op completed, we need to exit! */
            if (r == op_result_ok) {
                result = op;
                goto cleanup;
            }
        }
    }
    
    /* ## Slow path: we weren't able to find an operation that could send/receive, so we'll create a
       port to receive notifications on and put this thread to sleep until someone wakes us up. */
    /* Create our port that we'll attach to channels so that we can be notified when events occur. */
    port = eb_port_create();
        eb_assert_or_recover(port, goto cleanup);
    
    /* First, register our port for the appropriate notifications on every channel. */
    /* This adds 'port' to the channel's sends/recvs (depending on the op), which we clean up at the
       end of this function. */
    for (size_t i = 0; i < nops; i++) {
        eb_chan_t chan = ops[i]->chan;
        if (chan) {
            eb_chan_op_t *op = ops[i];
            if (op->send) {
                for (;;) {
                    if (CAS(unbuf_state_idle, unbuf_state_busy, &chan->unbuf_state)) {
                        port_list_add(chan->sends, port);
                        assert(CASB(unbuf_state_busy, unbuf_state_idle, &chan->unbuf_state));
                        break;
                    } else if (CAS(unbuf_state_send, unbuf_state_busy, &chan->unbuf_state)) {
                        port_list_add(chan->sends, port);
                        assert(CASB(unbuf_state_busy, unbuf_state_send, &chan->unbuf_state));
                        break;
                    } else if (CAS(unbuf_state_recv, unbuf_state_busy, &chan->unbuf_state)) {
                        port_list_add(chan->sends, port);
                        assert(CASB(unbuf_state_busy, unbuf_state_recv, &chan->unbuf_state));
                        break;
                    }
                }
            } else {
                for (;;) {
                    if (CAS(unbuf_state_idle, unbuf_state_busy, &chan->unbuf_state)) {
                        port_list_add(chan->recvs, port);
                        assert(CASB(unbuf_state_busy, unbuf_state_idle, &chan->unbuf_state));
                        break;
                    } else if (CAS(unbuf_state_send, unbuf_state_busy, &chan->unbuf_state)) {
                        port_list_add(chan->recvs, port);
                        assert(CASB(unbuf_state_busy, unbuf_state_send, &chan->unbuf_state));
                        break;
                    } else if (CAS(unbuf_state_recv, unbuf_state_busy, &chan->unbuf_state)) {
                        port_list_add(chan->recvs, port);
                        assert(CASB(unbuf_state_busy, unbuf_state_recv, &chan->unbuf_state));
                        break;
                    }
                }
            }
        }
    }
    
    for (;;) {
        // TODO: randomize iteration!
        for (size_t i = 0; i < nops; i++) {
            eb_chan_op_t *op = ops[i];
            op_result_t r;
            while ((r = try_op((uintptr_t)&result, op, NULL)) == op_result_busy);
            /* If the op completed, we need to exit! */
            if (r == op_result_ok) {
                result = op;
                goto cleanup;
            }
        }
        
        /* Go to sleep until someone alerts us of an event */
        eb_port_wait(port);
    }
    
    /* Cleanup! */
    cleanup: {
        if (port) {
            for (size_t i = 0; i < nops; i++) {
                eb_chan_t chan = ops[i]->chan;
                if (chan) {
                    eb_chan_op_t *op = ops[i];
                    if (op->send) {
                        for (;;) {
                            if (CAS(unbuf_state_idle, unbuf_state_busy, &chan->unbuf_state)) {
                                port_list_rm(chan->sends, port);
                                assert(CASB(unbuf_state_busy, unbuf_state_idle, &chan->unbuf_state));
                                break;
                            } else if (CAS(unbuf_state_send, unbuf_state_busy, &chan->unbuf_state)) {
                                port_list_rm(chan->sends, port);
                                assert(CASB(unbuf_state_busy, unbuf_state_send, &chan->unbuf_state));
                                break;
                            } else if (CAS(unbuf_state_recv, unbuf_state_busy, &chan->unbuf_state)) {
                                port_list_rm(chan->sends, port);
                                assert(CASB(unbuf_state_busy, unbuf_state_recv, &chan->unbuf_state));
                                break;
                            }
                        }
                    } else {
                        for (;;) {
                            if (CAS(unbuf_state_idle, unbuf_state_busy, &chan->unbuf_state)) {
                                port_list_rm(chan->recvs, port);
                                assert(CASB(unbuf_state_busy, unbuf_state_idle, &chan->unbuf_state));
                                break;
                            } else if (CAS(unbuf_state_send, unbuf_state_busy, &chan->unbuf_state)) {
                                port_list_rm(chan->recvs, port);
                                assert(CASB(unbuf_state_busy, unbuf_state_send, &chan->unbuf_state));
                                break;
                            } else if (CAS(unbuf_state_recv, unbuf_state_busy, &chan->unbuf_state)) {
                                port_list_rm(chan->recvs, port);
                                assert(CASB(unbuf_state_busy, unbuf_state_recv, &chan->unbuf_state));
                                break;
                            }
                        }
                    }
                }
            }
        }
        
        for (size_t i = 0; i < nops; i++) {
            while (cleanup_after_op(port, ops[i]) == op_result_busy);
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
    // TODO: we need to call cleanup_after_op() here, because we may need to reset unbuf_state
    eb_chan_op_t *result = NULL;
    for (size_t i = 0; i < nops; i++) {
        eb_chan_op_t *op = ops[i];
        op_result_t r;
        while ((r = try_op((uintptr_t)&result, op, NULL)) == op_result_busy);
        /* If the op completed, we need to exit! */
        if (r == op_result_ok) {
            result = op;
            break;
        }
    }
    return result;
}
