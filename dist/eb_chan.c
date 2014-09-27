// #######################################################
// ## eb_chan.c
// #######################################################

#include "eb_chan.h"
#include <stdlib.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sched.h>
// #######################################################
// ## eb_assert.h
// #######################################################

#include <stdbool.h>
#include <stdint.h>

#define eb_no_op

#define eb_assert_or_recover(cond, action) ({                                                                 \
    if (!(cond)) {                                                                                            \
        eb_assert_print("Assertion failed", #cond, __FILE__, (uintmax_t)__LINE__, __PRETTY_FUNCTION__);       \
        action;                                                                                               \
    }                                                                                                         \
})

#define eb_assert_or_bail(cond, msg) ({                                                        \
    if (!(cond)) {                                                                             \
        eb_assert_print(msg, #cond, __FILE__, (uintmax_t)__LINE__, __PRETTY_FUNCTION__);       \
        abort();                                                                               \
    }                                                                                          \
})

void eb_assert_print(const char *msg, const char *cond, const char *file, uintmax_t line, const char *func);
// #######################################################
// ## eb_assert.c
// #######################################################

#include <stdio.h>

void eb_assert_print(const char *msg, const char *cond, const char *file, uintmax_t line, const char *func) {
    fprintf(stderr, "=== %s ===\n"
                    "  Assertion: %s\n"
                    "  File: %s:%ju\n"
                    "  Function: %s\n", msg, cond, file, line, func);
}
// #######################################################
// ## eb_port.h
// #######################################################

#include <stddef.h>
#include <stdbool.h>

typedef struct eb_port *eb_port;

eb_port eb_port_create();
eb_port eb_port_retain(eb_port p);
void eb_port_release(eb_port p);

void eb_port_signal(eb_port p);
bool eb_port_wait(eb_port p, eb_nsec timeout);
// #######################################################
// ## eb_port.c
// #######################################################

#include <stdlib.h>
#include <assert.h>
#include <errno.h>
#include <string.h>
// #######################################################
// ## eb_sys.h
// #######################################################

#include <stddef.h>

#if __MACH__
    #define EB_SYS_DARWIN 1
#elif __linux__
    #define EB_SYS_LINUX 1
#else
    #error Unsupported system
#endif

/* ## Variables */
/* Returns the number of logical cores on the machine. _init must be called for this to be valid! */
size_t eb_sys_ncores;

/* ## Functions */
void eb_sys_init();
// #######################################################
// ## eb_sys.c
// #######################################################

// #######################################################
// ## eb_atomic.h
// #######################################################


#define eb_atomic_add(ptr, delta) __sync_add_and_fetch(ptr, delta) /* Returns the new value */
#define eb_atomic_compare_and_swap(ptr, old, new) __sync_bool_compare_and_swap(ptr, old, new)
#define eb_atomic_barrier() __sync_synchronize()

#if EB_SYS_DARWIN
    #include <mach/mach.h>
#elif EB_SYS_LINUX
    #include <unistd.h>
#endif

size_t ncores() {
    #if EB_SYS_DARWIN
        host_basic_info_data_t info;
        mach_msg_type_number_t count = HOST_BASIC_INFO_COUNT;
        kern_return_t r = host_info(mach_host_self(), HOST_BASIC_INFO, (host_info_t)&info, &count);
            eb_assert_or_recover(r == KERN_SUCCESS, return 0);
            eb_assert_or_recover(count == HOST_BASIC_INFO_COUNT, return 0);
            eb_assert_or_recover(info.logical_cpu > 0 && info.logical_cpu <= SIZE_MAX, return 0);
        return (size_t)info.logical_cpu;
    #elif EB_SYS_LINUX
        long ncores = sysconf(_SC_NPROCESSORS_ONLN);
            eb_assert_or_recover(ncores > 0 && ncores <= SIZE_MAX, return 0);
        return (size_t)ncores;
    #endif
}

void eb_sys_init() {
    if (!eb_sys_ncores) {
        eb_atomic_compare_and_swap(&eb_sys_ncores, 0, ncores());
    }
}
#if EB_SYS_DARWIN
    #include <mach/mach.h>
#elif EB_SYS_LINUX
    #include <time.h>
    #include <semaphore.h>
#endif
// #######################################################
// ## eb_spinlock.h
// #######################################################

#include <stdbool.h>
#include <sched.h>

/* ## Types */
typedef int eb_spinlock;
#define EB_SPINLOCK_INIT 0

/* ## Functions */
#define eb_spinlock_try(l) eb_atomic_compare_and_swap(l, 0, 1)

#define eb_spinlock_lock(l) ({             \
    if (eb_sys_ncores > 1) {               \
        while (!eb_spinlock_try(l));       \
    } else {                               \
        while (!eb_spinlock_try(l)) {      \
            sched_yield();                 \
        }                                  \
    }                                      \
})

#define eb_spinlock_unlock(l) eb_atomic_compare_and_swap(l, 1, 0)

//#define eb_spinlock_try(l) __sync_lock_test_and_set(l, 1) == 0
//#define eb_spinlock_lock(l) while (!eb_spinlock_try(l))
//#define eb_spinlock_unlock(l) __sync_lock_release(l)
//
//typedef OSSpinLock eb_spinlock;
//#define eb_spinlock_try(l) OSSpinLockTry(l)
//#define eb_spinlock_lock(l) OSSpinLockLock(l)
//#define eb_spinlock_unlock(l) OSSpinLockUnlock(l)
// #######################################################
// ## eb_time.h
// #######################################################


