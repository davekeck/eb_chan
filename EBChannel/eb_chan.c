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

/* Creates a new list, and copies every port into the new list by retaining each one */
// TODO: if profiling shows that this method is taking too long, we should copy the port_lists via the stack instead of the heap.
static inline eb_port_list_t port_list_copy(const eb_port_list_t l) {
    assert(l);
    
    eb_port_list_t r = port_list_alloc(l->cap);
        eb_assert_or_recover(r, return NULL);
    
    r->len = l->len;
    for (size_t i = 0; i < r->len; i++) {
        r->ports[i] = eb_port_retain(l->ports[i]);
    }
    
    return r;
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
            /* Shift the subsequent ports up one spot */
            memmove(&l->ports[i], &l->ports[i+1], (l->len - i - 1) * sizeof(*(l->ports)));
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

/* Return whether the list is empty, ignoring the existence of a specified port */
static inline bool port_list_empty(const eb_port_list_t l, const eb_port_t ignore) {
    assert(l);
    for (size_t i = 0; i < l->len; i++) {
        if (l->ports[i] != ignore) {
            return false;
        }
    }
    return true;
}

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
    bool unbuf_send;
    bool unbuf_recv;
    const void *unbuf_val;
    eb_port_t unbuf_port;
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
        c->unbuf_send = false;
        c->unbuf_recv = false;
        c->unbuf_val = NULL;
        c->unbuf_port = eb_port_create();
            eb_assert_or_recover(c->unbuf_port, goto failed);
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
    } else {
        /* ## Unbuffered */
        if (c->unbuf_port) {
            eb_port_release(c->unbuf_port);
            c->unbuf_port = NULL;
        }
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
    OSSpinLockLock(&c->lock);
        eb_assert_or_bail(c->open, "Can't close a channel that's already closed.");
        c->open = false;
        sends = port_list_copy(c->sends);
        recvs = port_list_copy(c->recvs);
    OSSpinLockUnlock(&c->lock);
    
    /* Wake up our senders/receivers so they notice our new closed state. */
    port_list_signal(sends, NULL);
    port_list_free(sends);
    sends = NULL;
    
    port_list_signal(recvs, NULL);
    port_list_free(recvs);
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
static inline bool send_buf(eb_port_t port, const eb_chan_op_t *op) {
        assert(op);
        assert(op->chan);
    
    eb_chan_t chan = op->chan;
    eb_port_list_t wakeup_ports = NULL;
    bool result = false;
    OSSpinLockLock(&chan->lock);
        eb_assert_or_bail(chan->open, "Can't send on closed channel.");
        if (chan->buf_len < chan->buf_cap) {
            /* ## Sending, buffered, channel open, buffer has space */
            /* Add the value to the buffer */
            chan->buf[chan->buf_len] = op->val;
            chan->buf_len++;
            /* Copy the channel's recvs so that we can signal them after we relinquish the lock, to notify
               them that we've added to the buffer. */
            wakeup_ports = port_list_copy(chan->recvs);
            /* Set our flag signifying that we completed this op. */
            result = true;
        } else {
            /* ## Sending, buffered, channel open, buffer full */
            if (port) {
                /* Add our port to the channel's sends, so that we get notified when someone receives on the channel. */
                port_list_add(chan->sends, port);
            }
        }
    OSSpinLockUnlock(&chan->lock);
    
    /* Signal every port in wakeup_ports */
    if (wakeup_ports) {
        // TODO: should we exclude 'port' here?
        port_list_signal(wakeup_ports, port);
        port_list_free(wakeup_ports);
        wakeup_ports = NULL;
    }
    
    return result;
}

static inline bool send_unbuf(eb_port_t port, const eb_chan_op_t *op) {
        assert(op);
        assert(op->chan);
    
    // TODO: add a fast path here that doesn't require using ports
    eb_chan_t chan = op->chan;
    eb_port_list_t wakeup_ports = NULL;
    bool sending = false;
    OSSpinLockLock(&chan->lock);
        eb_assert_or_bail(chan->open, "Can't send on closed channel.");
        /* Check that chan->recvs has more receivers than just our own port */
        if (!port_list_empty(chan->recvs, port) && !chan->unbuf_send) {
            /* ## Sending, unbuffered, channel open, receivers exist and there's no sender yet */
            /* Set our state, including the value being sent. */
            chan->unbuf_send = true;
            chan->unbuf_recv = false;
            chan->unbuf_val = op->val;
            /* Copy the channel's recvs so that we can signal them after we relinquish the lock, to notify
               them that we've added to the buffer. */
            wakeup_ports = port_list_copy(chan->recvs);
            /* Set our local flag marking that we're actually sending a value. */
            sending = true;
        } else {
            /* ## Sending, unbuffered, channel open, no receivers or a sender already has control */
            if (port) {
                /* Add our port to the channel's sends, so that we get notified when a receiver arrives. */
                port_list_add(chan->sends, port);
            }
        }
    OSSpinLockUnlock(&chan->lock);
    
    /* Signal every port in wakeup_ports */
    if (wakeup_ports) {
        // TODO: should we exclude 'port' here?
        port_list_signal(wakeup_ports, port);
        port_list_free(wakeup_ports);
        wakeup_ports = NULL;
    }
    
    /* If we're actually sending a value, wait until a receiver notifies the channel's _unbufPort */
    bool result = false;
    if (sending) {
        bool doneSending = false;
        while (!doneSending) {
            /* Wait until a receiver signals us. */
            eb_port_wait(chan->unbuf_port);
            
            /* When we're signaled, it's either because a receiver has received the value, or the channel's recvs
               changed so we need to make sure that we should still be blocking and trying to send. */
            OSSpinLockLock(&chan->lock);
                eb_assert_or_bail(chan->open, "Can't send on closed channel.");
                bool recv = chan->unbuf_recv;
                bool has_recvs = !port_list_empty(chan->recvs, port);
                if (recv || !has_recvs) {
                    /* Someone received our value, or there are no more receivers */
                    if (recv) {
                        /* We completed an op so set our result, which will cause us to exit from our outer loop. */
                        result = true;
                    } else if (!has_recvs) {
                        if (port) {
                            /* No receiver got our value and now there are no more receivers, so add our port to the
                               channel's sends, so that we get notified when a receiver arrives. */
                            port_list_add(chan->sends, port);
                        }
                    }
                    
                    /* Reset our channel's unbuf_send so others can send */
                    chan->unbuf_send = false;
                    /* Copy the channel's sends so we can wake them up (after we relinquish the lock), so that one of them can send. */
                    wakeup_ports = port_list_copy(chan->sends);
                    /* Set our flag so that we exit our inner loop that's waiting for someone to receive our value */
                    doneSending = true;
                }
            OSSpinLockUnlock(&chan->lock);
        }
    }
    
    /* Signal every port in wakeup_ports */
    if (wakeup_ports) {
        // TODO: should we exclude 'port' here?
        port_list_signal(wakeup_ports, port);
        port_list_free(wakeup_ports);
        wakeup_ports = NULL;
    }
    
    return result;
}

static inline bool recv_buf(eb_port_t port, eb_chan_op_t *op) {
        assert(op);
        assert(op->chan);
    
    eb_chan_t chan = op->chan;
    eb_port_list_t wakeup_ports = NULL;
    bool result = false;
    OSSpinLockLock(&chan->lock);
        if (chan->buf_len) {
            /* ## Receiving, buffered, buffer non-empty */
            /* Set our op's state signifying that we received a value */
            op->open = true;
            op->val = chan->buf[0];
            /* Update chan's buffer */
            chan->buf_len--;
            memmove(&chan->buf[0], &chan->buf[1], chan->buf_len * sizeof(*(chan->buf)));
            /* Copy the channel's sends so that we can signal them after we relinquish the lock, to notify
               them that we've removed from the buffer. */
            wakeup_ports = port_list_copy(chan->sends);
            /* Set our flag signifying that we completed this op. */
            result = true;
        } else if (!chan->open) {
            /* ## Receiving, buffered, buffer empty, channel closed */
            /* Set our op's state signifying that it completed because the channel's closed */
            op->open = false;
            op->val = NULL;
            /* Set our flag signifying that we completed this op. */
            result = true;
        } else {
            /* ## Receiving, buffered, buffer empty, channel open */
            if (port) {
                /* Add our port to the channel's recvs, so that we get notified when someone sends on the channel. */
                port_list_add(chan->recvs, port);
            }
        }
    OSSpinLockUnlock(&chan->lock);
    
    /* Signal every port in wakeup_ports */
    if (wakeup_ports) {
        // TODO: should we exclude 'port' here?
        port_list_signal(wakeup_ports, port);
        port_list_free(wakeup_ports);
        wakeup_ports = NULL;
    }
    
    return result;
}

static inline bool recv_unbuf(eb_port_t port, eb_chan_op_t *op) {
        assert(op);
        assert(op->chan);
    
    eb_chan_t chan = op->chan;
    eb_port_t unbuf_port = NULL;
    eb_port_list_t wakeup_ports = NULL;
    bool result = false;
    OSSpinLockLock(&chan->lock);
        if (chan->unbuf_send && !chan->unbuf_recv) {
            /* ## Receiving, unbuffered, value is available */
            /* Set our op's state signifying that we received a value */
            op->open = true;
            op->val = chan->unbuf_val;
            /* Set the channel's flag marking that someone received its value. */
            chan->unbuf_recv = true;
            /* Signal the channel's unbuf_port after we relinquish the lock, to notify the sender that we received its value. */
            unbuf_port = eb_port_retain(chan->unbuf_port);
            /* Set our flag signifying that we completed this op. */
            result = true;
        } else if (!chan->open) {
            /* ## Receiving, unbuffered, no value available, channel closed */
            /* Set our op's state signifying that it completed because the channel's closed */
            op->open = false;
            op->val = NULL;
            /* Set our flag signifying that we completed this op. */
            result = true;
        } else {
            /* ## Receiving, unbuffered, no value available, channel open */
            if (port) {
                /* Add our port to the channel's recvs, so that we get notified when someone sends on the channel. */
                port_list_add(chan->recvs, port);
                /* Copy the channel's sends so that we can signal them after we relinquish the lock, to notify
                   them that there's a receiver. */
                wakeup_ports = port_list_copy(chan->sends);
            }
        }
    OSSpinLockUnlock(&chan->lock);
    
    /* Signal wakeup_port */
    if (unbuf_port) {
        eb_port_signal(unbuf_port);
        eb_port_release(unbuf_port);
        unbuf_port = NULL;
    }
    
    /* Signal every port in wakeup_ports */
    if (wakeup_ports) {
        // TODO: should we exclude 'port' here?
        port_list_signal(wakeup_ports, port);
        port_list_free(wakeup_ports);
        wakeup_ports = NULL;
    }
    
    return result;
}

static inline void cleanup_after_op(eb_port_t port, eb_chan_op_t *op) {
        assert(op);
    
    eb_port_t wakeup_port = NULL;
    if (port) {
        eb_chan_t chan = op->chan;
        
        OSSpinLockLock(&chan->lock);
            if (op->send) {
                port_list_rm(chan->sends, port);
            } else {
                /* We need to tell whether we removed the port from the channel's 'recvs' so that we know whether we need to signal
                   the channel's unbuf_port, because it may have been waiting on this thread to receive. */
                bool rm_recv = port_list_rm(chan->recvs, port);
                /* If there's a sender trying to send an value on this channel (and the channel's unbuffered, as _unbufObj
                   will only be set if it's unbuffered), and we actually removed a receiver from recvs, we need to wake up
                   the sender so that it can check if it still should be trying to send there might not be any receivers anymore. */
                if (rm_recv && chan->unbuf_send && !chan->unbuf_recv) {
                    wakeup_port = eb_port_retain(chan->unbuf_port);
                }
            }
        OSSpinLockUnlock(&chan->lock);
    }
    
    if (wakeup_port) {
        eb_port_signal(wakeup_port);
        eb_port_release(wakeup_port);
        wakeup_port = NULL;
    }
}

static inline eb_chan_op_t *try_op(eb_port_t port, eb_chan_op_t *op) {
        assert(op);
    eb_chan_t chan = op->chan;
    if (chan && op->send) {
        /* ## Send */
        bool r = (chan->buf_cap ? send_buf(port, op) : send_unbuf(port, op));
        if (r) {
            return op;
        }
    } else if (chan && !op->send) {
        /* ## Receive */
        bool r = (chan->buf_cap ? recv_buf(port, op) : recv_unbuf(port, op));
        if (r) {
            return op;
        }
    }
    return NULL;
}

eb_chan_op_t *eb_chan_do(eb_chan_op_t *const ops[], size_t nops) {
    assert(ops);
    eb_port_t port = NULL;
    eb_chan_op_t *result = NULL;
    try: {
        /* ## Fast path: loop randomly over our operations to see if one of them was able to send/receive.
           If not, we'll enter the slow path where we put our thread to sleep until we're signalled. */
        if (nops) {
            static const size_t k_attempt_multiplier = 500;
            for (size_t i = 0; i < k_attempt_multiplier * nops; i++) {
                result = try_op(NULL, ops[(random() % nops)]);
                /* If the op completed, we need to exit! */
                if (result) {
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
        /* Note that this adds 'port' to the channel's sends/recvs (depending on the op), which we clean
           up at the end of this function via cleanup_after_op(). */
        // TODO: randomize iteration!
        for (size_t i = 0; i < nops; i++) {
            result = try_op(port, ops[i]);
            /* If this op completed, we need to exit! */
            if (result) {
                goto cleanup;
            }
        }
        
        for (;;) {
            /* Go to sleep until someone alerts us of an event */
            eb_port_wait(port);
            
            // TODO: randomize iteration!
            for (size_t i = 0; i < nops; i++) {
                // TODO: we still want to supply port here so that we don't notify our own port, right? we still want to prevent adding that port to channels' sets though.
                result = try_op(NULL, ops[i]);
                /* If the op completed, we need to exit! */
                if (result) {
                    goto cleanup;
                }
            }
        }
    }
    
    /* Cleanup! */
    cleanup: {
        for (size_t i = 0; i < nops; i++) {
            cleanup_after_op(port, ops[i]);
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
    for (size_t i = 0; i < nops; i++) {
        result = try_op(NULL, ops[i]);
        /* If this op completed, we need to exit! */
        if (result) {
            break;
        }
    }
    return result;
}
