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

/*
 * Anyraid vdevs are a way to get the benefits of mirror (and, in the future,
 * raidz) vdevs while using disks with mismatched sizes. The primary goal of
 * this feature is maximizing the available space of the provided devices.
 * Performance is secondary to that goal; nice to have, but not required. This
 * feature is also designed to work on modern hard drives: while the feature
 * will work on drives smaller than 1TB, the default tuning values are
 * optimized for drives of at least that size.
 *
 * Anyraid works by splitting the vdev into "tiles". Each tile is the same
 * size; by default, 1/64th of the size of the smallest disk in the vdev, or
 * 16GiB, whichever is larger. A tile represents an area of
 * logical-to-physical mapping: consecutive tiles in logical space (the first
 * physical tiles on disk, or to the same disk at all. When parity is not
 * When parity is not considered, this provides some small benefits (device
 * removal within the vdev is not yet implemented, but is very feasible, as is
 * rebalancing data onto new disks). However, if parity is considered, the
 * mirror parity P, each tile is allocated onto P separate disks, providing
 * separate disks, providing the reliability and performance characteristics of
 * because each tile can be allocated separately, smaller drives can work
 * smaller drives can work together to mirror larger ones dynamically and
 * seamlessly.
 *
 * The mapping for these tiles is stored in a special area at the start of
 * each device. Each disk has 8 fully copies of the tile map, which rotate
 * per txg in a similar manner to uberblocks. The tile map itself is 64MiB,
 * plus a small header (~8KiB) before it.
 *
 * The exact space that is allocatable in an anyraid vdev is not easy to
 * calculate in the general case. It's a variant of the bin-packing problem, so
 * an optimal solution is complex. However, this case seems to be a sub-problem
 * where greedy algorithms give optimal solutions, so that is what we do here.
 * Each tile is allocated from the P disks that have the most available
 * capacity. This does mean that calculating the size of a disk requires
 * running the allocation algorithm until completion, but for the relatively
 * small number of tiles we are working with, an O(n * log n) runtime is
 * acceptable.
 *
 * Currently, there is a limit of 2^24 tiles in an anyraid vdev: 2^8 disks,
 * and 2^16 tiles per disk. This means that by default, the largest device
 * that can be fully utilized by an anyraid vdev is 1024 times the size of the
 * smallest device that was present during device creation. This is not a
 * fundamental limit, and could be expanded in the future. However, this does
 * affect the size of the tile map. Currently, the tile map can always
 * store all tiles without running out of space; 2^24 4-byte entries is 2^26
 * bytes = 64MiB. Expanding the maximum number of tiles per disk or disks per
 * vdev would necessarily involve either expanding the tile map or adding
 * handling for the tile map running out of space.
 *
 * When it comes to performance, there is a tradeoff. While the per-disk I/O
 * rates are equivalent to using mirrors (because only a small amount of extra
 * logic is used on top of the mirror code), the overall vdev throughput may
 * not be. This is because the actively used tiles may be allocated to the
 * same devices, leaving other devices idle for writes. This is especially true
 * as the variation in drive sizes increases. To some extent, this problem is
 * fundamental: writes fill up disks. If we want to fill all the disks, smaller
 * disks will not be able to satisfy as many writes. Rewrite- and read-heavy
 * workloads will encounter this problem to a lesser extent. The performance
 * downsides can be mitigated with smaller tile sizes, larger metaslabs,
 * and more active metaslab allocators.
 *
 * Checkpoints are currently supported by storing the maximum allocated tile
 * at the time of the checkpoint, and then discarding all tiles after that
 * when a checkpoint is rolled back. Because device addition is forbidden while
 * a checkpoint is outstanding, no more complex logic is required.
 *
 * Currently, anyraid vdevs only work with mirror-type parity. However, plans
 * for future work include:
 *   Raidz-type parity
 *   Anyraid vdev shrinking via device removal
 *   Rebalancing after device addition
 *
 * Possible future work also includes:
 *   Enabling rebalancing with an outstanding checkpoint
 *   Trim and initialize beyond the end of the allocated tiles
 *   Store device asizes so we can make better allocation decisions while a
 *     device is faulted
 */

#include <sys/zfs_context.h>
#include <sys/spa.h>
#include <sys/vdev_impl.h>
#include <sys/vdev_anyraid.h>
#include <sys/vdev_mirror.h>

/*
 * The smallest allowable tile size. Shrinking this is mostly useful for
 * testing. Increasing it may be useful if you plan to add much larger disks to
 * an array in the future, and want to be sure their full capacity will be
 * usable.
 */
uint64_t zfs_anyraid_min_tile_size = (16ULL << 30);
/*
 * This controls how many tiles we have per disk (based on the smallest disk
 * present at creation time)
 */
int anyraid_disk_shift = 6;

static inline int
anyraid_tile_compare(const void *p1, const void *p2)
{
	const anyraid_tile_t *r1 = p1, *r2 = p2;

	return (TREE_CMP(r1->at_tile_id, r2->at_tile_id));
}

static inline int
anyraid_child_compare(const void *p1, const void *p2)
{
	const vdev_anyraid_node_t *van1 = p1, *van2 = p2;

	int cmp = TREE_CMP(van2->van_capacity - van2->van_next_offset,
	    van1->van_capacity - van1->van_next_offset);
	if (cmp != 0)
		return (cmp);

	return (TREE_CMP(van1->van_id, van2->van_id));
}

/*
 * Initialize private VDEV specific fields from the nvlist.
 */
static int
vdev_anyraid_init(spa_t *spa, nvlist_t *nv, void **tsd)
{
	(void) spa;
	uint_t children;
	nvlist_t **child;
	int error = nvlist_lookup_nvlist_array(nv,
	    ZPOOL_CONFIG_CHILDREN, &child, &children);
	if (error != 0 || children > UINT8_MAX)
		return (SET_ERROR(EINVAL));

	uint64_t nparity;
	if (nvlist_lookup_uint64(nv, ZPOOL_CONFIG_NPARITY, &nparity) != 0)
		return (SET_ERROR(EINVAL));

	vdev_anyraid_parity_type_t parity_type = VAP_TYPES;
	if (nvlist_lookup_uint8(nv, ZPOOL_CONFIG_ANYRAID_PARITY_TYPE,
	    (uint8_t *)&parity_type) != 0)
		return (SET_ERROR(EINVAL));
	if (parity_type != VAP_MIRROR)
		return (SET_ERROR(ENOTSUP));

	vdev_anyraid_t *var = kmem_zalloc(sizeof (*var), KM_SLEEP);
	var->vd_parity_type = parity_type;
	var->vd_nparity = nparity;
	rw_init(&var->vd_lock, NULL, RW_DEFAULT, NULL);
	avl_create(&var->vd_tile_map, anyraid_tile_compare,
	    sizeof (anyraid_tile_t), offsetof(anyraid_tile_t, at_node));
	avl_create(&var->vd_children_tree, anyraid_child_compare,
	    sizeof (vdev_anyraid_node_t),
	    offsetof(vdev_anyraid_node_t, van_node));

	var->vd_children = kmem_zalloc(sizeof (*var->vd_children) * children,
	    KM_SLEEP);
	for (int c = 0; c < children; c++) {
		vdev_anyraid_node_t *van = kmem_zalloc(sizeof (*van), KM_SLEEP);
		van->van_id = c;
		avl_add(&var->vd_children_tree, van);
		var->vd_children[c] = van;
	}

	*tsd = var;
	return (0);
}

