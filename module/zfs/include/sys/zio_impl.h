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

	ZIO_STAGE_ISSUE_ASYNC,			/* -W--- */

	ZIO_STAGE_READ_BP_INIT,			/* R---- */
	ZIO_STAGE_WRITE_BP_INIT,		/* -W--- */

	ZIO_STAGE_CHECKSUM_GENERATE,		/* -W--- */

	ZIO_STAGE_GANG_ASSEMBLE,		/* RWFC- */
	ZIO_STAGE_GANG_ISSUE,			/* RWFC- */

	ZIO_STAGE_DVA_ALLOCATE,			/* -W--- */
	ZIO_STAGE_DVA_FREE,			/* --F-- */
	ZIO_STAGE_DVA_CLAIM,			/* ---C- */

	ZIO_STAGE_READY,			/* RWFCI */

	ZIO_STAGE_VDEV_IO_START,		/* RW--I */
	ZIO_STAGE_VDEV_IO_DONE,			/* RW--I */
	ZIO_STAGE_VDEV_IO_ASSESS,		/* RW--I */

	ZIO_STAGE_CHECKSUM_VERIFY,		/* R---- */

	ZIO_STAGE_DONE,				/* RWFCI */
	ZIO_STAGES
} zio_stage_t;

#define	ZIO_INTERLOCK_STAGES					\
	((1U << ZIO_STAGE_READY) |				\
	(1U << ZIO_STAGE_DONE))

#define	ZIO_INTERLOCK_PIPELINE					\
	ZIO_INTERLOCK_STAGES

#define	ZIO_VDEV_IO_STAGES					\
	((1U << ZIO_STAGE_VDEV_IO_START) |			\
	(1U << ZIO_STAGE_VDEV_IO_DONE) |			\
	(1U << ZIO_STAGE_VDEV_IO_ASSESS))

#define	ZIO_VDEV_CHILD_PIPELINE					\
	(ZIO_VDEV_IO_STAGES |					\
	(1U << ZIO_STAGE_DONE))

#define	ZIO_READ_COMMON_STAGES					\
	(ZIO_INTERLOCK_STAGES |					\
	ZIO_VDEV_IO_STAGES |					\
	(1U << ZIO_STAGE_CHECKSUM_VERIFY))

#define	ZIO_READ_PHYS_PIPELINE					\
	ZIO_READ_COMMON_STAGES

#define	ZIO_READ_PIPELINE					\
	(ZIO_READ_COMMON_STAGES |				\
	(1U << ZIO_STAGE_READ_BP_INIT))

#define	ZIO_WRITE_COMMON_STAGES					\
	(ZIO_INTERLOCK_STAGES |					\
	ZIO_VDEV_IO_STAGES |					\
	(1U << ZIO_STAGE_ISSUE_ASYNC) |				\
	(1U << ZIO_STAGE_CHECKSUM_GENERATE))

#define	ZIO_WRITE_PHYS_PIPELINE					\
	ZIO_WRITE_COMMON_STAGES

#define	ZIO_REWRITE_PIPELINE					\
	(ZIO_WRITE_COMMON_STAGES |				\
	(1U << ZIO_STAGE_WRITE_BP_INIT))

#define	ZIO_WRITE_PIPELINE					\
	(ZIO_WRITE_COMMON_STAGES |				\
	(1U << ZIO_STAGE_WRITE_BP_INIT) |			\
	(1U << ZIO_STAGE_DVA_ALLOCATE))

#define	ZIO_GANG_STAGES						\
	((1U << ZIO_STAGE_GANG_ASSEMBLE) |			\
	(1U << ZIO_STAGE_GANG_ISSUE))

#define	ZIO_FREE_PIPELINE					\
	(ZIO_INTERLOCK_STAGES |					\
	(1U << ZIO_STAGE_DVA_FREE))

#define	ZIO_CLAIM_PIPELINE					\
	(ZIO_INTERLOCK_STAGES |					\
	(1U << ZIO_STAGE_DVA_CLAIM))

#define	ZIO_IOCTL_PIPELINE					\
	(ZIO_INTERLOCK_STAGES |					\
	(1U << ZIO_STAGE_VDEV_IO_START) |			\
	(1U << ZIO_STAGE_VDEV_IO_ASSESS))

#define	ZIO_CONFIG_LOCK_BLOCKING_STAGES				\
	((1U << ZIO_STAGE_VDEV_IO_START) |			\
	(1U << ZIO_STAGE_DVA_ALLOCATE) |			\
	(1U << ZIO_STAGE_DVA_CLAIM))

extern void zio_inject_init(void);
extern void zio_inject_fini(void);

#ifdef	__cplusplus
}
#endif

#endif	/* _ZIO_IMPL_H */
