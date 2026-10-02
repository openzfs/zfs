//
//  ZetaWatchDelegate.h
//  ZetaWatch
//
//  Created by Gerhard Röthlin on 2015.12.20.
//  Copyright © 2015 the-color-black.net. All rights reserved.
//
//  Redistribution and use in source and binary forms, with or without
//  modification, are permitted provided that the conditions of the "3-Clause
//  BSD" license described in the BSD.LICENSE file are met. Additional licensing
//  options are described in the README file.
//

/* CSTYLED */
#import <Cocoa/Cocoa.h>

@interface ZetaWatchDelegate : NSObject
/* CSTYLED */
	<NSApplicationDelegate, NSUserNotificationCenterDelegate>

// NSUserNotificationCenterDelegate
- (BOOL)userNotificationCenter:(NSUserNotificationCenter *)center
    shouldPresentNotification:(NSUserNotification *)notification;

@end