static void
vdev_anyraid_fini(vdev_t *vd)
{
	vdev_anyraid_t *var = vd->vdev_tsd;
	avl_destroy(&var->vd_tile_map);


	vdev_anyraid_node_t *node;
	
	void *cookie = NULL;
	while ((node = avl_destroy_nodes(&var->vd_children_tree, &cookie))) {
		kmem_free(node, sizeof (*node));
	}
	avl_destroy(&var->vd_children_tree);

	rw_destroy(&var->vd_lock);
	kmem_free(var->vd_children,
	    sizeof (*var->vd_children) * vd->vdev_children);
	kmem_free(var, sizeof (*var));
}

/*
 * Add ANYRAID specific fields to the config nvlist.
 */
static void
vdev_anyraid_config_generate(vdev_t *vd, nvlist_t *nv)
{
	ASSERT3P(vd->vdev_ops, ==, &vdev_anyraid_ops);
	vdev_anyraid_t *var = vd->vdev_tsd;

	fnvlist_add_uint64(nv, ZPOOL_CONFIG_NPARITY, var->vd_nparity);
	fnvlist_add_uint8(nv, ZPOOL_CONFIG_ANYRAID_PARITY_TYPE,
	    (uint8_t)var->vd_parity_type);
}

/*
 * Import/open related functions.
 */

/*
 * Add an entry to the tile map for the provided tile.
 */
static void
create_tile_entry(vdev_anyraid_t *var, anyraid_map_loc_entry_t *amle,
    uint8_t *pat_cnt, anyraid_tile_t **out_ar, uint32_t *cur_tile)
{
	uint8_t disk = amle->amle_disk;
	uint16_t offset = amle->amle_offset;
	anyraid_tile_t *ar = *out_ar;

	if (*pat_cnt == 0) {
		ar = kmem_alloc(sizeof (*ar), KM_SLEEP);
		ar->at_tile_id = *cur_tile;
		avl_add(&var->vd_tile_map, ar);
		list_create(&ar->at_list,
		    sizeof (anyraid_tile_node_t),
		    offsetof(anyraid_tile_node_t, atn_node));

		(*cur_tile)++;
	}

	anyraid_tile_node_t *arn = kmem_alloc(sizeof (*arn), KM_SLEEP);
	arn->atn_disk = disk;
	arn->atn_offset = offset;
	list_insert_tail(&ar->at_list, arn);
	*pat_cnt = (*pat_cnt + 1) % (var->vd_nparity + 1);

	vdev_anyraid_node_t *van = var->vd_children[disk];
	avl_remove(&var->vd_children_tree, van);
	van->van_next_offset = MAX(van->van_next_offset, offset + 1);
	avl_add(&var->vd_children_tree, van);
	*out_ar = ar;
}

/*
 * This function is non-static for ZDB, and shouldn't be used for anything else.
 * Utility function that issues the read for the header and parses out the
 * nvlist.
 */
int
vdev_anyraid_open_header(vdev_t *cvd, int header, anyraid_header_t *out_header)
{
	spa_t *spa = cvd->vdev_spa;
	uint64_t ashift = cvd->vdev_ashift;
	uint64_t header_offset = VDEV_LABEL_START_SIZE +
	    header * VDEV_ANYRAID_SINGLE_MAP_SIZE(ashift);
	uint64_t header_size = VDEV_ANYRAID_MAP_HEADER_SIZE(ashift);
	int flags = ZIO_FLAG_CONFIG_WRITER | ZIO_FLAG_CANFAIL |
	    ZIO_FLAG_SPECULATIVE;

	abd_t *header_abd = abd_alloc_linear(header_size, B_TRUE);
	zio_t *rio = zio_root(spa, NULL, NULL, flags);
	zio_nowait(zio_read_phys(rio, cvd, header_offset, header_size,
	    header_abd, ZIO_CHECKSUM_LABEL, NULL, NULL, ZIO_PRIORITY_SYNC_READ,
	    flags, B_FALSE));
	int error;
	if ((error = zio_wait(rio)) != 0) {
		zfs_dbgmsg("Error %d reading anyraid header %d on vdev %s",
		    error, header, cvd->vdev_path);
		abd_free(header_abd);
		return (error);
	}

	char *header_buf = abd_borrow_buf(header_abd, header_size);
	nvlist_t *header_nvl;
	error = nvlist_unpack(header_buf, header_size, &header_nvl,
	    KM_SLEEP);
	if (error != 0) {
		zfs_dbgmsg("Error %d unpacking anyraid header %d on vdev %s",
		    error, header, cvd->vdev_path);
		abd_return_buf(header_abd, header_buf, header_size);
		abd_free(header_abd);
		return (error);
	}
	out_header->ah_abd = header_abd;
	out_header->ah_buf = header_buf;
	out_header->ah_nvl = header_nvl;

	return (0);
}

static void
free_header(anyraid_header_t *header, uint64_t header_size) {
	fnvlist_free(header->ah_nvl);
	abd_return_buf(header->ah_abd, header->ah_buf, header_size);
	abd_free(header->ah_abd);
}

/*
 * This function is non-static for ZDB, and shouldn't be used for anything else.
 *
 * Iterate over all the copies of the map for the given child vdev and select
 * the best one.
 */
int
vdev_anyraid_pick_best_mapping(vdev_t *cvd, uint64_t *out_txg,
    anyraid_header_t *out_header, int *out_mapping)
{
	spa_t *spa = cvd->vdev_spa;
	uint64_t ashift = cvd->vdev_ashift;
	int error = 0;
	uint64_t header_size = VDEV_ANYRAID_MAP_HEADER_SIZE(ashift);

	int best_mapping = -1;
	uint64_t best_txg = 0;
	anyraid_header_t best_header = {0};
	boolean_t checkpoint_rb = spa_importing_checkpoint(spa);

	for (int i = 0; i < VDEV_ANYRAID_MAP_COPIES; i++) {
		anyraid_header_t header;
		error = vdev_anyraid_open_header(cvd, i, &header);

		if (error)
			continue;

		nvlist_t *hnvl = header.ah_nvl;
		uint16_t version;
		if ((error = nvlist_lookup_uint16(hnvl,
		    VDEV_ANYRAID_HEADER_VERSION, &version)) != 0) {
			free_header(&header, header_size);
			zfs_dbgmsg("Anyraid header %d on vdev %s: missing "
			    "version", i, cvd->vdev_path);
			continue;
		}
		if (version != 0) {
			free_header(&header, header_size);
			error = SET_ERROR(ENOTSUP);
			zfs_dbgmsg("Anyraid header %d on vdev %s: invalid "
			    "version", i, cvd->vdev_path);
			continue;
		}

		uint64_t pool_guid = 0;
		if (nvlist_lookup_uint64(hnvl, VDEV_ANYRAID_HEADER_GUID,
		    &pool_guid) != 0 || pool_guid != spa_guid(spa)) {
			free_header(&header, header_size);
			error = SET_ERROR(EINVAL);
			zfs_dbgmsg("Anyraid header %d on vdev %s: guid "
			    "mismatch: %llu %llu", i, cvd->vdev_path,
			    (u_longlong_t)pool_guid,
			    (u_longlong_t)spa_guid(spa));
			continue;
		}

		uint64_t written_txg;
		if (nvlist_lookup_uint64(hnvl, VDEV_ANYRAID_HEADER_TXG,
		    &written_txg) != 0) {
			free_header(&header, header_size);
			error = SET_ERROR(EINVAL);
			zfs_dbgmsg("Anyraid header %d on vdev %s: no txg",
			    i, cvd->vdev_path);
			continue;
		}
		/*
		 * If we're reopening, the current txg hasn't been synced out
		 * yet; look for one txg earlier.
		 */
		uint64_t min_txg = spa_current_txg(spa) -
		    (cvd->vdev_parent->vdev_reopening ? 1 : 0);
		if ((written_txg < min_txg && !checkpoint_rb) ||
		    written_txg > spa_load_max_txg(spa)) {
			free_header(&header, header_size);
			error = SET_ERROR(EINVAL);
			zfs_dbgmsg("Anyraid header %d on vdev %s: txg %llu out "
			    "of bounds (%llu, %llu)", i, cvd->vdev_path,
			    (u_longlong_t)written_txg,
			    (u_longlong_t)min_txg,
			    (u_longlong_t)spa_load_max_txg(spa));
			continue;
		}
		if (written_txg > best_txg) {
			best_txg = written_txg;
			best_mapping = i;
			if (best_header.ah_nvl)
				free_header(&best_header, header_size);

			best_header = header;
		} else {
			free_header(&header, header_size);
		}
	}

	if (best_txg != 0) {
		*out_txg = best_txg;
		*out_mapping = best_mapping;
		*out_header = best_header;
		return (0);
	}
	ASSERT(error);
	return (error);
}

