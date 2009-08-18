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
 * Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#include <sys/spa.h>
#include <sys/spa_impl.h>
#include <sys/zap.h>
#include <sys/dsl_synctask.h>
#include <sys/dmu_tx.h>
#include <sys/dmu_objset.h>
#include <sys/utsname.h>
#include <sys/cmn_err.h>
#include <sys/sunddi.h>
#ifdef _KERNEL
#include <sys/zone.h>
#endif

/*
 * Routines to manage the on-disk history log.
 *
 * The history log is stored as a dmu object containing
 * <packed record length, record nvlist> tuples.
 *
 * Where "record nvlist" is a nvlist containing uint64_ts and strings, and
 * "packed record length" is the packed length of the "record nvlist" stored
 * as a little endian uint64_t.
 *
 * The log is implemented as a ring buffer, though the original creation
 * of the pool ('zpool create') is never overwritten.
 *
 * The history log is tracked as object 'spa_t::spa_history'.  The bonus buffer
 * of 'spa_history' stores the offsets for logging/retrieving history as
 * 'spa_history_phys_t'.  'sh_pool_create_len' is the ending offset in bytes of
 * where the 'zpool create' record is stored.  This allows us to never
 * overwrite the original creation of the pool.  'sh_phys_max_off' is the
 * physical ending offset in bytes of the log.  This tells you the length of
 * the buffer. 'sh_eof' is the logical EOF (in bytes).  Whenever a record
 * is added, 'sh_eof' is incremented by the the size of the record.
 * 'sh_eof' is never decremented.  'sh_bof' is the logical BOF (in bytes).
 * This is where the consumer should start reading from after reading in
 * the 'zpool create' portion of the log.
 *
 * 'sh_records_lost' keeps track of how many records have been overwritten
 * and permanently lost.
 */

/* convert a logical offset to physical */
static uint64_t
spa_history_log_to_phys(uint64_t log_off, spa_history_phys_t *shpp)
{
	uint64_t phys_len;

	phys_len = shpp->sh_phys_max_off - shpp->sh_pool_create_len;
	return ((log_off - shpp->sh_pool_create_len) % phys_len
	    + shpp->sh_pool_create_len);
}

void
spa_history_create_obj(spa_t *spa, dmu_tx_t *tx)
{
	dmu_buf_t *dbp;
	spa_history_phys_t *shpp;
	objset_t *mos = spa->spa_meta_objset;

	ASSERT(spa->spa_history == 0);
	spa->spa_history = dmu_object_alloc(mos, DMU_OT_SPA_HISTORY,
	    SPA_MAXBLOCKSIZE, DMU_OT_SPA_HISTORY_OFFSETS,
	    sizeof (spa_history_phys_t), tx);

	VERIFY(zap_add(mos, DMU_POOL_DIRECTORY_OBJECT,
	    DMU_POOL_HISTORY, sizeof (uint64_t), 1,
	    &spa->spa_history, tx) == 0);

	VERIFY(0 == dmu_bonus_hold(mos, spa->spa_history, FTAG, &dbp));
	ASSERT(dbp->db_size >= sizeof (spa_history_phys_t));

	shpp = dbp->db_data;
	dmu_buf_will_dirty(dbp, tx);

	/*
	 * Figure out maximum size of history log.  We set it at
	 * 1% of pool size, with a max of 32MB and min of 128KB.
	 */
	shpp->sh_phys_max_off = spa_get_dspace(spa) / 100;
	shpp->sh_phys_max_off = MIN(shpp->sh_phys_max_off, 32<<20);
	shpp->sh_phys_max_off = MAX(shpp->sh_phys_max_off, 128<<10);

	dmu_buf_rele(dbp, FTAG);
}

/*
 * Change 'sh_bof' to the beginning of the next record.
 */
