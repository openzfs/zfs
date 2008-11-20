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
 * Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef _ZIO_IMPL_H
#define	_ZIO_IMPL_H

#include <sys/zfs_context.h>
#include <sys/zio.h>

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * I/O Groups: pipeline stage definitions.
 */
typedef enum zio_stage {
	ZIO_STAGE_OPEN = 0,			/* RWFCI */
	ZIO_STAGE_WAIT_FOR_CHILDREN_READY,	/* RWFCI */

	ZIO_STAGE_READ_INIT,			/* R---- */
	ZIO_STAGE_ISSUE_ASYNC,			/* -W--- */
	ZIO_STAGE_WRITE_COMPRESS,		/* -W--- */
	ZIO_STAGE_CHECKSUM_GENERATE,		/* -W--- */

	ZIO_STAGE_GET_GANG_HEADER,		/* -WFC- */
	ZIO_STAGE_REWRITE_GANG_MEMBERS,		/* -W--- */
	ZIO_STAGE_FREE_GANG_MEMBERS,		/* --F-- */
	ZIO_STAGE_CLAIM_GANG_MEMBERS,		/* ---C- */

	ZIO_STAGE_DVA_ALLOCATE,			/* -W--- */
	ZIO_STAGE_DVA_FREE,			/* --F-- */
	ZIO_STAGE_DVA_CLAIM,			/* ---C- */

	ZIO_STAGE_GANG_CHECKSUM_GENERATE,	/* -W--- */

	ZIO_STAGE_READY,			/* RWFCI */

	ZIO_STAGE_VDEV_IO_START,		/* RW--I */
	ZIO_STAGE_VDEV_IO_DONE,			/* RW--I */
	ZIO_STAGE_VDEV_IO_ASSESS,		/* RW--I */

	ZIO_STAGE_WAIT_FOR_CHILDREN_DONE,	/* RWFCI */

	ZIO_STAGE_CHECKSUM_VERIFY,		/* R---- */
	ZIO_STAGE_READ_GANG_MEMBERS,		/* R---- */
	ZIO_STAGE_READ_DECOMPRESS,		/* R---- */

	ZIO_STAGE_ASSESS,			/* RWFCI */
	ZIO_STAGE_DONE				/* RWFCI */
} zio_stage_t;

#define	ZIO_INTERLOCK_STAGES					\
	((1U << ZIO_STAGE_WAIT_FOR_CHILDREN_READY) |		\
	(1U << ZIO_STAGE_READY) |				\
	(1U << ZIO_STAGE_WAIT_FOR_CHILDREN_DONE) |		\
	(1U << ZIO_STAGE_ASSESS) |				\
	(1U << ZIO_STAGE_DONE))

#define	ZIO_VDEV_IO_STAGES					\
	((1U << ZIO_STAGE_VDEV_IO_START) |			\
	(1U << ZIO_STAGE_VDEV_IO_DONE) |			\
	(1U << ZIO_STAGE_VDEV_IO_ASSESS))

#define	ZIO_READ_PHYS_PIPELINE					\
	(ZIO_INTERLOCK_STAGES |					\
	ZIO_VDEV_IO_STAGES |					\
	(1U << ZIO_STAGE_CHECKSUM_VERIFY))

#define	ZIO_READ_GANG_PIPELINE					\
	ZIO_READ_PHYS_PIPELINE

#define	ZIO_READ_PIPELINE					\
	(1U << ZIO_STAGE_READ_INIT) |				\
	ZIO_READ_PHYS_PIPELINE

#define	ZIO_WRITE_COMMON_STAGES					\
	(ZIO_INTERLOCK_STAGES |					\
	ZIO_VDEV_IO_STAGES |					\
	(1U << ZIO_STAGE_ISSUE_ASYNC) |				\
	(1U << ZIO_STAGE_CHECKSUM_GENERATE))

#define	ZIO_WRITE_PHYS_PIPELINE					\
	ZIO_WRITE_COMMON_STAGES

#define	ZIO_WRITE_PIPELINE					\
	(ZIO_WRITE_COMMON_STAGES |				\
	(1U << ZIO_STAGE_WRITE_COMPRESS) |			\
	(1U << ZIO_STAGE_DVA_ALLOCATE))

#define	ZIO_GANG_REWRITE_STAGES					\
	((1U << ZIO_STAGE_GET_GANG_HEADER) |			\
	(1U << ZIO_STAGE_REWRITE_GANG_MEMBERS) |		\
	(1U << ZIO_STAGE_GANG_CHECKSUM_GENERATE))

#define	ZIO_GANG_FREE_STAGES					\
	((1U << ZIO_STAGE_GET_GANG_HEADER) |			\
	(1U << ZIO_STAGE_FREE_GANG_MEMBERS))

#define	ZIO_GANG_CLAIM_STAGES					\
	((1U << ZIO_STAGE_GET_GANG_HEADER) |			\
	(1U << ZIO_STAGE_CLAIM_GANG_MEMBERS))

#define	ZIO_REWRITE_PIPELINE(bp)				\
	(ZIO_WRITE_COMMON_STAGES |				\
	(BP_IS_GANG(bp) ? ZIO_GANG_REWRITE_STAGES : 0))

#define	ZIO_WRITE_ALLOCATE_PIPELINE				\
	(ZIO_WRITE_COMMON_STAGES |				\
	(1U << ZIO_STAGE_DVA_ALLOCATE))

#define	ZIO_FREE_PIPELINE(bp)					\
	(ZIO_INTERLOCK_STAGES |					\
	(1U << ZIO_STAGE_DVA_FREE) |				\
	(BP_IS_GANG(bp) ? ZIO_GANG_FREE_STAGES : 0))

#define	ZIO_CLAIM_PIPELINE(bp)					\
	(ZIO_INTERLOCK_STAGES |					\
	(1U << ZIO_STAGE_DVA_CLAIM) |				\
	(BP_IS_GANG(bp) ? ZIO_GANG_CLAIM_STAGES : 0))

#define	ZIO_IOCTL_PIPELINE					\
	(ZIO_INTERLOCK_STAGES |					\
	ZIO_VDEV_IO_STAGES)


#define	ZIO_WAIT_FOR_CHILDREN_PIPELINE				\
	ZIO_INTERLOCK_STAGES

#define	ZIO_VDEV_CHILD_PIPELINE					\
	(ZIO_VDEV_IO_STAGES |					\
	(1U << ZIO_STAGE_ASSESS) |				\
	(1U << ZIO_STAGE_WAIT_FOR_CHILDREN_DONE) |		\
	(1U << ZIO_STAGE_DONE))

#define	ZIO_ERROR_PIPELINE_MASK					\
	ZIO_INTERLOCK_STAGES

typedef struct zio_transform zio_transform_t;
struct zio_transform {
	void		*zt_data;
	uint64_t	zt_size;
	uint64_t	zt_bufsize;
	zio_transform_t	*zt_next;
};

extern void zio_inject_init(void);
extern void zio_inject_fini(void);

#ifdef	__cplusplus
}
#endif

#endif	/* _ZIO_IMPL_H */
