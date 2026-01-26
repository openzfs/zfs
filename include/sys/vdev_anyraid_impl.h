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
 * Copyright (c) 2025, Klara Inc.
 */

#ifndef _SYS_VDEV_ANYRAID_IMPL_H
#define	_SYS_VDEV_ANYRAID_IMPL_H

#include <sys/types.h>
#include <sys/bitops.h>
#include <sys/zfs_rlock.h>

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * ==========================================================================
 * Internal structures & definitions
 * ==========================================================================
 */
typedef struct anyraid_free_node {
	avl_node_t	afn_node;
	uint16_t	afn_tile;
} anyraid_free_node_t;

typedef struct anyraid_freelist {
	avl_tree_t	af_list;
	uint16_t	af_next_off;
} anyraid_freelist_t;

void anyraid_freelist_create(anyraid_freelist_t *, uint16_t);
void anyraid_freelist_destroy(anyraid_freelist_t *);
void anyraid_freelist_add(anyraid_freelist_t *, uint16_t);
void anyraid_freelist_remove(anyraid_freelist_t *, uint16_t);
uint16_t anyraid_freelist_pop(anyraid_freelist_t *);
uint16_t anyraid_freelist_alloc(const anyraid_freelist_t *);
boolean_t anyraid_freelist_isfree(const anyraid_freelist_t *af, uint16_t off);

typedef struct vdev_anyraid_node {
	avl_node_t	van_node;
	uint8_t		van_id;
	anyraid_freelist_t	van_freelist;
	uint32_t	van_capacity;
} vdev_anyraid_node_t;

typedef struct anyraid_tile_node {
	list_node_t	atn_node;
	uint8_t		atn_disk;
	uint16_t	atn_tile_idx;
} anyraid_tile_node_t;

typedef struct anyraid_tile {
	avl_node_t	at_node;
	uint32_t	at_tile_id;
	list_t		at_list;
	uint64_t	at_synced;
} anyraid_tile_t;

typedef struct anyraid_move_arg {
	vdev_anyraid_relocate_t *ama_var;
	zio_t			*ama_zio;
	zfs_locked_range_t	*ama_lr;
	uint64_t		ama_txg;
	uint64_t		ama_size;
	uint32_t		ama_tid;
} anyraid_move_arg_t;

typedef struct relocate_phys {
	uint64_t	rp_done;
	uint64_t	rp_total;
} relocate_phys_t;

typedef struct relocate_task_phys {
	uint32_t	rtp_source_disk;
	uint32_t	rtp_dest_disk;
	uint32_t	rtp_source_idx;
	uint32_t	rtp_dest_idx;
	uint32_t	rtp_tile;
	uint32_t	rtp_task;
	uint64_t	rtp_pad2;
} relocate_task_phys_t;

_Static_assert(sizeof (relocate_task_phys_t) == 32,
	"relocate_task_phys_t size wrong");

/*
 * The ondisk structure of the anyraid tile map is VDEV_ANYRAID_MAP_COPIES
 * copies of the following layout. We store the tile map on every disk, and
 * each TXG we update a different copy (txg % VDEV_ANYRAID_MAP_COPIES).
 *
 * First, we start with a MAX(8KiB, 1 << ashift) region that stores a packed
 * nvlist containing the header. The header contains a version number, a disk
 * ID, a TXG, the tile size (in bytes), the stripe width/parity of the
 * tiles, the length of the mapping (in bytes), the pool guid, and the
 * checksum of the mapping. This 8KiB region has an embedded checksum so that
 * uses the normal ZIO_CHECKSUM_LABEL algorithm.
 *
 * Then, there is a tile of size VDEV_ANYRAID_MAP_SIZE. This stores the actual
 * mapping. It is a series of entries. Right now, there are two entry types:
 *
 * 0: Skip entries represent a gap in logical tile IDs. From the current
 * tile ID, add the value stored in the lower 24 bits of the skip entry.
 *
 * 1: Location entries represent a mapped tile. Each one represents a single
 * physical tile backing the current logical tile. There can be multiple
 * physical tiles for one logical tile; that number is the stripe width/
 * parity from the header. These entries contain a 8 bit disk ID and a 16 bit
 * offset on that disk.
 *
 * Here is an example of what the mapping looks like on disk. This is for a
 * 1-parity mirror anyraid device:
 *
 * +----------+----------+----------+----------+----------+----------+
 * | Tile 0   | Tile 0   | Tile 1   | Tile 1   | Tile 2   | Tile 2   |
 * | Parity 0 | Parity 1 | Parity 0 | Parity 1 | Parity 0 | Parity 1 |
 * | Disk 0   | Disk 1   | Disk 0   | Disk 2   | Disk 0   | Disk 1   |
 * | Offset 0 | Offset 0 | Offset 1 | Offset 0 | Offset 2 | Offset 1 |
 * +----------+----------+----------+----------+----------+----------+
 *
 * Note that each of these entries acutally only contains the "disk" and
 * "offset" fields on-disk; the "tile" and "parity" information is derived from
 * context (since the entries are stored in tile/offset order, with no gaps
 * unless a skip entry is present).
 *
 * New entry types will be added eventually to store information like parity
 * changes.
 *
 * Because the mapping can be larger than the SPA_MAXBLOCKSIZE, it has to be
 * written in multiple IOs; each IO-sized region has their own checksum, which
 * is stored in the header block (using the ZIO_CHECKSUM_ANYRAID_MAP algorithm).
 */

