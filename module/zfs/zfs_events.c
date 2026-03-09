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

#include <sys/spa.h>
#include <sys/zap.h>
#include <sys/dmu_tx.h>
#include <sys/dmu_objset.h>
#include <sys/dsl_dataset.h>
#include <sys/dsl_dir.h>
#include <sys/cmn_err.h>
#include <sys/sunddi.h>
#include <sys/cred.h>
#include <sys/zfs_events.h>
#include <sys/zfs_znode.h>
#include <sys/byteorder.h>

/*
 * ZFS Events - File-level history tracking
 *
 * This module implements persistent event logging for file operations at the
 * dataset level. The design follows spa_history.c as a reference, using a
 * ring buffer of packed nvlists.
 *
 * Storage format:
 * The event log is stored as a DMU object containing
 * <packed record length (uint64_t LE), record nvlist> tuples.
 *
 * The header (zfs_events_phys_t) is stored in the bonus buffer and tracks:
 * - zep_phys_max_off: physical size of the log (max offset)
 * - zep_bof: logical beginning of file (oldest record)
 * - zep_eof: logical end of file (write position)
 * - zep_records_lost: count of overwritten records
 *
 * Ring buffer operation:
 * The log wraps around when zep_eof reaches zep_phys_max_off. When
 * a new record would overwrite old data, zep_bof is advanced and
 * zep_records_lost is incremented.
 */

/*
 * Name of the ZAP entry in the dataset's master node that points to the
 * event log object.
 */
#define	ZFS_EVENTS_ZAP_NAME	"com.zfs:events"

/*
 * Convert logical offset to physical offset in the ring buffer.
 */
static uint64_t
zfs_events_log_to_phys(uint64_t log_off, zfs_events_phys_t *zep)
{
	return (log_off % zep->zep_phys_max_off);
}

/*
 * Advance zep_bof to skip the oldest record, freeing space for new records.
 */
static int
zfs_events_advance_bof(objset_t *os, uint64_t obj, zfs_events_phys_t *zep)
{
	uint64_t firstread, reclen, phys_bof;
	char buf[sizeof (reclen)];
	int err;

	phys_bof = zfs_events_log_to_phys(zep->zep_bof, zep);
	firstread = MIN(sizeof (reclen), zep->zep_phys_max_off - phys_bof);

	if ((err = dmu_read(os, obj, phys_bof, firstread, buf,
	    DMU_READ_PREFETCH)) != 0)
		return (err);

	if (firstread != sizeof (reclen)) {
		if ((err = dmu_read(os, obj, 0,
		    sizeof (reclen) - firstread, buf + firstread,
		    DMU_READ_PREFETCH)) != 0)
			return (err);
	}

	reclen = LE_64(*((uint64_t *)buf));
	zep->zep_bof += reclen + sizeof (reclen);
	zep->zep_records_lost++;
	return (0);
}

/*
 * Write data to the event log at the current EOF position, wrapping if needed.
 */
static int
zfs_events_write(objset_t *os, uint64_t obj, void *buf, uint64_t len,
    zfs_events_phys_t *zep, dmu_tx_t *tx)
{
	uint64_t firstwrite, phys_eof;
	int err;

	/* Advance BOF if we need to make room */
	while (zep->zep_phys_max_off - (zep->zep_eof - zep->zep_bof) <= len) {
		if ((err = zfs_events_advance_bof(os, obj, zep)) != 0)
			return (err);
	}

	phys_eof = zfs_events_log_to_phys(zep->zep_eof, zep);
	firstwrite = MIN(len, zep->zep_phys_max_off - phys_eof);
	zep->zep_eof += len;

	dmu_write(os, obj, phys_eof, firstwrite, buf, tx,
	    DMU_READ_NO_PREFETCH);

	len -= firstwrite;
	if (len > 0) {
		/* wrap around to beginning of buffer */
		dmu_write(os, obj, 0, len, (char *)buf + firstwrite, tx,
		    DMU_READ_NO_PREFETCH);
	}

	return (0);
}

/*
 * Create the event log object for a dataset.
 */
