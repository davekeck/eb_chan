// DONE

// Test concurrency primitives: classical inefficient concurrent prime sieve.

// Generate primes up to 100 using channels, checking the results.
// This sieve consists of a linear chain of divisibility filters,
// equivalent to trial-dividing each n by all primes p ≤ n.

#include "testglue.h"

// Send the sequence 2, 3, 4, ... to channel 'ch'.
void Generate(eb_chan ch) {
	for (int i = 2;; i++) {
        eb_chan_send(ch, (void*)(intptr_t)i); // Send 'i' to channel 'ch'.
	}
}

// Copy the values from channel 'in' to channel 'out',
// removing those divisible by 'prime'.
void Filter(eb_chan in, eb_chan out, int prime) {
	for (const void *i; eb_chan_recv(in, &i);) { // Loop over values received from 'in'.
		if (((intptr_t)i)%prime != 0) {
            eb_chan_send(out, (void*)i); // Send 'i' to channel 'out'.
		}
	}
}

// The prime sieve: Daisy-chain Filter processes together.
void Sieve(eb_chan primes) {
	eb_chan ch = eb_chan_create(0); // Create a new channel.
	go( Generate(ch) );     // Start Generate() as a subprocess.
	for (;;) {
		// Note that ch is different on each iteration.
        const void *prime;
        assert(eb_chan_recv(ch, &prime));
        eb_chan_send(primes, prime);
        
        eb_chan ch1 = eb_chan_create(0);
		go( Filter(ch, ch1, (int)(intptr_t)prime) );
		ch = ch1;
	}
}

int main() {
	eb_chan primes = eb_chan_create(0);
	go( Sieve(primes) );
	int a[] = {2, 3, 5, 7, 11, 13, 17, 19, 23, 29, 31, 37, 41, 43, 47, 53, 59, 61, 67, 71, 73, 79, 83, 89, 97};
	for (int i = 0; i < (sizeof(a) / sizeof(*a)); i++) {
        const void *x;
        assert(eb_chan_recv(primes, &x));
        assert((int)(intptr_t)x == a[i]);
        printf("%ju good (%p)\n", (uintmax_t)i, x);
	}
}