static int
anyraid_open_existing(vdev_t *vd, uint64_t child, uint16_t **child_capacities)
{
	vdev_anyraid_t *var = vd->vdev_tsd;
	vdev_t *cvd = vd->vdev_child[child];
	uint64_t ashift = cvd->vdev_ashift;
	spa_t *spa = vd->vdev_spa;
	int flags = ZIO_FLAG_CONFIG_WRITER | ZIO_FLAG_CANFAIL |
	    ZIO_FLAG_SPECULATIVE;
	uint64_t header_size = VDEV_ANYRAID_MAP_HEADER_SIZE(ashift);
	boolean_t checkpoint_rb = spa_importing_checkpoint(spa);

	anyraid_header_t header;
	int mapping;
	uint64_t txg;
	int error = vdev_anyraid_pick_best_mapping(cvd, &txg, &header,
	    &mapping);
	if (error)
		return (error);

	uint8_t disk_id;
	if (nvlist_lookup_uint8(header.ah_nvl, VDEV_ANYRAID_HEADER_DISK,
	    &disk_id) != 0) {
		zfs_dbgmsg("Error opening anyraid vdev %llu: No disk ID",
		    (u_longlong_t)vd->vdev_id);
		free_header(&header, header_size);
		return (SET_ERROR(EINVAL));
	}

	uint64_t tile_size;
	if (nvlist_lookup_uint64(header.ah_nvl, VDEV_ANYRAID_HEADER_TILE_SIZE,
	    &tile_size) != 0) {
		zfs_dbgmsg("Error opening anyraid vdev %llu: No tile size",
		    (u_longlong_t)vd->vdev_id);
		free_header(&header, header_size);
		return (SET_ERROR(EINVAL));
	}

	uint32_t map_length;
	if (nvlist_lookup_uint32(header.ah_nvl, VDEV_ANYRAID_HEADER_LENGTH,
	    &map_length) != 0) {
		zfs_dbgmsg("Error opening anyraid vdev %llu: No map length",
		    (u_longlong_t)vd->vdev_id);
		free_header(&header, header_size);
		return (SET_ERROR(EINVAL));
	}

	uint16_t *caps = NULL;
	uint_t count;
	if (nvlist_lookup_uint16_array(header.ah_nvl,
	    VDEV_ANYRAID_HEADER_DISK_SIZES, &caps, &count) != 0) {
		zfs_dbgmsg("Error opening anyraid vdev %llu: No child sizes",
		    (u_longlong_t)vd->vdev_id);
		free_header(&header, header_size);
		return (SET_ERROR(EINVAL));
	}
	if (count != vd->vdev_children) {
		zfs_dbgmsg("Error opening anyraid vdev %llu: Incorrect child "
		    "count %u vs %u", (u_longlong_t)vd->vdev_id, count,
		    (uint_t)vd->vdev_children);
		free_header(&header, header_size);
		return (SET_ERROR(EINVAL));
	}

	*child_capacities = kmem_alloc(sizeof (*caps) * count, KM_SLEEP);
	memcpy(*child_capacities, caps, sizeof (*caps) * count);
	if (vd->vdev_reopening) {
		free_header(&header, header_size);
		return (0);
	}

	var->vd_checkpoint_tile = UINT32_MAX;
	(void) nvlist_lookup_uint32(header.ah_nvl,
	    VDEV_ANYRAID_HEADER_CHECKPOINT, &var->vd_checkpoint_tile);

	/*
	 * Because the tile map is 64 MiB and the maximum IO size is 16MiB,
	 * we may need to issue up to 4 reads to read in the whole thing.
	 * Similarly, when processing the mapping, we need to iterate across
	 * the 4 separate buffers.
	 */
	zio_t *rio = zio_root(spa, NULL, NULL, flags);
	abd_t *map_abds[VDEV_ANYRAID_MAP_COPIES] = {0};
	uint64_t header_offset = VDEV_LABEL_START_SIZE +
	    mapping * VDEV_ANYRAID_SINGLE_MAP_SIZE(ashift);
	uint64_t map_offset = header_offset + header_size;
	int i;
	for (i = 0; i <= (map_length / SPA_MAXBLOCKSIZE); i++) {
		zio_eck_t *cksum = (zio_eck_t *)
		    &header.ah_buf[VDEV_ANYRAID_NVL_BYTES(ashift) +
		    i * sizeof (*cksum)];
		map_abds[i] = abd_alloc_linear(SPA_MAXBLOCKSIZE, B_TRUE);
		zio_nowait(zio_read_phys(rio, cvd, map_offset +
		    i * SPA_MAXBLOCKSIZE, SPA_MAXBLOCKSIZE, map_abds[i],
		    ZIO_CHECKSUM_ANYRAID_MAP, NULL, cksum,
		    ZIO_PRIORITY_SYNC_READ, flags, B_FALSE));
	}
	i--;

	if ((error = zio_wait(rio))) {
		for (; i >= 0; i--)
			abd_free(map_abds[i]);
		free_header(&header, header_size);
		zfs_dbgmsg("Error opening anyraid vdev %llu: map read error %d",
		    (u_longlong_t)vd->vdev_id, error);
		return (error);
	}
	free_header(&header, header_size);

	uint32_t map = -1, cur_tile = 0;
	/*
	 * For now, all entries are the size of a uint32_t. If that
	 * ever changes, the logic here needs to be altered to work for
	 * adaptive sizes, including entries split across 16MiB boundaries.
	 */
	uint32_t size = sizeof (anyraid_map_loc_entry_t);
	uint8_t *map_buf = NULL;
	uint8_t pat_cnt = 0;
	anyraid_tile_t *ar = NULL;
	for (uint32_t off = 0; off < map_length; off += size) {
		if (checkpoint_rb && cur_tile > var->vd_checkpoint_tile &&
		    pat_cnt == 0)
			break;

		int next_map = off / SPA_MAXBLOCKSIZE;
		if (map != next_map) {
			// switch maps
			if (map != -1) {
				abd_return_buf(map_abds[map], map_buf,
				    SPA_MAXBLOCKSIZE);
			}
			map_buf = abd_borrow_buf(map_abds[next_map],
			    SPA_MAXBLOCKSIZE);
			map = next_map;
		}
		anyraid_map_entry_t *entry =
		    (anyraid_map_entry_t *)(map_buf + (off % SPA_MAXBLOCKSIZE));
		uint8_t type = entry->ame_u.ame_amle.amle_type;
		switch (type) {
			case AMET_SKIP: {
				anyraid_map_skip_entry_t *amse =
				    &entry->ame_u.ame_amse;
				ASSERT0(pat_cnt);
				cur_tile += amse_get_tile_id(amse);
				break;
			}
			case AMET_LOC: {
				anyraid_map_loc_entry_t *amle =
				    &entry->ame_u.ame_amle;
				create_tile_entry(var, amle, &pat_cnt, &ar,
				    &cur_tile);
				break;
			}
			default:
				PANIC("Invalid entry type %d", type);
		}
	}
	if (map_buf)
		abd_return_buf(map_abds[map], map_buf, SPA_MAXBLOCKSIZE);

	var->vd_tile_size = tile_size;

	for (; i >= 0; i--)
		abd_free(map_abds[i]);

	/*
	 * Now that we have the tile map read in, we have to reopen the
	 * children to properly set and handle the min_asize
	 */
	for (; i < vd->vdev_children; i++) {
		vdev_t *cvd = vd->vdev_child[i];
		vdev_reopen(cvd);
	}

	int lasterror = 0;
	int numerrors = 0;
	for (int c = 0; c < vd->vdev_children; c++) {
		vdev_t *cvd = vd->vdev_child[c];

		if (cvd->vdev_open_error != 0) {
			lasterror = cvd->vdev_open_error;
			numerrors++;
			continue;
		}
	}

	if (numerrors > var->vd_nparity) {
		vd->vdev_stat.vs_aux = VDEV_AUX_NO_REPLICAS;
		return (lasterror);
	}

	return (0);
}

