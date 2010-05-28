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

#ifndef _ZIO_IMPL_H
#define	_ZIO_IMPL_H

#include <sys/zfs_context.h>
#include <sys/zio.h>

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * zio pipeline stage definitions
 */
enum zio_stage {
	ZIO_STAGE_OPEN			= 1 << 0,	/* RWFCI */

	ZIO_STAGE_READ_BP_INIT		= 1 << 1,	/* R---- */
	ZIO_STAGE_FREE_BP_INIT		= 1 << 2,	/* --F-- */
	ZIO_STAGE_ISSUE_ASYNC		= 1 << 3,	/* RWF-- */
	ZIO_STAGE_WRITE_BP_INIT		= 1 << 4,	/* -W--- */

	ZIO_STAGE_CHECKSUM_GENERATE	= 1 << 5,	/* -W--- */

	ZIO_STAGE_DDT_READ_START	= 1 << 6,	/* R---- */
	ZIO_STAGE_DDT_READ_DONE		= 1 << 7,	/* R---- */
	ZIO_STAGE_DDT_WRITE		= 1 << 8,	/* -W--- */
	ZIO_STAGE_DDT_FREE		= 1 << 9,	/* --F-- */

	ZIO_STAGE_GANG_ASSEMBLE		= 1 << 10,	/* RWFC- */
	ZIO_STAGE_GANG_ISSUE		= 1 << 11,	/* RWFC- */

	ZIO_STAGE_DVA_ALLOCATE		= 1 << 12,	/* -W--- */
	ZIO_STAGE_DVA_FREE		= 1 << 13,	/* --F-- */
	ZIO_STAGE_DVA_CLAIM		= 1 << 14,	/* ---C- */

	ZIO_STAGE_READY			= 1 << 15,	/* RWFCI */

	ZIO_STAGE_VDEV_IO_START		= 1 << 16,	/* RW--I */
	ZIO_STAGE_VDEV_IO_DONE		= 1 << 17,	/* RW--I */
	ZIO_STAGE_VDEV_IO_ASSESS	= 1 << 18,	/* RW--I */

	ZIO_STAGE_CHECKSUM_VERIFY	= 1 << 19,	/* R---- */

	ZIO_STAGE_DONE			= 1 << 20	/* RWFCI */
};

#define	ZIO_INTERLOCK_STAGES			\
	(ZIO_STAGE_READY |			\
	ZIO_STAGE_DONE)

#define	ZIO_INTERLOCK_PIPELINE			\
	ZIO_INTERLOCK_STAGES

#define	ZIO_VDEV_IO_STAGES			\
	(ZIO_STAGE_VDEV_IO_START |		\
	ZIO_STAGE_VDEV_IO_DONE |		\
	ZIO_STAGE_VDEV_IO_ASSESS)

#define	ZIO_VDEV_CHILD_PIPELINE			\
	(ZIO_VDEV_IO_STAGES |			\
	ZIO_STAGE_DONE)

#define	ZIO_READ_COMMON_STAGES			\
	(ZIO_INTERLOCK_STAGES |			\
	ZIO_VDEV_IO_STAGES |			\
	ZIO_STAGE_CHECKSUM_VERIFY)

#define	ZIO_READ_PHYS_PIPELINE			\
	ZIO_READ_COMMON_STAGES

#define	ZIO_READ_PIPELINE			\
	(ZIO_READ_COMMON_STAGES |		\
	ZIO_STAGE_READ_BP_INIT)

#define	ZIO_DDT_CHILD_READ_PIPELINE		\
	ZIO_READ_COMMON_STAGES

#define	ZIO_DDT_READ_PIPELINE			\
	(ZIO_INTERLOCK_STAGES |			\
	ZIO_STAGE_READ_BP_INIT |		\
	ZIO_STAGE_DDT_READ_START |		\
	ZIO_STAGE_DDT_READ_DONE)

#define	ZIO_WRITE_COMMON_STAGES			\
	(ZIO_INTERLOCK_STAGES |			\
	ZIO_VDEV_IO_STAGES |			\
	ZIO_STAGE_ISSUE_ASYNC |			\
	ZIO_STAGE_CHECKSUM_GENERATE)

#define	ZIO_WRITE_PHYS_PIPELINE			\
	ZIO_WRITE_COMMON_STAGES

#define	ZIO_REWRITE_PIPELINE			\
	(ZIO_WRITE_COMMON_STAGES |		\
	ZIO_STAGE_WRITE_BP_INIT)

#define	ZIO_WRITE_PIPELINE			\
	(ZIO_WRITE_COMMON_STAGES |		\
	ZIO_STAGE_WRITE_BP_INIT |		\
	ZIO_STAGE_DVA_ALLOCATE)

#define	ZIO_DDT_CHILD_WRITE_PIPELINE		\
	(ZIO_INTERLOCK_STAGES |			\
	ZIO_VDEV_IO_STAGES |			\
	ZIO_STAGE_DVA_ALLOCATE)

#define	ZIO_DDT_WRITE_PIPELINE			\
	(ZIO_INTERLOCK_STAGES |			\
	ZIO_STAGE_ISSUE_ASYNC |			\
	ZIO_STAGE_WRITE_BP_INIT |		\
	ZIO_STAGE_CHECKSUM_GENERATE |		\
	ZIO_STAGE_DDT_WRITE)

#define	ZIO_GANG_STAGES				\
	(ZIO_STAGE_GANG_ASSEMBLE |		\
	ZIO_STAGE_GANG_ISSUE)

#define	ZIO_FREE_PIPELINE			\
	(ZIO_INTERLOCK_STAGES |			\
	ZIO_STAGE_FREE_BP_INIT |		\
	ZIO_STAGE_DVA_FREE)

#define	ZIO_DDT_FREE_PIPELINE			\
	(ZIO_INTERLOCK_STAGES |			\
	ZIO_STAGE_FREE_BP_INIT |		\
	ZIO_STAGE_ISSUE_ASYNC |			\
	ZIO_STAGE_DDT_FREE)

#define	ZIO_CLAIM_PIPELINE			\
	(ZIO_INTERLOCK_STAGES |			\
	ZIO_STAGE_DVA_CLAIM)

#define	ZIO_IOCTL_PIPELINE			\
	(ZIO_INTERLOCK_STAGES |			\
	ZIO_STAGE_VDEV_IO_START |		\
	ZIO_STAGE_VDEV_IO_ASSESS)

#define	ZIO_BLOCKING_STAGES			\
	(ZIO_STAGE_DVA_ALLOCATE |		\
	ZIO_STAGE_DVA_CLAIM |			\
	ZIO_STAGE_VDEV_IO_START)

extern void zio_inject_init(void);
extern void zio_inject_fini(void);

#ifdef	__cplusplus
}
#endif

#endif	/* _ZIO_IMPL_H */
