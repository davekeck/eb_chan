// DONE

// Test channel operations that test for blocking.
// Use several sizes and types of operands.

#include "testglue.h"

void i32receiver(eb_chan c, eb_chan strobe) {
    const void *v;
    assert(eb_chan_recv(c, &v));
    assert(v == (void*)123);
    
    eb_chan_send(strobe, (void*)true);
}

void i32sender(eb_chan c, eb_chan strobe) {
    eb_chan_send(c, (void*)234);
    eb_chan_send(strobe, (void*)true);
}

void i64receiver(eb_chan c, eb_chan strobe) {
    const void *v;
    assert(eb_chan_recv(c, &v));
    assert(v == (void*)123456);
    
    eb_chan_send(strobe, (void*)true);
}

void i64sender(eb_chan c, eb_chan strobe) {
    eb_chan_send(c, (void*)234567);
    eb_chan_send(strobe, (void*)true);
}

void breceiver(eb_chan c, eb_chan strobe) {
    const void *v;
    assert(eb_chan_recv(c, &v));
    assert(v == (void*)true);
    
    eb_chan_send(strobe, (void*)true);
}

void bsender(eb_chan c, eb_chan strobe) {
    eb_chan_send(c, (void*)true);
    eb_chan_send(strobe, (void*)true);
}

void sreceiver(eb_chan c, eb_chan strobe) {
    const void *v;
    assert(eb_chan_recv(c, &v));
    assert(!strcmp(v, "hello"));
    
    eb_chan_send(strobe, (void*)true);
}

void ssender(eb_chan c, eb_chan strobe) {
    eb_chan_send(c, (void*)"hello again");
    eb_chan_send(strobe, (void*)true);
}

void mysleep() {
    usleep(10);
    usleep(10);
    usleep(10);
}

static const int maxTries = 10000; // Up to 100ms per test.

int main() {
    int32_t i32;
    int64_t i64;
    bool b;
    const char *s;
    eb_chan sync = eb_chan_create(0);

	for (int buffer = 0; buffer < 2; buffer++) {
		eb_chan c32 = eb_chan_create(buffer);
		eb_chan c64 = eb_chan_create(buffer);
		eb_chan cb = eb_chan_create(buffer);
		eb_chan cs = eb_chan_create(buffer);
        
        eb_chan_op c32recv = eb_chan_op_recv(c32);
        eb_chan_op *r32 = eb_chan_do(eb_nsec_zero, &c32recv);
        if (r32 == &c32recv) {
            abort();
        } else {
            // OK
        }
        
        eb_chan_op c64recv = eb_chan_op_recv(c64);
        eb_chan_op *r64 = eb_chan_do(eb_nsec_zero, &c64recv);
        if (r64 == &c64recv) {
            abort();
        } else {
            // OK
        }
        
        eb_chan_op cbrecv = eb_chan_op_recv(cb);
        eb_chan_op *rb = eb_chan_do(eb_nsec_zero, &cbrecv);
        if (rb == &cbrecv) {
            abort();
        } else {
            // OK
        }
        
        eb_chan_op csrecv = eb_chan_op_recv(cs);
        eb_chan_op *rs = eb_chan_do(eb_nsec_zero, &csrecv);
        if (rs == &csrecv) {
            abort();
        } else {
            // OK
        }
        
		go( i32receiver(c32, sync) );
		int try = 0;
	Send32:
		for (;;) {
            eb_chan_op send = eb_chan_op_send(c32, (void*)123);
            eb_chan_op *r = eb_chan_do(eb_nsec_zero, &send);
            
            if (r == &send) {
                break;
            } else {
				try++;
                assert(try <= maxTries);
				mysleep();
            }
		}
        
        eb_chan_recv(sync, NULL);

		go( i32sender(c32, sync) );
		if (buffer > 0) {
            eb_chan_recv(sync, NULL);
		}
		try = 0;
	Recv32:
		for (;;) {
            eb_chan_op recv = eb_chan_op_recv(c32);
            eb_chan_op *r = eb_chan_do(eb_nsec_zero, &recv);
            
            if (r == &recv) {
                assert(recv.open);
                i32 = (int32_t)(intptr_t)recv.val;
                break;
            } else {
				try++;
                assert(try <= maxTries);
				mysleep();
            }
		}
        
        assert(i32 == 234);
        
		if (buffer == 0) {
            eb_chan_recv(sync, NULL);
		}

		go( i64receiver(c64, sync) );
		try = 0;
	Send64:
		for (;;) {
            eb_chan_op send = eb_chan_op_send(c64, (void*)123456);
            eb_chan_op *r = eb_chan_do(eb_nsec_zero, &send);
            
            if (r == &send) {
                break;
            } else {
				try++;
                assert(try <= maxTries);
				mysleep();
            }
		}
        
		eb_chan_recv(sync, NULL);

		go( i64sender(c64, sync) );
		if (buffer > 0) {
            eb_chan_recv(sync, NULL);
		}
		try = 0;
	Recv64:
		for (;;) {
            eb_chan_op recv = eb_chan_op_recv(c64);
            eb_chan_op *r = eb_chan_do(eb_nsec_zero, &recv);
            
            if (r == &recv) {
                assert(recv.open);
                i64 = (int64_t)(intptr_t)recv.val;
                break;
            } else {
				try++;
                assert(try <= maxTries);
				mysleep();
            }
		}
        assert(i64 == 234567);
        
		if (buffer == 0) {
			eb_chan_recv(sync, NULL);
		}

		go( breceiver(cb, sync) );
		try = 0;
	SendBool:
		for (;;) {
            eb_chan_op send = eb_chan_op_send(cb, (void*)true);
            eb_chan_op *r = eb_chan_do(eb_nsec_zero, &send);
            
            if (r == &send) {
                break;
            } else {
				try++;
                assert(try <= maxTries);
				mysleep();
            }
		}
		eb_chan_recv(sync, NULL);

		go( bsender(cb, sync) );
		if (buffer > 0) {
			eb_chan_recv(sync, NULL);
		}
		try = 0;
	RecvBool:
		for (;;) {
            eb_chan_op recv = eb_chan_op_recv(cb);
            eb_chan_op *r = eb_chan_do(eb_nsec_zero, &recv);
            
            if (r == &recv) {
                assert(recv.open);
                b = (bool)(intptr_t)recv.val;
                break;
            } else {
				try++;
                assert(try <= maxTries);
				mysleep();
            }
		}
        assert(b);
		if (buffer == 0) {
			eb_chan_recv(sync, NULL);
		}

		go( sreceiver(cs, sync) );
		try = 0;
	SendString:
		for (;;) {
            eb_chan_op send = eb_chan_op_send(cs, (void*)"hello");
            eb_chan_op *r = eb_chan_do(eb_nsec_zero, &send);
            
            if (r == &send) {
                break;
            } else {
				try++;
                assert(try <= maxTries);
				mysleep();
            }
		}
		eb_chan_recv(sync, NULL);

		go( ssender(cs, sync) );
		if (buffer > 0) {
			eb_chan_recv(sync, NULL);
		}
		try = 0;
	RecvString:
		for (;;) {
            eb_chan_op recv = eb_chan_op_recv(cs);
            eb_chan_op *r = eb_chan_do(eb_nsec_zero, &recv);
            
            if (r == &recv) {
                assert(recv.open);
                s = (const char *)recv.val;
                break;
            } else {
				try++;
                assert(try <= maxTries);
				mysleep();
            }
		}
        assert(!strcmp(s, "hello again"));
		if (buffer == 0) {
			eb_chan_recv(sync, NULL);
		}
	}
    return 0;
}
