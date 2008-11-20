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

#ifndef _ZIO_H
#define	_ZIO_H

#include <sys/zfs_context.h>
#include <sys/spa.h>
#include <sys/txg.h>
#include <sys/avl.h>
#include <sys/dkio.h>
#include <sys/fs/zfs.h>
#include <sys/zio_impl.h>

#ifdef	__cplusplus
extern "C" {
#endif

#define	ZBT_MAGIC	0x210da7ab10c7a11ULL	/* zio data bloc tail */

typedef struct zio_block_tail {
	uint64_t	zbt_magic;	/* for validation, endianness	*/
	zio_cksum_t	zbt_cksum;	/* 256-bit checksum		*/
} zio_block_tail_t;

/*
 * Gang block headers are self-checksumming and contain an array
 * of block pointers.
 */
#define	SPA_GANGBLOCKSIZE	SPA_MINBLOCKSIZE
#define	SPA_GBH_NBLKPTRS	((SPA_GANGBLOCKSIZE - \
	sizeof (zio_block_tail_t)) / sizeof (blkptr_t))
#define	SPA_GBH_FILLER		((SPA_GANGBLOCKSIZE - \
	sizeof (zio_block_tail_t) - \
	(SPA_GBH_NBLKPTRS * sizeof (blkptr_t))) /\
	sizeof (uint64_t))

#define	ZIO_GET_IOSIZE(zio)	\
	(BP_IS_GANG((zio)->io_bp) ? \
	SPA_GANGBLOCKSIZE : BP_GET_PSIZE((zio)->io_bp))

typedef struct zio_gbh {
	blkptr_t		zg_blkptr[SPA_GBH_NBLKPTRS];
	uint64_t		zg_filler[SPA_GBH_FILLER];
	zio_block_tail_t	zg_tail;
} zio_gbh_phys_t;

enum zio_checksum {
	ZIO_CHECKSUM_INHERIT = 0,
	ZIO_CHECKSUM_ON,
	ZIO_CHECKSUM_OFF,
	ZIO_CHECKSUM_LABEL,
	ZIO_CHECKSUM_GANG_HEADER,
	ZIO_CHECKSUM_ZILOG,
	ZIO_CHECKSUM_FLETCHER_2,
	ZIO_CHECKSUM_FLETCHER_4,
	ZIO_CHECKSUM_SHA256,
	ZIO_CHECKSUM_FUNCTIONS
};

#define	ZIO_CHECKSUM_ON_VALUE	ZIO_CHECKSUM_FLETCHER_2
#define	ZIO_CHECKSUM_DEFAULT	ZIO_CHECKSUM_ON

enum zio_compress {
	ZIO_COMPRESS_INHERIT = 0,
	ZIO_COMPRESS_ON,
	ZIO_COMPRESS_OFF,
	ZIO_COMPRESS_LZJB,
	ZIO_COMPRESS_EMPTY,
	ZIO_COMPRESS_GZIP_1,
	ZIO_COMPRESS_GZIP_2,
	ZIO_COMPRESS_GZIP_3,
	ZIO_COMPRESS_GZIP_4,
	ZIO_COMPRESS_GZIP_5,
	ZIO_COMPRESS_GZIP_6,
	ZIO_COMPRESS_GZIP_7,
	ZIO_COMPRESS_GZIP_8,
	ZIO_COMPRESS_GZIP_9,
	ZIO_COMPRESS_FUNCTIONS
};

#define	ZIO_COMPRESS_ON_VALUE	ZIO_COMPRESS_LZJB
#define	ZIO_COMPRESS_DEFAULT	ZIO_COMPRESS_OFF

#define	ZIO_FAILURE_MODE_WAIT		0
#define	ZIO_FAILURE_MODE_CONTINUE	1
#define	ZIO_FAILURE_MODE_PANIC		2

