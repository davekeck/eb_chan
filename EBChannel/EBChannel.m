#if __has_feature(objc_arc)
  #error For performance, EBChannel must be compiled with ARC disabled. (To do so, use the -fno-objc-arc compiler flag for this file.)
#endif

#import "EBChannel.h"
#import <EBFoundation/EBFoundation.h>
#import "eb_chan.h"
#import "eb_assert.h"

@implementation EBChannelOp {
    @public
    eb_chan_op_t _op;
}

- (instancetype)initWithChannel: (eb_chan_t)chan send: (BOOL)send obj: (id)obj {
        NSParameterAssert(chan);
    
    if (!(self = [super init])) {
        return nil;
    }
    
    _op.chan = chan;
    _op.send = send;
    _op.val = [obj retain];
    
    return self;
}

- (void)dealloc {
    [(id)_op.val release];
    _op.val = nil;
    
    [super dealloc];
}

- (id)obj {
    return _op.val;
}

@end

@implementation EBChannel {
    eb_chan_t _chan;
}

#pragma mark - Creation -
- (instancetype)initWithBufferCapacity: (NSUInteger)bufferCapacity {
    if (!(self = [super init])) {
        return nil;
    }
    
    _chan = eb_chan_create((size_t)bufferCapacity);
        EBAssertOrRecover(_chan, return nil);
    
    return self;
}

- (void)dealloc {
    if (_chan) {
        eb_chan_release(_chan);
        _chan = nil;
    }
    
    [super dealloc];
}

#pragma mark - Methods -
NS_INLINE EBChannelOp *doOps(NSArray *opsArray, BOOL block) {
        NSCParameterAssert(opsArray);
    
    size_t nops = [opsArray count];
    eb_chan_op_t *ops[nops];
    for (NSUInteger i = 0; i < nops; i++) {
        /* Reset every recv op's object */
        eb_chan_op_t *op = &((EBChannelOp *)opsArray[i])->_op;;
        if (!op->send) {
            [(id)op->val release];
            op->val = nil;
        }
        ops[i] = op;
    }
    
    eb_chan_op_t *r = (block ? eb_chan_do(ops, nops) : eb_chan_try(ops, nops));
        /* Either we're non-blocking and it doesn't matter whether an op completed, or we're blocking and an op did complete */
        EBAssertOrRecover(!block || r, return nil);
    
    EBChannelOp *result = nil;
    if (r) {
        if (r->send) {
            /* Send ops retain the object on behalf of the receiver */
            [(id)r->val retain];
        }
        
        for (EBChannelOp *op in opsArray) {
            if (&op->_op == r) {
                result = op;
                break;
            }
        }
        
        eb_assert_or_bail(result, "Couldn't find op!");
    }
    
    return result;
}

+ (EBChannelOp *)do: (NSArray *)ops {
    return doOps(ops, YES);
}

+ (EBChannelOp *)try: (NSArray *)ops {
    return doOps(ops, NO);
}

- (EBChannelOp *)send: (id)obj {
    return [[[EBChannelOp alloc] initWithChannel: _chan send: YES obj: obj] autorelease];
}

- (EBChannelOp *)recv {
    return [[[EBChannelOp alloc] initWithChannel: _chan send: NO obj: nil] autorelease];
}

- (void)close {
    eb_chan_close(_chan);
}

#pragma mark - Getters -
- (NSUInteger)bufferCapacity {
    return eb_chan_get_buf_cap(_chan);
}

- (NSUInteger)bufferLength {
    return eb_chan_get_buf_len(_chan);
}

@end