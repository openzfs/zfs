//
//  ZetaBookmarkMenu.m
//  ZetaWatch
//
//  Created by cbreak on 19.12.14.
//  Copyright © 2019 the-color-black.net. All rights reserved.
//

#import "ZetaBookmarkMenu.h"

#import "ZetaMainMenu.h"

@implementation ZetaBookmarkMenu
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

NSMenuItem * createBookmarkMenu(zfs::ZFileSystem const & bookmark, ZetaMainMenu * delegate)
{
	NSMenu * bMenu = [[NSMenu alloc] init];
	[bMenu setAutoenablesItems:NO];
	NSString * bName = [NSString stringWithUTF8String:bookmark.name()];
	auto addBookmarkCommand = [&](NSString * title, SEL selector)
	{
		auto item = [bMenu addItemWithTitle:title
									 action:selector keyEquivalent:@""];
		item.representedObject = bName;
		item.target = delegate;
	};
	addBookmarkCommand(NSLocalizedString(@"Destroy", @"Destroy"), @selector(destroy:));
	auto item = [[NSMenuItem alloc] initWithTitle:bName action:nullptr keyEquivalent:@""];
	item.representedObject = bName;
	item.submenu = bMenu;
	return item;
}

- (void)menuNeedsUpdate:(NSMenu*)menu
{
	[menu removeAllItems];
	auto bookmarks = _fs.bookmarks();
	if (!bookmarks.empty())
	{
		for (size_t i = bookmarks.size(); i > 0; --i)
		{
			NSMenuItem * item = createBookmarkMenu(bookmarks[i-1], _delegate);
			[menu addItem:item];
		}
	}
	else
	{
		[menu addItemWithTitle:NSLocalizedString(@"No bookmarks found", @"No Bookmarks")
						action:NULL keyEquivalent:@""];
	}
}

@end
