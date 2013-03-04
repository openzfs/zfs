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
 * Copyright (c) 2012 by Delphix. All rights reserved.
 * Copyright 2011 Nexenta Systems, Inc.  All rights reserved.
 * Copyright (c) 2012, Joyent, Inc. All rights reserved.
 */

/* Portions Copyright 2010 Robert Milkowski */

#ifndef	_SYS_FS_ZFS_H
#define	_SYS_FS_ZFS_H

#include <sys/time.h>

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
	ZFS_TYPE_FILESYSTEM	= 0x1,
	ZFS_TYPE_SNAPSHOT	= 0x2,
	ZFS_TYPE_VOLUME		= 0x4,
	ZFS_TYPE_POOL		= 0x8
} zfs_type_t;

#define	ZFS_TYPE_DATASET	\
	(ZFS_TYPE_FILESYSTEM | ZFS_TYPE_VOLUME | ZFS_TYPE_SNAPSHOT)

#define	ZAP_MAXNAMELEN 256
#define	ZAP_MAXVALUELEN (1024 * 8)
#define	ZAP_OLDMAXVALUELEN 1024

/*
 * Dataset properties are identified by these constants and must be added to
 * the end of this list to ensure that external consumers are not affected
 * by the change. If you make any changes to this list, be sure to update
 * the property table in usr/src/common/zfs/zfs_prop.c.
 */
typedef enum {
	ZFS_PROP_TYPE,
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
	ZFS_PROP_PRIVATE,		/* not exposed to user, temporary */
	ZFS_PROP_ACLINHERIT,
	ZFS_PROP_CREATETXG,		/* not exposed to the user */
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
	ZFS_PROP_OBJSETID,		/* not exposed to the user */
	ZFS_PROP_DEDUP,
	ZFS_PROP_MLSLABEL,
	ZFS_PROP_SYNC,
	ZFS_PROP_REFRATIO,
	ZFS_PROP_WRITTEN,
	ZFS_PROP_CLONES,
	ZFS_PROP_SNAPDEV,
	ZFS_NUM_PROPS
} zfs_prop_t;

typedef enum {
	ZFS_PROP_USERUSED,
	ZFS_PROP_USERQUOTA,
	ZFS_PROP_GROUPUSED,
	ZFS_PROP_GROUPQUOTA,
	ZFS_NUM_USERQUOTA_PROPS
} zfs_userquota_prop_t;

extern const char *zfs_userquota_prop_prefixes[ZFS_NUM_USERQUOTA_PROPS];

/*
 * Pool properties are identified by these constants and must be added to the
 * end of this list to ensure that external consumers are not affected
 * by the change. If you make any changes to this list, be sure to update
 * the property table in usr/src/common/zfs/zpool_prop.c.
 */
typedef enum {
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
	ZPOOL_NUM_PROPS
} zpool_prop_t;

/* Small enough to not hog a whole line of printout in zpool(1M). */
#define	ZPROP_MAX_COMMENT	32

#define	ZPROP_CONT		-2
#define	ZPROP_INVAL		-1

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
 * Dataset property functions shared between libzfs and kernel.
 */
const char *zfs_prop_default_string(zfs_prop_t);
uint64_t zfs_prop_default_numeric(zfs_prop_t);
boolean_t zfs_prop_readonly(zfs_prop_t);
boolean_t zfs_prop_inheritable(zfs_prop_t);
boolean_t zfs_prop_setonce(zfs_prop_t);
const char *zfs_prop_to_name(zfs_prop_t);
zfs_prop_t zfs_name_to_prop(const char *);
boolean_t zfs_prop_user(const char *);
boolean_t zfs_prop_userquota(const char *);
boolean_t zfs_prop_written(const char *);
int zfs_prop_index_to_string(zfs_prop_t, uint64_t, const char **);
int zfs_prop_string_to_index(zfs_prop_t, const char *, uint64_t *);
uint64_t zfs_prop_random_value(zfs_prop_t, uint64_t seed);
boolean_t zfs_prop_valid_for_type(int, zfs_type_t);

