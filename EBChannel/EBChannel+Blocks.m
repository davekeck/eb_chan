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

+ (void)select: (NSTimeInterval)timeout opsAndHandlers: (NSArray *)opsAndHandlers {
        NSParameterAssert(opsAndHandlers);
        NSParameterAssert(!([opsAndHandlers count]%2)); /* Every op must have a handler, so we must have an even number of objects. */
    
    EBChannelOp *defaultOp = [self default];
    NSMutableArray *ops = [NSMutableArray new];
    for (NSUInteger i = 0; i < [opsAndHandlers count]; i += 2) {
        EBChannelOp *op = opsAndHandlers[i];
        if (op != defaultOp) {
            [ops addObject: op];
        }
    }
    
    EBChannelOp *r = [EBChannel select: timeout ops: ops];
    EBChannelHandler handler = nil;
    for (NSUInteger i = 0; i < [opsAndHandlers count]; i += 2) {
        EBChannelOp *op = opsAndHandlers[i];
        if ((r && r == op) || (!r && op == defaultOp)) {
            handler = opsAndHandlers[i+1];
            break;
        }
    }
    
    eb_assert_or_bail(handler, "Couldn't find handler");
    handler([r open], [r obj]);
}

@end
