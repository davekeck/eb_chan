#import "EBChannel.h"
#import <mach/mach.h>
#import <EBFoundation/EBFoundation.h>
#import <libkern/OSAtomic.h>

static NSString *const EBChannelClosedException = @"EBChannelClosedException";

@interface EBMachPort : NSObject
@end

@implementation EBMachPort {
    mach_port_t _port;
}

- (instancetype)init {
    if (!(self = [super init])) {
        return nil;
    }
    
    _port = MACH_PORT_NULL;
    
    /* Create our receive right, and insert a send right. */
    kern_return_t r = mach_port_allocate(mach_task_self(), MACH_PORT_RIGHT_RECEIVE, &_port);
        EBAssertOrRecover(r == KERN_SUCCESS, return nil);
    r = mach_port_insert_right(mach_task_self(), _port, _port, MACH_MSG_TYPE_MAKE_SEND);
        EBAssertOrRecover(r == KERN_SUCCESS, return nil);
    
    return self;
}

- (void)signal {
    mach_msg_empty_send_t m = {
        .header = {
            .msgh_bits = MACH_MSGH_BITS(MACH_MSGH_BITS_REMOTE(MACH_MSG_TYPE_COPY_SEND), MACH_MSGH_BITS_LOCAL(MACH_MSGH_BITS_ZERO)),
            .msgh_size = sizeof(m),
            .msgh_remote_port = _port,
            .msgh_local_port = MACH_PORT_NULL,
            .msgh_id = 0,
        }
    };
    
    /* Send a message to our port, but don't block if the buffer's full. */
    mach_msg_return_t r = mach_msg(&m.header, MACH_SEND_MSG | MACH_SEND_TIMEOUT, sizeof(m), 0, MACH_PORT_NULL, 0, MACH_PORT_NULL);
        EBAssertOrRecover(r == MACH_MSG_SUCCESS || r == MACH_SEND_TIMED_OUT, EBNoOp);
}

- (void)wait {
    /* Wait indefinitely for a message to come on our port */
    mach_msg_empty_rcv_t m;
    mach_msg_return_t r = mach_msg(&m.header, MACH_RCV_MSG, 0, sizeof(m), _port, MACH_MSG_TIMEOUT_NONE, MACH_PORT_NULL);
        EBAssertOrRecover(r == MACH_MSG_SUCCESS, EBNoOp);
}

- (void)dealloc {
    if (_port != MACH_PORT_NULL) {
        kern_return_t r = mach_port_destroy(mach_task_self(), _port);
            EBAssertOrRecover(r == KERN_SUCCESS, EBNoOp);
        _port = MACH_PORT_NULL;
    }
}

@end

@implementation EBChannelOp {
    @public
    EBChannel *_chan;
    BOOL _send;
    id _obj;
}

- (instancetype)initWithChannel: (EBChannel *)chan send: (BOOL)send obj: (id)obj {
        NSParameterAssert(chan);
    
    if (!(self = [super init])) {
        return nil;
    }
    
    _chan = chan;
    _send = send;
    _obj = obj;
    
    return self;
}

- (id)obj {
    return _obj;
}

@end

@implementation EBChannel {
    NSUInteger _bufferSize;
    OSSpinLock _lock;
    NSMutableSet *_sends;
    NSMutableSet *_recvs;
    BOOL _open;
    
    /* Buffered ivars */
    NSMutableArray *_buf;
    
    /* Unbuffered ivars */
    id _unbufObj;
    EBMachPort *_unbufPort;
    BOOL _unbufRecvd;
}

#pragma mark - Creation -
+ (void)initialize {
    static dispatch_once_t onceToken;
    dispatch_once(&onceToken, ^{
        /* Seed the random-number generator for shuffledArray() */
        srandom((unsigned int)time(nil));
    });
}

- (instancetype)initWithBufferSize: (NSUInteger)bufferSize {
    if (!(self = [super init])) {
        return nil;
    }
    
    _bufferSize = bufferSize;
    _lock = OS_SPINLOCK_INIT;
    _sends = [NSMutableSet new];
    _recvs = [NSMutableSet new];
    _open = YES;
    
    if (_bufferSize) {
        /* Buffered */
        _buf = [NSMutableArray new];
    } else {
        /* Unbuffered */
        _unbufObj = nil;
        _unbufPort = [EBMachPort new];
        _unbufRecvd = NO;
    }
    
    return self;
}

#pragma mark - Methods -
NS_INLINE BOOL setHasMoreThanObj(NSSet *set, id obj) {
        NSCParameterAssert(set);
    NSUInteger count = [set count];
    return ((!obj && count > 0) || (count > 1) || (count == 1 && ![set containsObject: obj]));
}

