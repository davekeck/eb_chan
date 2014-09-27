#pragma once
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
