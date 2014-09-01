#import <Foundation/Foundation.h>
#import <EBFoundation/EBFoundation.h>
#import <pthread.h>
#import <math.h>
#import "eb_chan.h"

#define NTRIALS 1000000

eb_chan gChan = NULL;

void *threadDoSend(void *a)
{
    eb_chan_op send = eb_chan_send_op(gChan, "halla");
    eb_chan_op *const ops[] = {&send};
    for (NSUInteger i = 0; i < NTRIALS; i++) {
        assert(eb_chan_do(ops, (sizeof(ops) / sizeof(*ops)), eb_nsecs_forever));
        NSLog(@"SENT");
    }
    return NULL;
}

void *threadTryRecv(void *a)
{
    eb_chan_op recv = eb_chan_recv_op(gChan);
    eb_chan_op *const ops[] = {&recv};
    for (NSUInteger i = 0; i < NTRIALS; i++) {
        if (eb_chan_do(ops, (sizeof(ops) / sizeof(*ops)), 0) == 0) {
            NSLog(@"RECEIVED");
        } else {
            NSLog(@"NOT RECEIVED");
        }
    }
    return NULL;
}




void *threadDoRecv(void *a)
{
    eb_chan_op recv = eb_chan_recv_op(gChan);
    eb_chan_op *const ops[] = {&recv};
    for (NSUInteger i = 0; i < NTRIALS; i++) {
        assert(eb_chan_do(ops, (sizeof(ops) / sizeof(*ops)), eb_nsecs_forever));
        NSLog(@"RECEIVED");
    }
    return NULL;
}

void *threadTrySend(void *a)
{
    eb_chan_op send = eb_chan_send_op(gChan, "sup g");
    eb_chan_op *const ops[] = {&send};
    for (NSUInteger i = 0; i < NTRIALS; i++) {
        if (eb_chan_do(ops, (sizeof(ops) / sizeof(*ops)), 0) == 0) {
            NSLog(@"SENT");
        } else {
            NSLog(@"NOT SENT");
        }
    }
    return NULL;
}

void *threadSend(void *a)
{
//    for (NSUInteger i = 0; i < NTRIALS; i++) {
//        assert(eb_chan_send(gChan, "hallo", eb_nsecs_forever));
//    }
//    return NULL;
    
    eb_chan_op send = eb_chan_send_op(gChan, "hallo");
    eb_chan_op *const ops[] = {&send};
    for (NSUInteger i = 0; i < NTRIALS; i++) {
        assert(eb_chan_do(ops, (sizeof(ops) / sizeof(*ops)), eb_nsecs_forever));
    }
    return NULL;
    
}

void *threadRecv(void *a)
{
//    assert(eb_chan_recv(gChan, NULL, eb_nsecs_forever));
//    EBTime startTime = EBTimeCurrentTime();
//    for (NSUInteger i = 1; i < NTRIALS; i++) {
//        assert(eb_chan_recv(gChan, NULL, eb_nsecs_forever));
//    }
//    NSLog(@"elapsed: %f (%ju iterations)", EBTimeElapsedSecondsSince(startTime), (uintmax_t)NTRIALS);
//    exit(0);
//    return NULL;
    
    eb_chan_op recv = eb_chan_recv_op(gChan);
    eb_chan_op *const ops[] = {&recv};
    
    assert(eb_chan_do(ops, (sizeof(ops) / sizeof(*ops)), eb_nsecs_forever));
    EBTime startTime = EBTimeCurrentTime();
    for (NSUInteger i = 1; i < NTRIALS; i++) {
        assert(eb_chan_do(ops, (sizeof(ops) / sizeof(*ops)), eb_nsecs_forever));
    }
    
    NSLog(@"elapsed: %f (%ju iterations)", EBTimeElapsedSecondsSince(startTime), (uintmax_t)NTRIALS);
    exit(0);
    return NULL;
}

void *thread(void *a)
{
    eb_chan_op send = eb_chan_send_op(gChan, "hallo");
    eb_chan_op recv = eb_chan_recv_op(gChan);
    eb_chan_op *const ops[] = {&send, &recv};
    
    assert(eb_chan_do(ops, (sizeof(ops) / sizeof(*ops)), eb_nsecs_forever));
    EBTime startTime = EBTimeCurrentTime();
    for (NSUInteger i = 1; i < NTRIALS; i++) {
        assert(eb_chan_do(ops, (sizeof(ops) / sizeof(*ops)), eb_nsecs_forever));
    }
    
    NSLog(@"elapsed: %f (%ju iterations)", EBTimeElapsedSecondsSince(startTime), (uintmax_t)NTRIALS);
    exit(0);
    return NULL;
}

int main(int argc, const char * argv[])
{
    gChan = eb_chan_create(0);
    
    pthread_t thread1, thread2;
    
//    pthread_create(&thread1, NULL, threadDoSend, NULL);
//    pthread_create(&thread2, NULL, threadTryRecv, NULL);
    
//    pthread_create(&thread1, NULL, threadDoRecv, NULL);
//    pthread_create(&thread2, NULL, threadTrySend, NULL);
    
    pthread_create(&thread1, NULL, threadSend, NULL);
    pthread_create(&thread2, NULL, threadRecv, NULL);
    
//    pthread_create(&thread1, NULL, thread, NULL);
//    pthread_create(&thread2, NULL, thread, NULL);
    
    for (;;) {
        sleep(-1);
        NSLog(@"SLEEPING");
    }
    
    return 0;
}