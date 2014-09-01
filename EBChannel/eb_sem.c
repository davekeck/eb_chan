#include "eb_sem.h"
#include <stdlib.h>
#include <assert.h>
#include <mach/mach.h>
#include <semaphore.h>
#include <time.h>
#include <errno.h>
#include "eb_assert.h"
#include "eb_atomic.h"

#define DARWIN __MACH__
#define LINUX __linux__

#define SEM_POOL_CAP 0x10
static eb_spinlock g_sem_pool_lock = EB_SPINLOCK_INIT;
static eb_sem g_sem_pool[SEM_POOL_CAP];
static size_t g_sem_pool_len = 0;

struct eb_sem {
    eb_atomic_int retain_count;
#if DARWIN
    semaphore_t sem;
#elif LINUX
    sem_t sem;
#endif
    bool sem_valid;
};

static void eb_sem_free(eb_sem p) {
    /* Intentionally allowing p==NULL so that this function can be called from eb_sem_create() */
    if (!p) {
        return;
    }
    
    bool added_to_pool = false;
    if (p->sem_valid) {
        /* Determine whether we should clear the semaphore's buffer because we're going to try adding the semaphore to our pool. */
        bool clear_buffer = false;
        eb_spinlock_lock(&g_sem_pool_lock);
            clear_buffer = (g_sem_pool_len < SEM_POOL_CAP);
        eb_spinlock_unlock(&g_sem_pool_lock);
        
        if (clear_buffer) {
            while (eb_sem_wait(p, eb_nsecs_zero));
        }
        
        /* Now that the buffer's empty, add the semaphore to the pool as long as it'll still fit. */
        eb_spinlock_lock(&g_sem_pool_lock);
            if (g_sem_pool_len < SEM_POOL_CAP) {
                g_sem_pool[g_sem_pool_len] = p;
                g_sem_pool_len++;
                added_to_pool = true;
            }
        eb_spinlock_unlock(&g_sem_pool_lock);
        
        /* If we couldn't add the semaphore to the pool, destroy the underlying semaphore. */
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

eb_sem eb_sem_create() {
    eb_sem p = NULL;
    /* First try to pop a semaphore out of the pool */
    eb_spinlock_lock(&g_sem_pool_lock);
        if (g_sem_pool_len) {
            g_sem_pool_len--;
            p = g_sem_pool[g_sem_pool_len];
        }
    eb_spinlock_unlock(&g_sem_pool_lock);
    
    if (p) {
        /* We successfully popped a semaphore out of the pool */
        eb_assert_or_bail(!p->retain_count, "Sanity-check failed");
    } else {
        /* We couldn't get a semaphore out of the pool */
        p = malloc(sizeof(*p));
            eb_assert_or_recover(p, goto failed);
        bzero(p, sizeof(*p));
        
#if DARWIN
        /* Create the semaphore */
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
        eb_sem_free(p);
        return NULL;
    }
}

eb_sem eb_sem_retain(eb_sem p) {
    assert(p);
    eb_atomic_int_add(&p->retain_count, 1);
    return p;
}

void eb_sem_release(eb_sem p) {
    assert(p);
    if (eb_atomic_int_add(&p->retain_count, -1) == 1) {
        eb_sem_free(p);
    }
}

void eb_sem_signal(eb_sem p) {
    assert(p);
    
#if DARWIN
    kern_return_t r = semaphore_signal(p->sem);
        eb_assert_or_recover(r == KERN_SUCCESS, eb_no_op);
#elif LINUX
    int r = sem_post(&p->sem);
        eb_assert_or_recover(!r, eb_no_op);
#endif
}

bool eb_sem_wait(eb_sem p, eb_nsecs timeout) {
    assert(p);
    
    if (timeout == eb_nsecs_zero) {
        /* Non-blocking */
#if DARWIN
        kern_return_t r = semaphore_timedwait(p->sem, (mach_timespec_t){0, 0});
            eb_assert_or_recover(r == KERN_SUCCESS || r == KERN_OPERATION_TIMED_OUT, eb_no_op);
        return (r == KERN_SUCCESS);
#elif LINUX
        int r = 0;
        while ((r = sem_trywait(&p->sem)) == -1 && errno == EINTR);
            eb_assert_or_recover(!r || (r == -1 && errno == EAGAIN), eb_no_op);
        return !r;
#endif
    } else if (timeout == eb_nsecs_forever) {
        /* Blocking */
#if DARWIN
        kern_return_t r = semaphore_wait(p->sem);
            eb_assert_or_recover(r == KERN_SUCCESS, eb_no_op);
        return (r == KERN_SUCCESS);
#elif LINUX
        int r = 0;
        while ((r = sem_wait(&p->sem)) == -1 && errno == EINTR);
            eb_assert_or_recover(!r, eb_no_op);
        return !r;
#endif
    }
    
    /* Actual timeout */
#if DARWIN
    mach_timespec_t ts = {.tv_sec = (unsigned int)(timeout / NSEC_PER_SEC), .tv_nsec = (clock_res_t)(timeout % NSEC_PER_SEC)};
    kern_return_t r = semaphore_timedwait(p->sem, ts);
        eb_assert_or_recover(r == KERN_SUCCESS || r == KERN_OPERATION_TIMED_OUT, eb_no_op);
    return (r == KERN_SUCCESS);
#elif LINUX
    eb_nsecs start_time = eb_time_now();
    eb_nsecs remaining_timeout = timeout;
    bool result = false;
    for (;;) {
        /* Because sem_timedwait() uses the system's _REALTIME clock instead of the _MONOTONIC clock, we'll time out when
           the system's time changes. For that reason, we check for the timeout case ourself (instead of relying on errno
           after calling sem_timedwait()) condition ourself, using our own monotonic clock APIs (eb_time_now()), and
           restart sem_timedwait() if we determine independently that we haven't timed-out. */
        struct timespec ts;
        int r = clock_gettime(CLOCK_REALTIME, &ts);
            eb_assert_or_recover(!r, break);
        ts.tv_sec += (remaining_timeout / NSEC_PER_SEC);
        ts.tv_nsec += (remaining_timeout % NSEC_PER_SEC);
        r = sem_timedwait(&p->sem, &ts);
            /* The allowed return cases are: success (r==0), timed-out (r==-1, errno==ETIMEDOUT), (r==-1, errno==EINTR) */
            eb_assert_or_recover(!r || (r == -1 && (errno == ETIMEDOUT || errno == EINTR)), break);
        
        /* If we acquired the semaphore, set our flag and break! */
        if (!r) {
            result = true;
            break;
        }
        
        /* Determine whether we timed-out, and if not, update 'remaining_timeout' with the amount of time to go. */
        eb_nsecs elapsed = eb_time_now() - start_time;
        if (elapsed < timeout) {
            remaining_timeout = timeout - elapsed;
        } else {
            break;
        }
    }
    
    return result;
#endif
}
