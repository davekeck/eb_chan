#import <Foundation/Foundation.h>

@interface EBChannelOp : NSObject
- (BOOL)open; /* YES if the op completed due to a successful send/recv operation, NO if the op completed because the channel is closed. */
- (id)obj; /* The object to be sent or the object that was received */
@end

typedef enum {
    EBChannelResultOK,      /* Success */
    EBChannelResultClosed,  /* Failed because the channel is closed */
    EBChannelResultStalled, /* Failed because the send/recv couldn't proceed without blocking (applies to -trySend:/-tryRecv:) */
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
/* Returns the op that completed, or nil on timeout. Use a negative timeout to signify 'forver'. */
+ (EBChannelOp *)select: (NSTimeInterval)timeout ops: (NSArray *)ops;
- (EBChannelOp *)sendOp: (id)obj;
- (EBChannelOp *)recvOp;

@end

/* ## Blocks support */
typedef void(^EBChannelHandler)(BOOL open, id obj);
@interface EBChannel (Blocks)

/* ## Methods */
/* Executes the default block upon timeout */
+ (void)select: (NSTimeInterval)timeout opsAndHandlers: (NSArray *)opsAndHandlers;
+ (EBChannelOp *)defaultOp;

@end