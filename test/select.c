// DONE

// Test simple select.

#include "testglue.h"

unsigned int counter = 0;
unsigned int shift = 0;

unsigned int GetValue() {
	counter++;
	return 1 << shift;
}

int Send(eb_chan a, eb_chan b) {
	int i = 0;
	for (;;) {
        eb_chan_op asend = eb_chan_op_send(a, (void*)(uintptr_t)GetValue());
        eb_chan_op bsend = eb_chan_op_send(b, (void*)(uintptr_t)GetValue());
        
        eb_chan_op *r = eb_chan_select(eb_nsec_zero, &asend, &bsend);
        if (r == &asend) {
            i++;
            a = NULL;
        } else if (r == &bsend) {
            i++;
            b = NULL;
        } else {
            return i;
        }
        
		shift++;
	}
    // unreachable
    abort();
}

int main() {
	eb_chan a = eb_chan_create(1);
	eb_chan b = eb_chan_create(1);
    
    assert(Send(a, b) == 2);
    
    const void *av, *bv;
    assert(eb_chan_recv(a, &av) == eb_chan_res_ok);
    assert(eb_chan_recv(b, &bv) == eb_chan_res_ok);
    assert(((uintptr_t)av | (uintptr_t)bv) == 3);
    
    assert(Send(a, NULL) == 1);
    
    assert(counter == 10);
    return 0;
}