/*
 * When creating a new anyraid vdev, this function calculates the tile size
 * to use. We take (by default) 1/64th of the size of the smallest disk or 16
 * GiB, whichever is larger.
 */
static int
anyraid_calculate_size(vdev_t *vd)
{
	vdev_anyraid_t *var = vd->vdev_tsd;

	uint64_t smallest_disk_size = UINT64_MAX;
	for (int c = 0; c < vd->vdev_children; c++) {
		vdev_t *cvd = vd->vdev_child[c];
		smallest_disk_size = MIN(smallest_disk_size, cvd->vdev_asize);
	}

	uint64_t disk_shift = anyraid_disk_shift;
	uint64_t min_size = zfs_anyraid_min_tile_size;
	if (smallest_disk_size < 1 << disk_shift ||
	    smallest_disk_size < min_size) {
		return (SET_ERROR(ENOSPC));
	}


	ASSERT3U(smallest_disk_size, !=, UINT64_MAX);
	uint64_t tile_size = smallest_disk_size >> disk_shift;
	tile_size = MAX(tile_size, min_size);
	var->vd_tile_size = 1ULL << (highbit64(tile_size - 1));

	/*
	 * Later, we're going to cap the metaslab size at the tile
	 * size, so we need a tile to hold at least enough to store a
	 * max-size block, or we'll assert in that code.
	 */
	if (var->vd_tile_size <= SPA_MAXBLOCKSIZE)
		return (SET_ERROR(ENOSPC));
	return (0);
}

struct tile_count {
	avl_node_t node;
	int disk;
	int remaining;
};

static int
rc_compar(const void *a, const void *b)
{
	const struct tile_count *ra = a;
	const struct tile_count *rb = b;

	int cmp = TREE_CMP(rb->remaining, ra->remaining);
	if (cmp != 0)
		return (cmp);
	return (TREE_CMP(rb->disk, ra->disk));
}

/*
 * I think the only way to calculate the asize for anyraid devices is to
 * actually run the allocation algorithm and see what we end up with. It's a
 * variant of the bin-packing problem, which is NP-hard. Thankfully
 * a first-fit descending algorithm seems to give optimal results for this
 * variant.
 */
static uint64_t
calculate_asize(vdev_t *vd, uint64_t *num_tiles)
{
	vdev_anyraid_t *var = vd->vdev_tsd;

	if (var->vd_nparity == 0) {
		uint64_t count = 0;
		for (int c = 0; c < vd->vdev_children; c++) {
			count += num_tiles[c];
		}
		return (count * var->vd_tile_size);
	}

	/*
	 * Sort the disks by the number of additional tiles they can store.
	 */
	avl_tree_t t;
	avl_create(&t, rc_compar, sizeof (struct tile_count),
	    offsetof(struct tile_count, node));
	for (int c = 0; c < vd->vdev_children; c++) {
		if (num_tiles[c] == 0) {
			ASSERT(vd->vdev_child[c]->vdev_open_error);
			continue;
		}
		struct tile_count *rc = kmem_alloc(sizeof (*rc), KM_SLEEP);
		rc->disk = c;
		rc->remaining = num_tiles[c] -
		    var->vd_children[c]->van_next_offset;
		avl_add(&t, rc);
	}

	uint32_t map_width = var->vd_nparity + 1;
	uint64_t count = avl_numnodes(&var->vd_tile_map);
	struct tile_count **cur = kmem_alloc(sizeof (*cur) * map_width,
	    KM_SLEEP);
	for (;;) {
		/* Grab the nparity + 1 children with the most free capacity */
		for (int c = 0; c < map_width; c++) {
			struct tile_count *rc = avl_first(&t);
			ASSERT(rc);
			cur[c] = rc;
			avl_remove(&t, rc);
		}
		struct tile_count *rc = cur[map_width - 1];
		struct tile_count *next = avl_first(&t);
		uint64_t next_rem = next == NULL ? 0 : next->remaining;
		ASSERT3U(next_rem, <=, rc->remaining);
		/* If one of the top N + 1 has no capacity left, we're done */
		if (rc->remaining == 0)
			break;

		/*
		 * This is a performance optimization; if the child with the
		 * lowest free capacity of the ones we've selected has N more
		 * capacity than the next child, the next N iterations would
		 * all select the same children. So to save time, we add N
		 * tiles right now and reduce our iteration count.
		 */
		uint64_t this_iter = MAX(1, rc->remaining - next_rem);
		count += this_iter;

		/* Re-add the selected children with their reduced capacity */
		for (int c = 0; c < map_width; c++) {
			ASSERT3U(cur[c]->remaining, >=, this_iter);
			cur[c]->remaining -= this_iter;
			avl_add(&t, cur[c]);
		}
	}
	for (int c = 0; c < map_width; c++)
		kmem_free(cur[c], sizeof (*cur));
	kmem_free(cur, sizeof (*cur) * map_width);
	void *cookie = NULL;
	struct tile_count *node;

	while ((node = avl_destroy_nodes(&t, &cookie)) != NULL)
		kmem_free(node, sizeof (*node));
	avl_destroy(&t);
	return (count * var->vd_tile_size);
}

