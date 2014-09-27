#pragma once

#define eb_atomic_add(ptr, delta) __sync_add_and_fetch(ptr, delta) /* Returns the new value */
#define eb_atomic_compare_and_swap(ptr, old, new) __sync_bool_compare_and_swap(ptr, old, new)
#define eb_atomic_barrier() __sync_synchronize()
