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

#ifndef _SYS_SPA_H
#define	_SYS_SPA_H

#include <sys/avl.h>
#include <sys/zfs_context.h>
#include <sys/nvpair.h>
#include <sys/sysmacros.h>
#include <sys/types.h>
#include <sys/fs/zfs.h>

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * Forward references that lots of things need.
 */
typedef struct spa spa_t;
typedef struct vdev vdev_t;
typedef struct metaslab metaslab_t;
typedef struct zilog zilog_t;
typedef struct spa_aux_vdev spa_aux_vdev_t;
struct dsl_pool;

/*
 * General-purpose 32-bit and 64-bit bitfield encodings.
 */
#define	BF32_DECODE(x, low, len)	P2PHASE((x) >> (low), 1U << (len))
#define	BF64_DECODE(x, low, len)	P2PHASE((x) >> (low), 1ULL << (len))
#define	BF32_ENCODE(x, low, len)	(P2PHASE((x), 1U << (len)) << (low))
#define	BF64_ENCODE(x, low, len)	(P2PHASE((x), 1ULL << (len)) << (low))

#define	BF32_GET(x, low, len)		BF32_DECODE(x, low, len)
#define	BF64_GET(x, low, len)		BF64_DECODE(x, low, len)

#define	BF32_SET(x, low, len, val)	\
	((x) ^= BF32_ENCODE((x >> low) ^ (val), low, len))
#define	BF64_SET(x, low, len, val)	\
	((x) ^= BF64_ENCODE((x >> low) ^ (val), low, len))

#define	BF32_GET_SB(x, low, len, shift, bias)	\
	((BF32_GET(x, low, len) + (bias)) << (shift))
#define	BF64_GET_SB(x, low, len, shift, bias)	\
	((BF64_GET(x, low, len) + (bias)) << (shift))

#define	BF32_SET_SB(x, low, len, shift, bias, val)	\
	BF32_SET(x, low, len, ((val) >> (shift)) - (bias))
#define	BF64_SET_SB(x, low, len, shift, bias, val)	\
	BF64_SET(x, low, len, ((val) >> (shift)) - (bias))

/*
 * We currently support nine block sizes, from 512 bytes to 128K.
 * We could go higher, but the benefits are near-zero and the cost
 * of COWing a giant block to modify one byte would become excessive.
 */
#define	SPA_MINBLOCKSHIFT	9
#define	SPA_MAXBLOCKSHIFT	17
#define	SPA_MINBLOCKSIZE	(1ULL << SPA_MINBLOCKSHIFT)
#define	SPA_MAXBLOCKSIZE	(1ULL << SPA_MAXBLOCKSHIFT)

#define	SPA_BLOCKSIZES		(SPA_MAXBLOCKSHIFT - SPA_MINBLOCKSHIFT + 1)

/*
 * Size of block to hold the configuration data (a packed nvlist)
 */
#define	SPA_CONFIG_BLOCKSIZE	(1 << 14)

/*
 * The DVA size encodings for LSIZE and PSIZE support blocks up to 32MB.
 * The ASIZE encoding should be at least 64 times larger (6 more bits)
 * to support up to 4-way RAID-Z mirror mode with worst-case gang block
 * overhead, three DVAs per bp, plus one more bit in case we do anything
 * else that expands the ASIZE.
 */
#define	SPA_LSIZEBITS		16	/* LSIZE up to 32M (2^16 * 512)	*/
#define	SPA_PSIZEBITS		16	/* PSIZE up to 32M (2^16 * 512)	*/
#define	SPA_ASIZEBITS		24	/* ASIZE up to 64 times larger	*/

/*
 * All SPA data is represented by 128-bit data virtual addresses (DVAs).
 * The members of the dva_t should be considered opaque outside the SPA.
 */
typedef struct dva {
	uint64_t	dva_word[2];
} dva_t;

/*
 * Each block has a 256-bit checksum -- strong enough for cryptographic hashes.
 */
typedef struct zio_cksum {
	uint64_t	zc_word[4];
} zio_cksum_t;

