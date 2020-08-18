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
 * Copyright (c) 2012, 2016 by Delphix. All rights reserved.
 * Copyright (c) 2013 by Saso Kiselkov. All rights reserved.
 * Copyright (c) 2019, Allan Jude
 * Copyright (c) 2019, Klara Inc.
 */

#ifndef	_SYS_ARC_H
#define	_SYS_ARC_H

#include <sys/zfs_context.h>

#ifdef	__cplusplus
extern "C" {
#endif

#include <sys/zio.h>
#include <sys/dmu.h>
#include <sys/spa.h>
#include <sys/zfs_refcount.h>

/*
 * Used by arc_flush() to inform arc_evict_state() that it should evict
 * all available buffers from the arc state being passed in.
 */
#define	ARC_EVICT_ALL	-1ULL

#define	HDR_SET_LSIZE(hdr, x) do { \
	ASSERT(IS_P2ALIGNED(x, 1U << SPA_MINBLOCKSHIFT)); \
	(hdr)->b_lsize = ((x) >> SPA_MINBLOCKSHIFT); \
_NOTE(CONSTCOND) } while (0)

#define	HDR_SET_PSIZE(hdr, x) do { \
	ASSERT(IS_P2ALIGNED((x), 1U << SPA_MINBLOCKSHIFT)); \
	(hdr)->b_psize = ((x) >> SPA_MINBLOCKSHIFT); \
_NOTE(CONSTCOND) } while (0)

#define	HDR_GET_LSIZE(hdr)	((hdr)->b_lsize << SPA_MINBLOCKSHIFT)
#define	HDR_GET_PSIZE(hdr)	((hdr)->b_psize << SPA_MINBLOCKSHIFT)

typedef struct arc_buf_hdr arc_buf_hdr_t;
typedef struct arc_buf arc_buf_t;
typedef struct arc_prune arc_prune_t;

/*
 * Because the ARC can store encrypted data, errors (not due to bugs) may arise
 * while transforming data into its desired format - specifically, when
 * decrypting, the key may not be present, or the HMAC may not be correct
 * which signifies deliberate tampering with the on-disk state
 * (assuming that the checksum was correct). If any error occurs, the "buf"
 * parameter will be NULL.
 */
typedef void arc_read_done_func_t(zio_t *zio, const zbookmark_phys_t *zb,
    const blkptr_t *bp, arc_buf_t *buf, void *priv);
typedef void arc_write_done_func_t(zio_t *zio, arc_buf_t *buf, void *priv);
typedef void arc_prune_func_t(int64_t bytes, void *priv);

/* Shared module parameters */
extern int zfs_arc_average_blocksize;

/* generic arc_done_func_t's which you can use */
arc_read_done_func_t arc_bcopy_func;
arc_read_done_func_t arc_getbuf_func;

/* generic arc_prune_func_t wrapper for callbacks */
struct arc_prune {
	arc_prune_func_t	*p_pfunc;
	void			*p_private;
	uint64_t		p_adjust;
	list_node_t		p_node;
	zfs_refcount_t		p_refcnt;
};

typedef enum arc_strategy {
	ARC_STRATEGY_META_ONLY		= 0, /* Evict only meta data buffers */
	ARC_STRATEGY_META_BALANCED	= 1, /* Evict data buffers if needed */
} arc_strategy_t;

