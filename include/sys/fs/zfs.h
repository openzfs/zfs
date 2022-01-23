/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */

/*
 * Copyright (c) 2005, 2010, Oracle and/or its affiliates. All rights reserved.
 * Copyright (c) 2011, 2020 by Delphix. All rights reserved.
 * Copyright 2011 Nexenta Systems, Inc.  All rights reserved.
 * Copyright (c) 2013, 2017 Joyent, Inc. All rights reserved.
 * Copyright (c) 2014 Integros [integros.com]
 * Copyright (c) 2017, Intel Corporation.
 * Copyright (c) 2019 Datto Inc.
 * Portions Copyright 2010 Robert Milkowski
 * Copyright (c) 2021, Colm Buckley <colm@tuatha.org>
 */

#ifndef	_SYS_FS_ZFS_H
#define	_SYS_FS_ZFS_H extern __attribute__((visibility("default")))

#include <sys/time.h>
#include <sys/zio_priority.h>

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * Types and constants shared between userland and the kernel.
 */

/*
 * Each dataset can be one of the following types.  These constants can be
 * combined into masks that can be passed to various functions.
 */
typedef enum {
	ZFS_TYPE_FILESYSTEM	= (1 << 0),
	ZFS_TYPE_SNAPSHOT	= (1 << 1),
	ZFS_TYPE_VOLUME		= (1 << 2),
	ZFS_TYPE_POOL		= (1 << 3),
	ZFS_TYPE_BOOKMARK	= (1 << 4),
	ZFS_TYPE_VDEV		= (1 << 5),
} zfs_type_t;

/*
 * NB: lzc_dataset_type should be updated whenever a new objset type is added,
 * if it represents a real type of a dataset that can be created from userland.
 */
typedef enum dmu_objset_type {
	DMU_OST_NONE,
	DMU_OST_META,
	DMU_OST_ZFS,
	DMU_OST_ZVOL,
	DMU_OST_OTHER,			/* For testing only! */
	DMU_OST_ANY,			/* Be careful! */
	DMU_OST_NUMTYPES
} dmu_objset_type_t;

#define	ZFS_TYPE_DATASET	\
	(ZFS_TYPE_FILESYSTEM | ZFS_TYPE_VOLUME | ZFS_TYPE_SNAPSHOT)

/*
 * All of these include the terminating NUL byte.
 */
#define	ZAP_MAXNAMELEN 256
#define	ZAP_MAXVALUELEN (1024 * 8)
#define	ZAP_OLDMAXVALUELEN 1024
#define	ZFS_MAX_DATASET_NAME_LEN 256

/*
 * Dataset properties are identified by these constants and must be added to
 * the end of this list to ensure that external consumers are not affected
 * by the change. If you make any changes to this list, be sure to update
 * the property table in module/zcommon/zfs_prop.c.
 */
typedef enum {
	ZPROP_CONT = -2,
	ZPROP_INVAL = -1,
	ZFS_PROP_TYPE = 0,
	ZFS_PROP_CREATION,
	ZFS_PROP_USED,
	ZFS_PROP_AVAILABLE,
	ZFS_PROP_REFERENCED,
	ZFS_PROP_COMPRESSRATIO,
	ZFS_PROP_MOUNTED,
	ZFS_PROP_ORIGIN,
	ZFS_PROP_QUOTA,
	ZFS_PROP_RESERVATION,
	ZFS_PROP_VOLSIZE,
	ZFS_PROP_VOLBLOCKSIZE,
	ZFS_PROP_RECORDSIZE,
	ZFS_PROP_MOUNTPOINT,
	ZFS_PROP_SHARENFS,
	ZFS_PROP_CHECKSUM,
	ZFS_PROP_COMPRESSION,
	ZFS_PROP_ATIME,
	ZFS_PROP_DEVICES,
	ZFS_PROP_EXEC,
	ZFS_PROP_SETUID,
	ZFS_PROP_READONLY,
	ZFS_PROP_ZONED,
	ZFS_PROP_SNAPDIR,
	ZFS_PROP_ACLMODE,
	ZFS_PROP_ACLINHERIT,
	ZFS_PROP_CREATETXG,
	ZFS_PROP_NAME,			/* not exposed to the user */
	ZFS_PROP_CANMOUNT,
	ZFS_PROP_ISCSIOPTIONS,		/* not exposed to the user */
	ZFS_PROP_XATTR,
	ZFS_PROP_NUMCLONES,		/* not exposed to the user */
	ZFS_PROP_COPIES,
	ZFS_PROP_VERSION,
	ZFS_PROP_UTF8ONLY,
	ZFS_PROP_NORMALIZE,
	ZFS_PROP_CASE,
	ZFS_PROP_VSCAN,
	ZFS_PROP_NBMAND,
	ZFS_PROP_SHARESMB,
	ZFS_PROP_REFQUOTA,
	ZFS_PROP_REFRESERVATION,
	ZFS_PROP_GUID,
	ZFS_PROP_PRIMARYCACHE,
	ZFS_PROP_SECONDARYCACHE,
	ZFS_PROP_USEDSNAP,
	ZFS_PROP_USEDDS,
	ZFS_PROP_USEDCHILD,
	ZFS_PROP_USEDREFRESERV,
	ZFS_PROP_USERACCOUNTING,	/* not exposed to the user */
	ZFS_PROP_STMF_SHAREINFO,	/* not exposed to the user */
	ZFS_PROP_DEFER_DESTROY,
	ZFS_PROP_USERREFS,
	ZFS_PROP_LOGBIAS,
	ZFS_PROP_UNIQUE,		/* not exposed to the user */
	ZFS_PROP_OBJSETID,
	ZFS_PROP_DEDUP,
	ZFS_PROP_MLSLABEL,
	ZFS_PROP_SYNC,
	ZFS_PROP_DNODESIZE,
	ZFS_PROP_REFRATIO,
	ZFS_PROP_WRITTEN,
	ZFS_PROP_CLONES,
	ZFS_PROP_LOGICALUSED,
	ZFS_PROP_LOGICALREFERENCED,
	ZFS_PROP_INCONSISTENT,		/* not exposed to the user */
	ZFS_PROP_VOLMODE,
	ZFS_PROP_FILESYSTEM_LIMIT,
	ZFS_PROP_SNAPSHOT_LIMIT,
	ZFS_PROP_FILESYSTEM_COUNT,
	ZFS_PROP_SNAPSHOT_COUNT,
	ZFS_PROP_SNAPDEV,
	ZFS_PROP_ACLTYPE,
	ZFS_PROP_SELINUX_CONTEXT,
	ZFS_PROP_SELINUX_FSCONTEXT,
	ZFS_PROP_SELINUX_DEFCONTEXT,
	ZFS_PROP_SELINUX_ROOTCONTEXT,
	ZFS_PROP_RELATIME,
	ZFS_PROP_REDUNDANT_METADATA,
	ZFS_PROP_OVERLAY,
	ZFS_PROP_PREV_SNAP,
	ZFS_PROP_RECEIVE_RESUME_TOKEN,
	ZFS_PROP_ENCRYPTION,
	ZFS_PROP_KEYLOCATION,
	ZFS_PROP_KEYFORMAT,
	ZFS_PROP_PBKDF2_SALT,
	ZFS_PROP_PBKDF2_ITERS,
	ZFS_PROP_ENCRYPTION_ROOT,
	ZFS_PROP_KEY_GUID,
	ZFS_PROP_KEYSTATUS,
	ZFS_PROP_REMAPTXG,		/* obsolete - no longer used */
	ZFS_PROP_SPECIAL_SMALL_BLOCKS,
	ZFS_PROP_IVSET_GUID,		/* not exposed to the user */
	ZFS_PROP_REDACTED,
	ZFS_PROP_REDACT_SNAPS,
	ZFS_NUM_PROPS
} zfs_prop_t;

typedef enum {
	ZFS_PROP_USERUSED,
	ZFS_PROP_USERQUOTA,
	ZFS_PROP_GROUPUSED,
	ZFS_PROP_GROUPQUOTA,
	ZFS_PROP_USEROBJUSED,
	ZFS_PROP_USEROBJQUOTA,
	ZFS_PROP_GROUPOBJUSED,
	ZFS_PROP_GROUPOBJQUOTA,
	ZFS_PROP_PROJECTUSED,
	ZFS_PROP_PROJECTQUOTA,
	ZFS_PROP_PROJECTOBJUSED,
	ZFS_PROP_PROJECTOBJQUOTA,
	ZFS_NUM_USERQUOTA_PROPS
} zfs_userquota_prop_t;

_SYS_FS_ZFS_H const char *const zfs_userquota_prop_prefixes[
    ZFS_NUM_USERQUOTA_PROPS];

/*
 * Pool properties are identified by these constants and must be added to the
 * end of this list to ensure that external consumers are not affected
 * by the change.  Properties must be registered in zfs_prop_init().
 */
typedef enum {
	ZPOOL_PROP_INVAL = -1,
	ZPOOL_PROP_NAME,
	ZPOOL_PROP_SIZE,
	ZPOOL_PROP_CAPACITY,
	ZPOOL_PROP_ALTROOT,
	ZPOOL_PROP_HEALTH,
	ZPOOL_PROP_GUID,
	ZPOOL_PROP_VERSION,
	ZPOOL_PROP_BOOTFS,
	ZPOOL_PROP_DELEGATION,
	ZPOOL_PROP_AUTOREPLACE,
	ZPOOL_PROP_CACHEFILE,
	ZPOOL_PROP_FAILUREMODE,
	ZPOOL_PROP_LISTSNAPS,
	ZPOOL_PROP_AUTOEXPAND,
	ZPOOL_PROP_DEDUPDITTO,
	ZPOOL_PROP_DEDUPRATIO,
	ZPOOL_PROP_FREE,
	ZPOOL_PROP_ALLOCATED,
	ZPOOL_PROP_READONLY,
	ZPOOL_PROP_ASHIFT,
	ZPOOL_PROP_COMMENT,
	ZPOOL_PROP_EXPANDSZ,
	ZPOOL_PROP_FREEING,
	ZPOOL_PROP_FRAGMENTATION,
	ZPOOL_PROP_LEAKED,
	ZPOOL_PROP_MAXBLOCKSIZE,
	ZPOOL_PROP_TNAME,
	ZPOOL_PROP_MAXDNODESIZE,
	ZPOOL_PROP_MULTIHOST,
	ZPOOL_PROP_CHECKPOINT,
	ZPOOL_PROP_LOAD_GUID,
	ZPOOL_PROP_AUTOTRIM,
	ZPOOL_PROP_COMPATIBILITY,
	ZPOOL_NUM_PROPS
} zpool_prop_t;

/* Small enough to not hog a whole line of printout in zpool(8). */
#define	ZPROP_MAX_COMMENT	32
#define	ZPROP_BOOLEAN_NA	2

#define	ZPROP_VALUE		"value"
#define	ZPROP_SOURCE		"source"

typedef enum {
	ZPROP_SRC_NONE = 0x1,
	ZPROP_SRC_DEFAULT = 0x2,
	ZPROP_SRC_TEMPORARY = 0x4,
	ZPROP_SRC_LOCAL = 0x8,
	ZPROP_SRC_INHERITED = 0x10,
	ZPROP_SRC_RECEIVED = 0x20
} zprop_source_t;

#define	ZPROP_SRC_ALL	0x3f

#define	ZPROP_SOURCE_VAL_RECVD	"$recvd"
#define	ZPROP_N_MORE_ERRORS	"N_MORE_ERRORS"

/*
 * Dataset flag implemented as a special entry in the props zap object
 * indicating that the dataset has received properties on or after
 * SPA_VERSION_RECVD_PROPS. The first such receive blows away local properties
 * just as it did in earlier versions, and thereafter, local properties are
 * preserved.
 */