int
zfs_events_create_obj(objset_t *os, dmu_tx_t *tx, uint64_t *objp)
{
	dmu_buf_t *dbp;
	zfs_events_phys_t *zep;
	uint64_t obj;

	/* Use DMU_OTN_UINT8_METADATA for the event log data */
	obj = dmu_object_alloc(os, DMU_OTN_UINT8_METADATA,
	    SPA_OLD_MAXBLOCKSIZE, DMU_OTN_UINT64_METADATA,
	    sizeof (zfs_events_phys_t), tx);

	VERIFY0(dmu_bonus_hold(os, obj, FTAG, &dbp));
	ASSERT3U(dbp->db_size, >=, sizeof (zfs_events_phys_t));

	zep = dbp->db_data;
	dmu_buf_will_dirty(dbp, tx);

	/*
	 * Initialize the event log header.
	 * Default size: 1MB, can be adjusted via property.
	 */
	memset(zep, 0, sizeof (zfs_events_phys_t));
	zep->zep_phys_max_off = 1 << 20;	/* 1 MB default */
	zep->zep_version = ZFS_EVENTS_VERSION;

	dmu_buf_rele(dbp, FTAG);

	*objp = obj;
	return (0);
}

/*
 * Destroy the event log object for a dataset.
 */
int
zfs_events_destroy_obj(objset_t *os, uint64_t obj, dmu_tx_t *tx)
{
	return (dmu_object_free(os, obj, tx));
}

/*
 * Internal helper to log an event to the dataset's event log.
 * Creates the event log object lazily if it doesn't exist.
 *
 * Note: This function assumes that the caller has already verified
 * that events are enabled (via zfsvfs->z_events).
 */
static void
zfs_events_log_event(objset_t *os, dmu_tx_t *tx, nvlist_t *nvl)
{
	dmu_buf_t *dbp;
	zfs_events_phys_t *zep;
	uint64_t obj;
	char *packed = NULL;
	size_t packed_len;
	uint64_t le_len;
	int err;

	/* Look up the event log object from the objset's master node */
	err = zap_lookup(os, MASTER_NODE_OBJ, ZFS_EVENTS_ZAP_NAME,
	    sizeof (uint64_t), 1, &obj);
	if (err == ENOENT) {
		/*
		 * Event log object doesn't exist yet. Create it now.
		 * This happens on the first event after events are enabled.
		 */
		err = zfs_events_create_obj(os, tx, &obj);
		if (err != 0)
			return;

		/* Add the object to the master node ZAP */
		err = zap_add(os, MASTER_NODE_OBJ, ZFS_EVENTS_ZAP_NAME,
		    sizeof (uint64_t), 1, &obj, tx);
		if (err != 0) {
			/* Failed to add ZAP entry, clean up */
			(void) dmu_object_free(os, obj, tx);
			return;
		}
	} else if (err != 0) {
		/* Some other error, bail out */
		return;
	}

	/* Add transaction group and timestamp */
	fnvlist_add_uint64(nvl, ZFS_EV_TXG, dmu_tx_get_txg(tx));
	fnvlist_add_uint64(nvl, ZFS_EV_TIME, gethrtime());

	/* Pack the nvlist */
	VERIFY0(nvlist_pack(nvl, &packed, &packed_len, NV_ENCODE_NATIVE,
	    KM_SLEEP));

	/* Get the event log header from bonus buffer */
	err = dmu_bonus_hold(os, obj, FTAG, &dbp);
	if (err != 0) {
		fnvlist_pack_free(packed, packed_len);
		return;
	}

	zep = dbp->db_data;
	dmu_buf_will_dirty(dbp, tx);

	/* Write the packed length (little endian) followed by the record */
	le_len = LE_64((uint64_t)packed_len);
	err = zfs_events_write(os, obj, &le_len, sizeof (le_len), zep, tx);
	if (err == 0) {
		err = zfs_events_write(os, obj, packed, packed_len, zep, tx);
	}

	dmu_buf_rele(dbp, FTAG);
	fnvlist_pack_free(packed, packed_len);
}

/*
 * Log a file/directory creation event.
 */
