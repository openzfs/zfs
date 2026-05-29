// SPDX-License-Identifier: CDDL-1.0
/*
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this
 * source.  A copy of the CDDL is also available via the Internet at
 * http://www.illumos.org/license/CDDL.
 */

/*
 * Copyright (c) 2026, TrueNAS.
 */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/zfs_context.h>
#include <sys/dmu.h>
#include <sys/dmu_tx.h>
#include <sys/dnode.h>
#include <sys/dsl_dataset.h>
#include <sys/spa.h>
#include <sys/zfeature.h>

#include "mock_dmu.h"
#include "unit.h"

/*
 * A mock dbuf. A real dmu_buf_t (first for casting) plus the attached user
 * data pointer. Block data is stored in a separate allocation so that the
 * struct address remains stable across block resizes.
 */
struct mock_dbuf {
	dmu_buf_t		mdb_db;
	dmu_buf_user_t		*mdb_user;
	mock_dnode_t		*mdb_owner;
	void			*mdb_data;
};
typedef struct mock_dbuf mock_dbuf_t;

/*
 * A mock dnode. a real dnode_t (must be first for casting) with dn_type
 * and dn_object set, plus a flat array of mock_dbuf_t indexed by block id.
 */
struct mock_dnode {
	dnode_t			mdn_dn;
	uint64_t		mdn_refcount;
	size_t			mdn_blksize;
	size_t			mdn_nblocks;
	mock_dbuf_t		**mdn_blocks;
};

/*
 * A mock transaction. We only allocate and zero it, nothing currently uses
 * any of its internals.
 */
struct mock_dmu_tx {
	dmu_tx_t		mtx_tx;
};

/* Mock dnode */

static mock_dbuf_t *
mock_dnode_block_alloc(mock_dnode_t *mdn, uint64_t blkid)
{
	mock_dbuf_t *mdb = kmem_zalloc(sizeof (mock_dbuf_t), KM_SLEEP);
	mdb->mdb_data = kmem_zalloc(mdn->mdn_blksize, KM_SLEEP);

	mdb->mdb_db.db_object = mdn->mdn_dn.dn_object;
	mdb->mdb_db.db_offset = blkid * mdn->mdn_blksize;
	mdb->mdb_db.db_size   = mdn->mdn_blksize;
	mdb->mdb_db.db_data   = mdb->mdb_data;
	mdb->mdb_owner = mdn;

	return (mdb);
}

/* Grow the dbuf array if needed, then return (or create) the dbuf for blkid. */
static mock_dbuf_t *
mock_dnode_block_get(mock_dnode_t *mdn, uint64_t blkid)
{
	if (blkid >= mdn->mdn_nblocks) {
		size_t new_n = blkid + 1;
		mock_dbuf_t **new_blocks =
		    kmem_zalloc(new_n * sizeof (mock_dbuf_t *), KM_SLEEP);
		if (mdn->mdn_blocks != NULL) {
			memcpy(new_blocks, mdn->mdn_blocks,
			    mdn->mdn_nblocks * sizeof (mock_dbuf_t *));
			kmem_free(mdn->mdn_blocks,
			    mdn->mdn_nblocks * sizeof (mock_dbuf_t *));
		}
		mdn->mdn_blocks = new_blocks;
		mdn->mdn_nblocks = new_n;
	}

	mock_dbuf_t *mdb = mdn->mdn_blocks[blkid];
	if (mdb == NULL) {
		mdb = mock_dnode_block_alloc(mdn, blkid);
		mdn->mdn_blocks[blkid] = mdb;
	}
	return (mdb);
}

mock_dnode_t *
mock_dnode_create(size_t blksize, dmu_object_type_t type)
{
	ASSERT(IS_P2ALIGNED(blksize, 512));

	mock_dnode_t *mdn = kmem_zalloc(sizeof (mock_dnode_t), KM_SLEEP);
	mdn->mdn_refcount = 1;
	mdn->mdn_dn.dn_type = type;
	mdn->mdn_dn.dn_object = 1;	/* arbitrary non-zero object number */
	mdn->mdn_blksize = blksize;

	return (mdn);
}