static int
vdev_anyraid_open(vdev_t *vd, uint64_t *asize, uint64_t *max_asize,
    uint64_t *logical_ashift, uint64_t *physical_ashift)
{
	vdev_anyraid_t *var = vd->vdev_tsd;
	int lasterror = 0;
	int numerrors = 0;

	vdev_open_children(vd);

	for (int c = 0; c < vd->vdev_children; c++) {
		vdev_t *cvd = vd->vdev_child[c];

		if (cvd->vdev_open_error != 0) {
			lasterror = cvd->vdev_open_error;
			numerrors++;
			continue;
		}
	}

	/*
	 * If we have more faulted disks than parity, we can't open the device.
	 */
	if (numerrors > var->vd_nparity) {
		vd->vdev_stat.vs_aux = VDEV_AUX_NO_REPLICAS;
		return (lasterror);
	}

	uint16_t *child_capacities = NULL;
	if (vd->vdev_reopening) {
		child_capacities = kmem_alloc(sizeof (*child_capacities) *
		    vd->vdev_children, KM_SLEEP);
		for (uint64_t c = 0; c < vd->vdev_children; c++) {
			child_capacities[c] = var->vd_children[c]->van_capacity;
		}
	} else if (spa_load_state(vd->vdev_spa) != SPA_LOAD_CREATE &&
	    spa_load_state(vd->vdev_spa) != SPA_LOAD_ERROR && 
	    spa_load_state(vd->vdev_spa) != SPA_LOAD_NONE) {
		for (uint64_t c = 0; c < vd->vdev_children; c++) {
			vdev_t *cvd = vd->vdev_child[c];
			if (cvd->vdev_open_error != 0)
				continue;
			if ((lasterror = anyraid_open_existing(vd, c,
			    &child_capacities)) == 0)
				break;
		}
		if (lasterror)
			return (lasterror);
	} else if ((lasterror = anyraid_calculate_size(vd))) {
		return (lasterror);
	}

	/*
	 * Calculate the number of tiles each child could fit, then use that
	 * to calculate the asize and min_asize.
	 */
	uint64_t *num_tiles = kmem_zalloc(vd->vdev_children *
	    sizeof (*num_tiles), KM_SLEEP);
	for (int c = 0; c < vd->vdev_children; c++) {
		vdev_t *cvd = vd->vdev_child[c];

		uint64_t casize;
		if (cvd->vdev_open_error == 0) {
			vdev_set_min_asize(cvd);
			casize = cvd->vdev_asize -
			    VDEV_ANYRAID_TOTAL_MAP_SIZE(cvd->vdev_ashift);
		} else {
			ASSERT(child_capacities);
			casize = child_capacities[c] * var->vd_tile_size;
		}

		num_tiles[c] = casize / var->vd_tile_size;
		avl_remove(&var->vd_children_tree, var->vd_children[c]);
		var->vd_children[c]->van_capacity = num_tiles[c];
		avl_add(&var->vd_children_tree, var->vd_children[c]);
	}
	*asize = calculate_asize(vd, num_tiles);

	for (int c = 0; c < vd->vdev_children; c++) {
		vdev_t *cvd = vd->vdev_child[c];

		uint64_t cmasize;
		if (cvd->vdev_open_error == 0) {
			cmasize = cvd->vdev_max_asize -
			    VDEV_ANYRAID_TOTAL_MAP_SIZE(cvd->vdev_ashift);
		} else {
			cmasize = child_capacities[c] * var->vd_tile_size;
		}

		num_tiles[c] = cmasize / var->vd_tile_size;
	}
	*max_asize = calculate_asize(vd, num_tiles);

	if (child_capacities) {
		kmem_free(child_capacities, sizeof (*child_capacities) *
		    vd->vdev_children);
	}
	for (int c = 0; c < vd->vdev_children; c++) {
		vdev_t *cvd = vd->vdev_child[c];

		if (cvd->vdev_open_error != 0)
			continue;

		*logical_ashift = MAX(*logical_ashift, cvd->vdev_ashift);
		*physical_ashift = vdev_best_ashift(*logical_ashift,
		    *physical_ashift, cvd->vdev_physical_ashift);
	}
	return (0);
}

/*
 * We cap the metaslab size at the tile size. This prevents us from having to
 * split IOs across multiple tiles, which would be complex extra logic for
 * little gain.
 */
static void
vdev_anyraid_metaslab_size(vdev_t *vd, uint64_t *shiftp)
{
	vdev_anyraid_t *var = vd->vdev_tsd;
	*shiftp = MIN(*shiftp, highbit64(var->vd_tile_size) - 1);
}

static void
vdev_anyraid_close(vdev_t *vd)
{
	vdev_anyraid_t *var = vd->vdev_tsd;
	for (int c = 0; c < vd->vdev_children; c++) {
		if (vd->vdev_child[c] != NULL)
			vdev_close(vd->vdev_child[c]);
	}
	if (vd->vdev_reopening)
		return;
	anyraid_tile_t *tile = NULL;
	void *cookie = NULL;
	while ((tile = avl_destroy_nodes(&var->vd_tile_map, &cookie))) {
		if (var->vd_nparity != 0) {
			anyraid_tile_node_t *atn = NULL;
			while ((atn = list_remove_head(&tile->at_list))) {
				kmem_free(atn, sizeof (*atn));
			}
			list_destroy(&tile->at_list);
		}
		kmem_free(tile, sizeof (*tile));
	}
}

/*
 * I/O related functions.
 */

/*
 * Configure the mirror_map and then hand the write off to the normal mirror
 * logic.
 */
static void
vdev_anyraid_mirror_start(zio_t *zio, anyraid_tile_t *tile)
{
	vdev_t *vd = zio->io_vd;
	vdev_anyraid_t *var = vd->vdev_tsd;
	mirror_map_t *mm = vdev_mirror_map_alloc(var->vd_nparity + 1, B_FALSE,
	    B_FALSE);
	uint64_t rsize = var->vd_tile_size;

	anyraid_tile_node_t *arn = list_head(&tile->at_list);
	for (int c = 0; c < mm->mm_children; c++) {
		ASSERT(arn);
		mirror_child_t *mc = &mm->mm_child[c];
		mc->mc_vd = vd->vdev_child[arn->atn_disk];
		mc->mc_offset = VDEV_ANYRAID_TOTAL_MAP_SIZE(vd->vdev_ashift) +
		    arn->atn_offset * rsize + zio->io_offset % rsize;
		ASSERT3U(mc->mc_offset, <, mc->mc_vd->vdev_psize -
		    VDEV_LABEL_END_SIZE);
		mm->mm_rebuilding = mc->mc_rebuilding = B_FALSE;
		arn = list_next(&tile->at_list, arn);
	}
	ASSERT(arn == NULL);

	zio->io_vsd = mm;
	zio->io_vsd_ops = &vdev_mirror_vsd_ops;

	vdev_mirror_io_start_impl(zio, mm);
}

typedef struct anyraid_map {
	abd_t *am_abd;
} anyraid_map_t;

static void
vdev_anyraid_map_free_vsd(zio_t *zio)
{
	anyraid_map_t *mm = zio->io_vsd;
	abd_free(mm->am_abd);
	mm->am_abd = NULL;
	kmem_free(mm, sizeof (*mm));
}

const zio_vsd_ops_t vdev_anyraid_vsd_ops = {
	.vsd_free = vdev_anyraid_map_free_vsd,
};

static void
vdev_anyraid_child_done(zio_t *zio)
{
	zio_t *pio = zio->io_private;
	pio->io_error = zio_worst_error(pio->io_error, zio->io_error);
}

