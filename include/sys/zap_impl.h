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
 * Copyright (c) 2005, 2010, Oracle and/or its affiliates. All rights reserved.
 * Copyright (c) 2014 Spectra Logic Corporation, All rights reserved.
 * Copyright (c) 2013, 2016 by Delphix. All rights reserved.
 * Copyright 2017 Nexenta Systems, Inc.
 * Copyright (c) 2024, Klara, Inc.
 * Copyright (c) 2026, TrueNAS.
 * Copyright (c) 2026, Hewlett Packard Enterprise Development LP.
 */

#ifndef	_SYS_ZAP_IMPL_H
#define	_SYS_ZAP_IMPL_H

#include <sys/zap.h>
#include <sys/zfs_context.h>

#ifdef	__cplusplus
extern "C" {
#endif

extern int fzap_default_block_shift;

#define	ZAP_MAGIC 0x2F52AB2ABULL

#define	FZAP_BLOCK_SHIFT(zap)	((zap)->zap_f.zap_block_shift)

#define	MZAP_ENT_LEN		64
#define	MZAP_NAME_LEN		(MZAP_ENT_LEN - 8 - 4 - 2)

#define	ZAP_NEED_CD		(-1U)

/*
 * Flag to detect TinyZAP, a MicroZAP variant.
 */
#define	MZAP_FLAG_TINY	(1 << 0)

/*
 * TinyZAP: a variable-stride, variable-chunk-size variant of MicroZAP.
 *
 * MZAP_FLAG_TINY in mz_flags distinguishes TinyZAP from plain MicroZAP.
 * Chunk size and value width are stored in mz_chunk_shift / mz_value_ints.
 *
 * mzap_phys_t header layout (64 bytes total = MZAP_ENT_LEN):
 * [  0..  7]  mz_block_type   uint64_t   ZBT_MICRO
 * [  8.. 15]  mz_salt         uint64_t
 * [ 16.. 23]  mz_normflags    uint64_t
 * [     24]   mz_flags        uint8_t    MZAP_FLAG_TINY
 * [     25]   mz_chunk_shift  uint8_t    log2(chunk): 6=64, 7=128, 8=256
 * [     26]   mz_value_ints   uint8_t    num_integers; stride = *8
 * [     27]   mz_pad1         uint8_t    zero
 * [ 28.. 31]  mz_pad2         uint32_t   zero
 * [ 32.. 63]  mz_pad3[4]      uint64_t   zero
 *
 * Supported chunk sizes and resulting geometry (examples only).
 * name_len = chunk - stride - 4  (stride = mz_value_ints * 8)
 *
 * chunk | stride | name_len | integers | use-case
 * ------+--------+----------+----------+--------------------------------------
 *   64  |    8   |    52    |    1     | 1×uint64, name up to 51 chars
 *   64  |   16   |    44    |    2     | 2×uint64 (Lustre FID)
 *   64  |   24   |    36    |    3     | 3×uint64
 *   64  |   32   |    28    |    4     | 4×uint64
 *   64  |   56   |     4    |    7     | max stride for chunk=64
 *  128  |    8   |   116    |    1     | 1×uint64 + long name
 *  128  |   16   |   108    |    2     | 2×uint64 + long name
 *  128  |   48   |    76    |    6     | 6×uint64 (3×Lustre FID)
 *  128  |  120   |     4    |   15     | max stride for chunk=128
 *  256  |    8   |   244    |    1     | 1×uint64 + very long name
 *  256  |   16   |   236    |    2     | 2×uint64 + very long name
 *  256  |  128   |   124    |   16     | 16×uint64 (wide value, medium name)
 *  256  |  248   |     4    |   31     | max stride for chunk=256
 *  ...
 *
 * Note: stride=8 with chunk=64 is skipped by tzap_try_promote() because
 * it provides only 2 bytes more than MicroZAP.  Chunk=128 is the minimum
 * for stride=8. chunk=64 is only used when stride >= 16 (num_integers > 1).
 */

/*
 * TinyZAP chunk table: the three supported chunk sizes in bytes.
 * chunk_id 0=64B, 1=128B, 2=256B.
 */
#define	TZAP_CHUNK_SIZES		3
extern const uint16_t tzap_chunk_table[TZAP_CHUNK_SIZES];

/* chunk size constants */
#define	TZAP_MIN_CHUNK_LOG2	6 /* 64  bytes, backward compat */
#define	TZAP_MAX_CHUNK_LOG2	8 /* 256 bytes */
#define	TZAP_MIN_CHUNK		(1U << TZAP_MIN_CHUNK_LOG2)	/* 64 */
#define	TZAP_MAX_CHUNK		(1U << TZAP_MAX_CHUNK_LOG2)	/* 256 */

/* stride constants: min stride across ALL chunk sizes */
#define	TZAP_MIN_STRIDE		8 /* 1×uint64 minimum */
#define	TZAP_MIN_NAME_LEN	4 /* min bytes reserved for name string */

/*
 * Max stride is chunk dependent:
 * TZAP_MAX_STRIDE(chunk) = chunk - sizeof (uint32_t) - TZAP_MIN_NAME_LEN
 * Use tzap_max_stride(chunk) inline below.
 */

#define	MZAP_IS_TINYZAP(phys) \
	(((phys)->mz_flags & MZAP_FLAG_TINY) != 0)

#define	MZAP_CHUNK_SIZE(phys) \
	((uint16_t)(1U << (phys)->mz_chunk_shift))

#define	MZAP_STRIDE(phys) \
	((uint16_t)((phys)->mz_value_ints * sizeof (uint64_t)))

/*
 * Name length available in a TinyZAP chunk:
 * chunk - stride - sizeof (uint32_t cd)
 * Both chunk and stride are required, chunk is no longer fixed.
 */
#define	TZAP_NAME_LEN(chunk, stride) \
	((uint16_t)((chunk) - (stride) - sizeof (uint32_t)))

/*
 * tzap_max_stride: maximum value width for a given chunk size.
 * Leaves at least TZAP_MIN_NAME_LEN bytes for the name field.
 */
static inline uint16_t
tzap_max_stride(uint16_t chunk)
{
	return ((uint16_t)((chunk) - sizeof (uint32_t) - TZAP_MIN_NAME_LEN));
}

/*
 * TinyZAP physical entry.
 *
 * Size is variable (chunk bytes: 64, 128, or 256).
 * NEVER stack-allocate: always access via TZE_PHYS() pointer cast.
 *
 * [0 .. stride-1]        value blob   (mz_value_ints × uint64)
 * [stride .. stride+3]   cd           (uint32_t)
 * [stride+4 .. chunk-1]  name         (TZAP_NAME_LEN(chunk,stride) bytes)
 */
typedef struct tzap_ent_phys {
	uint8_t tze_data[0]; /* zero-length array */
} tzap_ent_phys_t;

static inline uint8_t *
tze_value(tzap_ent_phys_t *tze)
{
	return (tze->tze_data);
}

static inline uint32_t *
tze_cd_ptr(tzap_ent_phys_t *tze, uint16_t stride)
{
	return ((uint32_t *)(tze->tze_data + stride));
}

static inline char *
tze_name_ptr(tzap_ent_phys_t *tze, uint16_t stride)
{
	return ((char *)(tze->tze_data + stride + sizeof (uint32_t)));
}

typedef struct mzap_ent_phys {
	uint64_t mze_value;
	uint32_t mze_cd;
	uint16_t mze_pad;	/* in case we want to chain them someday */
	char mze_name[MZAP_NAME_LEN];
} mzap_ent_phys_t;

/*
 * MicroZAP / TinyZAP on-disk header.
 * Total size = MZAP_ENT_LEN (64 bytes).
 */
typedef struct mzap_phys {
	uint64_t mz_block_type;	/* ZBT_MICRO */
	uint64_t mz_salt;
	uint64_t mz_normflags;
	uint8_t  mz_flags;	/* MZAP_FLAG_TINY */
	/* log2(chunk) for TinyZAP, 0 = MicroZAP */
	uint8_t  mz_chunk_shift;
	uint8_t  mz_value_ints;	/* num_integers; stride = *8 */
	uint8_t  mz_pad1;	/* zero */
	uint32_t mz_pad2;	/* zero */
	uint64_t mz_pad3[4];	/* zero */
	/* actually variable size depending on block size */
	mzap_ent_phys_t mz_chunk[];
} mzap_phys_t;

typedef struct mzap_ent {
	uint32_t mze_hash;
	uint16_t mze_cd; /* copy from mze_phys->mze_cd */
	uint16_t mze_chunkid;
} mzap_ent_t;

#define	MZE_PHYS(zap, mze) \
	(&zap_m_phys(zap)->mz_chunk[(mze)->mze_chunkid])

/*
 * TinyZAP accessor: byte-offset into mz_chunk[] raw bytes using
 * variable chunk size. mze_chunkid is a slot index, not a byte offset.
 * Only valid when zap->zap_m.zap_stride != 0.
 */
#define	TZE_PHYS(zap, mze) \
	((tzap_ent_phys_t *)((uint8_t *)zap_m_phys(zap)->mz_chunk + \
	(size_t)(mze)->mze_chunkid * (zap)->zap_m.zap_chunk_size))

/*
 * The (fat) zap is stored in one object. It is an array of
 * 1<<FZAP_BLOCK_SHIFT byte blocks. The layout looks like one of:
 *
 * ptrtbl fits in first block:
 * 	[zap_phys_t zap_ptrtbl_shift < 6] [zap_leaf_t] ...
 *
 * ptrtbl too big for first block:
 * 	[zap_phys_t zap_ptrtbl_shift >= 6] [zap_leaf_t] [ptrtbl] ...
 *
 */

struct dmu_buf;
struct zap_leaf;

#define	ZBT_LEAF		((1ULL << 63) + 0)
#define	ZBT_HEADER		((1ULL << 63) + 1)
#define	ZBT_MICRO		((1ULL << 63) + 3)
/* any other values are ptrtbl blocks */

/*
 * the embedded pointer table takes up half a block:
 * block size / entry size (2^3) / 2
 */
#define	ZAP_EMBEDDED_PTRTBL_SHIFT(zap) (FZAP_BLOCK_SHIFT(zap) - 3 - 1)

/*
 * The embedded pointer table starts half-way through the block.  Since
 * the pointer table itself is half the block, it starts at (64-bit)
 * word number (1<<ZAP_EMBEDDED_PTRTBL_SHIFT(zap)).
 */
#define	ZAP_EMBEDDED_PTRTBL_ENT(zap, idx) \
	((uint64_t *)zap_f_phys(zap)) \
	[(idx) + (1<<ZAP_EMBEDDED_PTRTBL_SHIFT(zap))]

/*
 * TAKE NOTE:
 * If zap_phys_t is modified, zap_byteswap() must be modified.
 */
typedef struct zap_phys {
	uint64_t zap_block_type;	/* ZBT_HEADER */
	uint64_t zap_magic;		/* ZAP_MAGIC */

	struct zap_table_phys {
		uint64_t zt_blk;	/* starting block number */
		uint64_t zt_numblks;	/* number of blocks */
		uint64_t zt_shift;	/* bits to index it */
		uint64_t zt_nextblk;	/* next (larger) copy start block */
		uint64_t zt_blks_copied; /* number source blocks copied */
	} zap_ptrtbl;

	uint64_t zap_freeblk;		/* the next free block */
	uint64_t zap_num_leafs;		/* number of leafs */
	uint64_t zap_num_entries;	/* number of entries */
	uint64_t zap_salt;		/* salt to stir into hash function */
	uint64_t zap_normflags;		/* flags for u8_textprep_str() */
	uint64_t zap_flags;		/* zap_flags_t */
	/*
	 * This structure is followed by padding, and then the embedded
	 * pointer table.  The embedded pointer table takes up second
	 * half of the block.  It is accessed using the
	 * ZAP_EMBEDDED_PTRTBL_ENT() macro.
	 */
} zap_phys_t;

typedef struct zap_table_phys zap_table_phys_t;

typedef struct zap {
	dmu_buf_user_t zap_dbu;
	objset_t *zap_objset;
	uint64_t zap_object;
	dnode_t *zap_dnode;
	struct dmu_buf *zap_dbuf;
	krwlock_t zap_rwlock;
	boolean_t zap_ismicro;
	int zap_normflags;
	uint64_t zap_salt;
	union {
		struct {
			/*
			 * zap_num_entries_mtx protects
			 * zap_num_entries
			 */
			kmutex_t zap_num_entries_mtx;
			int zap_block_shift;
		} zap_fat;
		struct {
			int16_t zap_num_entries;
			int16_t zap_num_chunks;
			int16_t zap_alloc_next;
			/*
			 * zap_stride:
			 * 0        = plain MicroZAP
			 * 8..255   = TinyZAP, value width in bytes(mult of 8).
			 *            stride=8 (num_integers=1) only arises for
			 *            long-name entries that exceeded
			 *            MZAP_NAME_LEN.
			 */
			uint16_t zap_stride;
			/*
			 * zap_chunk_size: in-memory entry byte size.
			 *   0        = plain MicroZAP.
			 *              Physical entry is MZAP_ENT_LEN (64)
			 *   64       = TinyZAP  64-byte chunk (stride >= 16)
			 *   128      = TinyZAP 128-byte chunk
			 *   256      = TinyZAP 256-byte chunk
			 * Set alongside zap_stride by tzap_try_promote().
			 * Entries accessed via TZE_PHYS() / tzap_ent_phys_t.
			 */
			uint16_t zap_chunk_size;
			zfs_btree_t zap_tree;
		} zap_micro;
	} zap_u;
} zap_t;

#define	zap_f	zap_u.zap_fat
#define	zap_m	zap_u.zap_micro

static inline zap_phys_t *
zap_f_phys(zap_t *zap)
{
	return (zap->zap_dbuf->db_data);
}

static inline mzap_phys_t *
zap_m_phys(zap_t *zap)
{
	return (zap->zap_dbuf->db_data);
}

/*
 * zap_name_t carries the original key and whatever we've derived from it
 * (normalised form, hash, etc) as we work through completing the operation.
 */
typedef struct zap_name {
	zap_t *zn_zap;
	int zn_key_intlen;
	const void *zn_key_orig;
	int zn_key_orig_numints;
	const void *zn_key_norm;
	int zn_key_norm_numints;
	uint64_t zn_hash;
	matchtype_t zn_matchtype;
	int zn_normflags;
	int zn_normbuf_len;
	char zn_normbuf[];
} zap_name_t;

/*
 * Allocate a zap_name_t. The longname flag ensures there is enough room to
 * hold a long filename when the 'longname' pool feature is active.
 */
zap_name_t *zap_name_alloc(zap_t *zap, boolean_t longname);

/*
 * Allocate a zap_name_t for the given key. zap_name_init_str() will be
 * called to normalise the key and initialise the struct.
 */
zap_name_t *zap_name_alloc_str(zap_t *zap, const char *key, matchtype_t mt);

/*
 * Allocate a zap_name_t for a uint64 array key.
 */
zap_name_t *zap_name_alloc_uint64(zap_t *zap, const uint64_t *key, int numints);

/*
 * Free a zap_name_t.
 */
void zap_name_free(zap_name_t *zn);

/*
 * Initialise an existing zap_name_t with the normalised form of the key,
 * computed according to the given matchtype.
 */
int zap_name_init_str(zap_name_t *zn, const char *key, matchtype_t mt);

/*
 * Compare 'matchname' with the name represented by the zap_name_t, applying
 * the same normalisation method first. Returns true if the normalised forms
 * match, false otherwise.
 */
boolean_t zap_match(zap_name_t *zn, const char *matchname);

/*
 * Compute and return the 64-bit hash for the name, according to the name
 * type and hash flags.
 */
uint64_t zap_hash(zap_name_t *zn);

/*
 * Return a zap_t for the given on-disk object, locked and ready for use.
 * The zap_t will be allocated and loaded from disk if its not already loaded.
 */
int zap_lock(objset_t *os, uint64_t obj, dmu_tx_t *tx,
    krw_t lti, boolean_t fatreader, boolean_t adding, const void *tag,
    zap_t **zapp);
int zap_lock_by_dnode(dnode_t *dn, dmu_tx_t *tx,
    krw_t lti, boolean_t fatreader, boolean_t adding, const void *tag,
    zap_t **zapp);

/* Unlock and release a zap_t. */
void zap_unlock(zap_t *zap, const void *tag);

/*
 * Try to upgrade a zap lock from READER to WRITER. If the upgrade is not
 * possible without blocking, returns 0. If the upgrade happened, returns 1.
 */
int zap_lock_try_upgrade(zap_t *zap, dmu_tx_t *tx);

/*
 * Upgrade a zap lock from READER to WRITER. If it can't be upgraded
 * immediately it will block.
 */
void zap_lock_upgrade(zap_t *zap, dmu_tx_t *tx);

/* zap_t release function for when associated dbuf is evicted. */
void zap_evict_sync(void *dbu);

/* Misc internal state & config. */
int zap_hashbits(zap_t *zap);
uint32_t zap_maxcd(zap_t *zap);
uint64_t zap_getflags(zap_t *zap);

/* Microzap implementation. */
zap_t *mzap_open(dmu_buf_t *db);
int mzap_upgrade(zap_t **zapp, dmu_tx_t *tx, zap_flags_t flags);
mzap_ent_t *mze_find(zap_name_t *zn, zfs_btree_index_t *idx);
boolean_t mze_canfit_fzap_leaf(zap_name_t *zn, uint64_t hash);
void mze_destroy(zap_t *zap);
boolean_t mzap_normalization_conflict(zap_t *zap, zap_name_t *zn,
    mzap_ent_t *mze, zfs_btree_index_t *idx);
void mzap_addent(zap_name_t *zn, uint64_t value);
void mzap_byteswap(mzap_phys_t *buf, size_t size);
uint64_t zap_get_micro_max_size(spa_t *spa);

/*
 * MicroZAP in-memory tree helpers, also used by zap_tiny.c
 */
uint32_t mze_find_unused_cd(zap_t *zap, uint64_t hash);
void mze_insert(zap_t *zap, uint16_t chunkid, uint64_t hash);

/*
 * Shared between zap_tiny.c and zap_micro.c for deferred
 * spa_feature_incr/decr via dsl_sync_task.
 */
typedef struct {
	spa_t *tfa_spa;
} tzap_feature_arg_t;

void tzap_feature_incr_sync(void *arg, dmu_tx_t *tx);
void tzap_feature_decr_sync(void *arg, dmu_tx_t *tx);
void tzap_feature_incr_cb(void *arg, int error);
void tzap_feature_decr_cb(void *arg, int error);

/*
 * tzap_try_promote() - stamp TinyZAP geometry on-disk and promote
 * this MicroZAP to TinyZAP on the first qualifying add.
 *
 * Called from zap_add_by_dnode() when:
 *    - the entry does not fit plain MicroZAP constraints, AND
 *    - the ZAP has no committed entries with a different geometry.
 *
 * Validates integer_size, stride, and key length inline, then
 * selects the smallest chunk size (64->128->256) that fits:
 *    - stride=8  (num_integers=1, long key): starts at chunk=128, not 64
 *    - stride>=16 (num_integers>1):          starts at chunk=64
 * then writes three independent uint8_t fields on-disk:
 *    mz_flags       |= MZAP_FLAG_TINY
 *    mz_chunk_shift  = log2(chunk)   (6, 7, or 8)
 *    mz_value_ints   = stride / 8
 *
 * Updates in-memory state:
 *   zap->zap_m.zap_stride     = stride
 *   zap->zap_m.zap_chunk_size = chunk (64, 128, or 256)
 *   zap->zap_m.zap_num_chunks = block_size / chunk
 *
 * Feature flag: chunk > 64 requires SPA_FEATURE_TINY_ZAP.
 *   - stride>=16 + feature absent: falls back to chunk=64 if stride fits,
 *     otherwise returns B_FALSE -> FatZAP upgrade.
 *   - stride=8  + feature absent: chunk=64 is always skipped for stride=8,
 *     so returns B_FALSE immediately -> FatZAP upgrade.
 *
 * On any failure: clears MZAP_FLAG_TINY from mz_flags and returns B_FALSE.
 */
boolean_t tzap_try_promote(zap_t *zap, int integer_size,
    uint64_t num_integers, const char *key, dmu_tx_t *tx);

void tzap_reencode_micro_to_tiny(zap_t *zap, uint16_t chunk, dmu_tx_t *tx);

boolean_t tzap_try_chunk_upgrade(zap_t *zap, uint16_t stride,
    size_t keylen, dmu_tx_t *tx);

int tzap_upgrade_chunk(zap_t *zap, uint16_t new_chunk, dmu_tx_t *tx);

/*
 * tzap_addent() - write an entry into an active TinyZAP chunk slot.
 * zap_stride and zap_chunk_size must already be stamped by
 * tzap_try_promote(). Caller must hold the write lock and dirty the dbuf.
 */
void tzap_addent(zap_name_t *zn, const void *val);

/*
 * tzap_lookup() - retrieve a value from a TinyZAP entry.
 */
int tzap_lookup(zap_t *zap, mzap_ent_t *mze, uint64_t integer_size,
    uint64_t num_integers, void *buf, char *realname, int rn_len,
    boolean_t *ncp);

/*
 * tzap_cursor_fill() - populate a zap_attribute_t from a TinyZAP btree entry
 * during cursor iteration.
 */
void tzap_cursor_fill(zap_cursor_t *zc, mzap_ent_t *mze,
    zap_attribute_t *za);

/*
 * tzap_upgrade_entries() - re-encode all variable-stride TinyZAP entries
 * into FatZAP leaf blocks during mzap_upgrade().
 * Reads geometry from mzap_phys_t fields directly:
 *    mz_value_ints  -> stride
 *    mz_chunk_shift -> log2(chunk)
 */
int tzap_upgrade_entries(mzap_phys_t *mzp, size_t db_size,
    zap_name_t *zn, dmu_tx_t *tx);

void tzap_get_stats(zap_t *zap, zap_stats_t *zs);

#ifdef ZFS_DEBUG
#define	TZAP_VERIFY_PHYS(zap) \
	tzap_verify_phys(__FUNCTION__, zap)
#else
#define	TZAP_VERIFY_PHYS(zap) (B_TRUE)
#endif

extern boolean_t tzap_verify_phys(const char *caller, zap_t *zap);

/* Fatzap implementation. */
void fzap_byteswap(void *buf, size_t size);
int fzap_count(zap_t *zap, uint64_t *count);
int fzap_lookup(zap_name_t *zn,
    uint64_t integer_size, uint64_t num_integers, void *buf,
    char *realname, int rn_len, boolean_t *normalization_conflictp,
    uint64_t *actual_num_integers);
void fzap_prefetch(zap_name_t *zn);
int fzap_add(zap_name_t *zn, uint64_t integer_size, uint64_t num_integers,
    const void *val, dmu_tx_t *tx);
int fzap_update(zap_name_t *zn, int integer_size, uint64_t num_integers,
    const void *val, dmu_tx_t *tx);
int fzap_length(zap_name_t *zn,
    uint64_t *integer_size, uint64_t *num_integers);
int fzap_remove(zap_name_t *zn, dmu_tx_t *tx);
int fzap_cursor_retrieve(zap_t *zap, zap_cursor_t *zc, zap_attribute_t *za);
void fzap_get_stats(zap_t *zap, zap_stats_t *zs);
void zap_put_leaf(struct zap_leaf *l);
int fzap_add_cd(zap_name_t *zn, uint64_t integer_size, uint64_t num_integers,
    const void *val, uint32_t cd, dmu_tx_t *tx);
void fzap_upgrade(zap_t *zap, dmu_tx_t *tx, zap_flags_t flags);

#ifdef	__cplusplus
}
#endif

#endif /* _SYS_ZAP_IMPL_H */
