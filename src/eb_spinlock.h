#pragma once
#include <stdbool.h>
#include <sched.h>
#include "eb_sys.h"

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
