#include "eb_port.h"
#include <stdlib.h>
#include <assert.h>
#include <errno.h>
#include <string.h>

#if __MACH__
    #define DARWIN 1
    #include <mach/mach.h>
#elif __linux__
    #define LINUX 1
    #include <time.h>
    #include <semaphore.h>
#endif

#include "eb_assert.h"
#include "eb_atomic.h"

#define PORT_POOL_CAP 0x10
static eb_spinlock g_port_pool_lock = EB_SPINLOCK_INIT;
static eb_port g_port_pool[PORT_POOL_CAP];
static size_t g_port_pool_len = 0;

struct eb_port {
    unsigned int retain_count;
    bool sem_valid;
    bool signaled;
    #if DARWIN
        semaphore_t sem;
    #elif LINUX
        sem_t sem;
    #endif
};

static void eb_port_free(eb_port p) {
    /* Allowing p==NULL so that this function can be called unconditionally on failure from eb_port_create() */
    if (!p) {
        return;
    }
    
    bool added_to_pool = false;
    if (p->sem_valid) {
        /* Determine whether we should clear the reset the port because we're going to try adding the port to our pool. */
        bool reset = false;
        eb_spinlock_lock(&g_port_pool_lock);
            reset = (g_port_pool_len < PORT_POOL_CAP);
        eb_spinlock_unlock(&g_port_pool_lock);
        
        if (reset) {
            eb_port_wait(p, eb_nsecs_zero);
        }
        
        /* Now that the port's reset, add it to the pool as long as it'll still fit. */
        eb_spinlock_lock(&g_port_pool_lock);
            if (g_port_pool_len < PORT_POOL_CAP) {
                g_port_pool[g_port_pool_len] = p;
                g_port_pool_len++;
                added_to_pool = true;
            }
        eb_spinlock_unlock(&g_port_pool_lock);
        
        /* If we couldn't add the port to the pool, destroy the underlying semaphore. */
        if (!added_to_pool) {
            #if DARWIN
                kern_return_t r = semaphore_destroy(mach_task_self(), p->sem);
                    eb_assert_or_recover(r == KERN_SUCCESS, eb_no_op);
            #elif LINUX
                int r = sem_destroy(&p->sem);
                    eb_assert_or_recover(!r, eb_no_op);
            #endif
            
            p->sem_valid = false;
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
        memset(p, 0, sizeof(*p));
        
        /* Create the semaphore */
        #if DARWIN
            kern_return_t r = semaphore_create(mach_task_self(), &p->sem, SYNC_POLICY_FIFO, 0);
                eb_assert_or_recover(r == KERN_SUCCESS, goto failed);
        #elif LINUX
            int r = sem_init(&p->sem, 0, 0);
                eb_assert_or_recover(!r,  goto failed);
        #endif
    }
    
    p->sem_valid = true;
    p->retain_count = 1;
    return p;
    failed: {
        eb_port_free(p);
        return NULL;
    }
}

eb_port eb_port_retain(eb_port p) {
    assert(p);
    eb_atomic_add(&p->retain_count, 1);
    return p;
}

void eb_port_release(eb_port p) {
    assert(p);
    if (eb_atomic_add(&p->retain_count, -1) == 0) {
        eb_port_free(p);
    }
}

void eb_port_signal(eb_port p) {
    assert(p);
    
    if (eb_atomic_compare_and_swap(&p->signaled, false, true)) {
        #if DARWIN
            kern_return_t r = semaphore_signal(p->sem);
                eb_assert_or_recover(r == KERN_SUCCESS, eb_no_op);
        #elif LINUX
            int r = sem_post(&p->sem);
                eb_assert_or_recover(!r, eb_no_op);
        #endif
    }
}

bool eb_port_wait(eb_port p, eb_nsecs timeout) {
    assert(p);
    
    bool result = false;
    if (timeout == eb_nsecs_zero) {
        /* ## Non-blocking */
        #if DARWIN
            kern_return_t r = semaphore_timedwait(p->sem, (mach_timespec_t){0, 0});
                eb_assert_or_recover(r == KERN_SUCCESS || r == KERN_OPERATION_TIMED_OUT, eb_no_op);
            result = (r == KERN_SUCCESS);
        #elif LINUX
            int r = 0;
            while ((r = sem_trywait(&p->sem)) == -1 && errno == EINTR);
                eb_assert_or_recover(!r || (r == -1 && errno == EAGAIN), eb_no_op);
            result = !r;
        #endif
    } else if (timeout == eb_nsecs_forever) {
        /* ## Blocking */
        #if DARWIN
            kern_return_t r;
            while ((r = semaphore_wait(p->sem)) == KERN_ABORTED);
                eb_assert_or_recover(r == KERN_SUCCESS, eb_no_op);
            result = (r == KERN_SUCCESS);
        #elif LINUX
            int r;
            while ((r = sem_wait(&p->sem)) == -1 && errno == EINTR);
                eb_assert_or_recover(!r, eb_no_op);
            result = !r;
        #endif
    } else {
        /* ## Actual timeout */
        eb_nsecs start_time = eb_time_now();
        eb_nsecs remaining_timeout = timeout;
        for (;;) {
            #if DARWIN
                /* This needs to be in a loop because semaphore_timedwait() can return KERN_ABORTED, e.g. if the process receives a signal. */
                mach_timespec_t ts = {.tv_sec = (unsigned int)(remaining_timeout / eb_nsecs_per_sec), .tv_nsec = (clock_res_t)(remaining_timeout % eb_nsecs_per_sec)};
                kern_return_t r = semaphore_timedwait(p->sem, ts);
                    eb_assert_or_recover(r == KERN_SUCCESS || r == KERN_OPERATION_TIMED_OUT || r == KERN_ABORTED, eb_no_op);
                
                if (r == KERN_SUCCESS) {
                    result = true;
                    break;
                }
            #elif LINUX
                /* Because sem_timedwait() uses the system's _REALTIME clock instead of the _MONOTONIC clock, we'll time out when
                   the system's time changes. For that reason, we check for the timeout case ourself (instead of relying on errno
                   after calling sem_timedwait()) condition ourself, using our own monotonic clock APIs (eb_time_now()), and
                   restart sem_timedwait() if we determine independently that we haven't timed-out. */
                struct timespec ts;
                int r = clock_gettime(CLOCK_REALTIME, &ts);
                    eb_assert_or_recover(!r, break);
                ts.tv_sec += (remaining_timeout / eb_nsecs_per_sec);
                ts.tv_nsec += (remaining_timeout % eb_nsecs_per_sec);
                r = sem_timedwait(&p->sem, &ts);
                    /* The allowed return cases are: success (r==0), timed-out (r==-1, errno==ETIMEDOUT), (r==-1, errno==EINTR) */
                    eb_assert_or_recover(!r || (r == -1 && (errno == ETIMEDOUT || errno == EINTR)), break);
                
                /* If we acquired the semaphore, set our flag and break! */
                if (!r) {
                    result = true;
                    break;
                }
            #endif
                
            /* Determine whether we timed-out, and if not, update 'remaining_timeout' with the amount of time to go. */
            eb_nsecs elapsed = eb_time_now() - start_time;
            if (elapsed < timeout) {
                remaining_timeout = timeout - elapsed;
            } else {
                break;
            }
        }
    }
    
    if (result) {
        assert(eb_atomic_compare_and_swap(&p->signaled, true, false));
    }
    
    return result;
}
