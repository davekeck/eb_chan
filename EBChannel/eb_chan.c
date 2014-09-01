#include "eb_chan.h"
#include <stdlib.h>
#include <assert.h>
#include <stdio.h>
#include <strings.h>
#include "eb_assert.h"
#include "eb_sem.h"
#include "eb_atomic.h"

// TODO: update comments
// TODO: standardize assertion indentation

#pragma mark - Types -
typedef struct {
    eb_spinlock lock;
    size_t cap;
    size_t len;
    eb_sem *sems;
} *sem_list;

static inline void sem_list_free(sem_list l);

/* Creates a new empty list */
static inline sem_list sem_list_alloc(size_t cap) {
    assert(cap > 0);
    
    sem_list result = malloc(sizeof(*result));
        eb_assert_or_recover(result, goto failed);
    
    result->lock = 0;
    result->cap = cap;
    result->len = 0;
    result->sems = malloc(cap * sizeof(*(result->sems)));
        eb_assert_or_recover(result->sems, goto failed);
    
    return result;
    failed: {
        sem_list_free(result);
        return NULL;
    }
}

/* Releases every semaphore in the list, and frees the list itself */
static inline void sem_list_free(sem_list l) {
    /* Intentionally allowing l==NULL */
    if (!l) {
        return;
    }
    
    /* Release each semaphore in our list */
    for (size_t i = 0; i < l->len; i++) {
        eb_sem_release(l->sems[i]);
    }
    
    free(l->sems);
    l->sems = NULL;
    
    free(l);
    l = NULL;
}

/* Add a semaphore to the end of the list, expanding the buffer as necessary */
static inline void sem_list_add(sem_list l, const eb_sem p) {
    assert(l);
    assert(p);
    
    eb_spinlock_lock(&l->lock);
        /* Sanity-check that the list's length is less than its capacity */
        eb_assert_or_bail(l->len <= l->cap, "Sanity check failed");
        
        /* Expand the list's buffer if it's full */
        if (l->len == l->cap) {
            l->cap *= 2;
            // TODO: bad idea to call realloc() inside of a spinlock, but how should we fix it?
            // having our semaphores stored in a statically-sized array would arbitrarily limit us
            // to a certain number of semaphores, and that would suck...
            l->sems = realloc(l->sems, l->cap * sizeof(*(l->sems)));
            // TODO: handle allocation failures better
            eb_assert_or_recover(l->sems, return);
        }
        
        l->sems[l->len] = eb_sem_retain(p);
        l->len++;
    eb_spinlock_unlock(&l->lock);
}

/* Remove the first occurence of 'p' in the list. Returns whether a semaphore was actually removed. */
static inline bool sem_list_rm(sem_list l, eb_sem p) {
    assert(l);
    assert(p);
    
    bool result = false;
    eb_spinlock_lock(&l->lock);
        /* Sanity-check that the list's length is less than its capacity */
        eb_assert_or_bail(l->len <= l->cap, "Sanity-check failed");
        
        /* Search for first occurence of the given semaphore. If we find it, release it and move the last semaphore in the list into the hole. */
        for (size_t i = 0; i < l->len; i++) {
            if (l->sems[i] == p) {
                /* Release the semaphore */
                eb_sem_release(l->sems[i]);
                /* Move the last element in the semaphore list into the now-vacant spot */
                l->sems[i] = l->sems[l->len-1];
                /* Decrement the buffer length */
                l->len--;
                result = true;
                break;
            }
        }
    eb_spinlock_unlock(&l->lock);
    return result;
}

/* Signal the first semaphore in the list that isn't 'ignore' */
// TODO: we may want to randomize the semaphore that we pick out of the list?
static inline void sem_list_signal_first(const sem_list l, eb_sem ignore) {
    assert(l);
    
    eb_sem p = NULL;
    eb_spinlock_lock(&l->lock);
        for (size_t i = 0; i < l->len; i++) {
            if (l->sems[i] != ignore) {
                p = eb_sem_retain(l->sems[i]);
                break;
            }
        }
    eb_spinlock_unlock(&l->lock);
    
    if (p) {
        eb_sem_signal(p);
        eb_sem_release(p);
        p = NULL;
    }
}

