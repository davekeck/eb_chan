#import "EBAppDelegate.h"
#import <EBFoundation/EBFoundation.h>
#import "EBChannel.h"

#define NTRIALS 100000

@implementation EBAppDelegate
{
    EBChannel *_chan;
}

- (void)threadSend
{
    EBChannelOp *send = [_chan send: @"hello"];
    NSArray *ops = @[send];
    for (NSUInteger i = 0; i < NTRIALS; i++) {
        @autoreleasepool {
            assert([EBChannel do: ops]);
//            
//            if (r == read) {
////                NSLog(@"read");
//            } else if (r == write) {
////                NSLog(@"write");
//            } else {
//                abort();
//            }
        }
    }
}

- (void)threadRecv
{
    EBTime startTime = EBTimeCurrentTime();
    EBChannelOp *recv = [_chan recv];
    NSArray *ops = @[recv];
    for (NSUInteger i = 0; i < NTRIALS; i++) {
        @autoreleasepool {
            assert([EBChannel do: ops]);
//            
//            if (r == read) {
////                NSLog(@"read");
//            } else if (r == write) {
////                NSLog(@"write");
//            } else {
//                abort();
//            }
        }
    }
    
    NSLog(@"elapsed: %f", EBTimeElapsedSecondsSince(startTime));
}

- (void)applicationDidFinishLaunching: (NSNotification *)aNotification
{
    _chan = [[EBChannel alloc] initWithBufferSize: 0];
    [NSTimer scheduledTimerWithTimeInterval: 1 repeats: NO block:^(NSTimer *timer) {
        [NSThread detachNewThreadSelector: @selector(threadSend) toTarget: self withObject: nil];
        [NSThread detachNewThreadSelector: @selector(threadRecv) toTarget: self withObject: nil];
    }];
    
    
//    EBTime startTime = EBTimeCurrentTime();
//    [EBChannel do: @[[_doneChan recv]]];
//    NSLog(@"elapsed: %f", EBTimeElapsedSecondsSince(startTime));
//    
//    [NSThread detachNewThreadSelector: @selector(thread) toTarget: self withObject: nil];
//    [NSThread detachNewThreadSelector: @selector(thread) toTarget: self withObject: nil];
//    [NSThread detachNewThreadSelector: @selector(thread) toTarget: self withObject: nil];
//    [NSThread detachNewThreadSelector: @selector(thread) toTarget: self withObject: nil];
//    [NSThread detachNewThreadSelector: @selector(thread) toTarget: self withObject: nil];
//    [NSThread detachNewThreadSelector: @selector(thread) toTarget: self withObject: nil];
}

@end