static int
spa_history_advance_bof(spa_t *spa, spa_history_phys_t *shpp)
{
	objset_t *mos = spa->spa_meta_objset;
	uint64_t firstread, reclen, phys_bof;
	char buf[sizeof (reclen)];
	int err;

	phys_bof = spa_history_log_to_phys(shpp->sh_bof, shpp);
	firstread = MIN(sizeof (reclen), shpp->sh_phys_max_off - phys_bof);

	if ((err = dmu_read(mos, spa->spa_history, phys_bof, firstread,
	    buf, DMU_READ_PREFETCH)) != 0)
		return (err);
	if (firstread != sizeof (reclen)) {
		if ((err = dmu_read(mos, spa->spa_history,
		    shpp->sh_pool_create_len, sizeof (reclen) - firstread,
		    buf + firstread, DMU_READ_PREFETCH)) != 0)
			return (err);
	}

	reclen = LE_64(*((uint64_t *)buf));
	shpp->sh_bof += reclen + sizeof (reclen);
	shpp->sh_records_lost++;
	return (0);
}

static int
spa_history_write(spa_t *spa, void *buf, uint64_t len, spa_history_phys_t *shpp,
    dmu_tx_t *tx)
{
	uint64_t firstwrite, phys_eof;
	objset_t *mos = spa->spa_meta_objset;
	int err;

	ASSERT(MUTEX_HELD(&spa->spa_history_lock));

	/* see if we need to reset logical BOF */
	while (shpp->sh_phys_max_off - shpp->sh_pool_create_len -
	    (shpp->sh_eof - shpp->sh_bof) <= len) {
		if ((err = spa_history_advance_bof(spa, shpp)) != 0) {
			return (err);
		}
	}

	phys_eof = spa_history_log_to_phys(shpp->sh_eof, shpp);
	firstwrite = MIN(len, shpp->sh_phys_max_off - phys_eof);
	shpp->sh_eof += len;
	dmu_write(mos, spa->spa_history, phys_eof, firstwrite, buf, tx);

	len -= firstwrite;
	if (len > 0) {
		/* write out the rest at the beginning of physical file */
		dmu_write(mos, spa->spa_history, shpp->sh_pool_create_len,
		    len, (char *)buf + firstwrite, tx);
	}

	return (0);
}

static char *
spa_history_zone()
{
#ifdef _KERNEL
	return (curproc->p_zone->zone_name);
#else
	return ("global");
#endif
}

/*
 * Write out a history event.
 */
static void
spa_history_log_sync(void *arg1, void *arg2, cred_t *cr, dmu_tx_t *tx)
{
	spa_t		*spa = arg1;
	history_arg_t	*hap = arg2;
	const char	*history_str = hap->ha_history_str;
	objset_t	*mos = spa->spa_meta_objset;
	dmu_buf_t	*dbp;
	spa_history_phys_t *shpp;
	size_t		reclen;
	uint64_t	le_len;
	nvlist_t	*nvrecord;
	char		*record_packed = NULL;
	int		ret;

	/*
	 * If we have an older pool that doesn't have a command
	 * history object, create it now.
	 */
	mutex_enter(&spa->spa_history_lock);
	if (!spa->spa_history)
		spa_history_create_obj(spa, tx);
	mutex_exit(&spa->spa_history_lock);

	/*
	 * Get the offset of where we need to write via the bonus buffer.
	 * Update the offset when the write completes.
	 */
	VERIFY(0 == dmu_bonus_hold(mos, spa->spa_history, FTAG, &dbp));
	shpp = dbp->db_data;

	dmu_buf_will_dirty(dbp, tx);

#ifdef ZFS_DEBUG
	{
		dmu_object_info_t doi;
		dmu_object_info_from_db(dbp, &doi);
		ASSERT3U(doi.doi_bonus_type, ==, DMU_OT_SPA_HISTORY_OFFSETS);
	}
#endif

	VERIFY(nvlist_alloc(&nvrecord, NV_UNIQUE_NAME, KM_SLEEP) == 0);
	VERIFY(nvlist_add_uint64(nvrecord, ZPOOL_HIST_TIME,
	    gethrestime_sec()) == 0);
	VERIFY(nvlist_add_uint64(nvrecord, ZPOOL_HIST_WHO,
	    (uint64_t)crgetuid(cr)) == 0);
	if (hap->ha_zone[0] != '\0')
		VERIFY(nvlist_add_string(nvrecord, ZPOOL_HIST_ZONE,
		    hap->ha_zone) == 0);
#ifdef _KERNEL
	VERIFY(nvlist_add_string(nvrecord, ZPOOL_HIST_HOST,
	    utsname.nodename) == 0);
#endif
	if (hap->ha_log_type == LOG_CMD_POOL_CREATE ||
	    hap->ha_log_type == LOG_CMD_NORMAL) {
		VERIFY(nvlist_add_string(nvrecord, ZPOOL_HIST_CMD,
		    history_str) == 0);
	} else {
		VERIFY(nvlist_add_uint64(nvrecord, ZPOOL_HIST_INT_EVENT,
		    hap->ha_event) == 0);
		VERIFY(nvlist_add_uint64(nvrecord, ZPOOL_HIST_TXG,
		    tx->tx_txg) == 0);
		VERIFY(nvlist_add_string(nvrecord, ZPOOL_HIST_INT_STR,
		    history_str) == 0);
	}

	VERIFY(nvlist_size(nvrecord, &reclen, NV_ENCODE_XDR) == 0);
	record_packed = kmem_alloc(reclen, KM_SLEEP);

	VERIFY(nvlist_pack(nvrecord, &record_packed, &reclen,
	    NV_ENCODE_XDR, KM_SLEEP) == 0);

	mutex_enter(&spa->spa_history_lock);
	if (hap->ha_log_type == LOG_CMD_POOL_CREATE)
		VERIFY(shpp->sh_eof == shpp->sh_pool_create_len);

	/* write out the packed length as little endian */
	le_len = LE_64((uint64_t)reclen);
	ret = spa_history_write(spa, &le_len, sizeof (le_len), shpp, tx);
	if (!ret)
		ret = spa_history_write(spa, record_packed, reclen, shpp, tx);

	if (!ret && hap->ha_log_type == LOG_CMD_POOL_CREATE) {
		shpp->sh_pool_create_len += sizeof (le_len) + reclen;
		shpp->sh_bof = shpp->sh_pool_create_len;
	}

	mutex_exit(&spa->spa_history_lock);
	nvlist_free(nvrecord);
	kmem_free(record_packed, reclen);
	dmu_buf_rele(dbp, FTAG);

	if (hap->ha_log_type == LOG_INTERNAL) {
		kmem_free((void*)hap->ha_history_str, HIS_MAX_RECORD_LEN);
		kmem_free(hap, sizeof (history_arg_t));
	}
}

