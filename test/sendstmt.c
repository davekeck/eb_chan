// DONE

// Test various parsing cases that are a little
// different now that send is a statement, not a expression.

#include "testglue.h"

void chanchan() {
	eb_chan cc = eb_chan_create(1);
	eb_chan c = eb_chan_create(1);
    eb_chan_send(cc, (void*)c);
    
    const void *val;
    assert(eb_chan_recv(cc, &val) == eb_chan_ret_ok);
    
    assert(eb_chan_try_send((eb_chan)val, (void*)2) == eb_chan_ret_ok);
    
    const void *val2;
    assert(eb_chan_recv(c, &val2) == eb_chan_ret_ok);
    assert((intptr_t)val2 == 2);
}

void sendprec() {
	eb_chan c = eb_chan_create(1);
    
    eb_chan_send(c, (void*)(false || true));
    
    const void *val;
    assert(eb_chan_recv(c, &val) == eb_chan_ret_ok);
    assert(val);
}

int main() {
	chanchan();
	sendprec();
}