/*
 * Each block is described by its DVAs, time of birth, checksum, etc.
 * The word-by-word, bit-by-bit layout of the blkptr is as follows:
 *
 *	64	56	48	40	32	24	16	8	0
 *	+-------+-------+-------+-------+-------+-------+-------+-------+
 * 0	|		vdev1		| GRID  |	  ASIZE		|
 *	+-------+-------+-------+-------+-------+-------+-------+-------+
 * 1	|G|			 offset1				|
 *	+-------+-------+-------+-------+-------+-------+-------+-------+
 * 2	|		vdev2		| GRID  |	  ASIZE		|
 *	+-------+-------+-------+-------+-------+-------+-------+-------+
 * 3	|G|			 offset2				|
 *	+-------+-------+-------+-------+-------+-------+-------+-------+
 * 4	|		vdev3		| GRID  |	  ASIZE		|
 *	+-------+-------+-------+-------+-------+-------+-------+-------+
 * 5	|G|			 offset3				|
 *	+-------+-------+-------+-------+-------+-------+-------+-------+
 * 6	|E| lvl | type	| cksum | comp	|     PSIZE	|     LSIZE	|
 *	+-------+-------+-------+-------+-------+-------+-------+-------+
 * 7	|			padding					|
 *	+-------+-------+-------+-------+-------+-------+-------+-------+
 * 8	|			padding					|
 *	+-------+-------+-------+-------+-------+-------+-------+-------+
 * 9	|			padding					|
 *	+-------+-------+-------+-------+-------+-------+-------+-------+
 * a	|			birth txg				|
 *	+-------+-------+-------+-------+-------+-------+-------+-------+
 * b	|			fill count				|
 *	+-------+-------+-------+-------+-------+-------+-------+-------+
 * c	|			checksum[0]				|
 *	+-------+-------+-------+-------+-------+-------+-------+-------+
 * d	|			checksum[1]				|
 *	+-------+-------+-------+-------+-------+-------+-------+-------+
 * e	|			checksum[2]				|
 *	+-------+-------+-------+-------+-------+-------+-------+-------+
 * f	|			checksum[3]				|
 *	+-------+-------+-------+-------+-------+-------+-------+-------+
 *
 * Legend:
 *
 * vdev		virtual device ID
 * offset	offset into virtual device
 * LSIZE	logical size
 * PSIZE	physical size (after compression)
 * ASIZE	allocated size (including RAID-Z parity and gang block headers)
 * GRID		RAID-Z layout information (reserved for future use)
 * cksum	checksum function
 * comp		compression function
 * G		gang block indicator
 * E		endianness
 * type		DMU object type
 * lvl		level of indirection
 * birth txg	transaction group in which the block was born
 * fill count	number of non-zero blocks under this bp
 * checksum[4]	256-bit checksum of the data this bp describes
 */
typedef struct blkptr {
	dva_t		blk_dva[3];	/* 128-bit Data Virtual Address	*/
	uint64_t	blk_prop;	/* size, compression, type, etc	*/
	uint64_t	blk_pad[3];	/* Extra space for the future	*/
	uint64_t	blk_birth;	/* transaction group at birth	*/
	uint64_t	blk_fill;	/* fill count			*/
	zio_cksum_t	blk_cksum;	/* 256-bit checksum		*/
} blkptr_t;

#define	SPA_BLKPTRSHIFT	7		/* blkptr_t is 128 bytes	*/
#define	SPA_DVAS_PER_BP	3		/* Number of DVAs in a bp	*/

/*
 * Macros to get and set fields in a bp or DVA.
 */
#define	DVA_GET_ASIZE(dva)	\
	BF64_GET_SB((dva)->dva_word[0], 0, 24, SPA_MINBLOCKSHIFT, 0)
#define	DVA_SET_ASIZE(dva, x)	\
	BF64_SET_SB((dva)->dva_word[0], 0, 24, SPA_MINBLOCKSHIFT, 0, x)

#define	DVA_GET_GRID(dva)	BF64_GET((dva)->dva_word[0], 24, 8)
#define	DVA_SET_GRID(dva, x)	BF64_SET((dva)->dva_word[0], 24, 8, x)

#define	DVA_GET_VDEV(dva)	BF64_GET((dva)->dva_word[0], 32, 32)
#define	DVA_SET_VDEV(dva, x)	BF64_SET((dva)->dva_word[0], 32, 32, x)

#define	DVA_GET_OFFSET(dva)	\
	BF64_GET_SB((dva)->dva_word[1], 0, 63, SPA_MINBLOCKSHIFT, 0)
#define	DVA_SET_OFFSET(dva, x)	\
	BF64_SET_SB((dva)->dva_word[1], 0, 63, SPA_MINBLOCKSHIFT, 0, x)

#define	DVA_GET_GANG(dva)	BF64_GET((dva)->dva_word[1], 63, 1)
#define	DVA_SET_GANG(dva, x)	BF64_SET((dva)->dva_word[1], 63, 1, x)

