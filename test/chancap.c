// DONE

// Test the cap predeclared function applied to channels.

#include "testglue.h"

int main() {
	eb_chan c = eb_chan_create(10);
    assert(eb_chan_buf_len(c) == 0 && eb_chan_buf_cap(c) == 10);

	for (int i = 0; i < 3; i++) {
        eb_chan_send(c, (void*)(intptr_t)i);
	}
    
    assert(eb_chan_buf_len(c) == 3 && eb_chan_buf_cap(c) == 10);

	c = eb_chan_create(0);
    assert(eb_chan_buf_len(c) == 0 && eb_chan_buf_cap(c) == 0);
    return 0;
}