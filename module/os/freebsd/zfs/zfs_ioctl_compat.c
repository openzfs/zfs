/*
 * Copyright (c) 2020 iXsystems, Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHORS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/types.h>
#include <sys/param.h>
#include <sys/conf.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/mutex.h>
#include <sys/errno.h>
#include <sys/cmn_err.h>
#include <sys/zfs_ioctl_compat.h>

enum zfs_ioc_legacy {
	ZFS_IOC_LEGACY_NONE =	-1,
	ZFS_IOC_LEGACY_FIRST =	0,
	ZFS_LEGACY_IOC = ZFS_IOC_LEGACY_FIRST,
	ZFS_IOC_LEGACY_POOL_CREATE = ZFS_IOC_LEGACY_FIRST,
	ZFS_IOC_LEGACY_POOL_DESTROY,
	ZFS_IOC_LEGACY_POOL_IMPORT,
	ZFS_IOC_LEGACY_POOL_EXPORT,
	ZFS_IOC_LEGACY_POOL_CONFIGS,
	ZFS_IOC_LEGACY_POOL_STATS,
	ZFS_IOC_LEGACY_POOL_TRYIMPORT,
	ZFS_IOC_LEGACY_POOL_SCAN,
	ZFS_IOC_LEGACY_POOL_FREEZE,
	ZFS_IOC_LEGACY_POOL_UPGRADE,
	ZFS_IOC_LEGACY_POOL_GET_HISTORY,
	ZFS_IOC_LEGACY_VDEV_ADD,
	ZFS_IOC_LEGACY_VDEV_REMOVE,
	ZFS_IOC_LEGACY_VDEV_SET_STATE,
	ZFS_IOC_LEGACY_VDEV_ATTACH,
	ZFS_IOC_LEGACY_VDEV_DETACH,
	ZFS_IOC_LEGACY_VDEV_SETPATH,
	ZFS_IOC_LEGACY_VDEV_SETFRU,
	ZFS_IOC_LEGACY_OBJSET_STATS,
	ZFS_IOC_LEGACY_OBJSET_ZPLPROPS,
	ZFS_IOC_LEGACY_DATASET_LIST_NEXT,
	ZFS_IOC_LEGACY_SNAPSHOT_LIST_NEXT,
	ZFS_IOC_LEGACY_SET_PROP,
	ZFS_IOC_LEGACY_CREATE,
	ZFS_IOC_LEGACY_DESTROY,
	ZFS_IOC_LEGACY_ROLLBACK,
	ZFS_IOC_LEGACY_RENAME,
	ZFS_IOC_LEGACY_RECV,
	ZFS_IOC_LEGACY_SEND,
	ZFS_IOC_LEGACY_INJECT_FAULT,
	ZFS_IOC_LEGACY_CLEAR_FAULT,
	ZFS_IOC_LEGACY_INJECT_LIST_NEXT,
	ZFS_IOC_LEGACY_ERROR_LOG,
	ZFS_IOC_LEGACY_CLEAR,
	ZFS_IOC_LEGACY_PROMOTE,
	ZFS_IOC_LEGACY_DESTROY_SNAPS,
	ZFS_IOC_LEGACY_SNAPSHOT,
	ZFS_IOC_LEGACY_DSOBJ_TO_DSNAME,
	ZFS_IOC_LEGACY_OBJ_TO_PATH,
	ZFS_IOC_LEGACY_POOL_SET_PROPS,
	ZFS_IOC_LEGACY_POOL_GET_PROPS,
	ZFS_IOC_LEGACY_SET_FSACL,
	ZFS_IOC_LEGACY_GET_FSACL,
	ZFS_IOC_LEGACY_SHARE,
	ZFS_IOC_LEGACY_INHERIT_PROP,
	ZFS_IOC_LEGACY_SMB_ACL,
	ZFS_IOC_LEGACY_USERSPACE_ONE,
	ZFS_IOC_LEGACY_USERSPACE_MANY,
	ZFS_IOC_LEGACY_USERSPACE_UPGRADE,
	ZFS_IOC_LEGACY_HOLD,
	ZFS_IOC_LEGACY_RELEASE,
	ZFS_IOC_LEGACY_GET_HOLDS,
	ZFS_IOC_LEGACY_OBJSET_RECVD_PROPS,
	ZFS_IOC_LEGACY_VDEV_SPLIT,
	ZFS_IOC_LEGACY_NEXT_OBJ,
	ZFS_IOC_LEGACY_DIFF,
	ZFS_IOC_LEGACY_TMP_SNAPSHOT,
	ZFS_IOC_LEGACY_OBJ_TO_STATS,
	ZFS_IOC_LEGACY_JAIL,
	ZFS_IOC_LEGACY_UNJAIL,
	ZFS_IOC_LEGACY_POOL_REGUID,
	ZFS_IOC_LEGACY_SPACE_WRITTEN,
	ZFS_IOC_LEGACY_SPACE_SNAPS,
	ZFS_IOC_LEGACY_SEND_PROGRESS,
	ZFS_IOC_LEGACY_POOL_REOPEN,
	ZFS_IOC_LEGACY_LOG_HISTORY,
	ZFS_IOC_LEGACY_SEND_NEW,
	ZFS_IOC_LEGACY_SEND_SPACE,
	ZFS_IOC_LEGACY_CLONE,
	ZFS_IOC_LEGACY_BOOKMARK,
	ZFS_IOC_LEGACY_GET_BOOKMARKS,
	ZFS_IOC_LEGACY_DESTROY_BOOKMARKS,
	ZFS_IOC_LEGACY_NEXTBOOT,
	ZFS_IOC_LEGACY_CHANNEL_PROGRAM,
	ZFS_IOC_LEGACY_REMAP,
	ZFS_IOC_LEGACY_POOL_CHECKPOINT,
	ZFS_IOC_LEGACY_POOL_DISCARD_CHECKPOINT,
	ZFS_IOC_LEGACY_POOL_INITIALIZE,
	ZFS_IOC_LEGACY_POOL_SYNC,
	ZFS_IOC_LEGACY_LAST
};

unsigned static long zfs_ioctl_legacy_to_ozfs_[] = {
	ZFS_IOC_POOL_CREATE,			/* 0x00 */
	ZFS_IOC_POOL_DESTROY,			/* 0x01 */
	ZFS_IOC_POOL_IMPORT,			/* 0x02 */
	ZFS_IOC_POOL_EXPORT,			/* 0x03 */
	ZFS_IOC_POOL_CONFIGS,			/* 0x04 */
	ZFS_IOC_POOL_STATS,			/* 0x05 */
	ZFS_IOC_POOL_TRYIMPORT,			/* 0x06 */
	ZFS_IOC_POOL_SCAN,			/* 0x07 */
	ZFS_IOC_POOL_FREEZE,			/* 0x08 */
	ZFS_IOC_POOL_UPGRADE,			/* 0x09 */
	ZFS_IOC_POOL_GET_HISTORY,		/* 0x0a */
	ZFS_IOC_VDEV_ADD,			/* 0x0b */
	ZFS_IOC_VDEV_REMOVE,			/* 0x0c */
	ZFS_IOC_VDEV_SET_STATE,			/* 0x0d */
	ZFS_IOC_VDEV_ATTACH,			/* 0x0e */
	ZFS_IOC_VDEV_DETACH,			/* 0x0f */
	ZFS_IOC_VDEV_SETPATH,			/* 0x10 */
	ZFS_IOC_VDEV_SETFRU,			/* 0x11 */
	ZFS_IOC_OBJSET_STATS,			/* 0x12 */
	ZFS_IOC_OBJSET_ZPLPROPS,		/* 0x13 */
	ZFS_IOC_DATASET_LIST_NEXT,		/* 0x14 */
	ZFS_IOC_SNAPSHOT_LIST_NEXT,		/* 0x15 */
	ZFS_IOC_SET_PROP,			/* 0x16 */
	ZFS_IOC_CREATE,				/* 0x17 */
	ZFS_IOC_DESTROY,			/* 0x18 */
	ZFS_IOC_ROLLBACK,			/* 0x19 */
	ZFS_IOC_RENAME,				/* 0x1a */
	ZFS_IOC_RECV,				/* 0x1b */
	ZFS_IOC_SEND,				/* 0x1c */
	ZFS_IOC_INJECT_FAULT,			/* 0x1d */
	ZFS_IOC_CLEAR_FAULT,			/* 0x1e */
	ZFS_IOC_INJECT_LIST_NEXT,		/* 0x1f */
	ZFS_IOC_ERROR_LOG,			/* 0x20 */
	ZFS_IOC_CLEAR,				/* 0x21 */
	ZFS_IOC_PROMOTE,			/* 0x22 */
	/* start of mismatch */

	ZFS_IOC_DESTROY_SNAPS,			/* 0x23:0x3b */
	ZFS_IOC_SNAPSHOT,			/* 0x24:0x23 */
	ZFS_IOC_DSOBJ_TO_DSNAME,		/* 0x25:0x24 */
	ZFS_IOC_OBJ_TO_PATH,			/* 0x26:0x25 */
	ZFS_IOC_POOL_SET_PROPS,			/* 0x27:0x26 */
	ZFS_IOC_POOL_GET_PROPS,			/* 0x28:0x27 */
	ZFS_IOC_SET_FSACL,			/* 0x29:0x28 */
	ZFS_IOC_GET_FSACL,			/* 0x30:0x29 */
	ZFS_IOC_SHARE,				/* 0x2b:0x2a */
	ZFS_IOC_INHERIT_PROP,			/* 0x2c:0x2b */
	ZFS_IOC_SMB_ACL,			/* 0x2d:0x2c */
	ZFS_IOC_USERSPACE_ONE,			/* 0x2e:0x2d */
	ZFS_IOC_USERSPACE_MANY,			/* 0x2f:0x2e */
	ZFS_IOC_USERSPACE_UPGRADE,		/* 0x30:0x2f */
	ZFS_IOC_HOLD,				/* 0x31:0x30 */
	ZFS_IOC_RELEASE,			/* 0x32:0x31 */
	ZFS_IOC_GET_HOLDS,			/* 0x33:0x32 */
	ZFS_IOC_OBJSET_RECVD_PROPS,		/* 0x34:0x33 */
	ZFS_IOC_VDEV_SPLIT,			/* 0x35:0x34 */
	ZFS_IOC_NEXT_OBJ,			/* 0x36:0x35 */
	ZFS_IOC_DIFF,				/* 0x37:0x36 */
	ZFS_IOC_TMP_SNAPSHOT,			/* 0x38:0x37 */
	ZFS_IOC_OBJ_TO_STATS,			/* 0x39:0x38 */
	ZFS_IOC_JAIL,			/* 0x3a:0xc2 */
	ZFS_IOC_UNJAIL,			/* 0x3b:0xc3 */
	ZFS_IOC_POOL_REGUID,			/* 0x3c:0x3c */
	ZFS_IOC_SPACE_WRITTEN,			/* 0x3d:0x39 */
	ZFS_IOC_SPACE_SNAPS,			/* 0x3e:0x3a */
	ZFS_IOC_SEND_PROGRESS,			/* 0x3f:0x3e */
	ZFS_IOC_POOL_REOPEN,			/* 0x40:0x3d */
	ZFS_IOC_LOG_HISTORY,			/* 0x41:0x3f */
	ZFS_IOC_SEND_NEW,			/* 0x42:0x40 */
	ZFS_IOC_SEND_SPACE,			/* 0x43:0x41 */
	ZFS_IOC_CLONE,				/* 0x44:0x42 */
	ZFS_IOC_BOOKMARK,			/* 0x45:0x43 */
	ZFS_IOC_GET_BOOKMARKS,			/* 0x46:0x44 */
	ZFS_IOC_DESTROY_BOOKMARKS,		/* 0x47:0x45 */
	ZFS_IOC_NEXTBOOT,			/* 0x48:0xc1 */
	ZFS_IOC_CHANNEL_PROGRAM,		/* 0x49:0x48 */
	ZFS_IOC_REMAP,				/* 0x4a:0x4c */
	ZFS_IOC_POOL_CHECKPOINT,		/* 0x4b:0x4d */
	ZFS_IOC_POOL_DISCARD_CHECKPOINT,	/* 0x4c:0x4e */
	ZFS_IOC_POOL_INITIALIZE,		/* 0x4d:0x4f */
};