#define	BP_GET_LSIZE(bp)	\
	(BP_IS_HOLE(bp) ? 0 : \
	BF64_GET_SB((bp)->blk_prop, 0, 16, SPA_MINBLOCKSHIFT, 1))
#define	BP_SET_LSIZE(bp, x)	\
	BF64_SET_SB((bp)->blk_prop, 0, 16, SPA_MINBLOCKSHIFT, 1, x)

#define	BP_GET_PSIZE(bp)	\
	BF64_GET_SB((bp)->blk_prop, 16, 16, SPA_MINBLOCKSHIFT, 1)
#define	BP_SET_PSIZE(bp, x)	\
	BF64_SET_SB((bp)->blk_prop, 16, 16, SPA_MINBLOCKSHIFT, 1, x)

#define	BP_GET_COMPRESS(bp)	BF64_GET((bp)->blk_prop, 32, 8)
#define	BP_SET_COMPRESS(bp, x)	BF64_SET((bp)->blk_prop, 32, 8, x)

#define	BP_GET_CHECKSUM(bp)	BF64_GET((bp)->blk_prop, 40, 8)
#define	BP_SET_CHECKSUM(bp, x)	BF64_SET((bp)->blk_prop, 40, 8, x)

#define	BP_GET_TYPE(bp)		BF64_GET((bp)->blk_prop, 48, 8)
#define	BP_SET_TYPE(bp, x)	BF64_SET((bp)->blk_prop, 48, 8, x)

#define	BP_GET_LEVEL(bp)	BF64_GET((bp)->blk_prop, 56, 5)
#define	BP_SET_LEVEL(bp, x)	BF64_SET((bp)->blk_prop, 56, 5, x)

#define	BP_GET_BYTEORDER(bp)	(0 - BF64_GET((bp)->blk_prop, 63, 1))
#define	BP_SET_BYTEORDER(bp, x)	BF64_SET((bp)->blk_prop, 63, 1, x)

#define	BP_GET_ASIZE(bp)	\
	(DVA_GET_ASIZE(&(bp)->blk_dva[0]) + DVA_GET_ASIZE(&(bp)->blk_dva[1]) + \
		DVA_GET_ASIZE(&(bp)->blk_dva[2]))

#define	BP_GET_UCSIZE(bp) \
	((BP_GET_LEVEL(bp) > 0 || dmu_ot[BP_GET_TYPE(bp)].ot_metadata) ? \
	BP_GET_PSIZE(bp) : BP_GET_LSIZE(bp));

#define	BP_GET_NDVAS(bp)	\
	(!!DVA_GET_ASIZE(&(bp)->blk_dva[0]) + \
	!!DVA_GET_ASIZE(&(bp)->blk_dva[1]) + \
	!!DVA_GET_ASIZE(&(bp)->blk_dva[2]))

#define	BP_COUNT_GANG(bp)	\
	(DVA_GET_GANG(&(bp)->blk_dva[0]) + \
	DVA_GET_GANG(&(bp)->blk_dva[1]) + \
	DVA_GET_GANG(&(bp)->blk_dva[2]))

#define	DVA_EQUAL(dva1, dva2)	\
	((dva1)->dva_word[1] == (dva2)->dva_word[1] && \
	(dva1)->dva_word[0] == (dva2)->dva_word[0])

#define	ZIO_CHECKSUM_EQUAL(zc1, zc2) \
	(0 == (((zc1).zc_word[0] - (zc2).zc_word[0]) | \
	((zc1).zc_word[1] - (zc2).zc_word[1]) | \
	((zc1).zc_word[2] - (zc2).zc_word[2]) | \
	((zc1).zc_word[3] - (zc2).zc_word[3])))

#define	DVA_IS_VALID(dva)	(DVA_GET_ASIZE(dva) != 0)

#define	ZIO_SET_CHECKSUM(zcp, w0, w1, w2, w3)	\
{						\
	(zcp)->zc_word[0] = w0;			\
	(zcp)->zc_word[1] = w1;			\
	(zcp)->zc_word[2] = w2;			\
	(zcp)->zc_word[3] = w3;			\
}

#define	BP_IDENTITY(bp)		(&(bp)->blk_dva[0])
#define	BP_IS_GANG(bp)		DVA_GET_GANG(BP_IDENTITY(bp))
#define	BP_IS_HOLE(bp)		((bp)->blk_birth == 0)
#define	BP_IS_OLDER(bp, txg)	(!BP_IS_HOLE(bp) && (bp)->blk_birth < (txg))