NS_INLINE void signalPorts(NSSet *ports, EBMachPort *exclude) {
    for (EBMachPort *p in ports) {
        if (p != exclude) {
            [p signal];
        }
    }
}

NS_INLINE BOOL sendBuffered(EBMachPort *port, EBChannel *chan, id obj) {
        NSCParameterAssert(chan);
        NSCParameterAssert(obj);
    BOOL open = NO;
    BOOL result = NO;
    NSSet *wakeupPorts = nil;
    OSSpinLockLock(&chan->_lock);
        open = chan->_open;
        if (open) {
            if ([chan->_buf count] < chan->_bufferSize) {
                /* ## Sending, buffered, channel open, buffer has space */
                /* Add the object to the buffer */
                [chan->_buf addObject: obj];
                /* Copy the channel's _recvs so that we can signal them after we relinquish the lock, to notify
                   them that we've added to the buffer. */
                wakeupPorts = [chan->_recvs copy];
                /* Set our flag signifying that we completed this op. */
                result = YES;
            } else {
                /* ## Sending, buffered, channel open, buffer full */
                if (port) {
                    /* Add our port to the channel's _sends, so that we get notified when someone receives on the channel. */
                    [chan->_sends addObject: port];
                }
            }
        }
    OSSpinLockUnlock(&chan->_lock);
    
    if (!open) {
        [NSException raise: EBChannelClosedException format: @"Can't send on closed channel."];
    }
    
    /* Signal every port in signalPorts */
    // TODO: should we exclude 'port' here?
    signalPorts(wakeupPorts, port);
    return result;
}

NS_INLINE BOOL sendUnbuffered(EBMachPort *port, EBChannel *chan, id obj) {
        NSCParameterAssert(chan);
        NSCParameterAssert(obj);
    // TODO: add a fast path here that doesn't require using ports
    BOOL open = NO;
    BOOL sending = NO;
    NSSet *wakeupPorts = nil;
    OSSpinLockLock(&chan->_lock);
        open = chan->_open;
        if (open) {
            /* Check that chan->_recvs has more receivers than just our own port */
            if (setHasMoreThanObj(chan->_recvs, port) && !chan->_unbufObj) {
                /* ## Sending, unbuffered, channel open, receivers exist and there's no sender yet */
                /* Assign the object that we're sending */
                chan->_unbufObj = obj;
                /* Reset the _unbufRecvd flag so we can tell whether the object was successfully received when
                   we get notified on _unbufPort */
                chan->_unbufRecvd = NO;
                /* Copy the channel's _recvs so that we can message them after we relinquish the lock, to notify
                   them that an object's been sent. */
                wakeupPorts = [chan->_recvs copy];
                /* Set our local flag marking that we're actually sending an object. */
                sending = YES;
            } else {
                /* ## Sending, unbuffered, channel open, no receivers or a sender already has control */
                if (port) {
                    /* Add our port to the channel's _sends, so that we get notified when a receiver arrives. */
                    [chan->_sends addObject: port];
                }
            }
        }
    OSSpinLockUnlock(&chan->_lock);
    
    if (!open) {
        [NSException raise: EBChannelClosedException format: @"Can't send on closed channel."];
    }
    
    /* Signal every port in signalPorts */
    // TODO: should we exclude 'port' here?
    signalPorts(wakeupPorts, port);
    wakeupPorts = nil;
    
    /* If we're actually sending an object, wait until a receiver notifies the channel's _unbufPort */
    BOOL result = NO;
    if (sending) {
        BOOL doneSending = NO;
        while (!doneSending) {
            /* Wait until a receiver signals us. */
            [chan->_unbufPort wait];
            /* When we're signaled, it's either because a receiver has received the object, or the channel's _recvs
               changed so we need to make sure that we should still be blocking and trying to send. */
            OSSpinLockLock(&chan->_lock);
                open = chan->_open;
                if (open) {
                    BOOL recvd = chan->_unbufRecvd;
                    BOOL hasRecvs = setHasMoreThanObj(chan->_recvs, port);
                    if (recvd || !hasRecvs) {
                        /* Someone received our object, or there are no more receivers */
                        if (recvd) {
                            /* We completed an op so set our result, which will cause us to exit from our outer loop. */
                            result = YES;
                        } else if (!hasRecvs) {
                            if (port) {
                                /* No receiver got our object and now there are no more receivers, so add our port to the
                                   channel's _sends, so that we get notified when a receiver arrives. */
                                [chan->_sends addObject: port];
                            }
                        }
                        
                        /* Clear the channel's _unbufObj. */
                        chan->_unbufObj = nil;
                        /* Copy the channel's sends so we can wake them up (after we relinquish the lock), so that one of them can send. */
                        wakeupPorts = [chan->_sends copy];
                        /* Set our flag so that we exit our inner loop that's waiting for someone to receive our object */
                        doneSending = YES;
                    }
                }
            OSSpinLockUnlock(&chan->_lock);
            
            if (!open) {
                [NSException raise: EBChannelClosedException format: @"Can't send on closed channel."];
            }
        }
    }
    
    /* Signal every port in signalPorts */
    // TODO: should we exclude 'port' here?
    signalPorts(wakeupPorts, port);
    return result;
}

