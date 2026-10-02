//
//  main.m
//  ZetaLoginItemHelper
//
//  Created by cbreak on 19.11.02.
//  Copyright © 2019 the-color-black.net. All rights reserved.
//

#import <Cocoa/Cocoa.h>

int main(int argc, const char * argv[])
{
	// Start the main ZetaWatch application
	// This trampoline is needed since SMLoginItemSetEnabled only allows adding
	// helpers to start at login, not the main bundle.
	// see ZetaWatch/ZetaWatchDelegate.mm for the login item adding code
	@autoreleasepool
	{
		// Start any ZetaWatch without adding a new instance
		[[NSWorkspace sharedWorkspace]
		 launchAppWithBundleIdentifier:@"org.openzfsonosx.ZetaWatch"
		 options:NSWorkspaceLaunchDefault
		 additionalEventParamDescriptor:0
		 launchIdentifier:0];
	}
	// Don't exit since ServiceManagement doesn't like it
	return NSApplicationMain(argc, argv);
}