/*
 * Pool property functions shared between libzfs and kernel.
 */
zpool_prop_t zpool_name_to_prop(const char *);
const char *zpool_prop_to_name(zpool_prop_t);
const char *zpool_prop_default_string(zpool_prop_t);
uint64_t zpool_prop_default_numeric(zpool_prop_t);
boolean_t zpool_prop_readonly(zpool_prop_t);
boolean_t zpool_prop_feature(const char *);
boolean_t zpool_prop_unsupported(const char *);
int zpool_prop_index_to_string(zpool_prop_t, uint64_t, const char **);
int zpool_prop_string_to_index(zpool_prop_t, const char *, uint64_t *);
uint64_t zpool_prop_random_value(zpool_prop_t, uint64_t seed);

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
 * When bumping up SPA_VERSION, make sure GRUB ZFS understands the on-disk
 * format change. Go to usr/src/grub/grub-0.97/stage2/{zfs-include/, fsys_zfs*},
 * and do the appropriate changes.  Also bump the version number in
 * usr/src/grub/capability.
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
 *
 * When changing, be sure to teach GRUB how to read the new format!
 * See usr/src/grub/grub-0.97/stage2/{zfs-include/,fsys_zfs*}
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

/* Rewind request information */
#define	ZPOOL_NO_REWIND		1  /* No policy - default behavior */
#define	ZPOOL_NEVER_REWIND	2  /* Do not search for best txg or rewind */
#define	ZPOOL_TRY_REWIND	4  /* Search for best txg, but do not rewind */
#define	ZPOOL_DO_REWIND		8  /* Rewind to best txg w/in deferred frees */
#define	ZPOOL_EXTREME_REWIND	16 /* Allow extreme measures to find best txg */
#define	ZPOOL_REWIND_MASK	28 /* All the possible rewind bits */
#define	ZPOOL_REWIND_POLICIES	31 /* All the possible policy bits */

typedef struct zpool_rewind_policy {
	uint32_t	zrp_request;	/* rewind behavior requested */
	uint64_t	zrp_maxmeta;	/* max acceptable meta-data errors */
	uint64_t	zrp_maxdata;	/* max acceptable data errors */
	uint64_t	zrp_txg;	/* specific txg to load */
} zpool_rewind_policy_t;

/*
 * The following are configuration names used in the nvlist describing a pool's
 * configuration.
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
#define	ZPOOL_CONFIG_PATH		"path"
#define	ZPOOL_CONFIG_DEVID		"devid"
#define	ZPOOL_CONFIG_METASLAB_ARRAY	"metaslab_array"
#define	ZPOOL_CONFIG_METASLAB_SHIFT	"metaslab_shift"
#define	ZPOOL_CONFIG_ASHIFT		"ashift"
#define	ZPOOL_CONFIG_ASIZE		"asize"
#define	ZPOOL_CONFIG_DTL		"DTL"
#define	ZPOOL_CONFIG_SCAN_STATS		"scan_stats"	/* not stored on disk */
#define	ZPOOL_CONFIG_VDEV_STATS		"vdev_stats"	/* not stored on disk */
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
#define	ZPOOL_CONFIG_REMOVING		"removing"
#define	ZPOOL_CONFIG_RESILVERING	"resilvering"
#define	ZPOOL_CONFIG_COMMENT		"comment"
#define	ZPOOL_CONFIG_SUSPENDED		"suspended"	/* not stored on disk */
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

/* Rewind policy parameters */
#define	ZPOOL_REWIND_POLICY		"rewind-policy"
#define	ZPOOL_REWIND_REQUEST		"rewind-request"
#define	ZPOOL_REWIND_REQUEST_TXG	"rewind-request-txg"
#define	ZPOOL_REWIND_META_THRESH	"rewind-meta-thresh"
#define	ZPOOL_REWIND_DATA_THRESH	"rewind-data-thresh"

