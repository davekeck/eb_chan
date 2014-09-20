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
        eb_chan_op crecv = eb_chan_recv_op(c);
        eb_chan_op drecv = eb_chan_recv_op(dummy);
        eb_chan_op *r = eb_chan_do(eb_nsec_forever, &crecv, &drecv);
        if (r == &crecv) {
            // nothing
        } else {
            // dummy or NULL: bad
            abort();
        }
	}
}

size_t get_memory_usage(void) {
    // based on http://stackoverflow.com/questions/18389581/memory-used-by-a-process-under-mac-os-x
    mach_msg_type_number_t outCount = MACH_TASK_BASIC_INFO_COUNT;
    mach_task_basic_info_data_t taskInfo = {.virtual_size = 0};
    
    kern_return_t r = task_info(mach_task_self(), MACH_TASK_BASIC_INFO, (task_info_t)&taskInfo, &outCount);
    assert(r == KERN_SUCCESS);
    
    return taskInfo.virtual_size;
}

#define iterations 100000

int main() {
    eb_chan c = eb_chan_create(0);
    eb_chan dummy = eb_chan_create(0);

	// warm up
	go( sender(c, iterations) );
	receiver(c, dummy, iterations);
    size_t mem1 = get_memory_usage();

	// second time shouldn't increase footprint by much
	go( sender(c, iterations) );
	receiver(c, dummy, iterations);
    size_t mem2 = get_memory_usage();
    
	// Be careful to avoid wraparound.
	if (mem2 > mem1 && mem2-mem1 >= iterations) {
		printf("BUG: too much memory for %ju selects: %ju", (uintmax_t)iterations, (uintmax_t)(mem2 - mem1));
	}
    
    return 0;
}