#define	ZPROP_HAS_RECVD		"$hasrecvd"

typedef enum {
	ZPROP_ERR_NOCLEAR = 0x1, /* failure to clear existing props */
	ZPROP_ERR_NORESTORE = 0x2 /* failure to restore props on error */
} zprop_errflags_t;

typedef int (*zprop_func)(int, void *);

/*
 * Properties to be set on the root file system of a new pool
 * are stuffed into their own nvlist, which is then included in
 * the properties nvlist with the pool properties.
 */
#define	ZPOOL_ROOTFS_PROPS	"root-props-nvl"

/*
 * Length of 'written@' and 'written#'
 */
#define	ZFS_WRITTEN_PROP_PREFIX_LEN	8

/*
 * VDEV properties are identified by these constants and must be added to the
 * end of this list to ensure that external consumers are not affected
 * by the change. If you make any changes to this list, be sure to update
 * the property table in usr/src/common/zfs/zpool_prop.c.
 */
typedef enum {
	VDEV_PROP_INVAL = -1,
#define	VDEV_PROP_USER	VDEV_PROP_INVAL
	VDEV_PROP_NAME,
	VDEV_PROP_CAPACITY,
	VDEV_PROP_STATE,
	VDEV_PROP_GUID,
	VDEV_PROP_ASIZE,
	VDEV_PROP_PSIZE,
	VDEV_PROP_ASHIFT,
	VDEV_PROP_SIZE,
	VDEV_PROP_FREE,
	VDEV_PROP_ALLOCATED,
	VDEV_PROP_COMMENT,
	VDEV_PROP_EXPANDSZ,
	VDEV_PROP_FRAGMENTATION,
	VDEV_PROP_BOOTSIZE,
	VDEV_PROP_PARITY,
	VDEV_PROP_PATH,
	VDEV_PROP_DEVID,
	VDEV_PROP_PHYS_PATH,
	VDEV_PROP_ENC_PATH,
	VDEV_PROP_FRU,
	VDEV_PROP_PARENT,
	VDEV_PROP_CHILDREN,
	VDEV_PROP_NUMCHILDREN,
	VDEV_PROP_READ_ERRORS,
	VDEV_PROP_WRITE_ERRORS,
	VDEV_PROP_CHECKSUM_ERRORS,
	VDEV_PROP_INITIALIZE_ERRORS,
	VDEV_PROP_OPS_NULL,
	VDEV_PROP_OPS_READ,
	VDEV_PROP_OPS_WRITE,
	VDEV_PROP_OPS_FREE,
	VDEV_PROP_OPS_CLAIM,
	VDEV_PROP_OPS_TRIM,
	VDEV_PROP_BYTES_NULL,
	VDEV_PROP_BYTES_READ,
	VDEV_PROP_BYTES_WRITE,
	VDEV_PROP_BYTES_FREE,
	VDEV_PROP_BYTES_CLAIM,
	VDEV_PROP_BYTES_TRIM,
	VDEV_PROP_REMOVING,
	VDEV_PROP_ALLOCATING,
	VDEV_NUM_PROPS
} vdev_prop_t;

/*
 * Dataset property functions shared between libzfs and kernel.
 */
_SYS_FS_ZFS_H const char *zfs_prop_default_string(zfs_prop_t);
_SYS_FS_ZFS_H uint64_t zfs_prop_default_numeric(zfs_prop_t);
_SYS_FS_ZFS_H boolean_t zfs_prop_readonly(zfs_prop_t);
_SYS_FS_ZFS_H boolean_t zfs_prop_visible(zfs_prop_t prop);
_SYS_FS_ZFS_H boolean_t zfs_prop_inheritable(zfs_prop_t);
_SYS_FS_ZFS_H boolean_t zfs_prop_setonce(zfs_prop_t);
_SYS_FS_ZFS_H boolean_t zfs_prop_encryption_key_param(zfs_prop_t);
_SYS_FS_ZFS_H boolean_t zfs_prop_valid_keylocation(const char *, boolean_t);
_SYS_FS_ZFS_H const char *zfs_prop_to_name(zfs_prop_t);
_SYS_FS_ZFS_H zfs_prop_t zfs_name_to_prop(const char *);
_SYS_FS_ZFS_H boolean_t zfs_prop_user(const char *);
_SYS_FS_ZFS_H boolean_t zfs_prop_userquota(const char *);
_SYS_FS_ZFS_H boolean_t zfs_prop_written(const char *);
_SYS_FS_ZFS_H int zfs_prop_index_to_string(zfs_prop_t, uint64_t, const char **);
_SYS_FS_ZFS_H int zfs_prop_string_to_index(zfs_prop_t, const char *,
    uint64_t *);
_SYS_FS_ZFS_H uint64_t zfs_prop_random_value(zfs_prop_t, uint64_t seed);
_SYS_FS_ZFS_H boolean_t zfs_prop_valid_for_type(int, zfs_type_t, boolean_t);

/*
 * Pool property functions shared between libzfs and kernel.
 */
_SYS_FS_ZFS_H zpool_prop_t zpool_name_to_prop(const char *);
_SYS_FS_ZFS_H const char *zpool_prop_to_name(zpool_prop_t);
_SYS_FS_ZFS_H const char *zpool_prop_default_string(zpool_prop_t);
_SYS_FS_ZFS_H uint64_t zpool_prop_default_numeric(zpool_prop_t);
_SYS_FS_ZFS_H boolean_t zpool_prop_readonly(zpool_prop_t);
_SYS_FS_ZFS_H boolean_t zpool_prop_setonce(zpool_prop_t);
_SYS_FS_ZFS_H boolean_t zpool_prop_feature(const char *);
_SYS_FS_ZFS_H boolean_t zpool_prop_unsupported(const char *);
_SYS_FS_ZFS_H int zpool_prop_index_to_string(zpool_prop_t, uint64_t,
    const char **);
_SYS_FS_ZFS_H int zpool_prop_string_to_index(zpool_prop_t, const char *,
    uint64_t *);
_SYS_FS_ZFS_H uint64_t zpool_prop_random_value(zpool_prop_t, uint64_t seed);

/*
 * VDEV property functions shared between libzfs and kernel.
 */
_SYS_FS_ZFS_H vdev_prop_t vdev_name_to_prop(const char *);
_SYS_FS_ZFS_H boolean_t vdev_prop_user(const char *name);
_SYS_FS_ZFS_H const char *vdev_prop_to_name(vdev_prop_t);
_SYS_FS_ZFS_H const char *vdev_prop_default_string(vdev_prop_t);
_SYS_FS_ZFS_H uint64_t vdev_prop_default_numeric(vdev_prop_t);
_SYS_FS_ZFS_H boolean_t vdev_prop_readonly(vdev_prop_t prop);
_SYS_FS_ZFS_H int vdev_prop_index_to_string(vdev_prop_t, uint64_t,
    const char **);
_SYS_FS_ZFS_H int vdev_prop_string_to_index(vdev_prop_t, const char *,
    uint64_t *);
_SYS_FS_ZFS_H boolean_t zpool_prop_vdev(const char *name);
_SYS_FS_ZFS_H uint64_t vdev_prop_random_value(vdev_prop_t prop, uint64_t seed);

/*
 * Definitions for the Delegation.
 */
typedef enum {
	ZFS_DELEG_WHO_UNKNOWN = 0,
	ZFS_DELEG_USER = 'u',
	ZFS_DELEG_USER_SETS = 'U',
	ZFS_DELEG_GROUP = 'g',
	ZFS_DELEG_GROUP_SETS = 'G',
	ZFS_DELEG_EVERYONE = 'e',
	ZFS_DELEG_EVERYONE_SETS = 'E',
	ZFS_DELEG_CREATE = 'c',
	ZFS_DELEG_CREATE_SETS = 'C',
	ZFS_DELEG_NAMED_SET = 's',
	ZFS_DELEG_NAMED_SET_SETS = 'S'
} zfs_deleg_who_type_t;

typedef enum {
	ZFS_DELEG_NONE = 0,
	ZFS_DELEG_PERM_LOCAL = 1,
	ZFS_DELEG_PERM_DESCENDENT = 2,
	ZFS_DELEG_PERM_LOCALDESCENDENT = 3,
	ZFS_DELEG_PERM_CREATE = 4
} zfs_deleg_inherit_t;

#define	ZFS_DELEG_PERM_UID	"uid"
#define	ZFS_DELEG_PERM_GID	"gid"
#define	ZFS_DELEG_PERM_GROUPS	"groups"

#define	ZFS_MLSLABEL_DEFAULT	"none"

#define	ZFS_SMB_ACL_SRC		"src"
#define	ZFS_SMB_ACL_TARGET	"target"

typedef enum {
	ZFS_CANMOUNT_OFF = 0,
	ZFS_CANMOUNT_ON = 1,
	ZFS_CANMOUNT_NOAUTO = 2
} zfs_canmount_type_t;

typedef enum {
	ZFS_LOGBIAS_LATENCY = 0,
	ZFS_LOGBIAS_THROUGHPUT = 1
} zfs_logbias_op_t;

typedef enum zfs_share_op {
	ZFS_SHARE_NFS = 0,
	ZFS_UNSHARE_NFS = 1,
	ZFS_SHARE_SMB = 2,
	ZFS_UNSHARE_SMB = 3
} zfs_share_op_t;

typedef enum zfs_smb_acl_op {
	ZFS_SMB_ACL_ADD,
	ZFS_SMB_ACL_REMOVE,
	ZFS_SMB_ACL_RENAME,
	ZFS_SMB_ACL_PURGE
} zfs_smb_acl_op_t;

typedef enum zfs_cache_type {
	ZFS_CACHE_NONE = 0,
	ZFS_CACHE_METADATA = 1,
	ZFS_CACHE_ALL = 2
} zfs_cache_type_t;

typedef enum {
	ZFS_SYNC_STANDARD = 0,
	ZFS_SYNC_ALWAYS = 1,
	ZFS_SYNC_DISABLED = 2
} zfs_sync_type_t;

typedef enum {
	ZFS_XATTR_OFF = 0,
	ZFS_XATTR_DIR = 1,
	ZFS_XATTR_SA = 2
} zfs_xattr_type_t;

typedef enum {
	ZFS_DNSIZE_LEGACY = 0,
	ZFS_DNSIZE_AUTO = 1,
	ZFS_DNSIZE_1K = 1024,
	ZFS_DNSIZE_2K = 2048,
	ZFS_DNSIZE_4K = 4096,
	ZFS_DNSIZE_8K = 8192,
	ZFS_DNSIZE_16K = 16384
} zfs_dnsize_type_t;

typedef enum {
	ZFS_REDUNDANT_METADATA_ALL,
	ZFS_REDUNDANT_METADATA_MOST
} zfs_redundant_metadata_type_t;

typedef enum {
	ZFS_VOLMODE_DEFAULT = 0,
	ZFS_VOLMODE_GEOM = 1,
	ZFS_VOLMODE_DEV = 2,
	ZFS_VOLMODE_NONE = 3
} zfs_volmode_t;

typedef enum zfs_keystatus {
	ZFS_KEYSTATUS_NONE = 0,
	ZFS_KEYSTATUS_UNAVAILABLE,
	ZFS_KEYSTATUS_AVAILABLE,
} zfs_keystatus_t;

typedef enum zfs_keyformat {
	ZFS_KEYFORMAT_NONE = 0,
	ZFS_KEYFORMAT_RAW,
	ZFS_KEYFORMAT_HEX,
	ZFS_KEYFORMAT_PASSPHRASE,
	ZFS_KEYFORMAT_FORMATS
} zfs_keyformat_t;

