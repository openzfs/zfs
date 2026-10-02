//
//  ZFSStrings.m
//  ZetaWatch
//
//  Created by Gerhard Röthlin on 2015.12.25.
//  Copyright © 2015 the-color-black.net. All rights reserved.
//
//  Redistribution and use in source and binary forms, with or without
//  modification, are permitted provided that the conditions of the "3-Clause
//  BSD" license described in the BSD.LICENSE file are met. Additional licensing
//  options are described in the README file.
//

#include "ZFSStrings.hpp"

#import "Foundation/NSBundle.h"

namespace zfs
{
NSString * emojistring_pool_status_t(uint64_t stat)
{
	return [NSString stringWithUTF8String:emoji_pool_status_t(stat)];
}

NSString * localized_describe_zpool_status_t(uint64_t stat)
{
	return NSLocalizedString(
	    [NSString stringWithUTF8String:describe_zpool_status_t(stat)],
	    @"zpool_status_t");
}

NSString * localized_describe_vdev_state_t(uint64_t stat, uint64_t aux)
{
	return NSLocalizedString(
	    [NSString stringWithUTF8String:describe_vdev_state_t(stat, aux)],
	    @"vdev_state_t");
}

NSString * emojistring_vdev_state_t(uint64_t state, uint64_t aux)
{
	return [NSString stringWithUTF8String:emoji_vdev_state_t(state, aux)];
}
}