/*
 * ==========================================================================
 * Header-related definitions
 * ==========================================================================
 */
#define	VDEV_ANYRAID_HEADER_VERSION	"version"
#define	VDEV_ANYRAID_HEADER_DISK	"disk"
#define	VDEV_ANYRAID_HEADER_TXG		"txg"
#define	VDEV_ANYRAID_HEADER_TILE_SIZE	"tile_size"
#define	VDEV_ANYRAID_HEADER_LENGTH	"length"
#define	VDEV_ANYRAID_HEADER_CHECKPOINT	"checkpoint_txg"
#define	VDEV_ANYRAID_HEADER_DISK_SIZES	"sizes"
#define	VDEV_ANYRAID_HEADER_RELOC_STATE	"state"
#define	VDEV_ANYRAID_HEADER_CUR_TASK	"cur_task"
#define	VDEV_ANYRAID_HEADER_CONTRACTING_LEAF	"contracting_leaf"

#define	VART_TILE		"tile"
#define	VART_SOURCE_DISK	"source_disk"
#define	VART_SOURCE_OFF		"source_off"
#define	VART_DEST_DISK		"dest_disk"
#define	VART_DEST_OFF		"dest_off"
#define	VART_OFFSET		"offset"
#define	VART_TASK		"task"
/*
 * We store the pool guid to prevent disks being reused from an old pool from
 * causing any issues.
 */
#define	VDEV_ANYRAID_HEADER_GUID	"guid"

#define	VDEV_ANYRAID_MAP_HEADER_SIZE(ashift)	MAX(8 * 1024, 1ULL << (ashift))

#define	VDEV_ANYRAID_NVL_BYTES(ashift)	\
	(VDEV_ANYRAID_MAP_HEADER_SIZE(ashift) - \
	(VDEV_ANYRAID_MAP_COPIES + 1) * sizeof (zio_eck_t))

/*
 * ==========================================================================
 * Mapping-related definitions
 * ==========================================================================
 */
typedef enum anyraid_map_entry_type {
	AMET_SKIP = 0,
	AMET_LOC = 1,
	AMET_TYPES
} anyraid_map_entry_type_t;

#define	AME_TYPE_BITS	8

/*
 * ==========================================================================
 * Skip entry definitions and functions
 * ==========================================================================
 */
typedef uint32_t anyraid_map_skip_entry_t;

#define	AMSE_TILE_BITS	24

static inline void
amse_set_type(anyraid_map_skip_entry_t *amse)
{
	BF32_SET(*amse, 0, AME_TYPE_BITS, AMET_SKIP);
}

static inline void
amse_set_skip_count(anyraid_map_skip_entry_t *amse, uint32_t skip_count)
{
	BF32_SET(*amse, AME_TYPE_BITS, AMSE_TILE_BITS, skip_count);
}

static inline uint32_t
amse_get_skip_count(anyraid_map_skip_entry_t *amse)
{
	return (BF32_GET(*amse, AME_TYPE_BITS, AMSE_TILE_BITS));
}

/*
 * ==========================================================================
 * Location entry definitions and functions
 * ==========================================================================
 */