typedef enum zfs_key_location {
	ZFS_KEYLOCATION_NONE = 0,
	ZFS_KEYLOCATION_PROMPT,
	ZFS_KEYLOCATION_URI,
	ZFS_KEYLOCATION_LOCATIONS
} zfs_keylocation_t;

#define	DEFAULT_PBKDF2_ITERATIONS 350000
#define	MIN_PBKDF2_ITERATIONS 100000

/*
 * On-disk version number.
 */
#define	SPA_VERSION_1			1ULL
#define	SPA_VERSION_2			2ULL
#define	SPA_VERSION_3			3ULL
#define	SPA_VERSION_4			4ULL
#define	SPA_VERSION_5			5ULL
#define	SPA_VERSION_6			6ULL
#define	SPA_VERSION_7			7ULL
#define	SPA_VERSION_8			8ULL
#define	SPA_VERSION_9			9ULL
#define	SPA_VERSION_10			10ULL
#define	SPA_VERSION_11			11ULL
#define	SPA_VERSION_12			12ULL
#define	SPA_VERSION_13			13ULL
#define	SPA_VERSION_14			14ULL
#define	SPA_VERSION_15			15ULL
#define	SPA_VERSION_16			16ULL
#define	SPA_VERSION_17			17ULL
#define	SPA_VERSION_18			18ULL
#define	SPA_VERSION_19			19ULL
#define	SPA_VERSION_20			20ULL
#define	SPA_VERSION_21			21ULL
#define	SPA_VERSION_22			22ULL
#define	SPA_VERSION_23			23ULL
#define	SPA_VERSION_24			24ULL
#define	SPA_VERSION_25			25ULL
#define	SPA_VERSION_26			26ULL
#define	SPA_VERSION_27			27ULL
#define	SPA_VERSION_28			28ULL
#define	SPA_VERSION_5000		5000ULL

/*
 * The incrementing pool version number has been replaced by pool feature
 * flags.  For more details, see zfeature.c.
 */
#define	SPA_VERSION			SPA_VERSION_5000
#define	SPA_VERSION_STRING		"5000"

/*
 * Symbolic names for the changes that caused a SPA_VERSION switch.
 * Used in the code when checking for presence or absence of a feature.
 * Feel free to define multiple symbolic names for each version if there
 * were multiple changes to on-disk structures during that version.
 *
 * NOTE: When checking the current SPA_VERSION in your code, be sure
 *       to use spa_version() since it reports the version of the
 *       last synced uberblock.  Checking the in-flight version can
 *       be dangerous in some cases.
 */
#define	SPA_VERSION_INITIAL		SPA_VERSION_1
#define	SPA_VERSION_DITTO_BLOCKS	SPA_VERSION_2
#define	SPA_VERSION_SPARES		SPA_VERSION_3
#define	SPA_VERSION_RAIDZ2		SPA_VERSION_3
#define	SPA_VERSION_BPOBJ_ACCOUNT	SPA_VERSION_3
#define	SPA_VERSION_RAIDZ_DEFLATE	SPA_VERSION_3
#define	SPA_VERSION_DNODE_BYTES		SPA_VERSION_3
#define	SPA_VERSION_ZPOOL_HISTORY	SPA_VERSION_4
#define	SPA_VERSION_GZIP_COMPRESSION	SPA_VERSION_5
#define	SPA_VERSION_BOOTFS		SPA_VERSION_6
#define	SPA_VERSION_SLOGS		SPA_VERSION_7
#define	SPA_VERSION_DELEGATED_PERMS	SPA_VERSION_8
#define	SPA_VERSION_FUID		SPA_VERSION_9
#define	SPA_VERSION_REFRESERVATION	SPA_VERSION_9
#define	SPA_VERSION_REFQUOTA		SPA_VERSION_9
#define	SPA_VERSION_UNIQUE_ACCURATE	SPA_VERSION_9
#define	SPA_VERSION_L2CACHE		SPA_VERSION_10
#define	SPA_VERSION_NEXT_CLONES		SPA_VERSION_11
#define	SPA_VERSION_ORIGIN		SPA_VERSION_11
#define	SPA_VERSION_DSL_SCRUB		SPA_VERSION_11
#define	SPA_VERSION_SNAP_PROPS		SPA_VERSION_12
#define	SPA_VERSION_USED_BREAKDOWN	SPA_VERSION_13
#define	SPA_VERSION_PASSTHROUGH_X	SPA_VERSION_14
#define	SPA_VERSION_USERSPACE		SPA_VERSION_15
#define	SPA_VERSION_STMF_PROP		SPA_VERSION_16
#define	SPA_VERSION_RAIDZ3		SPA_VERSION_17
#define	SPA_VERSION_USERREFS		SPA_VERSION_18
#define	SPA_VERSION_HOLES		SPA_VERSION_19
#define	SPA_VERSION_ZLE_COMPRESSION	SPA_VERSION_20
#define	SPA_VERSION_DEDUP		SPA_VERSION_21
#define	SPA_VERSION_RECVD_PROPS		SPA_VERSION_22
#define	SPA_VERSION_SLIM_ZIL		SPA_VERSION_23
#define	SPA_VERSION_SA			SPA_VERSION_24
#define	SPA_VERSION_SCAN		SPA_VERSION_25
#define	SPA_VERSION_DIR_CLONES		SPA_VERSION_26
#define	SPA_VERSION_DEADLISTS		SPA_VERSION_26
#define	SPA_VERSION_FAST_SNAP		SPA_VERSION_27
#define	SPA_VERSION_MULTI_REPLACE	SPA_VERSION_28
#define	SPA_VERSION_BEFORE_FEATURES	SPA_VERSION_28
#define	SPA_VERSION_FEATURES		SPA_VERSION_5000

#define	SPA_VERSION_IS_SUPPORTED(v) \
	(((v) >= SPA_VERSION_INITIAL && (v) <= SPA_VERSION_BEFORE_FEATURES) || \
	((v) >= SPA_VERSION_FEATURES && (v) <= SPA_VERSION))

/*
 * ZPL version - rev'd whenever an incompatible on-disk format change
 * occurs.  This is independent of SPA/DMU/ZAP versioning.  You must
 * also update the version_table[] and help message in zfs_prop.c.
 */
#define	ZPL_VERSION_1			1ULL
#define	ZPL_VERSION_2			2ULL
#define	ZPL_VERSION_3			3ULL
#define	ZPL_VERSION_4			4ULL
#define	ZPL_VERSION_5			5ULL
#define	ZPL_VERSION			ZPL_VERSION_5
#define	ZPL_VERSION_STRING		"5"

#define	ZPL_VERSION_INITIAL		ZPL_VERSION_1
#define	ZPL_VERSION_DIRENT_TYPE		ZPL_VERSION_2
#define	ZPL_VERSION_FUID		ZPL_VERSION_3
#define	ZPL_VERSION_NORMALIZATION	ZPL_VERSION_3
#define	ZPL_VERSION_SYSATTR		ZPL_VERSION_3
#define	ZPL_VERSION_USERSPACE		ZPL_VERSION_4
#define	ZPL_VERSION_SA			ZPL_VERSION_5

/* Persistent L2ARC version */
#define	L2ARC_PERSISTENT_VERSION_1	1ULL
#define	L2ARC_PERSISTENT_VERSION	L2ARC_PERSISTENT_VERSION_1
#define	L2ARC_PERSISTENT_VERSION_STRING	"1"

/* Rewind policy information */
#define	ZPOOL_NO_REWIND		1  /* No policy - default behavior */
#define	ZPOOL_NEVER_REWIND	2  /* Do not search for best txg or rewind */
#define	ZPOOL_TRY_REWIND	4  /* Search for best txg, but do not rewind */
#define	ZPOOL_DO_REWIND		8  /* Rewind to best txg w/in deferred frees */
#define	ZPOOL_EXTREME_REWIND	16 /* Allow extreme measures to find best txg */
#define	ZPOOL_REWIND_MASK	28 /* All the possible rewind bits */
#define	ZPOOL_REWIND_POLICIES	31 /* All the possible policy bits */

typedef struct zpool_load_policy {
	uint32_t	zlp_rewind;	/* rewind policy requested */
	uint64_t	zlp_maxmeta;	/* max acceptable meta-data errors */
	uint64_t	zlp_maxdata;	/* max acceptable data errors */
	uint64_t	zlp_txg;	/* specific txg to load */
} zpool_load_policy_t;

/*
 * The following are configuration names used in the nvlist describing a pool's
 * configuration.  New on-disk names should be prefixed with "<reversed-DNS>:"
 * (e.g. "org.openzfs:") to avoid conflicting names being developed
 * independently.
 */
#define	ZPOOL_CONFIG_VERSION		"version"
#define	ZPOOL_CONFIG_POOL_NAME		"name"
#define	ZPOOL_CONFIG_POOL_STATE		"state"
#define	ZPOOL_CONFIG_POOL_TXG		"txg"
#define	ZPOOL_CONFIG_POOL_GUID		"pool_guid"
#define	ZPOOL_CONFIG_CREATE_TXG		"create_txg"
#define	ZPOOL_CONFIG_TOP_GUID		"top_guid"
#define	ZPOOL_CONFIG_VDEV_TREE		"vdev_tree"
#define	ZPOOL_CONFIG_TYPE		"type"
#define	ZPOOL_CONFIG_CHILDREN		"children"
#define	ZPOOL_CONFIG_ID			"id"
#define	ZPOOL_CONFIG_GUID		"guid"
#define	ZPOOL_CONFIG_INDIRECT_OBJECT	"com.delphix:indirect_object"
#define	ZPOOL_CONFIG_INDIRECT_BIRTHS	"com.delphix:indirect_births"
#define	ZPOOL_CONFIG_PREV_INDIRECT_VDEV	"com.delphix:prev_indirect_vdev"
#define	ZPOOL_CONFIG_PATH		"path"
#define	ZPOOL_CONFIG_DEVID		"devid"
#define	ZPOOL_CONFIG_SPARE_ID		"spareid"
#define	ZPOOL_CONFIG_METASLAB_ARRAY	"metaslab_array"
#define	ZPOOL_CONFIG_METASLAB_SHIFT	"metaslab_shift"
#define	ZPOOL_CONFIG_ASHIFT		"ashift"
#define	ZPOOL_CONFIG_ASIZE		"asize"
#define	ZPOOL_CONFIG_DTL		"DTL"
#define	ZPOOL_CONFIG_SCAN_STATS		"scan_stats"	/* not stored on disk */
#define	ZPOOL_CONFIG_REMOVAL_STATS	"removal_stats"	/* not stored on disk */
#define	ZPOOL_CONFIG_CHECKPOINT_STATS	"checkpoint_stats" /* not on disk */
#define	ZPOOL_CONFIG_VDEV_STATS		"vdev_stats"	/* not stored on disk */
#define	ZPOOL_CONFIG_INDIRECT_SIZE	"indirect_size"	/* not stored on disk */

/* container nvlist of extended stats */
#define	ZPOOL_CONFIG_VDEV_STATS_EX	"vdev_stats_ex"

/* Active queue read/write stats */
#define	ZPOOL_CONFIG_VDEV_SYNC_R_ACTIVE_QUEUE	"vdev_sync_r_active_queue"
#define	ZPOOL_CONFIG_VDEV_SYNC_W_ACTIVE_QUEUE	"vdev_sync_w_active_queue"
#define	ZPOOL_CONFIG_VDEV_ASYNC_R_ACTIVE_QUEUE	"vdev_async_r_active_queue"
#define	ZPOOL_CONFIG_VDEV_ASYNC_W_ACTIVE_QUEUE	"vdev_async_w_active_queue"
#define	ZPOOL_CONFIG_VDEV_SCRUB_ACTIVE_QUEUE	"vdev_async_scrub_active_queue"
#define	ZPOOL_CONFIG_VDEV_TRIM_ACTIVE_QUEUE	"vdev_async_trim_active_queue"
#define	ZPOOL_CONFIG_VDEV_REBUILD_ACTIVE_QUEUE	"vdev_rebuild_active_queue"

