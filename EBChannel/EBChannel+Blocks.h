#import <Foundation/Foundation.h>
#import "EBChannel.h"

typedef void(^EBChannelHandler)(id obj);

@interface EBChannel (Blocks)

/* Methods */
+ (void)select: (NSArray *)opsAndHandlers;
+ (EBChannelOp *)default;

@end
