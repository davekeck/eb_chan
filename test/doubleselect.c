// DONE

// Test the situation in which two cases of a select can
// both end up running. See http://codereview.appspot.com/180068.

#include "testglue.h"

#define iterations 1000

// sender sends a counter to one of four different channels. If two
// cases both end up running in the same iteration, the same value will be sent
// to two different channels.
void sender(eb_chan c1, eb_chan c2, eb_chan c3, eb_chan c4) {
	for (int i = 0; i < iterations; i++) {
        eb_chan_op o1 = eb_chan_op_send(c1, (void*)(intptr_t)i);
        eb_chan_op o2 = eb_chan_op_send(c2, (void*)(intptr_t)i);
        eb_chan_op o3 = eb_chan_op_send(c3, (void*)(intptr_t)i);
        eb_chan_op o4 = eb_chan_op_send(c4, (void*)(intptr_t)i);
        assert(eb_chan_do(eb_nsec_forever, &o1, &o2, &o3, &o4));
	}
    
    eb_chan_close(c4);
    eb_chan_close(c3);
    eb_chan_close(c2);
    eb_chan_close(c1);
}

// mux receives the values from sender and forwards them onto another channel.
// It would be simpler to just have sender's four cases all be the same
// channel, but this doesn't actually trigger the bug.
void mux(eb_chan out, eb_chan in, eb_chan done) {
    for (const void *v; eb_chan_recv(in, &v);) {
        eb_chan_send(out, v);
    }
    
    eb_chan_send(done, (void*)true);
}

// recver gets a steam of values from the four mux's and checks for duplicates.
void recver(eb_chan in) {
    bool *seen = calloc(iterations, sizeof(*seen));
    
    for (const void *v; eb_chan_recv(in, &v);) {
        assert(!seen[(uintptr_t)v]);
        seen[(uintptr_t)v] = true;
    }
}

void waitForDone(eb_chan done, eb_chan cmux) {
    eb_chan_recv(done, NULL);
    eb_chan_recv(done, NULL);
    eb_chan_recv(done, NULL);
    eb_chan_recv(done, NULL);
    eb_chan_close(cmux);
}

int main() {
	eb_chan c1 = eb_chan_create(0);
	eb_chan c2 = eb_chan_create(0);
	eb_chan c3 = eb_chan_create(0);
	eb_chan c4 = eb_chan_create(0);
	eb_chan done = eb_chan_create(0);
	eb_chan cmux = eb_chan_create(0);
	go( sender(c1, c2, c3, c4) );
	go( mux(cmux, c1, done) );
	go( mux(cmux, c2, done) );
	go( mux(cmux, c3, done) );
	go( mux(cmux, c4, done) );
	go( eb_chan_recv(done, NULL);
        eb_chan_recv(done, NULL);
        eb_chan_recv(done, NULL);
        eb_chan_recv(done, NULL);
        eb_chan_close(cmux); );
    
	// We keep the recver because it might catch more bugs in the future.
	// However, the result of the bug linked to at the top is that we'll
	// end up panicking with: "throw: bad g->status in ready".
	recver(cmux);
    return 0;
}