NS_INLINE BOOL recvBuffered(EBMachPort *port, EBChannel *chan, id *outObj) {
        NSCParameterAssert(chan);
        NSCParameterAssert(outObj);
    BOOL result = NO;
    NSSet *wakeupPorts = nil;
    OSSpinLockLock(&chan->_lock);
        if ([chan->_buf count]) {
            /* ## Receiving, buffered, buffer non-empty */
            /* Set our resulting object */
            *outObj = chan->_buf[0];
            /* Pop off the head of the buffer */
            [chan->_buf removeObjectAtIndex: 0];
            /* Copy the channel's _sends so that we can message them after we relinquish the lock, to notify
               them that we've removed from the buffer. */
            wakeupPorts = [chan->_sends copy];
            /* Set our flag signifying that we completed this op. */
            result = YES;
        } else if (!chan->_open) {
            /* ## Receiving, buffered, buffer empty, channel closed */
            /* Set our resulting object to nil (signifying to the client that the channel's closed), and set
               our flag signifying that we completed this op. */
            *outObj = nil;
            result = YES;
        } else {
            /* ## Receiving, buffered, buffer empty, channel open */
            if (port) {
                /* Add our port to the channel's _recvs, so that we get notified when someone sends on the channel. */
                [chan->_recvs addObject: port];
            }
        }
    OSSpinLockUnlock(&chan->_lock);
    
    /* Signal every port in signalPorts */
    // TODO: should we exclude 'port' here?
    signalPorts(wakeupPorts, port);
    return result;
}

NS_INLINE BOOL recvUnbuffered(EBMachPort *port, EBChannel *chan, id *outObj) {
        NSCParameterAssert(chan);
        NSCParameterAssert(outObj);
    BOOL result = NO;
    NSSet *wakeupPorts = nil;
    OSSpinLockLock(&chan->_lock);
        if (chan->_unbufObj && !chan->_unbufRecvd) {
            /* ## Receiving, unbuffered, object is available */
            /* Set our result */
            *outObj = chan->_unbufObj;
            /* Set the channel's flag marking that someone received its object. */
            chan->_unbufRecvd = YES;
            /* Signal the channel's _unbufPort after we relinquish the lock, to notify the sender that we received its object. */
            wakeupPorts = [NSSet setWithObject: chan->_unbufPort];
            /* Set our flag signifying that we completed this op. */
            result = YES;
        } else if (!chan->_open) {
            /* ## Receiving, unbuffered, no object available, channel closed */
            /* Set our resulting object to nil (signifying to the client that the channel's closed), and set
               our flag signifying that we completed this op. */
            *outObj = nil;
            result = YES;
        } else {
            /* ## Receiving, unbuffered, no object available, channel open */
            if (port) {
                [chan->_recvs addObject: port];
                wakeupPorts = [chan->_sends copy];
            }
        }
    OSSpinLockUnlock(&chan->_lock);
    
    /* Signal every port in signalPorts */
    // TODO: should we exclude 'port' here?
    signalPorts(wakeupPorts, port);
    return result;
}

NS_INLINE void removePortFromChan(EBMachPort *port, EBChannel *chan) {
        NSCParameterAssert(port);
        NSCParameterAssert(chan);
    BOOL signal = NO;
    OSSpinLockLock(&chan->_lock);
        [chan->_sends removeObject: port];
        
        /* Remember _recvs's count before we remove port, so we can quickly see if we removed it by
           comparing recvCount, without having to search for it again. */
        NSUInteger recvCount = [chan->_recvs count];
        [chan->_recvs removeObject: port];
        /* If there's a sender trying to send an object on this channel (and the channel's unbuffered, as _unbufObj
           will only be set if it's unbuffered), and we actually removed a receiver from _recvs, we need to wake up
           the sender so that it can check if it still should be trying to send there might not be any receivers anymore. */
        if (chan->_unbufObj && recvCount != [chan->_recvs count]) {
            signal = YES;
        }
    OSSpinLockUnlock(&chan->_lock);
    
    if (signal) {
        [chan->_unbufPort signal];
    }
}

