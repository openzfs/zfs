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
 * Copyright (c) 2016, 2017 by Delphix. All rights reserved.
 */

#ifndef _SYS_UBERBLOCK_IMPL_H
#define	_SYS_UBERBLOCK_IMPL_H

#include <sys/uberblock.h>

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * The uberblock version is incremented whenever an incompatible on-disk
 * format change is made to the SPA, DMU, or ZAP.
 *
 * Note: the first two fields should never be moved.  When a storage pool
 * is opened, the uberblock must be read off the disk before the version
 * can be checked.  If the ub_version field is moved, we may not detect
 * version mismatch.  If the ub_magic field is moved, applications that
 * expect the magic number in the first word won't work.
 */
#define	UBERBLOCK_MAGIC		0x00bab10c		/* oo-ba-bloc!	*/
#define	UBERBLOCK_SHIFT		10			/* up to 1K	*/
#define	MMP_MAGIC		0xa11cea11		/* all-see-all	*/

#define	MMP_INTERVAL_VALID_BIT	0x01
#define	MMP_SEQ_VALID_BIT	0x02
#define	MMP_FAIL_INT_VALID_BIT	0x04

#define	MMP_VALID(ubp)		(ubp->ub_magic == UBERBLOCK_MAGIC && \
				    ubp->ub_mmp_magic == MMP_MAGIC)
#define	MMP_INTERVAL_VALID(ubp)	(MMP_VALID(ubp) && (ubp->ub_mmp_config & \
				    MMP_INTERVAL_VALID_BIT))
#define	MMP_SEQ_VALID(ubp)	(MMP_VALID(ubp) && (ubp->ub_mmp_config & \
				    MMP_SEQ_VALID_BIT))
#define	MMP_FAIL_INT_VALID(ubp)	(MMP_VALID(ubp) && (ubp->ub_mmp_config & \
				    MMP_FAIL_INT_VALID_BIT))

#define	MMP_INTERVAL(ubp)	((ubp->ub_mmp_config & 0x00000000FFFFFF00) \
				    >> 8)
#define	MMP_SEQ(ubp)		((ubp->ub_mmp_config & 0x0000FFFF00000000) \
				    >> 32)
#define	MMP_FAIL_INT(ubp)	((ubp->ub_mmp_config & 0xFFFF000000000000) \
				    >> 48)

#define	MMP_INTERVAL_SET(write) \
	    (((uint64_t)(write & 0xFFFFFF) << 8) | MMP_INTERVAL_VALID_BIT)

#define	MMP_SEQ_SET(seq) \
	    (((uint64_t)(seq & 0xFFFF) << 32) | MMP_SEQ_VALID_BIT)

#define	MMP_FAIL_INT_SET(fail) \
	    (((uint64_t)(fail & 0xFFFF) << 48) | MMP_FAIL_INT_VALID_BIT)

/*
 * RAIDZ expansion reflow information.
 *
 *	64      56      48      40      32      24      16      8       0
 *	+-------+-------+-------+-------+-------+-------+-------+-------+
 *	|Scratch |                    Reflow                            |
 *	| State  |                    Offset                            |
 *	+-------+-------+-------+-------+-------+-------+-------+-------+
 */
typedef enum raidz_reflow_scratch_state {
	RRSS_SCRATCH_NOT_IN_USE = 0,
	RRSS_SCRATCH_VALID,
	RRSS_SCRATCH_INVALID_SYNCED,
	RRSS_SCRATCH_INVALID_SYNCED_ON_IMPORT,
	RRSS_SCRATCH_INVALID_SYNCED_REFLOW
} raidz_reflow_scratch_state_t;

#define	RRSS_GET_OFFSET(ub) \
	BF64_GET_SB((ub)->ub_raidz_reflow_info, 0, 55, SPA_MINBLOCKSHIFT, 0)
#define	RRSS_SET_OFFSET(ub, x) \
	BF64_SET_SB((ub)->ub_raidz_reflow_info, 0, 55, SPA_MINBLOCKSHIFT, 0, x)

#define	RRSS_GET_STATE(ub) \
	BF64_GET((ub)->ub_raidz_reflow_info, 55, 9)
#define	RRSS_SET_STATE(ub, x) \
	BF64_SET((ub)->ub_raidz_reflow_info, 55, 9, x)

#define	RAIDZ_REFLOW_SET(ub, state, offset) do { \
	(ub)->ub_raidz_reflow_info = 0; \
	RRSS_SET_OFFSET(ub, offset); \
	RRSS_SET_STATE(ub, state); \
} while (0)

struct uberblock {
	uint64_t	ub_magic;	/* UBERBLOCK_MAGIC		*/
	uint64_t	ub_version;	/* SPA_VERSION			*/
	uint64_t	ub_txg;		/* txg of last sync		*/
	uint64_t	ub_guid_sum;	/* sum of all vdev guids	*/
	uint64_t	ub_timestamp;	/* UTC time of last sync	*/
	blkptr_t	ub_rootbp;	/* MOS objset_phys_t		*/

	/* highest SPA_VERSION supported by software that wrote this txg */
	uint64_t	ub_software_version;

	/* Maybe missing in uberblocks we read, but always written */
	uint64_t	ub_mmp_magic;	/* MMP_MAGIC			*/
	/*
	 * If ub_mmp_delay == 0 and ub_mmp_magic is valid, MMP is off.
	 * Otherwise, nanosec since last MMP write.
	 */
	uint64_t	ub_mmp_delay;

	/*
	 * The ub_mmp_config contains the multihost write interval, multihost
	 * fail intervals, sequence number for sub-second granularity, and
	 * valid bit mask.  This layout is as follows:
	 *
	 *   64      56      48      40      32      24      16      8       0
	 *   +-------+-------+-------+-------+-------+-------+-------+-------+
	 * 0 | Fail Intervals|      Seq      |   Write Interval (ms) | VALID |
	 *   +-------+-------+-------+-------+-------+-------+-------+-------+
	 *
	 * This allows a write_interval of (2^24/1000)s, over 4.5 hours
	 *
	 * VALID Bits:
	 * - 0x01 - Write Interval (ms)
	 * - 0x02 - Sequence number exists
	 * - 0x04 - Fail Intervals
	 * - 0xf8 - Reserved
	 */
	uint64_t	ub_mmp_config;

	/*
	 * ub_checkpoint_txg indicates two things about the current uberblock:
	 *
	 * 1] If it is not zero then this uberblock is a checkpoint. If it is
	 *    zero, then this uberblock is not a checkpoint.
	 *
	 * 2] On checkpointed uberblocks, the value of ub_checkpoint_txg is
	 *    the ub_txg that the uberblock had at the time we moved it to
	 *    the MOS config.
	 *
	 * The field is set when we checkpoint the uberblock and continues to
	 * hold that value even after we've rewound (unlike the ub_txg that
	 * is reset to a higher value).
	 *
	 * Besides checks used to determine whether we are reopening the
	 * pool from a checkpointed uberblock [see spa_ld_select_uberblock()],
	 * the value of the field is used to determine which ZIL blocks have
	 * been allocated according to the ms_sm when we are rewinding to a
	 * checkpoint. Specifically, if logical birth > ub_checkpoint_txg,then
	 * the ZIL block is not allocated [see uses of spa_min_claim_txg()].
	 */
	uint64_t	ub_checkpoint_txg;

	uint64_t	ub_raidz_reflow_info;
};

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_UBERBLOCK_IMPL_H */
