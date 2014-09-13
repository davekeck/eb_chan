#include <pthread.h>
#include <math.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "eb_chan.h"

#define NTRIALS 1000000

eb_chan gChan = NULL;

void *threadDoSend(void *a)
{
    eb_chan_op send = eb_chan_send_op(gChan, "halla");
    for (size_t i = 0; i < NTRIALS; i++) {
        assert(eb_chan_do(eb_nsecs_forever, &send));
    }
    return NULL;
}

void *threadTryRecv(void *a)
{
    eb_chan_op recv = eb_chan_recv_op(gChan);
    size_t count = 0;
    eb_nsecs startTime = eb_time_now();
    for (;;) {
        eb_chan_op *op = eb_chan_do(eb_nsecs_zero, &recv);
        if (op == &recv) {
            count++;
            if (count == NTRIALS) {
                break;
            }
        } else {
        }
    }
    
    printf("elapsed: %f (%ju iterations)\n", ((double)(eb_time_now() - startTime) / eb_nsecs_per_sec), (uintmax_t)NTRIALS);
    exit(0);
    return NULL;
}

void *threadDoRecv(void *a)
{
    eb_chan_op recv = eb_chan_recv_op(gChan);
    for (size_t i = 0; i < NTRIALS; i++) {
        assert(eb_chan_do(eb_nsecs_forever, &recv));
    }
    return NULL;
}

void *threadTrySend(void *a)
{
    eb_chan_op send = eb_chan_send_op(gChan, "halla");
    size_t count = 0;
    eb_nsecs startTime = eb_time_now();
    for (;;) {
        eb_chan_op *op = eb_chan_do(eb_nsecs_zero, &send);
        if (op == &send) {
            count++;
            if (count == NTRIALS) {
                break;
            }
        } else {
        }
    }
    printf("elapsed: %f (%ju iterations)\n", ((double)(eb_time_now() - startTime) / eb_nsecs_per_sec), (uintmax_t)NTRIALS);
    exit(0);
    return NULL;
}

void *threadSend(void *a)
{
    eb_chan_op send = eb_chan_send_op(gChan, "hallo");
    for (size_t i = 0; i < NTRIALS; i++) {
        assert(eb_chan_do(eb_nsecs_forever, &send));
    }
    return NULL;
    
}

void *threadRecv(void *a)
{
//    assert(eb_chan_recv(gChan, NULL, eb_nsecs_forever));
//    eb_nsecs startTime = eb_time_now();
//    for (size_t i = 1; i < NTRIALS; i++) {
//        assert(eb_chan_recv(gChan, NULL, eb_nsecs_forever));
//    }
//    printf("elapsed: %f (%ju iterations)", ((double)(eb_time_now() - startTime) / eb_nsecs_per_sec), (uintmax_t)NTRIALS);
//    exit(0);
//    return NULL;
    
    eb_chan_op recv = eb_chan_recv_op(gChan);
    assert(eb_chan_do(eb_nsecs_forever, &recv));
    eb_nsecs startTime = eb_time_now();
    for (size_t i = 1; i < NTRIALS; i++) {
        assert(eb_chan_do(eb_nsecs_forever, &recv));
    }
    
    printf("elapsed: %f (%ju iterations)\n", ((double)(eb_time_now() - startTime) / eb_nsecs_per_sec), (uintmax_t)NTRIALS);
    return NULL;
}

void *thread(void *a)
{
    eb_chan_op send = eb_chan_send_op(gChan, "hallo");
    eb_chan_op recv = eb_chan_recv_op(gChan);
    assert(eb_chan_do(eb_nsecs_forever, &send, &recv));
    
    eb_nsecs startTime = eb_time_now();
    for (size_t i = 1; i < NTRIALS; i++) {
        assert(eb_chan_do(eb_nsecs_forever, &send, &recv));
    }
    
    printf("elapsed: %f (%ju iterations)\n", ((double)(eb_time_now() - startTime) / eb_nsecs_per_sec), (uintmax_t)NTRIALS);
    exit(0);
    return NULL;
}

void *threadTest(void *a)
{
    eb_chan_op recv = eb_chan_recv_op(gChan);
    eb_nsecs startTime = eb_time_now();
    eb_chan_do(2.5 * eb_nsecs_per_sec, &recv);
    printf("elapsed: %f (%ju iterations)\n", ((double)(eb_time_now() - startTime) / eb_nsecs_per_sec), (uintmax_t)NTRIALS);
    
    exit(0);
    return NULL;
}

int main(int argc, const char * argv[])
{
    gChan = eb_chan_create(100);
    
    pthread_t thread1, thread2;
    
//    pthread_create(&thread1, NULL, threadDoSend, NULL);
//    pthread_create(&thread2, NULL, threadTryRecv, NULL);
    
//    pthread_create(&thread2, NULL, threadTrySend, NULL);
//    pthread_create(&thread1, NULL, threadDoRecv, NULL);
    
    pthread_create(&thread1, NULL, threadSend, NULL);
    pthread_create(&thread1, NULL, threadSend, NULL);
    pthread_create(&thread1, NULL, threadSend, NULL);
    
    pthread_create(&thread2, NULL, threadRecv, NULL);
    pthread_create(&thread2, NULL, threadRecv, NULL);
    pthread_create(&thread2, NULL, threadRecv, NULL);
    
//    pthread_create(&thread1, NULL, thread, NULL);
//    pthread_create(&thread2, NULL, thread, NULL);
//    pthread_create(&thread2, NULL, thread, NULL);
//    pthread_create(&thread2, NULL, thread, NULL);
//    pthread_create(&thread2, NULL, thread, NULL);
//    pthread_create(&thread2, NULL, thread, NULL);
    
//    pthread_create(&thread1, NULL, threadTest, NULL);
    
    for (;;) {
        sleep(-1);
        printf("SLEEPING\n");
    }
    
    return 0;
}