/* Queue sizes */
#define	ZPOOL_CONFIG_VDEV_SYNC_R_PEND_QUEUE	"vdev_sync_r_pend_queue"
#define	ZPOOL_CONFIG_VDEV_SYNC_W_PEND_QUEUE	"vdev_sync_w_pend_queue"
#define	ZPOOL_CONFIG_VDEV_ASYNC_R_PEND_QUEUE	"vdev_async_r_pend_queue"
#define	ZPOOL_CONFIG_VDEV_ASYNC_W_PEND_QUEUE	"vdev_async_w_pend_queue"
#define	ZPOOL_CONFIG_VDEV_SCRUB_PEND_QUEUE	"vdev_async_scrub_pend_queue"
#define	ZPOOL_CONFIG_VDEV_TRIM_PEND_QUEUE	"vdev_async_trim_pend_queue"
#define	ZPOOL_CONFIG_VDEV_REBUILD_PEND_QUEUE	"vdev_rebuild_pend_queue"

/* Latency read/write histogram stats */
#define	ZPOOL_CONFIG_VDEV_TOT_R_LAT_HISTO	"vdev_tot_r_lat_histo"
#define	ZPOOL_CONFIG_VDEV_TOT_W_LAT_HISTO	"vdev_tot_w_lat_histo"
#define	ZPOOL_CONFIG_VDEV_DISK_R_LAT_HISTO	"vdev_disk_r_lat_histo"
#define	ZPOOL_CONFIG_VDEV_DISK_W_LAT_HISTO	"vdev_disk_w_lat_histo"
#define	ZPOOL_CONFIG_VDEV_SYNC_R_LAT_HISTO	"vdev_sync_r_lat_histo"
#define	ZPOOL_CONFIG_VDEV_SYNC_W_LAT_HISTO	"vdev_sync_w_lat_histo"
#define	ZPOOL_CONFIG_VDEV_ASYNC_R_LAT_HISTO	"vdev_async_r_lat_histo"
#define	ZPOOL_CONFIG_VDEV_ASYNC_W_LAT_HISTO	"vdev_async_w_lat_histo"
#define	ZPOOL_CONFIG_VDEV_SCRUB_LAT_HISTO	"vdev_scrub_histo"
#define	ZPOOL_CONFIG_VDEV_TRIM_LAT_HISTO	"vdev_trim_histo"
#define	ZPOOL_CONFIG_VDEV_REBUILD_LAT_HISTO	"vdev_rebuild_histo"

/* Request size histograms */
#define	ZPOOL_CONFIG_VDEV_SYNC_IND_R_HISTO	"vdev_sync_ind_r_histo"
#define	ZPOOL_CONFIG_VDEV_SYNC_IND_W_HISTO	"vdev_sync_ind_w_histo"
#define	ZPOOL_CONFIG_VDEV_ASYNC_IND_R_HISTO	"vdev_async_ind_r_histo"
#define	ZPOOL_CONFIG_VDEV_ASYNC_IND_W_HISTO	"vdev_async_ind_w_histo"
#define	ZPOOL_CONFIG_VDEV_IND_SCRUB_HISTO	"vdev_ind_scrub_histo"
#define	ZPOOL_CONFIG_VDEV_IND_TRIM_HISTO	"vdev_ind_trim_histo"
#define	ZPOOL_CONFIG_VDEV_IND_REBUILD_HISTO	"vdev_ind_rebuild_histo"
#define	ZPOOL_CONFIG_VDEV_SYNC_AGG_R_HISTO	"vdev_sync_agg_r_histo"
#define	ZPOOL_CONFIG_VDEV_SYNC_AGG_W_HISTO	"vdev_sync_agg_w_histo"
#define	ZPOOL_CONFIG_VDEV_ASYNC_AGG_R_HISTO	"vdev_async_agg_r_histo"
#define	ZPOOL_CONFIG_VDEV_ASYNC_AGG_W_HISTO	"vdev_async_agg_w_histo"
#define	ZPOOL_CONFIG_VDEV_AGG_SCRUB_HISTO	"vdev_agg_scrub_histo"
#define	ZPOOL_CONFIG_VDEV_AGG_TRIM_HISTO	"vdev_agg_trim_histo"
#define	ZPOOL_CONFIG_VDEV_AGG_REBUILD_HISTO	"vdev_agg_rebuild_histo"

/* Number of slow IOs */
#define	ZPOOL_CONFIG_VDEV_SLOW_IOS		"vdev_slow_ios"

/* vdev enclosure sysfs path */
#define	ZPOOL_CONFIG_VDEV_ENC_SYSFS_PATH	"vdev_enc_sysfs_path"

#define	ZPOOL_CONFIG_WHOLE_DISK		"whole_disk"
#define	ZPOOL_CONFIG_ERRCOUNT		"error_count"
#define	ZPOOL_CONFIG_NOT_PRESENT	"not_present"
#define	ZPOOL_CONFIG_SPARES		"spares"
#define	ZPOOL_CONFIG_IS_SPARE		"is_spare"
#define	ZPOOL_CONFIG_NPARITY		"nparity"
#define	ZPOOL_CONFIG_HOSTID		"hostid"
#define	ZPOOL_CONFIG_HOSTNAME		"hostname"
#define	ZPOOL_CONFIG_LOADED_TIME	"initial_load_time"
#define	ZPOOL_CONFIG_UNSPARE		"unspare"
#define	ZPOOL_CONFIG_PHYS_PATH		"phys_path"
#define	ZPOOL_CONFIG_IS_LOG		"is_log"
#define	ZPOOL_CONFIG_L2CACHE		"l2cache"
#define	ZPOOL_CONFIG_HOLE_ARRAY		"hole_array"
#define	ZPOOL_CONFIG_VDEV_CHILDREN	"vdev_children"
#define	ZPOOL_CONFIG_IS_HOLE		"is_hole"
#define	ZPOOL_CONFIG_DDT_HISTOGRAM	"ddt_histogram"
#define	ZPOOL_CONFIG_DDT_OBJ_STATS	"ddt_object_stats"
#define	ZPOOL_CONFIG_DDT_STATS		"ddt_stats"
#define	ZPOOL_CONFIG_SPLIT		"splitcfg"
#define	ZPOOL_CONFIG_ORIG_GUID		"orig_guid"
#define	ZPOOL_CONFIG_SPLIT_GUID		"split_guid"
#define	ZPOOL_CONFIG_SPLIT_LIST		"guid_list"
#define	ZPOOL_CONFIG_NONALLOCATING	"non_allocating"
#define	ZPOOL_CONFIG_REMOVING		"removing"
#define	ZPOOL_CONFIG_RESILVER_TXG	"resilver_txg"
#define	ZPOOL_CONFIG_REBUILD_TXG	"rebuild_txg"
#define	ZPOOL_CONFIG_COMMENT		"comment"
#define	ZPOOL_CONFIG_SUSPENDED		"suspended"	/* not stored on disk */
#define	ZPOOL_CONFIG_SUSPENDED_REASON	"suspended_reason"	/* not stored */
#define	ZPOOL_CONFIG_TIMESTAMP		"timestamp"	/* not stored on disk */
#define	ZPOOL_CONFIG_BOOTFS		"bootfs"	/* not stored on disk */
#define	ZPOOL_CONFIG_MISSING_DEVICES	"missing_vdevs"	/* not stored on disk */
#define	ZPOOL_CONFIG_LOAD_INFO		"load_info"	/* not stored on disk */
#define	ZPOOL_CONFIG_REWIND_INFO	"rewind_info"	/* not stored on disk */
#define	ZPOOL_CONFIG_UNSUP_FEAT		"unsup_feat"	/* not stored on disk */
#define	ZPOOL_CONFIG_ENABLED_FEAT	"enabled_feat"	/* not stored on disk */
#define	ZPOOL_CONFIG_CAN_RDONLY		"can_rdonly"	/* not stored on disk */
#define	ZPOOL_CONFIG_FEATURES_FOR_READ	"features_for_read"
#define	ZPOOL_CONFIG_FEATURE_STATS	"feature_stats"	/* not stored on disk */
#define	ZPOOL_CONFIG_ERRATA		"errata"	/* not stored on disk */
#define	ZPOOL_CONFIG_VDEV_TOP_ZAP	"com.delphix:vdev_zap_top"
#define	ZPOOL_CONFIG_VDEV_LEAF_ZAP	"com.delphix:vdev_zap_leaf"
#define	ZPOOL_CONFIG_HAS_PER_VDEV_ZAPS	"com.delphix:has_per_vdev_zaps"
#define	ZPOOL_CONFIG_RESILVER_DEFER	"com.datto:resilver_defer"
#define	ZPOOL_CONFIG_CACHEFILE		"cachefile"	/* not stored on disk */
#define	ZPOOL_CONFIG_MMP_STATE		"mmp_state"	/* not stored on disk */
#define	ZPOOL_CONFIG_MMP_TXG		"mmp_txg"	/* not stored on disk */
#define	ZPOOL_CONFIG_MMP_SEQ		"mmp_seq"	/* not stored on disk */
#define	ZPOOL_CONFIG_MMP_HOSTNAME	"mmp_hostname"	/* not stored on disk */
#define	ZPOOL_CONFIG_MMP_HOSTID		"mmp_hostid"	/* not stored on disk */
#define	ZPOOL_CONFIG_ALLOCATION_BIAS	"alloc_bias"	/* not stored on disk */
#define	ZPOOL_CONFIG_EXPANSION_TIME	"expansion_time"	/* not stored */
#define	ZPOOL_CONFIG_REBUILD_STATS	"org.openzfs:rebuild_stats"
#define	ZPOOL_CONFIG_COMPATIBILITY	"compatibility"

/*
 * The persistent vdev state is stored as separate values rather than a single
 * 'vdev_state' entry.  This is because a device can be in multiple states, such
 * as offline and degraded.
 */
#define	ZPOOL_CONFIG_OFFLINE		"offline"
#define	ZPOOL_CONFIG_FAULTED		"faulted"
#define	ZPOOL_CONFIG_DEGRADED		"degraded"
#define	ZPOOL_CONFIG_REMOVED		"removed"
#define	ZPOOL_CONFIG_FRU		"fru"
#define	ZPOOL_CONFIG_AUX_STATE		"aux_state"

/* Pool load policy parameters */
#define	ZPOOL_LOAD_POLICY		"load-policy"
#define	ZPOOL_LOAD_REWIND_POLICY	"load-rewind-policy"
#define	ZPOOL_LOAD_REQUEST_TXG		"load-request-txg"
#define	ZPOOL_LOAD_META_THRESH		"load-meta-thresh"
#define	ZPOOL_LOAD_DATA_THRESH		"load-data-thresh"

/* Rewind data discovered */
#define	ZPOOL_CONFIG_LOAD_TIME		"rewind_txg_ts"
#define	ZPOOL_CONFIG_LOAD_DATA_ERRORS	"verify_data_errors"
#define	ZPOOL_CONFIG_REWIND_TIME	"seconds_of_rewind"

/* dRAID configuration */
#define	ZPOOL_CONFIG_DRAID_NDATA	"draid_ndata"
#define	ZPOOL_CONFIG_DRAID_NSPARES	"draid_nspares"
#define	ZPOOL_CONFIG_DRAID_NGROUPS	"draid_ngroups"

