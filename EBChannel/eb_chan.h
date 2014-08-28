#include <stddef.h>
#include <stdbool.h>

/* ## Types */
typedef struct eb_chan *eb_chan_t;
typedef struct {
    eb_chan_t chan;     /* The applicable channel, where NULL channels block forever */
    bool send;          /* true if sending, false if receiving */
    bool open;          /* Valid only when receiving; true if the recv op completed due to a successful send operation, false if due to a closed channel. */
    const void *val;    /* When sending: the value to send; when receiving and 'open' is true: the value that was received.  */
} eb_chan_op_t;

/* ## Channel creation/lifecycle */
eb_chan_t eb_chan_create(size_t buf_cap);
eb_chan_t eb_chan_retain(eb_chan_t c);
void eb_chan_release(eb_chan_t c);

/* ## Close a channel */
void eb_chan_close(eb_chan_t c);

/* ## Getters */
size_t eb_chan_get_buf_cap(eb_chan_t c);
size_t eb_chan_get_buf_len(eb_chan_t c);

/* ## Op-making functions */
eb_chan_op_t eb_chan_send(eb_chan_t c, const void *val);
eb_chan_op_t eb_chan_recv(eb_chan_t c);

/* ## Performing operations on a channel */
/* Together, _do() and _try() implement Go's select() functionality (depending on whether there's a 'default' case) */
/* These functions return the index of the operation that completed, or SIZE_MAX if none completed (only in the case of _try()) */ 
size_t eb_chan_do(eb_chan_op_t *const ops[], size_t nops);
size_t eb_chan_try(eb_chan_op_t *const ops[], size_t nops);
