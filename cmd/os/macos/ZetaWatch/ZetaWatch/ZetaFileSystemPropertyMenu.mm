//
//  ZetaFileSystemPropertyMenu.mm
//  ZetaWatch
//
//  Created by cbreak on 19.06.22.
//  Copyright © 2019 the-color-black.net. All rights reserved.
//

#import "ZetaFileSystemPropertyMenu.h"

@implementation ZetaFileSystemPropertyMenu
{
	zfs::ZFileSystem _fs;
}

- (id)initWithFileSystem:(zfs::ZFileSystem)fs
{
	if (self = [super init])
	{
		_fs = std::move(fs);
	}
	return self;
}

- (void)menuNeedsUpdate:(NSMenu*)menu
{
	[menu removeAllItems];
	auto props = _fs.properties();
	for (auto const & p : props)
	{
		if (p.source.size() > 0)
		{
			addMenuItem(menu, self, NSLocalizedString(@"%-64s \t %-32s \t (from %s)", @"KeyValueSource"),
						p.name, p.value, p.source);
		}
		else
		{
			addMenuItem(menu, self, NSLocalizedString(@"%-64s \t %s", @"KeyValue"),
						p.name, p.value);
		}
	}
}

@end
