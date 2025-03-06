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
 * Copyright (c) 2023, Klara Inc.
 */

#include <sys/zfs_context.h>
#include <sys/spa.h>
#include <sys/ddt.h>
#include <sys/dmu_tx.h>
#include <sys/dmu.h>
#include <sys/ddt_impl.h>
#include <sys/dnode.h>
#include <sys/dbuf.h>
#include <sys/zap.h>
#include <sys/zio_checksum.h>

/*
 * No more than this many txgs before swapping logs.
 */
uint_t zfs_dedup_log_txg_max = 8;

/*
 * Max memory for the log AVL trees. If zfs_dedup_log_mem_max is zero at module
 * load, it will be set to zfs_dedup_log_mem_max_percent% of total memory.
 */
uint64_t zfs_dedup_log_mem_max = 0;
uint_t zfs_dedup_log_mem_max_percent = 1;


static kmem_cache_t *ddt_log_entry_flat_cache;
static kmem_cache_t *ddt_log_entry_trad_cache;

#define	DDT_LOG_ENTRY_FLAT_SIZE	\
	(sizeof (ddt_log_entry_t) + DDT_FLAT_PHYS_SIZE)
#define	DDT_LOG_ENTRY_TRAD_SIZE	\
	(sizeof (ddt_log_entry_t) + DDT_TRAD_PHYS_SIZE)

#define	DDT_LOG_ENTRY_SIZE(ddt)	\
	_DDT_PHYS_SWITCH(ddt, DDT_LOG_ENTRY_FLAT_SIZE, DDT_LOG_ENTRY_TRAD_SIZE)

void
ddt_log_init(void)
{
	ddt_log_entry_flat_cache = kmem_cache_create("ddt_log_entry_flat_cache",
	    DDT_LOG_ENTRY_FLAT_SIZE, 0, NULL, NULL, NULL, NULL, NULL, 0);
	ddt_log_entry_trad_cache = kmem_cache_create("ddt_log_entry_trad_cache",
	    DDT_LOG_ENTRY_TRAD_SIZE, 0, NULL, NULL, NULL, NULL, NULL, 0);

	/*
	 * Max memory for log AVL entries. At least 1M, because we need
	 * something (that's ~3800 entries per tree). They can say 100% if they
	 * want; it just means they're at the mercy of the the txg flush limit.
	 */
	if (zfs_dedup_log_mem_max == 0) {
		zfs_dedup_log_mem_max_percent =
		    MIN(zfs_dedup_log_mem_max_percent, 100);
		zfs_dedup_log_mem_max = (physmem * PAGESIZE) *
		    zfs_dedup_log_mem_max_percent / 100;
	}
	zfs_dedup_log_mem_max = MAX(zfs_dedup_log_mem_max, 1*1024*1024);
}

void
ddt_log_fini(void)
{
	kmem_cache_destroy(ddt_log_entry_trad_cache);
	kmem_cache_destroy(ddt_log_entry_flat_cache);
}

static void
ddt_log_name(ddt_t *ddt, char *name, uint_t n)
{
	snprintf(name, DDT_NAMELEN, DMU_POOL_DDT_LOG,
	    zio_checksum_table[ddt->ddt_checksum].ci_name, n);
}

static void
ddt_log_update_header(ddt_t *ddt, ddt_log_t *ddl, dmu_tx_t *tx)
{
	dmu_buf_t *db;
	VERIFY0(dmu_bonus_hold(ddt->ddt_os, ddl->ddl_object, FTAG, &db));
	dmu_buf_will_dirty(db, tx);

	ddt_log_header_t *hdr = (ddt_log_header_t *)db->db_data;
	DLH_SET_VERSION(hdr, 1);
	DLH_SET_FLAGS(hdr, ddl->ddl_flags);
	hdr->dlh_length = ddl->ddl_length;
	hdr->dlh_first_txg = ddl->ddl_first_txg;
	hdr->dlh_checkpoint = ddl->ddl_checkpoint;

	dmu_buf_rele(db, FTAG);
}

