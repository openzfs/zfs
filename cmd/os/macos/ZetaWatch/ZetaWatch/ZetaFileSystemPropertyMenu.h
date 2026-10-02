//
//  ZetaFileSystemPropertyMenu.h
//  ZetaWatch
//
//  Created by cbreak on 19.06.22.
//  Copyright © 2019 the-color-black.net. All rights reserved.
//

/* CSTYLED */
#import <Foundation/Foundation.h>
/* CSTYLED */
#import <Cocoa/Cocoa.h>

#import "ZetaCommanderBase.h"

#include "ZFSUtils.hpp"

/* CSTYLED */
@interface ZetaFileSystemPropertyMenu : ZetaCommanderBase <NSMenuDelegate>

- (id)initWithFileSystem:(zfs::ZFileSystem)fs;

- (void)menuNeedsUpdate:(NSMenu*)menu;

@end
