#include <stdint.h>

typedef uint64_t eb_nsecs; /* Units of nanoseconds */
#define eb_nsecs_zero 0
#define eb_nsecs_forever UINT64_MAX

typedef uint64_t eb_time;
eb_time eb_time_now();
eb_nsecs eb_time_nsecs_between(eb_time start, eb_time end);
