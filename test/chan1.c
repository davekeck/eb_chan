// DONE

// Test communication with multiple simultaneous goroutines.

#include "testglue.h"

#define N 1000 // sent messages
#define M 10   // receiving goroutines
#define W 2    // channel buffering
int h[N];      // marking of send/recv

void r(eb_chan c, int m) {
	for (;;) {
        const void *val;
        eb_chan_recv(c, &val);
        int r = (int)(intptr_t)val;
        if (h[r] != 1) {
            printf("r,"
                "m=%d,"
                "r=%d,"
                "h=%d,", m, r, h[r]);
            abort();
        }
        h[r] = 2;
	}
}

void s(eb_chan c) {
	for (int n = 0; n < N; n++) {
		int r = n;
		if (h[r] != 0) {
			printf("s\n");
            abort();
		}
		h[r] = 1;
        eb_chan_send(c, (void*)(intptr_t)r);
	}
}

int main() {
	eb_chan c = eb_chan_create(W);
	for (int m = 0; m < M; m++) {
		go( r(c, m) );
		usleep(100);
	}
    
    usleep(100);
    usleep(100);
	s(c);
    return 0;
}