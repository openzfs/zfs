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
 * Copyright (c) 2010, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef	_SYS_FS_ZFS_STAT_H
#define	_SYS_FS_ZFS_STAT_H

#ifdef _KERNEL
#include <sys/isa_defs.h>
#include <sys/types32.h>
#include <sys/dmu.h>
#endif

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * A limited number of zpl level stats are retrievable
 * with an ioctl.  zfs diff is the current consumer.
 */
typedef struct zfs_stat {
	uint64_t	zs_gen;
	uint64_t	zs_mode;
	uint64_t	zs_links;
	uint64_t	zs_ctime[2];
} zfs_stat_t;

extern int zfs_obj_to_stats(objset_t *osp, uint64_t obj, zfs_stat_t *sb,
    char *buf, int len, nvlist_t *nv);

/*
 * The legacy behavior of ZFS_IOC_OBJ_TO_STATS is to return a zfs_stat_t struct.
 * However, if the user passes in a nvlist dst buffer, we also return
 * "extended" object stats.  Currently, these extended stats are handpicked
 * fields from dmu_object_info_t, but they could be expanded to include
 * anything.
 */
#define	ZFS_OBJ_STAT_DATA_BLOCK_SIZE		"data_block_size"
#define	ZFS_OBJ_STAT_METADATA_BLOCK_SIZE	"metadata_block_size"
#define	ZFS_OBJ_STAT_DNODE_SIZE			"dnode_size"
#define	ZFS_OBJ_STAT_TYPE			"type"
#define	ZFS_OBJ_STAT_TYPE_STR			"type_str"
#define	ZFS_OBJ_STAT_BONUS_TYPE			"bonus_type"
#define	ZFS_OBJ_STAT_BONUS_TYPE_STR		"bonus_type_str"
#define	ZFS_OBJ_STAT_BONUS_SIZE			"bonus_size"
#define	ZFS_OBJ_STAT_CHECKSUM			"checksum"
#define	ZFS_OBJ_STAT_COMPRESS			"compress"
#define	ZFS_OBJ_STAT_PHYSICAL_BLOCKS_512	"physical_blocks_512"
#define	ZFS_OBJ_STAT_MAX_OFFSET			"max_offset"
#define	ZFS_OBJ_STAT_FILL_COUNT			"fill_count"

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_FS_ZFS_STAT_H */
