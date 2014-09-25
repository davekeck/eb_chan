#if __has_feature(objc_arc)
  #error For performance, EBChannel must be compiled with ARC disabled. (To do so, use the -fno-objc-arc compiler flag for this file.)
#endif

#import "EBChannel.h"
#import <EBFoundation/EBFoundation.h>
#import "eb_chan.h"
#import "eb_assert.h"

@interface EBChannel () {
    @public
    eb_chan _chan;
}
@end

@implementation EBChannelOp {
    @public
    EBChannel *_chan;
    eb_chan_op _op;
}

- (instancetype)initWithChannel: (EBChannel *)chan send: (BOOL)send obj: (id)obj {
        NSParameterAssert(chan);
        NSParameterAssert((bool)send == (bool)obj); /* If we're sending, we better have an object, or vice versa. */
    
    if (!(self = [super init])) {
        return nil;
    }
    
    _chan = [chan retain];
    
    _op.chan = _chan->_chan;
    _op.send = send;
    _op.val = [obj retain];
    
    return self;
}

- (void)dealloc {
    [(id)_op.val release];
    _op.val = nil;
    
    _op.chan = nil;
    
    [_chan release];
    _chan = nil;
    
    [super dealloc];
}

- (id)obj {
    return _op.val;
}

@end

@implementation EBChannel

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
+ (EBChannelOp *)select: (NSArray *)opsArray timeout: (NSTimeInterval)timeout {
        NSParameterAssert(opsArray);
    
    size_t nops = [opsArray count];
    eb_chan_op *ops[nops];
    NSUInteger i = 0;
    for (EBChannelOp *opObj in opsArray) {
        eb_chan_op *op = &opObj->_op;
        if (op->send) {
            /* Send ops retain their object on behalf of the receiver. We have to retain it before the actual
               send occurs, otherwise there's a race where the object could be released and deallocated on
               the other end before we retain it here, and then we retain it here, but we crash because the
               object was already deallocated. */
            [(id)op->val retain];
        } else {
            /* Reset receive ops' values before we start the _select() call, because the receive's val may be
               replaced if the op executes, in which case we'll have lost the ooportunity to release the old
               value. */
            [(id)op->val release];
            op->val = nil;
        }
        
        ops[i] = op;
        i++;
    }
    
    eb_nsec nsecTimeout = (timeout < 0 ? eb_nsec_forever : (eb_nsec)(timeout * eb_nsec_per_sec));
    eb_chan_op *r = eb_chan_select_list(nsecTimeout, ops, nops);
        /* Either we're non-blocking and it doesn't matter whether an op completed, or we're blocking and an op did complete */
        EBAssertOrRecover(nsecTimeout != eb_nsec_forever || r, return nil);
    
    EBChannelOp *result = nil;
    for (EBChannelOp *opObj in opsArray) {
        eb_chan_op *op = &opObj->_op;
        if (r == op) {
            result = opObj;
        } else if (op->send) {
            /* Release the send ops' values for each send op that didn't execute. */
            [(id)op->val release];
        }
    }
    
    return result;
}

- (EBChannelOp *)sendOp: (id)obj {
    return [[[EBChannelOp alloc] initWithChannel: self send: YES obj: obj] autorelease];
}

- (EBChannelOp *)recvOp {
    return [[[EBChannelOp alloc] initWithChannel: self send: NO obj: nil] autorelease];
}

- (void)close {
    eb_chan_close(_chan);
}

#pragma mark - Getters -
- (NSUInteger)bufferCapacity {
    return eb_chan_buf_cap(_chan);
}

- (NSUInteger)bufferLength {
    return eb_chan_buf_len(_chan);
}

@end