void
zfs_events_log_create(objset_t *os, dmu_tx_t *tx,
    uint64_t object, uint64_t parent, const char *name, uint64_t mode,
    uint64_t uid, uint64_t gid)
{
	nvlist_t *nvl;

	nvl = fnvlist_alloc();
	fnvlist_add_uint16(nvl, ZFS_EV_OP, ZFS_EV_CREATE);
	fnvlist_add_uint64(nvl, ZFS_EV_OBJECT, object);
	fnvlist_add_uint64(nvl, ZFS_EV_PARENT, parent);
	fnvlist_add_string(nvl, ZFS_EV_NAME, name);
	fnvlist_add_uint64(nvl, ZFS_EV_MODE, mode);
	fnvlist_add_uint64(nvl, ZFS_EV_UID, uid);
	fnvlist_add_uint64(nvl, ZFS_EV_GID, gid);

	zfs_events_log_event(os, tx, nvl);
	fnvlist_free(nvl);
}

/*
 * Log a file/directory removal event.
 */
void
zfs_events_log_remove(objset_t *os, dmu_tx_t *tx,
    uint64_t object, uint64_t parent, const char *name)
{
	nvlist_t *nvl;

	nvl = fnvlist_alloc();
	fnvlist_add_uint16(nvl, ZFS_EV_OP, ZFS_EV_REMOVE);
	fnvlist_add_uint64(nvl, ZFS_EV_OBJECT, object);
	fnvlist_add_uint64(nvl, ZFS_EV_PARENT, parent);
	fnvlist_add_string(nvl, ZFS_EV_NAME, name);

	zfs_events_log_event(os, tx, nvl);
	fnvlist_free(nvl);
}

/*
 * Log a rename event.
 */
void
zfs_events_log_rename(objset_t *os, dmu_tx_t *tx,
    uint64_t object, uint64_t old_parent, const char *old_name,
    uint64_t new_parent, const char *new_name)
{
	nvlist_t *nvl;

	nvl = fnvlist_alloc();
	fnvlist_add_uint16(nvl, ZFS_EV_OP, ZFS_EV_RENAME);
	fnvlist_add_uint64(nvl, ZFS_EV_OBJECT, object);
	fnvlist_add_uint64(nvl, ZFS_EV_OLD_PARENT, old_parent);
	fnvlist_add_string(nvl, ZFS_EV_OLD_NAME, old_name);
	fnvlist_add_uint64(nvl, ZFS_EV_PARENT, new_parent);
	fnvlist_add_string(nvl, ZFS_EV_NAME, new_name);

	zfs_events_log_event(os, tx, nvl);
	fnvlist_free(nvl);
}

/*
 * Log a hard link creation event.
 */
void
zfs_events_log_link(objset_t *os, dmu_tx_t *tx,
    uint64_t object, uint64_t parent, const char *name)
{
	nvlist_t *nvl;

	nvl = fnvlist_alloc();
	fnvlist_add_uint16(nvl, ZFS_EV_OP, ZFS_EV_LINK);
	fnvlist_add_uint64(nvl, ZFS_EV_OBJECT, object);
	fnvlist_add_uint64(nvl, ZFS_EV_PARENT, parent);
	fnvlist_add_string(nvl, ZFS_EV_NAME, name);

	zfs_events_log_event(os, tx, nvl);
	fnvlist_free(nvl);
}

/*
 * Log a symlink creation event.
 */
void
zfs_events_log_symlink(objset_t *os, dmu_tx_t *tx,
    uint64_t object, uint64_t parent, const char *name, const char *target)
{
	nvlist_t *nvl;

	nvl = fnvlist_alloc();
	fnvlist_add_uint16(nvl, ZFS_EV_OP, ZFS_EV_SYMLINK);
	fnvlist_add_uint64(nvl, ZFS_EV_OBJECT, object);
	fnvlist_add_uint64(nvl, ZFS_EV_PARENT, parent);
	fnvlist_add_string(nvl, ZFS_EV_NAME, name);
	fnvlist_add_string(nvl, ZFS_EV_TARGET, target);

	zfs_events_log_event(os, tx, nvl);
	fnvlist_free(nvl);
}

/*
 * Log a file truncation event.
 */
