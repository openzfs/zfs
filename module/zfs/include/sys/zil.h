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
 * Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef	_SYS_ZIL_H
#define	_SYS_ZIL_H

#include <sys/types.h>
#include <sys/spa.h>
#include <sys/zio.h>
#include <sys/dmu.h>

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * Intent log format:
 *
 * Each objset has its own intent log.  The log header (zil_header_t)
 * for objset N's intent log is kept in the Nth object of the SPA's
 * intent_log objset.  The log header points to a chain of log blocks,
 * each of which contains log records (i.e., transactions) followed by
 * a log block trailer (zil_trailer_t).  The format of a log record
 * depends on the record (or transaction) type, but all records begin
 * with a common structure that defines the type, length, and txg.
 */

/*
 * Intent log header - this on disk structure holds fields to manage
 * the log.  All fields are 64 bit to easily handle cross architectures.
 */
typedef struct zil_header {
	uint64_t zh_claim_txg;	/* txg in which log blocks were claimed */
	uint64_t zh_replay_seq;	/* highest replayed sequence number */
	blkptr_t zh_log;	/* log chain */
	uint64_t zh_claim_seq;	/* highest claimed sequence number */
	uint64_t zh_pad[5];
} zil_header_t;

/*
 * Log block trailer - structure at the end of the header and each log block
 *
 * The zit_bt contains a zbt_cksum which for the intent log is
 * the sequence number of this log block. A seq of 0 is invalid.
 * The zbt_cksum is checked by the SPA against the sequence
 * number passed in the blk_cksum field of the blkptr_t
 */
typedef struct zil_trailer {
	uint64_t zit_pad;
	blkptr_t zit_next_blk;	/* next block in chain */
	uint64_t zit_nused;	/* bytes in log block used */
	zio_block_tail_t zit_bt; /* block trailer */
} zil_trailer_t;

#define	ZIL_MIN_BLKSZ	4096ULL
#define	ZIL_MAX_BLKSZ	SPA_MAXBLOCKSIZE
#define	ZIL_BLK_DATA_SZ(lwb)	((lwb)->lwb_sz - sizeof (zil_trailer_t))

/*
 * The words of a log block checksum.
 */
#define	ZIL_ZC_GUID_0	0
#define	ZIL_ZC_GUID_1	1
#define	ZIL_ZC_OBJSET	2
#define	ZIL_ZC_SEQ	3

typedef enum zil_create {
	Z_FILE,
	Z_DIR,
	Z_XATTRDIR,
} zil_create_t;

/*
 * size of xvattr log section.
 * its composed of lr_attr_t + xvattr bitmap + 2 64 bit timestamps
 * for create time and a single 64 bit integer for all of the attributes,
 * and 4 64 bit integers (32 bytes) for the scanstamp.
 *
 */

#define	ZIL_XVAT_SIZE(mapsize) \
	sizeof (lr_attr_t) + (sizeof (uint32_t) * (mapsize - 1)) + \
	(sizeof (uint64_t) * 7)

/*
 * Size of ACL in log.  The ACE data is padded out to properly align
 * on 8 byte boundary.
 */

#define	ZIL_ACE_LENGTH(x)	(roundup(x, sizeof (uint64_t)))

/*
 * Intent log transaction types and record structures
 */
#define	TX_CREATE		1	/* Create file */
#define	TX_MKDIR		2	/* Make directory */
#define	TX_MKXATTR		3	/* Make XATTR directory */
#define	TX_SYMLINK		4	/* Create symbolic link to a file */
#define	TX_REMOVE		5	/* Remove file */
#define	TX_RMDIR		6	/* Remove directory */
#define	TX_LINK			7	/* Create hard link to a file */
#define	TX_RENAME		8	/* Rename a file */
#define	TX_WRITE		9	/* File write */
#define	TX_TRUNCATE		10	/* Truncate a file */
#define	TX_SETATTR		11	/* Set file attributes */
#define	TX_ACL_V0		12	/* Set old formatted ACL */
#define	TX_ACL			13	/* Set ACL */
#define	TX_CREATE_ACL		14	/* create with ACL */
#define	TX_CREATE_ATTR		15	/* create + attrs */
#define	TX_CREATE_ACL_ATTR 	16	/* create with ACL + attrs */
#define	TX_MKDIR_ACL		17	/* mkdir with ACL */
#define	TX_MKDIR_ATTR		18	/* mkdir with attr */
#define	TX_MKDIR_ACL_ATTR	19	/* mkdir with ACL + attrs */
#define	TX_MAX_TYPE		20	/* Max transaction type */

/*
 * The transactions for mkdir, symlink, remove, rmdir, link, and rename
 * may have the following bit set, indicating the original request
 * specified case-insensitive handling of names.
 */
#define	TX_CI	((uint64_t)0x1 << 63) /* case-insensitive behavior requested */

