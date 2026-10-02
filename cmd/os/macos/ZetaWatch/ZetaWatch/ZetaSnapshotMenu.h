//
//  ZetaSnapshotMenu.h
//  ZetaWatch
//
//  Created by cbreak on 19.10.05.
//  Copyright © 2019 the-color-black.net. All rights reserved.
//

/* CSTYLED */
#import <Cocoa/Cocoa.h>

#import "ZetaCommanderBase.h"

#include "ZFSUtils.hpp"

NS_ASSUME_NONNULL_BEGIN

@class ZetaMainMenu;

/* CSTYLED */
@interface ZetaSnapshotMenu : ZetaCommanderBase <NSMenuDelegate>

- (id)initWithFileSystem:(zfs::ZFileSystem)fs delegate:(ZetaMainMenu*)main;

- (void)menuNeedsUpdate:(NSMenu*)menu;

@end

NS_ASSUME_NONNULL_END