enum {
    chanstate_open,
    chanstate_closed,
    chanstate_send,
    chanstate_recv,
    chanstate_ack,
    chanstate_done,
    chanstate_cancelled
}; typedef int32_t chanstate;

struct eb_chan {
    eb_atomic_int retain_count;
    eb_spinlock lock;
    chanstate state;
    
    sem_list sends;
    sem_list recvs;
    
    /* Buffered ivars */
    size_t buf_cap;
    size_t buf_len;
    const void **buf;
    
    /* Unbuffered ivars */
    uintptr_t unbuf_id;
    eb_chan_op *unbuf_op;
    eb_sem unbuf_sem;
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
    
    sem_list_free(c->recvs);
    c->recvs = NULL;
    
    sem_list_free(c->sends);
    c->sends = NULL;
    
    free(c);
    c = NULL;
}

eb_chan eb_chan_create(size_t buf_cap) {
    static const size_t k_init_buf_cap = 16;
    
    eb_chan c = malloc(sizeof(*c));
        eb_assert_or_recover(c, goto failed);
    bzero(c, sizeof(*c));
    
    c->retain_count = 1;
    c->lock = 0;
    c->state = chanstate_open;
    
    c->sends = sem_list_alloc(k_init_buf_cap);
        eb_assert_or_recover(c->sends, goto failed);
    c->recvs = sem_list_alloc(k_init_buf_cap);
        eb_assert_or_recover(c->recvs, goto failed);
    
    if (buf_cap) {
        /* ## Buffered */
        c->buf_cap = buf_cap;
        c->buf_len = 0;
        c->buf = malloc(c->buf_cap * sizeof(*(c->buf)));
            eb_assert_or_recover(c->buf, goto failed);
    } else {
        /* ## Unbuffered */
        c->unbuf_id = 0;
        c->unbuf_op = NULL;
        c->unbuf_sem = NULL;
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
    eb_atomic_int_add(&c->retain_count, 1);
    return c;
}

void eb_chan_release(eb_chan c) {
    assert(c);
    if (eb_atomic_int_add(&c->retain_count, -1) == 1) {
        eb_chan_free(c);
    }
}

#pragma mark - Channel closing -
void eb_chan_close(eb_chan c) {
    assert(c);
    
    bool done = false;
    while (!done) {
        eb_sem wakeup_sem = NULL;
        eb_spinlock_lock(&c->lock);
            chanstate state = c->state;
                eb_assert_or_bail(state != chanstate_closed, "Illegal close of already-closed channel.");
                eb_assert_or_bail(state != chanstate_send && state != chanstate_ack, "Illegal close of channel while send is in progress.");
            
            if (state == chanstate_open) {
                c->state = chanstate_closed;
                done = true;
            } else if (state == chanstate_recv) {
                if (c->unbuf_sem) {
                    wakeup_sem = eb_sem_retain(c->unbuf_sem);
                }
            }
        eb_spinlock_unlock(&c->lock);
        
        /* Wake up the recv */
        if (wakeup_sem) {
            eb_sem_signal(wakeup_sem);
            eb_sem_release(wakeup_sem);
            wakeup_sem = NULL;
        }
    }
    
    /* Wake up the first receiver. We don't bother to wake up senders here because it's illegal for the channel to have senders at this point anyway. */
    sem_list_signal_first(c->recvs, NULL);
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
    op_result_busy,         /* The channel's busy and we should try the op again */
}; typedef unsigned int op_result;

static inline op_result send_buf(uintptr_t id, const eb_chan_op *op, eb_sem sem, eb_nsecs timeout) {
        assert(op);
        assert(op->chan);
    
    eb_chan c = op->chan;
    op_result result = op_result_next;
    
    if (eb_spinlock_try(&c->lock)) {
        /* Verify that the channel is open */
        eb_assert_or_bail(c->state == chanstate_open, "Illegal send on non-open channel");
        
        bool wakeup_recv = false;
        if (c->buf_len < c->buf_cap) {
            /* ## Sending, buffered, channel open, buffer has space */
            /* Notify the channel's recvs if our buffer is going from empty to non-empty */
            wakeup_recv = (!c->buf_len);
            /* Add the value to the buffer */
            c->buf[c->buf_len] = op->val;
            c->buf_len++;
            /* Set our flag signifying that we completed this op. */
            result = op_result_complete;
        }
        
        eb_spinlock_unlock(&c->lock);
        
        if (wakeup_recv) {
            sem_list_signal_first(c->recvs, sem);
        }
    } else {
        result = op_result_busy;
    }
    
    return result;
}

static inline op_result recv_buf(uintptr_t id, eb_chan_op *op, eb_sem sem, eb_nsecs timeout) {
        assert(op);
        assert(op->chan);
    
    eb_chan c = op->chan;
    op_result result = op_result_next;
    
    if (eb_spinlock_try(&c->lock)) {
        chanstate state = c->state;
        /* Sanity-check the channel's state */
        eb_assert_or_bail(state == chanstate_open || state == chanstate_closed, "Invalid channel state");
        
        bool wakeup_send = false;
        if (c->buf_len) {
            /* ## Receiving, buffered, buffer non-empty */
            /* Notify the channel's sends if our buffer is going from full to not-full */
            wakeup_send = (c->buf_len == c->buf_cap);
            /* Set our op's state signifying that we received a value */
            op->open = true;
            op->val = c->buf[0];
            /* Update chan's buffer */
            c->buf_len--;
            memmove(&c->buf[0], &c->buf[1], c->buf_len * sizeof(*(c->buf)));
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
        
        if (wakeup_send) {
            sem_list_signal_first(c->sends, sem);
        }
    } else {
        result = op_result_busy;
    }
    
    return result;
}

static inline op_result send_unbuf(uintptr_t id, eb_chan_op *op, eb_sem sem, eb_nsecs timeout) {
        assert(op);
        assert(op->chan);
    
    eb_chan c = op->chan;
    op_result result = op_result_next;
    
    if (eb_spinlock_try(&c->lock)) {
        chanstate state = c->state;
            /* Verify that the channel isn't closed */
            eb_assert_or_bail(state != chanstate_closed, "Illegal send on closed channel");
        
        bool wakeup_send = false;
        bool wakeup_recv = false;
        if (state == chanstate_open && timeout > 0) {
            c->state = chanstate_send;
            c->unbuf_id = id;
            c->unbuf_op = op;
            c->unbuf_sem = sem;
            wakeup_recv = true;
            result = op_result_next_clean;
        } else if (state == chanstate_send && timeout > 0) {
            if (c->unbuf_op == op) {
                /* We own the send op that's in progress, so assign chan's unbuf_sem */
                /* Verify that the unbuf_id matches our 'id' parameter. If this assertion fails, it means there's likely
                   one eb_chan_op being shared by multiple threads, which isn't allowed. */
                eb_assert_or_bail(c->unbuf_id == id, "Send id invalid");
                /* Assign the semaphore */
                c->unbuf_sem = sem;
                result = op_result_next_clean;
            }
        } else if (state == chanstate_recv) {
            /* Verify that the recv isn't part of the same select() (we can't do unbuffered sends/recvs from the same select()) */
            if (c->unbuf_id != id) {
                    /* Sanity check -- make sure the op is a recv */
                    eb_assert_or_bail(!c->unbuf_op->send, "Op isn't a recv as expected");
                
                c->state = chanstate_ack;
                c->unbuf_op->open = true;
                c->unbuf_op->val = op->val;
                
                eb_sem wakeup_sem = (c->unbuf_sem ? eb_sem_retain(c->unbuf_sem) : NULL);
                eb_spinlock_unlock(&c->lock);
                
                /* Wake up the recv */
                if (wakeup_sem) {
                    eb_sem_signal(wakeup_sem);
                    eb_sem_release(wakeup_sem);
                    wakeup_sem = NULL;
                }
                
                /* Wait until chan transitions to _done or _cancelled. */
                for (;;) {
                    chanstate state = *((volatile chanstate *)&c->state);
                    if (state == chanstate_done || state == chanstate_cancelled) {
                        eb_spinlock_lock(&c->lock);
                            if (c->state == chanstate_done) {
                                result = op_result_complete;
                            }
                            
                            if (c->state == chanstate_done || c->state == chanstate_cancelled) {
                                c->state = chanstate_open;
                                /* Wakeup a send since one of them can now proceed */
                                wakeup_send = true;
                                /* We're intentionally bypassing our loop's unlock because we unlock the channel
                                   outside the encompassing if-statement. */
                                break;
                            }
                        eb_spinlock_unlock(&c->lock);
                    }
                }
            }
        } else if (state == chanstate_ack) {
            if (c->unbuf_op == op) {
                /* A recv acknowledged our send! */
                /* Verify that the unbuf_id matches our 'id' parameter. If this assertion fails, it means there's likely
                   one eb_chan_op being shared by multiple threads, which isn't allowed. */
                eb_assert_or_bail(c->unbuf_id == id, "unbuf_id invalid");
                /* A recv is polling for chan's state to change, so update it to signal that we're done sending! */
                c->state = chanstate_done;
                result = op_result_complete;
            }
        }
        
        eb_spinlock_unlock(&c->lock);
        
        if (wakeup_send) {
            sem_list_signal_first(c->sends, sem);
        }
        
        if (wakeup_recv) {
            sem_list_signal_first(c->recvs, sem);
        }
    } else {
        result = op_result_busy;
    }
    
    return result;
}

static inline op_result recv_unbuf(uintptr_t id, eb_chan_op *op, eb_sem sem, eb_nsecs timeout) {
        assert(op);
        assert(op->chan);
    
    eb_chan c = op->chan;
    op_result result = op_result_next;
    
    if (eb_spinlock_try(&c->lock)) {
        chanstate state = c->state;
        bool wakeup_send = false;
        if (state == chanstate_open && timeout > 0) {
            c->state = chanstate_recv;
            c->unbuf_id = id;
            c->unbuf_op = op;
            c->unbuf_sem = sem;
            wakeup_send = true;
            result = op_result_next_clean;
        } else if (state == chanstate_closed) {
            /* Set our op's state signifying that we're returning the value due to a closed channel */
            op->open = false;
            op->val = NULL;
            /* We completed this op so set our return value! */
            result = op_result_complete;
        } else if (state == chanstate_send) {
            /* Verify that the send isn't part of the same select() (we can't do unbuffered sends/recvs from the same select()) */
            if (c->unbuf_id != id) {
                    /* Sanity check -- make sure the op is a send */
                    eb_assert_or_bail(c->unbuf_op->send, "Op isn't a send as expected");
                
                c->state = chanstate_ack;
                op->open = true;
                op->val = c->unbuf_op->val;
                
                eb_sem wakeup_sem = (c->unbuf_sem ? eb_sem_retain(c->unbuf_sem) : NULL);
                eb_spinlock_unlock(&c->lock);
                
                /* Wake up the send */
                if (wakeup_sem) {
                    eb_sem_signal(wakeup_sem);
                    eb_sem_release(wakeup_sem);
                    wakeup_sem = NULL;
                }
                
                /* Wait until chan transitions to _done or _cancelled. */
                for (;;) {
                    chanstate state = *((volatile chanstate *)&c->state);
                    if (state == chanstate_done || state == chanstate_cancelled) {
                        eb_spinlock_lock(&c->lock);
                            if (c->state == chanstate_done) {
                                result = op_result_complete;
                            }
                            
                            if (c->state == chanstate_done || c->state == chanstate_cancelled) {
                                c->state = chanstate_open;
                                /* Wakeup a send since one of them can now proceed */
                                wakeup_send = true;
                                /* We're intentionally bypassing our loop's unlock because we unlock the channel
                                   outside the encompassing if-statement. */
                                break;
                            }
                        eb_spinlock_unlock(&c->lock);
                    }
                }
            }
        } else if (state == chanstate_recv && timeout > 0) {
            if (c->unbuf_op == op) {
                /* We own the recv op that's in progress, so assign chan's unbuf_sem */
                /* Verify that the _recv_id matches our 'id' parameter. If this assertion fails, it means there's likely
                   one eb_chan_op being shared by multiple threads, which isn't allowed. */
                eb_assert_or_bail(c->unbuf_id == id, "unbuf_id invalid");
                /* Assign the semaphore */
                c->unbuf_sem = sem;
                result = op_result_next_clean;
            }
        } else if (state == chanstate_ack) {
            if (c->unbuf_op == op) {
                /* A send acknowledged our recv! */
                /* Verify that the unbuf_id matches our 'id' parameter. If this assertion fails, it means there's likely
                   one eb_chan_op being shared by multiple threads, which isn't allowed. */
                eb_assert_or_bail(c->unbuf_id == id, "unbuf_id invalid");
                /* A send is polling for chan's state to change, so update it to signal that we're done sending! */
                c->state = chanstate_done;
                result = op_result_complete;
            }
        }
        
        eb_spinlock_unlock(&c->lock);
        
        if (wakeup_send) {
            sem_list_signal_first(c->sends, sem);
        }
    } else {
        result = op_result_busy;
    }
    
    return result;
}

static inline void cleanup_op(const eb_chan_op *op, eb_sem sem) {
        assert(op);
    eb_chan c = op->chan;
    if (c && !c->buf_cap) {
        bool wakeup_send = false;
        bool wakeup_recv = false;
        eb_spinlock_lock(&c->lock);
            chanstate state = c->state;
            if (state == chanstate_send) {
                if (c->unbuf_op == op) {
                    /* 'op' was in the process of an unbuffered send on the channel, but no recv had arrived
                       yet, so reset state to _open. */
                    c->state = chanstate_open;
                    wakeup_send = true;
                }
            } else if (state == chanstate_recv) {
                if (c->unbuf_op == op) {
                    /* 'op' was in the process of an unbuffered recv on the channel, but no send had arrived
                       yet, so reset state to _open. */
                    c->state = chanstate_open;
                    wakeup_recv = true;
                }
            } else if (state == chanstate_ack) {
                if (c->unbuf_op == op) {
                    /* A counterpart acknowledged 'op' but, but 'op' isn't the one that completed in our select() call, so we're cancelling. */
                    c->state = chanstate_cancelled;
                }
            }
        eb_spinlock_unlock(&c->lock);
        
        if (wakeup_send) {
            sem_list_signal_first(c->sends, sem);
        }
        
        if (wakeup_recv) {
            sem_list_signal_first(c->recvs, sem);
        }
    }
}

static inline op_result try_op(uintptr_t id, eb_chan_op *op, eb_sem sem, eb_nsecs timeout) {
        assert(op);
    eb_chan c = op->chan;
    if (c) {
        if (op->send) {
            /* ## Send */
            return (c->buf_cap ? send_buf(id, op, sem, timeout) : send_unbuf(id, op, sem, timeout));
        } else {
            /* ## Receive */
            return (c->buf_cap ? recv_buf(id, op, sem, timeout) : recv_unbuf(id, op, sem, timeout));
        }
    }
    return op_result_next;
}

eb_chan_op *eb_chan_do(eb_chan_op *const ops[], size_t nops, eb_nsecs timeout) {
    // TODO: make timeout work!
    // TODO: randomize iteration by shuffling input array once (upon entry)
        assert(ops);
    eb_sem sem = NULL;
    eb_chan_op *result = NULL;
    uintptr_t id = (uintptr_t)&result;
    bool cleanup_ops[nops];
    bzero(cleanup_ops, sizeof(cleanup_ops));
    
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
    
    eb_time start_time = (timeout != eb_nsecs_zero && timeout != eb_nsecs_forever ? eb_time_now() : 0);
    for (;;) {
        /* ## Fast path: loop over our operations to see if one of them was able to send/receive. (If not,
           we'll enter the slow path where we put our thread to sleep until we're signalled.) */
        if (nops) {
            // TODO: this won't actually work for the poll case because we allow our ops to return _busy. If, for example, they all returned _busy in the first iteration, we'll wind up returning NULL when one of the ops may have been complete-able!
            const size_t k_attempt_multiplier = (timeout != eb_nsecs_zero ? 50 : 1);
            for (size_t i = 0; i < k_attempt_multiplier * nops; i++) {
                size_t idx = (i % nops);
                eb_chan_op *op = ops[idx];
                op_result r = try_op(id, op, sem, timeout);
                
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
        
        /* If we're polling and we get to this point, no operations could complete, so it's time to return. */
        if (timeout == eb_nsecs_zero) {
            goto cleanup;
        }
        
        /* ## Slow path: we weren't able to find an operation that could send/receive, so we'll create a
           semaphore to receive notifications on and put this thread to sleep until someone wakes us up. */
        if (!sem) {
            /* Create our semaphore that we'll attach to channels so that we can be notified when events occur. */
            sem = eb_sem_create();
                eb_assert_or_recover(sem, goto cleanup);
            
            /* Register our semaphore for the appropriate notifications on every channel. */
            /* This adds 'sem' to the channel's sends/recvs (depending on the op), which we clean up at the
               end of this function. */
            for (size_t i = 0; i < nops; i++) {
                eb_chan_op *op = ops[i];
                eb_chan c = op->chan;
                if (c) {
                    sem_list_add((op->send ? c->sends : c->recvs), sem);
                }
            }
        }
        
        /* Before we go to sleep, call try_op() for every op until we get a non-busy return value. This way we'll ensure
           that no op is actually able to be performed, and we'll also ensure that 'sem' is registered as the 'unbuf_sem'
           for the necessary channels. */
        for (size_t i = 0; i < nops; i++) {
            eb_chan_op *op = ops[i];
            op_result r;
            while ((r = try_op(id, op, sem, timeout)) == op_result_busy);
            
            /* Update cleanup_ops. Note that we don't need to consider _busy because we waited until
               the operation returned something other than _busy. */
            cleanup_ops[i] = (r == op_result_next_clean);
            
            /* If the op completed, we need to exit! */
            if (r == op_result_complete) {
                result = op;
                goto cleanup;
            }
        }
        
        /* If we have a timeout, determine how much time has elapsed, because we may have timed-out. */
        eb_nsecs elapsed = 0;
        if (timeout != eb_nsecs_zero && timeout != eb_nsecs_forever) {
            elapsed = eb_time_nsecs_between(start_time, eb_time_now());
            /* Check if we timed-out */
            if (elapsed >= timeout) {
                goto cleanup;
            }
        }
        
        /* Put our thread to sleep until someone alerts us of an event */
        eb_sem_wait(sem, timeout - elapsed);
    }
    
    /* Cleanup! */
    cleanup: {
        if (sem) {
            for (size_t i = 0; i < nops; i++) {
                eb_chan_op *op = ops[i];
                eb_chan c = op->chan;
                if (c) {
                    sem_list sems = (op->send ? c->sends : c->recvs);
                    sem_list_rm(sems, sem);
                    sem_list_signal_first(sems, sem);
                }
            }
        }
        
        for (size_t i = 0; i < nops; i++) {
            if (cleanup_ops[i]) {
                cleanup_op(ops[i], sem);
            }
        }
        
        if (sem) {
            eb_sem_release(sem);
            sem = NULL;
        }
    }
    
    return result;
}
