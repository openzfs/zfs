// SPDX-License-Identifier: CDDL-1.0
/*
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this
 * source.  A copy of the CDDL is also available via the Internet at
 * https://opensource.org/license/CDDL-1.0.
 *
 * Copyright (c) 2026 by HeonJe Lee
 */

#ifndef	_SYS_ZFS_CHANGELOG_H
#define	_SYS_ZFS_CHANGELOG_H

#include <sys/zfs_context.h>

#ifdef	__cplusplus
extern "C" {
#endif

struct objset;
struct dmu_tx;
struct zfsvfs;

/*
 * Record operation types.  A consumer mask is a bitmap of (1 << op).
 */
#define	CL_OP_CREATE		1
#define	CL_OP_REMOVE		2
#define	CL_OP_RENAME		3
#define	CL_OP_RMDIR		4
#define	CL_OP_MKDIR		5
#define	CL_OP_SETXATTR		6
#define	CL_OP_SETATTR		7
#define	CL_OP_SYMLINK		8
#define	CL_OP_LINK		9
#define	CL_OP_MKNOD		10
#define	CL_OP_NTYPES		11

#define	CL_MASK_ALL		UINT64_MAX
#define	CL_OP_MASK(op)		(1ULL << (op))

/* clr_flags */
#define	CL_F_NAME_TRUNC		0x01	/* name did not fit, truncated */
/*
 * RENAME packs two names into clr_name as "old\0new" (clr_namelen is
 * the length of the old name, the new name follows the NUL) and the
 * old parent object into clr_spare.  One record per rename, as in the
 * Lustre changelog.
 */
#define	CL_F_RENAME2		0x02	/* packed old\0new rename record */

/*
 * On-disk record.  Fixed size so that a record's slot index is its seq:
 * seq N lives at byte offset (N % recs_per_chunk) * CL_REC_SIZE of the
 * chunk object (N / recs_per_chunk).  Records are appended under the
 * per-dataset log lock in seq order, so the stream has no holes.
 *
 * Native byte order; clr_magic doubles as the byte-order check.
 */
/*
 * 128B record: the information in a change is ~60B (op + FIDs + a short
 * name), and the log stream's total volume is what the storage pipeline
 * pays for, so the name field is capped at 72 bytes.  Longer names set
 * CL_F_NAME_TRUNC; consumers recover them by FID (OBJ_TO_PATH).
 */
#define	CL_REC_SIZE		128
#define	CL_NAME_MAX		72
#define	CL_REC_MAGIC		0x474c4452U	/* "RDLG" */

typedef struct changelog_rec {
	uint32_t	clr_magic;
	uint16_t	clr_namelen;
	uint8_t		clr_op;
	uint8_t		clr_flags;
	uint64_t	clr_seq;	/* slot index, self-describing */
	uint64_t	clr_txg;	/* txg the change committed in */
	uint64_t	clr_time;	/* seconds, for consumer lag */
	uint64_t	clr_object;	/* target object number (FID) */
	uint64_t	clr_parent;	/* parent directory object number */
	uint64_t	clr_spare;
	char		clr_name[CL_NAME_MAX];
} changelog_rec_t;

/*
 * On-disk log header, written at offset 0 of the header object.  The
 * header object number is stored in the ZPL master node under
 * CHANGELOG_MASTER_KEY.
 */
#define	CL_PHYS_MAGIC		0x676f4c61746c6544ULL	/* "DeltaLog" */
#define	CL_VERSION		1
#define	CHANGELOG_MASTER_KEY	"CHANGELOG"

typedef struct changelog_phys {
	uint64_t	clp_magic;
	uint64_t	clp_version;
	/*
	 * Advisory only.  The head is recovered by scanning the newest
	 * chunk at open time, which keeps the append path from having to
	 * write this block on every record.  The scan is exact because
	 * the stream has no holes: slot i holds a record iff i < head.
	 */
	uint64_t	clp_head_hint;
	uint64_t	clp_tail;		/* oldest seq still kept */
	uint64_t	clp_generation;		/* bumped on forced trim */
	uint64_t	clp_catalog_obj;	/* ZAP: chunk index -> object */
	uint64_t	clp_consumer_obj;	/* ZAP: name -> consumer */
	uint64_t	clp_recs_per_chunk;
	uint64_t	clp_dropped;	/* records lost to forced trim */
	uint64_t	clp_max_chunk;		/* highest chunk ever created */
	uint64_t	clp_rec_size;	/* CL_REC_SIZE at creation time */
	/*
	 * Crash-gap detection: the op path stamps clp_enq_txg once per txg
	 * (the write lands in the same txg as the changes), the flusher
	 * stamps clp_covered_txg with the highest change txg it made
	 * durable.  At open, enq > covered means a crash lost the queued
	 * tail - declare a gap so consumers rescan instead of missing
	 * changes silently.
	 */
	uint64_t	clp_enq_txg;
	uint64_t	clp_covered_txg;
	uint64_t	clp_pad[3];
} changelog_phys_t;

/* Consumer record: 5 uint64s in the consumer ZAP, keyed by name. */
#define	CL_CONSUMER_INTS	5
#define	CL_CONSUMER_NAME_MAX	64

typedef struct changelog_consumer_phys {
	uint64_t	clc_clear;	/* records below this are acked */
	uint64_t	clc_mask;	/* op mask, 0 means all */
	uint64_t	clc_read;	/* last seq served (advisory) */
	uint64_t	clc_time;	/* last activity, seconds */
	uint64_t	clc_gen;	/* generation seen at registration */
} changelog_consumer_phys_t;

/*
 * Space reservation carried from the tx hold to the record append.  The
 * caller keeps this on the stack; no cleanup is required on error paths
 * because an unused reservation writes nothing.
 */
typedef struct changelog_res {
	boolean_t	clr_held;
	uint64_t	clr_chunk_idx;	/* chunk current at hold time */
	uint64_t	clr_chunk_obj;	/* that chunk's object */
	uint64_t	clr_next_obj;	/* the following chunk's object */
} changelog_res_t;

/*
 * Data path.  zfs_changelog_tx_hold() must be called before
 * dmu_tx_assign(); zfs_changelog_record() before dmu_tx_commit(), with
 * the same reservation.  Both are no-ops unless the dataset property
 * changelog=on is set.
 */
extern void zfs_changelog_tx_hold(struct objset *os, struct dmu_tx *tx,
    changelog_res_t *res);
extern void zfs_changelog_record(struct objset *os, struct dmu_tx *tx,
    changelog_res_t *res, uint8_t op, uint64_t object, uint64_t parent,
    const char *name);
extern void zfs_changelog_record_rename(struct objset *os, struct dmu_tx *tx,
    changelog_res_t *res, uint64_t object, uint64_t oldparent,
    const char *oldname, uint64_t newparent, const char *newname);

/* Consumer API, called from the ioctl handlers. */
extern int zfs_changelog_register(struct objset *os, const char *cname,
    uint64_t mask, uint64_t *seqp);
extern int zfs_changelog_unregister(struct objset *os, const char *cname);
extern int zfs_changelog_read(struct objset *os, const char *cname,
    uint64_t cursor, boolean_t use_cursor, uint64_t maxrecs, uint64_t wait_ms,
    changelog_rec_t *buf, uint64_t *nrecsp, uint64_t *next_cursorp,
    uint64_t *headp, uint64_t *tailp, uint64_t *genp, boolean_t *gapp);
extern int zfs_changelog_clear(struct objset *os, const char *cname,
    uint64_t seq);

/* Lifecycle. */
extern void zfs_changelog_flush_notify(struct objset *os);
extern int zfs_changelog_os_hold(const char *dsname, struct zfsvfs **zfvp,
    struct objset **osp);
extern void zfs_changelog_os_rele(struct zfsvfs *zfsvfs, struct objset *os);
extern void zfs_changelog_init(void);
extern void zfs_changelog_fini(void);
extern void zfs_changelog_objset_fini(struct objset *os);

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_ZFS_CHANGELOG_H */
