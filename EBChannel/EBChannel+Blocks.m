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

+ (void)select: (NSArray *)opsAndHandlersArray {
    NSParameterAssert(opsAndHandlersArray);
    NSParameterAssert(!([opsAndHandlersArray count]%2));
    
    id opsAndHandlers[[opsAndHandlersArray count]];
    NSUInteger i = 0;
    for (id o in opsAndHandlersArray) {
        opsAndHandlers[i] = o;
        i++;
    }
    
    EBChannelOp *defaultOp = [self default];
    NSUInteger maxNops = [opsAndHandlersArray count]/2;
    eb_chan_op_t *ops[maxNops];
    NSUInteger nops = 0;
    BOOL block = YES;
    for (NSUInteger i = 0; i < maxNops; i++) {
        EBChannelOp *objcOp = opsAndHandlers[i*2];
        if (objcOp == defaultOp) {
            block = NO;
        } else {
            eb_chan_op_t *op = &((EBChannelOp *)opsAndHandlers[i*2])->_op;
            /* Reset every recv op's object */
            if (!op->send) {
                [(id)op->val release];
                op->val = nil;
            }
            ops[i] = op;
            nops++;
        }
    }
    
    eb_chan_op_t *r = (block ? eb_chan_do(ops, nops) : eb_chan_try(ops, nops));
        /* Either we're non-blocking and therefore r can be nil, or we're blocking and a r cannot be nil */
        EBAssertOrRecover(!block || r, return);
    
    /* If we're non-blocking and r==nil, then make r our default op's eb_chan_op_t. */
    if (!r) {
        r = &defaultOp->_op;
    }
    
    /* At this point, r can't be nil! */
    eb_assert_or_bail(r, "No r!");
    
    if (r->send) {
        /* Send ops retain the object on behalf of the receiver */
        [(id)r->val retain];
    }
    
    for (NSUInteger i = 0; i < nops; i++) {
        EBChannelOp *op = opsAndHandlers[i*2];
        if (r == &op->_op) {
            EBChannelHandler handler = opsAndHandlers[(i*2)+1];
            handler(r->val);
            break;
        }
    }
}

@end