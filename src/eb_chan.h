#pragma once
#include <stddef.h>
#include <stdbool.h>
#include <assert.h>
#include "eb_time.h"

/* ## Types */
typedef struct eb_chan *eb_chan;
typedef struct {
    eb_chan chan;       /* The applicable channel, where NULL channels block forever */
    bool send;          /* True if sending, false if receiving */
    bool open;          /* True if the op completed due to a successful send/recv operation, false if the op completed because the channel is closed. */
    const void *val;    /* The value to be sent or the value that was received */
} eb_chan_op;

typedef enum {
    eb_chan_ret_ok,         /* Success */
    eb_chan_ret_closed,     /* Failed because the channel is closed */
    eb_chan_ret_stalled,    /* Failed because the send/recv couldn't proceed without blocking (applies to _try_send()/_try_recv()) */
} eb_chan_ret;

/* ## Channel creation/lifecycle */
eb_chan eb_chan_create(size_t buf_cap);
eb_chan eb_chan_retain(eb_chan c);
void eb_chan_release(eb_chan c);

/* ## Channel closing */
/* Returns _ok on success, or _closed if the channel was already closed. */
eb_chan_ret eb_chan_close(eb_chan c);

/* ## Getters */
size_t eb_chan_buf_cap(eb_chan c);
size_t eb_chan_buf_len(eb_chan c);

/* ## Sending/receiving */
/* Send/receive a value on a channel (where _send()/_recv() are blocking and _try_send()/_try_recv() are non-blocking) */
eb_chan_ret eb_chan_send(eb_chan c, const void *val);
eb_chan_ret eb_chan_try_send(eb_chan c, const void *val);
eb_chan_ret eb_chan_recv(eb_chan c, const void **val);
eb_chan_ret eb_chan_try_recv(eb_chan c, const void **val);

/* ## Multiplexing */
eb_chan_op *eb_chan_select_list(eb_nsec timeout, eb_chan_op *const ops[], size_t nops);

/* _select() is a convenience macro to allow supplying ops directly to _select() without having to create an array on the stack.
   For example:
        eb_chan_op op1 = eb_chan_op_send(c1, NULL);
        eb_chan_op op2 = eb_chan_op_recv(c2);
        eb_chan_op *result = eb_chan_select(timeout, &op1, &op2);
        ...
*/
#define eb_chan_select(timeout, ...) ({                                                                             \
    eb_chan_op *const eb_chan_select_ops[] = {__VA_ARGS__};                                                         \
    eb_chan_select_list(timeout, eb_chan_select_ops, (sizeof(eb_chan_select_ops) / sizeof(*eb_chan_select_ops)));   \
})

/* Return initialized send/recv ops for use with _select() */
static inline eb_chan_op eb_chan_op_send(eb_chan c, const void *val) {
    return (eb_chan_op){.chan = c, .send = true, .open = false, .val = val};
}

static inline eb_chan_op eb_chan_op_recv(eb_chan c) {
    return (eb_chan_op){.chan = c, .send = false, .open = false, .val = NULL};
}
