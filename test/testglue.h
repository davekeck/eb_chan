#pragma once
#include "eb_chan.h"
#include <assert.h>
#include <stdlib.h>
#include <pthread.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <strings.h>
#include <Block.h>

#if __MACH__
    #define DARWIN 1
    #include <mach/mach.h>
    #include <mach/message.h>
    #include <mach/kern_return.h>
    #include <mach/task_info.h>
#elif __linux__
    #define LINUX 1
#endif

typedef void (^VoidBlock)();
void spawnThread(VoidBlock b);
#define go(a) spawnThread(^{ a ;});

#define tg_atomic_add(ptr, delta) __sync_add_and_fetch(ptr, delta) /* Returns the new value */
#define tg_atomic_compare_and_swap(ptr, old, new) __sync_bool_compare_and_swap(ptr, old, new)
#define tg_atomic_barrier() __sync_synchronize()

typedef int tg_spinlock; /* Initialized with TG_SPINLOCK_INIT */
#define TG_SPINLOCK_INIT 0
#define tg_spinlock_try(l) tg_atomic_compare_and_swap(l, 0, 1)
#define tg_spinlock_lock(l) while (!tg_spinlock_try(l))
#define tg_spinlock_unlock(l) tg_atomic_compare_and_swap(l, 1, 0)

eb_nsec eb_time_now();