#define	BP_ZERO(bp)				\
{						\
	(bp)->blk_dva[0].dva_word[0] = 0;	\
	(bp)->blk_dva[0].dva_word[1] = 0;	\
	(bp)->blk_dva[1].dva_word[0] = 0;	\
	(bp)->blk_dva[1].dva_word[1] = 0;	\
	(bp)->blk_dva[2].dva_word[0] = 0;	\
	(bp)->blk_dva[2].dva_word[1] = 0;	\
	(bp)->blk_prop = 0;			\
	(bp)->blk_pad[0] = 0;			\
	(bp)->blk_pad[1] = 0;			\
	(bp)->blk_pad[2] = 0;			\
	(bp)->blk_birth = 0;			\
	(bp)->blk_fill = 0;			\
	ZIO_SET_CHECKSUM(&(bp)->blk_cksum, 0, 0, 0, 0);	\
}

#define	BLK_FILL_ALREADY_FREED	(-1ULL)

/*
 * Note: the byteorder is either 0 or -1, both of which are palindromes.
 * This simplifies the endianness handling a bit.
 */
#ifdef _BIG_ENDIAN
#define	ZFS_HOST_BYTEORDER	(0ULL)
#else
#define	ZFS_HOST_BYTEORDER	(-1ULL)
#endif

#define	BP_SHOULD_BYTESWAP(bp)	(BP_GET_BYTEORDER(bp) != ZFS_HOST_BYTEORDER)

#define	BP_SPRINTF_LEN	320

#include <sys/dmu.h>

#define	BP_GET_BUFC_TYPE(bp)						\
	(((BP_GET_LEVEL(bp) > 0) || (dmu_ot[BP_GET_TYPE(bp)].ot_metadata)) ? \
	ARC_BUFC_METADATA : ARC_BUFC_DATA);
/*
 * Routines found in spa.c
 */

/* state manipulation functions */
extern int spa_open(const char *pool, spa_t **, void *tag);
extern int spa_get_stats(const char *pool, nvlist_t **config,
    char *altroot, size_t buflen);
extern int spa_create(const char *pool, nvlist_t *config, nvlist_t *props,
    const char *history_str, nvlist_t *zplprops);
extern int spa_import_rootpool(char *devpath, char *devid);
extern int spa_import(const char *pool, nvlist_t *config, nvlist_t *props);
extern int spa_import_verbatim(const char *, nvlist_t *, nvlist_t *);
extern nvlist_t *spa_tryimport(nvlist_t *tryconfig);
extern int spa_destroy(char *pool);
extern int spa_export(char *pool, nvlist_t **oldconfig, boolean_t force,
    boolean_t hardforce);
extern int spa_reset(char *pool);
extern void spa_async_request(spa_t *spa, int flag);
extern void spa_async_unrequest(spa_t *spa, int flag);
extern void spa_async_suspend(spa_t *spa);
extern void spa_async_resume(spa_t *spa);
extern spa_t *spa_inject_addref(char *pool);
extern void spa_inject_delref(spa_t *spa);

#define	SPA_ASYNC_CONFIG_UPDATE	0x01
#define	SPA_ASYNC_REMOVE	0x02
#define	SPA_ASYNC_PROBE		0x04
#define	SPA_ASYNC_RESILVER_DONE	0x08
#define	SPA_ASYNC_RESILVER	0x10
#define	SPA_ASYNC_AUTOEXPAND	0x20

/* device manipulation */
extern int spa_vdev_add(spa_t *spa, nvlist_t *nvroot);
extern int spa_vdev_attach(spa_t *spa, uint64_t guid, nvlist_t *nvroot,
    int replacing);
extern int spa_vdev_detach(spa_t *spa, uint64_t guid, uint64_t pguid,
    int replace_done);
extern int spa_vdev_remove(spa_t *spa, uint64_t guid, boolean_t unspare);
extern int spa_vdev_setpath(spa_t *spa, uint64_t guid, const char *newpath);
extern int spa_vdev_setfru(spa_t *spa, uint64_t guid, const char *newfru);

/* spare state (which is global across all pools) */
extern void spa_spare_add(vdev_t *vd);
extern void spa_spare_remove(vdev_t *vd);
extern boolean_t spa_spare_exists(uint64_t guid, uint64_t *pool, int *refcnt);
extern void spa_spare_activate(vdev_t *vd);

