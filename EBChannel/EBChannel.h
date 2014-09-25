#import <Foundation/Foundation.h>
#import "eb_chan.h"

@interface EBChannelOp : NSObject
/* When sending, -obj returns the object that will be sent. */
/* When receiving, -obj returns the object that was received, where nil signifies that receive completed due to a closed channel. */
- (id)obj;
@end

@interface EBChannel : NSObject

/* ## Creation */
- (instancetype)initWithBufferCapacity: (NSUInteger)bufferCapacity;

/* ## Methods */
+ (EBChannelOp *)select: (NSArray *)ops timeout: (NSTimeInterval)timeout;
- (EBChannelOp *)sendOp: (id)obj;
- (EBChannelOp *)recvOp;
- (void)close;

/* ## Convenience wrappers */
/* Sends a value on a channel, blocking until it can do so. */
- (void)send: (id)obj;
/* Sends a value on a channel and returns true, or returns false if the value couldn't be sent without blocking. */
- (BOOL)trySend: (id)obj

/* Receives a value from a channel, blocking until it can do so. */
- (id)recv;
/*  */
- (id)tryRecv: 



/* Sends a value on a channel, blocking until it can do so. */
static inline void eb_chan_send(eb_chan c, const void *val) {
    eb_chan_op op = eb_chan_op_send(c, val);
    assert(eb_chan_select(eb_nsec_forever, &op));
}

/* Sends a value on a channel and returns true, or returns false if the value couldn't be sent without blocking. */
static inline bool eb_chan_try_send(eb_chan c, const void *val) {
    eb_chan_op op = eb_chan_op_send(c, val);
    return (eb_chan_select(eb_nsec_zero, &op) != NULL);
}

/* Receives a value from a channel, blocking until it can do so.
   Returns true if the value was generated due to a successful send, or false if the channel is closed.
   'val' is only valid if true is returned. */
static inline bool eb_chan_recv(eb_chan c, const void **val) {
    eb_chan_op op = eb_chan_op_recv(c);
    assert(eb_chan_select(eb_nsec_forever, &op));
    if (val) {
        *val = op.val;
    }
    return op.open;
}

/* Receives a value from a channel and returns true, or returns false if no value could be received without blocking.
   'open' is set to true if the value was generated due to a successful send, or false if the channel is closed.
   'val' is only valid if 'open' is true. */
static inline bool eb_chan_try_recv(eb_chan c, bool *open, const void **val) {
    eb_chan_op op = eb_chan_op_recv(c);
    bool result = (eb_chan_select(eb_nsec_zero, &op) != NULL);
    if (result) {
        if (open) {
            *open = op.open;
        }
        if (val) {
            *val = op.val;
        }
    }
    return result;
}






/* ## Getters */
- (NSUInteger)bufferCapacity;
- (NSUInteger)bufferLength;

@end
