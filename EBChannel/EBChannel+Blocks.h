#import <Foundation/Foundation.h>
#import "EBChannel.h"

typedef void(^EBChannelHandler)(id obj);

@interface EBChannel (Blocks)

/* Methods */
+ (EBChannelOp *)default;
+ (void)select: (NSArray *)opsAndHandlers;

@end

//for (;;) {
//    [EBChannel do: @[[writeChannel send: @"hello"]]];
//    
//    EBChannelOp *write = [writeChannel send: @"hello"];
//    EBChannelOp *read = [readChannel recv];
//    EBChannelOp *r = [EBChannel try: @[write, read]];
//    
//    if (r == write) {
//        
//    } else if (r == read) {
//        
//    } else if (!r) {
//        
//    }
//}











//@interface EBChannel : NSObject
//
///* Creation */
//- (instancetype)initWithBufferSize: (NSUInteger)bufferSize;
//
///* Methods */
//+ (void)select: (NSArray *)ops;
//+ (id)default;
//
//- (id)send: (id)obj;
//- (id)recv;
//- (void)close;
//
///* Getters */
//- (NSUInteger)queueLength;
//- (NSUInteger)bufferSize;
//
//@end
//
//
//for (;;) {
//    [EBChannel select: @[
//    [recvChannel recv], ^(id obj){
//        
//    },
//    
//    [sendChannel send: @"hallo"], ^{
//        
//    },
//    
//    [EBChannel default], ^{
//        
//    }]];
//}