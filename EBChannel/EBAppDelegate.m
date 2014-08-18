#import "EBAppDelegate.h"
#import <EBFoundation/EBFoundation.h>
#import "EBChannel.h"
#import "eb_chan.h"

#define NTRIALS 1000000

@implementation EBAppDelegate
{
    eb_chan_t _chan;
}

- (void)thread
{
    EBTime startTime = EBTimeCurrentTime();
    
    eb_chan_op_t recv = eb_chan_recv(_chan);
    eb_chan_op_t send = eb_chan_send(_chan, "hallo");
    eb_chan_op_t *const ops[] = {&send, &recv};
    
    for (NSUInteger i = 0; i < NTRIALS; i++) {
        eb_chan_op_t *op = eb_chan_do(ops, (sizeof(ops) / sizeof(*ops)));
        if (op == &send) {
//            NSLog(@"send");
        } else if (&recv) {
//            NSLog(@"recv");
        }
    }
    
    NSLog(@"elapsed: %f (%ju iterations)", EBTimeElapsedSecondsSince(startTime), (uintmax_t)NTRIALS);
//    exit(0);
}

- (void)applicationDidFinishLaunching: (NSNotification *)aNotification
{
    _chan = eb_chan_alloc(1);
    [NSTimer scheduledTimerWithTimeInterval: 1 repeats: NO block:^(NSTimer *timer) {
        [NSThread detachNewThreadSelector: @selector(thread) toTarget: self withObject: nil];
        [NSThread detachNewThreadSelector: @selector(thread) toTarget: self withObject: nil];
    }];
}

@end