// SPDX-License-Identifier: CDDL-1.0
/*
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this
 * source.  A copy of the CDDL is also available via the Internet at
 * https://opensource.org/license/CDDL-1.0.
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
	int32_t		van_capacity;
	int32_t		van_next_offset;
} vdev_anyraid_node_t;

typedef struct vdev_anyraid {
	vdev_anyraid_parity_type_t vd_parity_type;
	/*
	 * The parity of the mismatched vdev; 0 for raid0, or the number of
	 * mirrors.
	 */
	uint_t		vd_nparity;
	uint64_t	vd_tile_size;

	/* Version of the tile map format, kept from the vdev's creation */
	uint16_t	vd_version;

	krwlock_t	vd_lock;
	avl_tree_t	vd_tile_map;
	avl_tree_t	vd_children_tree;
	int64_t	vd_checkpoint_tile;
	uint8_t		vd_last_map;
	uint64_t	vd_last_map_txg;
	vdev_anyraid_node_t **vd_children;
} vdev_anyraid_t;

typedef struct anyraid_tile_node {
	list_node_t	atn_node;
	uint8_t		atn_disk;
	uint32_t	atn_offset;
} anyraid_tile_node_t;

typedef struct anyraid_tile {
	avl_node_t	at_node;
	uint32_t	at_tile_id;
	uint64_t	at_synced;
	list_t		at_list;
} anyraid_tile_t;

/*
 * The ondisk structure of the anyraid tile map is VDEV_ANYRAID_MAP_COPIES
 * copies of the following layout. We store the tile map on every disk, and
 * each TXG we update the copy after the one written in the previous TXG. The
 * tile map is stored immediately after the boot loader region after the second
 * label.
 *
 * First, we start with a MAX(8KiB, 1 << ashift) tile that stores a packed
 * nvlist containing the header. The header contains a version number, a disk
 * ID, a TXG, the tile size (in bytes), the stripe width/parity of the
 * tiles, the length of the mapping (in bytes), the pool guid, and the
 * checksum of the mapping. This 4KiB tile has an embedded checksum so that
 * uses the normal ZIO_CHECKSUM_LABEL algorithm.
 *
 * Then, there is a tile of size VDEV_ANYRAID_MAP_SIZE. This stores the actual
 * mapping. It is a series of 32-bit entries, each starting with its type in
 * the low bits. Right now, there are two entry types:
 *
 * 0: Skip entries represent a gap in logical tile IDs. From the current
 * tile ID, add the value stored in the remaining bits of the skip entry.
 *
 * 1: Location entries represent a mapped tile. Each one represents a single
 * physical tile backing the current logical tile. There can be multiple
 * physical tiles for one logical tile; that number is the stripe width/
 * parity from the header. These entries contain an 8 bit disk ID and, in
 * the remaining bits, the offset of the tile on that disk.
 *
 * The header's version decides how many bits the type takes:
 *
 * version  type  skip count  disk  offset  most tiles on a disk
 * 0        8     24          8     16      2^16
 * 1        4     28          8     20      2^20
 *
 * The header's "sizes" array holds each disk's capacity in tiles, minus one:
 * a uint16 array in version 0, a uint32 array in version 1. A new vdev is
 * created with VDEV_ANYRAID_VERSION, and keeps writing the version it was
 * created with.
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
#define	VDEV_ANYRAID_VERSION		1
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

static inline uint_t
ame_type_bits(uint16_t version)
{
	return (version == 0 ? 8 : 4);
}

/*
 * ==========================================================================
 * Skip entry definitions and functions
 * ==========================================================================
 */
typedef uint32_t anyraid_map_skip_entry_t;

static inline void
amse_set_type(anyraid_map_skip_entry_t *amse, uint16_t version)
{
	BF32_SET(*amse, 0, ame_type_bits(version), AMET_SKIP);
}

static inline void
amse_set_skip_count(anyraid_map_skip_entry_t *amse, uint16_t version,
    uint32_t skip_count)
{
	uint_t low = ame_type_bits(version);

	BF32_SET(*amse, low, 32 - low, skip_count);
}

static inline uint32_t
amse_get_skip_count(anyraid_map_skip_entry_t *amse, uint16_t version)
{
	uint_t low = ame_type_bits(version);

	return (BF32_GET(*amse, low, 32 - low));
}

/*
 * ==========================================================================
 * Location entry definitions and functions
 * ==========================================================================
 */
typedef uint32_t anyraid_map_loc_entry_t;

#define	AMLE_DISK_BITS		8

static inline uint_t
amle_offset_bits(uint16_t version)
{
	return (32 - ame_type_bits(version) - AMLE_DISK_BITS);
}

static inline void
amle_set_type(anyraid_map_loc_entry_t *amle, uint16_t version)
{
	BF32_SET(*amle, 0, ame_type_bits(version), AMET_LOC);
}

