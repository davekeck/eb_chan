#import <Foundation/Foundation.h>
#import <EBFoundation/EBFoundation.h>
#import <pthread.h>
#import "EBChannel.h"
#import "EBChannel+Blocks.h"

#define NTRIALS 1000000

static EBChannel *gChan = nil;

//void *threadDoSend(void *a)
//{
//    eb_chan_op_t send = eb_chan_send(gChan, "halla");
//    eb_chan_op_t *const ops[] = {&send};
//    for (NSUInteger i = 0; i < NTRIALS; i++) {
//        assert(eb_chan_do(ops, (sizeof(ops) / sizeof(*ops))));
//        NSLog(@"SENT");
//    }
//    return NULL;
//}
//
//void *threadTryRecv(void *a)
//{
//    eb_chan_op_t recv = eb_chan_recv(gChan);
//    eb_chan_op_t *const ops[] = {&recv};
//    for (NSUInteger i = 0; i < NTRIALS; i++) {
//        if (eb_chan_try(ops, (sizeof(ops) / sizeof(*ops))) == 0) {
//            NSLog(@"RECEIVED");
//        } else {
//            NSLog(@"NOT RECEIVED");
//        }
//    }
//    return NULL;
//}
//
//void *threadDoRecv(void *a)
//{
//    eb_chan_op_t recv = eb_chan_recv(gChan);
//    eb_chan_op_t *const ops[] = {&recv};
//    for (NSUInteger i = 0; i < NTRIALS; i++) {
//        assert(eb_chan_do(ops, (sizeof(ops) / sizeof(*ops))));
//        NSLog(@"RECEIVED");
//    }
//    return NULL;
//}
//
//void *threadTrySend(void *a)
//{
//    eb_chan_op_t send = eb_chan_send(gChan, "sup g");
//    eb_chan_op_t *const ops[] = {&send};
//    for (NSUInteger i = 0; i < NTRIALS; i++) {
//        if (eb_chan_try(ops, (sizeof(ops) / sizeof(*ops))) == 0) {
//            NSLog(@"SENT");
//        } else {
//            NSLog(@"NOT SENT");
//        }
//    }
//    return NULL;
//}

void *threadSend(void *a)
{
    @autoreleasepool {
        NSArray *ops = @[[gChan send: nil]];
        assert([EBChannel do: ops]);
        
        for (NSUInteger i = 0; i < NTRIALS; i++) {
            assert([EBChannel do: ops]);
        }
    }
    return NULL;
}

void *threadRecv(void *a)
{
    @autoreleasepool {
        NSArray *ops = @[[gChan recv]];
//        assert([EBChannel do: ops]);
        
        EBTime startTime = EBTimeCurrentTime();
        for (NSUInteger i = 0; i < NTRIALS; i++) {
            assert([EBChannel do: ops]);
        }
        
        NSLog(@"elapsed: %f (%ju iterations)", EBTimeElapsedSecondsSince(startTime), (uintmax_t)NTRIALS);
        exit(0);
    }
    return NULL;
}

//void *thread(void *a)
//{
//    @autoreleasepool {
//        NSArray *ops = @[[gChan send: nil], [gChan recv]];
//        assert([EBChannel do: ops]);
//        
//        EBTime startTime = EBTimeCurrentTime();
//        for (NSUInteger i = 0; i < NTRIALS; i++) {
//            assert([EBChannel do: ops]);
//        }
//        NSLog(@"elapsed: %f (%ju iterations)", EBTimeElapsedSecondsSince(startTime), (uintmax_t)NTRIALS);
//        exit(0);
//    }
//    return NULL;
//}

void *thread(void *a)
{
    @autoreleasepool {
        __block NSUInteger i = 0;
        EBTime startTime = EBTimeCurrentTime();
        NSArray *a = @[
            [gChan send: @"hallo"], ^{
//                    NSLog(@"sent");
            },
            
            [gChan recv], ^{
                i++;
                if (i == NTRIALS) {
                    NSLog(@"elapsed: %f (%ju iterations)", EBTimeElapsedSecondsSince(startTime), (uintmax_t)NTRIALS);
                    exit(0);
                } else {
//                        NSLog(@"recv");
                }
            },
        ];
        
        for (;;) {
//            @autoreleasepool
            {
                [EBChannel select: a];
            }
        }
    }
    return NULL;
}

int main(int argc, const char * argv[])
{
    gChan = [[EBChannel alloc] initWithBufferCapacity: 10];
    
    pthread_t thread1, thread2;
    
//    pthread_create(&thread1, NULL, threadDoSend, NULL);
//    pthread_create(&thread2, NULL, threadTryRecv, NULL);
    
//    pthread_create(&thread1, NULL, threadDoRecv, NULL);
//    pthread_create(&thread2, NULL, threadTrySend, NULL);
    
//    pthread_create(&thread1, NULL, threadSend, NULL);
//    pthread_create(&thread2, NULL, threadRecv, NULL);
    
    pthread_create(&thread1, NULL, thread, NULL);
    pthread_create(&thread2, NULL, thread, NULL);
    
    for (;;) {
        sleep(-1);
        NSLog(@"SLEEPING");
    }
    
    return 0;
}
