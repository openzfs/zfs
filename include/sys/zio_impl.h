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
 * Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

/*
 * Copyright (c) 2012, 2015 by Delphix. All rights reserved.
 * Copyright (c) 2024, Klara Inc.
 */

#ifndef _ZIO_IMPL_H
#define	_ZIO_IMPL_H

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * XXX -- Describe ZFS I/O pipeline here. Fill in as needed.
 *
 * The ZFS I/O pipeline is comprised of various stages which are defined
 * in the zio_stage enum below. The individual stages are used to construct
 * these basic I/O operations: Read, Write, Free, Claim, Ioctl and Trim.
 *
 * I/O operations: (XXX - provide detail for each of the operations)
 *
 * Read:
 * Write:
 * Free:
 * Claim:
 * Ioctl:
 * Trim:
 *
 * Although the most common pipeline are used by the basic I/O operations
 * above, there are some helper pipelines (one could consider them
 * sub-pipelines) which are used internally by the ZIO module and are
 * explained below:
 *
 * Interlock Pipeline:
 * The interlock pipeline is the most basic pipeline and is used by all
 * of the I/O operations. The interlock pipeline does not perform any I/O
 * and is used to coordinate the dependencies between I/Os that are being
 * issued (i.e. the parent/child relationship).
 *
 * Vdev child Pipeline:
 * The vdev child pipeline is responsible for performing the physical I/O.
 * It is in this pipeline where the I/O are queued and possibly cached.
 *
 * In addition to performing I/O, the pipeline is also responsible for
 * data transformations. The transformations performed are based on the
 * specific properties that user may have selected and modify the
 * behavior of the pipeline. Examples of supported transformations are
 * compression, dedup, and nop writes. Transformations will either modify
 * the data or the pipeline. This list below further describes each of
 * the supported transformations:
 *
 * Compression:
 * ZFS supports five different flavors of compression -- gzip, lzjb, lz4, zle,
 * and zstd. Compression occurs as part of the write pipeline and is
 * performed in the ZIO_STAGE_WRITE_BP_INIT stage.
 *
 * Block cloning:
 * The block cloning functionality introduces ZIO_STAGE_BRT_FREE stage which
 * is called during a free pipeline. If the block is referenced in the
 * Block Cloning Table (BRT) we will just decrease its reference counter
 * instead of actually freeing the block.
 *
 * Dedup:
 * Dedup reads are handled by the ZIO_STAGE_DDT_READ_START and
 * ZIO_STAGE_DDT_READ_DONE stages. These stages are added to an existing
 * read pipeline if the dedup bit is set on the block pointer.
 * Writing a dedup block is performed by the ZIO_STAGE_DDT_WRITE stage
 * and added to a write pipeline if a user has enabled dedup on that
 * particular dataset.
 *
 * NOP Write:
 * The NOP write feature is performed by the ZIO_STAGE_NOP_WRITE stage
 * and is added to an existing write pipeline if a cryptographically
 * secure checksum (i.e. SHA256) is enabled and compression is turned on.
 * The NOP write stage will compare the checksums of the current data
 * on-disk (level-0 blocks only) and the data that is currently being written.
 * If the checksum values are identical then the pipeline is converted to
 * an interlock pipeline skipping block allocation and bypassing the
 * physical I/O.  The nop write feature can handle writes in either
 * syncing or open context (i.e. zil writes) and as a result is mutually
 * exclusive with dedup.
 *
 * Encryption:
 * Encryption and authentication is handled by the ZIO_STAGE_ENCRYPT stage.
 * This stage determines how the encryption metadata is stored in the bp.
 * Decryption and MAC verification is performed during zio_decrypt() as a
 * transform callback. Encryption is mutually exclusive with nopwrite, because
 * blocks with the same plaintext will be encrypted with different salts and
 * IV's (if dedup is off), and therefore have different ciphertexts. For dedup
 * blocks we deterministically generate the IV and salt by performing an HMAC
 * of the plaintext, which is computationally expensive, but allows us to keep
 * support for encrypted dedup. See the block comment in zio_crypt.c for
 * details.
 */

/*
 * zio pipeline stage definitions
 */
enum zio_stage {
	ZIO_STAGE_OPEN			= 1 << 0,	/* RWFCIT */

