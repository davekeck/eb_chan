// DONE

// Test concurrency primitives: prime sieve of Eratosthenes.

// Generate primes up to 100 using channels, checking the results.
// This sieve is Eratosthenesque and only considers odd candidates.
// See discussion at <http://blog.onideas.ws/eratosthenes.go>.

#include "testglue.h"

// Return a chan of odd numbers, starting from 5.
eb_chan odds() {
	eb_chan out = eb_chan_create(50);
	go(
		int n = 5;
		for (;;) {
            eb_chan_send(out, (void*)(intptr_t)n);
			n += 2;
		}
	);
	return out;
}

// Return a chan of odd multiples of the prime number p, starting from p*p.
eb_chan multiples(int p) {
	eb_chan out = eb_chan_create(10);
	go(
		int n = p*p;
		for (;;) {
            eb_chan_send(out, (void*)(intptr_t)n);
			n += 2*p;
		}
	);
	return out;
}

typedef struct {
    int head;
    eb_chan ch;
} PeekCh;

#define BUF_SIZE 256
// Heap of PeekCh, sorting by head values, satisfies Heap interface.
typedef struct {
    size_t len;
    PeekCh *array[BUF_SIZE];
} PeekChHeap;

PeekCh *Pop(PeekChHeap *h) {
    assert(h->len > 0);
    
    /* Find the index with the least 'head' value, which is the one that we'll return */
    size_t idx = SIZE_MAX;
    for (size_t i = 0; i < h->len; i++) {
        if (idx == SIZE_MAX || h->array[i]->head < h->array[idx]->head) {
            idx = i;
        }
    }
    assert(idx != SIZE_MAX);
    PeekCh *result = h->array[idx];
    
    /* Replace the now-vacant hole with the last value in the array, and decrease the length */
    h->array[idx] = h->array[h->len-1];
    h->len--;
    
    return result;
}

void Push(PeekChHeap *h, PeekCh *v) {
    assert(h->len < BUF_SIZE); // make sure we're not going to overflow the buffer
    h->array[h->len] = v;
    h->len++;
}

// Return a channel to serve as a sending proxy to 'out'.
// Use a goroutine to receive values from 'out' and store them
// in an expanding buffer, so that sending to 'out' never blocks.
eb_chan sendproxy(eb_chan out) {
	eb_chan proxy = eb_chan_create(10);
	go(
        int buf[BUF_SIZE];
        size_t buf_idx = 0;
        size_t buf_len = 0;
        
		for (;;) {
			eb_chan c = out;
            int e = 0;
			if (!buf_len) {
				// buffer empty: disable output
				c = NULL;
			} else {
                e = buf[buf_idx];
			}
            
            eb_chan_op proxy_recv = eb_chan_recv_op(proxy);
            eb_chan_op c_send = eb_chan_send_op(c, (void*)(intptr_t)e);
            eb_chan_op *r = eb_chan_do(eb_nsec_forever, &proxy_recv, &c_send);
            if (r == &proxy_recv) {
                assert(proxy_recv.open); // make sure that the channel's open
                assert(buf_len+buf_idx < BUF_SIZE); // make sure we're not going to overflow our buffer
                buf[buf_idx+buf_len] = (int)(intptr_t)proxy_recv.val;
                buf_len++;
            } else if (r == &c_send) {
                buf_idx++;
                buf_len--;
                if (!buf_len) {
                    buf_idx = 0;
                }
            } else {
                // unreachable
                abort();
            }
		}
	);
	return proxy;
}

// Return a chan int of primes.
eb_chan Sieve() {
	// The output values.
	eb_chan out = eb_chan_create(10);
    eb_chan_send(out, (void*)2);
    eb_chan_send(out, (void*)3);

	// The channel of all composites to be eliminated in increasing order.
    eb_chan composites = eb_chan_create(50);

	// The feedback loop.
	eb_chan primes = eb_chan_create(10);
    eb_chan_send(primes, (void*)3);

	// Merge channels of multiples of 'primes' into 'composites'.
	go(
		PeekChHeap h = {.len = 0};
		int min = 15;
		for (;;) {
            const void *val;
            assert(eb_chan_recv(primes, &val));
            
            eb_chan m = multiples((int)(intptr_t)val);
            
            const void *head;
            assert(eb_chan_recv(m, &head));
            
			while (min < (intptr_t)head) {
                eb_chan_send(composites, (void*)(intptr_t)min);
				PeekCh *minchan = Pop(&h);
				min = minchan->head;
                
                const void *v;
                assert(eb_chan_recv(minchan->ch, &v));
                minchan->head = (int)(intptr_t)v;
				Push(&h, minchan);
			}
            
			while (min == (intptr_t)head) {
				PeekCh *minchan = Pop(&h);
				min = minchan->head;
                
                const void *v;
                assert(eb_chan_recv(minchan->ch, &v));
                minchan->head = (int)(intptr_t)v;
				Push(&h, minchan);
			}
            
            eb_chan_send(composites, head);
            
            const void *v;
            assert(eb_chan_recv(m, &v));
            
            PeekCh *p = malloc(sizeof(*p));
            p->head = (int)(intptr_t)v;
            p->ch = m;
			Push(&h, p);
		}
	);

	// Sieve out 'composites' from 'candidates'.
	go(
		// In order to generate the nth prime we only need multiples of
		// primes ≤ sqrt(nth prime).  Thus, the merging goroutine will
		// receive from 'primes' much slower than this goroutine
		// will send to it, making the buffer accumulate and block this
		// goroutine from sending, causing a deadlock.  The solution is to
		// use a proxy goroutine to do automatic buffering.
		eb_chan primesproxy = sendproxy(primes);

		eb_chan candidates = odds();
        
        const void *p;
        assert(eb_chan_recv(candidates, &p));

		for (;;) {
            const void *c;
            assert(eb_chan_recv(composites, &c));
            
			while ((intptr_t)p < (intptr_t)c) {
                eb_chan_send(primesproxy, p);
                eb_chan_send(out, p);
                assert(eb_chan_recv(candidates, &p));
			}
			if (p == c) {
                assert(eb_chan_recv(candidates, &p));
			}
		}
	);

	return out;
}

int main() {
	eb_chan primes = Sieve();
	int a[] = {2, 3, 5, 7, 11, 13, 17, 19, 23, 29, 31, 37, 41, 43, 47, 53, 59, 61, 67, 71, 73, 79, 83, 89, 97};
	for (int i = 0; i < (sizeof(a)/sizeof(*a)); i++) {
        const void *x;
        assert(eb_chan_recv(primes, &x));
        assert((int)(intptr_t)x == a[i]);
	}
    
    return 0;
}