static inline void
amle_set_disk(anyraid_map_loc_entry_t *amle, uint16_t version, uint8_t disk)
{
	uint_t low = ame_type_bits(version);

	BF32_SET(*amle, low, AMLE_DISK_BITS, disk);
}

static inline uint32_t
amle_get_disk(anyraid_map_loc_entry_t *amle, uint16_t version)
{
	return (BF32_GET(*amle, ame_type_bits(version), AMLE_DISK_BITS));
}

static inline void
amle_set_offset(anyraid_map_loc_entry_t *amle, uint16_t version,
    uint32_t offset)
{
	uint_t low = ame_type_bits(version) + AMLE_DISK_BITS;

	BF32_SET(*amle, low, amle_offset_bits(version), offset);
}

static inline uint32_t
amle_get_offset(anyraid_map_loc_entry_t *amle, uint16_t version)
{
	uint_t low = ame_type_bits(version) + AMLE_DISK_BITS;

	return (BF32_GET(*amle, low, amle_offset_bits(version)));
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
ame_get_type(anyraid_map_entry_t *ame, uint16_t version)
{
	return (BF32_GET(ame->ame_u.ame_amle, 0, ame_type_bits(version)));
}

#define	VDEV_ANYRAID_MAX_DISKS	(1 << AMLE_DISK_BITS)
/* The most tiles a disk can hold: 2^20, or 2^16 in version 0. */
#define	VDEV_ANYRAID_MAX_TPD(version)	(1U << amle_offset_bits(version))
/*
 * The map has room for 2^24 entries of 4 bytes, 64 MiB. A vdev of T logical
 * tiles, L of them mapped, writes width * L location entries and at most
 * T - L skip entries, one per run of unmapped tiles: at most width * T. So
 * its size is capped at VDEV_ANYRAID_MAX_TILES / width tiles, which 2^8
 * disks of 2^16 tiles, all that version 0 allows, never reach.
 */
#define	VDEV_ANYRAID_MAX_TILES	(1 << 24)
#define	VDEV_ANYRAID_MAP_SIZE	(sizeof (anyraid_map_loc_entry_t) * \
	VDEV_ANYRAID_MAX_TILES)
#define	VDEV_ANYRAID_HEADER_BUFFER	(1 << 20)
#define	VDEV_ANYRAID_SINGLE_MAP_SIZE	\
	((VDEV_ANYRAID_HEADER_BUFFER + VDEV_ANYRAID_MAP_SIZE))
#define	VDEV_ANYRAID_MAP_COPIES		4
#define	VDEV_ANYRAID_START_COPIES	(VDEV_ANYRAID_MAP_COPIES / 2)
#define	VDEV_ANYRAID_TOTAL_MAP_SIZE	(VDEV_ANYRAID_MAP_COPIES * \
	VDEV_ANYRAID_SINGLE_MAP_SIZE)
#define	VDEV_ANYRAID_START_OFFSET	(VDEV_ANYRAID_START_COPIES * \
	VDEV_ANYRAID_SINGLE_MAP_SIZE)

_Static_assert(VDEV_ANYRAID_TOTAL_MAP_SIZE % SPA_MINBLOCKSIZE == 0, "");
_Static_assert(VDEV_ANYRAID_MAP_SIZE % SPA_MAXBLOCKSIZE == 0, "");

/*
 * ==========================================================================
 * Externally-accessed function definitions
 * ==========================================================================
 */
extern void vdev_anyraid_write_map_sync(vdev_t *vd, zio_t *pio, uint64_t txg,
    uint64_t *good_writes, int flags, vdev_config_sync_status_t status);

extern void vdev_anyraid_expand(vdev_t *tvd, vdev_t *newvd);
extern boolean_t vdev_anyraid_mapped(vdev_t *vd, uint64_t offset, uint64_t txg);
extern uint64_t vdev_anyraid_count(vdev_t *vd);
extern void vdev_anyraid_map(vdev_t *vd, uint64_t tile_id);

/*
 * These functions are exposed for ZDB.
 */

typedef struct anyraid_header {
	avl_node_t ah_node;
	abd_t *ah_abd;
	char *ah_buf;
	nvlist_t *ah_nvl;
	uint64_t ah_txg;
	vdev_t *ah_vd;
	int ah_mapping;
	uint64_t ah_target;
} anyraid_header_t;

extern int vdev_anyraid_open_mappings(vdev_t *cvd, avl_tree_t *tree);
extern int vdev_anyraid_header_compar(const void *a, const void *b);
extern int vdev_anyraid_open_header(vdev_t *cvd, int header,
    anyraid_header_t *out_header);
extern int vdev_anyraid_read_map(vdev_t *cvd, int mapping,
    anyraid_header_t *header, uint32_t map_length, abd_t **map_abds,
    int flags);

#ifdef	__cplusplus
}
#endif

#endif /* _SYS_VDEV_ANYRAID_H */