/* L2ARC state (which is global across all pools) */
extern void spa_l2cache_add(vdev_t *vd);
extern void spa_l2cache_remove(vdev_t *vd);
extern boolean_t spa_l2cache_exists(uint64_t guid, uint64_t *pool);
extern void spa_l2cache_activate(vdev_t *vd);
extern void spa_l2cache_drop(spa_t *spa);
extern void spa_l2cache_space_update(vdev_t *vd, int64_t space, int64_t alloc);

/* scrubbing */
extern int spa_scrub(spa_t *spa, pool_scrub_type_t type);

/* spa syncing */
extern void spa_sync(spa_t *spa, uint64_t txg); /* only for DMU use */
extern void spa_sync_allpools(void);

/* spa namespace global mutex */
extern kmutex_t spa_namespace_lock;

/*
 * SPA configuration functions in spa_config.c
 */

#define	SPA_CONFIG_UPDATE_POOL	0
#define	SPA_CONFIG_UPDATE_VDEVS	1

extern void spa_config_sync(spa_t *, boolean_t, boolean_t);
extern void spa_config_load(void);
extern nvlist_t *spa_all_configs(uint64_t *);
extern void spa_config_set(spa_t *spa, nvlist_t *config);
extern nvlist_t *spa_config_generate(spa_t *spa, vdev_t *vd, uint64_t txg,
    int getstats);
extern void spa_config_update(spa_t *spa, int what);
extern void spa_config_update_common(spa_t *spa, int what, boolean_t isroot);

/*
 * Miscellaneous SPA routines in spa_misc.c
 */

/* Namespace manipulation */
extern spa_t *spa_lookup(const char *name);
extern spa_t *spa_add(const char *name, const char *altroot);
extern void spa_remove(spa_t *spa);
extern spa_t *spa_next(spa_t *prev);

/* Refcount functions */
extern void spa_open_ref(spa_t *spa, void *tag);
extern void spa_close(spa_t *spa, void *tag);
extern boolean_t spa_refcount_zero(spa_t *spa);

#define	SCL_CONFIG	0x01
#define	SCL_STATE	0x02
#define	SCL_L2ARC	0x04		/* hack until L2ARC 2.0 */
#define	SCL_ALLOC	0x08
#define	SCL_ZIO		0x10
#define	SCL_FREE	0x20
#define	SCL_VDEV	0x40
#define	SCL_LOCKS	7
#define	SCL_ALL		((1 << SCL_LOCKS) - 1)
#define	SCL_STATE_ALL	(SCL_STATE | SCL_L2ARC | SCL_ZIO)

/* Pool configuration locks */
extern int spa_config_tryenter(spa_t *spa, int locks, void *tag, krw_t rw);
extern void spa_config_enter(spa_t *spa, int locks, void *tag, krw_t rw);
extern void spa_config_exit(spa_t *spa, int locks, void *tag);
extern int spa_config_held(spa_t *spa, int locks, krw_t rw);

/* Pool vdev add/remove lock */
extern uint64_t spa_vdev_enter(spa_t *spa);
extern int spa_vdev_exit(spa_t *spa, vdev_t *vd, uint64_t txg, int error);

/* Pool vdev state change lock */
extern void spa_vdev_state_enter(spa_t *spa);
extern int spa_vdev_state_exit(spa_t *spa, vdev_t *vd, int error);

/* Accessor functions */
extern boolean_t spa_shutting_down(spa_t *spa);
extern struct dsl_pool *spa_get_dsl(spa_t *spa);
extern blkptr_t *spa_get_rootblkptr(spa_t *spa);
extern void spa_set_rootblkptr(spa_t *spa, const blkptr_t *bp);
extern void spa_altroot(spa_t *, char *, size_t);
extern int spa_sync_pass(spa_t *spa);
extern char *spa_name(spa_t *spa);
extern uint64_t spa_guid(spa_t *spa);
extern uint64_t spa_last_synced_txg(spa_t *spa);
extern uint64_t spa_first_txg(spa_t *spa);
extern uint64_t spa_version(spa_t *spa);
extern pool_state_t spa_state(spa_t *spa);
extern uint64_t spa_freeze_txg(spa_t *spa);
extern uint64_t spa_get_alloc(spa_t *spa);
extern uint64_t spa_get_space(spa_t *spa);
extern uint64_t spa_get_dspace(spa_t *spa);
extern uint64_t spa_get_asize(spa_t *spa, uint64_t lsize);
extern uint64_t spa_version(spa_t *spa);
extern int spa_max_replication(spa_t *spa);
extern int spa_busy(void);
extern uint8_t spa_get_failmode(spa_t *spa);
extern boolean_t spa_suspended(spa_t *spa);

