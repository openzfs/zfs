//
//  ZetaSnapshotMenu.m
//  ZetaWatch
//
//  Created by cbreak on 19.10.05.
//  Copyright © 2019 the-color-black.net. All rights reserved.
//

#import "ZetaSnapshotMenu.h"

#import "ZetaMainMenu.h"

@implementation ZetaSnapshotMenu
{
	zfs::ZFileSystem _fs;
	ZetaMainMenu __weak * _delegate;
}

- (id)initWithFileSystem:(zfs::ZFileSystem)fs delegate:(ZetaMainMenu*)delegate
{
	if (self = [super init])
	{
		_fs = std::move(fs);
		_delegate = delegate;
	}
	return self;
}

NSMenuItem * createSnapMenu(zfs::ZFileSystem const & snap, ZetaMainMenu * delegate)
{
	NSMenu * sMenu = [[NSMenu alloc] init];
	[sMenu setAutoenablesItems:NO];
	NSString * sName = [NSString stringWithUTF8String:snap.name()];
	auto addSnapCommand = [&](NSString * title, SEL selector)
	{
		auto item = [sMenu addItemWithTitle:title
									 action:selector keyEquivalent:@""];
		item.representedObject = sName;
		item.target = delegate;
	};
	addSnapCommand(NSLocalizedString(@"Clone", @"Clone"), @selector(cloneSnapshot:));
	addSnapCommand(NSLocalizedString(@"Rollback", @"Rollback"), @selector(rollbackFilesystem:));
	addSnapCommand(NSLocalizedString(@"Rollback (Force)", @"Rollback (Force)"), @selector(rollbackFilesystemForce:));
	[sMenu addItem:[NSMenuItem separatorItem]];
	if (!snap.mounted())
	{
		addSnapCommand(NSLocalizedString(@"Mount", @"Mount"), @selector(mountFilesystem:));
	}
	else
	{
		addSnapCommand(NSLocalizedString(@"Unmount", @"Unmount"), @selector(unmountFilesystem:));
		addSnapCommand(NSLocalizedString(@"Unmount (Force)", @"Unmount (Force)"), @selector(unmountFilesystemForce:));
	}
	[sMenu addItem:[NSMenuItem separatorItem]];
	if (snap.cloneCount() == 0)
	{
		addSnapCommand(NSLocalizedString(@"Destroy", @"Destroy"), @selector(destroy:));
	}
	else
	{
		addSnapCommand(NSLocalizedString(@"Destroy Recursive", @"Destroy Recursive"), @selector(destroyRecursive:));
	}
	auto item = [[NSMenuItem alloc] initWithTitle:sName action:nullptr keyEquivalent:@""];
	item.representedObject = sName;
	item.submenu = sMenu;
	return item;
}

- (void)menuNeedsUpdate:(NSMenu*)menu
{
	[menu removeAllItems];
	auto snap = _fs.snapshots();
	if (!snap.empty())
	{
		for (size_t i = snap.size(); i > 0; --i)
		{
			NSMenuItem * item = createSnapMenu(snap[i-1], _delegate);
			[menu addItem:item];
		}
	}
	else
	{
		[menu addItemWithTitle:NSLocalizedString(@"No snapshots found", @"No Snapshots")
						action:NULL keyEquivalent:@""];
	}
}

@end
