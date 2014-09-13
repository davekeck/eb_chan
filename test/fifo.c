// run

// Copyright 2009 The Go Authors. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

// Test that unbuffered channels act as pure fifos.

#include "eb_chan.h"

#define N 10

void AsynchFifo() {
    eb_chan ch = eb_chan_create(N);
    
    for (size_t i = 0; i < N; i++) {
        eb_chan_op send = eb_chan_send_op(gChan, (uintptr_t)i);
        eb_chan_do((eb_chan_op *const *){&send}, 1, eb_nsecs_forever);
        
        eb_chan_op *r = eb_chan_do(eb_nsecs_forever, &send, &recv);
    }
    
	for i := 0; i < N; i++ {
		ch <- i
	}
	for i := 0; i < N; i++ {
		if <-ch != i {
			print("bad receive\n")
			os.Exit(1)
		}
	}
}

void Chain(ch <-chan int, val int, in <-chan int, out chan<- int) {
	<-in
	if <-ch != val {
		panic(val)
	}
	out <- 1
}

// thread together a daisy chain to read the elements in sequence
void SynchFifo() {
	ch := make(chan int)
	in := make(chan int)
	start := in
	for i := 0; i < N; i++ {
		out := make(chan int)
		go Chain(ch, i, in, out)
		in = out
	}
	start <- 0
	for i := 0; i < N; i++ {
		ch <- i
	}
	<-in
}

int main() {
	AsynchFifo()
	SynchFifo()
    
    return 0;
}