#define	VDEV_TYPE_ROOT			"root"
#define	VDEV_TYPE_MIRROR		"mirror"
#define	VDEV_TYPE_REPLACING		"replacing"
#define	VDEV_TYPE_RAIDZ			"raidz"
#define	VDEV_TYPE_DRAID			"draid"
#define	VDEV_TYPE_DRAID_SPARE		"dspare"
#define	VDEV_TYPE_DISK			"disk"
#define	VDEV_TYPE_FILE			"file"
#define	VDEV_TYPE_MISSING		"missing"
#define	VDEV_TYPE_HOLE			"hole"
#define	VDEV_TYPE_SPARE			"spare"
#define	VDEV_TYPE_LOG			"log"
#define	VDEV_TYPE_L2CACHE		"l2cache"
#define	VDEV_TYPE_INDIRECT		"indirect"

#define	VDEV_RAIDZ_MAXPARITY		3

#define	VDEV_DRAID_MAXPARITY		3
#define	VDEV_DRAID_MIN_CHILDREN		2
#define	VDEV_DRAID_MAX_CHILDREN		UINT8_MAX

/* VDEV_TOP_ZAP_* are used in top-level vdev ZAP objects. */
#define	VDEV_TOP_ZAP_INDIRECT_OBSOLETE_SM \
	"com.delphix:indirect_obsolete_sm"
#define	VDEV_TOP_ZAP_OBSOLETE_COUNTS_ARE_PRECISE \
	"com.delphix:obsolete_counts_are_precise"
#define	VDEV_TOP_ZAP_POOL_CHECKPOINT_SM \
	"com.delphix:pool_checkpoint_sm"
#define	VDEV_TOP_ZAP_MS_UNFLUSHED_PHYS_TXGS \
	"com.delphix:ms_unflushed_phys_txgs"

#define	VDEV_TOP_ZAP_VDEV_REBUILD_PHYS \
	"org.openzfs:vdev_rebuild"

#define	VDEV_TOP_ZAP_ALLOCATION_BIAS \
	"org.zfsonlinux:allocation_bias"

/* vdev metaslab allocation bias */
#define	VDEV_ALLOC_BIAS_LOG		"log"
#define	VDEV_ALLOC_BIAS_SPECIAL		"special"
#define	VDEV_ALLOC_BIAS_DEDUP		"dedup"

/* vdev initialize state */
#define	VDEV_LEAF_ZAP_INITIALIZE_LAST_OFFSET	\
	"com.delphix:next_offset_to_initialize"
#define	VDEV_LEAF_ZAP_INITIALIZE_STATE	\
	"com.delphix:vdev_initialize_state"
#define	VDEV_LEAF_ZAP_INITIALIZE_ACTION_TIME	\
	"com.delphix:vdev_initialize_action_time"

/* vdev TRIM state */
#define	VDEV_LEAF_ZAP_TRIM_LAST_OFFSET	\
	"org.zfsonlinux:next_offset_to_trim"
#define	VDEV_LEAF_ZAP_TRIM_STATE	\
	"org.zfsonlinux:vdev_trim_state"
#define	VDEV_LEAF_ZAP_TRIM_ACTION_TIME	\
	"org.zfsonlinux:vdev_trim_action_time"
#define	VDEV_LEAF_ZAP_TRIM_RATE		\
	"org.zfsonlinux:vdev_trim_rate"
#define	VDEV_LEAF_ZAP_TRIM_PARTIAL	\
	"org.zfsonlinux:vdev_trim_partial"
#define	VDEV_LEAF_ZAP_TRIM_SECURE	\
	"org.zfsonlinux:vdev_trim_secure"

/*
 * This is needed in userland to report the minimum necessary device size.
 */
#define	SPA_MINDEVSIZE		(64ULL << 20)

/*
 * Set if the fragmentation has not yet been calculated. This can happen
 * because the space maps have not been upgraded or the histogram feature
 * is not enabled.
 */
#define	ZFS_FRAG_INVALID	UINT64_MAX

/*
 * The location of the pool configuration repository, shared between kernel and
 * userland.
 */
#define	ZPOOL_CACHE_BOOT	"/boot/zfs/zpool.cache"
#define	ZPOOL_CACHE		"/etc/zfs/zpool.cache"
/*
 * Settings for zpool compatibility features files
 */
#define	ZPOOL_SYSCONF_COMPAT_D	SYSCONFDIR "/zfs/compatibility.d"
#define	ZPOOL_DATA_COMPAT_D	PKGDATADIR "/compatibility.d"
#define	ZPOOL_COMPAT_MAXSIZE	16384

/*
 * Hard-wired compatibility settings
 */
#define	ZPOOL_COMPAT_LEGACY	"legacy"
#define	ZPOOL_COMPAT_OFF	"off"

/*
 * vdev states are ordered from least to most healthy.
 * A vdev that's CANT_OPEN or below is considered unusable.
 */
typedef enum vdev_state {
	VDEV_STATE_UNKNOWN = 0,	/* Uninitialized vdev			*/
	VDEV_STATE_CLOSED,	/* Not currently open			*/
	VDEV_STATE_OFFLINE,	/* Not allowed to open			*/
	VDEV_STATE_REMOVED,	/* Explicitly removed from system	*/
	VDEV_STATE_CANT_OPEN,	/* Tried to open, but failed		*/
	VDEV_STATE_FAULTED,	/* External request to fault device	*/
	VDEV_STATE_DEGRADED,	/* Replicated vdev with unhealthy kids	*/
	VDEV_STATE_HEALTHY	/* Presumed good			*/
} vdev_state_t;

#define	VDEV_STATE_ONLINE	VDEV_STATE_HEALTHY

/*
 * vdev aux states.  When a vdev is in the CANT_OPEN state, the aux field
 * of the vdev stats structure uses these constants to distinguish why.
 */
typedef enum vdev_aux {
	VDEV_AUX_NONE,		/* no error				*/
	VDEV_AUX_OPEN_FAILED,	/* ldi_open_*() or vn_open() failed	*/
	VDEV_AUX_CORRUPT_DATA,	/* bad label or disk contents		*/
	VDEV_AUX_NO_REPLICAS,	/* insufficient number of replicas	*/
	VDEV_AUX_BAD_GUID_SUM,	/* vdev guid sum doesn't match		*/
	VDEV_AUX_TOO_SMALL,	/* vdev size is too small		*/
	VDEV_AUX_BAD_LABEL,	/* the label is OK but invalid		*/
	VDEV_AUX_VERSION_NEWER,	/* on-disk version is too new		*/
	VDEV_AUX_VERSION_OLDER,	/* on-disk version is too old		*/
	VDEV_AUX_UNSUP_FEAT,	/* unsupported features			*/
	VDEV_AUX_SPARED,	/* hot spare used in another pool	*/
	VDEV_AUX_ERR_EXCEEDED,	/* too many errors			*/
	VDEV_AUX_IO_FAILURE,	/* experienced I/O failure		*/
	VDEV_AUX_BAD_LOG,	/* cannot read log chain(s)		*/
	VDEV_AUX_EXTERNAL,	/* external diagnosis or forced fault	*/
	VDEV_AUX_SPLIT_POOL,	/* vdev was split off into another pool	*/
	VDEV_AUX_BAD_ASHIFT,	/* vdev ashift is invalid		*/
	VDEV_AUX_EXTERNAL_PERSIST,	/* persistent forced fault	*/
	VDEV_AUX_ACTIVE,	/* vdev active on a different host	*/
	VDEV_AUX_CHILDREN_OFFLINE, /* all children are offline		*/
	VDEV_AUX_ASHIFT_TOO_BIG, /* vdev's min block size is too large   */
} vdev_aux_t;

/*
 * pool state.  The following states are written to disk as part of the normal
 * SPA lifecycle: ACTIVE, EXPORTED, DESTROYED, SPARE, L2CACHE.  The remaining
 * states are software abstractions used at various levels to communicate
 * pool state.
 */
typedef enum pool_state {
	POOL_STATE_ACTIVE = 0,		/* In active use		*/
	POOL_STATE_EXPORTED,		/* Explicitly exported		*/
	POOL_STATE_DESTROYED,		/* Explicitly destroyed		*/
	POOL_STATE_SPARE,		/* Reserved for hot spare use	*/
	POOL_STATE_L2CACHE,		/* Level 2 ARC device		*/
	POOL_STATE_UNINITIALIZED,	/* Internal spa_t state		*/
	POOL_STATE_UNAVAIL,		/* Internal libzfs state	*/
	POOL_STATE_POTENTIALLY_ACTIVE	/* Internal libzfs state	*/
} pool_state_t;

/*
 * mmp state. The following states provide additional detail describing
 * why a pool couldn't be safely imported.
 */
typedef enum mmp_state {
	MMP_STATE_ACTIVE = 0,		/* In active use		*/
	MMP_STATE_INACTIVE,		/* Inactive and safe to import	*/
	MMP_STATE_NO_HOSTID		/* System hostid is not set	*/
} mmp_state_t;

/*
 * Scan Functions.
 */
typedef enum pool_scan_func {
	POOL_SCAN_NONE,
	POOL_SCAN_SCRUB,
	POOL_SCAN_RESILVER,
	POOL_SCAN_FUNCS
} pool_scan_func_t;

/*
 * Used to control scrub pause and resume.
 */
typedef enum pool_scrub_cmd {
	POOL_SCRUB_NORMAL = 0,
	POOL_SCRUB_PAUSE,
	POOL_SCRUB_FLAGS_END
} pool_scrub_cmd_t;

typedef enum {
	CS_NONE,
	CS_CHECKPOINT_EXISTS,
	CS_CHECKPOINT_DISCARDING,
	CS_NUM_STATES
} checkpoint_state_t;

typedef struct pool_checkpoint_stat {
	uint64_t pcs_state;		/* checkpoint_state_t */
	uint64_t pcs_start_time;	/* time checkpoint/discard started */
	uint64_t pcs_space;		/* checkpointed space */
} pool_checkpoint_stat_t;

/*
 * ZIO types.  Needed to interpret vdev statistics below.
 */
typedef enum zio_type {
	ZIO_TYPE_NULL = 0,
	ZIO_TYPE_READ,
	ZIO_TYPE_WRITE,
	ZIO_TYPE_FREE,
	ZIO_TYPE_CLAIM,
	ZIO_TYPE_IOCTL,
	ZIO_TYPE_TRIM,
	ZIO_TYPES
} zio_type_t;

/*
 * Pool statistics.  Note: all fields should be 64-bit because this
 * is passed between kernel and userland as an nvlist uint64 array.
 */
typedef struct pool_scan_stat {
	/* values stored on disk */
	uint64_t	pss_func;	/* pool_scan_func_t */
	uint64_t	pss_state;	/* dsl_scan_state_t */
	uint64_t	pss_start_time;	/* scan start time */
	uint64_t	pss_end_time;	/* scan end time */
	uint64_t	pss_to_examine;	/* total bytes to scan */
	uint64_t	pss_examined;	/* total bytes located by scanner */
	uint64_t	pss_to_process; /* total bytes to process */
	uint64_t	pss_processed;	/* total processed bytes */
	uint64_t	pss_errors;	/* scan errors	*/

	/* values not stored on disk */
	uint64_t	pss_pass_exam; /* examined bytes per scan pass */
	uint64_t	pss_pass_start;	/* start time of a scan pass */
	uint64_t	pss_pass_scrub_pause; /* pause time of a scrub pass */
	/* cumulative time scrub spent paused, needed for rate calculation */
	uint64_t	pss_pass_scrub_spent_paused;
	uint64_t	pss_pass_issued; /* issued bytes per scan pass */
	uint64_t	pss_issued;	/* total bytes checked by scanner */
} pool_scan_stat_t;