/* Rewind data discovered */
#define	ZPOOL_CONFIG_LOAD_TIME		"rewind_txg_ts"
#define	ZPOOL_CONFIG_LOAD_DATA_ERRORS	"verify_data_errors"
#define	ZPOOL_CONFIG_REWIND_TIME	"seconds_of_rewind"

#define	VDEV_TYPE_ROOT			"root"
#define	VDEV_TYPE_MIRROR		"mirror"
#define	VDEV_TYPE_REPLACING		"replacing"
#define	VDEV_TYPE_RAIDZ			"raidz"
#define	VDEV_TYPE_DISK			"disk"
#define	VDEV_TYPE_FILE			"file"
#define	VDEV_TYPE_MISSING		"missing"
#define	VDEV_TYPE_HOLE			"hole"
#define	VDEV_TYPE_SPARE			"spare"
#define	VDEV_TYPE_LOG			"log"
#define	VDEV_TYPE_L2CACHE		"l2cache"

/*
 * This is needed in userland to report the minimum necessary device size.
 */
#define	SPA_MINDEVSIZE		(64ULL << 20)

/*
 * The location of the pool configuration repository, shared between kernel and
 * userland.
 */
#define	ZPOOL_CACHE		"/etc/zfs/zpool.cache"

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
	VDEV_AUX_EXTERNAL,	/* external diagnosis			*/
	VDEV_AUX_SPLIT_POOL	/* vdev was split off into another pool	*/
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
 * Scan Functions.
 */
typedef enum pool_scan_func {
	POOL_SCAN_NONE,
	POOL_SCAN_SCRUB,
	POOL_SCAN_RESILVER,
	POOL_SCAN_FUNCS
} pool_scan_func_t;

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
	uint64_t	pss_examined;	/* total examined bytes	*/
	uint64_t	pss_to_process; /* total bytes to process */
	uint64_t	pss_processed;	/* total processed bytes */
	uint64_t	pss_errors;	/* scan errors	*/

	/* values not stored on disk */
	uint64_t	pss_pass_exam;	/* examined bytes per scan pass */
	uint64_t	pss_pass_start;	/* start time of a scan pass */
} pool_scan_stat_t;

typedef enum dsl_scan_state {
	DSS_NONE,
	DSS_SCANNING,
	DSS_FINISHED,
	DSS_CANCELED,
	DSS_NUM_STATES
} dsl_scan_state_t;


/*
 * Vdev statistics.  Note: all fields should be 64-bit because this
 * is passed between kernel and userland as an nvlist uint64 array.
 */
typedef struct vdev_stat {
	hrtime_t	vs_timestamp;		/* time since vdev load	*/
	uint64_t	vs_state;		/* vdev state		*/
	uint64_t	vs_aux;			/* see vdev_aux_t	*/
	uint64_t	vs_alloc;		/* space allocated	*/
	uint64_t	vs_space;		/* total capacity	*/
	uint64_t	vs_dspace;		/* deflated capacity	*/
	uint64_t	vs_rsize;		/* replaceable dev size */
	uint64_t	vs_esize;		/* expandable dev size */
	uint64_t	vs_ops[ZIO_TYPES];	/* operation count	*/
	uint64_t	vs_bytes[ZIO_TYPES];	/* bytes read/written	*/
	uint64_t	vs_read_errors;		/* read errors		*/
	uint64_t	vs_write_errors;	/* write errors		*/
	uint64_t	vs_checksum_errors;	/* checksum errors	*/
	uint64_t	vs_self_healed;		/* self-healed bytes	*/
	uint64_t	vs_scan_removing;	/* removing?	*/
	uint64_t	vs_scan_processed;	/* scan processed bytes	*/
} vdev_stat_t;

