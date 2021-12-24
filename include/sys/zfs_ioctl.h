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
 * Copyright (c) 2012, 2020 by Delphix. All rights reserved.
 * Copyright 2016 RackTop Systems.
 * Copyright (c) 2017, Intel Corporation.
 */

#ifndef	_SYS_ZFS_IOCTL_H
#define	_SYS_ZFS_IOCTL_H

#include <sys/cred.h>
#include <sys/dmu.h>
#include <sys/zio.h>
#include <sys/dsl_deleg.h>
#include <sys/spa.h>
#include <sys/zfs_stat.h>

#ifdef _KERNEL
#include <sys/nvpair.h>
#endif	/* _KERNEL */

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * The structures in this file are passed between userland and the
 * kernel.  Userland may be running a 32-bit process, while the kernel
 * is 64-bit.  Therefore, these structures need to compile the same in
 * 32-bit and 64-bit.  This means not using type "long", and adding
 * explicit padding so that the 32-bit structure will not be packed more
 * tightly than the 64-bit structure (which requires 64-bit alignment).
 */

/*
 * Property values for snapdir
 */
#define	ZFS_SNAPDIR_HIDDEN		0
#define	ZFS_SNAPDIR_VISIBLE		1

/*
 * Property values for snapdev
 */
#define	ZFS_SNAPDEV_HIDDEN		0
#define	ZFS_SNAPDEV_VISIBLE		1
/*
 * Property values for acltype
 */
#define	ZFS_ACLTYPE_OFF			0
#define	ZFS_ACLTYPE_POSIX		1
#define	ZFS_ACLTYPE_NFSV4		2

/*
 * Field manipulation macros for the drr_versioninfo field of the
 * send stream header.
 */

/*
 * Header types for zfs send streams.
 */
typedef enum drr_headertype {
	DMU_SUBSTREAM = 0x1,
	DMU_COMPOUNDSTREAM = 0x2
} drr_headertype_t;

#define	DMU_GET_STREAM_HDRTYPE(vi)	BF64_GET((vi), 0, 2)
#define	DMU_SET_STREAM_HDRTYPE(vi, x)	BF64_SET((vi), 0, 2, x)

#define	DMU_GET_FEATUREFLAGS(vi)	BF64_GET((vi), 2, 30)
#define	DMU_SET_FEATUREFLAGS(vi, x)	BF64_SET((vi), 2, 30, x)

/*
 * Feature flags for zfs send streams (flags in drr_versioninfo)
 */

#define	DMU_BACKUP_FEATURE_DEDUP		(1 << 0)
#define	DMU_BACKUP_FEATURE_DEDUPPROPS		(1 << 1)
#define	DMU_BACKUP_FEATURE_SA_SPILL		(1 << 2)
/* flags #3 - #15 are reserved for incompatible closed-source implementations */
#define	DMU_BACKUP_FEATURE_EMBED_DATA		(1 << 16)
#define	DMU_BACKUP_FEATURE_LZ4			(1 << 17)
/* flag #18 is reserved for a Delphix feature */
#define	DMU_BACKUP_FEATURE_LARGE_BLOCKS		(1 << 19)
#define	DMU_BACKUP_FEATURE_RESUMING		(1 << 20)
#define	DMU_BACKUP_FEATURE_REDACTED		(1 << 21)
#define	DMU_BACKUP_FEATURE_COMPRESSED		(1 << 22)
#define	DMU_BACKUP_FEATURE_LARGE_DNODE		(1 << 23)
#define	DMU_BACKUP_FEATURE_RAW			(1 << 24)
#define	DMU_BACKUP_FEATURE_ZSTD			(1 << 25)
#define	DMU_BACKUP_FEATURE_HOLDS		(1 << 26)
/*
 * The SWITCH_TO_LARGE_BLOCKS feature indicates that we can receive
 * incremental LARGE_BLOCKS streams (those with WRITE records of >128KB) even
 * if the previous send did not use LARGE_BLOCKS, and thus its large blocks
 * were split into multiple 128KB WRITE records.  (See
 * flush_write_batch_impl() and receive_object()).  Older software that does
 * not support this flag may encounter a bug when switching to large blocks,
 * which causes files to incorrectly be zeroed.
 *
 * This flag is currently not set on any send streams.  In the future, we
 * intend for incremental send streams of snapshots that have large blocks to
 * use LARGE_BLOCKS by default, and these streams will also have the
 * SWITCH_TO_LARGE_BLOCKS feature set. This ensures that streams from the
 * default use of "zfs send" won't encounter the bug mentioned above.
 */
