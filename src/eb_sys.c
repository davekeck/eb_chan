#include "eb_sys.h"
#include "eb_assert.h"
#include "eb_atomic.h"

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
