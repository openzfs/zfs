//
//  ZetaWatchDelegate.mm
//  ZetaWatch
//
//  Created by Gerhard Röthlin on 2015.12.20.
//  Copyright © 2015 the-color-black.net. All rights reserved.
//
//  Redistribution and use in source and binary forms, with or without modification, are permitted
//  provided that the conditions of the "3-Clause BSD" license described in the BSD.LICENSE file are met.
//  Additional licensing options are described in the README file.
//

#import "ZetaWatchDelegate.h"

#import "ZetaAuthorization.h"
#import "ZetaMainMenu.h"
#import "ZetaKeyLoader.h"
#import "ZetaQueryDialog.h"
#import "ZetaDictQueryDialog.h"
#import "ZetaConfirmDialog.h"

#import "ZFSUtils.hpp"

#import <ServiceManagement/SMLoginItem.h>

@interface ZetaWatchDelegate ()
{
	NSStatusItem * _statusItem;
}

@property (weak) IBOutlet NSMenu * zetaMenu;
@property (weak) IBOutlet ZetaAuthorization * authorization;
@property (weak) IBOutlet ZetaMainMenu * zetaMainMenu;
@property (weak) IBOutlet ZetaKeyLoader * zetaKeyLoader;
@property (weak) IBOutlet ZetaQueryDialog * zetaQueryDialog;
@property (weak) IBOutlet ZetaConfirmDialog * zetaConfirmDialog;
@property (weak) IBOutlet ZetaPoolWatcher * poolWatcher;
@property (weak) IBOutlet NSPopover * settings;

@property (strong) ZetaDictQueryDialog * zetaNewFSDialog;
@property (strong) ZetaDictQueryDialog * zetaNewVolDialog;

@end

@implementation ZetaWatchDelegate

- (void)applicationDidFinishLaunching:(NSNotification *)aNotification
{
	// Menu Item
	NSStatusBar * bar = [NSStatusBar systemStatusBar];
	_statusItem = [bar statusItemWithLength:NSSquareStatusItemLength];
	NSImage * zetaImage = [NSImage imageNamed:@"Zeta"];
	[zetaImage setTemplate:YES];
	_statusItem.button.image = zetaImage;
	_statusItem.menu = _zetaMenu;
	_zetaKeyLoader.statusItem = _statusItem;
	_zetaQueryDialog.statusItem = _statusItem;
	_zetaConfirmDialog.statusItem = _statusItem;
	// Dialogs
	_zetaNewFSDialog = [[ZetaDictQueryDialog alloc] initWithDialog:@"NewFS"];
	_zetaNewFSDialog.statusItem = _statusItem;
	_zetaMainMenu.zetaNewFSDialog = _zetaNewFSDialog;
	_zetaNewVolDialog = [[ZetaDictQueryDialog alloc] initWithDialog:@"NewVol"];
	_zetaNewVolDialog.statusItem = _statusItem;
	_zetaMainMenu.zetaNewVolDialog = _zetaNewVolDialog;
	// Watcher
	[[self poolWatcher] checkForChanges];
	// User Notification Center Delegate
	[[NSUserNotificationCenter defaultUserNotificationCenter] setDelegate:self];
	// Login Item
	[[NSUserDefaults standardUserDefaults] addObserver:self forKeyPath:@"startAtLogin"
											   options:NSKeyValueObservingOptionInitial
											   context:nullptr];
}

- (void)applicationWillFinishLaunching:(NSNotification *)notification
{
	// User Defaults
	[[NSUserDefaults standardUserDefaults] registerDefaults:@{
		@"autoUnlock": @YES,
		@"autoImport": @YES,
		@"allowHostIDMismatch": @NO,
		@"useKeychain": @NO,
		@"startAtLogin": @YES,
		@"keepAwakeDuringScrub": @YES,
		@"defaultAltroot": @"/Volumes",
		@"useAltroot": @NO,
		@"searchPathOverride": @[
				@"/var/run/disk/by-serial",
				@"/var/run/disk/by-id",
		],
	}];
}

- (void)applicationWillTerminate:(NSNotification *)aNotification
{
	[[NSUserDefaults standardUserDefaults] removeObserver:self forKeyPath:@"startAtLogin"];
	[self.authorization stopHelper];
}

- (BOOL)userNotificationCenter:(NSUserNotificationCenter *)center
	 shouldPresentNotification:(NSUserNotification *)notification
{
	return YES;
}

- (void)observeValueForKeyPath:(NSString *)keyPath
					  ofObject:(id)object
						change:(NSDictionary<NSKeyValueChangeKey, id> *)change
					   context:(void *)context
{
	if ([keyPath isEqualToString:@"startAtLogin"])
	{
		bool startAtLogin = [object boolForKey:@"startAtLogin"];
		SMLoginItemSetEnabled(CFSTR("org.openzfsonosx.ZetaLoginItemHelper"), startAtLogin);
	}
}

- (IBAction)showSettings:(id)sender
{
	NSView * positioningView = [_statusItem button];
	[_settings showRelativeToRect:NSMakeRect(0, 0, 0, 0)
						   ofView:positioningView preferredEdge:NSRectEdgeMinY];
}

@end