unsigned static long zfs_ioctl_ozfs_to_legacy_common_[] = {
	ZFS_IOC_POOL_CREATE,			/* 0x00 */
	ZFS_IOC_POOL_DESTROY,			/* 0x01 */
	ZFS_IOC_POOL_IMPORT,			/* 0x02 */
	ZFS_IOC_POOL_EXPORT,			/* 0x03 */
	ZFS_IOC_POOL_CONFIGS,			/* 0x04 */
	ZFS_IOC_POOL_STATS,			/* 0x05 */
	ZFS_IOC_POOL_TRYIMPORT,			/* 0x06 */
	ZFS_IOC_POOL_SCAN,			/* 0x07 */
	ZFS_IOC_POOL_FREEZE,			/* 0x08 */
	ZFS_IOC_POOL_UPGRADE,			/* 0x09 */
	ZFS_IOC_POOL_GET_HISTORY,		/* 0x0a */
	ZFS_IOC_VDEV_ADD,			/* 0x0b */
	ZFS_IOC_VDEV_REMOVE,			/* 0x0c */
	ZFS_IOC_VDEV_SET_STATE,			/* 0x0d */
	ZFS_IOC_VDEV_ATTACH,			/* 0x0e */
	ZFS_IOC_VDEV_DETACH,			/* 0x0f */
	ZFS_IOC_VDEV_SETPATH,			/* 0x10 */
	ZFS_IOC_VDEV_SETFRU,			/* 0x11 */
	ZFS_IOC_OBJSET_STATS,			/* 0x12 */
	ZFS_IOC_OBJSET_ZPLPROPS,		/* 0x13 */
	ZFS_IOC_DATASET_LIST_NEXT,		/* 0x14 */
	ZFS_IOC_SNAPSHOT_LIST_NEXT,		/* 0x15 */
	ZFS_IOC_SET_PROP,			/* 0x16 */
	ZFS_IOC_CREATE,				/* 0x17 */
	ZFS_IOC_DESTROY,			/* 0x18 */
	ZFS_IOC_ROLLBACK,			/* 0x19 */
	ZFS_IOC_RENAME,				/* 0x1a */
	ZFS_IOC_RECV,				/* 0x1b */
	ZFS_IOC_SEND,				/* 0x1c */
	ZFS_IOC_INJECT_FAULT,			/* 0x1d */
	ZFS_IOC_CLEAR_FAULT,			/* 0x1e */
	ZFS_IOC_INJECT_LIST_NEXT,		/* 0x1f */
	ZFS_IOC_ERROR_LOG,			/* 0x20 */
	ZFS_IOC_CLEAR,				/* 0x21 */
	ZFS_IOC_PROMOTE,			/* 0x22 */
	/* start of mismatch */
	ZFS_IOC_LEGACY_SNAPSHOT,		/* 0x23 */
	ZFS_IOC_LEGACY_DSOBJ_TO_DSNAME,		/* 0x24 */
	ZFS_IOC_LEGACY_OBJ_TO_PATH,		/* 0x25 */
	ZFS_IOC_LEGACY_POOL_SET_PROPS,		/* 0x26 */
	ZFS_IOC_LEGACY_POOL_GET_PROPS,		/* 0x27 */
	ZFS_IOC_LEGACY_SET_FSACL,		/* 0x28 */
	ZFS_IOC_LEGACY_GET_FSACL,		/* 0x29 */
	ZFS_IOC_LEGACY_SHARE,			/* 0x2a */
	ZFS_IOC_LEGACY_INHERIT_PROP,		/* 0x2b */
	ZFS_IOC_LEGACY_SMB_ACL,			/* 0x2c */
	ZFS_IOC_LEGACY_USERSPACE_ONE,		/* 0x2d */
	ZFS_IOC_LEGACY_USERSPACE_MANY,		/* 0x2e */
	ZFS_IOC_LEGACY_USERSPACE_UPGRADE,	/* 0x2f */
	ZFS_IOC_LEGACY_HOLD,			/* 0x30 */
	ZFS_IOC_LEGACY_RELEASE,			/* 0x31 */
	ZFS_IOC_LEGACY_GET_HOLDS,		/* 0x32 */
	ZFS_IOC_LEGACY_OBJSET_RECVD_PROPS,	/* 0x33 */
	ZFS_IOC_LEGACY_VDEV_SPLIT,		/* 0x34 */
	ZFS_IOC_LEGACY_NEXT_OBJ,		/* 0x35 */
	ZFS_IOC_LEGACY_DIFF,			/* 0x36 */
	ZFS_IOC_LEGACY_TMP_SNAPSHOT,		/* 0x37 */
	ZFS_IOC_LEGACY_OBJ_TO_STATS,		/* 0x38 */
	ZFS_IOC_LEGACY_SPACE_WRITTEN,		/* 0x39 */
	ZFS_IOC_LEGACY_SPACE_SNAPS,		/* 0x3a */
	ZFS_IOC_LEGACY_DESTROY_SNAPS,		/* 0x3b */
	ZFS_IOC_LEGACY_POOL_REGUID,		/* 0x3c */
	ZFS_IOC_LEGACY_POOL_REOPEN,		/* 0x3d */
	ZFS_IOC_LEGACY_SEND_PROGRESS,		/* 0x3e */
	ZFS_IOC_LEGACY_LOG_HISTORY,		/* 0x3f */
	ZFS_IOC_LEGACY_SEND_NEW,		/* 0x40 */
	ZFS_IOC_LEGACY_SEND_SPACE,		/* 0x41 */
	ZFS_IOC_LEGACY_CLONE,			/* 0x42 */
	ZFS_IOC_LEGACY_BOOKMARK,		/* 0x43 */
	ZFS_IOC_LEGACY_GET_BOOKMARKS,		/* 0x44 */
	ZFS_IOC_LEGACY_DESTROY_BOOKMARKS,	/* 0x45 */
	ZFS_IOC_LEGACY_NONE, /* ZFS_IOC_RECV_NEW */
	ZFS_IOC_LEGACY_POOL_SYNC,		/* 0x47 */
	ZFS_IOC_LEGACY_CHANNEL_PROGRAM,		/* 0x48 */
	ZFS_IOC_LEGACY_NONE, /* ZFS_IOC_LOAD_KEY */
	ZFS_IOC_LEGACY_NONE, /* ZFS_IOC_UNLOAD_KEY */
	ZFS_IOC_LEGACY_NONE, /* ZFS_IOC_CHANGE_KEY */
	ZFS_IOC_LEGACY_REMAP,			/* 0x4c */
	ZFS_IOC_LEGACY_POOL_CHECKPOINT,		/* 0x4d */
	ZFS_IOC_LEGACY_POOL_DISCARD_CHECKPOINT,	/* 0x4e */
	ZFS_IOC_LEGACY_POOL_INITIALIZE,		/* 0x4f  */
	ZFS_IOC_LEGACY_NONE, /* ZFS_IOC_POOL_TRIM */
	ZFS_IOC_LEGACY_NONE, /* ZFS_IOC_REDACT */
	ZFS_IOC_LEGACY_NONE, /* ZFS_IOC_GET_BOOKMARK_PROPS */
	ZFS_IOC_LEGACY_NONE, /* ZFS_IOC_WAIT */
	ZFS_IOC_LEGACY_NONE, /* ZFS_IOC_WAIT_FS */
};

