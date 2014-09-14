#pragma once
#include <stdint.h>

typedef uint64_t eb_nsec; /* Units of nanoseconds */
#define eb_nsec_zero UINT64_C(0)
#define eb_nsec_forever UINT64_MAX
#define eb_nsec_per_sec UINT64_C(1000000000)

/* Returns the number of nanoseconds since an arbitrary point in time (usually the machine's boot time) */
eb_nsec eb_time_now();
