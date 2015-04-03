//
//  ViewController.m
//  IOSChannel
//
//  Created by Дмитрий Дорофеев on 02/04/15.
//  Copyright (c) 2015 YASP. All rights reserved.
//

#import "EBChannel.h"
#import "EBChannelLib.h"

#define NTRIALS 1000000
#define go_pool(a) go(@autoreleasepool { a; })

#import "ViewController.h"

@interface ViewController ()

@end

@implementation ViewController

- (void)viewDidLoad {
    [super viewDidLoad];
    _gChan = [[EBChannel alloc] initWithBufferCapacity: 0];
    // Do any additional setup after loading the view, typically from a nib.
}

- (void)didReceiveMemoryWarning {
    [super didReceiveMemoryWarning];
    // Dispose of any resources that can be recreated.
}

- (IBAction)runTest1:(id)sender {
    
    dispatch_queue_t sendQueue = dispatch_queue_create("com.yasp.sendQueue", NULL);
    dispatch_queue_t recvQueue = dispatch_queue_create("com.yasp.recvQueue", NULL);
    
    dispatch_async(sendQueue, ^{
         [self threadDoSend];
    });
    
    dispatch_async(recvQueue, ^{
        [self threadTryRecv];
    });
    
    self.resultsLabel.text = @"running...";
}

-(void) threadDoSend {
    for (size_t i = 0; i < NTRIALS; i++) @autoreleasepool {
        EBChannelRes r = [self.gChan send: @(i)];
        assert(r == EBChannelResOK || r == EBChannelResClosed);
        if (r == EBChannelResOK) {
           // NSLog(@"SEND: SEND");
        } else {
            NSLog(@"SEND %zu: CLOSED", i);
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
    
-(void) threadTryRecv {
        __block size_t count = 0;
        eb_nsec startTime = eb_time_now();
        while (count < NTRIALS) @autoreleasepool {
            id obj = nil;
            EBChannelRes r = [self.gChan tryRecv: &obj];
            if (r == EBChannelResOK) {
                //            NSLog(@"TRYRECV: RECV (%@)", obj);
                count++;
            } else if (r == EBChannelResStalled) {
                //            NSLog(@"TRYRECV: STALLED");
            } else if (r == EBChannelResClosed) {
                //            NSLog(@"TRYRECV: CLOSED");
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
    NSString * results = [NSString stringWithFormat: @"elapsed: %f (%ju iterations)", ((double)(eb_time_now() - startTime) / eb_nsec_per_sec), (uintmax_t)NTRIALS];
    dispatch_async(dispatch_get_main_queue(), ^{
        self.resultsLabel.text = results;
    });
    
    
    NSLog(@"elapsed: %f (%ju iterations)", ((double)(eb_time_now() - startTime) / eb_nsec_per_sec), (uintmax_t)NTRIALS);
    }
    


@end
