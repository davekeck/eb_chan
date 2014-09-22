// DONE

// Test for select: Issue 2075
// A bug in select corrupts channel queues of failed cases
// if there are multiple waiters on those channels and the
// select is the last in the queue. If further waits are made
// on the channel without draining it first then those waiters
// will never wake up. In the code below c1 is such a channel.

#include "testglue.h"

int main() {
	eb_chan c1 = eb_chan_create(0);
	eb_chan c2 = eb_chan_create(0);
	eb_chan c3 = eb_chan_create(0);
    
    go( eb_chan_recv(c1, NULL) );
    
    go(
        eb_chan_op c1recv = eb_chan_op_recv(c1);
        eb_chan_op c2recv = eb_chan_op_recv(c2);
        eb_chan_op *r = eb_chan_do(eb_nsec_forever, &c1recv, &c2recv);
        if (r == &c1recv) {
            abort();
        } else if (r == &c2recv) {
            eb_chan_send(c3, (void*)true);
        } else {
            abort();
        }
        
        assert(eb_chan_recv(c1, NULL));
    );
    
    go( eb_chan_send(c2, (void*)true) );
    assert(eb_chan_recv(c3, NULL));
    
    eb_chan_send(c1, (void*)true);
    eb_chan_send(c1, (void*)true);
    return 0;
}