typedef struct pool_removal_stat {
	uint64_t prs_state; /* dsl_scan_state_t */
	uint64_t prs_removing_vdev;
	uint64_t prs_start_time;
	uint64_t prs_end_time;
	uint64_t prs_to_copy; /* bytes that need to be copied */
	uint64_t prs_copied; /* bytes copied so far */
	/*
	 * bytes of memory used for indirect mappings.
	 * This includes all removed vdevs.
	 */
	uint64_t prs_mapping_memory;
} pool_removal_stat_t;

typedef enum dsl_scan_state {
	DSS_NONE,
	DSS_SCANNING,
	DSS_FINISHED,
	DSS_CANCELED,
	DSS_NUM_STATES
} dsl_scan_state_t;

typedef struct vdev_rebuild_stat {
	uint64_t vrs_state;		/* vdev_rebuild_state_t */
	uint64_t vrs_start_time;	/* time_t */
	uint64_t vrs_end_time;		/* time_t */
	uint64_t vrs_scan_time_ms;	/* total run time (millisecs) */
	uint64_t vrs_bytes_scanned;	/* allocated bytes scanned */
	uint64_t vrs_bytes_issued;	/* read bytes issued */
	uint64_t vrs_bytes_rebuilt;	/* rebuilt bytes */
	uint64_t vrs_bytes_est;		/* total bytes to scan */
	uint64_t vrs_errors;		/* scanning errors */
	uint64_t vrs_pass_time_ms;	/* pass run time (millisecs) */
	uint64_t vrs_pass_bytes_scanned; /* bytes scanned since start/resume */
	uint64_t vrs_pass_bytes_issued;	/* bytes rebuilt since start/resume */
} vdev_rebuild_stat_t;

/*
 * Errata described by https://openzfs.github.io/openzfs-docs/msg/ZFS-8000-ER.
 * The ordering of this enum must be maintained to ensure the errata identifiers
 * map to the correct documentation.  New errata may only be appended to the
 * list and must contain corresponding documentation at the above link.
 */
typedef enum zpool_errata {
	ZPOOL_ERRATA_NONE,
	ZPOOL_ERRATA_ZOL_2094_SCRUB,
	ZPOOL_ERRATA_ZOL_2094_ASYNC_DESTROY,
	ZPOOL_ERRATA_ZOL_6845_ENCRYPTION,
	ZPOOL_ERRATA_ZOL_8308_ENCRYPTION,
} zpool_errata_t;

/*
 * Vdev statistics.  Note: all fields should be 64-bit because this
 * is passed between kernel and user land as an nvlist uint64 array.
 *
 * The vs_ops[] and vs_bytes[] arrays must always be an array size of 6 in
 * order to keep subsequent members at their known fixed offsets.  When
 * adding a new field it must be added to the end the structure.
 */
#define	VS_ZIO_TYPES	6

typedef struct vdev_stat {
	hrtime_t	vs_timestamp;		/* time since vdev load	*/
	uint64_t	vs_state;		/* vdev state		*/
	uint64_t	vs_aux;			/* see vdev_aux_t	*/
	uint64_t	vs_alloc;		/* space allocated	*/
	uint64_t	vs_space;		/* total capacity	*/
	uint64_t	vs_dspace;		/* deflated capacity	*/
	uint64_t	vs_rsize;		/* replaceable dev size */
	uint64_t	vs_esize;		/* expandable dev size */
	uint64_t	vs_ops[VS_ZIO_TYPES];	/* operation count	*/
	uint64_t	vs_bytes[VS_ZIO_TYPES];	/* bytes read/written	*/
	uint64_t	vs_read_errors;		/* read errors		*/
	uint64_t	vs_write_errors;	/* write errors		*/
	uint64_t	vs_checksum_errors;	/* checksum errors	*/
	uint64_t	vs_initialize_errors;	/* initializing errors	*/
	uint64_t	vs_self_healed;		/* self-healed bytes	*/
	uint64_t	vs_scan_removing;	/* removing?	*/
	uint64_t	vs_scan_processed;	/* scan processed bytes	*/
	uint64_t	vs_fragmentation;	/* device fragmentation */
	uint64_t	vs_initialize_bytes_done; /* bytes initialized */
	uint64_t	vs_initialize_bytes_est; /* total bytes to initialize */
	uint64_t	vs_initialize_state;	/* vdev_initializing_state_t */
	uint64_t	vs_initialize_action_time; /* time_t */
	uint64_t	vs_checkpoint_space;    /* checkpoint-consumed space */
	uint64_t	vs_resilver_deferred;	/* resilver deferred	*/
	uint64_t	vs_slow_ios;		/* slow IOs */
	uint64_t	vs_trim_errors;		/* trimming errors	*/
	uint64_t	vs_trim_notsup;		/* supported by device */
	uint64_t	vs_trim_bytes_done;	/* bytes trimmed */
	uint64_t	vs_trim_bytes_est;	/* total bytes to trim */
	uint64_t	vs_trim_state;		/* vdev_trim_state_t */
	uint64_t	vs_trim_action_time;	/* time_t */
	uint64_t	vs_rebuild_processed;	/* bytes rebuilt */
	uint64_t	vs_configured_ashift;   /* TLV vdev_ashift */
	uint64_t	vs_logical_ashift;	/* vdev_logical_ashift  */
	uint64_t	vs_physical_ashift;	/* vdev_physical_ashift */
	uint64_t	vs_noalloc;		/* allocations halted?	*/
} vdev_stat_t;

/* BEGIN CSTYLED */
#define	VDEV_STAT_VALID(field, uint64_t_field_count) \
    ((uint64_t_field_count * sizeof (uint64_t)) >=	 \
     (offsetof(vdev_stat_t, field) + sizeof (((vdev_stat_t *)NULL)->field)))
/* END CSTYLED */

/*
 * Extended stats
 *
 * These are stats which aren't included in the original iostat output.  For
 * convenience, they are grouped together in vdev_stat_ex, although each stat
 * is individually exported as an nvlist.
 */
typedef struct vdev_stat_ex {
	/* Number of ZIOs issued to disk and waiting to finish */
	uint64_t vsx_active_queue[ZIO_PRIORITY_NUM_QUEUEABLE];

	/* Number of ZIOs pending to be issued to disk */
	uint64_t vsx_pend_queue[ZIO_PRIORITY_NUM_QUEUEABLE];

	/*
	 * Below are the histograms for various latencies. Buckets are in
	 * units of nanoseconds.
	 */

	/*
	 * 2^37 nanoseconds = 134s. Timeouts will probably start kicking in
	 * before this.
	 */
#define	VDEV_L_HISTO_BUCKETS 37		/* Latency histo buckets */
#define	VDEV_RQ_HISTO_BUCKETS 25	/* Request size histo buckets */

	/* Amount of time in ZIO queue (ns) */
	uint64_t vsx_queue_histo[ZIO_PRIORITY_NUM_QUEUEABLE]
	    [VDEV_L_HISTO_BUCKETS];

	/* Total ZIO latency (ns).  Includes queuing and disk access time */
	uint64_t vsx_total_histo[ZIO_TYPES][VDEV_L_HISTO_BUCKETS];

	/* Amount of time to read/write the disk (ns) */
	uint64_t vsx_disk_histo[ZIO_TYPES][VDEV_L_HISTO_BUCKETS];

	/* "lookup the bucket for a value" histogram macros */
#define	HISTO(val, buckets) (val != 0 ? MIN(highbit64(val) - 1, \
	    buckets - 1) : 0)
#define	L_HISTO(a) HISTO(a, VDEV_L_HISTO_BUCKETS)
#define	RQ_HISTO(a) HISTO(a, VDEV_RQ_HISTO_BUCKETS)

	/* Physical IO histogram */
	uint64_t vsx_ind_histo[ZIO_PRIORITY_NUM_QUEUEABLE]
	    [VDEV_RQ_HISTO_BUCKETS];

	/* Delegated (aggregated) physical IO histogram */
	uint64_t vsx_agg_histo[ZIO_PRIORITY_NUM_QUEUEABLE]
	    [VDEV_RQ_HISTO_BUCKETS];

} vdev_stat_ex_t;

/*
 * Initialize functions.
 */
typedef enum pool_initialize_func {
	POOL_INITIALIZE_START,
	POOL_INITIALIZE_CANCEL,
	POOL_INITIALIZE_SUSPEND,
	POOL_INITIALIZE_FUNCS
} pool_initialize_func_t;

/*
 * TRIM functions.
 */
typedef enum pool_trim_func {
	POOL_TRIM_START,
	POOL_TRIM_CANCEL,
	POOL_TRIM_SUSPEND,
	POOL_TRIM_FUNCS
} pool_trim_func_t;

/*
 * DDT statistics.  Note: all fields should be 64-bit because this
 * is passed between kernel and userland as an nvlist uint64 array.
 */
typedef struct ddt_object {
	uint64_t	ddo_count;	/* number of elements in ddt	*/
	uint64_t	ddo_dspace;	/* size of ddt on disk		*/
	uint64_t	ddo_mspace;	/* size of ddt in-core		*/
} ddt_object_t;

typedef struct ddt_stat {
	uint64_t	dds_blocks;	/* blocks			*/
	uint64_t	dds_lsize;	/* logical size			*/
	uint64_t	dds_psize;	/* physical size		*/
	uint64_t	dds_dsize;	/* deflated allocated size	*/
	uint64_t	dds_ref_blocks;	/* referenced blocks		*/
	uint64_t	dds_ref_lsize;	/* referenced lsize * refcnt	*/
	uint64_t	dds_ref_psize;	/* referenced psize * refcnt	*/
	uint64_t	dds_ref_dsize;	/* referenced dsize * refcnt	*/
} ddt_stat_t;

typedef struct ddt_histogram {
	ddt_stat_t	ddh_stat[64];	/* power-of-two histogram buckets */
} ddt_histogram_t;

#define	ZVOL_DRIVER	"zvol"
#define	ZFS_DRIVER	"zfs"
#define	ZFS_DEV		"/dev/zfs"

#define	ZFS_SUPER_MAGIC	0x2fc12fc1

/* general zvol path */
#define	ZVOL_DIR		"/dev/zvol/"

#define	ZVOL_MAJOR		230
#define	ZVOL_MINOR_BITS		4
#define	ZVOL_MINOR_MASK		((1U << ZVOL_MINOR_BITS) - 1)
#define	ZVOL_MINORS		(1 << 4)
#define	ZVOL_DEV_NAME		"zd"

#define	ZVOL_PROP_NAME		"name"
#define	ZVOL_DEFAULT_BLOCKSIZE	16384

typedef enum {
	VDEV_INITIALIZE_NONE,
	VDEV_INITIALIZE_ACTIVE,
	VDEV_INITIALIZE_CANCELED,
	VDEV_INITIALIZE_SUSPENDED,
	VDEV_INITIALIZE_COMPLETE
} vdev_initializing_state_t;

typedef enum {
	VDEV_TRIM_NONE,
	VDEV_TRIM_ACTIVE,
	VDEV_TRIM_CANCELED,
	VDEV_TRIM_SUSPENDED,
	VDEV_TRIM_COMPLETE,
} vdev_trim_state_t;

typedef enum {
	VDEV_REBUILD_NONE,
	VDEV_REBUILD_ACTIVE,
	VDEV_REBUILD_CANCELED,
	VDEV_REBUILD_COMPLETE,
} vdev_rebuild_state_t;

/*
 * nvlist name constants. Facilitate restricting snapshot iteration range for
 * the "list next snapshot" ioctl
 */
#define	SNAP_ITER_MIN_TXG	"snap_iter_min_txg"
#define	SNAP_ITER_MAX_TXG	"snap_iter_max_txg"

