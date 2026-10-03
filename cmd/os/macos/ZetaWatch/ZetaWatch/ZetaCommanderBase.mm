//
//  ZetaCommanderBase.mm
//  ZetaWatch
//
//  Created by cbreak on 19.06.02.
//  Copyright © 2019 the-color-black.net. All rights reserved.
//

#import "ZetaCommanderBase.h"

@implementation ZetaCommanderBase

- (void)notifySuccessWithTitle:(NSString*)title text:(NSString*)text
{
	NSUserNotification * notification = [[NSUserNotification alloc] init];
	notification.title = title;
	notification.informativeText = text == nil ? title : text;
	notification.hasActionButton = NO;
	auto nc = [NSUserNotificationCenter defaultUserNotificationCenter];
	[nc deliverNotification:notification];
	[nc performSelector:@selector(removeDeliveredNotification:)
			 withObject:notification afterDelay:10];
}

- (void)notifyErrorFromHelper:(NSError*)error
{
	NSUserNotification * notification = [[NSUserNotification alloc] init];
	notification.title = NSLocalizedString(@"ZetaWatch Error", @"Helper Error notification Title");
	NSString * errorFormat = NSLocalizedString(@"%@.", @"Helper Error notification Format");
	notification.informativeText = [NSString stringWithFormat:errorFormat, [error localizedDescription]];
	notification.hasActionButton = NO;
	[[NSUserNotificationCenter defaultUserNotificationCenter] deliverNotification:notification];
}

- (IBAction)copyRepresentedObject:(id)sender
{
	NSPasteboard * pb = [NSPasteboard generalPasteboard];
	[pb clearContents];
	[pb writeObjects:@[[sender representedObject]]];
}

@end