static void
ddt_log_create_one(ddt_t *ddt, ddt_log_t *ddl, uint_t n, dmu_tx_t *tx)
{
	ASSERT3U(ddt->ddt_dir_object, >, 0);
	ASSERT3U(ddl->ddl_object, ==, 0);

	char name[DDT_NAMELEN];
	ddt_log_name(ddt, name, n);

	ddl->ddl_object = dmu_object_alloc(ddt->ddt_os,
	    DMU_OTN_UINT64_METADATA, SPA_OLD_MAXBLOCKSIZE,
	    DMU_OTN_UINT64_METADATA, sizeof (ddt_log_header_t), tx);
	VERIFY0(zap_add(ddt->ddt_os, ddt->ddt_dir_object, name,
	    sizeof (uint64_t), 1, &ddl->ddl_object, tx));
	ddl->ddl_length = 0;
	ddl->ddl_first_txg = tx->tx_txg;
	ddt_log_update_header(ddt, ddl, tx);
}

static void
ddt_log_create(ddt_t *ddt, dmu_tx_t *tx)
{
	ddt_log_create_one(ddt, ddt->ddt_log_active, 0, tx);
	ddt_log_create_one(ddt, ddt->ddt_log_flushing, 1, tx);
}

static void
ddt_log_destroy_one(ddt_t *ddt, ddt_log_t *ddl, uint_t n, dmu_tx_t *tx)
{
	ASSERT3U(ddt->ddt_dir_object, >, 0);

	if (ddl->ddl_object == 0)
		return;

	ASSERT0(ddl->ddl_length);

	char name[DDT_NAMELEN];
	ddt_log_name(ddt, name, n);

	VERIFY0(zap_remove(ddt->ddt_os, ddt->ddt_dir_object, name, tx));
	VERIFY0(dmu_object_free(ddt->ddt_os, ddl->ddl_object, tx));

	ddl->ddl_object = 0;
}

void
ddt_log_destroy(ddt_t *ddt, dmu_tx_t *tx)
{
	ddt_log_destroy_one(ddt, ddt->ddt_log_active, 0, tx);
	ddt_log_destroy_one(ddt, ddt->ddt_log_flushing, 1, tx);
}

static void
ddt_log_update_stats(ddt_t *ddt)
{
	/*
	 * Log object stats. We count the number of live entries in the log
	 * tree, even if there are more than on disk, and even if the same
	 * entry is on both append and flush trees, because that's more what
	 * the user expects to see. This does mean the on-disk size is not
	 * really correlated with the number of entries, but I don't think
	 * that's reasonable to expect anyway.
	 */
	dmu_object_info_t doi;
	uint64_t nblocks;
	dmu_object_info(ddt->ddt_os, ddt->ddt_log_active->ddl_object, &doi);
	nblocks = doi.doi_physical_blocks_512;
	dmu_object_info(ddt->ddt_os, ddt->ddt_log_flushing->ddl_object, &doi);
	nblocks += doi.doi_physical_blocks_512;

	ddt_object_t *ddo = &ddt->ddt_log_stats;
	ddo->ddo_count =
	    avl_numnodes(&ddt->ddt_log_active->ddl_tree) +
	    avl_numnodes(&ddt->ddt_log_flushing->ddl_tree);
	ddo->ddo_mspace = ddo->ddo_count * DDT_LOG_ENTRY_SIZE(ddt);
	ddo->ddo_dspace = nblocks << 9;
}

