// DONE

// Torture test for goroutines.
// Make a lot of goroutines, threaded together, and tear them down cleanly.

#include "testglue.h"

void f(eb_chan left, eb_chan right) {
    const void *val;
    assert(eb_chan_recv(right, &val) == eb_chan_res_ok);
    eb_chan_send(left, val);
}

int main() {
	const int n = 2040;
	eb_chan leftmost = eb_chan_create(0);
	eb_chan right = leftmost;
	eb_chan left = leftmost;
	for (int i = 0; i < n; i++) {
		right = eb_chan_create(0);
		go( f(left, right) );
		left = right;
	}
    
    go (eb_chan_send(right, (void*)1) );
    eb_chan_recv(leftmost, NULL);
    
    return 0;
}