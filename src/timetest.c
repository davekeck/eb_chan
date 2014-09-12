#include "eb_time.h"
#include <stdio.h>
#include <stdint.h>

int main() {
    eb_nsecs now = eb_time_now();
    printf("%ju\n", (uintmax_t)now);
    return 0;
}

//#define _POSIX_C_SOURCE 200112L
//#include <time.h>
//
//int main() {
//	struct timespec ts;
//	int r = clock_gettime(0, &ts);
//	return 0;
//}
