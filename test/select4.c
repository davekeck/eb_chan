// DONE

// Test that a select statement proceeds when a value is ready.

#include "testglue.h"

int main() {
	eb_chan c = eb_chan_create(1);
	eb_chan c1 = eb_chan_create(0);
    
    eb_chan_send(c, (void*)42);
    
    eb_chan_op c1recv = eb_chan_op_recv(c1);
    eb_chan_op crecv = eb_chan_op_recv(c);
    
    eb_chan_op *r = eb_chan_select(eb_nsec_forever, &c1recv, &crecv);
    if (r == &crecv) {
        assert(crecv.val == (void*)42);
    } else {
        abort();
    }
    
    return 0;
}