/* Returns the number of nanoseconds since an arbitrary point in time (usually the machine's boot time) */
eb_nsec eb_time_now();
// #######################################################
// ## eb_time.c
// #######################################################

#include <stdint.h>
#include <stdlib.h>
#if EB_SYS_DARWIN
    #include <mach/mach_time.h>
#elif EB_SYS_LINUX
    #include <time.h>
#endif

eb_nsec eb_time_now() {
#if EB_SYS_DARWIN
    /* Initialize k_timebase_info, thread-safely */
    static mach_timebase_info_t k_timebase_info = NULL;
    if (!k_timebase_info) {
        mach_timebase_info_t timebase_info = malloc(sizeof(*timebase_info));
        kern_return_t r = mach_timebase_info(timebase_info);
            eb_assert_or_recover(r == KERN_SUCCESS, return 0);
        
        /* Make sure the writes to 'timebase_info' are complete before we assign k_timebase_info */
        eb_atomic_barrier();
        
        if (!eb_atomic_compare_and_swap(&k_timebase_info, NULL, timebase_info)) {
            free(timebase_info);
            timebase_info = NULL;
        }
    }
    
    return ((mach_absolute_time() * k_timebase_info->numer) / k_timebase_info->denom);
#elif EB_SYS_LINUX
    struct timespec ts;
    int r = clock_gettime(CLOCK_MONOTONIC, &ts);
        eb_assert_or_recover(!r, return 0);
    return ((uint64_t)ts.tv_sec * eb_nsec_per_sec) + ts.tv_nsec;
#endif
}

#define PORT_POOL_CAP 0x10
static eb_spinlock g_port_pool_lock = EB_SPINLOCK_INIT;
static eb_port g_port_pool[PORT_POOL_CAP];
static size_t g_port_pool_len = 0;

struct eb_port {
    unsigned int retain_count;
    bool sem_valid;
    bool signaled;
    #if EB_SYS_DARWIN
        semaphore_t sem;
    #elif EB_SYS_LINUX
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
            eb_port_wait(p, eb_nsec_zero);
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
            #if EB_SYS_DARWIN
                kern_return_t r = semaphore_destroy(mach_task_self(), p->sem);
                    eb_assert_or_recover(r == KERN_SUCCESS, eb_no_op);
            #elif EB_SYS_LINUX
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
        /* Using calloc so that bytes are zeroed */
        p = calloc(1, sizeof(*p));
            eb_assert_or_recover(p, goto failed);
        
        /* Create the semaphore */
        #if EB_SYS_DARWIN
            kern_return_t r = semaphore_create(mach_task_self(), &p->sem, SYNC_POLICY_FIFO, 0);
                eb_assert_or_recover(r == KERN_SUCCESS, goto failed);
        #elif EB_SYS_LINUX
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
        #if EB_SYS_DARWIN
            kern_return_t r = semaphore_signal(p->sem);
                eb_assert_or_recover(r == KERN_SUCCESS, eb_no_op);
        #elif EB_SYS_LINUX
            int r = sem_post(&p->sem);
                eb_assert_or_recover(!r, eb_no_op);
        #endif
    }
}

