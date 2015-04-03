//
//  ViewController.h
//  IOSChannel
//
//  Created by Дмитрий Дорофеев on 02/04/15.
//  Copyright (c) 2015 YASP. All rights reserved.
//

#import <UIKit/UIKit.h>
@class EBChannel;

@interface ViewController : UIViewController

@property (nonatomic, strong, readonly) EBChannel *gChan;

@property (nonatomic, weak) IBOutlet UILabel * resultsLabel;

@end

