#if __has_feature(objc_arc)
  #error For performance, EBChannel must be compiled with ARC disabled. (Use the -fno-objc-arc compiler flag for this file.)
#endif

#import "EBChannel.h"
#import <EBFoundation/EBFoundation.h>
#import "eb_chan.h"

@implementation EBChannelOp {
    @public
    id _obj;
    eb_chan_op_t _op;
}

- (instancetype)initWithChannel: (eb_chan_t)chan send: (BOOL)send obj: (id)obj {
        NSParameterAssert(chan);
    
    if (!(self = [super init])) {
        return nil;
    }
    
    _obj = [obj retain];
    
    _op.chan = chan;
    _op.send = send;
    _op.val = _obj;
    
    return self;
}

- (void)dealloc {
    [_obj release];
    _obj = nil;
    
    [super dealloc];
}

- (id)obj {
    return _obj;
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
+ (EBChannelOp *)do: (NSArray *)opsArray {
        NSParameterAssert(opsArray);
    
    size_t nops = [opsArray count];
    eb_chan_op_t *ops[nops];
    for (NSUInteger i = 0; i < nops; i++) {
        ops[i] = &((EBChannelOp *)opsArray[i])->_op;
    }
    
    size_t r = eb_chan_do(ops, nops);
        EBAssertOrRecover(r < nops, return nil);
    
    EBChannelOp *op = opsArray[r];
    if (op->_op.send) {
        /* ## Send op completed */
        /* Retain the object on behalf of the receiver */
        [op->_obj retain];
    } else {
        /* ## Recv op completed */
        /* Assign our op's obj ivar (it was retained on our behalf by the send side), and release the existing obj if necessary */
        [op->_obj release];
        op->_obj = op->_op.val;
    }
    
    return op;
}

+ (EBChannelOp *)try: (NSArray *)opsArray {
        NSParameterAssert(opsArray);
    size_t nops = [opsArray count];
    eb_chan_op_t *ops[nops];
    for (NSUInteger i = 0; i < nops; i++) {
        ops[i] = &((EBChannelOp *)opsArray[i])->_op;
    }
    
    size_t r = eb_chan_try(ops, nops);
    if (r == SIZE_MAX) {
        return nil;
    }
    
        EBAssertOrRecover(r < nops, return nil);
    return opsArray[r];
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