void
ddt_log_begin(ddt_t *ddt, size_t nentries, dmu_tx_t *tx, ddt_log_update_t *dlu)
{
	ASSERT3U(nentries, >, 0);
	ASSERT3P(dlu->dlu_dbp, ==, NULL);

	if (ddt->ddt_log_active->ddl_object == 0)
		ddt_log_create(ddt, tx);

	/*
	 * We want to store as many entries as we can in a block, but never
	 * split an entry across block boundaries.
	 */
	size_t reclen = P2ALIGN_TYPED(
	    sizeof (ddt_log_record_t) + sizeof (ddt_log_record_entry_t) +
	    DDT_PHYS_SIZE(ddt), sizeof (uint64_t), size_t);
	ASSERT3U(reclen, <=, UINT16_MAX);
	dlu->dlu_reclen = reclen;

	VERIFY0(dnode_hold(ddt->ddt_os, ddt->ddt_log_active->ddl_object, FTAG,
	    &dlu->dlu_dn));
	dnode_set_storage_type(dlu->dlu_dn, DMU_OT_DDT_ZAP);

	uint64_t nblocks = howmany(nentries,
	    dlu->dlu_dn->dn_datablksz / dlu->dlu_reclen);
	uint64_t offset = ddt->ddt_log_active->ddl_length;
	uint64_t length = nblocks * dlu->dlu_dn->dn_datablksz;

	VERIFY0(dmu_buf_hold_array_by_dnode(dlu->dlu_dn, offset, length,
	    B_FALSE, FTAG, &dlu->dlu_ndbp, &dlu->dlu_dbp,
	    DMU_READ_NO_PREFETCH));

	dlu->dlu_tx = tx;
	dlu->dlu_block = dlu->dlu_offset = 0;
}

static ddt_log_entry_t *
ddt_log_alloc_entry(ddt_t *ddt)
{
	ddt_log_entry_t *ddle;

	if (ddt->ddt_flags & DDT_FLAG_FLAT) {
		ddle = kmem_cache_alloc(ddt_log_entry_flat_cache, KM_SLEEP);
		memset(ddle, 0, DDT_LOG_ENTRY_FLAT_SIZE);
	} else {
		ddle = kmem_cache_alloc(ddt_log_entry_trad_cache, KM_SLEEP);
		memset(ddle, 0, DDT_LOG_ENTRY_TRAD_SIZE);
	}

	return (ddle);
}

static void
ddt_log_update_entry(ddt_t *ddt, ddt_log_t *ddl, ddt_lightweight_entry_t *ddlwe)
{
	/* Create the log tree entry from a live or stored entry */
	avl_index_t where;
	ddt_log_entry_t *ddle =
	    avl_find(&ddl->ddl_tree, &ddlwe->ddlwe_key, &where);
	if (ddle == NULL) {
		ddle = ddt_log_alloc_entry(ddt);
		ddle->ddle_key = ddlwe->ddlwe_key;
		avl_insert(&ddl->ddl_tree, ddle, where);
	}
	ddle->ddle_type = ddlwe->ddlwe_type;
	ddle->ddle_class = ddlwe->ddlwe_class;
	memcpy(ddle->ddle_phys, &ddlwe->ddlwe_phys, DDT_PHYS_SIZE(ddt));
}

void
ddt_log_entry(ddt_t *ddt, ddt_lightweight_entry_t *ddlwe, ddt_log_update_t *dlu)
{
	ASSERT3U(dlu->dlu_dbp, !=, NULL);

	ddt_log_update_entry(ddt, ddt->ddt_log_active, ddlwe);
	ddt_histogram_add_entry(ddt, &ddt->ddt_log_histogram, ddlwe);

	/* Get our block */
	ASSERT3U(dlu->dlu_block, <, dlu->dlu_ndbp);
	dmu_buf_t *db = dlu->dlu_dbp[dlu->dlu_block];

	/*
	 * If this would take us past the end of the block, finish it and
	 * move to the next one.
	 */
	if (db->db_size < (dlu->dlu_offset + dlu->dlu_reclen)) {
		ASSERT3U(dlu->dlu_offset, >, 0);
		dmu_buf_fill_done(db, dlu->dlu_tx, B_FALSE);
		dlu->dlu_block++;
		dlu->dlu_offset = 0;
		ASSERT3U(dlu->dlu_block, <, dlu->dlu_ndbp);
		db = dlu->dlu_dbp[dlu->dlu_block];
	}

	/*
	 * If this is the first time touching the block, inform the DMU that
	 * we will fill it, and zero it out.
	 */
	if (dlu->dlu_offset == 0) {
		dmu_buf_will_fill(db, dlu->dlu_tx, B_FALSE);
		memset(db->db_data, 0, db->db_size);
	}

	/* Create the log record directly in the buffer */
	ddt_log_record_t *dlr = (db->db_data + dlu->dlu_offset);
	DLR_SET_TYPE(dlr, DLR_ENTRY);
	DLR_SET_RECLEN(dlr, dlu->dlu_reclen);
	DLR_SET_ENTRY_TYPE(dlr, ddlwe->ddlwe_type);
	DLR_SET_ENTRY_CLASS(dlr, ddlwe->ddlwe_class);

	ddt_log_record_entry_t *dlre =
	    (ddt_log_record_entry_t *)&dlr->dlr_payload;
	dlre->dlre_key = ddlwe->ddlwe_key;
	memcpy(dlre->dlre_phys, &ddlwe->ddlwe_phys, DDT_PHYS_SIZE(ddt));

	/* Advance offset for next record. */
	dlu->dlu_offset += dlu->dlu_reclen;
}

