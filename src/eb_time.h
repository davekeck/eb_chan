#pragma once
#include "eb_nsec.h"

/* Returns the number of nanoseconds since an arbitrary point in time (usually the machine's boot time) */
eb_nsec eb_time_now();
