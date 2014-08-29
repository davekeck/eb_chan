#import <Foundation/Foundation.h>
#import "eb_chan.h"

@interface EBChannelOp : NSObject {
    @public
    eb_chan_op_t _op;
}
- (id)obj; /* The object received/sent. For a receive operation, nil signifies that the channel was closed. */
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
