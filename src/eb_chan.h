#pragma once
#include <stddef.h>
#include <stdbool.h>
#include <assert.h>
#include "eb_time.h"

/* ## Types */
typedef struct eb_chan *eb_chan;
typedef struct {
    eb_chan chan;       /* The applicable channel, where NULL channels block forever */
    bool send;          /* true if sending, false if receiving */
    bool open;          /* Valid only when receiving; true if the recv op completed due to a successful send operation, false if due to a closed channel. */
    const void *val;    /* When sending: the value to send; when receiving and 'open' is true: the value that was received.  */
} eb_chan_op;

/* ## Channel creation/lifecycle */
eb_chan eb_chan_create(size_t buf_cap);
eb_chan eb_chan_retain(eb_chan c);
void eb_chan_release(eb_chan c);

/* ## Channel closing */
void eb_chan_close(eb_chan c);

/* ## Getters */
size_t eb_chan_buf_cap(eb_chan c);
size_t eb_chan_buf_len(eb_chan c);

/* ## Performing operations */
eb_chan_op *eb_chan_do_list(eb_nsec timeout, eb_chan_op *const ops[], size_t nops);

/* ## Convenience functions */
#define eb_chan_do(timeout, ...) ({                                                                 \
    eb_chan_op *const eb_chan_do_ops[] = {__VA_ARGS__};                                             \
    eb_chan_do_list(timeout, eb_chan_do_ops, (sizeof(eb_chan_do_ops) / sizeof(*eb_chan_do_ops)));   \
})

/* Returns an initialized send op */
static inline eb_chan_op eb_chan_op_send(eb_chan c, const void *val) {
    return (eb_chan_op){.chan = c, .send = true, .open = false, .val = val};
}

/* Returns an initialized recv op */
static inline eb_chan_op eb_chan_op_recv(eb_chan c) {
    return (eb_chan_op){.chan = c, .send = false, .open = false, .val = NULL};
}

/* Sends a value on a channel, blocking until it can do so. */
static inline void eb_chan_send(eb_chan c, const void *val) {
    eb_chan_op op = eb_chan_op_send(c, val);
    assert(eb_chan_do(eb_nsec_forever, &op));
}

/* Sends a value on a channel and returns true, or returns false if the value couldn't be sent without blocking. */
static inline bool eb_chan_try_send(eb_chan c, const void *val) {
    eb_chan_op op = eb_chan_op_send(c, val);
    return (eb_chan_do(eb_nsec_zero, &op) != NULL);
}

/* Receives a value from a channel, blocking until it can do so.
   Returns true if the value was generated due to a successful send, or false if the channel is closed.
   'val' is only valid if true is returned. */
static inline bool eb_chan_recv(eb_chan c, const void **val) {
    eb_chan_op op = eb_chan_op_recv(c);
    assert(eb_chan_do(eb_nsec_forever, &op));
    if (val) {
        *val = op.val;
    }
    return op.open;
}

/* Receives a value from a channel and returns true, or returns false if no value could be received without blocking.
   'open' is set to true if the value was generated due to a successful send, or false if the channel is closed.
   'val' is only valid if 'open' is true. */
static inline bool eb_chan_try_recv(eb_chan c, bool *open, const void **val) {
    eb_chan_op op = eb_chan_op_recv(c);
    bool result = (eb_chan_do(eb_nsec_zero, &op) != NULL);
    if (result) {
        if (open) {
            *open = op.open;
        }
        if (val) {
            *val = op.val;
        }
    }
    return result;
}
