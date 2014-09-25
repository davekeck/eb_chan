// DONE

// Test the semantics of the select statement
// for basic empty/non-empty cases.

#include "testglue.h"

const char *const always = "function did not";
const char *const never = "function did";

void unreachable() {
    abort();
}

// Calls f and verifies that f always/never panics depending on signal.
void testPanic(const char *signal, VoidBlock f) {
	f();
    usleep(100000); //allow signal to be handled
    
    if (signal == always) {
        fprintf(stderr, "We should have panicked but we didn't!\n");
        exit(1);
    }
}


// Calls f and empirically verifies that f always/never blocks depending on signal.
void testBlock(const char *signal, VoidBlock f) {
	eb_chan c = eb_chan_create(0);
	go(
		f();
        eb_chan_send(c, never); // f didn't block
	);
	
    go(
        usleep(100000); // 0.1s seems plenty long
        eb_chan_send(c, always); // f blocked always
	);
    
    const void *v;
    assert(eb_chan_recv(c, &v) == eb_chan_ret_ok);
    
    if (v != signal) {
        char msg[256];
        snprintf(msg, sizeof(msg), "%s block", signal);
        abort();
    }
}


int main() {
	const int async = 1; // asynchronous channels
    eb_chan nilch = NULL;
	eb_chan closedch = eb_chan_create(0);
    eb_chan_close(closedch);

	// sending/receiving from a nil channel blocks
	testBlock(always, ^{
        eb_chan_send(nilch, (void*)7);
	});
	testBlock(always, ^{
        eb_chan_recv(nilch, NULL);
	});

	// sending/receiving from a nil channel inside a select is never selected
	testPanic(never, ^{
        eb_chan_op nilchsend = eb_chan_op_send(NULL, (void*)7);
        eb_chan_op *r = eb_chan_select(eb_nsec_zero, &nilchsend);
        if (r == &nilchsend) {
            unreachable();
        } else {
            // OK
        }
	});
    
	testPanic(never, ^{
        eb_chan_op nilchrecv = eb_chan_op_recv(NULL);
        eb_chan_op *r = eb_chan_select(eb_nsec_zero, &nilchrecv);
        if (r == &nilchrecv) {
            unreachable();
        } else {
            // OK
        }
	});

	// sending to an async channel with free buffer space never blocks
	testBlock(never, ^{
		eb_chan ch = eb_chan_create(async);
        eb_chan_send(ch, (void*)7);
	});

	// receiving from a closed channel never blocks
	testBlock(never, ^{
		for (int i = 0; i < 10; i++) {
            
            if (eb_chan_recv(closedch, NULL) == eb_chan_ret_ok) {
				abort();
			}
            
			if (eb_chan_recv(closedch, NULL) == eb_chan_ret_ok) {
				abort();
			}
		}
	});
    
	// receiving from a non-ready channel always blocks
	testBlock(always, ^{
		eb_chan ch = eb_chan_create(0);
        eb_chan_recv(ch, NULL);
	});

	// empty selects always block
	testBlock(always, ^{
        eb_chan_select(eb_nsec_forever);
	});

	// selects with only nil channels always block
	testBlock(always, ^{
        eb_chan_op nilchrecv = eb_chan_op_recv(nilch);
        eb_chan_select(eb_nsec_forever, &nilchrecv);
        unreachable();
	});
	testBlock(always, ^{
        eb_chan_op nilchsend = eb_chan_op_send(nilch, (void*)7);
        eb_chan_select(eb_nsec_forever, &nilchsend);
        unreachable();
	});
	testBlock(always, ^{
        eb_chan_op nilchrecv = eb_chan_op_recv(nilch);
        eb_chan_op nilchsend = eb_chan_op_send(nilch, (void*)7);
        eb_chan_select(eb_nsec_forever, &nilchrecv, &nilchsend);
        unreachable();
	});

	// selects with non-ready non-nil channels always block
	testBlock(always, ^{
		eb_chan ch = eb_chan_create(0);
        eb_chan_op recvop = eb_chan_op_recv(ch);
        eb_chan_select(eb_nsec_forever, &recvop);
        unreachable();
	});

	// selects with default cases don't block
	testBlock(never, ^{
        eb_chan_select(eb_nsec_zero);
	});
	testBlock(never, ^{
        eb_chan_op nilchrecv = eb_chan_op_recv(nilch);
        eb_chan_select(eb_nsec_zero, &nilchrecv);
	});
	testBlock(never, ^{
        eb_chan_op nilchsend = eb_chan_op_send(nilch, (void*)7);
        eb_chan_select(eb_nsec_zero, &nilchsend);
	});

	// selects with ready channels don't block
	testBlock(never, ^{
		eb_chan ch = eb_chan_create(async);
        eb_chan_op sendop = eb_chan_op_send(ch, (void*)7);
        eb_chan_op *r = eb_chan_select(eb_nsec_zero, &sendop);
        if (r == &sendop) {
            // OK
        } else {
            unreachable();
        }
	});
	testBlock(never, ^{
		eb_chan ch = eb_chan_create(async);
        eb_chan_send(ch, (void*)7);
        
        eb_chan_op recvop = eb_chan_op_recv(ch);
        eb_chan_op *r = eb_chan_select(eb_nsec_zero, &recvop);
        if (r == &recvop) {
            // OK
        } else {
            unreachable();
        }
	});

	// selects with closed channels behave like ordinary operations
	testBlock(never, ^{
        eb_chan_op recvop = eb_chan_op_recv(closedch);
        assert(eb_chan_select(eb_nsec_forever, &recvop) == &recvop);
        assert(!recvop.open);
	});

	// select should not get confused if it sees itself
	testBlock(always, ^{
		eb_chan c = eb_chan_create(0);
        eb_chan_op sendop = eb_chan_op_send(c, (void*)1);
        eb_chan_op recvop = eb_chan_op_recv(c);
        eb_chan_select(eb_nsec_forever, &sendop, &recvop);
        unreachable();
	});
    
    
    // test panicing behavior (uncomment one of these blocks)
//	testPanic(always, ^{
//        eb_chan_op sendop = eb_chan_op_send(closedch, (void*)7);
//        eb_chan_select(eb_nsec_zero, &sendop);
//	});
    
//	// sending to a closed channel panics.
//	testPanic(always, ^{
//        eb_chan_send(closedch, (void*)7);
//	});
    
    
    return 0;
}