void
ddt_log_commit(ddt_t *ddt, ddt_log_update_t *dlu)
{
	ASSERT3U(dlu->dlu_dbp, !=, NULL);
	ASSERT3U(dlu->dlu_block+1, ==, dlu->dlu_ndbp);
	ASSERT3U(dlu->dlu_offset, >, 0);

	/*
	 * Close out the last block. Whatever we haven't used will be zeroed,
	 * which matches DLR_INVALID, so we can detect this during load.
	 */
	dmu_buf_fill_done(dlu->dlu_dbp[dlu->dlu_block], dlu->dlu_tx, B_FALSE);

	dmu_buf_rele_array(dlu->dlu_dbp, dlu->dlu_ndbp, FTAG);

	ddt->ddt_log_active->ddl_length +=
	    dlu->dlu_ndbp * (uint64_t)dlu->dlu_dn->dn_datablksz;
	dnode_rele(dlu->dlu_dn, FTAG);

	ddt_log_update_header(ddt, ddt->ddt_log_active, dlu->dlu_tx);

	memset(dlu, 0, sizeof (ddt_log_update_t));

	ddt_log_update_stats(ddt);
}

boolean_t
ddt_log_take_first(ddt_t *ddt, ddt_log_t *ddl, ddt_lightweight_entry_t *ddlwe)
{
	ddt_log_entry_t *ddle = avl_first(&ddl->ddl_tree);
	if (ddle == NULL)
		return (B_FALSE);

	DDT_LOG_ENTRY_TO_LIGHTWEIGHT(ddt, ddle, ddlwe);

	ddt_histogram_sub_entry(ddt, &ddt->ddt_log_histogram, ddlwe);

	avl_remove(&ddl->ddl_tree, ddle);
	kmem_cache_free(ddt->ddt_flags & DDT_FLAG_FLAT ?
	    ddt_log_entry_flat_cache : ddt_log_entry_trad_cache, ddle);

	return (B_TRUE);
}

boolean_t
ddt_log_remove_key(ddt_t *ddt, ddt_log_t *ddl, const ddt_key_t *ddk)
{
	ddt_log_entry_t *ddle = avl_find(&ddl->ddl_tree, ddk, NULL);
	if (ddle == NULL)
		return (B_FALSE);

	ddt_lightweight_entry_t ddlwe;
	DDT_LOG_ENTRY_TO_LIGHTWEIGHT(ddt, ddle, &ddlwe);
	ddt_histogram_sub_entry(ddt, &ddt->ddt_log_histogram, &ddlwe);

	avl_remove(&ddl->ddl_tree, ddle);
	kmem_cache_free(ddt->ddt_flags & DDT_FLAG_FLAT ?
	    ddt_log_entry_flat_cache : ddt_log_entry_trad_cache, ddle);

	return (B_TRUE);
}