/*
 * /dev/zfs ioctl numbers.
 *
 * These numbers cannot change over time. New ioctl numbers must be appended.
 */
typedef enum zfs_ioc {
	/*
	 * Core features - 81/128 numbers reserved.
	 */
#ifdef __FreeBSD__
	ZFS_IOC_FIRST =	0,
#else
	ZFS_IOC_FIRST =	('Z' << 8),
#endif
	ZFS_IOC = ZFS_IOC_FIRST,
	ZFS_IOC_POOL_CREATE = ZFS_IOC_FIRST,	/* 0x5a00 */
	ZFS_IOC_POOL_DESTROY,			/* 0x5a01 */
	ZFS_IOC_POOL_IMPORT,			/* 0x5a02 */
	ZFS_IOC_POOL_EXPORT,			/* 0x5a03 */
	ZFS_IOC_POOL_CONFIGS,			/* 0x5a04 */
	ZFS_IOC_POOL_STATS,			/* 0x5a05 */
	ZFS_IOC_POOL_TRYIMPORT,			/* 0x5a06 */
	ZFS_IOC_POOL_SCAN,			/* 0x5a07 */
	ZFS_IOC_POOL_FREEZE,			/* 0x5a08 */
	ZFS_IOC_POOL_UPGRADE,			/* 0x5a09 */
	ZFS_IOC_POOL_GET_HISTORY,		/* 0x5a0a */
	ZFS_IOC_VDEV_ADD,			/* 0x5a0b */
	ZFS_IOC_VDEV_REMOVE,			/* 0x5a0c */
	ZFS_IOC_VDEV_SET_STATE,			/* 0x5a0d */
	ZFS_IOC_VDEV_ATTACH,			/* 0x5a0e */
	ZFS_IOC_VDEV_DETACH,			/* 0x5a0f */
	ZFS_IOC_VDEV_SETPATH,			/* 0x5a10 */
	ZFS_IOC_VDEV_SETFRU,			/* 0x5a11 */
	ZFS_IOC_OBJSET_STATS,			/* 0x5a12 */
	ZFS_IOC_OBJSET_ZPLPROPS,		/* 0x5a13 */
	ZFS_IOC_DATASET_LIST_NEXT,		/* 0x5a14 */
	ZFS_IOC_SNAPSHOT_LIST_NEXT,		/* 0x5a15 */
	ZFS_IOC_SET_PROP,			/* 0x5a16 */
	ZFS_IOC_CREATE,				/* 0x5a17 */
	ZFS_IOC_DESTROY,			/* 0x5a18 */
	ZFS_IOC_ROLLBACK,			/* 0x5a19 */
	ZFS_IOC_RENAME,				/* 0x5a1a */
	ZFS_IOC_RECV,				/* 0x5a1b */
	ZFS_IOC_SEND,				/* 0x5a1c */
	ZFS_IOC_INJECT_FAULT,			/* 0x5a1d */
	ZFS_IOC_CLEAR_FAULT,			/* 0x5a1e */
	ZFS_IOC_INJECT_LIST_NEXT,		/* 0x5a1f */
	ZFS_IOC_ERROR_LOG,			/* 0x5a20 */
	ZFS_IOC_CLEAR,				/* 0x5a21 */
	ZFS_IOC_PROMOTE,			/* 0x5a22 */
	ZFS_IOC_SNAPSHOT,			/* 0x5a23 */
	ZFS_IOC_DSOBJ_TO_DSNAME,		/* 0x5a24 */
	ZFS_IOC_OBJ_TO_PATH,			/* 0x5a25 */
	ZFS_IOC_POOL_SET_PROPS,			/* 0x5a26 */
	ZFS_IOC_POOL_GET_PROPS,			/* 0x5a27 */
	ZFS_IOC_SET_FSACL,			/* 0x5a28 */
	ZFS_IOC_GET_FSACL,			/* 0x5a29 */
	ZFS_IOC_SHARE,				/* 0x5a2a */
	ZFS_IOC_INHERIT_PROP,			/* 0x5a2b */
	ZFS_IOC_SMB_ACL,			/* 0x5a2c */
	ZFS_IOC_USERSPACE_ONE,			/* 0x5a2d */
	ZFS_IOC_USERSPACE_MANY,			/* 0x5a2e */
	ZFS_IOC_USERSPACE_UPGRADE,		/* 0x5a2f */
	ZFS_IOC_HOLD,				/* 0x5a30 */
	ZFS_IOC_RELEASE,			/* 0x5a31 */
	ZFS_IOC_GET_HOLDS,			/* 0x5a32 */
	ZFS_IOC_OBJSET_RECVD_PROPS,		/* 0x5a33 */
	ZFS_IOC_VDEV_SPLIT,			/* 0x5a34 */
	ZFS_IOC_NEXT_OBJ,			/* 0x5a35 */
	ZFS_IOC_DIFF,				/* 0x5a36 */
	ZFS_IOC_TMP_SNAPSHOT,			/* 0x5a37 */
	ZFS_IOC_OBJ_TO_STATS,			/* 0x5a38 */
	ZFS_IOC_SPACE_WRITTEN,			/* 0x5a39 */
	ZFS_IOC_SPACE_SNAPS,			/* 0x5a3a */
	ZFS_IOC_DESTROY_SNAPS,			/* 0x5a3b */
	ZFS_IOC_POOL_REGUID,			/* 0x5a3c */
	ZFS_IOC_POOL_REOPEN,			/* 0x5a3d */
	ZFS_IOC_SEND_PROGRESS,			/* 0x5a3e */
	ZFS_IOC_LOG_HISTORY,			/* 0x5a3f */
	ZFS_IOC_SEND_NEW,			/* 0x5a40 */
	ZFS_IOC_SEND_SPACE,			/* 0x5a41 */
	ZFS_IOC_CLONE,				/* 0x5a42 */
	ZFS_IOC_BOOKMARK,			/* 0x5a43 */
	ZFS_IOC_GET_BOOKMARKS,			/* 0x5a44 */
	ZFS_IOC_DESTROY_BOOKMARKS,		/* 0x5a45 */
	ZFS_IOC_RECV_NEW,			/* 0x5a46 */
	ZFS_IOC_POOL_SYNC,			/* 0x5a47 */
	ZFS_IOC_CHANNEL_PROGRAM,		/* 0x5a48 */
	ZFS_IOC_LOAD_KEY,			/* 0x5a49 */
	ZFS_IOC_UNLOAD_KEY,			/* 0x5a4a */
	ZFS_IOC_CHANGE_KEY,			/* 0x5a4b */
	ZFS_IOC_REMAP,				/* 0x5a4c */
	ZFS_IOC_POOL_CHECKPOINT,		/* 0x5a4d */
	ZFS_IOC_POOL_DISCARD_CHECKPOINT,	/* 0x5a4e */
	ZFS_IOC_POOL_INITIALIZE,		/* 0x5a4f */
	ZFS_IOC_POOL_TRIM,			/* 0x5a50 */
	ZFS_IOC_REDACT,				/* 0x5a51 */
	ZFS_IOC_GET_BOOKMARK_PROPS,		/* 0x5a52 */
	ZFS_IOC_WAIT,				/* 0x5a53 */
	ZFS_IOC_WAIT_FS,			/* 0x5a54 */
	ZFS_IOC_VDEV_GET_PROPS,			/* 0x5a55 */
	ZFS_IOC_VDEV_SET_PROPS,			/* 0x5a56 */

	/*
	 * Per-platform (Optional) - 8/128 numbers reserved.
	 */
	ZFS_IOC_PLATFORM = ZFS_IOC_FIRST + 0x80,
	ZFS_IOC_EVENTS_NEXT,			/* 0x81 (Linux) */
	ZFS_IOC_EVENTS_CLEAR,			/* 0x82 (Linux) */
	ZFS_IOC_EVENTS_SEEK,			/* 0x83 (Linux) */
	ZFS_IOC_NEXTBOOT,			/* 0x84 (FreeBSD) */
	ZFS_IOC_JAIL,				/* 0x85 (FreeBSD) */
	ZFS_IOC_UNJAIL,				/* 0x86 (FreeBSD) */
	ZFS_IOC_SET_BOOTENV,			/* 0x87 */
	ZFS_IOC_GET_BOOTENV,			/* 0x88 */
	ZFS_IOC_LAST
} zfs_ioc_t;

/*
 * zvol ioctl to get dataset name
 */
#define	BLKZNAME		_IOR(0x12, 125, char[ZFS_MAX_DATASET_NAME_LEN])

/*
 * ZFS-specific error codes used for returning descriptive errors
 * to the userland through zfs ioctls.
 *
 * The enum implicitly includes all the error codes from errno.h.
 * New code should use and extend this enum for errors that are
 * not described precisely by generic errno codes.
 *
 * These numbers should not change over time. New entries should be appended.
 *
 * (Keep in sync with contrib/pyzfs/libzfs_core/_constants.py)
 */
typedef enum {
	ZFS_ERR_CHECKPOINT_EXISTS = 1024,
	ZFS_ERR_DISCARDING_CHECKPOINT,
	ZFS_ERR_NO_CHECKPOINT,
	ZFS_ERR_DEVRM_IN_PROGRESS,
	ZFS_ERR_VDEV_TOO_BIG,
	ZFS_ERR_IOC_CMD_UNAVAIL,
	ZFS_ERR_IOC_ARG_UNAVAIL,
	ZFS_ERR_IOC_ARG_REQUIRED,
	ZFS_ERR_IOC_ARG_BADTYPE,
	ZFS_ERR_WRONG_PARENT,
	ZFS_ERR_FROM_IVSET_GUID_MISSING,
	ZFS_ERR_FROM_IVSET_GUID_MISMATCH,
	ZFS_ERR_SPILL_BLOCK_FLAG_MISSING,
	ZFS_ERR_UNKNOWN_SEND_STREAM_FEATURE,
	ZFS_ERR_EXPORT_IN_PROGRESS,
	ZFS_ERR_BOOKMARK_SOURCE_NOT_ANCESTOR,
	ZFS_ERR_STREAM_TRUNCATED,
	ZFS_ERR_STREAM_LARGE_BLOCK_MISMATCH,
	ZFS_ERR_RESILVER_IN_PROGRESS,
	ZFS_ERR_REBUILD_IN_PROGRESS,
	ZFS_ERR_BADPROP,
	ZFS_ERR_VDEV_NOTSUP,
} zfs_errno_t;

/*
 * Internal SPA load state.  Used by FMA diagnosis engine.
 */
typedef enum {
	SPA_LOAD_NONE,		/* no load in progress	*/
	SPA_LOAD_OPEN,		/* normal open		*/
	SPA_LOAD_IMPORT,	/* import in progress	*/
	SPA_LOAD_TRYIMPORT,	/* tryimport in progress */
	SPA_LOAD_RECOVER,	/* recovery requested	*/
	SPA_LOAD_ERROR,		/* load failed		*/
	SPA_LOAD_CREATE		/* creation in progress */
} spa_load_state_t;

typedef enum {
	ZPOOL_WAIT_CKPT_DISCARD,
	ZPOOL_WAIT_FREE,
	ZPOOL_WAIT_INITIALIZE,
	ZPOOL_WAIT_REPLACE,
	ZPOOL_WAIT_REMOVE,
	ZPOOL_WAIT_RESILVER,
	ZPOOL_WAIT_SCRUB,
	ZPOOL_WAIT_TRIM,
	ZPOOL_WAIT_NUM_ACTIVITIES
} zpool_wait_activity_t;

typedef enum {
	ZFS_WAIT_DELETEQ,
	ZFS_WAIT_NUM_ACTIVITIES
} zfs_wait_activity_t;