void
mock_dnode_destroy(mock_dnode_t *mdn)
{
	for (size_t i = 0; i < mdn->mdn_nblocks; i++) {
		mock_dbuf_t *mdb = mdn->mdn_blocks[i];
		if (mdb == NULL)
			continue;

		/*
		 * Call the sync evict callback if one is set, mimicking the
		 * real DMU when a buffer's refcount drops to zero.
		 */
		if (mdb->mdb_user != NULL &&
		    mdb->mdb_user->dbu_evict_func_sync != NULL)
			mdb->mdb_user->dbu_evict_func_sync(mdb->mdb_user);

		kmem_free(mdb->mdb_data, mdb->mdb_db.db_size);
		kmem_free(mdb, sizeof (mock_dbuf_t));
	}

	kmem_free(mdn->mdn_blocks,
	    mdn->mdn_nblocks * sizeof (mock_dbuf_t *));
	kmem_free(mdn, sizeof (mock_dnode_t));
}

size_t
mock_dnode_block_count(mock_dnode_t *mdn)
{
	return (mdn->mdn_nblocks);
}

const void *
mock_dnode_block_data(mock_dnode_t *mdn, uint64_t blkid)
{
	if (blkid >= mdn->mdn_nblocks)
		return (NULL);
	return (mdn->mdn_blocks[blkid]->mdb_db.db_data);
}

uint64_t
mock_dnode_refcount(mock_dnode_t *mdn)
{
	return (mdn->mdn_refcount);
}

/* Mock transaction */

mock_dmu_tx_t *
mock_tx_create(void)
{
	return (kmem_zalloc(sizeof (mock_dmu_tx_t), KM_SLEEP));
}

void
mock_tx_destroy(mock_dmu_tx_t *tx)
{
	kmem_free(tx, sizeof (mock_dmu_tx_t));
}

/* DMU stubs, either no-op or light access to mock dnode internals. */

int
dmu_buf_hold_by_dnode(dnode_t *dn, uint64_t offset, const void *tag,
    dmu_buf_t **dbp, dmu_flags_t flags)
{
	(void) tag; (void) flags;

	mock_dnode_t *mdn = (mock_dnode_t *)dn;
	uint64_t blkid = offset / mdn->mdn_blksize;
	mock_dbuf_t *mdb = mock_dnode_block_get(mdn, blkid);

	*dbp = &mdb->mdb_db;
	return (0);
}

void
dmu_buf_rele(dmu_buf_t *db, const void *tag)
{
	(void) db; (void) tag;
}

void *
dmu_buf_get_user(dmu_buf_t *db)
{
	mock_dbuf_t *mdb = (mock_dbuf_t *)db;
	return (mdb->mdb_user);
}

void *
dmu_buf_set_user(dmu_buf_t *db, dmu_buf_user_t *new_user)
{
	mock_dbuf_t *mdb = (mock_dbuf_t *)db;
	if (mdb->mdb_user != NULL)
		return (mdb->mdb_user);	/* existing user wins */
	mdb->mdb_user = new_user;
	return (NULL);			/* new_user wins */
}

void
dmu_buf_will_dirty(dmu_buf_t *db, dmu_tx_t *tx)
{
	(void) db; (void) tx;
}

objset_t *
dmu_buf_get_objset(dmu_buf_t *db)
{
	mock_dbuf_t *mdb = (mock_dbuf_t *)db;

	/*
	 * We return the mock_dnode_t pointer cast to objset_t so that
	 * dmu_object_set_blocksize() below can recover the dnode without
	 * needing a separate objset structure.
	 */
	return ((objset_t *)mdb->mdb_owner);
}

int
dmu_object_set_blocksize(objset_t *os, uint64_t object, uint64_t size,
    int ibs, dmu_tx_t *tx)
{
	(void) object; (void) ibs; (void) tx;

	/* os is a mock_dnode_t (see dmu_buf_get_objset() above). */
	mock_dnode_t *mdn = (mock_dnode_t *)os;

	/*
	 * Resize block 0's data buffer in place so the struct address stays
	 * stable.
	 */
	mock_dbuf_t *mdb = mdn->mdn_blocks[0];
	void *new_data = kmem_zalloc(size, KM_SLEEP);
	memcpy(new_data, mdb->mdb_data,
	    MIN(size, (size_t)mdb->mdb_db.db_size));
	kmem_free(mdb->mdb_data, mdb->mdb_db.db_size);

	mdb->mdb_data = new_data;
	mdb->mdb_db.db_size = size;
	mdb->mdb_db.db_data = new_data;
	mdn->mdn_blksize = size;

	return (0);
}