#define	DMU_BACKUP_FEATURE_SWITCH_TO_LARGE_BLOCKS (1 << 27)
#define	DMU_BACKUP_FEATURE_BLAKE3		(1 << 28)

/*
 * Mask of all supported backup features
 */
#define	DMU_BACKUP_FEATURE_MASK	(DMU_BACKUP_FEATURE_SA_SPILL | \
    DMU_BACKUP_FEATURE_EMBED_DATA | DMU_BACKUP_FEATURE_LZ4 | \
    DMU_BACKUP_FEATURE_RESUMING | DMU_BACKUP_FEATURE_LARGE_BLOCKS | \
    DMU_BACKUP_FEATURE_COMPRESSED | DMU_BACKUP_FEATURE_LARGE_DNODE | \
    DMU_BACKUP_FEATURE_RAW | DMU_BACKUP_FEATURE_HOLDS | \
    DMU_BACKUP_FEATURE_REDACTED | DMU_BACKUP_FEATURE_SWITCH_TO_LARGE_BLOCKS | \
    DMU_BACKUP_FEATURE_ZSTD | DMU_BACKUP_FEATURE_BLAKE3)

/* Are all features in the given flag word currently supported? */
#define	DMU_STREAM_SUPPORTED(x)	(!((x) & ~DMU_BACKUP_FEATURE_MASK))

typedef enum dmu_send_resume_token_version {
	ZFS_SEND_RESUME_TOKEN_VERSION = 1
} dmu_send_resume_token_version_t;

/*
 * The drr_versioninfo field of the dmu_replay_record has the
 * following layout:
 *
 *	64	56	48	40	32	24	16	8	0
 *	+-------+-------+-------+-------+-------+-------+-------+-------+
 *	|		reserved	|        feature-flags	    |C|S|
 *	+-------+-------+-------+-------+-------+-------+-------+-------+
 *
 * The low order two bits indicate the header type: SUBSTREAM (0x1)
 * or COMPOUNDSTREAM (0x2).  Using two bits for this is historical:
 * this field used to be a version number, where the two version types
 * were 1 and 2.  Using two bits for this allows earlier versions of
 * the code to be able to recognize send streams that don't use any
 * of the features indicated by feature flags.
 */

#define	DMU_BACKUP_MAGIC 0x2F5bacbacULL

/*
 * Send stream flags.  Bits 24-31 are reserved for vendor-specific
 * implementations and should not be used.
 */
#define	DRR_FLAG_CLONE		(1<<0)
#define	DRR_FLAG_CI_DATA	(1<<1)
/*
 * This send stream, if it is a full send, includes the FREE and FREEOBJECT
 * records that are created by the sending process.  This means that the send
 * stream can be received as a clone, even though it is not an incremental.
 * This is not implemented as a feature flag, because the receiving side does
 * not need to have implemented it to receive this stream; it is fully backwards
 * compatible.  We need a flag, though, because full send streams without it
 * cannot necessarily be received as a clone correctly.
 */