#define	ZIO_PRIORITY_NOW		(zio_priority_table[0])
#define	ZIO_PRIORITY_SYNC_READ		(zio_priority_table[1])
#define	ZIO_PRIORITY_SYNC_WRITE		(zio_priority_table[2])
#define	ZIO_PRIORITY_ASYNC_READ		(zio_priority_table[3])
#define	ZIO_PRIORITY_ASYNC_WRITE	(zio_priority_table[4])
#define	ZIO_PRIORITY_FREE		(zio_priority_table[5])
#define	ZIO_PRIORITY_CACHE_FILL		(zio_priority_table[6])
#define	ZIO_PRIORITY_LOG_WRITE		(zio_priority_table[7])
#define	ZIO_PRIORITY_RESILVER		(zio_priority_table[8])
#define	ZIO_PRIORITY_SCRUB		(zio_priority_table[9])
#define	ZIO_PRIORITY_TABLE_SIZE		10

#define	ZIO_FLAG_MUSTSUCCEED		0x00000
#define	ZIO_FLAG_CANFAIL		0x00001
#define	ZIO_FLAG_FAILFAST		0x00002
#define	ZIO_FLAG_CONFIG_HELD		0x00004
#define	ZIO_FLAG_CONFIG_GRABBED		0x00008

#define	ZIO_FLAG_DONT_CACHE		0x00010
#define	ZIO_FLAG_DONT_QUEUE		0x00020
#define	ZIO_FLAG_DONT_PROPAGATE		0x00040
#define	ZIO_FLAG_DONT_RETRY		0x00080

#define	ZIO_FLAG_PHYSICAL		0x00100
#define	ZIO_FLAG_IO_BYPASS		0x00200
#define	ZIO_FLAG_IO_REPAIR		0x00400
#define	ZIO_FLAG_SPECULATIVE		0x00800

#define	ZIO_FLAG_RESILVER		0x01000
#define	ZIO_FLAG_SCRUB			0x02000
#define	ZIO_FLAG_SCRUB_THREAD		0x04000
#define	ZIO_FLAG_SUBBLOCK		0x08000

#define	ZIO_FLAG_NOBOOKMARK		0x10000
#define	ZIO_FLAG_USER			0x20000
#define	ZIO_FLAG_METADATA		0x40000
#define	ZIO_FLAG_WRITE_RETRY		0x80000

#define	ZIO_FLAG_GANG_INHERIT		\
	(ZIO_FLAG_CANFAIL |		\
	ZIO_FLAG_FAILFAST |		\
	ZIO_FLAG_CONFIG_HELD |		\
	ZIO_FLAG_DONT_CACHE |		\
	ZIO_FLAG_DONT_RETRY |		\
	ZIO_FLAG_IO_REPAIR |		\
	ZIO_FLAG_SPECULATIVE |		\
	ZIO_FLAG_RESILVER |		\
	ZIO_FLAG_SCRUB |		\
	ZIO_FLAG_SCRUB_THREAD |		\
	ZIO_FLAG_USER | 		\
	ZIO_FLAG_METADATA)

#define	ZIO_FLAG_VDEV_INHERIT		\
	(ZIO_FLAG_GANG_INHERIT |	\
	ZIO_FLAG_PHYSICAL)

#define	ZIO_FLAG_RETRY_INHERIT		\
	(ZIO_FLAG_VDEV_INHERIT |	\
	ZIO_FLAG_CONFIG_GRABBED |	\
	ZIO_FLAG_DONT_PROPAGATE |	\
	ZIO_FLAG_NOBOOKMARK)


#define	ZIO_PIPELINE_CONTINUE		0x100
#define	ZIO_PIPELINE_STOP		0x101

/*
 * We'll take the unused errnos, 'EBADE' and 'EBADR' (from the Convergent
 * graveyard) to indicate checksum errors and fragmentation.
 */
#define	ECKSUM	EBADE
#define	EFRAGS	EBADR

typedef struct zio zio_t;
typedef void zio_done_func_t(zio_t *zio);

