#include "eb_port.h"
#include <stdlib.h>
#include <assert.h>
#include <libkern/OSAtomic.h>
#include <mach/mach.h>
#include "eb_assert.h"

struct eb_port {
    int32_t retain_count;
    mach_port_t port;
};

static void eb_port_free(eb_port_t p) {
    /* Intentionally allowing p==NULL */
    if (!p) {
        return;
    }
    
    if (p->port != MACH_PORT_NULL) {
        mach_port_destroy(mach_task_self(), p->port);
        p->port = MACH_PORT_NULL;
    }
    
    free(p);
    p = NULL;
}

eb_port_t eb_port_create() {
    eb_port_t p = malloc(sizeof(*p));
        eb_assert_or_recover(p, goto failed);
    
    p->retain_count = 1;
    p->port = MACH_PORT_NULL;
    
    /* Create our receive right */
    kern_return_t r = mach_port_allocate(mach_task_self(), MACH_PORT_RIGHT_RECEIVE, &p->port);
        eb_assert_or_recover(r == KERN_SUCCESS, goto failed);
    /* Insert a send right. */
    r = mach_port_insert_right(mach_task_self(), p->port, p->port, MACH_MSG_TYPE_MAKE_SEND);
        eb_assert_or_recover(r == KERN_SUCCESS, goto failed);
    
    return p;
    failed: {
        eb_port_free(p);
        return NULL;
    }
}

eb_port_t eb_port_retain(eb_port_t p) {
    assert(p);
    OSAtomicIncrement32(&p->retain_count);
    return p;
}

void eb_port_release(eb_port_t p) {
    assert(p);
    if (!OSAtomicDecrement32(&p->retain_count)) {
        eb_port_free(p);
    }
}

void eb_port_signal(eb_port_t p) {
    assert(p);
    /* Send a message to our port, but don't block if the buffer's full. */
    mach_msg_empty_send_t m = {
        .header = {
            .msgh_bits = MACH_MSGH_BITS(MACH_MSGH_BITS_REMOTE(MACH_MSG_TYPE_COPY_SEND), MACH_MSGH_BITS_LOCAL(MACH_MSGH_BITS_ZERO)),
            .msgh_size = sizeof(m),
            .msgh_remote_port = p->port,
            .msgh_local_port = MACH_PORT_NULL,
            .msgh_id = 0,
        }
    };
    mach_msg_return_t r = mach_msg(&m.header, MACH_SEND_MSG | MACH_SEND_TIMEOUT, sizeof(m), 0, MACH_PORT_NULL, 0, MACH_PORT_NULL);
        eb_assert_or_recover(r == MACH_MSG_SUCCESS || r == MACH_SEND_TIMED_OUT, eb_no_op);
}

void eb_port_wait(eb_port_t p) {
    assert(p);
    /* Wait indefinitely for a message to come on our port */
    mach_msg_empty_rcv_t m;
    mach_msg_return_t r = mach_msg(&m.header, MACH_RCV_MSG, 0, sizeof(m), p->port, MACH_MSG_TIMEOUT_NONE, MACH_PORT_NULL);
        eb_assert_or_recover(r == MACH_MSG_SUCCESS, eb_no_op);
}
