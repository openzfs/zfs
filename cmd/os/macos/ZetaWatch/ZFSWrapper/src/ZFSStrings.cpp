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

extern "C"
{
#include <libzfs.h>
}

namespace zfs
{
char const * describe_zpool_status_t(uint64_t stat)
{
	switch (zpool_status_t(stat))
	{
	case ZPOOL_STATUS_CORRUPT_CACHE:
		return "corrupt /kernel/drv/zpool.cache";
	case ZPOOL_STATUS_MISSING_DEV_R:
		return "missing device with replicas";
	case ZPOOL_STATUS_MISSING_DEV_NR:
		return "missing device with no replicas";
	case ZPOOL_STATUS_CORRUPT_LABEL_R:
		return "bad device label with replicas";
	case ZPOOL_STATUS_CORRUPT_LABEL_NR:
		return "bad device label with no replicas";
	case ZPOOL_STATUS_BAD_GUID_SUM:
		return "sum of device guids didn't match";
	case ZPOOL_STATUS_CORRUPT_POOL:
		return "pool metadata is corrupted";
	case ZPOOL_STATUS_CORRUPT_DATA:
		return "data errors in user (meta)data";
	case ZPOOL_STATUS_FAILING_DEV:
		return "device experiencing errors";
	case ZPOOL_STATUS_VERSION_NEWER:
		return "newer on-disk version";
	case ZPOOL_STATUS_HOSTID_MISMATCH:
		return "last accessed by another system";
	case ZPOOL_STATUS_HOSTID_ACTIVE:
		return "currently active on another system";
	case ZPOOL_STATUS_HOSTID_REQUIRED:
		return "multihost=on and hostid=0";
	case ZPOOL_STATUS_IO_FAILURE_WAIT:
		return "failed I/O, failmode 'wait'";
	case ZPOOL_STATUS_IO_FAILURE_CONTINUE:
		return "failed I/O, failmode 'continue'";
	case ZPOOL_STATUS_IO_FAILURE_MMP:
		return "failed MMP, failmode not 'panic'";
	case ZPOOL_STATUS_BAD_LOG:
		return "cannot read log chain(s)";
	case ZPOOL_STATUS_ERRATA:
		return "informational errata available";
	case ZPOOL_STATUS_UNSUP_FEAT_READ:
		return "unsupported features for read";
	case ZPOOL_STATUS_UNSUP_FEAT_WRITE:
		return "unsupported features for write";
	case ZPOOL_STATUS_FAULTED_DEV_R:
		return "faulted device with replicas";
	case ZPOOL_STATUS_FAULTED_DEV_NR:
		return "faulted device with no replicas";
	case ZPOOL_STATUS_VERSION_OLDER:
		return "older legacy on-disk version";
	case ZPOOL_STATUS_FEAT_DISABLED:
		return "supported features are disabled";
	case ZPOOL_STATUS_RESILVERING:
		return "device being resilvered";
	case ZPOOL_STATUS_OFFLINE_DEV:
		return "device offline";
	case ZPOOL_STATUS_REMOVED_DEV:
		return "removed device";
	case ZPOOL_STATUS_REBUILDING:
		return "rebuilding";
	case ZPOOL_STATUS_REBUILD_SCRUB:
		return "rebuild scrub";
	case ZPOOL_STATUS_NON_NATIVE_ASHIFT:
		return "non-native ashift";
#if defined(ZFSW_HAS_ZPOOL_STATUS_COMPATIBILITY_ERR)
	case ZPOOL_STATUS_COMPATIBILITY_ERR:
		return "compatibility error";
#endif
#if defined(ZFSW_HAS_ZPOOL_STATUS_INCOMPATIBLE_FEAT)
	case ZPOOL_STATUS_INCOMPATIBLE_FEAT:
		return u8"incompatible feature";
#endif
	case ZPOOL_STATUS_OK:
		return "ok";
	}
	return "unknown status";
}

char const * emoji_pool_status_t(uint64_t stat)
{
	switch (zpool_status_t(stat))
	{
	case ZPOOL_STATUS_CORRUPT_CACHE:
		return u8"❌🗂";
	case ZPOOL_STATUS_MISSING_DEV_R:
		return u8"⚠️⚙️🕳";
	case ZPOOL_STATUS_MISSING_DEV_NR:
		return u8"❌⚙️🕳";
	case ZPOOL_STATUS_CORRUPT_LABEL_R:
		return u8"⚠️🏷";
	case ZPOOL_STATUS_CORRUPT_LABEL_NR:
		return u8"❌🏷";
	case ZPOOL_STATUS_BAD_GUID_SUM:
		return u8"❌🆔";
	case ZPOOL_STATUS_CORRUPT_POOL:
		return u8"⚠️💀🎱";
	case ZPOOL_STATUS_CORRUPT_DATA:
		return u8"⚠️💀💾";
	case ZPOOL_STATUS_FAILING_DEV:
		return u8"⚠️📉⚙️";
	case ZPOOL_STATUS_VERSION_NEWER:
		return u8"⚠️🎂";
	case ZPOOL_STATUS_HOSTID_MISMATCH:
		return u8"⚠️🔌";
	case ZPOOL_STATUS_HOSTID_ACTIVE:
		return u8"⚠️🔌";
	case ZPOOL_STATUS_HOSTID_REQUIRED:
		return u8"⚠️🔌";
	case ZPOOL_STATUS_IO_FAILURE_WAIT:
		return u8"📬⛔️";
	case ZPOOL_STATUS_IO_FAILURE_CONTINUE:
		return u8"📬🛑";
	case ZPOOL_STATUS_IO_FAILURE_MMP:
		return u8"📬📛";
	case ZPOOL_STATUS_BAD_LOG:
		return u8"⚠️📝";
	case ZPOOL_STATUS_ERRATA:
		return u8"✅⚠️";
	case ZPOOL_STATUS_UNSUP_FEAT_READ:
		return u8"⛔️🎛";
	case ZPOOL_STATUS_UNSUP_FEAT_WRITE:
		return u8"⛔️🎛";
	case ZPOOL_STATUS_FAULTED_DEV_R:
		return u8"⚠️⚙️";
	case ZPOOL_STATUS_FAULTED_DEV_NR:
		return u8"❌⚙️";
	case ZPOOL_STATUS_VERSION_OLDER:
		return u8"✅🎂";
	case ZPOOL_STATUS_FEAT_DISABLED:
		return u8"✅🎛";
	case ZPOOL_STATUS_RESILVERING:
		return u8"♻️🥈";
	case ZPOOL_STATUS_OFFLINE_DEV:
		return u8"⚠️⚙️💤";
	case ZPOOL_STATUS_REMOVED_DEV:
		return u8"⚠️⚙️🕳";
	case ZPOOL_STATUS_REBUILDING:
		return u8"♻️🛠";
	case ZPOOL_STATUS_REBUILD_SCRUB:
		return u8"♻️🧽";
	case ZPOOL_STATUS_NON_NATIVE_ASHIFT:
		return u8"✅🐌";
#if defined(ZFSW_HAS_ZPOOL_STATUS_COMPATIBILITY_ERR)
	case ZPOOL_STATUS_COMPATIBILITY_ERR:
		return u8"❌🔌";
#endif
#if defined(ZFSW_HAS_ZPOOL_STATUS_INCOMPATIBLE_FEAT)
	case ZPOOL_STATUS_INCOMPATIBLE_FEAT:
		return u8"🔌🎛";
#endif
	case ZPOOL_STATUS_OK:
		return u8"✅";
	}
	return u8"⁉️";
}

char const * describe_vdev_state_t(uint64_t stat, uint64_t aux)
{
	return zpool_state_to_name(vdev_state_t(stat), vdev_aux_t(aux));
}

char const * emoji_vdev_state_t(uint64_t state, uint64_t aux)
{
	switch (state)
	{
	default:
		break;
	case VDEV_STATE_CLOSED:
	case VDEV_STATE_OFFLINE:
		return u8"⭕️";
	case VDEV_STATE_REMOVED:
		return u8"❌";
	case VDEV_STATE_CANT_OPEN:
		if (aux == VDEV_AUX_CORRUPT_DATA)
			return u8"💀💾";
		else if (aux == VDEV_AUX_BAD_LOG)
			return u8"💀📝";
		else if (aux == VDEV_AUX_SPLIT_POOL)
			return u8"✂️";
		else
			return u8"🔍";
	case VDEV_STATE_FAULTED:
		return u8"💀";
	case VDEV_STATE_DEGRADED:
		return u8"⚠️";
	case VDEV_STATE_HEALTHY:
		return u8"✅";
	}
	return u8"⁉️";
}

} // namespace zfs