typedef enum arc_flags
{
	/*
	 * Public flags that can be passed into the ARC by external consumers.
	 */
	ARC_FLAG_WAIT			= 1 << 0,	/* perform sync I/O */
	ARC_FLAG_NOWAIT			= 1 << 1,	/* perform async I/O */
	ARC_FLAG_PREFETCH		= 1 << 2,	/* I/O is a prefetch */
	ARC_FLAG_CACHED			= 1 << 3,	/* I/O was in cache */
	ARC_FLAG_L2CACHE		= 1 << 4,	/* cache in L2ARC */
	ARC_FLAG_PREDICTIVE_PREFETCH	= 1 << 5,	/* I/O from zfetch */
	ARC_FLAG_PRESCIENT_PREFETCH	= 1 << 6,	/* long min lifespan */

	/*
	 * Private ARC flags.  These flags are private ARC only flags that
	 * will show up in b_flags in the arc_hdr_buf_t. These flags should
	 * only be set by ARC code.
	 */
	ARC_FLAG_IN_HASH_TABLE		= 1 << 7,	/* buffer is hashed */
	ARC_FLAG_IO_IN_PROGRESS		= 1 << 8,	/* I/O in progress */
	ARC_FLAG_IO_ERROR		= 1 << 9,	/* I/O failed for buf */
	ARC_FLAG_INDIRECT		= 1 << 10,	/* indirect block */
	/* Indicates that block was read with ASYNC priority. */
	ARC_FLAG_PRIO_ASYNC_READ	= 1 << 11,
	ARC_FLAG_L2_WRITING		= 1 << 12,	/* write in progress */
	ARC_FLAG_L2_EVICTED		= 1 << 13,	/* evicted during I/O */
	ARC_FLAG_L2_WRITE_HEAD		= 1 << 14,	/* head of write list */
	/*
	 * Encrypted or authenticated on disk (may be plaintext in memory).
	 * This header has b_crypt_hdr allocated. Does not include indirect
	 * blocks with checksums of MACs which will also have their X
	 * (encrypted) bit set in the bp.
	 */
	ARC_FLAG_PROTECTED		= 1 << 15,
	/* data has not been authenticated yet */
	ARC_FLAG_NOAUTH			= 1 << 16,
	/* indicates that the buffer contains metadata (otherwise, data) */
	ARC_FLAG_BUFC_METADATA		= 1 << 17,

	/* Flags specifying whether optional hdr struct fields are defined */
	ARC_FLAG_HAS_L1HDR		= 1 << 18,
	ARC_FLAG_HAS_L2HDR		= 1 << 19,

	/*
	 * Indicates the arc_buf_hdr_t's b_pdata matches the on-disk data.
	 * This allows the l2arc to use the blkptr's checksum to verify
	 * the data without having to store the checksum in the hdr.
	 */
	ARC_FLAG_COMPRESSED_ARC		= 1 << 20,
	ARC_FLAG_SHARED_DATA		= 1 << 21,

	/*
	 * Fail this arc_read() (with ENOENT) if the data is not already present
	 * in cache.
	 */
	ARC_FLAG_CACHED_ONLY		= 1 << 22,

	/*
	 * The arc buffer's compression mode is stored in the top 7 bits of the
	 * flags field, so these dummy flags are included so that MDB can
	 * interpret the enum properly.
	 */
	ARC_FLAG_COMPRESS_0		= 1 << 24,
	ARC_FLAG_COMPRESS_1		= 1 << 25,
	ARC_FLAG_COMPRESS_2		= 1 << 26,
	ARC_FLAG_COMPRESS_3		= 1 << 27,
	ARC_FLAG_COMPRESS_4		= 1 << 28,
	ARC_FLAG_COMPRESS_5		= 1 << 29,
	ARC_FLAG_COMPRESS_6		= 1 << 30

} arc_flags_t;

typedef enum arc_buf_flags {
	ARC_BUF_FLAG_SHARED		= 1 << 0,
	ARC_BUF_FLAG_COMPRESSED		= 1 << 1,
	/*
	 * indicates whether this arc_buf_t is encrypted, regardless of
	 * state on-disk
	 */
	ARC_BUF_FLAG_ENCRYPTED		= 1 << 2
} arc_buf_flags_t;

struct arc_buf {
	arc_buf_hdr_t		*b_hdr;
	arc_buf_t		*b_next;
	kmutex_t		b_evict_lock;
	void			*b_data;
	arc_buf_flags_t		b_flags;
};

typedef enum arc_buf_contents {
	ARC_BUFC_INVALID,			/* invalid type */
	ARC_BUFC_DATA,				/* buffer contains data */
	ARC_BUFC_METADATA,			/* buffer contains metadata */
	ARC_BUFC_NUMTYPES
} arc_buf_contents_t;

/*
 * The following breakdowns of arc_size exist for kstat only.
 */
typedef enum arc_space_type {
	ARC_SPACE_DATA,
	ARC_SPACE_META,
	ARC_SPACE_HDRS,
	ARC_SPACE_L2HDRS,
	ARC_SPACE_DBUF,
	ARC_SPACE_DNODE,
	ARC_SPACE_BONUS,
	ARC_SPACE_ABD_CHUNK_WASTE,
	ARC_SPACE_NUMTYPES
} arc_space_type_t;

typedef enum arc_state_type {
	ARC_STATE_ANON,
	ARC_STATE_MRU,
	ARC_STATE_MRU_GHOST,
	ARC_STATE_MFU,
	ARC_STATE_MFU_GHOST,
	ARC_STATE_L2C_ONLY,
	ARC_STATE_NUMTYPES
} arc_state_type_t;

typedef struct arc_buf_info {
	arc_state_type_t	abi_state_type;
	arc_buf_contents_t	abi_state_contents;
	uint32_t		abi_flags;
	uint32_t		abi_bufcnt;
	uint64_t		abi_size;
	uint64_t		abi_spa;
	uint64_t		abi_access;
	uint32_t		abi_mru_hits;
	uint32_t		abi_mru_ghost_hits;
	uint32_t		abi_mfu_hits;
	uint32_t		abi_mfu_ghost_hits;
	uint32_t		abi_l2arc_hits;
	uint32_t		abi_holds;
	uint64_t		abi_l2arc_dattr;
	uint64_t		abi_l2arc_asize;
	enum zio_compress	abi_l2arc_compress;
} arc_buf_info_t;

void arc_space_consume(uint64_t space, arc_space_type_t type);
void arc_space_return(uint64_t space, arc_space_type_t type);
boolean_t arc_is_metadata(arc_buf_t *buf);
boolean_t arc_is_encrypted(arc_buf_t *buf);
boolean_t arc_is_unauthenticated(arc_buf_t *buf);
enum zio_compress arc_get_compression(arc_buf_t *buf);
void arc_get_raw_params(arc_buf_t *buf, boolean_t *byteorder, uint8_t *salt,
    uint8_t *iv, uint8_t *mac);
