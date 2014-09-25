#import <Foundation/Foundation.h>

@interface EBChannelOp : NSObject
- (BOOL)open; /* YES if the op completed due to a successful send/recv operation, NO if the op completed because the channel is closed. */
- (id)obj; /* The object to be sent or the object that was received */
@end

typedef enum {
    EBChannelResultOK,      /* Success */
    EBChannelResultClosed,  /* Failed because the channel is closed */
    EBChannelResultStalled, /* Failed because the send/recv couldn't proceed without blocking */
} EBChannelResult;

@interface EBChannel : NSObject

/* ## Creation/lifecycle */
- (instancetype)initWithBufferCapacity: (NSUInteger)bufferCapacity;
- (EBChannelResult)close;

/* ## Getters */
- (NSUInteger)bufferCapacity;
- (NSUInteger)bufferLength;

/* ## Sending/receiving */
- (EBChannelResult)send: (id)obj;
- (EBChannelResult)trySend: (id)obj;
- (EBChannelResult)recv: (id *)obj;
- (EBChannelResult)tryRecv: (id *)obj;

/* ## Multiplexing */
+ (EBChannelOp *)select: (NSArray *)ops timeout: (NSTimeInterval)timeout; /* Returns the op that completed, or nil on timeout. */
- (EBChannelOp *)sendOp: (id)obj;
- (EBChannelOp *)recvOp;

@end
