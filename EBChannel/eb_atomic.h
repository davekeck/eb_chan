#include <assert.h>

typedef int eb_atomic_int;
#define eb_atomic_int_add(v, delta) __sync_fetch_and_add(v, delta) /* Returns the old value */
#define eb_atomic_barrier() __sync_synchronize()

typedef int eb_spinlock; /* Initialize with EB_SPINLOCK_INIT */
#define EB_SPINLOCK_INIT 0
#define eb_spinlock_try(l) __sync_lock_test_and_set(l, 1) == 0
#define eb_spinlock_lock(l) while (!eb_spinlock_try(l))
#define eb_spinlock_unlock(l) __sync_lock_release(l)

//#define eb_spinlock_try(l) __sync_bool_compare_and_swap(l, 0, 1)
//#define eb_spinlock_lock(l) while (!eb_spinlock_try(l))
//#define eb_spinlock_unlock(l) __sync_bool_compare_and_swap(l, 1, 0)
