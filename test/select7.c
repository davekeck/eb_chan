// DONE

// Test select when discarding a value.

#include "testglue.h"

void recv1(eb_chan c) {
    assert(eb_chan_recv(c, NULL) == eb_chan_ret_ok);
}

void recv2(eb_chan c) {
    eb_chan_op recv = eb_chan_op_recv(c);
    assert(eb_chan_select(eb_nsec_forever, &recv) == &recv);
}

void recv3(eb_chan c) {
    eb_chan c2 = eb_chan_create(0);
    eb_chan_op crecv = eb_chan_op_recv(c);
    eb_chan_op c2recv = eb_chan_op_recv(c2);
    eb_chan_op *r = eb_chan_select(eb_nsec_forever, &crecv, &c2recv);
    if (r == &crecv) {
    } else if (r == &c2recv) {
    } else {
        abort();
    }
}

typedef void (*RecvFunc)(eb_chan c);

void send1(RecvFunc recv) {
	eb_chan c = eb_chan_create(0);
	go( recv(c) );
    usleep(10);
    eb_chan_send(c, (void*)1);
}

void send2(RecvFunc recv) {
	eb_chan c = eb_chan_create(0);
	go( recv(c) );
	usleep(10);
    
    eb_chan_op csend = eb_chan_op_send(c, (void*)1);
    assert(eb_chan_select(eb_nsec_forever, &csend) == &csend);
}

void send3(RecvFunc recv) {
	eb_chan c = eb_chan_create(0);
	go( recv(c) );
	usleep(10);
    
    eb_chan c2 = eb_chan_create(0);
    
    eb_chan_op csend = eb_chan_op_send(c, (void*)1);
    eb_chan_op c2send = eb_chan_op_send(c2, (void*)1);
    eb_chan_op *r = eb_chan_select(eb_nsec_forever, &csend, &c2send);
    if (r == &csend) {
    } else if (r == &c2send) {
    } else {
        abort();
    }
}

int main() {
	send1(recv1);
	send2(recv1);
	send3(recv1);
	send1(recv2);
	send2(recv2);
	send3(recv2);
	send1(recv3);
	send2(recv3);
	send3(recv3);
    return 0;
}
