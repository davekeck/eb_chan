#include "eb_time.h"
#include <stdint.h>
#include <stdlib.h>
#include <mach/mach.h>
#include "eb_assert.h"
#include "eb_atomic.h"

#define DARWIN __MACH__
#define LINUX __linux__

eb_nsecs eb_time_now() {
#if DARWIN
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
#elif LINUX
    struct timespec ts;
    int r = clock_gettime(CLOCK_MONOTONIC, &ts);
        eb_assert_or_recover(!r, return 0);
    return ((uint64_t)ts.tv_sec * NSEC_PER_SEC) + ts.tv_nsec;
#endif
}