#define	DRR_FLAG_FREERECORDS	(1<<2)
/*
 * When DRR_FLAG_SPILL_BLOCK is set it indicates the DRR_OBJECT_SPILL
 * and DRR_SPILL_UNMODIFIED flags are meaningful in the send stream.
 *
 * When DRR_FLAG_SPILL_BLOCK is set, DRR_OBJECT records will have
 * DRR_OBJECT_SPILL set if and only if they should have a spill block
 * (either an existing one, or a new one in the send stream).  When clear
 * the object does not have a spill block and any existing spill block
 * should be freed.
 *
 * Similarly, when DRR_FLAG_SPILL_BLOCK is set, DRR_SPILL records will
 * have DRR_SPILL_UNMODIFIED set if and only if they were included for
 * backward compatibility purposes, and can be safely ignored by new versions
 * of zfs receive.  Previous versions of ZFS which do not understand the
 * DRR_FLAG_SPILL_BLOCK will process this record and recreate any missing
 * spill blocks.
 */
#define	DRR_FLAG_SPILL_BLOCK	(1<<3)

/*
 * flags in the drr_flags field in the DRR_WRITE, DRR_SPILL, DRR_OBJECT,
 * DRR_WRITE_BYREF, and DRR_OBJECT_RANGE blocks
 */
#define	DRR_CHECKSUM_DEDUP	(1<<0) /* not used for SPILL records */
#define	DRR_RAW_BYTESWAP	(1<<1)
#define	DRR_OBJECT_SPILL	(1<<2) /* OBJECT record has a spill block */
#define	DRR_SPILL_UNMODIFIED	(1<<2) /* SPILL record for unmodified block */

#define	DRR_IS_DEDUP_CAPABLE(flags)	((flags) & DRR_CHECKSUM_DEDUP)
#define	DRR_IS_RAW_BYTESWAPPED(flags)	((flags) & DRR_RAW_BYTESWAP)
#define	DRR_OBJECT_HAS_SPILL(flags)	((flags) & DRR_OBJECT_SPILL)
#define	DRR_SPILL_IS_UNMODIFIED(flags)	((flags) & DRR_SPILL_UNMODIFIED)

/* deal with compressed drr_write replay records */
#define	DRR_WRITE_COMPRESSED(drrw)	((drrw)->drr_compressiontype != 0)
#define	DRR_WRITE_PAYLOAD_SIZE(drrw) \
	(DRR_WRITE_COMPRESSED(drrw) ? (drrw)->drr_compressed_size : \
	(drrw)->drr_logical_size)
#define	DRR_SPILL_PAYLOAD_SIZE(drrs) \
	((drrs)->drr_compressed_size ? \
	(drrs)->drr_compressed_size : (drrs)->drr_length)
#define	DRR_OBJECT_PAYLOAD_SIZE(drro) \
	((drro)->drr_raw_bonuslen != 0 ? \
	(drro)->drr_raw_bonuslen : P2ROUNDUP((drro)->drr_bonuslen, 8))

/*
 * zfs ioctl command structure
 */

/* Header is used in C++ so can't forward declare untagged struct */
struct drr_begin {
	uint64_t drr_magic;
	uint64_t drr_versioninfo; /* was drr_version */
	uint64_t drr_creation_time;
	dmu_objset_type_t drr_type;
	uint32_t drr_flags;
	uint64_t drr_toguid;
	uint64_t drr_fromguid;
	char drr_toname[MAXNAMELEN];
};