/*
 * Write out a history event.
 */
int
spa_history_log(spa_t *spa, const char *history_str, history_log_type_t what)
{
	history_arg_t ha;

	ASSERT(what != LOG_INTERNAL);

	ha.ha_history_str = history_str;
	ha.ha_log_type = what;
	(void) strlcpy(ha.ha_zone, spa_history_zone(), sizeof (ha.ha_zone));
	return (dsl_sync_task_do(spa_get_dsl(spa), NULL, spa_history_log_sync,
	    spa, &ha, 0));
}

/*
 * Read out the command history.
 */
int
spa_history_get(spa_t *spa, uint64_t *offp, uint64_t *len, char *buf)
{
	objset_t *mos = spa->spa_meta_objset;
	dmu_buf_t *dbp;
	uint64_t read_len, phys_read_off, phys_eof;
	uint64_t leftover = 0;
	spa_history_phys_t *shpp;
	int err;

	/*
	 * If the command history  doesn't exist (older pool),
	 * that's ok, just return ENOENT.
	 */
	if (!spa->spa_history)
		return (ENOENT);

	if ((err = dmu_bonus_hold(mos, spa->spa_history, FTAG, &dbp)) != 0)
		return (err);
	shpp = dbp->db_data;

#ifdef ZFS_DEBUG
	{
		dmu_object_info_t doi;
		dmu_object_info_from_db(dbp, &doi);
		ASSERT3U(doi.doi_bonus_type, ==, DMU_OT_SPA_HISTORY_OFFSETS);
	}
#endif

	mutex_enter(&spa->spa_history_lock);
	phys_eof = spa_history_log_to_phys(shpp->sh_eof, shpp);

	if (*offp < shpp->sh_pool_create_len) {
		/* read in just the zpool create history */
		phys_read_off = *offp;
		read_len = MIN(*len, shpp->sh_pool_create_len -
		    phys_read_off);
	} else {
		/*
		 * Need to reset passed in offset to BOF if the passed in
		 * offset has since been overwritten.
		 */
		*offp = MAX(*offp, shpp->sh_bof);
		phys_read_off = spa_history_log_to_phys(*offp, shpp);

		/*
		 * Read up to the minimum of what the user passed down or
		 * the EOF (physical or logical).  If we hit physical EOF,
		 * use 'leftover' to read from the physical BOF.
		 */
		if (phys_read_off <= phys_eof) {
			read_len = MIN(*len, phys_eof - phys_read_off);
		} else {
			read_len = MIN(*len,
			    shpp->sh_phys_max_off - phys_read_off);
			if (phys_read_off + *len > shpp->sh_phys_max_off) {
				leftover = MIN(*len - read_len,
				    phys_eof - shpp->sh_pool_create_len);
			}
		}
	}

	/* offset for consumer to use next */
	*offp += read_len + leftover;

	/* tell the consumer how much you actually read */
	*len = read_len + leftover;

	if (read_len == 0) {
		mutex_exit(&spa->spa_history_lock);
		dmu_buf_rele(dbp, FTAG);
		return (0);
	}

	err = dmu_read(mos, spa->spa_history, phys_read_off, read_len, buf,
	    DMU_READ_PREFETCH);
	if (leftover && err == 0) {
		err = dmu_read(mos, spa->spa_history, shpp->sh_pool_create_len,
		    leftover, buf + read_len, DMU_READ_PREFETCH);
	}
	mutex_exit(&spa->spa_history_lock);

	dmu_buf_rele(dbp, FTAG);
	return (err);
}