NS_INLINE NSArray *shuffledArray(NSArray *a) {
        NSCParameterAssert(a);
//    #warning debug
//    return a;
    
    NSMutableArray *result = [NSMutableArray new];
    for (id o in a) {
        [result insertObject: o atIndex: (random() % ([result count] + 1))];
    }
    return result;
}

NS_INLINE EBChannelOp *tryOp(EBMachPort *port, EBChannelOp *op) {
        NSCParameterAssert(op);
    EBChannel *chan = op->_chan;
    if (op->_send) {
        /* ## Send */
        BOOL sent = (chan->_bufferSize ? sendBuffered(port, chan, op->_obj) : sendUnbuffered(port, chan, op->_obj));
        if (sent) {
            return op;
        }
    } else {
        /* ## Receive */
        id obj = nil;
        BOOL recvd = (chan->_bufferSize ? recvBuffered(port, chan, &obj) : recvUnbuffered(port, chan, &obj));
        if (recvd) {
            op->_obj = obj;
            return op;
        }
    }
    return nil;
}

+ (EBChannelOp *)do: (NSArray *)ops {
        NSCParameterAssert(ops);
    EBMachPort *port = nil;
    EBChannelOp *result = nil;
    try: {
        /* ## Fast path: loop randomly over our operations to see if one of them was able to send/receive.
           If not, we'll enter the slow path where we put our thread to sleep. */
        NSUInteger opsCount = [ops count];
        if (opsCount) {
            static const NSUInteger kFastPathAttemptMultiplier = 100;
            NSUInteger loopCount = opsCount * kFastPathAttemptMultiplier;
            @autoreleasepool {
                for (NSUInteger i = 0; i < loopCount; i++) {
                    result = tryOp(nil, ops[(random() % opsCount)]);
                    /* If the op completed, we need to exit! */
                    if (result) {
                        goto cleanup;
                    }
                }
            }
        }
        
        /* ## Slow path: we weren't able to find an operation that could send/receive, so we'll create a
           port to receive notifications on and put this thread to sleep until someone wakes us up. */
        /* Create our port that we'll attach to channels so that we can be notified when events occur. */
        port = [EBMachPort new];
        /* First, register our port for the appropriate notifications on every channel. */
        for (EBChannelOp *op in shuffledArray(ops)) {
            result = tryOp(port, op);
            /* If this op completed, we need to exit! */
            if (result) {
                goto cleanup;
            }
        }
        
        for (;;) {
            @autoreleasepool {
                /* Go to sleep until someone alerts us of an event */
                [port wait];
                for (EBChannelOp *op in shuffledArray(ops)) {
                    // TODO: we still want to supply port here so that we don't notify our own port, right? we still want to prevent adding that port to channels' sets though.
                    result = tryOp(nil, op);
                    /* If the op completed, we need to exit! */
                    if (result) {
                        goto cleanup;
                    }
                }
            }
        }
    }
    
    /* Cleanup! */
    cleanup: {
        if (port) {
            for (EBChannelOp *op in ops) {
                removePortFromChan(port, op->_chan);
            }
        }
    }
    
    return result;
}

+ (EBChannelOp *)try: (NSArray *)ops {
    for (EBChannelOp *op in shuffledArray(ops)) {
        EBChannelOp *r = tryOp(nil, op);
        /* If the op completed, return it! */
        if (r) {
            return r;
        }
    }
    return nil;
}

- (EBChannelOp *)send: (id)obj {
        NSParameterAssert(obj);
    return [[EBChannelOp alloc] initWithChannel: self send: YES obj: obj];
}

- (EBChannelOp *)recv {
    return [[EBChannelOp alloc] initWithChannel: self send: NO obj: nil];
}

- (void)close {
    /* Mark ourself as closed, and wake up senders/receivers so that they notice our new state and act appropriately. */
    NSMutableSet *wakeupPorts = [NSMutableSet new];
    BOOL wasOpen = NO;
    OSSpinLockLock(&_lock);
        wasOpen = _open;
        _open = NO;
        
        [wakeupPorts unionSet: _sends];
        [wakeupPorts unionSet: _recvs];
    OSSpinLockUnlock(&_lock);
    
    /* Raise an exception if this channel was already closed before we tried to close it this time. */
    if (!wasOpen) {
        [NSException raise: EBChannelClosedException format: @"Can't close a channel that's already closed."];
    }
    
    /* Wake up our senders/receivers so they notice our new closed state. */
    signalPorts(wakeupPorts, nil);
}

#pragma mark - Getters -
- (NSUInteger)queueLength {
        EBConfirmOrPerform(_bufferSize, return 0);
    
    NSUInteger r = 0;
    OSSpinLockLock(&_lock);
        r = [_buf count];
    OSSpinLockUnlock(&_lock);
    return r;
}

- (NSUInteger)bufferSize {
    return _bufferSize;
}

@end