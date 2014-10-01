#ifndef EB_CHAN_H
#define EB_CHAN_H

#include <stddef.h>
#include <stdbool.h>
#include "eb_nsec.h"

/* ## Types */
typedef enum {
    eb_chan_res_ok,         /* Success */
    eb_chan_res_closed,     /* Failed because the channel is closed */
    eb_chan_res_stalled,    /* Failed because the send/recv couldn't proceed without blocking (applies to _try_send()/_try_recv()) */
} eb_chan_res;

typedef struct eb_chan *eb_chan;
typedef struct {
    eb_chan chan;       /* The applicable channel, where NULL channels block forever */
    bool send;          /* True if sending, false if receiving */
    eb_chan_res res;    /* _ok if the op completed due to a successful send/recv operation, _closed if the op completed because the channel is closed. */
    const void *val;    /* The value to be sent/the value that was received */
} eb_chan_op;

/* ## Channel creation/lifecycle */
eb_chan eb_chan_create(size_t buf_cap);
eb_chan eb_chan_retain(eb_chan c);
void eb_chan_release(eb_chan c);

/* ## Channel closing */
/* Returns _ok on success, or _closed if the channel was already closed. */
eb_chan_res eb_chan_close(eb_chan c);

/* ## Getters */
size_t eb_chan_buf_cap(eb_chan c);
size_t eb_chan_buf_len(eb_chan c);

/* ## Sending/receiving */
/* Send/receive a value on a channel (where _send()/_recv() are blocking and _try_send()/_try_recv() are non-blocking) */
eb_chan_res eb_chan_send(eb_chan c, const void *val);
eb_chan_res eb_chan_try_send(eb_chan c, const void *val);
eb_chan_res eb_chan_recv(eb_chan c, const void **val);
eb_chan_res eb_chan_try_recv(eb_chan c, const void **val);

/* ## Multiplexing */
/* _select_list() performs at most one of the operations in the supplied list, and returns the one that was performed.
   It returns NULL if no operation was performed before the timeout. */
eb_chan_op *eb_chan_select_list(eb_nsec timeout, eb_chan_op *const ops[], size_t nops);

/* _select() is a convenience macro that wraps _select_list() to avoid having to manually create an array of ops on the stack.
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
    return (eb_chan_op){.chan = c, .send = true, .res = eb_chan_res_closed, .val = val};
}

static inline eb_chan_op eb_chan_op_recv(eb_chan c) {
    return (eb_chan_op){.chan = c, .send = false, .res = eb_chan_res_closed, .val = NULL};
}

#endif /* EB_CHAN_H */