boolean_t
ddt_log_find_key(ddt_t *ddt, const ddt_key_t *ddk,
    ddt_lightweight_entry_t *ddlwe)
{
	ddt_log_entry_t *ddle =
	    avl_find(&ddt->ddt_log_active->ddl_tree, ddk, NULL);
	if (!ddle)
		ddle = avl_find(&ddt->ddt_log_flushing->ddl_tree, ddk, NULL);
	if (!ddle)
		return (B_FALSE);
	if (ddlwe)
		DDT_LOG_ENTRY_TO_LIGHTWEIGHT(ddt, ddle, ddlwe);
	return (B_TRUE);
}

void
ddt_log_checkpoint(ddt_t *ddt, ddt_lightweight_entry_t *ddlwe, dmu_tx_t *tx)
{
	ddt_log_t *ddl = ddt->ddt_log_flushing;

	ASSERT3U(ddl->ddl_object, !=, 0);

#ifdef ZFS_DEBUG
	/*
	 * There should not be any entries on the log tree before the given
	 * checkpoint. Assert that this is the case.
	 */
	ddt_log_entry_t *ddle = avl_first(&ddl->ddl_tree);
	if (ddle != NULL)
		VERIFY3U(ddt_key_compare(&ddle->ddle_key, &ddlwe->ddlwe_key),
		    >, 0);
#endif

	ddl->ddl_flags |= DDL_FLAG_CHECKPOINT;
	ddl->ddl_checkpoint = ddlwe->ddlwe_key;
	ddt_log_update_header(ddt, ddl, tx);

	ddt_log_update_stats(ddt);
}

void
ddt_log_truncate(ddt_t *ddt, dmu_tx_t *tx)
{
	ddt_log_t *ddl = ddt->ddt_log_flushing;

	if (ddl->ddl_object == 0)
		return;

	ASSERT(avl_is_empty(&ddl->ddl_tree));

	/* Eject the entire object */
	dmu_free_range(ddt->ddt_os, ddl->ddl_object, 0, DMU_OBJECT_END, tx);

	ddl->ddl_length = 0;
	ddl->ddl_flags &= ~DDL_FLAG_CHECKPOINT;
	memset(&ddl->ddl_checkpoint, 0, sizeof (ddt_key_t));
	ddt_log_update_header(ddt, ddl, tx);

	ddt_log_update_stats(ddt);
}

boolean_t
ddt_log_swap(ddt_t *ddt, dmu_tx_t *tx)
{
	/* Swap the logs. The old flushing one must be empty */
	VERIFY(avl_is_empty(&ddt->ddt_log_flushing->ddl_tree));

	/*
	 * If there are still blocks on the flushing log, truncate it first.
	 * This can happen if there were entries on the flushing log that were
	 * removed in memory via ddt_lookup(); their vestigal remains are
	 * on disk.
	 */
	if (ddt->ddt_log_flushing->ddl_length > 0)
		ddt_log_truncate(ddt, tx);

	/*
	 * Swap policy. We swap the logs (and so begin flushing) when the
	 * active tree grows too large, or when we haven't swapped it in
	 * some amount of time, or if something has requested the logs be
	 * flushed ASAP (see ddt_walk_init()).
	 */

	/*
	 * The log tree is too large if the memory usage of its entries is over
	 * half of the memory limit. This effectively gives each log tree half
	 * the available memory.
	 */
	const boolean_t too_large =
	    (avl_numnodes(&ddt->ddt_log_active->ddl_tree) *
	    DDT_LOG_ENTRY_SIZE(ddt)) >= (zfs_dedup_log_mem_max >> 1);

	const boolean_t too_old =
	    tx->tx_txg >=
	    (ddt->ddt_log_active->ddl_first_txg +
	    MAX(1, zfs_dedup_log_txg_max));

	const boolean_t force =
	    ddt->ddt_log_active->ddl_first_txg <= ddt->ddt_flush_force_txg;

	if (!(too_large || too_old || force))
		return (B_FALSE);

	ddt_log_t *swap = ddt->ddt_log_active;
	ddt->ddt_log_active = ddt->ddt_log_flushing;
	ddt->ddt_log_flushing = swap;

	ASSERT(ddt->ddt_log_active->ddl_flags & DDL_FLAG_FLUSHING);
	ddt->ddt_log_active->ddl_flags &=
	    ~(DDL_FLAG_FLUSHING | DDL_FLAG_CHECKPOINT);

	ASSERT(!(ddt->ddt_log_flushing->ddl_flags & DDL_FLAG_FLUSHING));
	ddt->ddt_log_flushing->ddl_flags |= DDL_FLAG_FLUSHING;

	ddt->ddt_log_active->ddl_first_txg = tx->tx_txg;

	ddt_log_update_header(ddt, ddt->ddt_log_active, tx);
	ddt_log_update_header(ddt, ddt->ddt_log_flushing, tx);

	ddt_log_update_stats(ddt);

	return (B_TRUE);
}

