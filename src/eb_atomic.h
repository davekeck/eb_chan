#pragma once
#include <assert.h>

#define eb_atomic_add(ptr, delta) __sync_add_and_fetch(ptr, delta) /* Returns the new value */
#define eb_atomic_compare_and_swap(ptr, old, new) __sync_bool_compare_and_swap(ptr, old, new)
#define eb_atomic_barrier() __sync_synchronize()

typedef int eb_spinlock; /* Initialized with EB_SPINLOCK_INIT */
#define EB_SPINLOCK_INIT 0
#define eb_spinlock_try(l) eb_atomic_compare_and_swap(l, 0, 1)
#define eb_spinlock_lock(l) while (!eb_spinlock_try(l))
#define eb_spinlock_unlock(l) eb_atomic_compare_and_swap(l, 1, 0)

//#define eb_spinlock_try(l) __sync_lock_test_and_set(l, 1) == 0
//#define eb_spinlock_lock(l) while (!eb_spinlock_try(l))
//#define eb_spinlock_unlock(l) __sync_lock_release(l)
//
//typedef OSSpinLock eb_spinlock; /* Initialized with EB_SPINLOCK_INIT */
//#define EB_SPINLOCK_INIT OS_SPINLOCK_INIT
//#define eb_spinlock_try(l) OSSpinLockTry(l)
//#define eb_spinlock_lock(l) OSSpinLockLock(l)
//#define eb_spinlock_unlock(l) OSSpinLockUnlock(l)