typedef struct dmu_replay_record {
	enum {
		DRR_BEGIN, DRR_OBJECT, DRR_FREEOBJECTS,
		DRR_WRITE, DRR_FREE, DRR_END, DRR_WRITE_BYREF,
		DRR_SPILL, DRR_WRITE_EMBEDDED, DRR_OBJECT_RANGE, DRR_REDACT,
		DRR_NUMTYPES
	} drr_type;
	uint32_t drr_payloadlen;
	union {
		struct drr_begin drr_begin;
		struct drr_end {
			zio_cksum_t drr_checksum;
			uint64_t drr_toguid;
		} drr_end;
		struct drr_object {
			uint64_t drr_object;
			dmu_object_type_t drr_type;
			dmu_object_type_t drr_bonustype;
			uint32_t drr_blksz;
			uint32_t drr_bonuslen;
			uint8_t drr_checksumtype;
			uint8_t drr_compress;
			uint8_t drr_dn_slots;
			uint8_t drr_flags;
			uint32_t drr_raw_bonuslen;
			uint64_t drr_toguid;
			/* only (possibly) nonzero for raw streams */
			uint8_t drr_indblkshift;
			uint8_t drr_nlevels;
			uint8_t drr_nblkptr;
			uint8_t drr_pad[5];
			uint64_t drr_maxblkid;
			/* bonus content follows */
		} drr_object;
		struct drr_freeobjects {
			uint64_t drr_firstobj;
			uint64_t drr_numobjs;
			uint64_t drr_toguid;
		} drr_freeobjects;
		struct drr_write {
			uint64_t drr_object;
			dmu_object_type_t drr_type;
			uint32_t drr_pad;
			uint64_t drr_offset;
			uint64_t drr_logical_size;
			uint64_t drr_toguid;
			uint8_t drr_checksumtype;
			uint8_t drr_flags;
			uint8_t drr_compressiontype;
			uint8_t drr_pad2[5];
			/* deduplication key */
			ddt_key_t drr_key;
			/* only nonzero if drr_compressiontype is not 0 */
			uint64_t drr_compressed_size;
			/* only nonzero for raw streams */
			uint8_t drr_salt[ZIO_DATA_SALT_LEN];
			uint8_t drr_iv[ZIO_DATA_IV_LEN];
			uint8_t drr_mac[ZIO_DATA_MAC_LEN];
			/* content follows */
		} drr_write;
		struct drr_free {
			uint64_t drr_object;
			uint64_t drr_offset;
			uint64_t drr_length;
			uint64_t drr_toguid;
		} drr_free;
		struct drr_write_byref {
			/* where to put the data */
			uint64_t drr_object;
			uint64_t drr_offset;
			uint64_t drr_length;
			uint64_t drr_toguid;
			/* where to find the prior copy of the data */
			uint64_t drr_refguid;
			uint64_t drr_refobject;
			uint64_t drr_refoffset;
			/* properties of the data */
			uint8_t drr_checksumtype;
			uint8_t drr_flags;
			uint8_t drr_pad2[6];
			ddt_key_t drr_key; /* deduplication key */
		} drr_write_byref;
		struct drr_spill {
			uint64_t drr_object;
			uint64_t drr_length;
			uint64_t drr_toguid;
			uint8_t drr_flags;
			uint8_t drr_compressiontype;
			uint8_t drr_pad[6];
			/* only nonzero for raw streams */
			uint64_t drr_compressed_size;
			uint8_t drr_salt[ZIO_DATA_SALT_LEN];
			uint8_t drr_iv[ZIO_DATA_IV_LEN];
			uint8_t drr_mac[ZIO_DATA_MAC_LEN];
			dmu_object_type_t drr_type;
			/* spill data follows */
		} drr_spill;
		struct drr_write_embedded {
			uint64_t drr_object;
			uint64_t drr_offset;
			/* logical length, should equal blocksize */
			uint64_t drr_length;
			uint64_t drr_toguid;
			uint8_t drr_compression;
			uint8_t drr_etype;
			uint8_t drr_pad[6];
			uint32_t drr_lsize; /* uncompressed size of payload */
			uint32_t drr_psize; /* compr. (real) size of payload */
			/* (possibly compressed) content follows */
		} drr_write_embedded;
		struct drr_object_range {
			uint64_t drr_firstobj;
			uint64_t drr_numslots;
			uint64_t drr_toguid;
			uint8_t drr_salt[ZIO_DATA_SALT_LEN];
			uint8_t drr_iv[ZIO_DATA_IV_LEN];
			uint8_t drr_mac[ZIO_DATA_MAC_LEN];
			uint8_t drr_flags;
			uint8_t drr_pad[3];
		} drr_object_range;
		struct drr_redact {
			uint64_t drr_object;
			uint64_t drr_offset;
			uint64_t drr_length;
			uint64_t drr_toguid;
		} drr_redact;

		/*
		 * Note: drr_checksum is overlaid with all record types
		 * except DRR_BEGIN.  Therefore its (non-pad) members
		 * must not overlap with members from the other structs.
		 * We accomplish this by putting its members at the very
		 * end of the struct.
		 */
		struct drr_checksum {
			uint64_t drr_pad[34];
			/*
			 * fletcher-4 checksum of everything preceding the
			 * checksum.
			 */
			zio_cksum_t drr_checksum;
		} drr_checksum;
	} drr_u;
} dmu_replay_record_t;