bool eb_port_wait(eb_port p, eb_nsec timeout) {
    assert(p);
    
    bool result = false;
    if (timeout == eb_nsec_zero) {
        /* ## Non-blocking */
        #if EB_SYS_DARWIN
            kern_return_t r = semaphore_timedwait(p->sem, (mach_timespec_t){0, 0});
                eb_assert_or_recover(r == KERN_SUCCESS || r == KERN_OPERATION_TIMED_OUT, eb_no_op);
            result = (r == KERN_SUCCESS);
        #elif EB_SYS_LINUX
            int r = 0;
            while ((r = sem_trywait(&p->sem)) == -1 && errno == EINTR);
                eb_assert_or_recover(!r || (r == -1 && errno == EAGAIN), eb_no_op);
            result = !r;
        #endif
    } else if (timeout == eb_nsec_forever) {
        /* ## Blocking */
        #if EB_SYS_DARWIN
            kern_return_t r;
            while ((r = semaphore_wait(p->sem)) == KERN_ABORTED);
                eb_assert_or_recover(r == KERN_SUCCESS, eb_no_op);
            result = (r == KERN_SUCCESS);
        #elif EB_SYS_LINUX
            int r;
            while ((r = sem_wait(&p->sem)) == -1 && errno == EINTR);
                eb_assert_or_recover(!r, eb_no_op);
            result = !r;
        #endif
    } else {
        /* ## Actual timeout */
        eb_nsec start_time = eb_time_now();
        eb_nsec remaining_timeout = timeout;
        for (;;) {
            #if EB_SYS_DARWIN
                /* This needs to be in a loop because semaphore_timedwait() can return KERN_ABORTED, e.g. if the process receives a signal. */
                mach_timespec_t ts = {.tv_sec = (unsigned int)(remaining_timeout / eb_nsec_per_sec), .tv_nsec = (clock_res_t)(remaining_timeout % eb_nsec_per_sec)};
                kern_return_t r = semaphore_timedwait(p->sem, ts);
                    eb_assert_or_recover(r == KERN_SUCCESS || r == KERN_OPERATION_TIMED_OUT || r == KERN_ABORTED, eb_no_op);
                
                if (r == KERN_SUCCESS) {
                    result = true;
                    break;
                }
            #elif EB_SYS_LINUX
                /* Because sem_timedwait() uses the system's _REALTIME clock instead of the _MONOTONIC clock, we'll time out when
                   the system's time changes. For that reason, we check for the timeout case ourself (instead of relying on errno
                   after calling sem_timedwait()) condition ourself, using our own monotonic clock APIs (eb_time_now()), and
                   restart sem_timedwait() if we determine independently that we haven't timed-out. */
                struct timespec ts;
                int r = clock_gettime(CLOCK_REALTIME, &ts);
                    eb_assert_or_recover(!r, break);
                ts.tv_sec += (remaining_timeout / eb_nsec_per_sec);
                ts.tv_nsec += (remaining_timeout % eb_nsec_per_sec);
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
            eb_nsec elapsed = eb_time_now() - start_time;
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

// TODO: update comments
// TODO: standardize assertion indentation

#pragma mark - Types -
typedef struct {
    eb_spinlock lock;
    size_t cap;
    size_t len;
    eb_port *ports;
} *port_list;

static inline void port_list_free(port_list l);

/* Creates a new empty list */
static inline port_list port_list_alloc(size_t cap) {
    assert(cap > 0);
    
    port_list result = malloc(sizeof(*result));
        eb_assert_or_recover(result, goto failed);
    
    result->lock = EB_SPINLOCK_INIT;
    result->cap = cap;
    result->len = 0;
    result->ports = malloc(cap * sizeof(*(result->ports)));
        eb_assert_or_recover(result->ports, goto failed);
    
    return result;
    failed: {
        port_list_free(result);
        return NULL;
    }
}

/* Releases every port in the list, and frees the list itself */
static inline void port_list_free(port_list l) {
    /* Intentionally allowing l==NULL */
    if (!l) {
        return;
    }
    
    /* Release each port in our list */
    for (size_t i = 0; i < l->len; i++) {
        eb_port_release(l->ports[i]);
    }
    
    free(l->ports);
    l->ports = NULL;
    
    free(l);
    l = NULL;
}

/* Add a port to the end of the list, expanding the buffer as necessary */
static inline void port_list_add(port_list l, eb_port p) {
    assert(l);
    assert(p);
    
    /* First retain the port! */
    eb_port_retain(p);
    
    eb_spinlock_lock(&l->lock);
        /* Sanity-check that the list's length is less than its capacity */
        eb_assert_or_bail(l->len <= l->cap, "Sanity check failed");
        
        /* Expand the list's buffer if it's full */
        if (l->len == l->cap) {
            l->cap *= 2;
            // TODO: bad idea to call realloc() inside of a spinlock, but how should we fix it?
            // having our ports stored in a statically-sized array would arbitrarily limit us
            // to a certain number of ports, and that would suck...
            l->ports = realloc(l->ports, l->cap * sizeof(*(l->ports)));
            // TODO: handle allocation failures better
            eb_assert_or_recover(l->ports, return);
        }
        
        l->ports[l->len] = p;
        l->len++;
    eb_spinlock_unlock(&l->lock);
}

/* Remove the first occurence of 'p' in the list. Returns whether a port was actually removed. */
static inline bool port_list_rm(port_list l, eb_port p) {
    assert(l);
    assert(p);
    
    bool result = false;
    eb_spinlock_lock(&l->lock);
        /* Sanity-check that the list's length is less than its capacity */
        eb_assert_or_bail(l->len <= l->cap, "Sanity-check failed");
        
        /* Search for first occurence of the given port. If we find it, release it and move the last port in the list into the hole. */
        for (size_t i = 0; i < l->len; i++) {
            if (l->ports[i] == p) {
                /* Move the last element in the port list into the now-vacant spot */
                l->ports[i] = l->ports[l->len-1];
                /* Decrement the buffer length */
                l->len--;
                result = true;
                break;
            }
        }
    eb_spinlock_unlock(&l->lock);
    
    if (result) {
        /* Release the port, but do so outside of the spinlock because releasing does some stuff that might not be quick. */
        eb_port_release(p);
    }
    
    return result;
}

/* Signal the first port in the list that isn't 'ignore' */
static inline void port_list_signal_first(const port_list l, eb_port ignore) {
    assert(l);
    
    eb_port p = NULL;
    eb_spinlock_lock(&l->lock);
        for (size_t i = 0; i < l->len; i++) {
            if (l->ports[i] != ignore) {
                p = eb_port_retain(l->ports[i]);
                break;
            }
        }
    eb_spinlock_unlock(&l->lock);
    
    if (p) {
        eb_port_signal(p);
        eb_port_release(p);
        p = NULL;
    }
}

enum {
    /* Buffered/unbuffered channel states */
    chanstate_open,
    chanstate_closed,
    /* Unbuffered channel states */
    chanstate_send,
    chanstate_recv,
    chanstate_ack,
    chanstate_done,
    chanstate_cancelled
}; typedef int32_t chanstate;

typedef struct {
    eb_chan_op *const *ops;
    size_t nops;
    bool *cleanup_ops;
    
    eb_nsec timeout;
    eb_port port;
} do_state;

struct eb_chan {
    unsigned int retain_count;
    eb_spinlock lock;
    chanstate state;
    
    port_list sends;
    port_list recvs;
    
    /* Buffered ivars */
    size_t buf_cap;
    size_t buf_len;
    size_t buf_idx;
    const void **buf;
    
    /* Unbuffered ivars */
    const do_state *unbuf_state;
    eb_chan_op *unbuf_op;
    eb_port unbuf_port;
};

#pragma mark - Channel creation/lifecycle -
static inline void eb_chan_free(eb_chan c) {
    /* Intentionally allowing c==NULL so that this function can be called from eb_chan_create() */
    if (!c) {
        return;
    }
    
    if (c->buf_cap) {
        /* ## Buffered */
        free(c->buf);
        c->buf = NULL;
    }
    
    port_list_free(c->recvs);
    c->recvs = NULL;
    
    port_list_free(c->sends);
    c->sends = NULL;
    
    free(c);
    c = NULL;
}

eb_chan eb_chan_create(size_t buf_cap) {
    static const size_t k_init_buf_cap = 16;
    
    /* Initialize eb_sys so that eb_sys_ncores is valid. */
    eb_sys_init();
    
    /* Using calloc so that the bytes are zeroed. */
    eb_chan c = calloc(1, sizeof(*c));
        eb_assert_or_recover(c, goto failed);
    
    c->retain_count = 1;
    c->lock = EB_SPINLOCK_INIT;
    c->state = chanstate_open;
    
    c->sends = port_list_alloc(k_init_buf_cap);
        eb_assert_or_recover(c->sends, goto failed);
    c->recvs = port_list_alloc(k_init_buf_cap);
        eb_assert_or_recover(c->recvs, goto failed);
    
    if (buf_cap) {
        /* ## Buffered */
        c->buf_cap = buf_cap;
        c->buf_len = 0;
        c->buf_idx = 0;
        c->buf = malloc(c->buf_cap * sizeof(*(c->buf)));
            eb_assert_or_recover(c->buf, goto failed);
    } else {
        /* ## Unbuffered */
        c->unbuf_state = NULL;
        c->unbuf_op = NULL;
        c->unbuf_port = NULL;
    }
    
    /* Issue a memory barrier since we didn't have the lock acquired for our set up (and this channel could theoretically
       be passed to another thread without a barrier, and that'd be bad news...) */
    eb_atomic_barrier();
    
    return c;
    failed: {
        eb_chan_free(c);
        return NULL;
    }
}

eb_chan eb_chan_retain(eb_chan c) {
    assert(c);
    eb_atomic_add(&c->retain_count, 1);
    return c;
}

void eb_chan_release(eb_chan c) {
    assert(c);
    if (eb_atomic_add(&c->retain_count, -1) == 0) {
        eb_chan_free(c);
    }
}

#pragma mark - Channel closing -
eb_chan_ret eb_chan_close(eb_chan c) {
    assert(c);
    
    eb_chan_ret result = eb_chan_ret_stalled;
    while (result == eb_chan_ret_stalled) {
        eb_port signal_port = NULL;
        eb_spinlock_lock(&c->lock);
            if (c->state == chanstate_open) {
                c->state = chanstate_closed;
                result = eb_chan_ret_ok;
            } else if (c->state == chanstate_closed) {
                result = eb_chan_ret_closed;
            } else if (c->state == chanstate_send || c->state == chanstate_recv) {
                if (c->unbuf_port) {
                    signal_port = eb_port_retain(c->unbuf_port);
                }
                c->state = chanstate_closed;
                result = eb_chan_ret_ok;
            }
        eb_spinlock_unlock(&c->lock);
        
        /* Wake up the send/recv */
        if (signal_port) {
            eb_port_signal(signal_port);
            eb_port_release(signal_port);
            signal_port = NULL;
        }
    }
    
    if (result == eb_chan_ret_ok) {
        /* Wake up the sends/recvs so that they see the channel's now closed */
        port_list_signal_first(c->sends, NULL);
        port_list_signal_first(c->recvs, NULL);
    }
    
    return result;
}

#pragma mark - Getters -
size_t eb_chan_buf_cap(eb_chan c) {
    assert(c);
    return c->buf_cap;
}

size_t eb_chan_buf_len(eb_chan c) {
    assert(c);
    
    /* buf_len is only valid if the channel's buffered */
    if (!c->buf_cap) {
        return 0;
    }
    
    size_t r = 0;
    eb_spinlock_lock(&c->lock);
        r = c->buf_len;
    eb_spinlock_unlock(&c->lock);
    return r;
}

#pragma mark - Performing operations -
enum {
    op_result_complete,     /* The op completed and the caller should return */
    op_result_next,         /* The op couldn't make any progress and the caller should move on to the next op */
    op_result_retry,        /* The channel's busy and we should try the op again */
}; typedef unsigned int op_result;

static inline void cleanup_ops(const do_state *state) {
        assert(state);
    
    for (size_t i = 0; i < state->nops; i++) {
        if (state->cleanup_ops[i]) {
            eb_chan_op *op = state->ops[i];
            eb_chan c = op->chan;
            bool signal_send = false;
            bool signal_recv = false;
            eb_spinlock_lock(&c->lock);
                if (c->state == chanstate_send && c->unbuf_op == op) {
                    /* 'op' was in the process of an unbuffered send on the channel, but no recv had arrived
                       yet, so reset state to _open. */
                    c->state = chanstate_open;
                    signal_send = true;
                } else if (c->state == chanstate_recv && c->unbuf_op == op) {
                    /* 'op' was in the process of an unbuffered recv on the channel, but no send had arrived
                       yet, so reset state to _open. */
                    c->state = chanstate_open;
                    signal_recv = true;
                } else if (c->state == chanstate_ack && c->unbuf_op == op) {
                    /* A counterpart acknowledged 'op' but, but 'op' isn't the one that completed in our select() call, so we're cancelling. */
                    c->state = chanstate_cancelled;
                }
            eb_spinlock_unlock(&c->lock);
            
            if (signal_send) {
                port_list_signal_first(c->sends, state->port);
            }
            
            if (signal_recv) {
                port_list_signal_first(c->recvs, state->port);
            }
            
            state->cleanup_ops[i] = false;
        }
    }
}

static inline op_result send_buf(const do_state *state, eb_chan_op *op, size_t op_idx) {
        assert(state);
        assert(op);
        assert(op->chan);
    
    eb_chan c = op->chan;
    op_result result = op_result_next;
    
    if (c->buf_len < c->buf_cap || c->state == chanstate_closed) {
        /* It looks like our channel's in an acceptable state, so try to acquire the lock */
        if (eb_spinlock_try(&c->lock)) {
            /* Sanity-check the channel's state */
            eb_assert_or_bail(c->state == chanstate_open || c->state == chanstate_closed, "Invalid channel state");
            
            bool signal_recv = false;
            if (c->state == chanstate_closed) {
                /* ## Sending, buffered, channel closed */
                /* Set our op's state and our return value */
                op->open = false;
                result = op_result_complete;
            } else if (c->buf_len < c->buf_cap) {
                /* ## Sending, buffered, channel open, buffer has space */
                /* Notify the channel's recvs if our buffer is going from empty to non-empty */
                signal_recv = (!c->buf_len);
                /* Add the value to the buffer */
                size_t idx = (c->buf_idx + c->buf_len) % c->buf_cap;
                c->buf[idx] = op->val;
                c->buf_len++;
                /* Set our op's state and our return value */
                op->open = true;
                result = op_result_complete;
            }
            
            eb_spinlock_unlock(&c->lock);
            
            if (signal_recv) {
                port_list_signal_first(c->recvs, state->port);
            }
        } else {
            result = op_result_retry;
        }
    }
    
    return result;
}

static inline op_result recv_buf(const do_state *state, eb_chan_op *op, size_t op_idx) {
        assert(state);
        assert(op);
        assert(op->chan);
    
    eb_chan c = op->chan;
    op_result result = op_result_next;
    
    if (c->buf_len || c->state == chanstate_closed) {
        if (eb_spinlock_try(&c->lock)) {
            /* Sanity-check the channel's state */
            eb_assert_or_bail(c->state == chanstate_open || c->state == chanstate_closed, "Invalid channel state");
            
            bool signal_send = false;
            if (c->buf_len) {
                /* ## Receiving, buffered, buffer non-empty */
                /* Notify the channel's sends if our buffer is going from full to not-full */
                signal_send = (c->buf_len == c->buf_cap);
                /* Set our op's state and our return value */
                op->open = true;
                op->val = c->buf[c->buf_idx];
                result = op_result_complete;
                /* Update chan's buffer. (Updating buf_idx needs to come after we use it!) */
                c->buf_len--;
                c->buf_idx = (c->buf_idx + 1) % c->buf_cap;
            } else if (c->state == chanstate_closed) {
                /* ## Receiving, buffered, buffer empty, channel closed */
                /* Set our op's state and our return value */
                op->open = false;
                op->val = NULL;
                result = op_result_complete;
            }
            
            eb_spinlock_unlock(&c->lock);
            
            if (signal_send) {
                port_list_signal_first(c->sends, state->port);
            }
        } else {
            result = op_result_retry;
        }
    }
    
    return result;
}

static inline op_result send_unbuf(const do_state *state, eb_chan_op *op, size_t op_idx) {
        assert(state);
        assert(op);
        assert(op->chan);
    
    eb_chan c = op->chan;
    op_result result = op_result_next;
    
    if ((c->state == chanstate_open && state->timeout != eb_nsec_zero) ||
        c->state == chanstate_closed ||
        (c->state == chanstate_send && c->unbuf_op == op) ||
        (c->state == chanstate_recv && c->unbuf_state != state) ||
        (c->state == chanstate_ack && c->unbuf_op == op)) {
        
        /* It looks like our channel's in an acceptable state, so try to acquire the lock */
        if (eb_spinlock_try(&c->lock)) {
            /* Reset the cleanup state since we acquired the lock and are actually getting a look at the channel's state */
            state->cleanup_ops[op_idx] = false;
            
            bool signal_recv = false;
            if (c->state == chanstate_open && state->timeout != eb_nsec_zero) {
                c->state = chanstate_send;
                c->unbuf_state = state;
                c->unbuf_op = op;
                c->unbuf_port = state->port;
                /* We need to cleanup after this since we put it in the _send state! */
                state->cleanup_ops[op_idx] = true;
                /* Signal a recv since one of them can continue now */
                signal_recv = true;
            } else if (c->state == chanstate_closed) {
                /* Set our op's state and our return value */
                op->open = false;
                result = op_result_complete;
            } else if (c->state == chanstate_send && c->unbuf_op == op) {
                /* We own the send op that's in progress, so assign chan's unbuf_port */
                /* Verify that the unbuf_state matches our 'id' parameter. If this assertion fails, it means there's likely
                   one eb_chan_op being shared by multiple threads, which isn't allowed. */
                eb_assert_or_bail(c->unbuf_state == state, "unbuf_state invalid");
                /* Assign the port */
                c->unbuf_port = state->port;
                /* We need to cleanup after this since we put it in the _send state! */
                state->cleanup_ops[op_idx] = true;
            } else if (c->state == chanstate_recv && c->unbuf_state != state) {
                /* We verified (immediately above) that the recv isn't part of the same op pool (we can't do unbuffered
                   sends/recvs from the same _do() call) */
                
                /* Sanity check -- make sure the op is a recv */
                eb_assert_or_bail(!c->unbuf_op->send, "Op isn't a recv as expected");
                
                /* Set the recv op's value. This needs to happen before we transition out of the _recv state, otherwise the unbuf_op may no longer be valid! */
                c->unbuf_op->val = op->val;
                /* Acknowledge the receive */
                c->state = chanstate_ack;
                /* Get a reference to the unbuf_port that needs to be signaled */
                eb_port signal_port = (c->unbuf_port ? eb_port_retain(c->unbuf_port) : NULL);
                eb_spinlock_unlock(&c->lock);
                
                /* Wake up the recv */
                if (signal_port) {
                    eb_port_signal(signal_port);
                    eb_port_release(signal_port);
                    signal_port = NULL;
                }
                
                /* We have to cleanup all our ops here to cancel any outstanding unbuffered send/recvs, to avoid a deadlock
                   situation that arises when another _do() is waiting on our _do() to complete, but it never does because
                   we're about to wait for the other _do() to complete. */
                cleanup_ops(state);
                
                for (;;) {
                    if (*((volatile chanstate *)&c->state) != chanstate_ack) {
                        eb_spinlock_lock(&c->lock);
                            if (c->state == chanstate_done) {
                                /* Reset the channel state back to _open */
                                c->state = chanstate_open;
                                /* We reset our state to _open, so signal a send since it can proceed now. */
                                signal_recv = true;
                                /* Set our op's state and our return value */
                                op->open = true;
                                result = op_result_complete;
                                /* Breaking here so that we skip the _unlock() call, because we unlock the spinlock outside
                                   of our large if-statement. */
                                break;
                            } else if (c->state == chanstate_cancelled) {
                                /* Reset the channel state back to _open */
                                c->state = chanstate_open;
                                /* As long as we're not polling, we should try the op again */
                                if (state->timeout != eb_nsec_zero) {
                                    result = op_result_retry;
                                } else {
                                    /* We're not telling the caller to retry, so signal a send since it can proceed now. */
                                    signal_recv = true;
                                }
                                /* Breaking here so that we skip the _unlock() call, because we unlock the spinlock outside
                                   of our large if-statement. */
                                break;
                            }
                        eb_spinlock_unlock(&c->lock);
                    } else if (eb_sys_ncores == 1) {
                        /* On uniprocessor machines, yield to the scheduler because we can't continue until another
                           thread updates the channel's state. */
                        sched_yield();
                    }
                }
            } else if (c->state == chanstate_ack && c->unbuf_op == op) {
                /* A recv acknowledged our send! */
                /* Verify that the unbuf_state matches our 'id' parameter. If this assertion fails, it means there's likely
                   one eb_chan_op being shared by multiple threads, which isn't allowed. */
                eb_assert_or_bail(c->unbuf_state == state, "unbuf_state invalid");
                /* A recv is polling for chan's state to change, so update it to signal that we're done sending! */
                c->state = chanstate_done;
                /* Set our op's state and our return value */
                op->open = true;
                result = op_result_complete;
            }
            
            eb_spinlock_unlock(&c->lock);
            
            if (signal_recv) {
                port_list_signal_first(c->recvs, state->port);
            }
        } else {
            result = op_result_retry;
        }
    }
    
    return result;
}

static inline op_result recv_unbuf(const do_state *state, eb_chan_op *op, size_t op_idx) {
        assert(state);
        assert(op);
        assert(op->chan);
    
    eb_chan c = op->chan;
    op_result result = op_result_next;
    
    if ((c->state == chanstate_open && state->timeout != eb_nsec_zero) ||
        c->state == chanstate_closed ||
        (c->state == chanstate_send && c->unbuf_state != state) ||
        (c->state == chanstate_recv && c->unbuf_op == op) ||
        (c->state == chanstate_ack && c->unbuf_op == op)) {
        
        /* It looks like our channel's in an acceptable state, so try to acquire the lock */
        if (eb_spinlock_try(&c->lock)) {
            /* Reset the cleanup state since we acquired the lock and are actually getting a look at the channel's state */
            state->cleanup_ops[op_idx] = false;
            
            bool signal_send = false;
            if (c->state == chanstate_open && state->timeout != eb_nsec_zero) {
                c->state = chanstate_recv;
                c->unbuf_state = state;
                c->unbuf_op = op;
                c->unbuf_port = state->port;
                /* We need to cleanup after this since we put it in the _send state! */
                state->cleanup_ops[op_idx] = true;
                /* Signal a send since one of them can continue now */
                signal_send = true;
            } else if (c->state == chanstate_closed) {
                /* Set our op's state and our return value */
                op->open = false;
                op->val = NULL;
                result = op_result_complete;
            } else if (c->state == chanstate_send && c->unbuf_state != state) {
                /* We verified (immediately above) that the send isn't part of the same op pool (we can't do unbuffered
                   sends/recvs from the same _do() call) */
                
                /* Sanity check -- make sure the op is a send */
                eb_assert_or_bail(c->unbuf_op->send, "Op isn't a send as expected");
                
                /* Get the op's value. This needs to happen before we transition out of the _send state, otherwise the unbuf_op may no longer be valid! */
                op->val = c->unbuf_op->val;
                /* Acknowledge the send */
                c->state = chanstate_ack;
                /* Get a reference to the unbuf_port that needs to be signaled */
                eb_port signal_port = (c->unbuf_port ? eb_port_retain(c->unbuf_port) : NULL);
                eb_spinlock_unlock(&c->lock);
                
                /* Wake up the send */
                if (signal_port) {
                    eb_port_signal(signal_port);
                    eb_port_release(signal_port);
                    signal_port = NULL;
                }
                
                /* We have to cleanup all our ops here to cancel any outstanding unbuffered send/recvs, to avoid a deadlock
                   situation that arises when another _do() is waiting on our _do() to complete, but it never does because
                   we're about to wait for the other _do() to complete. */
                cleanup_ops(state);
                
                for (;;) {
                    if (*((volatile chanstate *)&c->state) != chanstate_ack) {
                        eb_spinlock_lock(&c->lock);
                            if (c->state == chanstate_done) {
                                /* Reset the channel state back to _open */
                                c->state = chanstate_open;
                                /* We reset our state to _open, so signal a recv since it can proceed now. */
                                signal_send = true;
                                /* Set our op's state and our return value */
                                op->open = true;
                                result = op_result_complete;
                                /* Breaking here so that we skip the _unlock() call, because we unlock the spinlock outside
                                   of our large if-statement. */
                                break;
                            } else if (c->state == chanstate_cancelled) {
                                /* Reset the channel state back to _open */
                                c->state = chanstate_open;
                                /* As long as we're not polling, we should try the op again */
                                if (state->timeout != eb_nsec_zero) {
                                    result = op_result_retry;
                                } else {
                                    /* We're not telling the caller to retry, so signal a recv since it can proceed now. */
                                    signal_send = true;
                                }
                                /* Breaking here so that we skip the _unlock() call, because we unlock the spinlock outside
                                   of our large if-statement. */
                                break;
                            }
                        eb_spinlock_unlock(&c->lock);
                    } else if (eb_sys_ncores == 1) {
                        /* On uniprocessor machines, yield to the scheduler because we can't continue until another
                           thread updates the channel's state. */
                        sched_yield();
                    }
                }
            } else if (c->state == chanstate_recv && c->unbuf_op == op) {
                /* We own the recv op that's in progress, so assign chan's unbuf_port */
                /* Verify that the _recv_id matches our 'id' parameter. If this assertion fails, it means there's likely
                   one eb_chan_op being shared by multiple threads, which isn't allowed. */
                eb_assert_or_bail(c->unbuf_state == state, "unbuf_state invalid");
                /* Assign the port */
                c->unbuf_port = state->port;
                /* We need to cleanup after this since we put it in the _send state! */
                state->cleanup_ops[op_idx] = true;
            } else if (c->state == chanstate_ack && c->unbuf_op == op) {
                /* A send acknowledged our recv! */
                /* Verify that the unbuf_state matches our 'id' parameter. If this assertion fails, it means there's likely
                   one eb_chan_op being shared by multiple threads, which isn't allowed. */
                eb_assert_or_bail(c->unbuf_state == state, "unbuf_state invalid");
                /* A send is polling for chan's state to change, so update it to signal that we're done sending! */
                c->state = chanstate_done;
                /* Set our op's state and our return value */
                op->open = true;
                result = op_result_complete;
            }
            
            eb_spinlock_unlock(&c->lock);
            
            if (signal_send) {
                port_list_signal_first(c->sends, state->port);
            }
        } else {
            result = op_result_retry;
        }
    }
    
    return result;
}

static inline op_result try_op(const do_state *state, eb_chan_op *op, size_t op_idx) {
        assert(state);
        assert(op);
    
    eb_chan c = op->chan;
    if (c) {
        if (op->send) {
            /* ## Send */
            return (c->buf_cap ? send_buf(state, op, op_idx) : send_unbuf(state, op, op_idx));
        } else {
            /* ## Receive */
            return (c->buf_cap ? recv_buf(state, op, op_idx) : recv_unbuf(state, op, op_idx));
        }
    }
    return op_result_next;
}

eb_chan_ret eb_chan_send(eb_chan c, const void *val) {
    eb_chan_op op = eb_chan_op_send(c, val);
    eb_assert_or_bail(eb_chan_select(eb_nsec_forever, &op) == &op, "Invalid select() return value");
    return (op.open ? eb_chan_ret_ok : eb_chan_ret_closed);
}

eb_chan_ret eb_chan_try_send(eb_chan c, const void *val) {
    eb_chan_op op = eb_chan_op_send(c, val);
    eb_chan_op *r = eb_chan_select(eb_nsec_zero, &op);
    eb_assert_or_bail(r == NULL || r == &op, "Invalid select() return value");
    if (r) {
        return (op.open ? eb_chan_ret_ok : eb_chan_ret_closed);
    }
    return eb_chan_ret_stalled;
}

eb_chan_ret eb_chan_recv(eb_chan c, const void **val) {
    eb_chan_op op = eb_chan_op_recv(c);
    eb_assert_or_bail(eb_chan_select(eb_nsec_forever, &op) == &op, "Invalid select() return value");
    if (op.open && val) {
        *val = op.val;
    }
    return (op.open ? eb_chan_ret_ok : eb_chan_ret_closed);
}

eb_chan_ret eb_chan_try_recv(eb_chan c, const void **val) {
    eb_chan_op op = eb_chan_op_recv(c);
    eb_chan_op *r = eb_chan_select(eb_nsec_zero, &op);
    eb_assert_or_bail(r == NULL || r == &op, "Invalid select() return value");
    if (r) {
        if (op.open && val) {
            *val = op.val;
        }
        return (op.open ? eb_chan_ret_ok : eb_chan_ret_closed);
    }
    return eb_chan_ret_stalled;
}

#pragma mark - Multiplexing -
#define next_idx(nops, delta, idx) (delta == 1 && idx == nops-1 ? 0 : ((delta == -1 && idx == 0) ? nops-1 : idx+delta))
eb_chan_op *eb_chan_select_list(eb_nsec timeout, eb_chan_op *const ops[], size_t nops) {
        assert(!nops || ops);
    
    const size_t k_attempt_multiplier = (eb_sys_ncores == 1 ? 1 : 500);
    eb_nsec start_time = 0;
    size_t idx_start = 0;
    int8_t idx_delta = 0;
    if (nops > 1) {
        /* Assign idx_start/idx_delta, which control the op pseudo-randomization */
        start_time = eb_time_now();
        idx_start = (start_time/1000)%nops;
        idx_delta = (!((start_time/10000)%2) ? 1 : -1);
    }
    
    bool co[nops];
    memset(co, 0, sizeof(co));
    
    eb_chan_op *result = NULL;
    do_state state = {
        .ops = ops,
        .nops = nops,
        .cleanup_ops = co,
        .timeout = timeout,
        .port = NULL};
    
    if (timeout == eb_nsec_zero) {
        /* ## timeout == 0: try every op exactly once; if none of them can proceed, return NULL. */
        for (size_t i = 0, idx = idx_start; i < nops; i++, idx = next_idx(nops, idx_delta, idx)) {
            eb_chan_op *op = ops[idx];
            op_result r;
            while ((r = try_op(&state, op, idx)) == op_result_retry) {
                if (eb_sys_ncores == 1) {
                    /* On uniprocessor machines, yield to the scheduler because we can't continue until another
                       thread updates the channel's state. */
                    sched_yield();
                }
            }
            
            /* If the op completed, we need to exit! */
            if (r == op_result_complete) {
                result = op;
                goto cleanup;
            }
        }
    } else {
        /* ## timeout != 0 */
        if (timeout != eb_nsec_forever && !start_time) {
            start_time = eb_time_now();
        }
        
        for (;;) {
            /* ## Fast path: loop over our operations to see if one of them was able to send/receive. (If not,
               we'll enter the slow path where we put our thread to sleep until we're signaled.) */
            for (size_t i = 0, idx = idx_start; i < k_attempt_multiplier*nops; i++, idx = next_idx(nops, idx_delta, idx)) {
                eb_chan_op *op = ops[idx];
                op_result r = try_op(&state, op, idx);
                /* If the op completed, we need to exit! */
                if (r == op_result_complete) {
                    result = op;
                    goto cleanup;
                }
            }
            
            /* ## Slow path: we weren't able to find an operation that could send/receive, so we'll create a
               port to receive notifications on and put this thread to sleep until someone wakes us up. */
            if (!state.port) {
                /* Create our port that we'll attach to channels so that we can be notified when events occur. */
                state.port = eb_port_create();
                    eb_assert_or_recover(state.port, goto cleanup);
                
                /* Register our port for the appropriate notifications on every channel. */
                /* This adds 'port' to the channel's sends/recvs (depending on the op), which we clean up at the
                   end of this function. */
                for (size_t i = 0; i < nops; i++) {
                    eb_chan_op *op = ops[i];
                    eb_chan c = op->chan;
                    if (c) {
                        port_list_add((op->send ? c->sends : c->recvs), state.port);
                    }
                }
            }
            
            /* Before we go to sleep, call try_op() for every op until we get a non-busy return value. This way we'll ensure
               that no op is actually able to be performed, and we'll also ensure that 'port' is registered as the 'unbuf_port'
               for the necessary channels. */
            for (size_t i = 0, idx = idx_start; i < nops; i++, idx = next_idx(nops, idx_delta, idx)) {
                eb_chan_op *op = ops[idx];
                op_result r;
                while ((r = try_op(&state, op, idx)) == op_result_retry) {
                    if (eb_sys_ncores == 1) {
                        /* On uniprocessor machines, yield to the scheduler because we can't continue until another
                           thread updates the channel's state. */
                        sched_yield();
                    }
                }
                
                /* If the op completed, we need to exit! */
                if (r == op_result_complete) {
                    result = op;
                    goto cleanup;
                }
            }
            
            eb_nsec wait_timeout = eb_nsec_forever;
            if (timeout != eb_nsec_forever) {
                /* If we have a timeout, determine how much time has elapsed, because we may have timed-out. */
                eb_nsec elapsed = eb_time_now() - start_time;
                /* Check if we timed-out */
                if (elapsed < timeout) {
                    wait_timeout = timeout - elapsed;
                } else {
                    goto cleanup;
                }
            }
            
            /* Put our thread to sleep until someone alerts us of an event */
            eb_port_wait(state.port, wait_timeout);
        }
    }
    
    /* Cleanup! */
    cleanup: {
        if (state.port) {
            for (size_t i = 0; i < nops; i++) {
                eb_chan_op *op = ops[i];
                eb_chan c = op->chan;
                if (c) {
                    port_list ports = (op->send ? c->sends : c->recvs);
                    port_list_rm(ports, state.port);
                    port_list_signal_first(ports, state.port);
                }
            }
        }
        
        cleanup_ops(&state);
        
        if (state.port) {
            eb_port_release(state.port);
            state.port = NULL;
        }
    }
    
    return result;
}
