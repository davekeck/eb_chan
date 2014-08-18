#import "EBAppDelegate.h"
#import <EBFoundation/EBFoundation.h>
#import "EBChannel.h"
#import "eb_chan.h"

#define NTRIALS 1000000

const char *const mystr = "hello";

@implementation EBAppDelegate
{
    eb_chan_t _chan;
}

- (void)threadSend
{
    eb_chan_op_t sendop = eb_chan_send(_chan, mystr);
    eb_chan_op_t *const ops[] = {&sendop};
    for (NSUInteger i = 0; i < NTRIALS; i++) {
        assert(eb_chan_do(ops, 1));
    }
}

- (void)threadRecv
{
    EBTime startTime = EBTimeCurrentTime();
    eb_chan_op_t recvop = eb_chan_recv(_chan);
    eb_chan_op_t *const ops[] = {&recvop};
    for (NSUInteger i = 0; i < NTRIALS; i++) {
        assert(eb_chan_do(ops, 1) == &recvop);
    }
    
    NSLog(@"elapsed: %f (%ju iterations)", EBTimeElapsedSecondsSince(startTime), (uintmax_t)NTRIALS);
    exit(0);
}

- (void)applicationDidFinishLaunching: (NSNotification *)aNotification
{
    _chan = eb_chan_alloc(0);
    [NSTimer scheduledTimerWithTimeInterval: 1 repeats: NO block:^(NSTimer *timer) {
        [NSThread detachNewThreadSelector: @selector(threadSend) toTarget: self withObject: nil];
        [NSThread detachNewThreadSelector: @selector(threadRecv) toTarget: self withObject: nil];
    }];
}

@end