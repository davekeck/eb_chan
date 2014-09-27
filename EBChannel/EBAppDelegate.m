#import "EBAppDelegate.h"
#import "testglue.h"
#import "EBChannel.h"
#import "EBChannel+Blocks.h"

#define NTRIALS 1000000

EBChannel *gChan = nil;

@implementation EBAppDelegate

void threadDoSend()
{
    for (size_t i = 0; i < NTRIALS; i++) @autoreleasepool {
        EBChannelResult r = [gChan send: @(i)];
        assert(r == EBChannelResultOK || r == EBChannelResultClosed);
        if (r == EBChannelResultOK) {
//            NSLog(@"SEND: SEND");
        } else {
            NSLog(@"SEND: CLOSED");
            return;
        }
        
//        [EBChannel select: @[
//            [gChan sendOp: @(i)],
//            ^(BOOL open, id obj){
////                NSLog(@"SEND: %@", obj);
//            }
//        ]];
    }
}

void threadTryRecv()
{
    __block size_t count = 0;
    eb_nsec startTime = eb_time_now();
    while (count < NTRIALS) @autoreleasepool {
        id obj = nil;
        EBChannelResult r = [gChan tryRecv: &obj];
        if (r == EBChannelResultOK) {
            NSLog(@"TRYRECV: RECV (%@)", obj);
            count++;
        } else if (r == EBChannelResultStalled) {
//            NSLog(@"TRYRECV: STALLED");
        } else if (r == EBChannelResultClosed) {
            NSLog(@"TRYRECV: CLOSED");
            return;
        }
        
//        [EBChannel select: @[
//            [gChan recvOp],
//            ^(BOOL open, id obj){
////                NSLog(@"RECV: %@", obj);
//                count++;
//            },
//            
//            [EBChannel default],
//            ^{
////                NSLog(@"NO RECV");
//            }
//        ]];
        
        if (count == NTRIALS) {
            break;
        }
    }
    
    NSLog(@"elapsed: %f (%ju iterations)", ((double)(eb_time_now() - startTime) / eb_nsec_per_sec), (uintmax_t)NTRIALS);
    exit(0);
}

void threadDoRecv() {
    for (size_t i = 0; i < NTRIALS; i++) @autoreleasepool {
        id obj = nil;
        EBChannelResult r = [gChan recv: &obj];
        assert(r == EBChannelResultOK || r == EBChannelResultClosed);
        if (r == EBChannelResultOK) {
            NSLog(@"RECV: RECV (%@)", obj);
        } else {
            NSLog(@"RECV: CLOSED");
            return;
        }
        
//        [EBChannel select: @[
//            [gChan recvOp],
//            ^(BOOL open, id obj){
////                NSLog(@"RECV: %@", obj);
//            }
//        ]];
    }
}

void threadTrySend() {
    __block size_t count = 0;
    eb_nsec startTime = eb_time_now();
    while (count < NTRIALS) @autoreleasepool {
        EBChannelResult r = [gChan trySend: @"hallo"];
        if (r == EBChannelResultOK) {
//            NSLog(@"TRYSEND: SEND");
            count++;
        } else if (r == EBChannelResultStalled) {
//                NSLog(@"TRYSEND: STALLED");
        } else if (r == EBChannelResultClosed) {
            NSLog(@"TRYSEND: CLOSED");
            return;
        }
        
//        [EBChannel select: @[
//            [gChan sendOp: @"hello"],
//            ^(BOOL open, id obj){
////                NSLog(@"SEND: %@", obj);
//                count++;
//            },
//            
//            [EBChannel default],
//            ^{
////                NSLog(@"NO SEND");
//            }
//        ]];
        
        if (count == NTRIALS) {
            break;
        }
    }
    
    NSLog(@"elapsed: %f (%ju iterations)", ((double)(eb_time_now() - startTime) / eb_nsec_per_sec), (uintmax_t)NTRIALS);
    exit(0);
}

