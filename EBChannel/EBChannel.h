#import <Foundation/Foundation.h>

@interface EBChannelOp : NSObject
- (id)obj; /* For a receive operation, nil signifies that the channel was closed. */
@end

@interface EBChannel : NSObject

/* Creation */
- (instancetype)initWithBufferCapacity: (NSUInteger)bufferCapacity;

/* Methods */
+ (EBChannelOp *)do: (NSArray *)ops;
+ (EBChannelOp *)try: (NSArray *)ops;

- (EBChannelOp *)send: (id)obj;
- (EBChannelOp *)recv;
- (void)close;

/* Getters */
- (NSUInteger)bufferCapacity;
- (NSUInteger)bufferLength;

@end
