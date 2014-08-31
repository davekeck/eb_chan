#include <stddef.h>
#include <stdbool.h>
#include "eb_timeout.h"

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
size_t eb_chan_get_buf_cap(eb_chan c);
size_t eb_chan_get_buf_len(eb_chan c);

/* ## Performing operations */
eb_chan_op eb_chan_send_op(eb_chan c, const void *val);
eb_chan_op eb_chan_recv_op(eb_chan c);
eb_chan_op *eb_chan_do(eb_chan_op *const ops[], size_t nops, eb_timeout timeout);
