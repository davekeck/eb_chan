//#if __has_feature(objc_arc)
//  #error For performance, EBChannel must be compiled with ARC disabled. (To do so, use the -fno-objc-arc compiler flag for this file.)
//#endif

#import "EBChannel+Blocks.h"
#import <EBFoundation/EBFoundation.h>
#import "eb_assert.h"

@implementation EBChannel (Blocks)

+ (EBChannelOp *)default {
    static EBChannelOp *op = nil;
    static dispatch_once_t token;
    dispatch_once(&token, ^{
        op = [EBChannelOp new];
    });
    
    return op;
}

+ (void)select: (NSArray *)opsAndHandlers {
        NSParameterAssert(opsAndHandlers);
        NSParameterAssert(!([opsAndHandlers count]%2)); /* Every op must have a handler, so we must have an even number of objects. */
    
    EBChannelOp *defaultOp = [self default];
    NSTimeInterval timeout = -1;
    NSMutableArray *ops = [NSMutableArray new];
    for (NSUInteger i = 0; i < [opsAndHandlers count]; i += 2) {
        EBChannelOp *op = opsAndHandlers[i];
        if (op == defaultOp) {
            /* The op set has a default op, so change the timeout to 0 */
            timeout = 0;
        } else {
            [ops addObject: op];
        }
    }
    
    EBChannelOp *r = [EBChannel select: ops timeout: timeout];
    EBChannelHandler handler = nil;
    for (NSUInteger i = 0; i < [opsAndHandlers count]; i += 2) {
        EBChannelOp *op = opsAndHandlers[i];
        if ((r && r == op) || (!r && op == defaultOp)) {
            handler = opsAndHandlers[i+1];
            break;
        }
    }
    
    eb_assert_or_recover(handler, return);
    handler([r obj]);
}

@end