static void
vdev_anyraid_io_start(zio_t *zio)
{
	vdev_t *vd = zio->io_vd;
	vdev_anyraid_t *var = vd->vdev_tsd;
	uint64_t rsize = var->vd_tile_size;

	uint64_t start_tile_id = zio->io_offset / rsize;
	anyraid_tile_t search;
	search.at_tile_id = start_tile_id;
	avl_index_t where;
	rw_enter(&var->vd_lock, RW_READER);
	anyraid_tile_t *tile = avl_find(&var->vd_tile_map, &search,
	    &where);

	/*
	 * If we're doing an I/O somewhere that hasn't been allocated yet, we
	 * may need to allocate a new tile. Upgrade to a write lock so we can
	 * safely modify the data structure, and then check if someone else
	 * beat us to it.
	 */
	if (tile == NULL) {
		rw_exit(&var->vd_lock);
		rw_enter(&var->vd_lock, RW_WRITER);
		tile = avl_find(&var->vd_tile_map, &search, &where);
	}
	if (tile == NULL) {
		ASSERT3U(zio->io_type, ==, ZIO_TYPE_WRITE);
		zfs_dbgmsg("Allocating tile %llu for zio %px",
		    (u_longlong_t)start_tile_id, zio);
		tile = kmem_alloc(sizeof (*tile), KM_SLEEP);
		tile->at_tile_id = start_tile_id;
		list_create(&tile->at_list, sizeof (anyraid_tile_node_t),
		    offsetof(anyraid_tile_node_t, atn_node));

		uint_t width = var->vd_nparity + 1;
		vdev_anyraid_node_t **vans = kmem_alloc(sizeof (*vans) * width,
		    KM_SLEEP);
		for (int i = 0; i < width; i++) {
			vans[i] = avl_first(&var->vd_children_tree);
			avl_remove(&var->vd_children_tree, vans[i]);

			anyraid_tile_node_t *arn =
			    kmem_alloc(sizeof (*arn), KM_SLEEP);
			arn->atn_disk = vans[i]->van_id;
			arn->atn_offset =
			    vans[i]->van_next_offset++;
			list_insert_tail(&tile->at_list, arn);
		}
		for (int i = 0; i < width; i++)
			avl_add(&var->vd_children_tree, vans[i]);

		kmem_free(vans, sizeof (*vans) * width);
		avl_insert(&var->vd_tile_map, tile, where);
	}
	rw_exit(&var->vd_lock);

	ASSERT3U(zio->io_offset % rsize + zio->io_size, <=,
	    var->vd_tile_size);

	if (var->vd_nparity > 0) {
		vdev_anyraid_mirror_start(zio, tile);
		zio_execute(zio);
		return;
	}

	anyraid_tile_node_t *arn = list_head(&tile->at_list);
	vdev_t *cvd = vd->vdev_child[arn->atn_disk];
	uint64_t child_offset = arn->atn_offset * rsize +
	    zio->io_offset % rsize;
	child_offset += VDEV_ANYRAID_TOTAL_MAP_SIZE(vd->vdev_ashift);

	anyraid_map_t *mm = kmem_alloc(sizeof (*mm), KM_SLEEP);
	mm->am_abd = abd_get_offset(zio->io_abd, 0);
	zio->io_vsd = mm;
	zio->io_vsd_ops = &vdev_anyraid_vsd_ops;

	zio_t *cio = zio_vdev_child_io(zio, NULL, cvd, child_offset,
	    mm->am_abd, zio->io_size, zio->io_type, zio->io_priority, 0,
	    vdev_anyraid_child_done, zio);
	zio_nowait(cio);

	zio_execute(zio);
}

static void
vdev_anyraid_io_done(zio_t *zio)
{
	vdev_t *vd = zio->io_vd;
	vdev_anyraid_t *var = vd->vdev_tsd;

	if (var->vd_nparity > 0)
		vdev_mirror_io_done(zio);
}

static void
vdev_anyraid_state_change(vdev_t *vd, int faulted, int degraded)
{
	vdev_anyraid_t *var = vd->vdev_tsd;
	if (faulted > var->vd_nparity) {
		vdev_set_state(vd, B_FALSE, VDEV_STATE_CANT_OPEN,
		    VDEV_AUX_NO_REPLICAS);
	} else if (degraded + faulted != 0) {
		vdev_set_state(vd, B_FALSE, VDEV_STATE_DEGRADED, VDEV_AUX_NONE);
	} else {
		vdev_set_state(vd, B_FALSE, VDEV_STATE_HEALTHY, VDEV_AUX_NONE);
	}
}

/*
 * Determine if any portion of the provided block resides on a child vdev
 * with a dirty DTL and therefore needs to be resilvered.  The function
 * assumes that at least one DTL is dirty which implies that full stripe
 * width blocks must be resilvered.
 */
static boolean_t
vdev_anyraid_need_resilver(vdev_t *vd, const dva_t *dva, size_t psize,
    uint64_t phys_birth)
{
	(void) psize;
	vdev_anyraid_t *var = vd->vdev_tsd;
	if (!vdev_dtl_contains(vd, DTL_PARTIAL, phys_birth, 1))
		return (B_FALSE);

	uint64_t start_tile_id = DVA_GET_OFFSET(dva) / var->vd_tile_size;
	anyraid_tile_t search;
	search.at_tile_id = start_tile_id;
	avl_index_t where;
	rw_enter(&var->vd_lock, RW_READER);
	anyraid_tile_t *tile = avl_find(&var->vd_tile_map, &search,
	    &where);
	rw_exit(&var->vd_lock);
	ASSERT(tile);

	for (anyraid_tile_node_t *arn = list_head(&tile->at_list);
	    arn != NULL; arn = list_next(&tile->at_list, arn)) {
		vdev_t *cvd = vd->vdev_child[arn->atn_disk];

		if (!vdev_dtl_empty(cvd, DTL_PARTIAL))
			return (B_TRUE);
	}

	return (B_FALSE);
}

/*
 * Right now, we don't translate anything beyond the end of the allocated
 * ranges for the target leaf vdev. This means that trim and initialize won't
 * affect those areas on anyraid devices. Given the target use case, this is
 * not a significant concern, but a rework of the xlate logic could enable this
 * in the future.
 */
static void
vdev_anyraid_xlate(vdev_t *cvd, const zfs_range_seg64_t *logical_rs,
    zfs_range_seg64_t *physical_rs, zfs_range_seg64_t *remain_rs)
{
	vdev_t *anyraidvd = cvd->vdev_parent;
	ASSERT3P(anyraidvd->vdev_ops, ==, &vdev_anyraid_ops);
	vdev_anyraid_t *var = anyraidvd->vdev_tsd;
	uint64_t rsize = var->vd_tile_size;

	uint64_t start_tile_id = logical_rs->rs_start / rsize;
	ASSERT3U(start_tile_id, ==, (logical_rs->rs_end - 1) / rsize);
	anyraid_tile_t search;
	search.at_tile_id = start_tile_id;
	avl_index_t where;
	rw_enter(&var->vd_lock, RW_READER);
	anyraid_tile_t *tile = avl_find(&var->vd_tile_map, &search,
	    &where);
	rw_exit(&var->vd_lock);
	// This tile doesn't exist yet
	if (tile == NULL) {
		physical_rs->rs_start = physical_rs->rs_end = 0;
		return;
	}
	anyraid_tile_node_t *arn = list_head(&tile->at_list);
	for (; arn != NULL; arn = list_next(&tile->at_list, arn))
		if (anyraidvd->vdev_child[arn->atn_disk] == cvd)
			break;
	// The tile exists, but isn't stored on this child
	if (arn == NULL) {
		physical_rs->rs_start = physical_rs->rs_end = 0;
		return;
	}

	uint64_t child_offset = arn->atn_offset * rsize +
	    logical_rs->rs_start % rsize;
	child_offset += VDEV_ANYRAID_TOTAL_MAP_SIZE(anyraidvd->vdev_ashift);
	uint64_t size = logical_rs->rs_end - logical_rs->rs_start;

	physical_rs->rs_start = child_offset;
	physical_rs->rs_end = child_offset + size;
	remain_rs->rs_start = 0;
	remain_rs->rs_end = 0;
}