static inline void
ddt_log_load_entry(ddt_t *ddt, ddt_log_t *ddl, ddt_log_record_t *dlr,
    const ddt_key_t *checkpoint)
{
	ASSERT3U(DLR_GET_TYPE(dlr), ==, DLR_ENTRY);

	ddt_log_record_entry_t *dlre =
	    (ddt_log_record_entry_t *)dlr->dlr_payload;
	if (checkpoint != NULL &&
	    ddt_key_compare(&dlre->dlre_key, checkpoint) <= 0) {
		/* Skip pre-checkpoint entries; they're already flushed. */
		return;
	}

	ddt_lightweight_entry_t ddlwe;
	ddlwe.ddlwe_type = DLR_GET_ENTRY_TYPE(dlr);
	ddlwe.ddlwe_class = DLR_GET_ENTRY_CLASS(dlr);

	ddlwe.ddlwe_key = dlre->dlre_key;
	memcpy(&ddlwe.ddlwe_phys, dlre->dlre_phys, DDT_PHYS_SIZE(ddt));

	ddt_log_update_entry(ddt, ddl, &ddlwe);
}

static void
ddt_log_empty(ddt_t *ddt, ddt_log_t *ddl)
{
	void *cookie = NULL;
	ddt_log_entry_t *ddle;
	IMPLY(ddt->ddt_version == UINT64_MAX, avl_is_empty(&ddl->ddl_tree));
	while ((ddle =
	    avl_destroy_nodes(&ddl->ddl_tree, &cookie)) != NULL) {
		kmem_cache_free(ddt->ddt_flags & DDT_FLAG_FLAT ?
		    ddt_log_entry_flat_cache : ddt_log_entry_trad_cache, ddle);
	}
	ASSERT(avl_is_empty(&ddl->ddl_tree));
}

