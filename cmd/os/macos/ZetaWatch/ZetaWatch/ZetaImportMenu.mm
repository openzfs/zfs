//
//  ZetaImportMenu.m
//  ZetaWatch
//
//  Created by cbreak on 19.06.02.
//  Copyright © 2019 the-color-black.net. All rights reserved.
//

#import "ZetaImportMenu.h"

#import "ZetaAuthorization.h"
#import "ZetaPoolWatcher.h"

#include "ZFSStrings.hpp"

@implementation ZetaImportMenu

- (void)menuNeedsUpdate:(NSMenu*)menu
{
	[menu removeAllItems];
	NSMenuItem * importAllItem = [menu addItemWithTitle:@"Import all Pools" action:@selector(importAllPools:) keyEquivalent:@""];
	importAllItem.target = self;
	[menu addItem:[NSMenuItem separatorItem]];
	auto importablePools = [self.autoImporter importablePools];
	if (importablePools.size() > 0)
	{
		auto addPoolImportMenu = [self](NSMenu * menu, auto const & pool)
		{
			NSString * title = [NSString stringWithFormat:@"%s %@ (%llu)",
				pool.name.c_str(),
				zfs::emojistring_pool_status_t(pool.status),
				pool.guid];
			NSMenuItem * item = [menu addItemWithTitle:title action:@selector(importPool:) keyEquivalent:@""];
			[item setAction:@selector(importPool:)];
			[item setTarget:self];
			// Communicate pool to callback
			NSNumber * guid = [NSNumber numberWithUnsignedLongLong:pool.guid];
			NSString * name = [NSString stringWithUTF8String:pool.name.c_str()];
			NSMutableDictionary * poolDict = [NSMutableDictionary dictionary];
			poolDict[@"poolGUID"] = guid;
			poolDict[@"poolName"] = name;
			[item setRepresentedObject:poolDict];
			return poolDict;
		};
		for (auto const & pool : importablePools)
		{
			addPoolImportMenu(menu, pool);
		}
		[menu addItem:[NSMenuItem separatorItem]];
		// Read-Only Menu
		NSMenuItem * importROItem = [menu addItemWithTitle:
			NSLocalizedString(@"Import Read Only", @"Import Read Only") action:NULL keyEquivalent:@""];
		NSMenu * importROMenu = [[NSMenu alloc] init];
		importROItem.submenu = importROMenu;
		for (auto const & pool : importablePools)
		{
			auto dict = addPoolImportMenu(importROMenu, pool);
			dict[@"readOnly"] = @YES;
		}
		// Read-Write Menu
		NSMenuItem * importRWItem = [menu addItemWithTitle:
			NSLocalizedString(@"Import Read Write", @"Import Read Write") action:NULL keyEquivalent:@""];
		NSMenu * importRWMenu = [[NSMenu alloc] init];
		importRWItem.submenu = importRWMenu;
		for (auto const & pool : importablePools)
		{
			auto dict = addPoolImportMenu(importRWMenu, pool);
			dict[@"readOnly"] = @NO;
		}
	}
	else
	{
		[_importMenu addItemWithTitle:NSLocalizedString(@"No importable Pools found",
														@"No Importable Pools")
							   action:NULL keyEquivalent:@""];
	}
}

- (void)importablePoolsDiscovered:(NSDictionary*)importablePools
{
}

- (void)importablePoolsError:(NSError*)error
{
	NSMenuItem * item = [_importMenu itemAtIndex:2];
	[item setTitle:[error localizedDescription]];
}

- (IBAction)importPool:(id)sender
{
	NSDictionary * pool = [sender representedObject];
	NSMutableDictionary * mutablePool = [pool mutableCopy];
	auto defaults = [NSUserDefaults standardUserDefaults];
	if ([defaults boolForKey:@"useAltroot"])
	{
		[mutablePool setObject:[defaults stringForKey:@"defaultAltroot"] forKey:@"altroot"];
	}
	if (auto spo = [defaults arrayForKey:@"searchPathOverride"])
	{
		[mutablePool setObject:spo forKey:@"searchPathOverride"];
	}
	// Direct imports always allow unhealthy pools with mismatching host IDs.
	[mutablePool setObject:@YES forKey:@"allowUnhealthy"];
	[mutablePool setObject:@YES forKey:@"allowHostIDMismatch"];
	[_authorization importPools:mutablePool withReply:^(NSError * error)
	 {
		 if (!error)
		 {
			 NSString * title = [NSString stringWithFormat:
				NSLocalizedString(@"Pool %@ imported",
								  @"Pool Import Success short format"),
				pool[@"poolName"]];
			 NSString * text = [NSString stringWithFormat:
				NSLocalizedString(@"%@ (%@) imported",
								  @"Pool Import Success format"),
				pool[@"poolName"], pool[@"poolGUID"]];
			 [self notifySuccessWithTitle:title text:text];
		 }
		 [self handlePoolChangeReply:error];
	 }];
}

- (IBAction)importAllPools:(id)sender
{
	NSMutableDictionary * mutablePool = [NSMutableDictionary dictionary];
	auto defaults = [NSUserDefaults standardUserDefaults];
	if ([defaults boolForKey:@"allowHostIDMismatch"])
	{
		[mutablePool setObject:@YES forKey:@"allowHostIDMismatch"];
	}
	if ([defaults boolForKey:@"useAltroot"])
	{
		[mutablePool setObject:[defaults stringForKey:@"defaultAltroot"] forKey:@"altroot"];
	}
	if (auto spo = [defaults arrayForKey:@"searchPathOverride"])
	{
		[mutablePool setObject:spo forKey:@"searchPathOverride"];
	}
	[_authorization importPools:mutablePool withReply:^(NSError * error)
	 {
		 if (!error)
		 {
			 NSString * title = [NSString stringWithFormat:
				NSLocalizedString(@"All pools imported",
								  @"Pool Import Success all")];
			 [self notifySuccessWithTitle:title text:nil];
		 }
		 [self handlePoolChangeReply:error];
	 }];
}

- (void)handlePoolChangeReply:(NSError*)error
{
	if (error)
	{
		[self notifyErrorFromHelper:error];
	}
	else
	{
		[[self poolWatcher] checkForChanges];
	}
}

@end