extern uint8_t zio_priority_table[ZIO_PRIORITY_TABLE_SIZE];
extern char *zio_type_name[ZIO_TYPES];

/*
 * A bookmark is a four-tuple <objset, object, level, blkid> that uniquely
 * identifies any block in the pool.  By convention, the meta-objset (MOS)
 * is objset 0, the meta-dnode is object 0, the root block (osphys_t) is
 * level -1 of the meta-dnode, and intent log blocks (which are chained
 * off the root block) have blkid == sequence number.  In summary:
 *
 *	mos is objset 0
 *	meta-dnode is object 0
 *	root block is <objset, 0, -1, 0>
 *	intent log is <objset, 0, -1, ZIL sequence number>
 *
 * Note: this structure is called a bookmark because its first purpose was
 * to remember where to resume a pool-wide traverse.  The absolute ordering
 * for block visitation during traversal is defined in compare_bookmark().
 *
 * Note: this structure is passed between userland and the kernel.
 * Therefore it must not change size or alignment between 32/64 bit
 * compilation options.
 */
typedef struct zbookmark {
	uint64_t	zb_objset;
	uint64_t	zb_object;
	int64_t		zb_level;
	uint64_t	zb_blkid;
} zbookmark_t;

struct zio {
	/* Core information about this I/O */
	zio_t		*io_parent;
	zio_t		*io_root;
	spa_t		*io_spa;
	zbookmark_t	io_bookmark;
	enum zio_checksum io_checksum;
	enum zio_compress io_compress;
	int		io_ndvas;
	uint64_t	io_txg;
	blkptr_t	*io_bp;
	blkptr_t	io_bp_copy;
	zio_t		*io_child;
	zio_t		*io_sibling_prev;
	zio_t		*io_sibling_next;
	zio_transform_t *io_transform_stack;
	zio_t		*io_logical;
	list_node_t	zio_link_node;

	/* Callback info */
	zio_done_func_t	*io_ready;
	zio_done_func_t	*io_done;
	void		*io_private;
	blkptr_t	io_bp_orig;

	/* Data represented by this I/O */
	void		*io_data;
	uint64_t	io_size;

	/* Stuff for the vdev stack */
	vdev_t		*io_vd;
	void		*io_vsd;
	uint64_t	io_offset;
	uint64_t	io_deadline;
	uint64_t	io_timestamp;
	avl_node_t	io_offset_node;
	avl_node_t	io_deadline_node;
	avl_tree_t	*io_vdev_tree;
	zio_t		*io_delegate_list;
	zio_t		*io_delegate_next;

	/* Internal pipeline state */
	int		io_flags;
	int		io_orig_flags;
	enum zio_type	io_type;
	enum zio_stage	io_stage;
	enum zio_stage	io_orig_stage;
	uint8_t		io_stalled;
	uint8_t		io_priority;
	struct dk_callback io_dk_callback;
	int		io_cmd;
	int		io_retries;
	int		io_error;
	uint32_t	io_numerrors;
	uint32_t	io_pipeline;
	uint32_t	io_orig_pipeline;
	uint64_t	io_children_notready;
	uint64_t	io_children_notdone;
	void		*io_waiter;
	kmutex_t	io_lock;
	kcondvar_t	io_cv;

	/* FMA state */
	uint64_t	io_ena;
};

extern zio_t *zio_null(zio_t *pio, spa_t *spa,
    zio_done_func_t *done, void *private, int flags);

extern zio_t *zio_root(spa_t *spa,
    zio_done_func_t *done, void *private, int flags);

extern zio_t *zio_read(zio_t *pio, spa_t *spa, blkptr_t *bp, void *data,
    uint64_t size, zio_done_func_t *done, void *private,
    int priority, int flags, zbookmark_t *zb);

