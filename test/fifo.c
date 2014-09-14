// DONE

#include "testglue.h"

#define N 10

void AsynchFifo() {
    eb_chan ch = eb_chan_create(N);
	for (size_t i = 0; i < N; i++) {
        eb_chan_send(ch, (const void *)i);
	}
	for (size_t i = 0; i < N; i++) {
        const void *val;
        eb_chan_recv(ch, &val);
        assert((size_t)val == i);
	}
    
    printf("AsynchFifo returned\n");
}


void Chain(eb_chan ch, int val, eb_chan in, eb_chan out) {
    eb_chan_recv(in, NULL);
    
    const void *recvd_val;
    eb_chan_recv(ch, &recvd_val);
    assert((int)(intptr_t)recvd_val == val);
    
    eb_chan_send(out, (const void *)1);
    printf("Chain returned\n");
}

// thread together a daisy chain to read the elements in sequence
void SynchFifo() {
	eb_chan ch = eb_chan_create(0);
	eb_chan in = eb_chan_create(0);
	eb_chan start = in;
	for (size_t i = 0; i < N; i++) {
		eb_chan out = eb_chan_create(0);
        go(^{ Chain(ch, (int)i, in, out); });
		in = out;
	}
    
    eb_chan_send(start, (const void *)0);
	for (size_t i = 0; i < N; i++) {
        eb_chan_send(ch, (const void *)i);
	}
    eb_chan_recv(in, NULL);
    printf("SynchFifo returned\n");
}

int main() {
	AsynchFifo();
	SynchFifo();
    exit(0);
}