unsigned static long zfs_ioctl_ozfs_to_legacy_platform_[] = {
	ZFS_IOC_LEGACY_NONE, /* ZFS_IOC_EVENTS_NEXT */
	ZFS_IOC_LEGACY_NONE, /* ZFS_IOC_EVENTS_CLEAR */
	ZFS_IOC_LEGACY_NONE, /* ZFS_IOC_EVENTS_SEEK */
	ZFS_IOC_LEGACY_NEXTBOOT,
	ZFS_IOC_LEGACY_JAIL,
	ZFS_IOC_LEGACY_UNJAIL,
	ZFS_IOC_LEGACY_NONE, /* ZFS_IOC_SET_BOOTENV */
	ZFS_IOC_LEGACY_NONE, /* ZFS_IOC_GET_BOOTENV */
};

int
zfs_ioctl_legacy_to_ozfs(int request)
{
	if (request >= sizeof (zfs_ioctl_legacy_to_ozfs_)/sizeof (long))
		return (-1);
	return (zfs_ioctl_legacy_to_ozfs_[request]);
}

int
zfs_ioctl_ozfs_to_legacy(int request)
{
	if (request > ZFS_IOC_LAST)
		return (-1);

	if (request > ZFS_IOC_PLATFORM) {
		request -= ZFS_IOC_PLATFORM + 1;
		return (zfs_ioctl_ozfs_to_legacy_platform_[request]);
	}
	if (request >= sizeof (zfs_ioctl_ozfs_to_legacy_common_)/sizeof (long))
		return (-1);
	return (zfs_ioctl_ozfs_to_legacy_common_[request]);
}