int arc_untransform(arc_buf_t *buf, spa_t *spa, const zbookmark_phys_t *zb,
    boolean_t in_place);
void arc_convert_to_raw(arc_buf_t *buf, uint64_t dsobj, boolean_t byteorder,
    dmu_object_type_t ot, const uint8_t *salt, const uint8_t *iv,
    const uint8_t *mac);
arc_buf_t *arc_alloc_buf(spa_t *spa, void *tag, arc_buf_contents_t type,
    int32_t size);
arc_buf_t *arc_alloc_compressed_buf(spa_t *spa, void *tag,
    uint64_t psize, uint64_t lsize, enum zio_compress compression_type,
    uint8_t complevel);
arc_buf_t *arc_alloc_raw_buf(spa_t *spa, void *tag, uint64_t dsobj,
    boolean_t byteorder, const uint8_t *salt, const uint8_t *iv,
    const uint8_t *mac, dmu_object_type_t ot, uint64_t psize, uint64_t lsize,
    enum zio_compress compression_type, uint8_t complevel);
uint8_t arc_get_complevel(arc_buf_t *buf);
arc_buf_t *arc_loan_buf(spa_t *spa, boolean_t is_metadata, int size);
arc_buf_t *arc_loan_compressed_buf(spa_t *spa, uint64_t psize, uint64_t lsize,
    enum zio_compress compression_type, uint8_t complevel);
arc_buf_t *arc_loan_raw_buf(spa_t *spa, uint64_t dsobj, boolean_t byteorder,
    const uint8_t *salt, const uint8_t *iv, const uint8_t *mac,
    dmu_object_type_t ot, uint64_t psize, uint64_t lsize,
    enum zio_compress compression_type, uint8_t complevel);
void arc_return_buf(arc_buf_t *buf, void *tag);
void arc_loan_inuse_buf(arc_buf_t *buf, void *tag);
void arc_buf_destroy(arc_buf_t *buf, void *tag);
void arc_buf_info(arc_buf_t *buf, arc_buf_info_t *abi, int state_index);
uint64_t arc_buf_size(arc_buf_t *buf);
uint64_t arc_buf_lsize(arc_buf_t *buf);
void arc_buf_access(arc_buf_t *buf);
void arc_release(arc_buf_t *buf, void *tag);
int arc_released(arc_buf_t *buf);
void arc_buf_sigsegv(int sig, siginfo_t *si, void *unused);
void arc_buf_freeze(arc_buf_t *buf);
void arc_buf_thaw(arc_buf_t *buf);
#ifdef ZFS_DEBUG
int arc_referenced(arc_buf_t *buf);
#endif

int arc_read(zio_t *pio, spa_t *spa, const blkptr_t *bp,
    arc_read_done_func_t *done, void *priv, zio_priority_t priority,
    int flags, arc_flags_t *arc_flags, const zbookmark_phys_t *zb);
zio_t *arc_write(zio_t *pio, spa_t *spa, uint64_t txg,
    blkptr_t *bp, arc_buf_t *buf, boolean_t l2arc, const zio_prop_t *zp,
    arc_write_done_func_t *ready, arc_write_done_func_t *child_ready,
    arc_write_done_func_t *physdone, arc_write_done_func_t *done,
    void *priv, zio_priority_t priority, int zio_flags,
    const zbookmark_phys_t *zb);

arc_prune_t *arc_add_prune_callback(arc_prune_func_t *func, void *priv);
void arc_remove_prune_callback(arc_prune_t *p);
void arc_freed(spa_t *spa, const blkptr_t *bp);

void arc_flush(spa_t *spa, boolean_t retry);
void arc_tempreserve_clear(uint64_t reserve);
int arc_tempreserve_space(spa_t *spa, uint64_t reserve, uint64_t txg);

uint64_t arc_all_memory(void);
uint64_t arc_default_max(uint64_t min, uint64_t allmem);
uint64_t arc_target_bytes(void);
void arc_init(void);
void arc_fini(void);

/*
 * Level 2 ARC
 */

void l2arc_add_vdev(spa_t *spa, vdev_t *vd);
void l2arc_remove_vdev(vdev_t *vd);
boolean_t l2arc_vdev_present(vdev_t *vd);
void l2arc_rebuild_vdev(vdev_t *vd, boolean_t reopen);
boolean_t l2arc_range_check_overlap(uint64_t bottom, uint64_t top,
    uint64_t check);
void l2arc_init(void);
void l2arc_fini(void);
void l2arc_start(void);
void l2arc_stop(void);
void l2arc_spa_rebuild_start(spa_t *spa);

#ifndef _KERNEL
extern boolean_t arc_watch;
#endif

#ifdef	__cplusplus
}
#endif

#endif /* _SYS_ARC_H */
