#include "eb_port.h"
#include <stdlib.h>
#include <assert.h>
#include <libkern/OSAtomic.h>
#include <mach/mach.h>
#include "eb_assert.h"

#define PORT_POOL_CAP 0x10
static OSSpinLock g_port_pool_lock = OS_SPINLOCK_INIT;
static eb_port_t g_port_pool[PORT_POOL_CAP];
static size_t g_port_pool_len = 0;

struct eb_port {
    int32_t retain_count;
    mach_port_t port;
};

static inline bool wait_on_port(eb_port_t p, bool block) {
    assert(p);
    /* Wait indefinitely for a message to come on our port */
    mach_msg_empty_rcv_t m;
    mach_msg_return_t r = mach_msg(&m.header, MACH_RCV_MSG, (block ? 0 : MACH_RCV_TIMEOUT), sizeof(m), p->port, MACH_MSG_TIMEOUT_NONE, MACH_PORT_NULL);
        eb_assert_or_recover(r == MACH_MSG_SUCCESS || r == MACH_RCV_TIMED_OUT, eb_no_op);
    return (r == MACH_MSG_SUCCESS);
}

static void eb_port_free(eb_port_t p) {
    /* Intentionally allowing p==NULL */
    if (!p) {
        return;
    }
    
    bool added_to_pool = false;
    if (p->port != MACH_PORT_NULL) {
        /* Determine whether we should clear the port's buffer because we're going to try adding the port to our pool. */
        bool clear_buffer = false;
        OSSpinLockLock(&g_port_pool_lock);
            clear_buffer = (g_port_pool_len < PORT_POOL_CAP);
        OSSpinLockUnlock(&g_port_pool_lock);
        
        if (clear_buffer) {
            while (wait_on_port(p, false));
        }
        
        /* Now that the buffer's empty, add the port to the pool as long as it'll still fit. */
        OSSpinLockLock(&g_port_pool_lock);
            if (g_port_pool_len < PORT_POOL_CAP) {
                g_port_pool[g_port_pool_len] = p;
                g_port_pool_len++;
                added_to_pool = true;
            }
        OSSpinLockUnlock(&g_port_pool_lock);
        
        /* If we couldn't add the port to the pool, destroy the underlying mach port. */
        if (!added_to_pool) {
            mach_port_destroy(mach_task_self(), p->port);
            p->port = MACH_PORT_NULL;
        }
    }
    
    if (!added_to_pool) {
        free(p);
        p = NULL;
    }
}

eb_port_t eb_port_create() {
    eb_port_t p = NULL;
    /* First try to pop a port out of the pool */
    OSSpinLockLock(&g_port_pool_lock);
        if (g_port_pool_len) {
            g_port_pool_len--;
            p = g_port_pool[g_port_pool_len];
        }
    OSSpinLockUnlock(&g_port_pool_lock);
    
    if (p) {
//        printf("GOT FROM POOL\n");
        /* We successfully popped a port out of the pool */
        eb_assert_or_bail(!p->retain_count, "Sanity-check failed");
    } else {
        /* We couldn't get a port out of the pool */
        p = malloc(sizeof(*p));
            eb_assert_or_recover(p, goto failed);
        bzero(p, sizeof(*p));
        
        /* Create our receive right and insert a send right. */
        kern_return_t r = mach_port_allocate(mach_task_self(), MACH_PORT_RIGHT_RECEIVE, &p->port);
            eb_assert_or_recover(r == KERN_SUCCESS, goto failed);
        /* Insert a send right */
        r = mach_port_insert_right(mach_task_self(), p->port, p->port, MACH_MSG_TYPE_MAKE_SEND);
            eb_assert_or_recover(r == KERN_SUCCESS, goto failed);
    }
    
    p->retain_count = 1;
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
    wait_on_port(p, true);
}