void
zfs_events_log_truncate(objset_t *os, dmu_tx_t *tx,
    uint64_t object, uint64_t old_size, uint64_t new_size)
{
	nvlist_t *nvl;

	nvl = fnvlist_alloc();
	fnvlist_add_uint16(nvl, ZFS_EV_OP, ZFS_EV_TRUNCATE);
	fnvlist_add_uint64(nvl, ZFS_EV_OBJECT, object);
	fnvlist_add_uint64(nvl, ZFS_EV_OLD_SIZE, old_size);
	fnvlist_add_uint64(nvl, ZFS_EV_NEW_SIZE, new_size);

	zfs_events_log_event(os, tx, nvl);
	fnvlist_free(nvl);
}

/*
 * Log an attribute change event.
 */
void
zfs_events_log_setattr(objset_t *os, dmu_tx_t *tx,
    uint64_t object, uint64_t attr_mask)
{
	nvlist_t *nvl;

	nvl = fnvlist_alloc();
	fnvlist_add_uint16(nvl, ZFS_EV_OP, ZFS_EV_SETATTR);
	fnvlist_add_uint64(nvl, ZFS_EV_OBJECT, object);
	fnvlist_add_uint64(nvl, ZFS_EV_ATTRS, attr_mask);

	zfs_events_log_event(os, tx, nvl);
	fnvlist_free(nvl);
}

/*
 * Read events from the event log.
 *
 * offp: in/out - logical offset to start reading, updated on return
 * lenp: in/out - buffer length on input, bytes read on return
 * buf: buffer to read events into
 */
int
zfs_events_get(objset_t *os, uint64_t *offp, uint64_t *lenp, char *buf)
{
	dmu_buf_t *dbp;
	uint64_t obj;
	uint64_t read_len, phys_read_off, phys_eof;
	uint64_t leftover = 0;
	zfs_events_phys_t *zep;
	int err;

	/* Look up the event log object */
	err = zap_lookup(os, MASTER_NODE_OBJ, ZFS_EVENTS_ZAP_NAME,
	    sizeof (uint64_t), 1, &obj);
	if (err != 0)
		return (SET_ERROR(ENOENT));

	err = dmu_bonus_hold(os, obj, FTAG, &dbp);
	if (err != 0)
		return (err);

	zep = dbp->db_data;

	/* Validate and clamp the read offset */
	if (*offp < zep->zep_bof)
		*offp = zep->zep_bof;
	else if (*offp > zep->zep_eof)
		*offp = zep->zep_eof;

	/* Calculate how much we can read */
	read_len = MIN(*lenp, zep->zep_eof - *offp);

	if (read_len == 0) {
		dmu_buf_rele(dbp, FTAG);
		*lenp = 0;
		return (0);
	}

	/* Convert to physical offset and handle wrap-around */
	phys_read_off = zfs_events_log_to_phys(*offp, zep);
	phys_eof = zfs_events_log_to_phys(zep->zep_eof, zep);

	if (phys_read_off < phys_eof) {
		/* No wrap, simple read */
		err = dmu_read(os, obj, phys_read_off, read_len, buf,
		    DMU_READ_PREFETCH);
	} else {
		/* Handle wrap-around read */
		uint64_t first = zep->zep_phys_max_off - phys_read_off;
		if (first > read_len)
			first = read_len;

		err = dmu_read(os, obj, phys_read_off, first, buf,
		    DMU_READ_PREFETCH);
		if (err == 0 && read_len > first) {
			leftover = read_len - first;
			err = dmu_read(os, obj, 0, leftover, buf + first,
			    DMU_READ_PREFETCH);
		}
	}

	dmu_buf_rele(dbp, FTAG);

	if (err == 0) {
		*offp += read_len;
		*lenp = read_len;
	}

	return (err);
}

#if defined(_KERNEL)
EXPORT_SYMBOL(zfs_events_create_obj);
EXPORT_SYMBOL(zfs_events_destroy_obj);
EXPORT_SYMBOL(zfs_events_log_create);
EXPORT_SYMBOL(zfs_events_log_remove);
EXPORT_SYMBOL(zfs_events_log_rename);
EXPORT_SYMBOL(zfs_events_log_link);
EXPORT_SYMBOL(zfs_events_log_symlink);
EXPORT_SYMBOL(zfs_events_log_truncate);
EXPORT_SYMBOL(zfs_events_log_setattr);
EXPORT_SYMBOL(zfs_events_get);
#endif