/*
 * Format of log records.
 * The fields are carefully defined to allow them to be aligned
 * and sized the same on sparc & intel architectures.
 * Each log record has a common structure at the beginning.
 *
 * Note, lrc_seq holds two different sequence numbers. Whilst in memory
 * it contains the transaction sequence number.  The log record on
 * disk holds the sequence number of all log records which is used to
 * ensure we don't replay the same record.  The two sequence numbers are
 * different because the transactions can now be pushed out of order.
 */
typedef struct {			/* common log record header */
	uint64_t	lrc_txtype;	/* intent log transaction type */
	uint64_t	lrc_reclen;	/* transaction record length */
	uint64_t	lrc_txg;	/* dmu transaction group number */
	uint64_t	lrc_seq;	/* see comment above */
} lr_t;

/*
 * Handle option extended vattr attributes.
 *
 * Whenever new attributes are added the version number
 * will need to be updated as will code in
 * zfs_log.c and zfs_replay.c
 */
typedef struct {
	uint32_t	lr_attr_masksize; /* number of elements in array */
	uint32_t	lr_attr_bitmap; /* First entry of array */
	/* remainder of array and any additional fields */
} lr_attr_t;

/*
 * log record for creates without optional ACL.
 * This log record does support optional xvattr_t attributes.
 */
typedef struct {
	lr_t		lr_common;	/* common portion of log record */
	uint64_t	lr_doid;	/* object id of directory */
	uint64_t	lr_foid;	/* object id of created file object */
	uint64_t	lr_mode;	/* mode of object */
	uint64_t	lr_uid;		/* uid of object */
	uint64_t	lr_gid;		/* gid of object */
	uint64_t	lr_gen;		/* generation (txg of creation) */
	uint64_t	lr_crtime[2];	/* creation time */
	uint64_t	lr_rdev;	/* rdev of object to create */
	/* name of object to create follows this */
	/* for symlinks, link content follows name */
	/* for creates with xvattr data, the name follows the xvattr info */
} lr_create_t;

/*
 * FUID ACL record will be an array of ACEs from the original ACL.
 * If this array includes ephemeral IDs, the record will also include
 * an array of log-specific FUIDs to replace the ephemeral IDs.
 * Only one copy of each unique domain will be present, so the log-specific
 * FUIDs will use an index into a compressed domain table.  On replay this
 * information will be used to construct real FUIDs (and bypass idmap,
 * since it may not be available).
 */

/*
 * Log record for creates with optional ACL
 * This log record is also used for recording any FUID
 * information needed for replaying the create.  If the
 * file doesn't have any actual ACEs then the lr_aclcnt
 * would be zero.
 */
typedef struct {
	lr_create_t	lr_create;	/* common create portion */
	uint64_t	lr_aclcnt;	/* number of ACEs in ACL */
	uint64_t	lr_domcnt;	/* number of unique domains */
	uint64_t	lr_fuidcnt;	/* number of real fuids */
	uint64_t	lr_acl_bytes;	/* number of bytes in ACL */
	uint64_t	lr_acl_flags;	/* ACL flags */
	/* lr_acl_bytes number of variable sized ace's follows */
	/* if create is also setting xvattr's, then acl data follows xvattr */
	/* if ACE FUIDs are needed then they will follow the xvattr_t */
	/* Following the FUIDs will be the domain table information. */
	/* The FUIDs for the owner and group will be in the lr_create */
	/* portion of the record. */
	/* name follows ACL data */
} lr_acl_create_t;

typedef struct {
	lr_t		lr_common;	/* common portion of log record */
	uint64_t	lr_doid;	/* obj id of directory */
	/* name of object to remove follows this */
} lr_remove_t;

typedef struct {
	lr_t		lr_common;	/* common portion of log record */
	uint64_t	lr_doid;	/* obj id of directory */
	uint64_t	lr_link_obj;	/* obj id of link */
	/* name of object to link follows this */
} lr_link_t;

typedef struct {
	lr_t		lr_common;	/* common portion of log record */
	uint64_t	lr_sdoid;	/* obj id of source directory */
	uint64_t	lr_tdoid;	/* obj id of target directory */
	/* 2 strings: names of source and destination follow this */
} lr_rename_t;

typedef struct {
	lr_t		lr_common;	/* common portion of log record */
	uint64_t	lr_foid;	/* file object to write */
	uint64_t	lr_offset;	/* offset to write to */
	uint64_t	lr_length;	/* user data length to write */
	uint64_t	lr_blkoff;	/* offset represented by lr_blkptr */
	blkptr_t	lr_blkptr;	/* spa block pointer for replay */
	/* write data will follow for small writes */
} lr_write_t;

typedef struct {
	lr_t		lr_common;	/* common portion of log record */
	uint64_t	lr_foid;	/* object id of file to truncate */
	uint64_t	lr_offset;	/* offset to truncate from */
	uint64_t	lr_length;	/* length to truncate */
} lr_truncate_t;