/*
 * DDT statistics.  Note: all fields should be 64-bit because this
 * is passed between kernel and userland as an nvlist uint64 array.
 */
typedef struct ddt_object {
	uint64_t	ddo_count;	/* number of elments in ddt 	*/
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

/* general zvol path */
#define	ZVOL_DIR	"/dev"

#define	ZVOL_MAJOR		230
#define	ZVOL_MINOR_BITS		4
#define	ZVOL_MINOR_MASK		((1U << ZVOL_MINOR_BITS) - 1)
#define	ZVOL_MINORS		(1 << 4)
#define	ZVOL_DEV_NAME		"zd"

#define	ZVOL_PROP_NAME		"name"
#define	ZVOL_DEFAULT_BLOCKSIZE	8192

/*
 * /dev/zfs ioctl numbers.
 */
#define	ZFS_IOC		('Z' << 8)

typedef enum zfs_ioc {
	ZFS_IOC_POOL_CREATE = ZFS_IOC,
	ZFS_IOC_POOL_DESTROY,
	ZFS_IOC_POOL_IMPORT,
	ZFS_IOC_POOL_EXPORT,
	ZFS_IOC_POOL_CONFIGS,
	ZFS_IOC_POOL_STATS,
	ZFS_IOC_POOL_TRYIMPORT,
	ZFS_IOC_POOL_SCAN,
	ZFS_IOC_POOL_FREEZE,
	ZFS_IOC_POOL_UPGRADE,
	ZFS_IOC_POOL_GET_HISTORY,
	ZFS_IOC_VDEV_ADD,
	ZFS_IOC_VDEV_REMOVE,
	ZFS_IOC_VDEV_SET_STATE,
	ZFS_IOC_VDEV_ATTACH,
	ZFS_IOC_VDEV_DETACH,
	ZFS_IOC_VDEV_SETPATH,
	ZFS_IOC_VDEV_SETFRU,
	ZFS_IOC_OBJSET_STATS,
	ZFS_IOC_OBJSET_ZPLPROPS,
	ZFS_IOC_DATASET_LIST_NEXT,
	ZFS_IOC_SNAPSHOT_LIST_NEXT,
	ZFS_IOC_SET_PROP,
	ZFS_IOC_CREATE_MINOR,
	ZFS_IOC_REMOVE_MINOR,
	ZFS_IOC_CREATE,
	ZFS_IOC_DESTROY,
	ZFS_IOC_ROLLBACK,
	ZFS_IOC_RENAME,
	ZFS_IOC_RECV,
	ZFS_IOC_SEND,
	ZFS_IOC_INJECT_FAULT,
	ZFS_IOC_CLEAR_FAULT,
	ZFS_IOC_INJECT_LIST_NEXT,
	ZFS_IOC_ERROR_LOG,
	ZFS_IOC_CLEAR,
	ZFS_IOC_PROMOTE,
	ZFS_IOC_DESTROY_SNAPS_NVL,
	ZFS_IOC_SNAPSHOT,
	ZFS_IOC_DSOBJ_TO_DSNAME,
	ZFS_IOC_OBJ_TO_PATH,
	ZFS_IOC_POOL_SET_PROPS,
	ZFS_IOC_POOL_GET_PROPS,
	ZFS_IOC_SET_FSACL,
	ZFS_IOC_GET_FSACL,
	ZFS_IOC_SHARE,
	ZFS_IOC_INHERIT_PROP,
	ZFS_IOC_SMB_ACL,
	ZFS_IOC_USERSPACE_ONE,
	ZFS_IOC_USERSPACE_MANY,
	ZFS_IOC_USERSPACE_UPGRADE,
	ZFS_IOC_HOLD,
	ZFS_IOC_RELEASE,
	ZFS_IOC_GET_HOLDS,
	ZFS_IOC_OBJSET_RECVD_PROPS,
	ZFS_IOC_VDEV_SPLIT,
	ZFS_IOC_NEXT_OBJ,
	ZFS_IOC_DIFF,
	ZFS_IOC_TMP_SNAPSHOT,
	ZFS_IOC_OBJ_TO_STATS,
	ZFS_IOC_EVENTS_NEXT,
	ZFS_IOC_EVENTS_CLEAR,
	ZFS_IOC_POOL_REGUID,
	ZFS_IOC_SPACE_WRITTEN,
	ZFS_IOC_SPACE_SNAPS,
	ZFS_IOC_POOL_REOPEN,
	ZFS_IOC_SEND_PROGRESS,
} zfs_ioc_t;

/*
 * zvol ioctl to get dataset name
 */
#define BLKZNAME		_IOR(0x12,125,char[ZFS_MAXNAMELEN])

/*
 * Internal SPA load state.  Used by FMA diagnosis engine.
 */
typedef enum {
	SPA_LOAD_NONE,		/* no load in progress	*/
	SPA_LOAD_OPEN,		/* normal open		*/
	SPA_LOAD_IMPORT,	/* import in progress	*/
	SPA_LOAD_TRYIMPORT,	/* tryimport in progress */
	SPA_LOAD_RECOVER,	/* recovery requested	*/
	SPA_LOAD_ERROR		/* load failed		*/
} spa_load_state_t;

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

/*
 * Sysevent payload members.  ZFS will generate the following sysevents with the
 * given payloads:
 *
 *	ESC_ZFS_RESILVER_START
 *	ESC_ZFS_RESILVER_END
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
 */
#define	ZFS_EV_POOL_NAME	"pool_name"
#define	ZFS_EV_POOL_GUID	"pool_guid"
#define	ZFS_EV_VDEV_PATH	"vdev_path"
#define	ZFS_EV_VDEV_GUID	"vdev_guid"

/*
 * Note: This is encoded on-disk, so new events must be added to the
 * end, and unused events can not be removed.  Be sure to edit
 * libzfs_pool.c: hist_event_table[].
 */
typedef enum history_internal_events {
	LOG_NO_EVENT = 0,
	LOG_POOL_CREATE,
	LOG_POOL_VDEV_ADD,
	LOG_POOL_REMOVE,
	LOG_POOL_DESTROY,
	LOG_POOL_EXPORT,
	LOG_POOL_IMPORT,
	LOG_POOL_VDEV_ATTACH,
	LOG_POOL_VDEV_REPLACE,
	LOG_POOL_VDEV_DETACH,
	LOG_POOL_VDEV_ONLINE,
	LOG_POOL_VDEV_OFFLINE,
	LOG_POOL_UPGRADE,
	LOG_POOL_CLEAR,
	LOG_POOL_SCAN,
	LOG_POOL_PROPSET,
	LOG_DS_CREATE,
	LOG_DS_CLONE,
	LOG_DS_DESTROY,
	LOG_DS_DESTROY_BEGIN,
	LOG_DS_INHERIT,
	LOG_DS_PROPSET,
	LOG_DS_QUOTA,
	LOG_DS_PERM_UPDATE,
	LOG_DS_PERM_REMOVE,
	LOG_DS_PERM_WHO_REMOVE,
	LOG_DS_PROMOTE,
	LOG_DS_RECEIVE,
	LOG_DS_RENAME,
	LOG_DS_RESERVATION,
	LOG_DS_REPLAY_INC_SYNC,
	LOG_DS_REPLAY_FULL_SYNC,
	LOG_DS_ROLLBACK,
	LOG_DS_SNAPSHOT,
	LOG_DS_UPGRADE,
	LOG_DS_REFQUOTA,
	LOG_DS_REFRESERV,
	LOG_POOL_SCAN_DONE,
	LOG_DS_USER_HOLD,
	LOG_DS_USER_RELEASE,
	LOG_POOL_SPLIT,
	LOG_POOL_GUID_CHANGE,
	LOG_END
} history_internal_events_t;

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_FS_ZFS_H */
