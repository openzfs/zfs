//
//  ZetaNotificationCenter.h
//  ZetaWatch
//
//  Created by cbreak on 19.08.25.
//  Copyright © 2019 the-color-black.net. All rights reserved.
//

#import "ZetaPoolWatcher.h"

/* CSTYLED */
#import <Cocoa/Cocoa.h>

@interface ZetaNotification : NSObject
{
}

@property(readonly) NSString * title;

@end

/* CSTYLED */
@interface ZetaNotificationCenter : NSObject <ZetaPoolWatcherDelegate>
{
}

- (ZetaNotification*)startAction:(NSString*)title;
- (void)stopAction:(ZetaNotification*)notification;
- (void)stopAction:(ZetaNotification*)notification withError:(NSError*)error;

- (void)errorDetected:(std::string const &)error;
- (void)errorDetectedInPool:(std::string const &)pool;

/* CSTYLED */
@property(readonly) NSArray<ZetaNotification*> * inProgressActions;

@property(weak) IBOutlet ZetaPoolWatcher * poolWatcher;

@end