/*
 * Bookmark name values.
 */
#define	ZPOOL_ERR_LIST		"error list"
#define	ZPOOL_ERR_DATASET	"dataset"
#define	ZPOOL_ERR_OBJECT	"object"

#define	HIS_MAX_RECORD_LEN	(MAXPATHLEN + MAXPATHLEN + 1)

/*
 * The following are names used in the nvlist describing
 * the pool's history log.
 */
#define	ZPOOL_HIST_RECORD	"history record"
#define	ZPOOL_HIST_TIME		"history time"
#define	ZPOOL_HIST_CMD		"history command"
#define	ZPOOL_HIST_WHO		"history who"
#define	ZPOOL_HIST_ZONE		"history zone"
#define	ZPOOL_HIST_HOST		"history hostname"
#define	ZPOOL_HIST_TXG		"history txg"
#define	ZPOOL_HIST_INT_EVENT	"history internal event"
#define	ZPOOL_HIST_INT_STR	"history internal str"
#define	ZPOOL_HIST_INT_NAME	"internal_name"
#define	ZPOOL_HIST_IOCTL	"ioctl"
#define	ZPOOL_HIST_INPUT_NVL	"in_nvl"
#define	ZPOOL_HIST_OUTPUT_NVL	"out_nvl"
#define	ZPOOL_HIST_OUTPUT_SIZE	"out_size"
#define	ZPOOL_HIST_DSNAME	"dsname"
#define	ZPOOL_HIST_DSID		"dsid"
#define	ZPOOL_HIST_ERRNO	"errno"
#define	ZPOOL_HIST_ELAPSED_NS	"elapsed_ns"

/*
 * Special nvlist name that will not have its args recorded in the pool's
 * history log.
 */
#define	ZPOOL_HIDDEN_ARGS	"hidden_args"

/*
 * The following are names used when invoking ZFS_IOC_POOL_INITIALIZE.
 */
#define	ZPOOL_INITIALIZE_COMMAND	"initialize_command"
#define	ZPOOL_INITIALIZE_VDEVS		"initialize_vdevs"

/*
 * The following are names used when invoking ZFS_IOC_POOL_TRIM.
 */
#define	ZPOOL_TRIM_COMMAND		"trim_command"
#define	ZPOOL_TRIM_VDEVS		"trim_vdevs"
#define	ZPOOL_TRIM_RATE			"trim_rate"
#define	ZPOOL_TRIM_SECURE		"trim_secure"

/*
 * The following are names used when invoking ZFS_IOC_POOL_WAIT.
 */
#define	ZPOOL_WAIT_ACTIVITY		"wait_activity"
#define	ZPOOL_WAIT_TAG			"wait_tag"
#define	ZPOOL_WAIT_WAITED		"wait_waited"

/*
 * The following are names used when invoking ZFS_IOC_VDEV_GET_PROP.
 */
#define	ZPOOL_VDEV_PROPS_GET_VDEV	"vdevprops_get_vdev"
#define	ZPOOL_VDEV_PROPS_GET_PROPS	"vdevprops_get_props"

/*
 * The following are names used when invoking ZFS_IOC_VDEV_SET_PROP.
 */
#define	ZPOOL_VDEV_PROPS_SET_VDEV	"vdevprops_set_vdev"
#define	ZPOOL_VDEV_PROPS_SET_PROPS	"vdevprops_set_props"

/*
 * The following are names used when invoking ZFS_IOC_WAIT_FS.
 */
#define	ZFS_WAIT_ACTIVITY		"wait_activity"
#define	ZFS_WAIT_WAITED			"wait_waited"

/*
 * Flags for ZFS_IOC_VDEV_SET_STATE
 */
#define	ZFS_ONLINE_CHECKREMOVE	0x1
#define	ZFS_ONLINE_UNSPARE	0x2
#define	ZFS_ONLINE_FORCEFAULT	0x4
#define	ZFS_ONLINE_EXPAND	0x8
#define	ZFS_OFFLINE_TEMPORARY	0x1

/*
 * Flags for ZFS_IOC_POOL_IMPORT
 */
#define	ZFS_IMPORT_NORMAL	0x0
#define	ZFS_IMPORT_VERBATIM	0x1
#define	ZFS_IMPORT_ANY_HOST	0x2
#define	ZFS_IMPORT_MISSING_LOG	0x4
#define	ZFS_IMPORT_ONLY		0x8
#define	ZFS_IMPORT_TEMP_NAME	0x10
#define	ZFS_IMPORT_SKIP_MMP	0x20
#define	ZFS_IMPORT_LOAD_KEYS	0x40
#define	ZFS_IMPORT_CHECKPOINT	0x80

/*
 * Channel program argument/return nvlist keys and defaults.
 */
#define	ZCP_ARG_PROGRAM		"program"
#define	ZCP_ARG_ARGLIST		"arg"
#define	ZCP_ARG_SYNC		"sync"
#define	ZCP_ARG_INSTRLIMIT	"instrlimit"
#define	ZCP_ARG_MEMLIMIT	"memlimit"

#define	ZCP_ARG_CLIARGV		"argv"

#define	ZCP_RET_ERROR		"error"
#define	ZCP_RET_RETURN		"return"

#define	ZCP_DEFAULT_INSTRLIMIT	(10 * 1000 * 1000)
#define	ZCP_MAX_INSTRLIMIT	(10 * ZCP_DEFAULT_INSTRLIMIT)
#define	ZCP_DEFAULT_MEMLIMIT	(10 * 1024 * 1024)
#define	ZCP_MAX_MEMLIMIT	(10 * ZCP_DEFAULT_MEMLIMIT)

/*
 * Sysevent payload members.  ZFS will generate the following sysevents with the
 * given payloads:
 *
 *	ESC_ZFS_RESILVER_START
 *	ESC_ZFS_RESILVER_FINISH
 *
 *		ZFS_EV_POOL_NAME	DATA_TYPE_STRING
 *		ZFS_EV_POOL_GUID	DATA_TYPE_UINT64
 *		ZFS_EV_RESILVER_TYPE	DATA_TYPE_STRING
 *
 *	ESC_ZFS_POOL_DESTROY
 *	ESC_ZFS_POOL_REGUID
 *
 *		ZFS_EV_POOL_NAME	DATA_TYPE_STRING
 *		ZFS_EV_POOL_GUID	DATA_TYPE_UINT64
 *
 *	ESC_ZFS_VDEV_REMOVE
 *	ESC_ZFS_VDEV_CLEAR
 *	ESC_ZFS_VDEV_CHECK
 *
 *		ZFS_EV_POOL_NAME	DATA_TYPE_STRING
 *		ZFS_EV_POOL_GUID	DATA_TYPE_UINT64
 *		ZFS_EV_VDEV_PATH	DATA_TYPE_STRING	(optional)
 *		ZFS_EV_VDEV_GUID	DATA_TYPE_UINT64
 *
 *	ESC_ZFS_HISTORY_EVENT
 *
 *		ZFS_EV_POOL_NAME	DATA_TYPE_STRING
 *		ZFS_EV_POOL_GUID	DATA_TYPE_UINT64
 *		ZFS_EV_HIST_TIME	DATA_TYPE_UINT64	(optional)
 *		ZFS_EV_HIST_CMD		DATA_TYPE_STRING	(optional)
 *		ZFS_EV_HIST_WHO		DATA_TYPE_UINT64	(optional)
 *		ZFS_EV_HIST_ZONE	DATA_TYPE_STRING	(optional)
 *		ZFS_EV_HIST_HOST	DATA_TYPE_STRING	(optional)
 *		ZFS_EV_HIST_TXG		DATA_TYPE_UINT64	(optional)
 *		ZFS_EV_HIST_INT_EVENT	DATA_TYPE_UINT64	(optional)
 *		ZFS_EV_HIST_INT_STR	DATA_TYPE_STRING	(optional)
 *		ZFS_EV_HIST_INT_NAME	DATA_TYPE_STRING	(optional)
 *		ZFS_EV_HIST_IOCTL	DATA_TYPE_STRING	(optional)
 *		ZFS_EV_HIST_DSNAME	DATA_TYPE_STRING	(optional)
 *		ZFS_EV_HIST_DSID	DATA_TYPE_UINT64	(optional)
 *
 * The ZFS_EV_HIST_* members will correspond to the ZPOOL_HIST_* members in the
 * history log nvlist.  The keynames will be free of any spaces or other
 * characters that could be potentially unexpected to consumers of the
 * sysevents.
 */
#define	ZFS_EV_POOL_NAME	"pool_name"
#define	ZFS_EV_POOL_GUID	"pool_guid"
#define	ZFS_EV_VDEV_PATH	"vdev_path"
#define	ZFS_EV_VDEV_GUID	"vdev_guid"
#define	ZFS_EV_HIST_TIME	"history_time"
#define	ZFS_EV_HIST_CMD		"history_command"
#define	ZFS_EV_HIST_WHO		"history_who"
#define	ZFS_EV_HIST_ZONE	"history_zone"
#define	ZFS_EV_HIST_HOST	"history_hostname"
#define	ZFS_EV_HIST_TXG		"history_txg"
#define	ZFS_EV_HIST_INT_EVENT	"history_internal_event"
#define	ZFS_EV_HIST_INT_STR	"history_internal_str"
#define	ZFS_EV_HIST_INT_NAME	"history_internal_name"
#define	ZFS_EV_HIST_IOCTL	"history_ioctl"
#define	ZFS_EV_HIST_DSNAME	"history_dsname"
#define	ZFS_EV_HIST_DSID	"history_dsid"
#define	ZFS_EV_RESILVER_TYPE	"resilver_type"


/*
 * We currently support block sizes from 512 bytes to 16MB.
 * The benefits of larger blocks, and thus larger IO, need to be weighed
 * against the cost of COWing a giant block to modify one byte, and the
 * large latency of reading or writing a large block.
 *
 * Note that although blocks up to 16MB are supported, the recordsize
 * property can not be set larger than zfs_max_recordsize (default 1MB).
 * See the comment near zfs_max_recordsize in dsl_dataset.c for details.
 *
 * Note that although the LSIZE field of the blkptr_t can store sizes up
 * to 32MB, the dnode's dn_datablkszsec can only store sizes up to
 * 32MB - 512 bytes.  Therefore, we limit SPA_MAXBLOCKSIZE to 16MB.
 */
#define	SPA_MINBLOCKSHIFT	9
#define	SPA_OLD_MAXBLOCKSHIFT	17
#define	SPA_MAXBLOCKSHIFT	24
#define	SPA_MINBLOCKSIZE	(1ULL << SPA_MINBLOCKSHIFT)
#define	SPA_OLD_MAXBLOCKSIZE	(1ULL << SPA_OLD_MAXBLOCKSHIFT)
#define	SPA_MAXBLOCKSIZE	(1ULL << SPA_MAXBLOCKSHIFT)


/* supported encryption algorithms */
enum zio_encrypt {
	ZIO_CRYPT_INHERIT = 0,
	ZIO_CRYPT_ON,
	ZIO_CRYPT_OFF,
	ZIO_CRYPT_AES_128_CCM,
	ZIO_CRYPT_AES_192_CCM,
	ZIO_CRYPT_AES_256_CCM,
	ZIO_CRYPT_AES_128_GCM,
	ZIO_CRYPT_AES_192_GCM,
	ZIO_CRYPT_AES_256_GCM,
	ZIO_CRYPT_FUNCTIONS
};

#define	ZIO_CRYPT_ON_VALUE	ZIO_CRYPT_AES_256_GCM
#define	ZIO_CRYPT_DEFAULT	ZIO_CRYPT_OFF


#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_FS_ZFS_H */