/* Miscellaneous support routines */
extern int spa_rename(const char *oldname, const char *newname);
extern boolean_t spa_guid_exists(uint64_t pool_guid, uint64_t device_guid);
extern char *spa_strdup(const char *);
extern void spa_strfree(char *);
extern uint64_t spa_get_random(uint64_t range);
extern void sprintf_blkptr(char *buf, int len, const blkptr_t *bp);
extern void spa_freeze(spa_t *spa);
extern void spa_upgrade(spa_t *spa, uint64_t version);
extern void spa_evict_all(void);
extern vdev_t *spa_lookup_by_guid(spa_t *spa, uint64_t guid,
    boolean_t l2cache);
extern boolean_t spa_has_spare(spa_t *, uint64_t guid);
extern uint64_t bp_get_dasize(spa_t *spa, const blkptr_t *bp);
extern boolean_t spa_has_slogs(spa_t *spa);
extern boolean_t spa_is_root(spa_t *spa);
extern boolean_t spa_writeable(spa_t *spa);
extern int spa_mode(spa_t *spa);

/* history logging */
typedef enum history_log_type {
	LOG_CMD_POOL_CREATE,
	LOG_CMD_NORMAL,
	LOG_INTERNAL
} history_log_type_t;

typedef struct history_arg {
	const char *ha_history_str;
	history_log_type_t ha_log_type;
	history_internal_events_t ha_event;
	char ha_zone[MAXPATHLEN];
} history_arg_t;

extern char *spa_his_ievent_table[];

extern void spa_history_create_obj(spa_t *spa, dmu_tx_t *tx);
extern int spa_history_get(spa_t *spa, uint64_t *offset, uint64_t *len_read,
    char *his_buf);
extern int spa_history_log(spa_t *spa, const char *his_buf,
    history_log_type_t what);
void spa_history_internal_log(history_internal_events_t event, spa_t *spa,
    dmu_tx_t *tx, cred_t *cr, const char *fmt, ...);

/* error handling */
struct zbookmark;
struct zio;
extern void spa_log_error(spa_t *spa, struct zio *zio);
extern void zfs_ereport_post(const char *class, spa_t *spa, vdev_t *vd,
    struct zio *zio, uint64_t stateoroffset, uint64_t length);
extern void zfs_post_remove(spa_t *spa, vdev_t *vd);
extern void zfs_post_autoreplace(spa_t *spa, vdev_t *vd);
extern uint64_t spa_get_errlog_size(spa_t *spa);
extern int spa_get_errlog(spa_t *spa, void *uaddr, size_t *count);
extern void spa_errlog_rotate(spa_t *spa);
extern void spa_errlog_drain(spa_t *spa);
extern void spa_errlog_sync(spa_t *spa, uint64_t txg);
extern void spa_get_errlists(spa_t *spa, avl_tree_t *last, avl_tree_t *scrub);

/* vdev cache */
extern void vdev_cache_stat_init(void);
extern void vdev_cache_stat_fini(void);

/* Initialization and termination */
extern void spa_init(int flags);
extern void spa_fini(void);
extern void spa_boot_init();

/* properties */
extern int spa_prop_set(spa_t *spa, nvlist_t *nvp);
extern int spa_prop_get(spa_t *spa, nvlist_t **nvp);
extern void spa_prop_clear_bootfs(spa_t *spa, uint64_t obj, dmu_tx_t *tx);
extern void spa_configfile_set(spa_t *, nvlist_t *, boolean_t);

/* asynchronous event notification */
extern void spa_event_notify(spa_t *spa, vdev_t *vdev, const char *name);

#ifdef ZFS_DEBUG
#define	dprintf_bp(bp, fmt, ...) do {				\
	if (zfs_flags & ZFS_DEBUG_DPRINTF) { 			\
	char *__blkbuf = kmem_alloc(BP_SPRINTF_LEN, KM_SLEEP);	\
	sprintf_blkptr(__blkbuf, BP_SPRINTF_LEN, (bp));		\
	dprintf(fmt " %s\n", __VA_ARGS__, __blkbuf);		\
	kmem_free(__blkbuf, BP_SPRINTF_LEN);			\
	} \
_NOTE(CONSTCOND) } while (0)
#else
#define	dprintf_bp(bp, fmt, ...)
#endif

extern int spa_mode_global;			/* mode, e.g. FREAD | FWRITE */

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_SPA_H */