extern zio_t *zio_write(zio_t *pio, spa_t *spa, int checksum, int compress,
    int ncopies, uint64_t txg, blkptr_t *bp, void *data, uint64_t size,
    zio_done_func_t *ready, zio_done_func_t *done, void *private, int priority,
    int flags, zbookmark_t *zb);

extern zio_t *zio_rewrite(zio_t *pio, spa_t *spa, int checksum,
    uint64_t txg, blkptr_t *bp, void *data, uint64_t size,
    zio_done_func_t *done, void *private, int priority, int flags,
    zbookmark_t *zb);

extern zio_t *zio_free(zio_t *pio, spa_t *spa, uint64_t txg, blkptr_t *bp,
    zio_done_func_t *done, void *private);

extern zio_t *zio_claim(zio_t *pio, spa_t *spa, uint64_t txg, blkptr_t *bp,
    zio_done_func_t *done, void *private);

extern zio_t *zio_ioctl(zio_t *pio, spa_t *spa, vdev_t *vd, int cmd,
    zio_done_func_t *done, void *private, int priority, int flags);

extern zio_t *zio_read_phys(zio_t *pio, vdev_t *vd, uint64_t offset,
    uint64_t size, void *data, int checksum,
    zio_done_func_t *done, void *private, int priority, int flags,
    boolean_t labels);

extern zio_t *zio_write_phys(zio_t *pio, vdev_t *vd, uint64_t offset,
    uint64_t size, void *data, int checksum,
    zio_done_func_t *done, void *private, int priority, int flags,
    boolean_t labels);

extern int zio_alloc_blk(spa_t *spa, uint64_t size, blkptr_t *new_bp,
    blkptr_t *old_bp, uint64_t txg);
extern void zio_free_blk(spa_t *spa, blkptr_t *bp, uint64_t txg);
extern void zio_flush(zio_t *zio, vdev_t *vd);

extern int zio_wait(zio_t *zio);
extern void zio_nowait(zio_t *zio);
extern void zio_execute(zio_t *zio);
extern void zio_interrupt(zio_t *zio);

extern int zio_wait_for_children_ready(zio_t *zio);
extern int zio_wait_for_children_done(zio_t *zio);

extern void *zio_buf_alloc(size_t size);
extern void zio_buf_free(void *buf, size_t size);
extern void *zio_data_buf_alloc(size_t size);
extern void zio_data_buf_free(void *buf, size_t size);

extern void zio_resubmit_stage_async(void *);

/*
 * Delegate I/O to a child vdev.
 */
extern zio_t *zio_vdev_child_io(zio_t *zio, blkptr_t *bp, vdev_t *vd,
    uint64_t offset, void *data, uint64_t size, int type, int priority,
    int flags, zio_done_func_t *done, void *private);

extern void zio_vdev_io_bypass(zio_t *zio);
extern void zio_vdev_io_reissue(zio_t *zio);
extern void zio_vdev_io_redone(zio_t *zio);

extern void zio_checksum_verified(zio_t *zio);
extern void zio_set_gang_verifier(zio_t *zio, zio_cksum_t *zcp);

extern uint8_t zio_checksum_select(uint8_t child, uint8_t parent);
extern uint8_t zio_compress_select(uint8_t child, uint8_t parent);

extern boolean_t zio_should_retry(zio_t *zio);
extern int zio_vdev_resume_io(spa_t *);

/*
 * Initial setup and teardown.
 */
extern void zio_init(void);
extern void zio_fini(void);

/*
 * Fault injection
 */
struct zinject_record;
extern uint32_t zio_injection_enabled;
extern int zio_inject_fault(char *name, int flags, int *id,
    struct zinject_record *record);
extern int zio_inject_list_next(int *id, char *name, size_t buflen,
    struct zinject_record *record);
extern int zio_clear_fault(int id);
extern int zio_handle_fault_injection(zio_t *zio, int error);
extern int zio_handle_device_injection(vdev_t *vd, int error);

#ifdef	__cplusplus
}
#endif

#endif	/* _ZIO_H */