void threadSend() {
    for (size_t i = 0; i < NTRIALS; i++) @autoreleasepool {
//        assert([gChan send: ([[NSString alloc] initWithFormat: @"%ju", (uintmax_t)i])] == EBChannelResultOK);
        
        EBChannelResult r = [gChan send: ([[NSString alloc] initWithFormat: @"%ju", (uintmax_t)i])];
        if (r != EBChannelResultOK) {
            NSLog(@"SEND RET: %d", r);
            break;
        }
        
//        [EBChannel select: @[[gChan sendOp: [[NSString alloc] initWithFormat: @"%ju", (uintmax_t)i]]] timeout: -1];
    }
    
//    NSArray *ops = @[[gChan sendOp: @"hallo"]];
//    for (size_t i = 0; i < NTRIALS; i++) {
//        [EBChannel select: ops timeout: -1];
//    }
    
//    for (size_t i = 0; i < NTRIALS; i++) {
//        [EBChannel select: @[
//            [gChan sendOp: @"hallo"],
//            ^(BOOL open, id obj){
////                NSLog(@"SEND: %@", obj);
//            }
//        ]];
//    }
}

void threadRecv() {
    eb_nsec startTime = eb_time_now();
    for (size_t i = 0; i < NTRIALS; i++) @autoreleasepool {
        EBChannelResult r = [gChan recv: nil];
        if (r != EBChannelResultOK) {
            NSLog(@"RECV RET: %d", r);
            break;
        }
    }
    NSLog(@"elapsed: %f (%ju iterations)", ((double)(eb_time_now() - startTime) / eb_nsec_per_sec), (uintmax_t)NTRIALS);
    
//    eb_nsec startTime = eb_time_now();
//    for (size_t i = 0; i < NTRIALS; i++) {
//        [EBChannel select: @[
//            [gChan recvOp],
//            ^(BOOL open, id obj){
////                NSLog(@"RECV: %@", obj);
//            }
//        ]];
//    }
//    NSLog(@"elapsed: %f (%ju iterations)", ((double)(eb_time_now() - startTime) / eb_nsec_per_sec), (uintmax_t)NTRIALS);
}

void thread() {
    eb_nsec startTime = eb_time_now();
    for (size_t i = 0; i < NTRIALS; i++) @autoreleasepool {
        [EBChannel select: -1 opsAndHandlers: @[
            [gChan sendOp: @(i)],
            ^(BOOL open, id obj){
                NSLog(@"SEND: %d / %@", open, obj);
            },
            
            [gChan recvOp],
            ^(BOOL open, id obj){
                NSLog(@"RECV: %d / %@", open, obj);
            }
        ]];
    }
    
    NSLog(@"elapsed: %f (%ju iterations)", ((double)(eb_time_now() - startTime) / eb_nsec_per_sec), (uintmax_t)NTRIALS);
//    exit(0);
}

void timeoutTest() {
    eb_nsec startTime = eb_time_now();
    EBChannelOp *r = [EBChannel select: 2.5 ops: @[[gChan recvOp]]];
    NSLog(@"(%@) elapsed: %f (%ju iterations)", r, ((double)(eb_time_now() - startTime) / eb_nsec_per_sec), (uintmax_t)NTRIALS);
    exit(0);
}

void deadlock(EBChannel *a, EBChannel *b) {
    for (;;) @autoreleasepool {
        [EBChannel select: -1 ops: @[
            [a sendOp: @"xxx"],
            ^(BOOL open, id obj){
//                NSLog(@"A SEND: %d / %@", open, obj);
            },
            
            [b recvOp],
            ^(BOOL open, id obj){
//                NSLog(@"B RECV: %d / %@", open, obj);
            }
        ]];
    }
}

#define go_pool(a) go(@autoreleasepool { a; })