static int
ddt_log_load_one(ddt_t *ddt, uint_t n)
{
	ASSERT3U(n, <, 2);

	ddt_log_t *ddl = &ddt->ddt_log[n];

	char name[DDT_NAMELEN];
	ddt_log_name(ddt, name, n);

	uint64_t obj;
	int err = zap_lookup(ddt->ddt_os, ddt->ddt_dir_object, name,
	    sizeof (uint64_t), 1, &obj);
	if (err == ENOENT)
		return (0);
	if (err != 0)
		return (err);

	dnode_t *dn;
	err = dnode_hold(ddt->ddt_os, obj, FTAG, &dn);
	if (err != 0)
		return (err);

	ddt_log_header_t hdr;
	dmu_buf_t *db;
	err = dmu_bonus_hold_by_dnode(dn, FTAG, &db, DMU_READ_NO_PREFETCH);
	if (err != 0) {
		dnode_rele(dn, FTAG);
		return (err);
	}
	memcpy(&hdr, db->db_data, sizeof (ddt_log_header_t));
	dmu_buf_rele(db, FTAG);

	if (DLH_GET_VERSION(&hdr) != 1) {
		dnode_rele(dn, FTAG);
		zfs_dbgmsg("ddt_log_load: spa=%s ddt_log=%s "
		    "unknown version=%llu", spa_name(ddt->ddt_spa), name,
		    (u_longlong_t)DLH_GET_VERSION(&hdr));
		return (SET_ERROR(EINVAL));
	}

	ddt_key_t *checkpoint = NULL;
	if (DLH_GET_FLAGS(&hdr) & DDL_FLAG_CHECKPOINT) {
		/*
		 * If the log has a checkpoint, then we can ignore any entries
		 * that have already been flushed.
		 */
		ASSERT(DLH_GET_FLAGS(&hdr) & DDL_FLAG_FLUSHING);
		checkpoint = &hdr.dlh_checkpoint;
	}

	if (hdr.dlh_length > 0) {
		dmu_prefetch_by_dnode(dn, 0, 0, hdr.dlh_length,
		    ZIO_PRIORITY_SYNC_READ);

		for (uint64_t offset = 0; offset < hdr.dlh_length;
		    offset += dn->dn_datablksz) {
			err = dmu_buf_hold_by_dnode(dn, offset, FTAG, &db,
			    DMU_READ_PREFETCH);
			if (err != 0) {
				dnode_rele(dn, FTAG);
				ddt_log_empty(ddt, ddl);
				return (err);
			}

			uint64_t boffset = 0;
			while (boffset < db->db_size) {
				ddt_log_record_t *dlr =
				    (ddt_log_record_t *)(db->db_data + boffset);

				/* Partially-filled block, skip the rest */
				if (DLR_GET_TYPE(dlr) == DLR_INVALID)
					break;

				switch (DLR_GET_TYPE(dlr)) {
				case DLR_ENTRY:
					ddt_log_load_entry(ddt, ddl, dlr,
					    checkpoint);
					break;

				default:
					dmu_buf_rele(db, FTAG);
					dnode_rele(dn, FTAG);
					ddt_log_empty(ddt, ddl);
					return (SET_ERROR(EINVAL));
				}

				boffset += DLR_GET_RECLEN(dlr);
			}

			dmu_buf_rele(db, FTAG);
		}
	}

	dnode_rele(dn, FTAG);

	ddl->ddl_object = obj;
	ddl->ddl_flags = DLH_GET_FLAGS(&hdr);
	ddl->ddl_length = hdr.dlh_length;
	ddl->ddl_first_txg = hdr.dlh_first_txg;

	if (ddl->ddl_flags & DDL_FLAG_FLUSHING)
		ddt->ddt_log_flushing = ddl;
	else
		ddt->ddt_log_active = ddl;

	return (0);
}

