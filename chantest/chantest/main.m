#import <Foundation/Foundation.h>
#import <EBFoundation/EBFoundation.h>
#import <pthread.h>
#import "eb_chan.h"

#define NTRIALS 10000000

eb_chan_t gChan = NULL;

void *threadSend(void *a)
{
    eb_chan_op_t send = eb_chan_send(gChan, "hallo");
    eb_chan_op_t *const ops[] = {&send};
    for (NSUInteger i = 0; i < NTRIALS; i++) {
        assert(eb_chan_do(ops, (sizeof(ops) / sizeof(*ops))));
    }
    return NULL;
}

void *threadRecv(void *a)
{
    eb_chan_op_t recv = eb_chan_recv(gChan);
    eb_chan_op_t *const ops[] = {&recv};
    
    assert(eb_chan_do(ops, (sizeof(ops) / sizeof(*ops))));
    EBTime startTime = EBTimeCurrentTime();
    for (NSUInteger i = 1; i < NTRIALS; i++) {
        assert(eb_chan_do(ops, (sizeof(ops) / sizeof(*ops))));
    }
    
    NSLog(@"elapsed: %f (%ju iterations)", EBTimeElapsedSecondsSince(startTime), (uintmax_t)NTRIALS);
    exit(0);
    return NULL;
}

void *thread(void *a)
{
    eb_chan_op_t send = eb_chan_send(gChan, "hallo");
    eb_chan_op_t recv = eb_chan_recv(gChan);
    eb_chan_op_t *const ops[] = {&send, &recv};
    
    assert(eb_chan_do(ops, (sizeof(ops) / sizeof(*ops))));
    EBTime startTime = EBTimeCurrentTime();
    for (NSUInteger i = 1; i < NTRIALS; i++) {
        assert(eb_chan_do(ops, (sizeof(ops) / sizeof(*ops))));
    }
    
    NSLog(@"elapsed: %f (%ju iterations)", EBTimeElapsedSecondsSince(startTime), (uintmax_t)NTRIALS);
    exit(0);
    return NULL;
}

int main(int argc, const char * argv[])
{
    gChan = eb_chan_alloc(0);
    
    pthread_t thread1, thread2;
    
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