static void
log_internal(history_internal_events_t event, spa_t *spa,
    dmu_tx_t *tx, cred_t *cr, const char *fmt, va_list adx)
{
	history_arg_t *hap;
	char *str;

	/*
	 * If this is part of creating a pool, not everything is
	 * initialized yet, so don't bother logging the internal events.
	 */
	if (tx->tx_txg == TXG_INITIAL)
		return;

	hap = kmem_alloc(sizeof (history_arg_t), KM_SLEEP);
	str = kmem_alloc(HIS_MAX_RECORD_LEN, KM_SLEEP);

	(void) vsnprintf(str, HIS_MAX_RECORD_LEN, fmt, adx);

	hap->ha_log_type = LOG_INTERNAL;
	hap->ha_history_str = str;
	hap->ha_event = event;
	hap->ha_zone[0] = '\0';

	if (dmu_tx_is_syncing(tx)) {
		spa_history_log_sync(spa, hap, cr, tx);
	} else {
		dsl_sync_task_do_nowait(spa_get_dsl(spa), NULL,
		    spa_history_log_sync, spa, hap, 0, tx);
	}
	/* spa_history_log_sync() will free hap and str */
}

void
spa_history_internal_log(history_internal_events_t event, spa_t *spa,
    dmu_tx_t *tx, cred_t *cr, const char *fmt, ...)
{
	dmu_tx_t *htx = tx;
	va_list adx;

	/* create a tx if we didn't get one */
	if (tx == NULL) {
		htx = dmu_tx_create_dd(spa_get_dsl(spa)->dp_mos_dir);
		if (dmu_tx_assign(htx, TXG_WAIT) != 0) {
			dmu_tx_abort(htx);
			return;
		}
	}

	va_start(adx, fmt);
	log_internal(event, spa, htx, cr, fmt, adx);
	va_end(adx);

	/* if we didn't get a tx from the caller, commit the one we made */
	if (tx == NULL)
		dmu_tx_commit(htx);
}

void
spa_history_log_version(spa_t *spa, history_internal_events_t event)
{
#ifdef _KERNEL
	uint64_t current_vers = spa_version(spa);

	if (current_vers >= SPA_VERSION_ZPOOL_HISTORY) {
		spa_history_internal_log(event, spa, NULL, CRED(),
		    "pool spa %llu; zfs spa %llu; zpl %d; uts %s %s %s %s",
		    (u_longlong_t)current_vers, SPA_VERSION, ZPL_VERSION,
		    utsname.nodename, utsname.release, utsname.version,
		    utsname.machine);
	}
	cmn_err(CE_CONT, "!%s version %llu pool %s using %llu",
	    event == LOG_POOL_IMPORT ? "imported" :
	    event == LOG_POOL_CREATE ? "created" : "accessed",
	    (u_longlong_t)current_vers, spa_name(spa), SPA_VERSION);
#endif
}