/* diff record range types */
typedef enum diff_type {
	DDR_NONE = 0x1,
	DDR_INUSE = 0x2,
	DDR_FREE = 0x4
} diff_type_t;

/*
 * The diff reports back ranges of free or in-use objects.
 */
typedef struct dmu_diff_record {
	uint64_t ddr_type;
	uint64_t ddr_first;
	uint64_t ddr_last;
} dmu_diff_record_t;

typedef struct zinject_record {
	uint64_t	zi_objset;
	uint64_t	zi_object;
	uint64_t	zi_start;
	uint64_t	zi_end;
	uint64_t	zi_guid;
	uint32_t	zi_level;
	uint32_t	zi_error;
	uint64_t	zi_type;
	uint32_t	zi_freq;
	uint32_t	zi_failfast;
	char		zi_func[MAXNAMELEN];
	uint32_t	zi_iotype;
	int32_t		zi_duration;
	uint64_t	zi_timer;
	uint64_t	zi_nlanes;
	uint32_t	zi_cmd;
	uint32_t	zi_dvas;
} zinject_record_t;

#define	ZINJECT_NULL		0x1
#define	ZINJECT_FLUSH_ARC	0x2
#define	ZINJECT_UNLOAD_SPA	0x4
#define	ZINJECT_CALC_RANGE	0x8

#define	ZEVENT_NONE		0x0
#define	ZEVENT_NONBLOCK		0x1
#define	ZEVENT_SIZE		1024

#define	ZEVENT_SEEK_START	0
#define	ZEVENT_SEEK_END		UINT64_MAX

/* scaled frequency ranges */
#define	ZI_PERCENTAGE_MIN	4294UL
#define	ZI_PERCENTAGE_MAX	UINT32_MAX

#define	ZI_NO_DVA		(-1)

typedef enum zinject_type {
	ZINJECT_UNINITIALIZED,
	ZINJECT_DATA_FAULT,
	ZINJECT_DEVICE_FAULT,
	ZINJECT_LABEL_FAULT,
	ZINJECT_IGNORED_WRITES,
	ZINJECT_PANIC,
	ZINJECT_DELAY_IO,
	ZINJECT_DECRYPT_FAULT,
} zinject_type_t;

typedef struct zfs_share {
	uint64_t	z_exportdata;
	uint64_t	z_sharedata;
	uint64_t	z_sharetype;	/* 0 = share, 1 = unshare */
	uint64_t	z_sharemax;  /* max length of share string */
} zfs_share_t;

/*
 * ZFS file systems may behave the usual, POSIX-compliant way, where
 * name lookups are case-sensitive.  They may also be set up so that
 * all the name lookups are case-insensitive, or so that only some
 * lookups, the ones that set an FIGNORECASE flag, are case-insensitive.
 */
typedef enum zfs_case {
	ZFS_CASE_SENSITIVE,
	ZFS_CASE_INSENSITIVE,
	ZFS_CASE_MIXED
} zfs_case_t;

/*
 * Note: this struct must have the same layout in 32-bit and 64-bit, so
 * that 32-bit processes (like /sbin/zfs) can pass it to the 64-bit
 * kernel.  Therefore, we add padding to it so that no "hidden" padding
 * is automatically added on 64-bit (but not on 32-bit).
 */