- (void)applicationDidFinishLaunching: (NSNotification *)aNotification
{
    gChan = [[EBChannel alloc] initWithBufferCapacity: 0];
    
//    go( threadDoSend() );
//    go( threadTryRecv() );
//    
//    go( threadTrySend() );
//    go( threadDoRecv() );
    
    go_pool( threadSend() );
    go_pool( threadSend() );
    go_pool( threadSend() );
    
    
    go_pool( threadRecv() );
    go_pool( threadRecv() );
    go_pool( threadRecv() );
    
    
    
//    sleep(5);
//    assert([gChan close] == EBChannelResultOK);
    
    
//    go( thread() );
//    go( thread() );
//    go( thread() );
//    go( thread() );
//    go( thread() );
//    go( thread() );
//    go( thread() );
//    go( thread() );
    
//    // at one time this caused a deadlock
//    EBChannel *a = [[EBChannel alloc] initWithBufferCapacity: 0];
//    EBChannel *b = [[EBChannel alloc] initWithBufferCapacity: 0];
//    go( deadlock(a,b) );
//    go( deadlock(b,a) );
    
//    go_pool( timeoutTest() );
//    
//    usleep(500000);
//    assert([gChan close] == EBChannelResultOK);
//    assert([gChan close] == EBChannelResultOK);
}

@end



//#import "EBAppDelegate.h"
//#import "EBChannel.h"
//#import "eb_chan.h"
//
//#define NTRIALS 1000000
//
//@implementation EBAppDelegate
//{
//    EBChannel *_chan;
//}
//
////- (void)thread
////{
////    EBTime startTime = EBTimeCurrentTime();
////    
////    eb_chan_op_t recv = eb_chan_recv(_chan);
////    eb_chan_op_t send = eb_chan_send(_chan, "hallo");
////    eb_chan_op_t *const ops[] = {&send, &recv};
////    
////    for (NSUInteger i = 0; i < NTRIALS; i++) {
////        eb_chan_op_t *op = eb_chan_select(ops, (sizeof(ops) / sizeof(*ops)));
////        if (op == &send) {
//////            NSLog(@"send");
////        } else if (&recv) {
//////            NSLog(@"recv");
////        }
////    }
////    
////    NSLog(@"elapsed: %f (%ju iterations)", EBTimeElapsedSecondsSince(startTime), (uintmax_t)NTRIALS);
////    exit(0);
////}
//
//- (void)threadSend
//{
//    NSArray *ops = @[[_chan sendOp: @"hallo"]];
//    assert([EBChannel select: ops timeout: -1]);
//    
//    for (NSUInteger i = 0; i < NTRIALS; i++) {
//        assert([EBChannel select: ops timeout: -1]);
//    }
//}
//
//- (void)threadRecv
//{
//    NSArray *ops = @[[_chan recvOp]];
//    assert([EBChannel select: ops timeout: -1]);
//    
//    EBTime startTime = EBTimeCurrentTime();
//    for (NSUInteger i = 1; i < NTRIALS; i++) {
//        assert([EBChannel select: ops timeout: -1]);
//    }
//    
//    NSLog(@"elapsed: %f (%ju iterations)", EBTimeElapsedSecondsSince(startTime), (uintmax_t)NTRIALS);
//    exit(0);
//}
//
//- (void)applicationDidFinishLaunching: (NSNotification *)aNotification
//{
//    _chan = [[EBChannel alloc] initWithBufferCapacity: 0];
////    [NSTimer scheduledTimerWithTimeInterval: 1 repeats: NO block:^(NSTimer *timer) {
////        [NSThread detachNewThreadSelector: @selector(thread) toTarget: self withObject: nil];
////        [NSThread detachNewThreadSelector: @selector(thread) toTarget: self withObject: nil];
////    }];
//    [NSTimer scheduledTimerWithTimeInterval: 1 repeats: NO block:^(NSTimer *timer) {
//        [NSThread detachNewThreadSelector: @selector(threadSend) toTarget: self withObject: nil];
//        [NSThread detachNewThreadSelector: @selector(threadRecv) toTarget: self withObject: nil];
//    }];
//}
//
//@end
