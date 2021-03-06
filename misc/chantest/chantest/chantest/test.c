#include "eb_chan.h"
#include "testglue.h"

#define NTRIALS 1000000

int main() {
    eb_nsec eb_time_now();
    eb_chan c = eb_chan_create(0);
    
    // start the receivier
    go(
        for (;;) {
            assert(eb_chan_recv(c, NULL) == eb_chan_res_ok);
        }
    );
    
    // start the sender
    go(
        for (;;) {
            eb_nsec startTime = eb_time_now();
            for (size_t i = 0; i < NTRIALS; i++) {
                assert(eb_chan_send(c, (void*)(intptr_t)i) == eb_chan_res_ok);
            }
            printf("%ju iterations: %f seconds\n", (uintmax_t)NTRIALS, (eb_time_now()-startTime)/(double)eb_nsec_per_sec);
        }
    );
    
    for (;;) {
        sleep(-1);
    }
    
    return 0;
}











//#include "testglue.h"
//#include "eb_chan.h"
//#include "eb_time.h"
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
//        if (eb_chan_try_recv(gChan, NULL) == eb_chan_res_ok) {
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
//        assert(eb_chan_recv(gChan, NULL) == eb_chan_res_ok);
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
////    assert(eb_chan_recv(gChan, NULL, eb_nsec_forever) == eb_chan_res_ok);
////    eb_nsec startTime = eb_time_now();
////    for (size_t i = 1; i < NTRIALS; i++) {
////        assert(eb_chan_recv(gChan, NULL, eb_nsec_forever) == eb_chan_res_ok);
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
//    eb_chan_op send = eb_chan_op_send(gChan, "hallo");
//    eb_chan_op recv = eb_chan_op_recv(gChan);
//    assert(eb_chan_select(eb_nsec_forever, &send, &recv));
//    
//    eb_nsec startTime = eb_time_now();
//    for (size_t i = 1; i < NTRIALS; i++) {
//        assert(eb_chan_select(eb_nsec_forever, &send, &recv));
//    }
//    
//    printf("elapsed: %f (%ju iterations)\n", ((double)(eb_time_now() - startTime) / eb_nsec_per_sec), (uintmax_t)NTRIALS);
////    exit(0);
//}
//
//void timeoutTest()
//{
//    eb_chan_op recv = eb_chan_op_recv(gChan);
//    eb_nsec startTime = eb_time_now();
//    eb_chan_select(2.5 * eb_nsec_per_sec, &recv);
//    printf("elapsed: %f (%ju iterations)\n", ((double)(eb_time_now() - startTime) / eb_nsec_per_sec), (uintmax_t)NTRIALS);
//    
//    exit(0);
//}
//
//void deadlock(eb_chan a, eb_chan b) {
//    eb_chan_op send = eb_chan_op_send(a, "xxx");
//    eb_chan_op recv = eb_chan_op_recv(b);
//    for (;;) {
//        eb_chan_op *r = eb_chan_select(eb_nsec_forever, &send, &recv);
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
//    
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
