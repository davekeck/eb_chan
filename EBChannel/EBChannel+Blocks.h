#import <Foundation/Foundation.h>
#import "EBChannel.h"

typedef void(^EBChannelHandler)(BOOL open, id obj);

@interface EBChannel (Blocks)

/* ## Methods */
+ (void)select: (NSTimeInterval)timeout opsAndHandlers: (NSArray *)opsAndHandlers;
+ (EBChannelOp *)default;

@end
