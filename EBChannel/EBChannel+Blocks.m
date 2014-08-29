//#if __has_feature(objc_arc)
//  #error For performance, EBChannel must be compiled with ARC disabled. (To do so, use the -fno-objc-arc compiler flag for this file.)
//#endif

#import "EBChannel+Blocks.h"
#import <EBFoundation/EBFoundation.h>

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
    NSParameterAssert(!([opsAndHandlers count]%2));
    
    EBChannelOp *defaultOp = [self default];
    NSUInteger nops = [opsAndHandlers count]/2;
    
    /* Create our array of only ops, and also determine whether there's a default
       case (and therefore whether we should use do() or try()) */
    NSMutableArray *ops = [NSMutableArray arrayWithCapacity: nops];
    BOOL block = YES;
    for (NSUInteger i = 0; i < nops; i++) {
        EBChannelOp *op = opsAndHandlers[i*2];
        if (op == defaultOp) {
            block = NO;
        } else {
            [ops addObject: op];
        }
    }
    
    EBChannelOp *r = (block ? [self do: ops] : [self try: ops]);
        EBAssertOrRecover(!block || r, return);
    
    if (!r) {
        r = defaultOp;
    }
    
    for (NSUInteger i = 0; i < nops; i++) {
        EBChannelOp *op = opsAndHandlers[i*2];
        if (op == r) {
            EBChannelHandler handler = opsAndHandlers[(i*2)+1];
            handler([op obj]);
            break;
        }
    }
}

@end