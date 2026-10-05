//
//  ZetaPoolPropertyMenu.h
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
@interface ZetaPoolPropertyMenu : ZetaCommanderBase <NSMenuDelegate>

- (id)initWithPool:(zfs::ZPool)pool;

- (void)menuNeedsUpdate:(NSMenu*)menu;

@end
