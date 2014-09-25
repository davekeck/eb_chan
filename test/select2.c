// DONE

// Test that selects do not consume undue memory.

#include "testglue.h"

void sender(eb_chan c, int n) {
	for (int i = 0; i < n; i++) {
        eb_chan_send(c, (void*)1);
	}
}

void receiver(eb_chan c, eb_chan dummy, int n) {
	for (int i = 0; i < n; i++) {
        eb_chan_op crecv = eb_chan_op_recv(c);
        eb_chan_op drecv = eb_chan_op_recv(dummy);
        eb_chan_op *r = eb_chan_select(eb_nsec_forever, &crecv, &drecv);
        if (r == &crecv) {
            // nothing
        } else {
            // dummy or NULL: bad
            abort();
        }
	}
}

size_t get_memory_usage(void) {
    #if DARWIN
        // based on http://stackoverflow.com/questions/18389581/memory-used-by-a-process-under-mac-os-x
        mach_msg_type_number_t outCount = MACH_TASK_BASIC_INFO_COUNT;
        mach_task_basic_info_data_t taskInfo = {.virtual_size = 0};
        
        kern_return_t r = task_info(mach_task_self(), MACH_TASK_BASIC_INFO, (task_info_t)&taskInfo, &outCount);
        assert(r == KERN_SUCCESS);
        
        return taskInfo.virtual_size;
    #elif LINUX
        // based on http://stackoverflow.com/questions/372484/how-do-i-programmatically-check-memory-use-in-a-fairly-portable-way-c-c
        FILE *file = fopen("/proc/self/statm", "r");
        size_t result = 0;
        if (file) {
            fscanf(file, "%zu", &result);
            fclose(file);
            result *= getpagesize();
        }
        return result;
    #endif
}

#define iterations 100000

int main() {
    eb_chan c = eb_chan_create(0);
    eb_chan dummy = eb_chan_create(0);
    
    // warm up
	go( sender(c, iterations) );
	receiver(c, dummy, iterations);
    size_t mem1 = get_memory_usage();
//    printf("mem1: %ju\n", (uintmax_t)mem1);
    
	// second time shouldn't increase footprint by much
	go( sender(c, iterations) );
	receiver(c, dummy, iterations);
    size_t mem2 = get_memory_usage();
//    printf("mem2: %ju\n", (uintmax_t)mem2);
    
	if (mem2 > mem1 && mem2-mem1 >= iterations) {
		printf("BUG: too much memory for %ju selects: %ju\n", (uintmax_t)iterations, (uintmax_t)(mem2 - mem1));
	}
    
    return 0;
}