static uint64_t
vdev_anyraid_nparity(vdev_t *vd)
{
	vdev_anyraid_t *var = vd->vdev_tsd;
	return (var->vd_nparity);
}

static uint64_t
vdev_anyraid_ndisks(vdev_t *vd)
{
	return (vd->vdev_children);
}

/*
 * Functions related to syncing out the tile map each TXG.
 */
static boolean_t
map_write_loc_entry(anyraid_tile_node_t *arn, void *buf, uint32_t *offset)
{
	anyraid_map_loc_entry_t *entry = (void *)((char *)buf + *offset);
	entry->amle_type = AMET_LOC;
	entry->amle_disk = arn->atn_disk;
	entry->amle_offset = arn->atn_offset;
	*offset += sizeof (*entry);
	return (*offset == SPA_MAXBLOCKSIZE);
}

static boolean_t
map_write_skip_entry(uint32_t tile, void *buf, uint32_t *offset,
    uint32_t prev_id)
{
	anyraid_map_skip_entry_t *entry = (void *)((char *)buf + *offset);
	amse_set_type(entry);
	amse_set_tile_id(entry, tile - prev_id - 1);
	*offset += sizeof (*entry);
	return (*offset == SPA_MAXBLOCKSIZE);
}

static void
anyraid_map_write_done(zio_t *zio)
{
	abd_free(zio->io_abd);
}

static void
map_write_issue(zio_t *zio, vdev_t *vd, uint64_t base_offset, uint8_t idx,
    abd_t *abd, zio_eck_t *cksum_out, int flags)
{
	zio_nowait(zio_write_phys(zio, vd, base_offset +
	    idx * VDEV_ANYRAID_MAP_SIZE +
	    VDEV_ANYRAID_MAP_HEADER_SIZE(vd->vdev_ashift), SPA_MAXBLOCKSIZE,
	    abd, ZIO_CHECKSUM_ANYRAID_MAP, anyraid_map_write_done, cksum_out,
	    ZIO_PRIORITY_SYNC_WRITE, flags, B_FALSE));
}

static void
vdev_anyraid_write_map_done(zio_t *zio)
{
	uint64_t *good_writes = zio->io_private;

	if (zio->io_error == 0 && good_writes != NULL)
		atomic_inc_64(good_writes);
}

void
vdev_anyraid_write_map_sync(vdev_t *vd, zio_t *pio, uint64_t txg,
    uint64_t *good_writes, int flags, vdev_config_sync_status_t status)
{
	vdev_t *anyraidvd = vd->vdev_parent;
	ASSERT3P(anyraidvd->vdev_ops, ==, &vdev_anyraid_ops);
	spa_t *spa = vd->vdev_spa;
	vdev_anyraid_t *var = anyraidvd->vdev_tsd;
	uint32_t header_size = VDEV_ANYRAID_MAP_HEADER_SIZE(vd->vdev_ashift);
	uint32_t full_size = VDEV_ANYRAID_SINGLE_MAP_SIZE(vd->vdev_ashift);
	uint32_t nvl_bytes = VDEV_ANYRAID_NVL_BYTES(vd->vdev_ashift);
	uint8_t update_target = txg % VDEV_ANYRAID_MAP_COPIES;
	uint64_t base_offset = VDEV_LABEL_START_SIZE +
	    update_target * full_size;

	abd_t *header_abd =
	    abd_alloc_linear(header_size, B_TRUE);
	abd_zero(header_abd, header_size);
	void *header_buf = abd_borrow_buf(header_abd, header_size);
	zio_eck_t *cksums = (zio_eck_t *)&((char *)header_buf)[nvl_bytes];

	abd_t *map_abd = abd_alloc_linear(SPA_MAXBLOCKSIZE, B_TRUE);
	uint8_t written = 0;
	void *buf = abd_borrow_buf(map_abd, SPA_MAXBLOCKSIZE);

	rw_enter(&var->vd_lock, RW_READER);
	anyraid_tile_t *cur = avl_first(&var->vd_tile_map);
	anyraid_tile_node_t *curn = cur != NULL ?
	    list_head(&cur->at_list) : NULL;
	uint32_t buf_offset = 0, prev_id = UINT32_MAX;
	zio_t *zio = zio_root(spa, NULL, NULL, flags);
	/* Write out each sub-tile in turn */
	while (cur) {
		if (status == VDEV_CONFIG_REWINDING_CHECKPOINT &&
		    cur->at_tile_id > var->vd_checkpoint_tile)
			break;

		anyraid_tile_t *next = AVL_NEXT(&var->vd_tile_map, cur);
		IMPLY(prev_id != UINT32_MAX, cur->at_tile_id >= prev_id);
		/*
		 * Determine if we need to write a skip entry before the
		 * current one.
		 */
		boolean_t skip =
		    (prev_id == UINT32_MAX && cur->at_tile_id != 0) ||
		    (prev_id != UINT32_MAX && cur->at_tile_id > prev_id + 1);
		if ((skip && map_write_skip_entry(cur->at_tile_id, buf,
		    &buf_offset, prev_id)) ||
		    (!skip && map_write_loc_entry(curn, buf, &buf_offset))) {
			// Let the final write handle it
			if (next == NULL)
				break;
			abd_return_buf_copy(map_abd, buf, SPA_MAXBLOCKSIZE);
			map_write_issue(zio, vd, base_offset, written,
			    map_abd, &cksums[written], flags);

			map_abd = abd_alloc_linear(SPA_MAXBLOCKSIZE, B_TRUE);
			written++;
			ASSERT3U(written, <,
			    VDEV_ANYRAID_MAP_SIZE / SPA_MAXBLOCKSIZE);
			buf = abd_borrow_buf(map_abd, SPA_MAXBLOCKSIZE);
			buf_offset = 0;
		}
		prev_id = cur->at_tile_id;
		/*
		 * Advance the current sub-tile; if it moves us past the end
		 * of the current list of sub-tiles, start the next tile.
		 */
		if (!skip) {
			curn = list_next(&cur->at_list, curn);
			if (curn == NULL) {
				cur = next;
				curn = cur != NULL ?
				    list_head(&cur->at_list) : NULL;
			}
		}
	}

	if (status == VDEV_CONFIG_DISCARDING_CHECKPOINT ||
	    status == VDEV_CONFIG_REWINDING_CHECKPOINT) {
		var->vd_checkpoint_tile = UINT32_MAX;
	} else if (status == VDEV_CONFIG_CREATING_CHECKPOINT) {
		anyraid_tile_t *ar = avl_last(&var->vd_tile_map);
		ASSERT(ar);
		var->vd_checkpoint_tile = ar->at_tile_id;
	}
	rw_exit(&var->vd_lock);

	abd_return_buf_copy(map_abd, buf, SPA_MAXBLOCKSIZE);
	map_write_issue(zio, vd, base_offset, written, map_abd,
	    &cksums[written], flags);

	if (zio_wait(zio))
		return;

	// Populate the header
	uint16_t *sizes = kmem_zalloc(sizeof (*sizes) *
	    anyraidvd->vdev_children, KM_SLEEP);
	uint8_t disk_id = 0;
	for (uint8_t i = 0; i < anyraidvd->vdev_children; i++) {
		if (anyraidvd->vdev_child[i] == vd)
			disk_id = i;
		sizes[i] = var->vd_children[i]->van_capacity;
	}
	ASSERT3U(disk_id, <, anyraidvd->vdev_children);
	nvlist_t *header = fnvlist_alloc();
	fnvlist_add_uint16(header, VDEV_ANYRAID_HEADER_VERSION, 0);
	fnvlist_add_uint8(header, VDEV_ANYRAID_HEADER_DISK, disk_id);
	fnvlist_add_uint64(header, VDEV_ANYRAID_HEADER_TXG, txg);
	fnvlist_add_uint64(header, VDEV_ANYRAID_HEADER_GUID, spa_guid(spa));
	fnvlist_add_uint64(header, VDEV_ANYRAID_HEADER_TILE_SIZE,
	    var->vd_tile_size);
	fnvlist_add_uint32(header, VDEV_ANYRAID_HEADER_LENGTH,
	    written * SPA_MAXBLOCKSIZE + buf_offset);
	fnvlist_add_uint16_array(header, VDEV_ANYRAID_HEADER_DISK_SIZES, sizes,
	    anyraidvd->vdev_children);

	if (var->vd_checkpoint_tile != UINT32_MAX) {
		fnvlist_add_uint32(header, VDEV_ANYRAID_HEADER_CHECKPOINT,
		    var->vd_checkpoint_tile);
	}
	size_t packed_size;
	char *packed = fnvlist_pack(header, &packed_size);
	ASSERT3U(packed_size, <, nvl_bytes);
	memcpy(header_buf, packed, packed_size);
	fnvlist_pack_free(packed, packed_size);
	abd_return_buf_copy(header_abd, header_buf, header_size);

	// Write out the header
	zio_t *header_zio = zio_write_phys(pio, vd, base_offset, header_size,
	    header_abd, ZIO_CHECKSUM_LABEL, vdev_anyraid_write_map_done,
	    good_writes, ZIO_PRIORITY_SYNC_WRITE, flags, B_FALSE);
	zio_nowait(header_zio);
	abd_free(header_abd);
}