typedef uint32_t anyraid_map_loc_entry_t;

#define	AMLE_DISK_BITS		8
#define	AMLE_OFFSET_BITS	16

static inline void
amle_set_type(anyraid_map_loc_entry_t *amle)
{
	BF32_SET(*amle, 0, AME_TYPE_BITS, AMET_LOC);
}

static inline void
amle_set_disk(anyraid_map_loc_entry_t *amle, uint8_t disk)
{
	BF32_SET(*amle, AME_TYPE_BITS, AMLE_DISK_BITS, disk);
}

static inline uint32_t
amle_get_disk(anyraid_map_loc_entry_t *amle)
{
	return (BF32_GET(*amle, AME_TYPE_BITS, AMLE_DISK_BITS));
}

static inline void
amle_set_offset(anyraid_map_loc_entry_t *amle, uint8_t offset)
{
	BF32_SET(*amle, (AME_TYPE_BITS + AMLE_DISK_BITS), AMLE_OFFSET_BITS,
	    offset);
}

static inline uint32_t
amle_get_offset(anyraid_map_loc_entry_t *amle)
{
	return (BF32_GET(*amle, (AME_TYPE_BITS + AMLE_DISK_BITS),
	    AMLE_OFFSET_BITS));
}

/*
 * ==========================================================================
 * Overall mapping definitions
 * ==========================================================================
 */

typedef struct anyraid_map_entry {
	union {
		anyraid_map_skip_entry_t ame_amse;
		anyraid_map_loc_entry_t ame_amle;
	} ame_u;
} anyraid_map_entry_t;

static inline anyraid_map_entry_type_t
ame_get_type(anyraid_map_entry_t *ame)
{
	return (BF32_GET(ame->ame_u.ame_amle, 0, AME_TYPE_BITS));
}

#define	VDEV_ANYRAID_MAX_TPD	(1 << 16)
#define	VDEV_ANYRAID_MAX_TILES	(VDEV_ANYRAID_MAX_DISKS * VDEV_ANYRAID_MAX_TPD)
/*
 * The worst case scenario here is that we have a loc entry for every single
 * tile (0 skips). At that point, we're using 4 bytes per tile.
 * That gives us 2^24 * 4 bytes = 64 MB to store the entire map.
 */
#define	VDEV_ANYRAID_MAP_SIZE	(sizeof (anyraid_map_loc_entry_t) * \
	VDEV_ANYRAID_MAX_TILES)
#define	VDEV_ANYRAID_SINGLE_MAP_SIZE(ashift)	\
	((VDEV_ANYRAID_MAP_HEADER_SIZE(ashift) + VDEV_ANYRAID_MAP_SIZE))
#define	VDEV_ANYRAID_MAP_COPIES		4
#define	VDEV_ANYRAID_START_COPIES	(VDEV_ANYRAID_MAP_COPIES / 2)
#define	VDEV_ANYRAID_TOTAL_MAP_SIZE(ashift)	(VDEV_ANYRAID_MAP_COPIES * \
	VDEV_ANYRAID_SINGLE_MAP_SIZE(ashift))
#define	VDEV_ANYRAID_START_OFFSET(ashift)	VDEV_ANYRAID_START_COPIES * \
	VDEV_ANYRAID_SINGLE_MAP_SIZE(ashift)

_Static_assert(VDEV_ANYRAID_TOTAL_MAP_SIZE(9) % SPA_MINBLOCKSIZE == 0, "");
_Static_assert(VDEV_ANYRAID_TOTAL_MAP_SIZE(12) % SPA_MINBLOCKSIZE == 0, "");
_Static_assert(VDEV_ANYRAID_MAP_SIZE % SPA_MAXBLOCKSIZE == 0, "");

/*
 * These functions are exposed for ZDB.
 */

typedef struct anyraid_header {
	abd_t *ah_abd;
	char *ah_buf;
	nvlist_t *ah_nvl;
} anyraid_header_t;

int vdev_anyraid_pick_best_mapping(vdev_t *cvd,
    uint64_t *out_txg, anyraid_header_t *out_header, int *out_mapping);
int vdev_anyraid_open_header(vdev_t *cvd, int header,
    anyraid_header_t *out_header);

#ifdef	__cplusplus
}
#endif

#endif /* _SYS_VDEV_ANYRAID_IMPL_H */