boolean_t
dnode_add_ref(dnode_t *dn, const void *tag)
{
	(void) tag;
	mock_dnode_t *mdn = (mock_dnode_t *)dn;
	if (mdn->mdn_refcount == 0)
		return (B_FALSE);
	mdn->mdn_refcount++;
	return (B_TRUE);
}

void
dnode_rele(dnode_t *dn, const void *tag)
{
	(void) tag;
	mock_dnode_t *mdn = (mock_dnode_t *)dn;
	unit_gt(mdn->mdn_refcount, 0);
	mdn->mdn_refcount--;
}

/*
 * Misc other stubs. Not strictly DMU mocks, and might move elsewhere later,
 * but for now this is all we need for our limited test set.
 */

spa_t *
dmu_objset_spa(objset_t *os)
{
	(void) os;
	return (NULL);
}

int
dmu_free_range(objset_t *os, uint64_t object, uint64_t offset,
    uint64_t size, dmu_tx_t *tx)
{
	(void) os; (void) object; (void) offset; (void) size; (void) tx;
	return (0);
}

void
dmu_prefetch_by_dnode(dnode_t *dn, int64_t level, uint64_t offset,
    uint64_t len, zio_priority_t pri)
{
	(void) dn; (void) level; (void) offset; (void) len; (void) pri;
}

dsl_dataset_t *
dmu_objset_ds(objset_t *os)
{
	(void) os;
	return (NULL);
}

boolean_t
dsl_dataset_feature_is_active(dsl_dataset_t *ds, spa_feature_t f)
{
	(void) ds; (void) f;
	return (B_FALSE);
}

void
dsl_dataset_dirty(dsl_dataset_t *ds, dmu_tx_t *tx)
{
	(void) ds; (void) tx;
}

boolean_t
spa_feature_is_enabled(spa_t *spa, spa_feature_t f)
{
	(void) spa; (void) f;
	return (B_FALSE);
}

int
spa_maxblocksize(spa_t *spa)
{
	(void) spa;
	return (SPA_OLD_MAXBLOCKSIZE);
}

const dmu_object_type_info_t dmu_ot[DMU_OT_NUMTYPES];

void
byteswap_uint64_array(void *buf, size_t size)
{
	(void) buf; (void) size;
}

/*
 * Various objset+object calls; returning error, as they need to use
 * _by_dnode() variants to get the mock.
 */
int
dnode_hold(objset_t *os, uint64_t object, const void *tag, dnode_t **dnp)
{
	(void) os; (void) object; (void) tag; (void) dnp;
	return (EIO);
}

int
dmu_object_free(objset_t *os, uint64_t object, dmu_tx_t *tx)
{
	(void) os; (void) object; (void) tx;
	return (EIO);
}

uint64_t
dmu_object_alloc_hold(objset_t *os, dmu_object_type_t ot,
    int blocksize, int indirect_blockshift, dmu_object_type_t bonustype,
    int bonuslen, int dnodesize, dnode_t **allocated_dnode,
    const void *tag, dmu_tx_t *tx)
{
	(void) os; (void) ot; (void) blocksize; (void) indirect_blockshift;
	(void) bonustype; (void) bonuslen; (void) dnodesize;
	(void) allocated_dnode; (void) tag; (void) tx;
	return (EIO);
}

int
dmu_object_claim_dnsize(objset_t *os, uint64_t object, dmu_object_type_t ot,
    int blocksize, dmu_object_type_t bonus_type, int bonus_len,
    int dnodesize, dmu_tx_t *tx)
{
	(void) os; (void) object; (void) ot; (void) blocksize;
	(void) bonus_type; (void) bonus_len; (void) dnodesize; (void) tx;
	return (EIO);
}

int
dmu_object_info(objset_t *os, uint64_t object, dmu_object_info_t *doi)
{
	(void) os; (void) object; (void) doi;
	return (EIO);
}

int
dmu_prefetch_wait(objset_t *os, uint64_t object, uint64_t offset,
    uint64_t len)
{
	(void) os; (void) object; (void) offset; (void) len;
	return (EIO);
}