typedef struct {
	lr_t		lr_common;	/* common portion of log record */
	uint64_t	lr_foid;	/* file object to change attributes */
	uint64_t	lr_mask;	/* mask of attributes to set */
	uint64_t	lr_mode;	/* mode to set */
	uint64_t	lr_uid;		/* uid to set */
	uint64_t	lr_gid;		/* gid to set */
	uint64_t	lr_size;	/* size to set */
	uint64_t	lr_atime[2];	/* access time */
	uint64_t	lr_mtime[2];	/* modification time */
	/* optional attribute lr_attr_t may be here */
} lr_setattr_t;

typedef struct {
	lr_t		lr_common;	/* common portion of log record */
	uint64_t	lr_foid;	/* obj id of file */
	uint64_t	lr_aclcnt;	/* number of acl entries */
	/* lr_aclcnt number of ace_t entries follow this */
} lr_acl_v0_t;

typedef struct {
	lr_t		lr_common;	/* common portion of log record */
	uint64_t	lr_foid;	/* obj id of file */
	uint64_t	lr_aclcnt;	/* number of ACEs in ACL */
	uint64_t	lr_domcnt;	/* number of unique domains */
	uint64_t	lr_fuidcnt;	/* number of real fuids */
	uint64_t	lr_acl_bytes;	/* number of bytes in ACL */
	uint64_t	lr_acl_flags;	/* ACL flags */
	/* lr_acl_bytes number of variable sized ace's follows */
} lr_acl_t;

/*
 * ZIL structure definitions, interface function prototype and globals.
 */

/*
 * ZFS intent log transaction structure
 */
typedef enum {
	WR_INDIRECT,	/* indirect - a large write (dmu_sync() data */
			/* and put blkptr in log, rather than actual data) */
	WR_COPIED,	/* immediate - data is copied into lr_write_t */
	WR_NEED_COPY,	/* immediate - data needs to be copied if pushed */
} itx_wr_state_t;

typedef struct itx {
	list_node_t	itx_node;	/* linkage on zl_itx_list */
	void		*itx_private;	/* type-specific opaque data */
	itx_wr_state_t	itx_wr_state;	/* write state */
	uint8_t		itx_sync;	/* synchronous transaction */
	uint64_t	itx_sod;	/* record size on disk */
	lr_t		itx_lr;		/* common part of log record */
	/* followed by type-specific part of lr_xx_t and its immediate data */
} itx_t;


/*
 * zgd_t is passed through dmu_sync() to the callback routine zfs_get_done()
 * to handle the cleanup of the dmu_sync() buffer write
 */
typedef struct {
	zilog_t		*zgd_zilog;	/* zilog */
	blkptr_t	*zgd_bp;	/* block pointer */
	struct rl	*zgd_rl;	/* range lock */
} zgd_t;


typedef void zil_parse_blk_func_t(zilog_t *zilog, blkptr_t *bp, void *arg,
    uint64_t txg);
typedef void zil_parse_lr_func_t(zilog_t *zilog, lr_t *lr, void *arg,
    uint64_t txg);
typedef int zil_replay_func_t();
typedef void zil_replay_cleaner_t();
typedef int zil_get_data_t(void *arg, lr_write_t *lr, char *dbuf, zio_t *zio);

extern uint64_t	zil_parse(zilog_t *zilog, zil_parse_blk_func_t *parse_blk_func,
    zil_parse_lr_func_t *parse_lr_func, void *arg, uint64_t txg);

extern void	zil_init(void);
extern void	zil_fini(void);

extern zilog_t	*zil_alloc(objset_t *os, zil_header_t *zh_phys);
extern void	zil_free(zilog_t *zilog);

extern zilog_t	*zil_open(objset_t *os, zil_get_data_t *get_data);
extern void	zil_close(zilog_t *zilog);

extern void	zil_replay(objset_t *os, void *arg, uint64_t *txgp,
    zil_replay_func_t *replay_func[TX_MAX_TYPE],
    zil_replay_cleaner_t *replay_cleaner);
extern void	zil_destroy(zilog_t *zilog, boolean_t keep_first);
extern void	zil_rollback_destroy(zilog_t *zilog, dmu_tx_t *tx);

extern itx_t	*zil_itx_create(uint64_t txtype, size_t lrsize);
extern uint64_t zil_itx_assign(zilog_t *zilog, itx_t *itx, dmu_tx_t *tx);

extern void	zil_commit(zilog_t *zilog, uint64_t seq, uint64_t oid);

extern int	zil_claim(char *osname, void *txarg);
extern int	zil_check_log_chain(char *osname, void *txarg);
extern int	zil_clear_log_chain(char *osname, void *txarg);
extern void	zil_sync(zilog_t *zilog, dmu_tx_t *tx);
extern void	zil_clean(zilog_t *zilog);
extern int	zil_is_committed(zilog_t *zilog);

extern int	zil_suspend(zilog_t *zilog);
extern void	zil_resume(zilog_t *zilog);

extern void	zil_add_block(zilog_t *zilog, blkptr_t *bp);

extern int zil_disable;

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_ZIL_H */
