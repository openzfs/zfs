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

#ifndef _SYS_VDEV_ANYRAID_H
#define	_SYS_VDEV_ANYRAID_H

#include <sys/types.h>
#include <sys/bitops.h>
#include <sys/vdev.h>

#ifdef	__cplusplus
extern "C" {
#endif

typedef enum vdev_anyraid_parity_type {
	VAP_MIRROR, // includes raid0, i.e. a 0-parity mirror
	VAP_TYPES,
} vdev_anyraid_parity_type_t;

typedef struct vdev_anyraid_node {
	avl_node_t	van_node;
	uint8_t		van_id;
	uint16_t	van_next_offset;
	uint16_t	van_capacity;
} vdev_anyraid_node_t;

typedef struct vdev_anyraid {
	vdev_anyraid_parity_type_t vd_parity_type;
	/*
	 * The parity of the mismatched vdev; 0 for raid0, or the number of
	 * mirrors.
	 */
	uint_t		vd_nparity;
	uint64_t	vd_tile_size;

	krwlock_t	vd_lock;
	avl_tree_t	vd_tile_map;
	avl_tree_t	vd_children_tree;
	uint32_t	vd_checkpoint_tile;
	vdev_anyraid_node_t **vd_children;
} vdev_anyraid_t;

typedef struct anyraid_tile_node {
	list_node_t	atn_node;
	uint8_t		atn_disk;
	uint16_t	atn_offset;
} anyraid_tile_node_t;

typedef struct anyraid_tile {
	avl_node_t	at_node;
	uint32_t	at_tile_id;
	list_t		at_list;
} anyraid_tile_t;

/*
 * The ondisk structure of the anyraid tile map is VDEV_ANYRAID_MAP_COPIES
 * copies of the following layout. We store the tile map on every disk, and
 * each TXG we update a different copy (txg % VDEV_ANYRAID_MAP_COPIES).
 *
 * First, we start with a MAX(8KiB, 1 << ashift) tile that stores a packed
 * nvlist containing the header. The header contains a version number, a disk
 * ID, a TXG, the tile size (in bytes), the stripe width/parity of the
 * tiles, the length of the mapping (in bytes), the pool guid, and the
 * checksum of the mapping. This 4KiB tile has an embedded checksum so that
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

/*
 * ==========================================================================
 * Skip entry definitions and functions
 * ==========================================================================
 */
typedef struct anyraid_map_skip_entry {
	union {
		uint8_t amse_type;
		uint32_t amse_tile_id; // tile count to skip ahead
	} amse_u;
} anyraid_map_skip_entry_t;

#define	AMSE_TILE_BITS	24

static inline void
amse_set_type(anyraid_map_skip_entry_t *amse)
{
	amse->amse_u.amse_type = AMET_SKIP;
	ASSERT3U(amse->amse_u.amse_type, ==,
	    BF32_GET(amse->amse_u.amse_type, 0, 8));
}

static inline void
amse_set_tile_id(anyraid_map_skip_entry_t *amse, uint32_t tile_id)
{
	BF32_SET(amse->amse_u.amse_tile_id, 8, AMSE_TILE_BITS, tile_id);
}

static inline uint32_t
amse_get_tile_id(anyraid_map_skip_entry_t *amse)
{
	return (BF32_GET(amse->amse_u.amse_tile_id, 8, AMSE_TILE_BITS));
}

/*
 * ==========================================================================
 * Location entry definitions and functions
 * ==========================================================================
 */
typedef struct anyraid_map_loc_entry {
	uint8_t amle_type;
	uint8_t amle_disk;
	uint16_t amle_offset;
} anyraid_map_loc_entry_t;
_Static_assert(sizeof (anyraid_map_loc_entry_t) == sizeof (uint32_t), "");

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

#define	VDEV_ANYRAID_MAX_DISKS	(1 << 8)
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
#define	VDEV_ANYRAID_TOTAL_MAP_SIZE(ashift)	(VDEV_ANYRAID_MAP_COPIES * \
	VDEV_ANYRAID_SINGLE_MAP_SIZE(ashift))

_Static_assert(VDEV_ANYRAID_TOTAL_MAP_SIZE(9) % SPA_MINBLOCKSIZE == 0, "");
_Static_assert(VDEV_ANYRAID_TOTAL_MAP_SIZE(12) % SPA_MINBLOCKSIZE == 0, "");
_Static_assert(VDEV_ANYRAID_MAP_SIZE % SPA_MAXBLOCKSIZE == 0, "");

/*
 * ==========================================================================
 * Externally-accessed function definitions
 * ==========================================================================
 */
void vdev_anyraid_write_map_sync(vdev_t *vd, zio_t *pio, uint64_t txg,
    uint64_t *good_writes, int flags, vdev_config_sync_status_t status);

uint64_t vdev_anyraid_min_newsize(vdev_t *vd, uint64_t ashift);
void vdev_anyraid_expand(vdev_t *tvd, vdev_t *newvd);

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

#endif /* _SYS_VDEV_ANYRAID_H */
