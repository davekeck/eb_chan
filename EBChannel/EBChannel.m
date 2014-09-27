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
    
    if (!(self = [super init])) {
        return nil;
    }
    
    _chan = [chan retain];
    _op = (eb_chan_op){.chan = _chan->_chan, .send = send, .val = [obj retain]};
    
    return self;
}

- (void)dealloc {
    [(id)_op.val release];
    [_chan release];
    
    _op = (eb_chan_op){.chan = NULL, .send = NO, .val = nil};
    _chan = nil;
    
    [super dealloc];
}

- (BOOL)open {
    return _op.open;
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
        eb_assert_or_recover(_chan, return nil);
    
    return self;
}

- (void)dealloc {
    if (_chan) {
        eb_chan_release(_chan);
        _chan = nil;
    }
    
    [super dealloc];
}

- (EBChannelResult)close {
    eb_chan_ret r = eb_chan_close(_chan);
    eb_assert_or_bail(r == eb_chan_ret_ok || r == eb_chan_ret_closed, "Unknown return value");
    if (r == eb_chan_ret_ok) {
        return EBChannelResultOK;
    }
    return EBChannelResultClosed;
}

#pragma mark - Getters -
- (NSUInteger)bufferCapacity {
    return eb_chan_buf_cap(_chan);
}

- (NSUInteger)bufferLength {
    return eb_chan_buf_len(_chan);
}

#pragma mark - Sending/receiving -
- (EBChannelResult)send: (id)obj {
    EBChannelOp *r = [EBChannel select: -1 ops: @[[self sendOp: obj]]];
    eb_assert_or_bail(r, "Invalid select return value");
    return (r->_op.open ? EBChannelResultOK : EBChannelResultClosed);
}

- (EBChannelResult)trySend: (id)obj {
    EBChannelOp *r = [EBChannel select: 0 ops: @[[self sendOp: obj]]];
    if (r) {
        return (r->_op.open ? EBChannelResultOK : EBChannelResultClosed);
    }
    return EBChannelResultStalled;
}

- (EBChannelResult)recv: (id *)obj {
    EBChannelOp *r = [EBChannel select: -1 ops: @[[self recvOp]]];
    eb_assert_or_bail(r, "Invalid select return value");
    if (r->_op.open && obj) {
        *obj = r->_op.val;
    }
    
    return (r->_op.open ? EBChannelResultOK : EBChannelResultClosed);
}

- (EBChannelResult)tryRecv: (id *)obj {
    EBChannelOp *r = [EBChannel select: 0 ops: @[[self recvOp]]];
    if (r) {
        if (r->_op.open && obj) {
            *obj = r->_op.val;
        }
        return (r->_op.open ? EBChannelResultOK : EBChannelResultClosed);
    }
    return EBChannelResultStalled;
}

#pragma mark - Multiplexing -
+ (EBChannelOp *)select: (NSTimeInterval)timeout ops: (NSArray *)opsArray {
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
        eb_assert_or_recover(nsecTimeout != eb_nsec_forever || r, return nil);
    
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

@end
