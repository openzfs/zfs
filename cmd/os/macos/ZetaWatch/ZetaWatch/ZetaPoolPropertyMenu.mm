//
//  ZetaPoolPropertyMenu.mm
//  ZetaWatch
//
//  Created by cbreak on 19.06.22.
//  Copyright © 2019 the-color-black.net. All rights reserved.
//

#import "ZetaPoolPropertyMenu.h"

@implementation ZetaPoolPropertyMenu
{
	zfs::ZPool _pool;
}

- (id)initWithPool:(zfs::ZPool)pool
{
	if (self = [super init])
	{
		_pool = std::move(pool);
	}
	return self;
}

- (void)menuNeedsUpdate:(NSMenu*)menu
{
	[menu removeAllItems];
	auto props = _pool.properties();
	for (auto const & p : props)
	{
		addMenuItem(menu, self, NSLocalizedString(@"%-64s \t %s", @"KeyValue"),
						p.name, p.value);
	}
}

@end
