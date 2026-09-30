// SPDX-License-Identifier: CDDL-1.0
/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or https://opensource.org/licenses/CDDL-1.0.
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
 * Copyright (c) 2026 by [contributors]
 * File-level history tracking for ZFS datasets.
 */

#ifndef	_SYS_ZFS_EVENTS_H
#define	_SYS_ZFS_EVENTS_H

#include <sys/dmu.h>
#include <sys/nvpair.h>
#include <sys/zfs_context.h>

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * ZFS Events - File-level history tracking
 *
 * This subsystem provides persistent logging of file operations at the
 * dataset level. Events are stored in a ring buffer per dataset and can
 * be queried to understand file history (creates, renames, deletes, etc.).
 *
 * The event log is stored as a DMU object containing:
 *   <packed record length (uint64_t LE), record nvlist> tuples
 *
 * The log is implemented as a ring buffer (similar to spa_history).
 * The header is stored in the object's bonus buffer.
 */

/*
 * Event operation types
 */
typedef enum zfs_event_op {
	ZFS_EV_NONE = 0,
	ZFS_EV_CREATE,		/* File/dir creation */
	ZFS_EV_REMOVE,		/* File/dir deletion */
	ZFS_EV_RENAME,		/* Rename operation */
	ZFS_EV_LINK,		/* Hard link creation */
	ZFS_EV_SYMLINK,		/* Symlink creation */
	ZFS_EV_TRUNCATE,	/* File truncation */
	ZFS_EV_SETATTR,		/* Attribute change */
	ZFS_EV_WRITE,		/* Data written to a file */
	ZFS_EV_READ,		/* Data read from a file */
	ZFS_EV_MAX_TYPE
} zfs_event_op_t;

/*
 * Event log header - stored in the bonus buffer of the event log object.
 * All fields are uint64_t for byteswap purposes.
 */
typedef struct zfs_events_phys {
	uint64_t	zep_phys_max_off;	/* physical EOF (max size) */
	uint64_t	zep_bof;		/* logical BOF */
	uint64_t	zep_eof;		/* logical EOF */
	uint64_t	zep_records_lost;	/* num of records overwritten */
	uint64_t	zep_version;		/* format version */
	uint64_t	zep_guid;		/* ring identity GUID */
	uint64_t	zep_pad[2];		/* reserved for future use */
} zfs_events_phys_t;

#define	ZFS_EVENTS_VERSION	2

/*
 * Nvlist keys for event records
 */
#define	ZFS_EV_TXG		"txg"		/* uint64: transaction group */
#define	ZFS_EV_TIME		"time"		/* uint64: hrtime timestamp */
#define	ZFS_EV_OBJECT		"object"	/* uint64: object ID affected */
#define	ZFS_EV_OP		"op"		/* uint16: operation type */
#define	ZFS_EV_NAME		"name"		/* string: file/dir name */
#define	ZFS_EV_PARENT		"parent"	/* uint64: parent object ID */
#define	ZFS_EV_OLD_NAME		"old_name"	/* old name (rename) */
#define	ZFS_EV_OLD_PARENT	"old_parent"	/* old parent (rename) */
#define	ZFS_EV_TARGET		"target"	/* symlink target */
#define	ZFS_EV_MODE		"mode"		/* file mode (create) */
#define	ZFS_EV_OLD_SIZE		"old_size"	/* size before truncate */
#define	ZFS_EV_NEW_SIZE		"new_size"	/* size after truncate */
#define	ZFS_EV_ATTRS		"attrs"		/* uint64: changed attr mask */
#define	ZFS_EV_UID		"uid"		/* uint64: user ID */
#define	ZFS_EV_GID		"gid"		/* uint64: group ID */
#define	ZFS_EV_IO_OFFSET	"io_offset"	/* uint64: IO start offset */
#define	ZFS_EV_IO_BYTES		"io_bytes"	/* uint64: IO byte count */

/*
 * Default and limits for event log size
 */
#define	ZFS_EVENTS_MIN_SIZE	(128 << 10)	/* 128 KB minimum */
#define	ZFS_EVENTS_MAX_SIZE	(1ULL << 30)	/* 1 GB maximum */
#define	ZFS_EVENTS_DEFAULT_PCT	10		/* 0.1% of dataset refquota */

/*
 * events_io_window limits (milliseconds). 0 disables the IO TIME fence
 * (every read/write emits immediately); the maximum is one hour.
 */
#define	ZFS_EVENTS_IO_WINDOW_MAX	3600000

/*
 * Core API functions
 */
extern int zfs_events_create_obj(objset_t *os, dmu_tx_t *tx, uint64_t max_size,
    uint64_t *objp);