	ZIO_STAGE_READ_BP_INIT		= 1 << 1,	/* R----- */
	ZIO_STAGE_WRITE_BP_INIT		= 1 << 2,	/* -W---- */
	ZIO_STAGE_FREE_BP_INIT		= 1 << 3,	/* --F--- */
	ZIO_STAGE_ISSUE_ASYNC		= 1 << 4,	/* -WF--T */
	ZIO_STAGE_WRITE_COMPRESS	= 1 << 5,	/* -W---- */

	ZIO_STAGE_ENCRYPT		= 1 << 6,	/* -W---- */
	ZIO_STAGE_CHECKSUM_GENERATE	= 1 << 7,	/* -W---- */

	ZIO_STAGE_NOP_WRITE		= 1 << 8,	/* -W---- */

	ZIO_STAGE_BRT_FREE		= 1 << 9,	/* --F--- */

	ZIO_STAGE_DDT_READ_START	= 1 << 10,	/* R----- */
	ZIO_STAGE_DDT_READ_DONE		= 1 << 11,	/* R----- */
	ZIO_STAGE_DDT_WRITE		= 1 << 12,	/* -W---- */
	ZIO_STAGE_DDT_FREE		= 1 << 13,	/* --F--- */

	ZIO_STAGE_GANG_ASSEMBLE		= 1 << 14,	/* RWFC-- */
	ZIO_STAGE_GANG_ISSUE		= 1 << 15,	/* RWFC-- */

	ZIO_STAGE_DVA_THROTTLE		= 1 << 16,	/* -W---- */
	ZIO_STAGE_DVA_ALLOCATE		= 1 << 17,	/* -W---- */
	ZIO_STAGE_DVA_FREE		= 1 << 18,	/* --F--- */
	ZIO_STAGE_DVA_CLAIM		= 1 << 19,	/* ---C-- */

	ZIO_STAGE_READY			= 1 << 20,	/* RWFCIT */

	ZIO_STAGE_VDEV_IO_START		= 1 << 21,	/* RW--IT */
	ZIO_STAGE_VDEV_IO_DONE		= 1 << 22,	/* RW---T */
	ZIO_STAGE_VDEV_IO_ASSESS	= 1 << 23,	/* RW--IT */

	ZIO_STAGE_CHECKSUM_VERIFY	= 1 << 24,	/* R----- */

	ZIO_STAGE_DONE			= 1 << 25	/* RWFCIT */
};

#define	ZIO_ROOT_PIPELINE			\
	ZIO_STAGE_DONE

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
	ZIO_STAGE_WRITE_COMPRESS |		\
	ZIO_STAGE_ENCRYPT |			\
	ZIO_STAGE_WRITE_BP_INIT)

#define	ZIO_WRITE_PIPELINE			\
	(ZIO_WRITE_COMMON_STAGES |		\
	ZIO_STAGE_WRITE_BP_INIT |		\
	ZIO_STAGE_WRITE_COMPRESS |		\
	ZIO_STAGE_ENCRYPT |			\
	ZIO_STAGE_DVA_THROTTLE |		\
	ZIO_STAGE_DVA_ALLOCATE)

#define	ZIO_DDT_CHILD_WRITE_PIPELINE		\
	(ZIO_INTERLOCK_STAGES |			\
	ZIO_VDEV_IO_STAGES |			\
	ZIO_STAGE_DVA_THROTTLE |		\
	ZIO_STAGE_DVA_ALLOCATE)

#define	ZIO_DDT_WRITE_PIPELINE			\
	(ZIO_INTERLOCK_STAGES |			\
	ZIO_STAGE_WRITE_BP_INIT |		\
	ZIO_STAGE_ISSUE_ASYNC |			\
	ZIO_STAGE_WRITE_COMPRESS |		\
	ZIO_STAGE_ENCRYPT |			\
	ZIO_STAGE_CHECKSUM_GENERATE |		\
	ZIO_STAGE_DDT_WRITE)

#define	ZIO_GANG_STAGES				\
	(ZIO_STAGE_GANG_ASSEMBLE |		\
	ZIO_STAGE_GANG_ISSUE)

#define	ZIO_FREE_PIPELINE			\
	(ZIO_INTERLOCK_STAGES |			\
	ZIO_STAGE_FREE_BP_INIT |		\
	ZIO_STAGE_BRT_FREE |			\
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

#define	ZIO_TRIM_PIPELINE			\
	(ZIO_INTERLOCK_STAGES |			\
	ZIO_STAGE_ISSUE_ASYNC |			\
	ZIO_VDEV_IO_STAGES)

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