static uint64_t
vdev_anyraid_min_asize(vdev_t *pvd, vdev_t *cvd)
{
	ASSERT3P(pvd->vdev_ops, ==, &vdev_anyraid_ops);
	ASSERT3U(spa_config_held(pvd->vdev_spa, SCL_ALL, RW_READER), !=, 0);
	vdev_anyraid_t *var = pvd->vdev_tsd;
	if (var->vd_tile_size == 0)
		return (VDEV_ANYRAID_TOTAL_MAP_SIZE(cvd->vdev_ashift));

	rw_enter(&var->vd_lock, RW_READER);
	uint64_t size = VDEV_ANYRAID_TOTAL_MAP_SIZE(cvd->vdev_ashift) +
	    var->vd_children[cvd->vdev_id]->van_next_offset *
	    var->vd_tile_size;
	rw_exit(&var->vd_lock);
	return (size);
}

/*
 * Used by the attach logic to determine if a device is big enough to be
 * usefully attached.
 */
uint64_t
vdev_anyraid_min_newsize(vdev_t *vd, uint64_t ashift)
{
	vdev_anyraid_t *var = vd->vdev_tsd;
	return (VDEV_LABEL_START_SIZE + VDEV_LABEL_END_SIZE +
	    VDEV_ANYRAID_TOTAL_MAP_SIZE(ashift) + var->vd_tile_size);
}

void
vdev_anyraid_expand(vdev_t *tvd, vdev_t *newvd)
{
	vdev_anyraid_t *var = tvd->vdev_tsd;
	uint64_t old_children = tvd->vdev_children - 1;

	ASSERT3U(spa_config_held(tvd->vdev_spa, SCL_ALL, RW_WRITER), ==,
	    SCL_ALL);
	vdev_anyraid_node_t **nc = kmem_alloc(tvd->vdev_children * sizeof (*nc),
	    KM_SLEEP);
	vdev_anyraid_node_t *newchild = kmem_alloc(sizeof (*newchild),
	    KM_SLEEP);
	newchild->van_id = newvd->vdev_id;
	newchild->van_next_offset = 0;
	newchild->van_capacity = (newvd->vdev_asize -
	    VDEV_ANYRAID_TOTAL_MAP_SIZE(newvd->vdev_ashift)) /
	    var->vd_tile_size;
	rw_enter(&var->vd_lock, RW_WRITER);
	memcpy(nc, var->vd_children, old_children * sizeof (*nc));
	kmem_free(var->vd_children, old_children * sizeof (*nc));
	var->vd_children = nc;
	var->vd_children[old_children] = newchild;
	avl_add(&var->vd_children_tree, newchild);
	rw_exit(&var->vd_lock);
}

/*
 * Return the maximum asize for a rebuild zio in the provided range
 * given the following constraints.  An anyraid chunk may not:
 *
 * - Exceed the maximum allowed block size (SPA_MAXBLOCKSIZE), or
 * - Span anyraid tiles
 */
static uint64_t
vdev_anyraid_rebuild_asize(vdev_t *vd, uint64_t start, uint64_t asize,
    uint64_t max_segment)
{
	vdev_anyraid_t *var = vd->vdev_tsd;
	ASSERT3P(vd->vdev_ops, ==, &vdev_anyraid_ops);

	uint64_t psize = MIN(P2ROUNDUP(max_segment, 1 << vd->vdev_ashift),
	    SPA_MAXBLOCKSIZE);

	if (start / var->vd_tile_size !=
	    (start + psize) / var->vd_tile_size) {
		psize = P2ROUNDUP(start, var->vd_tile_size) - start;
	}

	return (MIN(asize, vdev_psize_to_asize(vd, psize)));
}

vdev_ops_t vdev_anyraid_ops = {
	.vdev_op_init = vdev_anyraid_init,
	.vdev_op_fini = vdev_anyraid_fini,
	.vdev_op_open = vdev_anyraid_open,
	.vdev_op_close = vdev_anyraid_close,
	.vdev_op_psize_to_asize = vdev_default_asize,
	.vdev_op_asize_to_psize = vdev_default_asize,
	.vdev_op_min_asize = vdev_anyraid_min_asize,
	.vdev_op_min_alloc = NULL,
	.vdev_op_io_start = vdev_anyraid_io_start,
	.vdev_op_io_done = vdev_anyraid_io_done,
	.vdev_op_state_change = vdev_anyraid_state_change,
	.vdev_op_need_resilver = vdev_anyraid_need_resilver,
	.vdev_op_hold = NULL,
	.vdev_op_rele = NULL,
	.vdev_op_remap = NULL,
	.vdev_op_xlate = vdev_anyraid_xlate,
	.vdev_op_rebuild_asize = vdev_anyraid_rebuild_asize,
	.vdev_op_metaslab_init = NULL,
	.vdev_op_config_generate = vdev_anyraid_config_generate,
	.vdev_op_nparity = vdev_anyraid_nparity,
	.vdev_op_ndisks = vdev_anyraid_ndisks,
	.vdev_op_metaslab_size = vdev_anyraid_metaslab_size,
	.vdev_op_type = VDEV_TYPE_ANYRAID,	/* name of this vdev type */
	.vdev_op_leaf = B_FALSE			/* not a leaf vdev */
};


ZFS_MODULE_PARAM(zfs_anyraid, zfs_anyraid_, min_tile_size, U64, ZMOD_RW,
	"Minimum tile size for anyraid");