extern int zfs_events_destroy_obj(objset_t *os, uint64_t obj, dmu_tx_t *tx);
extern void zfs_events_txhold(objset_t *os, dmu_tx_t *tx);

/*
 * Event logging functions - called from vnops
 */
extern void zfs_events_log_create(objset_t *os, dmu_tx_t *tx,
    uint64_t object, uint64_t parent, const char *name, uint64_t mode,
    uint64_t uid, uint64_t gid, uint64_t events_size, uint64_t *objp,
    kmutex_t *lockp);
extern void zfs_events_log_remove(objset_t *os, dmu_tx_t *tx,
    uint64_t object, uint64_t parent, const char *name,
    uint64_t events_size, uint64_t *objp, kmutex_t *lockp);
extern void zfs_events_log_rename(objset_t *os, dmu_tx_t *tx,
    uint64_t object, uint64_t old_parent, const char *old_name,
    uint64_t new_parent, const char *new_name, uint64_t events_size,
    uint64_t *objp, kmutex_t *lockp);
extern void zfs_events_log_link(objset_t *os, dmu_tx_t *tx,
    uint64_t object, uint64_t parent, const char *name,
    uint64_t events_size, uint64_t *objp, kmutex_t *lockp);
extern void zfs_events_log_symlink(objset_t *os, dmu_tx_t *tx,
    uint64_t object, uint64_t parent, const char *name, const char *target,
    uint64_t events_size, uint64_t *objp, kmutex_t *lockp);
extern void zfs_events_log_truncate(objset_t *os, dmu_tx_t *tx,
    uint64_t object, uint64_t old_size, uint64_t new_size,
    uint64_t events_size, uint64_t *objp, kmutex_t *lockp);
extern void zfs_events_log_setattr(objset_t *os, dmu_tx_t *tx,
    uint64_t object, uint64_t attr_mask, uint64_t events_size,
    uint64_t *objp, kmutex_t *lockp);
extern void zfs_events_log_write(objset_t *os, dmu_tx_t *tx,
    uint64_t object, uint64_t offset, uint64_t bytes, const cred_t *cr,
    uint64_t events_size, uint64_t *objp, kmutex_t *lockp, uint64_t txg);
extern void zfs_events_log_read(objset_t *os, uint64_t object,
    uint64_t offset, uint64_t bytes, const cred_t *cr,
    uint64_t events_size, uint64_t *objp, kmutex_t *lockp);

/*
 * Per-file IO event TIME fence. zfs_events_io_account() feeds a
 * completed read/write into the fence; callers holding a transaction
 * pass its txg, transaction-less callers pass 0 (the pool's open txg
 * is used). zfs_events_io_flush() emits a file's pending window
 * early; tx may be NULL (open txg attribution).
 *
 * Kernel-only: userspace (libzpool) has no struct znode.
 */
#if defined(_KERNEL)
struct znode;
struct zfsvfs;

/*
 * Fixed-size deferred IO-record queue entry (window == 0 emission).
 * See zfs_events.c for the queue protocol.
 */
typedef struct zfs_events_qent {
	list_node_t		qe_node;
	uint16_t		qe_op;		/* ZFS_EV_WRITE / _READ */
	uint64_t		qe_object;
	uint64_t		qe_offset;
	uint64_t		qe_bytes;
	uint64_t		qe_uid;
	uint64_t		qe_gid;
	uint64_t		qe_txg;
} zfs_events_qent_t;

extern void zfs_events_io_account(struct znode *zp, boolean_t is_write,
    uint64_t offset, uint64_t bytes, const cred_t *cr, uint64_t txg);
extern void zfs_events_io_flush(struct znode *zp, objset_t *os,
    dmu_tx_t *tx, boolean_t is_write);
extern void zfs_events_drain_shutdown(struct zfsvfs *zfsvfs);
extern void zfs_events_qent_init(void);
extern void zfs_events_qent_fini(void);
#endif	/* _KERNEL */

/*
 * Event retrieval functions
 */
extern int zfs_events_get(objset_t *os, kmutex_t *lockp, uint64_t *offp,
    uint64_t *lenp, char *buf);
extern int zfs_events_get_lost(objset_t *os, uint64_t *lostp);
extern int zfs_events_get_schema_version(objset_t *os, uint64_t *verp);
extern int zfs_events_get_guid(objset_t *os, uint64_t *guidp);
extern int zfs_events_clear(objset_t *os, dmu_tx_t *tx, uint64_t *countp);
extern int zfs_events_clear_task(objset_t *os);

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_ZFS_EVENTS_H */