typedef struct zfs_cmd {
	char		zc_name[MAXPATHLEN];	/* name of pool or dataset */
	uint64_t	zc_nvlist_src;		/* really (char *) */
	uint64_t	zc_nvlist_src_size;
	uint64_t	zc_nvlist_dst;		/* really (char *) */
	uint64_t	zc_nvlist_dst_size;
	boolean_t	zc_nvlist_dst_filled;	/* put an nvlist in dst? */
	int		zc_pad2;

	/*
	 * The following members are for legacy ioctls which haven't been
	 * converted to the new method.
	 */
	uint64_t	zc_history;		/* really (char *) */
	char		zc_value[MAXPATHLEN * 2];
	char		zc_string[MAXNAMELEN];
	uint64_t	zc_guid;
	uint64_t	zc_nvlist_conf;		/* really (char *) */
	uint64_t	zc_nvlist_conf_size;
	uint64_t	zc_cookie;
	uint64_t	zc_objset_type;
	uint64_t	zc_perm_action;
	uint64_t	zc_history_len;
	uint64_t	zc_history_offset;
	uint64_t	zc_obj;
	uint64_t	zc_iflags;		/* internal to zfs(7fs) */
	zfs_share_t	zc_share;
	dmu_objset_stats_t zc_objset_stats;
	struct drr_begin zc_begin_record;
	zinject_record_t zc_inject_record;
	uint32_t	zc_defer_destroy;
	uint32_t	zc_flags;
	uint64_t	zc_action_handle;
	int		zc_cleanup_fd;
	uint8_t		zc_simple;
	uint8_t		zc_pad[3];		/* alignment */
	uint64_t	zc_sendobj;
	uint64_t	zc_fromobj;
	uint64_t	zc_createtxg;
	zfs_stat_t	zc_stat;
	uint64_t	zc_zoneid;
} zfs_cmd_t;

typedef struct zfs_useracct {
	char zu_domain[256];
	uid_t zu_rid;
	uint32_t zu_pad;
	uint64_t zu_space;
} zfs_useracct_t;

#define	ZFSDEV_MAX_MINOR	(1 << 16)

#define	ZPOOL_EXPORT_AFTER_SPLIT 0x1

#ifdef _KERNEL
struct objset;
struct zfsvfs;

typedef struct zfs_creat {
	nvlist_t	*zct_zplprops;
	nvlist_t	*zct_props;
} zfs_creat_t;

extern int zfs_secpolicy_snapshot_perms(const char *, cred_t *);
extern int zfs_secpolicy_rename_perms(const char *, const char *, cred_t *);
extern int zfs_secpolicy_destroy_perms(const char *, cred_t *);
extern void zfs_unmount_snap(const char *);
extern void zfs_destroy_unmount_origin(const char *);
extern int getzfsvfs_impl(struct objset *, struct zfsvfs **);
extern int getzfsvfs(const char *, struct zfsvfs **);

enum zfsdev_state_type {
	ZST_ONEXIT,
	ZST_ZEVENT,
	ZST_ALL,
};

/*
 * The zfsdev_state_t structure is managed as a singly-linked list
 * from which items are never deleted.  This allows for lock-free
 * reading of the list so long as assignments to the zs_next and
 * reads from zs_minor are performed atomically.  Empty items are
 * indicated by storing -1 into zs_minor.
 */
typedef struct zfsdev_state {
	struct zfsdev_state	*zs_next;	/* next zfsdev_state_t link */
	minor_t			zs_minor;	/* made up minor number */
	void			*zs_onexit;	/* onexit data */
	void			*zs_zevent;	/* zevent data */
} zfsdev_state_t;

extern void *zfsdev_get_state(minor_t minor, enum zfsdev_state_type which);
extern int zfsdev_getminor(zfs_file_t *fp, minor_t *minorp);

extern uint_t zfs_fsyncer_key;
extern uint_t zfs_allow_log_key;

#endif	/* _KERNEL */

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_ZFS_IOCTL_H */
