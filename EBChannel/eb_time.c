#include "eb_time.h"
#include <stdint.h>
#include <stdlib.h>
#include <mach/mach.h>
#include "eb_assert.h"
#include "eb_atomic.h"

eb_time eb_time_now() {
    return mach_absolute_time();
}

eb_nsecs eb_time_nsecs_between(eb_time start, eb_time end) {
    /* Initialize k_timebase_info, thread-safely */
    static mach_timebase_info_t k_timebase_info = NULL;
    if (!k_timebase_info) {
        mach_timebase_info_t timebase_info = malloc(sizeof(*timebase_info));
        kern_return_t r = mach_timebase_info(timebase_info);
            eb_assert_or_recover(r == KERN_SUCCESS, eb_no_op);
        
        /* Make sure the writes to 'timebase_info' are complete before we assign k_timebase_info */
        eb_atomic_barrier();
        
        if (!eb_atomic_compare_and_swap(&k_timebase_info, NULL, timebase_info)) {
            free(timebase_info);
            timebase_info = NULL;
        }
    }
    
    return (((end - start) * k_timebase_info->numer) / k_timebase_info->denom);
}