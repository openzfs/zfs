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
 * Copyright (c) 2018, Lawrence Livermore National Security, LLC.
 */

#ifndef	_SYS_FIEMAP_H
#define	_SYS_FIEMAP_H

/*
 * FIEMAP interface flags
 *
 * The following flags have been submitted for inclusion in future
 * Linux kernels and the filefrag(8) utility.
 */
#define	fe_device_reserved		fe_reserved[0]
#define	fe_physical_length_reserved	fe_reserved64[0]

/*
 * Request that all copies of an extent be reported.  They will be reported
 * as overlapping logical extents with different physical extents.
 */
#ifndef FIEMAP_FLAG_COPIES
#define	FIEMAP_FLAG_COPIES	0x08000000
#endif

/*
 * Request that each block be reported and not merged in to an extent.
 */
#ifndef FIEMAP_FLAG_NOMERGE
#define	FIEMAP_FLAG_NOMERGE	0x04000000
#endif

/*
 * Request that holes be reported as FIEMAP_EXTENT_UNWRITTEN extents.  This
 * flag can be used internally to implement version of SEEK_HOLE which
 * properly account for dirty data.
 */
#ifndef FIEMAP_FLAG_HOLES
#define	FIEMAP_FLAG_HOLES	0x02000000
#endif

/*
 * Extent is shared with other space.  Introduced in 2.6.33 kernel.
 */
#ifndef FIEMAP_EXTENT_SHARED
#define	FIEMAP_EXTENT_SHARED	0x00002000
#endif

#ifdef _KERNEL

#include <sys/spa.h>		/* for SPA_DVAS_PER_BP */
#include <sys/avl.h>
#include <sys/range_tree.h>

/*
 * Generic supported flags.  The flags FIEMAP_FLAG_COPIES, FIEMAP_FLAG_NOMERGE,
 * and FIEMAP_FLAG_HOLES are excluded from the compatibility check until they
 * are provided by a future Linux kernel.  Until then they are a ZFS specific
 * extension.
 */
#define	ZFS_FIEMAP_FLAGS_COMPAT	(FIEMAP_FLAG_SYNC)
#define	ZFS_FIEMAP_FLAGS_ZFS	(FIEMAP_FLAG_COPIES | FIEMAP_FLAG_NOMERGE | \
				FIEMAP_FLAG_HOLES)

typedef struct zfs_fiemap_entry {
	uint64_t fe_logical_start;
	uint64_t fe_logical_len;
	uint64_t fe_physical_start;
	uint64_t fe_physical_len;
	uint64_t fe_vdev;
	uint64_t fe_flags;
	avl_node_t fe_node;
} zfs_fiemap_entry_t;

typedef struct zfs_fiemap {
	avl_tree_t fm_extent_trees[SPA_DVAS_PER_BP];	/* extent trees */
	range_tree_t *fm_dirty_tree;	/* pending dirty ranges */
	range_tree_t *fm_free_tree;	/* pending free ranges */

	uint64_t fm_file_size;		/* cached inode size */
	uint64_t fm_block_size;		/* cached dnp block size */
	uint64_t fm_fill_count;		/* only used with FIEMAP_FLAG_NOMERGE */

	/* Immutable */
	uint64_t fm_start;		/* stat of requested range */
	uint64_t fm_length;		/* length of requested range */
	uint64_t fm_flags;		/* copy of fei.fi_flags */
	uint64_t fm_extents_max;	/* copy of fei.fi_extents_mapped */
	int fm_copies;
} zfs_fiemap_t;

#endif /* _KERNEL */
#endif	/* _SYS_FIEMAP_H */