int
ddt_log_load(ddt_t *ddt)
{
	int err;

	if (spa_load_state(ddt->ddt_spa) == SPA_LOAD_TRYIMPORT) {
		/*
		 * The DDT is going to be freed again in a moment, so there's
		 * no point loading the log; it'll just slow down import.
		 */
		return (0);
	}

	ASSERT0(ddt->ddt_log[0].ddl_object);
	ASSERT0(ddt->ddt_log[1].ddl_object);
	if (ddt->ddt_dir_object == 0) {
		/*
		 * If we're configured but the containing dir doesn't exist
		 * yet, then the log object can't possibly exist either.
		 */
		ASSERT3U(ddt->ddt_version, !=, UINT64_MAX);
		return (SET_ERROR(ENOENT));
	}

	if ((err = ddt_log_load_one(ddt, 0)) != 0)
		return (err);
	if ((err = ddt_log_load_one(ddt, 1)) != 0)
		return (err);

	VERIFY3P(ddt->ddt_log_active, !=, ddt->ddt_log_flushing);
	VERIFY(!(ddt->ddt_log_active->ddl_flags & DDL_FLAG_FLUSHING));
	VERIFY(!(ddt->ddt_log_active->ddl_flags & DDL_FLAG_CHECKPOINT));
	VERIFY(ddt->ddt_log_flushing->ddl_flags & DDL_FLAG_FLUSHING);

	/*
	 * We have two finalisation tasks:
	 *
	 * - rebuild the histogram. We do this at the end rather than while
	 *   we're loading so we don't need to uncount and recount entries that
	 *   appear multiple times in the log.
	 *
	 * - remove entries from the flushing tree that are on both trees. This
	 *   happens when ddt_lookup() rehydrates an entry from the flushing
	 *   tree, as ddt_log_take_key() removes the entry from the in-memory
	 *   tree but doesn't remove it from disk.
	 */

	/*
	 * We don't technically need a config lock here, since there shouldn't
	 * be pool config changes during DDT load. dva_get_dsize_sync() via
	 * ddt_stat_generate() is expecting it though, and it won't hurt
	 * anything, so we take it.
	 */
	spa_config_enter(ddt->ddt_spa, SCL_STATE, FTAG, RW_READER);

	avl_tree_t *al = &ddt->ddt_log_active->ddl_tree;
	avl_tree_t *fl = &ddt->ddt_log_flushing->ddl_tree;
	ddt_log_entry_t *ae = avl_first(al);
	ddt_log_entry_t *fe = avl_first(fl);
	while (ae != NULL || fe != NULL) {
		ddt_log_entry_t *ddle;
		if (ae == NULL) {
			/* active exhausted, take flushing */
			ddle = fe;
			fe = AVL_NEXT(fl, fe);
		} else if (fe == NULL) {
			/* flushing exuhausted, take active */
			ddle = ae;
			ae = AVL_NEXT(al, ae);
		} else {
			/* compare active and flushing */
			int c = ddt_key_compare(&ae->ddle_key, &fe->ddle_key);
			if (c < 0) {
				/* active behind, take and advance */
				ddle = ae;
				ae = AVL_NEXT(al, ae);
			} else if (c > 0) {
				/* flushing behind, take and advance */
				ddle = fe;
				fe = AVL_NEXT(fl, fe);
			} else {
				/* match. remove from flushing, take active */
				ddle = fe;
				fe = AVL_NEXT(fl, fe);
				avl_remove(fl, ddle);

				ddle = ae;
				ae = AVL_NEXT(al, ae);
			}
		}

		ddt_lightweight_entry_t ddlwe;
		DDT_LOG_ENTRY_TO_LIGHTWEIGHT(ddt, ddle, &ddlwe);
		ddt_histogram_add_entry(ddt, &ddt->ddt_log_histogram, &ddlwe);
	}

	spa_config_exit(ddt->ddt_spa, SCL_STATE, FTAG);

	ddt_log_update_stats(ddt);

	return (0);
}

void
ddt_log_alloc(ddt_t *ddt)
{
	ASSERT3P(ddt->ddt_log_active, ==, NULL);
	ASSERT3P(ddt->ddt_log_flushing, ==, NULL);

	avl_create(&ddt->ddt_log[0].ddl_tree, ddt_key_compare,
	    sizeof (ddt_log_entry_t), offsetof(ddt_log_entry_t, ddle_node));
	avl_create(&ddt->ddt_log[1].ddl_tree, ddt_key_compare,
	    sizeof (ddt_log_entry_t), offsetof(ddt_log_entry_t, ddle_node));
	ddt->ddt_log_active = &ddt->ddt_log[0];
	ddt->ddt_log_flushing = &ddt->ddt_log[1];
	ddt->ddt_log_flushing->ddl_flags |= DDL_FLAG_FLUSHING;
}

void
ddt_log_free(ddt_t *ddt)
{
	ddt_log_empty(ddt, &ddt->ddt_log[0]);
	ddt_log_empty(ddt, &ddt->ddt_log[1]);
	avl_destroy(&ddt->ddt_log[0].ddl_tree);
	avl_destroy(&ddt->ddt_log[1].ddl_tree);
}

ZFS_MODULE_PARAM(zfs_dedup, zfs_dedup_, log_txg_max, UINT, ZMOD_RW,
	"Max transactions before starting to flush dedup logs");

ZFS_MODULE_PARAM(zfs_dedup, zfs_dedup_, log_mem_max, U64, ZMOD_RD,
	"Max memory for dedup logs");

ZFS_MODULE_PARAM(zfs_dedup, zfs_dedup_, log_mem_max_percent, UINT, ZMOD_RD,
	"Max memory for dedup logs, as % of total memory");
