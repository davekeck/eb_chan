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
















//#include "testglue.h"
//#include "eb_chan.h"
//
//#define NTRIALS 1000000
//
//eb_chan gChan = NULL;
//
//void threadDoSend()
//{
//    for (size_t i = 0; i < NTRIALS; i++) {
//        eb_chan_send(gChan, "halla");
////        printf("send\n");
//    }
//}
//
//void threadTryRecv()
//{
//    size_t count = 0;
//    eb_nsec startTime = eb_time_now();
//    for (;;) {
//        if (eb_chan_try_recv(gChan, NULL, NULL)) {
////            printf("recv\n");
//            count++;
//            if (count == NTRIALS) {
//                break;
//            }
//        } else {
////            printf("no recv\n");
//        }
//    }
//    
//    printf("elapsed: %f (%ju iterations)\n", ((double)(eb_time_now() - startTime) / eb_nsec_per_sec), (uintmax_t)NTRIALS);
//    exit(0);
//}
//
//void threadDoRecv()
//{
//    for (size_t i = 0; i < NTRIALS; i++) {
//        assert(eb_chan_recv(gChan, NULL));
//    }
//}
//
//void threadTrySend()
//{
//    size_t count = 0;
//    eb_nsec startTime = eb_time_now();
//    for (;;) {
//        if (eb_chan_try_send(gChan, "hello")) {
//            count++;
//            if (count == NTRIALS) {
//                break;
//            }
//        }
//    }
//    printf("elapsed: %f (%ju iterations)\n", ((double)(eb_time_now() - startTime) / eb_nsec_per_sec), (uintmax_t)NTRIALS);
//    exit(0);
//}
//
//void threadSend()
//{
//    for (size_t i = 0; i < NTRIALS; i++) {
//        eb_chan_send(gChan, "hallo");
//    }
//    
//}
//
//void threadRecv()
//{
////    assert(eb_chan_recv(gChan, NULL, eb_nsec_forever));
////    eb_nsec startTime = eb_time_now();
////    for (size_t i = 1; i < NTRIALS; i++) {
////        assert(eb_chan_recv(gChan, NULL, eb_nsec_forever));
////    }
////    printf("elapsed: %f (%ju iterations)", ((double)(eb_time_now() - startTime) / eb_nsec_per_sec), (uintmax_t)NTRIALS);
////    exit(0);
////    
//    eb_chan_recv(gChan, NULL);
//    eb_nsec startTime = eb_time_now();
//    for (size_t i = 1; i < NTRIALS; i++) {
//        eb_chan_recv(gChan, NULL);
//    }
//    
//    printf("elapsed: %f (%ju iterations)\n", ((double)(eb_time_now() - startTime) / eb_nsec_per_sec), (uintmax_t)NTRIALS);
////    exit(0);//
//}
//
//void thread()
//{
//    eb_chan_op send = eb_chan_send_op(gChan, "hallo");
//    eb_chan_op recv = eb_chan_recv_op(gChan);
//    assert(eb_chan_do(eb_nsec_forever, &send, &recv));
//    
//    eb_nsec startTime = eb_time_now();
//    for (size_t i = 1; i < NTRIALS; i++) {
//        assert(eb_chan_do(eb_nsec_forever, &send, &recv));
//    }
//    
//    printf("elapsed: %f (%ju iterations)\n", ((double)(eb_time_now() - startTime) / eb_nsec_per_sec), (uintmax_t)NTRIALS);
////    exit(0);
//}
//
//void timeoutTest()
//{
//    eb_chan_op recv = eb_chan_recv_op(gChan);
//    eb_nsec startTime = eb_time_now();
//    eb_chan_do(2.5 * eb_nsec_per_sec, &recv);
//    printf("elapsed: %f (%ju iterations)\n", ((double)(eb_time_now() - startTime) / eb_nsec_per_sec), (uintmax_t)NTRIALS);
//    
//    exit(0);
//}
//
//void deadlock(eb_chan a, eb_chan b) {
//    eb_chan_op send = eb_chan_send_op(a, "xxx");
//    eb_chan_op recv = eb_chan_recv_op(b);
//    for (;;) {
//        eb_chan_op *r = eb_chan_do(eb_nsec_forever, &send, &recv);
//        if (r == &send) {
//            printf("send\n");
//        } else if (r == &recv) {
//            printf("recv\n");
//        } else {
//            abort();
//        }
//    }
//}
//
//int main(int argc, const char * argv[])
//{
//    gChan = eb_chan_create(0);
//    
////    go( threadDoSend() );
////    go( threadTryRecv() );
////    
////    go( threadTrySend() );
////    go( threadDoRecv() );
//    
//    go( threadSend() );
//    go( threadSend() );
//    go( threadSend() );
//    
//    go( threadRecv() );
//    go( threadRecv() );
//    go( threadRecv() );
//    
////    go( thread() );
////    go( thread() );
////    go( thread() );
////    go( thread() );
////    go( thread() );
////    go( thread() );
////    go( thread() );
////    go( thread() );
//    
////    // at one time this caused a deadlock
////    eb_chan a = eb_chan_create(0);
////    eb_chan b = eb_chan_create(0);
////    go( deadlock(a,b) );
////    go( deadlock(b,a) );
//    
////    timeoutTest();
//    
//    for (;;) {
//        sleep(-1);
//        printf("SLEEPING\n");
//    }
//    
//    return 0;
//}
