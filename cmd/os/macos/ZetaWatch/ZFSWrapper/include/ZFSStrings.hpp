//
//  ZFSStrings.hpp
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

#ifndef ZETA_ZFSSTRINGS_HPP
#define ZETA_ZFSSTRINGS_HPP

#include <cstdint>

namespace zfs
{
/*!
 Returns a string with english human-facing description of the zpool status.
 */
char const * describe_zpool_status_t(uint64_t stat);

/*!
 Returns an emoji for the pool status as utf8 C String.
 */
char const * emoji_pool_status_t(uint64_t stat);

/*!
 Returns a string with english human-facing description of the vdev status.
 */
char const * describe_vdev_state_t(uint64_t state, uint64_t aux);

/*!
 Returns an emoji for the vdev status.
 */
char const * emoji_vdev_state_t(uint64_t state, uint64_t aux);

} // namespace zfs

#if defined(__APPLE__) && defined(__OBJC__)

#import <Foundation/NSString.h>

namespace zfs
{
/*!
 Returns a localized string description of the zpool status.
 */
NSString * localized_describe_zpool_status_t(uint64_t stat);

/*!
 Returns an emoji for the pool status as NSString.
 */
NSString * emojistring_pool_status_t(uint64_t stat);

/*!
 Returns a localized string description of the vdev status.
 */
NSString * localized_describe_vdev_state_t(uint64_t stat, uint64_t aux);

/*!
 Returns an emoji for the vdev status.
 */
NSString * emojistring_vdev_state_t(uint64_t state, uint64_t aux);

} // namespace zfs
#endif

#endif
