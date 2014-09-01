#include <stdint.h>

typedef uint64_t eb_nsecs; /* Units of nanoseconds */
#define eb_nsecs_zero 0
#define eb_nsecs_forever UINT64_MAX

/* Returns the number of nanoseconds since an arbitrary point in time (usually the machine's boot time) */
eb_nsecs eb_time_now();
