#if __MACH__

#include "eb_port.h"
#include <stdlib.h>
#include <assert.h>
#include <mach/mach.h>
#include "eb_assert.h"
#include "eb_atomic.h"

#define PORT_POOL_CAP 0x10
static eb_spinlock g_port_pool_lock = EB_SPINLOCK_INIT;
static eb_port g_port_pool[PORT_POOL_CAP];
static size_t g_port_pool_len = 0;

struct eb_port {
    eb_atomic_int retain_count;
    semaphore_t sem;
};

static void eb_port_free(eb_port p) {
    /* Intentionally allowing p==NULL so that this function can be called from eb_port_create() */
    if (!p) {
        return;
    }
    
    bool added_to_pool = false;
    if (p->sem != MACH_PORT_NULL) {
        /* Determine whether we should clear the port's buffer because we're going to try adding the port to our pool. */
        bool clear_buffer = false;
        eb_spinlock_lock(&g_port_pool_lock);
            clear_buffer = (g_port_pool_len < PORT_POOL_CAP);
        eb_spinlock_unlock(&g_port_pool_lock);
        
        if (clear_buffer) {
            while (eb_port_wait(p, eb_timeout_now));
        }
        
        /* Now that the buffer's empty, add the port to the pool as long as it'll still fit. */
        eb_spinlock_lock(&g_port_pool_lock);
            if (g_port_pool_len < PORT_POOL_CAP) {
                g_port_pool[g_port_pool_len] = p;
                g_port_pool_len++;
                added_to_pool = true;
            }
        eb_spinlock_unlock(&g_port_pool_lock);
        
        /* If we couldn't add the port to the pool, destroy the underlying mach port. */
        if (!added_to_pool) {
            semaphore_destroy(mach_task_self(), p->sem);
            p->sem = MACH_PORT_NULL;
        }
    }
    
    if (!added_to_pool) {
        free(p);
        p = NULL;
    }
}

eb_port eb_port_create() {
    eb_port p = NULL;
    /* First try to pop a port out of the pool */
    eb_spinlock_lock(&g_port_pool_lock);
        if (g_port_pool_len) {
            g_port_pool_len--;
            p = g_port_pool[g_port_pool_len];
        }
    eb_spinlock_unlock(&g_port_pool_lock);
    
    if (p) {
        /* We successfully popped a port out of the pool */
        eb_assert_or_bail(!p->retain_count, "Sanity-check failed");
    } else {
        /* We couldn't get a port out of the pool */
        p = malloc(sizeof(*p));
            eb_assert_or_recover(p, goto failed);
        bzero(p, sizeof(*p));
        
        /* Create the semaphore */
        kern_return_t r = semaphore_create(mach_task_self(), &p->sem, SYNC_POLICY_FIFO, 0);
            eb_assert_or_recover(r == KERN_SUCCESS, goto failed);
    }
    
    p->retain_count = 1;
    return p;
    failed: {
        eb_port_free(p);
        return NULL;
    }
}

eb_port eb_port_retain(eb_port p) {
    assert(p);
    eb_atomic_int_add(&p->retain_count, 1);
    return p;
}

void eb_port_release(eb_port p) {
    assert(p);
    if (eb_atomic_int_add(&p->retain_count, -1) == 1) {
        eb_port_free(p);
    }
}

void eb_port_signal(eb_port p) {
    assert(p);
    kern_return_t r = semaphore_signal(p->sem);
        eb_assert_or_recover(r == KERN_SUCCESS, eb_no_op);
}

bool eb_port_wait(eb_port p, eb_timeout timeout) {
    assert(p);
    
    kern_return_t r = KERN_SUCCESS;
    if (!timeout) {
        /* Non-blocking */
        r = semaphore_timedwait(p->sem, (mach_timespec_t){0, 0});
            eb_assert_or_recover(r == KERN_SUCCESS || r == KERN_OPERATION_TIMED_OUT, eb_no_op);
    } else if (timeout == eb_timeout_never) {
        /* Blocking */
        r = semaphore_wait(p->sem);
            eb_assert_or_recover(r == KERN_SUCCESS, eb_no_op);
    } else {
        /* Timeout */
        mach_timespec_t t = {.tv_sec = (unsigned int)(timeout / NSEC_PER_SEC), .tv_nsec = (clock_res_t)(timeout % NSEC_PER_SEC)};
        r = semaphore_timedwait(p->sem, t);
            eb_assert_or_recover(r == KERN_SUCCESS || r == KERN_OPERATION_TIMED_OUT, eb_no_op);
    }
    
    return (r == KERN_SUCCESS);
}

#endif