void
zfs_cmd_legacy_to_ozfs(zfs_cmd_legacy_t *src, zfs_cmd_t *dst)
{
	memcpy(dst, src, offsetof(zfs_cmd_t, zc_objset_stats));
	*&dst->zc_objset_stats = *&src->zc_objset_stats;
	memcpy(&dst->zc_begin_record, &src->zc_begin_record,
	    offsetof(zfs_cmd_t, zc_sendobj) -
	    offsetof(zfs_cmd_t, zc_begin_record));
	memcpy(&dst->zc_sendobj, &src->zc_sendobj,
	    sizeof (zfs_cmd_t) - 8 - offsetof(zfs_cmd_t, zc_sendobj));
	dst->zc_zoneid = src->zc_jailid;
}

void
zfs_cmd_ozfs_to_legacy(zfs_cmd_t *src, zfs_cmd_legacy_t *dst)
{
	memcpy(dst, src, offsetof(zfs_cmd_t, zc_objset_stats));
	*&dst->zc_objset_stats = *&src->zc_objset_stats;
	*&dst->zc_begin_record.drr_u.drr_begin = *&src->zc_begin_record;
	dst->zc_begin_record.drr_payloadlen = 0;
	dst->zc_begin_record.drr_type = 0;

	memcpy(&dst->zc_inject_record, &src->zc_inject_record,
	    offsetof(zfs_cmd_t, zc_sendobj) -
	    offsetof(zfs_cmd_t, zc_inject_record));
	dst->zc_resumable = B_FALSE;
	memcpy(&dst->zc_sendobj, &src->zc_sendobj,
	    sizeof (zfs_cmd_t) - 8 - offsetof(zfs_cmd_t, zc_sendobj));
	dst->zc_jailid = src->zc_zoneid;
}
