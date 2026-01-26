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
 * logical-to-physical mapping: bytes within that logical tile are stored
 * physically together. Subsequent tiles may be stored in different locations
 * on the same disk, or different disks altogether. A mapping is stored on each
 * disk to enable the vdev to be read normally.
 *
 * When parity is not considered, this provides some small benefits (device
 * removal within the vdev is not yet implemented, but is very feasible, as is
 * rebalancing data onto new disks), but is not generally recommended. However,
 * if parity is considered, it is more useful. With mirror parity P, each
 * tile is allocated onto P separate disks, providing the reliability and
 * performance characteristics of a mirror vdev. In addition, because each tile
 * can be allocated separately, smaller drives can work together to mirror
 * larger ones dynamically and seamlessly.
 *
 * The mapping for these tiles is stored in a special area at the start of
 * each device. Each disk has 4 full copies of the tile map, which rotate
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
#include <sys/metaslab_impl.h>
#include <sys/spa.h>
#include <sys/spa_impl.h>
#include <sys/vdev_impl.h>
#include <sys/vdev_anyraid.h>
#include <sys/vdev_anyraid_impl.h>
#include <sys/vdev_mirror.h>
#include <sys/vdev_raidz.h>
#include <sys/vdev_raidz_impl.h>
#include <sys/dsl_scan.h>

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

/*
 * Maximum amount of copy io's outstanding at once.
 */
#ifdef _ILP32
static unsigned long anyraid_relocate_max_move_bytes = SPA_MAXBLOCKSIZE;
#else
static unsigned long anyraid_relocate_max_move_bytes = SPA_MAXBLOCKSIZE;
#endif

/*
 * Automatically start a pool scrub when a RAIDZ expansion completes in
 * order to verify the checksums of all blocks which have been copied
 * during the expansion.  Automatic scrubbing is enabled by default and
 * is strongly recommended.
 */
static int zfs_scrub_after_relocate = 1;

/*
 * For testing only: pause the anyraid relocate operations after reflowing this
 * amount (accessed by ZTS and ztest).
 */
#ifdef	_KERNEL
static
#endif	/* _KERNEL */
unsigned long anyraid_relocate_max_bytes_pause = 0;

static int tasklist_read(vdev_t *vd);
static void anyraid_scrub_done(spa_t *spa, dmu_tx_t *tx, void *arg);

struct anyraid_done_arg {
	vdev_t *vd;
};

static int
af_compar(const void *p1, const void *p2)
{
	const anyraid_free_node_t *af1 = p1, *af2 = p2;

	return (TREE_CMP(af2->afn_tile, af1->afn_tile));
}

void
anyraid_freelist_create(anyraid_freelist_t *af, uint16_t off)
{
	avl_create(&af->af_list, af_compar,
	    sizeof (anyraid_free_node_t),
	    offsetof(anyraid_free_node_t, afn_node));
	af->af_next_off = off;
}

void
anyraid_freelist_destroy(anyraid_freelist_t *af)
{
	void *cookie = NULL;
	anyraid_free_node_t *node;
	while ((node = avl_destroy_nodes(&af->af_list, &cookie)) != NULL)
		kmem_free(node, sizeof (*node));
	avl_destroy(&af->af_list);
}

void
anyraid_freelist_add(anyraid_freelist_t *af, uint16_t off)
{
	avl_tree_t *t = &af->af_list;
	ASSERT3U(off, <, af->af_next_off);
	if (off != af->af_next_off - 1) {
		anyraid_free_node_t *new = kmem_alloc(sizeof (*new), KM_SLEEP);
		new->afn_tile = off;
		avl_add(t, new);
		return;
	}
	af->af_next_off--;
	for (anyraid_free_node_t *tail = avl_last(t);
	    tail != NULL && tail->afn_tile == af->af_next_off - 1;
	    tail = avl_last(t)) {
		af->af_next_off--;
		avl_remove(t, tail);
		kmem_free(tail, sizeof (*tail));
	}
}

void
anyraid_freelist_remove(anyraid_freelist_t *af, uint16_t off)
{
	avl_tree_t *t = &af->af_list;
	anyraid_free_node_t search;
	search.afn_tile = off;
	avl_index_t where;
	anyraid_free_node_t *node = avl_find(t, &search, &where);
	if (node) {
		avl_remove(t, node);
		kmem_free(node, sizeof (*node));
		return;
	}
	ASSERT3U(off, >=, af->af_next_off);
	while (off > af->af_next_off) {
		node = kmem_alloc(sizeof (*node), KM_SLEEP);
		node->afn_tile = af->af_next_off++;
		avl_add(t, node);
	}
	af->af_next_off++;
	return;

}

uint16_t
anyraid_freelist_pop(anyraid_freelist_t *af)
{
	avl_tree_t *t = &af->af_list;
	if (avl_numnodes(t) == 0) {
		return (af->af_next_off++);
	}

	anyraid_free_node_t *head = avl_first(t);
	avl_remove(t, head);
	uint16_t ret = head->afn_tile;
	kmem_free(head, sizeof (*head));
	return (ret);
}

uint16_t
anyraid_freelist_alloc(const anyraid_freelist_t *af)
{
	return (af->af_next_off - avl_numnodes(&af->af_list));
}

boolean_t
anyraid_freelist_isfree(const anyraid_freelist_t *af, uint16_t off)
{
	if (off >= af->af_next_off)
		return (B_TRUE);
	anyraid_free_node_t search;
	search.afn_tile = off;
	avl_index_t where;
	anyraid_free_node_t *node = avl_find(&af->af_list, &search, &where);
	return (node != NULL);
}

static inline uint64_t
vdev_anyraid_header_offset(vdev_t *vd, int id)
{
	uint64_t full_size = VDEV_ANYRAID_SINGLE_MAP_SIZE(vd->vdev_ashift);
	if (id < VDEV_ANYRAID_START_COPIES)
		return (VDEV_LABEL_START_SIZE + id * full_size);
	else
		return (vd->vdev_psize - VDEV_LABEL_END_SIZE -
		    (VDEV_ANYRAID_MAP_COPIES - id) * full_size);
}

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

	int cmp = TREE_CMP(
	    (int64_t)van2->van_capacity -
	    anyraid_freelist_alloc(&van2->van_freelist),
	    (int64_t)van1->van_capacity -
	    anyraid_freelist_alloc(&van1->van_freelist));
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
	if (error != 0 || children > VDEV_ANYRAID_MAX_DISKS)
		return (SET_ERROR(EINVAL));

	uint64_t nparity;
	if (nvlist_lookup_uint64(nv, ZPOOL_CONFIG_NPARITY, &nparity) != 0)
		return (SET_ERROR(EINVAL));

	vdev_anyraid_parity_type_t parity_type = VAP_TYPES;
	if (nvlist_lookup_uint8(nv, ZPOOL_CONFIG_ANYRAID_PARITY_TYPE,
	    (uint8_t *)&parity_type) != 0)
		return (SET_ERROR(EINVAL));
	uint8_t ndata = 1;
	if (nvlist_lookup_uint8(nv, ZPOOL_CONFIG_ANYRAID_NDATA,
	    &ndata) != 0 && parity_type == VAP_RAIDZ) {
		return (SET_ERROR(EINVAL));
	}

	if (ndata + nparity > children) {
		zfs_dbgmsg("width too high when creating anyraid vdev");
		return (SET_ERROR(EINVAL));
	}

	vdev_anyraid_t *va = kmem_zalloc(sizeof (*va), KM_SLEEP);
	va->vd_parity_type = parity_type;
	va->vd_ndata = ndata;
	va->vd_nparity = nparity;
	va->vd_contracting_leaf = -1;
	switch (parity_type) {
		case VAP_MIRROR:
			va->vd_width = ndata;
			break;
		case VAP_RAIDZ:
			va->vd_width = ndata + nparity;
			break;
		default:
			PANIC("Invalid parity type %d", parity_type);
	}
	rw_init(&va->vd_lock, NULL, RW_DEFAULT, NULL);
	avl_create(&va->vd_tile_map, anyraid_tile_compare,
	    sizeof (anyraid_tile_t), offsetof(anyraid_tile_t, at_node));
	avl_create(&va->vd_children_tree, anyraid_child_compare,
	    sizeof (vdev_anyraid_node_t),
	    offsetof(vdev_anyraid_node_t, van_node));
	zfs_rangelock_init(&va->vd_rangelock, NULL, NULL);
	vdev_anyraid_relocate_t *var = &va->vd_relocate;
	var->var_offset = var->var_failed_offset = UINT64_MAX;
	list_create(&var->var_list,
	    sizeof (vdev_anyraid_relocate_task_t),
	    offsetof(vdev_anyraid_relocate_task_t, vart_node));
	list_create(&var->var_done_list,
	    sizeof (vdev_anyraid_relocate_task_t),
	    offsetof(vdev_anyraid_relocate_task_t, vart_node));
	mutex_init(&var->var_lock, NULL, MUTEX_DEFAULT, NULL);
	cv_init(&var->var_cv, NULL, CV_DEFAULT, NULL);

	va->vd_children = kmem_zalloc(sizeof (*va->vd_children) * children,
	    KM_SLEEP);
	for (int c = 0; c < children; c++) {
		vdev_anyraid_node_t *van = kmem_zalloc(sizeof (*van), KM_SLEEP);
		van->van_id = c;
		anyraid_freelist_create(&van->van_freelist, 0);
		avl_add(&va->vd_children_tree, van);
		va->vd_children[c] = van;
	}

	*tsd = va;
	return (0);
}

static void
vdev_anyraid_fini(vdev_t *vd)
{
	vdev_anyraid_t *va = vd->vdev_tsd;

	if (vd->vdev_spa->spa_anyraid_relocate == &va->vd_relocate)
		vd->vdev_spa->spa_anyraid_relocate = NULL;
	avl_destroy(&va->vd_tile_map);

	vdev_anyraid_node_t *node;
	void *cookie = NULL;
	while ((node = avl_destroy_nodes(&va->vd_children_tree, &cookie))) {
		anyraid_freelist_destroy(&node->van_freelist);
		kmem_free(node, sizeof (*node));
	}
	avl_destroy(&va->vd_children_tree);
	zfs_rangelock_fini(&va->vd_rangelock);
	vdev_anyraid_relocate_t *var = &va->vd_relocate;
	mutex_destroy(&var->var_lock);
	cv_destroy(&var->var_cv);
	list_destroy(&var->var_list);
	list_destroy(&var->var_done_list);

	rw_destroy(&va->vd_lock);
	kmem_free(va->vd_children,
	    sizeof (*va->vd_children) * vd->vdev_children);
	kmem_free(va, sizeof (*va));
}

/*
 * Add ANYRAID specific fields to the config nvlist.
 */
static void
vdev_anyraid_config_generate(vdev_t *vd, nvlist_t *nv)
{
	ASSERT(vdev_is_anyraid(vd));
	vdev_anyraid_t *va = vd->vdev_tsd;

	fnvlist_add_uint64(nv, ZPOOL_CONFIG_NPARITY, va->vd_nparity);
	fnvlist_add_uint8(nv, ZPOOL_CONFIG_ANYRAID_PARITY_TYPE,
	    (uint8_t)va->vd_parity_type);
	fnvlist_add_uint8(nv, ZPOOL_CONFIG_ANYRAID_NDATA,
	    (uint8_t)va->vd_ndata);
}

/*
 * Import/open related functions.
 */

/*
 * Add an entry to the tile map for the provided tile.
 */
static void
create_tile_entry(spa_t *spa, vdev_anyraid_t *va,
    anyraid_map_loc_entry_t *amle, uint8_t *pat_cnt, anyraid_tile_t **out_at,
    uint32_t *cur_tile)
{
	uint8_t disk = amle_get_disk(amle);
	uint16_t offset = amle_get_offset(amle);
	anyraid_tile_t *at = *out_at;

	if (*pat_cnt == 0) {
		at = kmem_alloc(sizeof (*at), KM_SLEEP);
		at->at_tile_id = *cur_tile;
		at->at_synced = spa_current_txg(spa);
		avl_add(&va->vd_tile_map, at);
		list_create(&at->at_list,
		    sizeof (anyraid_tile_node_t),
		    offsetof(anyraid_tile_node_t, atn_node));

		(*cur_tile)++;
	}

	anyraid_tile_node_t *atn = kmem_alloc(sizeof (*atn), KM_SLEEP);
	atn->atn_disk = disk;
	atn->atn_tile_idx = offset;
	list_insert_tail(&at->at_list, atn);
	*pat_cnt = (*pat_cnt + 1) % (va->vd_nparity + va->vd_ndata);

	vdev_anyraid_node_t *van = va->vd_children[disk];
	avl_remove(&va->vd_children_tree, van);

	anyraid_freelist_remove(&van->van_freelist, offset);
	avl_add(&va->vd_children_tree, van);
	*out_at = at;
}

static void
child_read_done(zio_t *zio)
{
	zio_t *pio = zio_unique_parent(zio);
	abd_t **cbp = pio->io_private;

	if (zio->io_error == 0) {
		mutex_enter(&pio->io_lock);
		if (*cbp == NULL)
			*cbp = zio->io_abd;
		else
			abd_free(zio->io_abd);
		mutex_exit(&pio->io_lock);
	} else {
		abd_free(zio->io_abd);
	}
}

static void
child_read(zio_t *zio, vdev_t *vd, uint64_t offset, uint64_t size,
    int checksum, void *private, int flags)
{
	for (int c = 0; c < vd->vdev_children; c++) {
		child_read(zio, vd->vdev_child[c], offset, size, checksum,
		    private, flags);
	}

	if (vd->vdev_ops->vdev_op_leaf && vdev_readable(vd)) {
		zio_nowait(zio_read_phys(zio, vd, offset, size,
		    abd_alloc_linear(size, B_TRUE), checksum,
		    child_read_done, private, ZIO_PRIORITY_SYNC_READ, flags,
		    B_FALSE));
	}
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
	uint64_t header_offset = vdev_anyraid_header_offset(cvd, header);
	uint64_t header_size = VDEV_ANYRAID_MAP_HEADER_SIZE(ashift);
	int flags = ZIO_FLAG_CONFIG_WRITER | ZIO_FLAG_CANFAIL |
	    ZIO_FLAG_SPECULATIVE;

	abd_t *header_abd = NULL;
	zio_t *rio = zio_root(spa, NULL, &header_abd, flags);
	child_read(rio, cvd, header_offset, header_size, ZIO_CHECKSUM_LABEL,
	    NULL, flags);

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
anyraid_open_existing(vdev_t *vd, uint64_t child, uint32_t **child_capacities)
{
	vdev_anyraid_t *va = vd->vdev_tsd;
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

	*child_capacities = kmem_alloc(sizeof (**child_capacities) * count,
	    KM_SLEEP);
	for (int i = 0; i < count; i++)
		(*child_capacities)[i] = caps[i] + 1;
	if (vd->vdev_reopening) {
		if (va->vd_contracting_leaf != -1) {
			(*child_capacities)[va->vd_contracting_leaf] = 0;
		}
		free_header(&header, header_size);
		return (0);
	}

	uint32_t state = ARS_NONE;
	(void) nvlist_lookup_uint32(header.ah_nvl,
	    VDEV_ANYRAID_HEADER_RELOC_STATE, &state);
	if (state != ARS_NONE) {
		vdev_anyraid_relocate_t *var = &va->vd_relocate;
		var->var_state = state;
		var->var_vd = vd->vdev_id;
		if (spa->spa_anyraid_relocate != NULL) {
			zfs_dbgmsg("Error opening anyraid vdev %llu: Relocate "
			    "active when another relocate is in progress",
			    (u_longlong_t)vd->vdev_id);
			free_header(&header, header_size);
			return (EINVAL);
		}
		spa->spa_anyraid_relocate = var;
	}
	if (state == ARS_CONTRACTING)
		spa_async_request(spa, SPA_ASYNC_CONTRACTION_DONE);

	nvlist_t *cur_task;
	error = nvlist_lookup_nvlist(header.ah_nvl,
	    VDEV_ANYRAID_HEADER_CUR_TASK, &cur_task);
	if (error != 0 && error != ENOENT) {
		zfs_dbgmsg("Error opening anyraid vdev %llu: Error opening "
		    "relocate info %d", (u_longlong_t)vd->vdev_id, error);
		free_header(&header, header_size);
		return (error);
	}

	if (nvlist_lookup_uint32(header.ah_nvl,
	    VDEV_ANYRAID_HEADER_CONTRACTING_LEAF,
	    (uint32_t *)&va->vd_contracting_leaf) != 0)
		va->vd_contracting_leaf = -1;

	if (error == 0) {
		vdev_anyraid_relocate_t *var = &va->vd_relocate;

		ASSERT3U(var->var_state, ==, ARS_SCANNING);
		var->var_failed_offset = UINT64_MAX;
		var->var_failed_task = UINT64_MAX;

		var->var_offset = var->var_synced_offset =
		    fnvlist_lookup_uint64(cur_task, VART_OFFSET);
		var->var_task = var->var_synced_task =
		    fnvlist_lookup_uint32(cur_task, VART_TASK);
		zfs_dbgmsg("Setting at open %d", (int)var->var_task);
		vdev_anyraid_relocate_task_t *vart =
		    kmem_alloc(sizeof (*vart), KM_SLEEP);
		vart->vart_source_disk = fnvlist_lookup_uint8(cur_task,
		    VART_SOURCE_DISK);
		vart->vart_source_idx = fnvlist_lookup_uint16(cur_task,
		    VART_SOURCE_OFF);
		vart->vart_dest_disk = fnvlist_lookup_uint8(cur_task,
		    VART_DEST_DISK);
		vart->vart_dest_idx = fnvlist_lookup_uint16(cur_task,
		    VART_DEST_OFF);
		vart->vart_tile = fnvlist_lookup_uint32(cur_task,
		    VART_TILE);
		vart->vart_task = var->var_task;
		list_insert_head(&var->var_list, vart);
		(*child_capacities)[va->vd_contracting_leaf] = 0;
		spa->spa_anyraid_relocate = var;
	}

	va->vd_checkpoint_tile = UINT32_MAX;
	(void) nvlist_lookup_uint32(header.ah_nvl,
	    VDEV_ANYRAID_HEADER_CHECKPOINT, &va->vd_checkpoint_tile);

	/*
	 * Because the tile map is 64 MiB and the maximum IO size is 16MiB,
	 * we may need to issue up to 4 reads to read in the whole thing.
	 * Similarly, when processing the mapping, we need to iterate across
	 * the 4 separate buffers.
	 */
	zio_t *rio = zio_root(spa, NULL, NULL, flags);
	abd_t *map_abds[VDEV_ANYRAID_MAP_COPIES] = {0};
	uint64_t header_offset = vdev_anyraid_header_offset(cvd, mapping);
	uint64_t map_offset = header_offset + header_size;
	int i;
	for (i = 0; i <= (map_length / SPA_MAXBLOCKSIZE); i++) {
		zio_eck_t *cksum = (zio_eck_t *)
		    &header.ah_buf[VDEV_ANYRAID_NVL_BYTES(ashift) +
		    i * sizeof (*cksum)];
		zio_t *nio = zio_null(rio, spa, cvd, NULL, &map_abds[i], flags);
		child_read(nio, cvd, map_offset + i * SPA_MAXBLOCKSIZE,
		    SPA_MAXBLOCKSIZE, ZIO_CHECKSUM_ANYRAID_MAP, cksum, flags);
		zio_nowait(nio);
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
	anyraid_tile_t *at = NULL;
	for (uint32_t off = 0; off < map_length; off += size) {
		if (checkpoint_rb && cur_tile > va->vd_checkpoint_tile &&
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

#ifdef _ZFS_BIG_ENDIAN
			uint32_t length = map_length -
			    next_map * SPA_MAXBLOCKSIZE;
			byteswap_uint32_array(map_buf, MIN(length,
			    SPA_MAXBLOCKSIZE));
#endif
		}
		anyraid_map_entry_t *entry =
		    (anyraid_map_entry_t *)(map_buf + (off % SPA_MAXBLOCKSIZE));
		uint8_t type = ame_get_type(entry);
		switch (type) {
			case AMET_SKIP: {
				anyraid_map_skip_entry_t *amse =
				    &entry->ame_u.ame_amse;
				ASSERT0(pat_cnt);
				cur_tile += amse_get_skip_count(amse);
				break;
			}
			case AMET_LOC: {
				anyraid_map_loc_entry_t *amle =
				    &entry->ame_u.ame_amle;
				create_tile_entry(vd->vdev_spa, va, amle,
				    &pat_cnt, &at, &cur_tile);
				break;
			}
			default:
				PANIC("Invalid entry type %d", type);
		}
	}
	if (map_buf)
		abd_return_buf(map_abds[map], map_buf, SPA_MAXBLOCKSIZE);

	va->vd_tile_size = tile_size;

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

	if (numerrors > va->vd_nparity) {
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
	vdev_anyraid_t *va = vd->vdev_tsd;

	uint64_t smallest_disk_size = UINT64_MAX;
	for (int c = 0; c < vd->vdev_children; c++) {
		vdev_t *cvd = vd->vdev_child[c];
		smallest_disk_size = MIN(smallest_disk_size, cvd->vdev_asize -
		    VDEV_ANYRAID_TOTAL_MAP_SIZE(cvd->vdev_ashift));
	}

	uint64_t disk_shift = anyraid_disk_shift;
	uint64_t min_size = zfs_anyraid_min_tile_size;
	if (smallest_disk_size < 1 << disk_shift ||
	    smallest_disk_size < min_size) {
		return (SET_ERROR(ENOLCK));
	}


	ASSERT3U(smallest_disk_size, !=, UINT64_MAX);
	uint64_t tile_size = smallest_disk_size >> disk_shift;
	tile_size = MAX(tile_size, min_size);
	va->vd_tile_size = 1ULL << (highbit64(tile_size - 1));

	/*
	 * Later, we're going to cap the metaslab size at the tile
	 * size, so we need a tile to hold at least enough to store a
	 * max-size block, or we'll assert in that code.
	 */
	if (va->vd_tile_size * va->vd_ndata < SPA_MAXBLOCKSIZE)
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
	vdev_anyraid_t *va = vd->vdev_tsd;

	if (va->vd_nparity == 0) {
		uint64_t count = 0;
		for (int c = 0; c < vd->vdev_children; c++) {
			count += num_tiles[c];
		}
		return (count * va->vd_tile_size);
	}

	/*
	 * Sort the disks by the number of additional tiles they can store.
	 */
	avl_tree_t t;
	avl_create(&t, rc_compar, sizeof (struct tile_count),
	    offsetof(struct tile_count, node));
	for (int c = 0; c < vd->vdev_children; c++) {
		if (num_tiles[c] == 0) {
			ASSERTF(vd->vdev_child[c]->vdev_open_error ||
			    va->vd_contracting_leaf == c, "%d %d",
			    va->vd_contracting_leaf, c);
			continue;
		}
		struct tile_count *rc = kmem_alloc(sizeof (*rc), KM_SLEEP);
		rc->disk = c;
		rc->remaining = num_tiles[c] -
		    anyraid_freelist_alloc(&va->vd_children[c]->van_freelist);
		avl_add(&t, rc);
	}

	uint32_t map_width = va->vd_nparity + va->vd_ndata;
	uint64_t count = avl_numnodes(&va->vd_tile_map);
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
		kmem_free(cur[c], sizeof (*cur[c]));
	kmem_free(cur, sizeof (*cur) * map_width);
	void *cookie = NULL;
	struct tile_count *node;

	while ((node = avl_destroy_nodes(&t, &cookie)) != NULL)
		kmem_free(node, sizeof (*node));
	avl_destroy(&t);
	return (count * va->vd_width * va->vd_tile_size);
}

static int
vdev_anyraid_open(vdev_t *vd, uint64_t *asize, uint64_t *max_asize,
    uint64_t *logical_ashift, uint64_t *physical_ashift)
{
	vdev_anyraid_t *va = vd->vdev_tsd;
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
	if (numerrors > va->vd_nparity) {
		vd->vdev_stat.vs_aux = VDEV_AUX_NO_REPLICAS;
		return (lasterror);
	}

	uint32_t *child_capacities = NULL;
	if (vd->vdev_reopening) {
		child_capacities = kmem_alloc(sizeof (*child_capacities) *
		    vd->vdev_children, KM_SLEEP);
		for (uint64_t c = 0; c < vd->vdev_children; c++) {
			child_capacities[c] = va->vd_children[c]->van_capacity;
		}
		if (va->vd_contracting_leaf != -1)
			child_capacities[va->vd_contracting_leaf] = 0;
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

	uint64_t max_size = VDEV_ANYRAID_MAX_TPD * va->vd_tile_size;

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
			casize = MIN(max_size, cvd->vdev_asize -
			    VDEV_ANYRAID_TOTAL_MAP_SIZE(cvd->vdev_ashift));
		} else {
			ASSERT(child_capacities);
			casize = child_capacities[c] * va->vd_tile_size;
		}

		num_tiles[c] = casize / va->vd_tile_size;
		avl_remove(&va->vd_children_tree, va->vd_children[c]);
		if (va->vd_contracting_leaf == c)
			va->vd_children[c]->van_capacity = 0;
		else
			va->vd_children[c]->van_capacity = num_tiles[c];
		avl_add(&va->vd_children_tree, va->vd_children[c]);
	}
	*asize = calculate_asize(vd, num_tiles);

	for (int c = 0; c < vd->vdev_children; c++) {
		vdev_t *cvd = vd->vdev_child[c];

		uint64_t cmasize;
		if (cvd->vdev_open_error == 0) {
			cmasize = MIN(max_size, cvd->vdev_max_asize -
			    VDEV_ANYRAID_TOTAL_MAP_SIZE(cvd->vdev_ashift));
		} else {
			cmasize = child_capacities[c] * va->vd_tile_size;
		}

		num_tiles[c] = cmasize / va->vd_tile_size;
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
	kmem_free(num_tiles, vd->vdev_children * sizeof (*num_tiles));
	return (0);
}

int
vdev_anyraid_load(vdev_t *vd)
{
	vdev_anyraid_t *va = vd->vdev_tsd;

	if (va->vd_relocate.var_state == ARS_NONE ||
	    va->vd_relocate.var_state == ARS_FINISHED)
		return (0);

	return (tasklist_read(vd));
}

/*
 * We cap the metaslab size at the tile size. This prevents us from having to
 * split IOs across multiple tiles, which would be complex extra logic for
 * little gain.
 */
static void
vdev_anyraid_metaslab_size(vdev_t *vd, uint64_t *shiftp)
{
	vdev_anyraid_t *va = vd->vdev_tsd;
	*shiftp = MIN(*shiftp, highbit64(va->vd_tile_size) - 1);
}

static void
vdev_anyraid_close(vdev_t *vd)
{
	vdev_anyraid_t *va = vd->vdev_tsd;
	for (int c = 0; c < vd->vdev_children; c++) {
		if (vd->vdev_child[c] != NULL)
			vdev_close(vd->vdev_child[c]);
	}
	if (vd->vdev_reopening)
		return;
	anyraid_tile_t *tile = NULL;
	void *cookie = NULL;
	while ((tile = avl_destroy_nodes(&va->vd_tile_map, &cookie))) {
		if (va->vd_nparity != 0) {
			anyraid_tile_node_t *atn = NULL;
			while ((atn = list_remove_head(&tile->at_list))) {
				kmem_free(atn, sizeof (*atn));
			}
			list_destroy(&tile->at_list);
		}
		kmem_free(tile, sizeof (*tile));
	}

	if (vd->vdev_spa->spa_anyraid_relocate == &va->vd_relocate)
		vd->vdev_spa->spa_anyraid_relocate = NULL;
	vdev_anyraid_relocate_t *var = &va->vd_relocate;
	vdev_anyraid_relocate_task_t *vart;
	while ((vart = list_remove_head(&var->var_list)))
		kmem_free(vart, sizeof (*vart));
	while ((vart = list_remove_head(&var->var_done_list)))
		kmem_free(vart, sizeof (*vart));
	var->var_synced_offset = var->var_offset = 0;
	var->var_synced_task = var->var_task = 0;
}

/*
 * Configure the mirror_map and then hand the write off to the normal mirror
 * logic.
 */
static void
vdev_anyraid_mirror_start(zio_t *zio, anyraid_tile_t *tile,
    vdev_anyraid_relocate_task_t *task, zfs_locked_range_t *lr)
{
	vdev_t *vd = zio->io_vd;
	vdev_anyraid_t *va = vd->vdev_tsd;
	mirror_map_t *mm = vdev_mirror_map_alloc(va->vd_nparity + 1, B_FALSE,
	    B_FALSE);
	uint64_t tsize = va->vd_tile_size;

	anyraid_tile_node_t *atn = list_head(&tile->at_list);
	for (int c = 0; c < mm->mm_children; c++) {
		uint8_t disk;
		uint16_t offset;
		if (task && task->vart_source_disk == atn->atn_disk) {
			disk = task->vart_dest_disk;
			offset = task->vart_source_idx;
		} else {
			disk = atn->atn_disk;
			offset = atn->atn_tile_idx;
		}
		ASSERT(atn);
		mirror_child_t *mc = &mm->mm_child[c];
		mc->mc_vd = vd->vdev_child[disk];
		mc->mc_offset = VDEV_ANYRAID_START_OFFSET(vd->vdev_ashift) +
		    offset * tsize + zio->io_offset % tsize;
		ASSERT3U(mc->mc_offset, <, mc->mc_vd->vdev_psize -
		    VDEV_LABEL_END_SIZE);
		mm->mm_rebuilding = mc->mc_rebuilding = B_FALSE;
		atn = list_next(&tile->at_list, atn);
	}
	ASSERT(atn == NULL);

	zio->io_aux_vsd = lr;
	zio->io_vsd = mm;
	zio->io_vsd_ops = &vdev_mirror_vsd_ops;

	vdev_mirror_io_start_impl(zio, mm);
}

/*
 * Translate the allocated and configured raidz map to use the proper disks
 * based on the anyraid tile mapping.
 */
static void
vdev_anyraid_raidz_map_translate(vdev_t *vd, raidz_map_t *rm,
    anyraid_tile_t *tile, vdev_anyraid_relocate_task_t *task)
{
	vdev_anyraid_t *va = vd->vdev_tsd;
	ASSERT3U(rm->rm_nrows, ==, 1);
	raidz_row_t *rr = rm->rm_row[0];
	anyraid_tile_node_t **mapping = kmem_zalloc(sizeof (*mapping) *
	    va->vd_width, KM_SLEEP);
	ASSERT(tile);
	anyraid_tile_node_t *atn = list_head(&tile->at_list);
	for (int i = 0; i < va->vd_width; i++) {
		ASSERT(atn);
		mapping[i] = atn;
		atn = list_next(&tile->at_list, atn);
	}
	ASSERT3U(rr->rr_scols, <=, va->vd_width);
	for (uint64_t c = 0; c < rr->rr_scols; c++) {
		raidz_col_t *rc = &rr->rr_col[c];
		atn = mapping[rc->rc_devidx];
		uint8_t disk;
		uint16_t offset;
		if (task && task->vart_source_disk == atn->atn_disk) {
			disk = task->vart_dest_disk;
			offset = task->vart_source_idx;
		} else {
			disk = atn->atn_disk;
			offset = atn->atn_tile_idx;
		}
		uint64_t tile_off = rc->rc_offset % va->vd_tile_size;
		uint64_t disk_off = tile_off +
		    offset * va->vd_tile_size;
		rc->rc_offset = VDEV_ANYRAID_START_OFFSET(vd->vdev_ashift) +
		    disk_off;
		rc->rc_devidx = disk;
	}
	kmem_free(mapping, sizeof (*mapping) * va->vd_width);
}

/*
 * Configure the raidz_map and then hand the write off to the normal raidz
 * logic.
 */
static void
vdev_anyraid_raidz_start(zio_t *zio, anyraid_tile_t *tile,
    vdev_anyraid_relocate_task_t *task, zfs_locked_range_t *lr)
{
	vdev_t *vd = zio->io_vd;
	vdev_anyraid_t *va = vd->vdev_tsd;
	raidz_map_t *rm = vdev_raidz_map_alloc(zio, vd->vdev_ashift,
	    va->vd_width, va->vd_nparity);
	vdev_anyraid_raidz_map_translate(vd, rm, tile, task);

	zio->io_vsd = rm;
	zio->io_vsd_ops = &vdev_raidz_vsd_ops;
	zio->io_aux_vsd = lr;
	vdev_raidz_io_start_impl(zio, rm, va->vd_width, va->vd_width);
}

typedef struct anyraid_map {
	abd_t *am_abd;
} anyraid_map_t;

static void
vdev_anyraid_child_done(zio_t *zio)
{
	zio_t *pio = zio->io_private;
	mutex_enter(&pio->io_lock);
	pio->io_error = zio_worst_error(pio->io_error, zio->io_error);
	mutex_exit(&pio->io_lock);
}

static void
vdev_anyraid_map_free_vsd(zio_t *zio)
{
	anyraid_map_t *am = zio->io_vsd;
	abd_free(am->am_abd);
	am->am_abd = NULL;
	kmem_free(am, sizeof (*am));
}

const zio_vsd_ops_t vdev_anyraid_vsd_ops = {
	.vsd_free = vdev_anyraid_map_free_vsd,
};

static void
vdev_anyraid_io_start(zio_t *zio)
{
	vdev_t *vd = zio->io_vd;
	vdev_anyraid_t *va = vd->vdev_tsd;
	uint64_t tsize = va->vd_tile_size * va->vd_width;

	uint64_t start_tile_id = zio->io_offset / tsize;
	anyraid_tile_t search;
	search.at_tile_id = start_tile_id;
	avl_index_t where;
	rw_enter(&va->vd_lock, RW_READER);
	anyraid_tile_t *tile = avl_find(&va->vd_tile_map, &search,
	    &where);

	/*
	 * If we're doing an I/O somewhere that hasn't been allocated yet, we
	 * may need to allocate a new tile. Upgrade to a write lock so we can
	 * safely modify the data structure, and then check if someone else
	 * beat us to it.
	 */
	if (tile == NULL) {
		rw_exit(&va->vd_lock);
		rw_enter(&va->vd_lock, RW_WRITER);
		tile = avl_find(&va->vd_tile_map, &search, &where);
	}
	if (tile == NULL) {
		ASSERT3U(zio->io_type, ==, ZIO_TYPE_WRITE);
		zfs_dbgmsg("Allocating tile %llu for zio %px",
		    (u_longlong_t)start_tile_id, zio);
		tile = kmem_alloc(sizeof (*tile), KM_SLEEP);
		tile->at_tile_id = start_tile_id;
		list_create(&tile->at_list, sizeof (anyraid_tile_node_t),
		    offsetof(anyraid_tile_node_t, atn_node));

		uint_t width = va->vd_nparity + va->vd_ndata;
		vdev_anyraid_node_t **vans = kmem_alloc(sizeof (*vans) * width,
		    KM_SLEEP);
		for (int i = 0; i < width; i++) {
			vans[i] = avl_first(&va->vd_children_tree);
			avl_remove(&va->vd_children_tree, vans[i]);

			ASSERT3U(vans[i]->van_id, !=, va->vd_contracting_leaf);
			anyraid_tile_node_t *atn =
			    kmem_alloc(sizeof (*atn), KM_SLEEP);
			atn->atn_disk = vans[i]->van_id;
			atn->atn_tile_idx =
			    anyraid_freelist_pop(&vans[i]->van_freelist);
			list_insert_tail(&tile->at_list, atn);
		}
		for (int i = 0; i < width; i++)
			avl_add(&va->vd_children_tree, vans[i]);

		kmem_free(vans, sizeof (*vans) * width);
		avl_insert(&va->vd_tile_map, tile, where);
	}

	zfs_locked_range_t *lr = zfs_rangelock_enter(&va->vd_rangelock,
	    zio->io_offset, zio->io_size, RL_READER);

	vdev_anyraid_relocate_task_t *task = NULL;
	if (va->vd_relocate.var_state == ARS_SCANNING) {
		vdev_anyraid_relocate_t *var = &va->vd_relocate;
		mutex_enter(&var->var_lock);
		vdev_anyraid_relocate_task_t *vart = list_head(&var->var_list);
		if (vart && vart->vart_tile == tile->at_tile_id) {
			ASSERTF(var->var_offset <= zio->io_offset ||
			    var->var_offset >= zio->io_offset + zio->io_size,
			    "var_offset %llx is in the middle of IO %llx/%llx "
			    "%d %llx", (u_longlong_t)var->var_offset,
			    (u_longlong_t)zio->io_offset,
			    (u_longlong_t)zio->io_size, zio->io_type,
			    (u_longlong_t)zio->io_flags);
			if (var->var_offset >= zio->io_offset + zio->io_size) {
				task = kmem_zalloc(sizeof (*vart), KM_SLEEP);
				*task = *vart;
			}
		}
		mutex_exit(&var->var_lock);
	}
	rw_exit(&va->vd_lock);

	switch (va->vd_parity_type) {
		case VAP_MIRROR:
			if (va->vd_nparity > 0) {
				vdev_anyraid_mirror_start(zio, tile, task, lr);
				zio_execute(zio);
				if (task)
					kmem_free(task, sizeof (*task));
				return;
			}
			break;
		case VAP_RAIDZ:
			vdev_anyraid_raidz_start(zio, tile, task, lr);
			zio_execute(zio);
			if (task)
				kmem_free(task, sizeof (*task));
			return;
		default:
			ASSERT0(1);
			PANIC("Invalid parity type: %d", va->vd_parity_type);
	}


	anyraid_tile_node_t *atn = list_head(&tile->at_list);
	vdev_t *cvd = vd->vdev_child[atn->atn_disk];
	uint64_t child_offset = atn->atn_tile_idx * tsize +
	    zio->io_offset % tsize;
	child_offset += VDEV_ANYRAID_START_OFFSET(vd->vdev_ashift);

	anyraid_map_t *mm = kmem_alloc(sizeof (*mm), KM_SLEEP);
	mm->am_abd = abd_get_offset(zio->io_abd, 0);
	zio->io_vsd = mm;
	zio->io_vsd_ops = &vdev_anyraid_vsd_ops;
	zio->io_aux_vsd = lr;

	zio_t *cio = zio_vdev_child_io(zio, NULL, cvd, child_offset,
	    mm->am_abd, zio->io_size, zio->io_type, zio->io_priority, 0,
	    vdev_anyraid_child_done, zio);
	zio_nowait(cio);

	zio_execute(zio);
	if (task)
		kmem_free(task, sizeof (*task));
}

static void
vdev_anyraid_io_done(zio_t *zio)
{
	vdev_t *vd = zio->io_vd;
	vdev_anyraid_t *va = vd->vdev_tsd;

	switch (va->vd_parity_type) {
		case VAP_MIRROR:
			if (va->vd_nparity > 0) {
				vdev_mirror_io_done(zio);
				break;
			}
			break;
		case VAP_RAIDZ:
			vdev_raidz_io_done(zio);
			break;
		default:
			panic("Invalid parity type: %d", va->vd_parity_type);
	}
	if (zio->io_stage != ZIO_STAGE_VDEV_IO_DONE)
		return;
	zfs_locked_range_t *lr = zio->io_aux_vsd;
	ASSERT(lr);
	zfs_rangelock_exit(lr);
	zio->io_aux_vsd = NULL;
}

static void
vdev_anyraid_state_change(vdev_t *vd, int faulted, int degraded)
{
	vdev_anyraid_t *va = vd->vdev_tsd;
	if (faulted > va->vd_nparity) {
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
	vdev_anyraid_t *va = vd->vdev_tsd;
	// TODO should we always resilver if we're rebalancing/contracting?
	if (!vdev_dtl_contains(vd, DTL_PARTIAL, phys_birth, 1))
		return (B_FALSE);

	uint64_t tsize = va->vd_tile_size * va->vd_width;
	uint64_t start_tile_id = DVA_GET_OFFSET(dva) / tsize;
	anyraid_tile_t search;
	search.at_tile_id = start_tile_id;
	avl_index_t where;
	rw_enter(&va->vd_lock, RW_READER);
	anyraid_tile_t *tile = avl_find(&va->vd_tile_map, &search,
	    &where);
	rw_exit(&va->vd_lock);
	ASSERT(tile);

	for (anyraid_tile_node_t *atn = list_head(&tile->at_list);
	    atn != NULL; atn = list_next(&tile->at_list, atn)) {
		vdev_t *cvd = vd->vdev_child[atn->atn_disk];

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
	ASSERT(vdev_is_anyraid(anyraidvd));
	vdev_anyraid_t *va = anyraidvd->vdev_tsd;
	uint64_t ptsize = va->vd_tile_size;
	uint64_t ltsize = ptsize * va->vd_width;
	// TODO should we always fail if we're rebalancing/contracting?

	uint64_t start_tile_id = logical_rs->rs_start / ltsize;
	ASSERT3U(start_tile_id, ==, (logical_rs->rs_end - 1) / ltsize);
	anyraid_tile_t search;
	search.at_tile_id = start_tile_id;
	avl_index_t where;
	rw_enter(&va->vd_lock, RW_READER);
	anyraid_tile_t *tile = avl_find(&va->vd_tile_map, &search,
	    &where);
	rw_exit(&va->vd_lock);
	// This tile doesn't exist yet
	if (tile == NULL) {
		physical_rs->rs_start = physical_rs->rs_end = 0;
		return;
	}
	uint64_t idx = 0;
	anyraid_tile_node_t *atn = list_head(&tile->at_list);
	for (; atn != NULL; atn = list_next(&tile->at_list, atn), idx++)
		if (anyraidvd->vdev_child[atn->atn_disk] == cvd)
			break;
	// The tile exists, but isn't stored on this child
	if (atn == NULL) {
		physical_rs->rs_start = physical_rs->rs_end = 0;
		return;
	}

	switch (va->vd_parity_type) {
		case VAP_MIRROR:
		{
			uint64_t child_offset = atn->atn_tile_idx * ptsize +
			    logical_rs->rs_start % ptsize;
			child_offset +=
			    VDEV_ANYRAID_START_OFFSET(anyraidvd->vdev_ashift);
			uint64_t size = logical_rs->rs_end -
			    logical_rs->rs_start;

			physical_rs->rs_start = child_offset;
			physical_rs->rs_end = child_offset + size;
			break;
		}
		case VAP_RAIDZ:
		{
			uint64_t width = va->vd_width;
			uint64_t tgt_col = idx;
			uint64_t ashift = anyraidvd->vdev_ashift;
			uint64_t tile_start = VDEV_ANYRAID_START_OFFSET(
			    anyraidvd->vdev_ashift) + atn->atn_tile_idx *
			    ptsize;

			uint64_t b_start =
			    (logical_rs->rs_start % ltsize) >> ashift;
			uint64_t b_end =
			    (logical_rs->rs_end % ltsize) >> ashift;

			uint64_t start_row = 0;
			/* avoid underflow */
			if (b_start > tgt_col) {
				start_row = ((b_start - tgt_col - 1) / width) +
				    1;
			}

			uint64_t end_row = 0;
			if (b_end > tgt_col)
				end_row = ((b_end - tgt_col - 1) / width) + 1;

			physical_rs->rs_start =
			    tile_start + (start_row << ashift);
			physical_rs->rs_end =
			    tile_start + (end_row << ashift);
			break;
		}
		default:
			panic("Invalid parity type: %d", va->vd_parity_type);
	}
	remain_rs->rs_start = 0;
	remain_rs->rs_end = 0;
}

static uint64_t
vdev_anyraid_nparity(vdev_t *vd)
{
	vdev_anyraid_t *va = vd->vdev_tsd;
	return (va->vd_nparity);
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
map_write_loc_entry(anyraid_tile_node_t *atn, void *buf, uint32_t *offset)
{
	anyraid_map_loc_entry_t *entry = (void *)((char *)buf + *offset);
	amle_set_type(entry);
	amle_set_disk(entry, atn->atn_disk);
	amle_set_offset(entry, atn->atn_tile_idx);
	*offset += sizeof (*entry);
	return (*offset == SPA_MAXBLOCKSIZE);
}

static boolean_t
map_write_skip_entry(uint32_t tile, void *buf, uint32_t *offset,
    uint32_t prev_id)
{
	anyraid_map_skip_entry_t *entry = (void *)((char *)buf + *offset);
	amse_set_type(entry);
	amse_set_skip_count(entry, tile - prev_id - 1);
	*offset += sizeof (*entry);
	return (*offset == SPA_MAXBLOCKSIZE);
}

static void
anyraid_map_write_done(zio_t *zio)
{
	abd_free(zio->io_abd);
}

static void
map_write_issue(zio_t *zio, vdev_t *vd, uint64_t base_offset,
    uint8_t idx, uint32_t length, abd_t *abd, zio_eck_t *cksum_out,
    int flags)
{
#ifdef _ZFS_BIG_ENDIAN
	void *buf = abd_borrow_buf(abd, SPA_MAXBLOCKSIZE);
	byteswap_uint32_array(buf, length);
	abd_return_buf(abd, buf, SPA_MAXBLOCKSIZE);
#else
	(void) length;
#endif

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
	ASSERT(vdev_is_anyraid(anyraidvd));
	spa_t *spa = vd->vdev_spa;
	vdev_anyraid_t *va = anyraidvd->vdev_tsd;
	uint32_t header_size = VDEV_ANYRAID_MAP_HEADER_SIZE(vd->vdev_ashift);
	uint32_t nvl_bytes = VDEV_ANYRAID_NVL_BYTES(vd->vdev_ashift);
	uint8_t update_target = txg % VDEV_ANYRAID_MAP_COPIES;
	uint64_t base_offset = vdev_anyraid_header_offset(vd, update_target);

	abd_t *header_abd =
	    abd_alloc_linear(header_size, B_TRUE);
	abd_zero(header_abd, header_size);
	void *header_buf = abd_borrow_buf(header_abd, header_size);
	zio_eck_t *cksums = (zio_eck_t *)&((char *)header_buf)[nvl_bytes];

	abd_t *map_abd = abd_alloc_linear(SPA_MAXBLOCKSIZE, B_TRUE);
	uint8_t written = 0;
	void *buf = abd_borrow_buf(map_abd, SPA_MAXBLOCKSIZE);

	rw_enter(&va->vd_lock, RW_READER);
	anyraid_tile_t *cur = avl_first(&va->vd_tile_map);
	anyraid_tile_node_t *curn = cur != NULL ?
	    list_head(&cur->at_list) : NULL;
	uint32_t buf_offset = 0, prev_id = UINT32_MAX;
	zio_t *zio = zio_root(spa, NULL, NULL, flags);
	/* Write out each sub-tile in turn */
	while (cur) {
		if (status == VDEV_CONFIG_REWINDING_CHECKPOINT &&
		    cur->at_tile_id > va->vd_checkpoint_tile)
			break;

		anyraid_tile_t *next = AVL_NEXT(&va->vd_tile_map, cur);
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
			    buf_offset, map_abd, &cksums[written], flags);

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

	if (status == VDEV_CONFIG_NO_CHECKPOINT ||
	    status == VDEV_CONFIG_REWINDING_CHECKPOINT) {
		va->vd_checkpoint_tile = UINT32_MAX;
	} else if (status == VDEV_CONFIG_CREATING_CHECKPOINT) {
		anyraid_tile_t *at = avl_last(&va->vd_tile_map);
		ASSERT(at);
		va->vd_checkpoint_tile = at->at_tile_id;
	}
	rw_exit(&va->vd_lock);

	abd_return_buf_copy(map_abd, buf, SPA_MAXBLOCKSIZE);
	map_write_issue(zio, vd, base_offset, written, buf_offset, map_abd,
	    &cksums[written], flags);

	if (zio_wait(zio))
		return;

	// Populate the header
	uint16_t *sizes = kmem_zalloc(sizeof (*sizes) *
	    anyraidvd->vdev_children, KM_SLEEP);
	uint64_t disk_id = 0;
	for (uint64_t i = 0; i < anyraidvd->vdev_children; i++) {
		if (anyraidvd->vdev_child[i] == vd)
			disk_id = i;
		sizes[i] = va->vd_children[i]->van_capacity - 1;
	}
	ASSERT3U(disk_id, <, anyraidvd->vdev_children);
	nvlist_t *header = fnvlist_alloc();
	fnvlist_add_uint16(header, VDEV_ANYRAID_HEADER_VERSION, 0);
	fnvlist_add_uint8(header, VDEV_ANYRAID_HEADER_DISK, disk_id);
	fnvlist_add_uint64(header, VDEV_ANYRAID_HEADER_TXG, txg);
	fnvlist_add_uint64(header, VDEV_ANYRAID_HEADER_GUID, spa_guid(spa));
	fnvlist_add_uint64(header, VDEV_ANYRAID_HEADER_TILE_SIZE,
	    va->vd_tile_size);
	fnvlist_add_uint32(header, VDEV_ANYRAID_HEADER_LENGTH,
	    written * SPA_MAXBLOCKSIZE + buf_offset);
	fnvlist_add_uint16_array(header, VDEV_ANYRAID_HEADER_DISK_SIZES, sizes,
	    anyraidvd->vdev_children);
	kmem_free(sizes, sizeof (*sizes) * anyraidvd->vdev_children);

	if (va->vd_checkpoint_tile != UINT32_MAX) {
		fnvlist_add_uint32(header, VDEV_ANYRAID_HEADER_CHECKPOINT,
		    va->vd_checkpoint_tile);
	}
	vdev_anyraid_relocate_t *var = &va->vd_relocate;
	if (var->var_state != ARS_NONE && var->var_state != ARS_FINISHED)
		fnvlist_add_uint32(header, VDEV_ANYRAID_HEADER_RELOC_STATE,
		    (uint32_t)var->var_state);
	if (var->var_state == ARS_SCANNING) {
		mutex_enter(&va->vd_relocate.var_lock);
		uint64_t task = va->vd_relocate.var_synced_task;
		list_t *l = &va->vd_relocate.var_done_list;
		vdev_anyraid_relocate_task_t *vart = list_head(l);
		for (;;) {
			if (vart == NULL) {
				l = &va->vd_relocate.var_list;
				vart = list_head(l);
			}
			if (vart->vart_task == task)
				break;
			vart = list_next(l, vart);
		}
		nvlist_t *rebal_task = fnvlist_alloc();
		fnvlist_add_uint32(rebal_task, VART_TILE,
		    vart->vart_tile);
		fnvlist_add_uint8(rebal_task, VART_SOURCE_DISK,
		    vart->vart_source_disk);
		fnvlist_add_uint8(rebal_task, VART_DEST_DISK,
		    vart->vart_dest_disk);
		fnvlist_add_uint16(rebal_task, VART_SOURCE_OFF,
		    vart->vart_source_idx);
		fnvlist_add_uint16(rebal_task, VART_DEST_OFF,
		    vart->vart_dest_idx);
		fnvlist_add_uint64(rebal_task, VART_OFFSET,
		    va->vd_relocate.var_synced_offset);
		fnvlist_add_uint32(rebal_task, VART_TASK, task);
		fnvlist_add_nvlist(header,
		    VDEV_ANYRAID_HEADER_CUR_TASK, rebal_task);
		fnvlist_free(rebal_task);
		mutex_exit(&va->vd_relocate.var_lock);
	}
	if (va->vd_contracting_leaf != -1) {
		fnvlist_add_uint32(header,
		    VDEV_ANYRAID_HEADER_CONTRACTING_LEAF,
		    va->vd_contracting_leaf);
	}
	size_t packed_size;
	char *packed = NULL;
	VERIFY0(nvlist_pack(header, &packed, &packed_size, NV_ENCODE_XDR,
	    KM_SLEEP));
	fnvlist_free(header);
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
vdev_anyraid_min_attach_size(vdev_t *vd)
{
	ASSERT(vdev_is_anyraid(vd));
	ASSERT3U(spa_config_held(vd->vdev_spa, SCL_ALL, RW_READER), !=, 0);
	vdev_anyraid_t *va = vd->vdev_tsd;
	ASSERT(va->vd_tile_size);
	return (VDEV_ANYRAID_TOTAL_MAP_SIZE(vd->vdev_ashift) +
	    va->vd_tile_size);
}

static uint64_t
vdev_anyraid_min_asize(vdev_t *pvd, vdev_t *cvd)
{
	ASSERT(vdev_is_anyraid(pvd));
	vdev_anyraid_t *va = pvd->vdev_tsd;
	if (va->vd_tile_size == 0)
		return (VDEV_ANYRAID_TOTAL_MAP_SIZE(cvd->vdev_ashift));

	rw_enter(&va->vd_lock, RW_READER);
	uint64_t size = VDEV_ANYRAID_TOTAL_MAP_SIZE(cvd->vdev_ashift) +
	    va->vd_children[cvd->vdev_id]->van_capacity *
	    va->vd_tile_size;
	rw_exit(&va->vd_lock);
	return (size);
}

void
vdev_anyraid_expand(vdev_t *tvd, vdev_t *newvd)
{
	vdev_anyraid_t *va = tvd->vdev_tsd;
	uint64_t old_children = tvd->vdev_children - 1;

	ASSERT3U(spa_config_held(tvd->vdev_spa, SCL_ALL, RW_WRITER), ==,
	    SCL_ALL);
	vdev_anyraid_node_t **nc = kmem_alloc(tvd->vdev_children * sizeof (*nc),
	    KM_SLEEP);
	vdev_anyraid_node_t *newchild = kmem_alloc(sizeof (*newchild),
	    KM_SLEEP);
	newchild->van_id = newvd->vdev_id;
	anyraid_freelist_create(&newchild->van_freelist, 0);
	uint64_t max_size = VDEV_ANYRAID_MAX_TPD * va->vd_tile_size;
	newchild->van_capacity = (MIN(max_size, (newvd->vdev_asize -
	    VDEV_ANYRAID_TOTAL_MAP_SIZE(newvd->vdev_ashift))) /
	    va->vd_tile_size);
	rw_enter(&va->vd_lock, RW_WRITER);
	memcpy(nc, va->vd_children, old_children * sizeof (*nc));
	kmem_free(va->vd_children, old_children * sizeof (*nc));
	va->vd_children = nc;
	va->vd_children[old_children] = newchild;
	avl_add(&va->vd_children_tree, newchild);
	rw_exit(&va->vd_lock);
}

boolean_t
vdev_anyraid_mapped(vdev_t *vd, uint64_t offset, uint64_t txg)
{
	vdev_anyraid_t *va = vd->vdev_tsd;
	anyraid_tile_t search;
	search.at_tile_id = offset / va->vd_tile_size;

	rw_enter(&va->vd_lock, RW_READER);
	anyraid_tile_t *tile = avl_find(&va->vd_tile_map, &search, NULL);
	boolean_t result = tile != NULL && tile->at_synced +
	    VDEV_ANYRAID_MAP_COPIES <= txg;
	rw_exit(&va->vd_lock);

	return (result);
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
	vdev_anyraid_t *va = vd->vdev_tsd;
	ASSERT(vdev_is_anyraid(vd));

	uint64_t psize = MIN(P2ROUNDUP(max_segment, 1 << vd->vdev_ashift),
	    SPA_MAXBLOCKSIZE);

	if (start / va->vd_tile_size !=
	    (start + psize) / va->vd_tile_size) {
		psize = P2ROUNDUP(start, va->vd_tile_size) - start;
	}

	return (MIN(asize, vdev_psize_to_asize(vd, psize)));
}

static uint64_t
vdev_anyraid_asize(vdev_t *vd, uint64_t psize, uint64_t txg)
{
	vdev_anyraid_t *va = vd->vdev_tsd;
	ASSERT(vdev_is_anyraid(vd));
	if (va->vd_parity_type == VAP_MIRROR)
		return (vdev_default_asize(vd, psize, txg));

	uint64_t ashift = vd->vdev_top->vdev_ashift;
	uint64_t nparity = va->vd_nparity;
	uint64_t cols = va->vd_width;

	uint64_t asize = ((psize - 1) >> ashift) + 1;
	asize += nparity * ((asize + cols - nparity - 1) / (cols - nparity));
	asize = roundup(asize, nparity + 1) << ashift;

#ifdef ZFS_DEBUG
	uint64_t asize_new = ((psize - 1) >> ashift) + 1;
	uint64_t ncols_new = cols;
	asize_new += nparity * ((asize_new + ncols_new - nparity - 1) /
	    (ncols_new - nparity));
	asize_new = roundup(asize_new, nparity + 1) << ashift;
	VERIFY3U(asize_new, <=, asize);
#endif

	return (asize);
}

static uint64_t
vdev_anyraid_psize(vdev_t *vd, uint64_t asize, uint64_t txg)
{
	(void) txg;
	vdev_anyraid_t *va = vd->vdev_tsd;
	ASSERT(vdev_is_anyraid(vd));
	ASSERT3U(va->vd_parity_type, ==, VAP_RAIDZ);

	uint64_t ashift = vd->vdev_top->vdev_ashift;
	uint64_t nparity = va->vd_nparity;
	uint64_t cols = va->vd_width;

	ASSERT0(asize % (1 << ashift));

	uint64_t psize = (asize >> ashift);
	/*
	 * If the roundup to nparity + 1 caused us to spill into a new row, we
	 * need to ignore that row entirely (since it can't store data or
	 * parity).
	 */
	uint64_t rows = psize / cols;
	psize = psize - (rows * cols) <= nparity ? rows * cols : psize;
	/*  Subtract out parity sectors for each row storing data. */
	psize -= nparity * DIV_ROUND_UP(psize, cols);
	psize <<= ashift;

	return (psize);
}

uint64_t
vdev_anyraid_child_num_tiles(vdev_t *vd, vdev_t *cvd)
{
	vdev_anyraid_t *va = vd->vdev_tsd;
	ASSERT(vdev_is_anyraid(vd));

	uint64_t total = 0;
	rw_enter(&va->vd_lock, RW_READER);
	if (cvd != NULL) {
		vdev_anyraid_node_t *n = va->vd_children[cvd->vdev_id];
		total = anyraid_freelist_alloc(&n->van_freelist);
	} else {
		for (int i = 0; i < vd->vdev_children; i++) {
			vdev_anyraid_node_t *n = va->vd_children[i];
			total += anyraid_freelist_alloc(&n->van_freelist);
		}
	}
	rw_exit(&va->vd_lock);
	return (total);
}

uint64_t
vdev_anyraid_child_capacity(vdev_t *vd, vdev_t *cvd)
{
	vdev_anyraid_t *va = vd->vdev_tsd;
	ASSERT(vdev_is_anyraid(vd));

	uint64_t total = 0;
	rw_enter(&va->vd_lock, RW_READER);
	if (cvd != NULL) {
		vdev_anyraid_node_t *n = va->vd_children[cvd->vdev_id];
		total = n->van_capacity;
	} else {
		for (int i = 0; i < vd->vdev_children; i++) {
			vdev_anyraid_node_t *n = va->vd_children[i];
			total += n->van_capacity;
		}
	}
	rw_exit(&va->vd_lock);
	return (total);
}

vdev_ops_t vdev_anymirror_ops = {
	.vdev_op_init = vdev_anyraid_init,
	.vdev_op_fini = vdev_anyraid_fini,
	.vdev_op_open = vdev_anyraid_open,
	.vdev_op_close = vdev_anyraid_close,
	.vdev_op_psize_to_asize = vdev_anyraid_asize,
	.vdev_op_asize_to_psize = vdev_default_psize,
	.vdev_op_min_asize = vdev_anyraid_min_asize,
	.vdev_op_min_attach_size = vdev_anyraid_min_attach_size,
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
	.vdev_op_type = VDEV_TYPE_ANYMIRROR,	/* name of this vdev type */
	.vdev_op_leaf = B_FALSE			/* not a leaf vdev */
};

vdev_ops_t vdev_anyraidz_ops = {
	.vdev_op_init = vdev_anyraid_init,
	.vdev_op_fini = vdev_anyraid_fini,
	.vdev_op_open = vdev_anyraid_open,
	.vdev_op_close = vdev_anyraid_close,
	.vdev_op_psize_to_asize = vdev_anyraid_asize,
	.vdev_op_asize_to_psize = vdev_anyraid_psize,
	.vdev_op_min_asize = vdev_anyraid_min_asize,
	.vdev_op_min_attach_size = vdev_anyraid_min_attach_size,
	.vdev_op_min_alloc = NULL,
	.vdev_op_io_start = vdev_anyraid_io_start,
	.vdev_op_io_done = vdev_anyraid_io_done,
	.vdev_op_state_change = vdev_anyraid_state_change,
	.vdev_op_need_resilver = vdev_anyraid_need_resilver,
	.vdev_op_hold = NULL,
	.vdev_op_rele = NULL,
	.vdev_op_remap = NULL,
	.vdev_op_xlate = vdev_anyraid_xlate,
	.vdev_op_rebuild_asize = NULL,
	.vdev_op_metaslab_init = NULL,
	.vdev_op_config_generate = vdev_anyraid_config_generate,
	.vdev_op_nparity = vdev_anyraid_nparity,
	.vdev_op_ndisks = vdev_anyraid_ndisks,
	.vdev_op_metaslab_size = vdev_anyraid_metaslab_size,
	.vdev_op_type = VDEV_TYPE_ANYRAIDZ,	/* name of this vdev type */
	.vdev_op_leaf = B_FALSE			/* not a leaf vdev */
};


/*
 * ==========================================================================
 * TILE MOTION & REBALANCE LOGIC
 * ==========================================================================
 */

vdev_anyraid_relocate_t *
vdev_anyraid_relocate_status(vdev_t *vd)
{
	ASSERT(vdev_is_anyraid(vd));
	vdev_anyraid_t *va = vd->vdev_tsd;
	return (&va->vd_relocate);
}

static void
tasklist_write(spa_t *spa, vdev_anyraid_relocate_t *var, dmu_tx_t *tx)
{
	uint64_t obj = var->var_object;
	objset_t *mos = spa->spa_meta_objset;
	ASSERT(MUTEX_HELD(&var->var_lock));

	size_t total_count = 0, done_count = 0;
	for (vdev_anyraid_relocate_task_t *t = list_head(&var->var_done_list);
	    t; t = list_next(&var->var_done_list, t)) {
		done_count++;
		total_count++;
	}
	for (vdev_anyraid_relocate_task_t *t = list_head(&var->var_list); t;
	    t = list_next(&var->var_list, t))
		total_count++;
	size_t buflen = MIN(SPA_OLD_MAXBLOCKSIZE,
	    total_count * sizeof (relocate_task_phys_t));
	relocate_task_phys_t *buf = kmem_alloc(buflen, KM_SLEEP);

	size_t count = 0;
	size_t written = 0;
	list_t *ls[2];
	ls[0] = &var->var_done_list;
	ls[1] = &var->var_list;
	for (int i = 0; i < 2; i++) {
		for (vdev_anyraid_relocate_task_t *t = list_head(ls[i]); t;
		    t = list_next(ls[i], t)) {
			if (count == SPA_OLD_MAXBLOCKSIZE / sizeof (*buf)) {
				ASSERT3U(buflen, ==, SPA_OLD_MAXBLOCKSIZE);
				dmu_write(mos, obj, written *
				    SPA_OLD_MAXBLOCKSIZE, buflen, buf, tx,
				    DMU_READ_NO_PREFETCH);

				size_t next_buflen =  MIN(SPA_OLD_MAXBLOCKSIZE,
				    (total_count - count) * sizeof (*buf));
				if (next_buflen != buflen) {
					kmem_free(buf, buflen);
					buf = kmem_alloc(next_buflen, KM_SLEEP);
					buflen = next_buflen;
				}
				count = 0;
			}

			ASSERT3U(count * sizeof (*buf), <, buflen);
			relocate_task_phys_t *rtp = buf + count++;
			rtp->rtp_source_disk = t->vart_source_disk;
			rtp->rtp_dest_disk = t->vart_dest_disk;
			rtp->rtp_source_idx = t->vart_source_idx;
			rtp->rtp_dest_idx = t->vart_dest_idx;
			rtp->rtp_tile = t->vart_tile;
			rtp->rtp_task = t->vart_task;
			rtp->rtp_pad2 = 0;
		}
	}
	dmu_write(mos, obj, written * SPA_OLD_MAXBLOCKSIZE, buflen, buf, tx,
	    DMU_READ_NO_PREFETCH);
	kmem_free(buf, buflen);

	dmu_buf_t *dbp;
	VERIFY0(dmu_bonus_hold(mos, obj, FTAG, &dbp));
	ASSERT3U(dbp->db_size, >=, sizeof (relocate_phys_t));
	relocate_phys_t *rp = dbp->db_data;
	dmu_buf_will_dirty(dbp, tx);
	rp->rp_total = total_count;
	rp->rp_done = done_count;
	dmu_buf_rele(dbp, FTAG);
}

static int
tasklist_read(vdev_t *vd)
{
	spa_t *spa = vd->vdev_spa;
	objset_t *mos = spa->spa_meta_objset;
	vdev_anyraid_t *va = vd->vdev_tsd;
	vdev_anyraid_relocate_t *var = &va->vd_relocate;
	ASSERT3P(spa->spa_anyraid_relocate, ==, var);

	uint64_t object;
	int error = zap_lookup(mos, DMU_POOL_DIRECTORY_OBJECT,
	    DMU_POOL_RELOCATE_OBJ, sizeof (uint64_t), 1, &object);
	if (error == ENOENT) {
		if (var->var_state != ARS_CONTRACTING)
			return (ENOENT);
		goto disable_tail;
	}
	if (error != 0)
		return (error);

	dmu_buf_t *dbp;
	if ((error = dmu_bonus_hold(mos, object, FTAG, &dbp)) != 0)
		return (error);

	relocate_phys_t *rpp = dbp->db_data;
	size_t done = rpp->rp_done;
	size_t total = rpp->rp_total;
	dmu_buf_rele(dbp, FTAG);

	mutex_enter(&var->var_lock);
	ASSERT0(var->var_object);
	var->var_object = object;
	mutex_exit(&var->var_lock);
	size_t buflen = MIN(SPA_OLD_MAXBLOCKSIZE,
	    total * sizeof (relocate_task_phys_t));
	relocate_task_phys_t *buf = kmem_alloc(buflen, KM_SLEEP);
	list_t *l = &var->var_list;
	size_t i;
	for (i = 0; i < total; i++) {
		size_t idx = i % (SPA_OLD_MAXBLOCKSIZE / sizeof (*buf));
		if (idx == 0) {
			size_t next_buflen = MIN(SPA_OLD_MAXBLOCKSIZE,
			    (total - i) * sizeof (relocate_task_phys_t));
			if (next_buflen != buflen) {
				kmem_free(buf, buflen);
				buflen = next_buflen;
				buf = kmem_alloc(buflen, KM_SLEEP);
			}
			error = dmu_read(mos, var->var_object,
			    i * sizeof (*buf), buflen, buf, DMU_READ_PREFETCH);
			if (error) {
				// The task lists will be freed when we fini vd
				kmem_free(buf, buflen);
				return (error);
			}
		}
		if (i == done && list_head(&var->var_list)) {
			l = &var->var_list;
			vdev_anyraid_relocate_task_t *vart =
			    list_remove_head(l);
			ASSERT(vart);
			kmem_free(vart, sizeof (*vart));
		}
		vdev_anyraid_relocate_task_t *vart =
		    kmem_alloc(sizeof (*vart), KM_SLEEP);
		relocate_task_phys_t *rtp = buf + idx;
		vart->vart_source_disk = rtp->rtp_source_disk;
		vart->vart_dest_disk = rtp->rtp_dest_disk;
		vart->vart_source_idx = rtp->rtp_source_idx;
		vart->vart_dest_idx = rtp->rtp_dest_idx;
		vart->vart_tile = rtp->rtp_tile;
		vart->vart_task = rtp->rtp_task;

		/*
		 * We need to disable metaslabs here; any metaslabs that are
		 * after the first done task but before or containing the
		 * resume offset.
		 */
		if (i >= done && vart->vart_task <= var->var_task) {
			uint64_t ms_per_tile = va->vd_tile_size >>
			    vd->vdev_ms_shift;
			uint64_t start = vart->vart_tile * ms_per_tile;
			uint64_t end = start + ms_per_tile;
			for (uint64_t m = start; m < end; m++) {
				ASSERT(vd->vdev_ms);
				metaslab_t *ms = vd->vdev_ms[m];
				if (vart->vart_task == var->var_task) {
					zfs_range_seg64_t log, phys, rem;
					log.rs_start = ms->ms_start;
					log.rs_end = ms->ms_start + ms->ms_size;
					vdev_xlate(vd->vdev_child[
					    vart->vart_source_disk], &log,
					    &phys, &rem);
					if (phys.rs_start == phys.rs_end ||
					    phys.rs_start > var->var_offset)
						continue;
				}
				metaslab_disable_nowait(ms);
				vart->vart_dis_ms++;
			}
		}

		rw_enter(&va->vd_lock, RW_WRITER);
		anyraid_freelist_t *af =
		    &va->vd_children[vart->vart_source_disk]->van_freelist;
		boolean_t sourcefree = anyraid_freelist_isfree(af,
		    vart->vart_source_idx);
		if (sourcefree)
			anyraid_freelist_remove(af, vart->vart_source_idx);

		af = &va->vd_children[vart->vart_dest_disk]->van_freelist;
		boolean_t destfree = anyraid_freelist_isfree(af,
		    vart->vart_dest_idx);
		if (destfree)
			anyraid_freelist_remove(af, vart->vart_dest_idx);

		// Either one or the other should be in the mapping already.
		ASSERT3U(sourcefree, !=, destfree);
		rw_exit(&va->vd_lock);

		list_insert_tail(l, vart);
	}
	if (i == done) {
		vdev_anyraid_relocate_task_t *vart =
		    list_remove_head(&var->var_list);
		ASSERT(vart);
		kmem_free(vart, sizeof (*vart));
	}
	kmem_free(buf, buflen);

disable_tail:
	uint64_t *num_tiles = kmem_zalloc(sizeof (*num_tiles) *
	    vd->vdev_children, KM_SLEEP);
	rw_enter(&va->vd_lock, RW_READER);
	for (int c = 0; c < vd->vdev_children; c++) {
		vdev_anyraid_node_t *van = va->vd_children[c];
		if (va->vd_contracting_leaf == c) {
			num_tiles[c] = 0;
			continue;
		}
		num_tiles[c] = van->van_capacity;
	}
	uint64_t updated_asize = calculate_asize(vd, num_tiles);
	rw_exit(&va->vd_lock);
	kmem_free(num_tiles, vd->vdev_children * sizeof (*num_tiles));
	var->var_nonalloc = vd->vdev_asize - updated_asize;
	vdev_update_nonallocating_space(vd, var->var_nonalloc, B_TRUE);
	if (va->vd_contracting_leaf != -1) {
		uint64_t start = MIN(vd->vdev_ms_count,
		    updated_asize >> vd->vdev_ms_shift);
		uint64_t end = vd->vdev_ms_count;
		for (uint64_t m = start; m < end; m++) {
			metaslab_t *ms = vd->vdev_ms[m];
			metaslab_disable_nowait(ms);
		}
	}
	return (0);
}

static void
anyraid_relocate_sync(void *arg, dmu_tx_t *tx)
{
	spa_t *spa = arg;
	int txgoff = dmu_tx_get_txg(tx) & TXG_MASK;
	vdev_anyraid_relocate_t *var = spa->spa_anyraid_relocate;

	mutex_enter(&var->var_lock);
	/*
	 * Ensure there are no i/os to the range that is being committed.
	 */
	uint64_t old_offset = var->var_synced_offset;
	uint64_t old_task = var->var_synced_task;

	ASSERT3U(var->var_task_pertxg[txgoff], >=, old_task);
	ASSERT(var->var_task_pertxg[txgoff] > old_task ||
	    var->var_offset_pertxg[txgoff] >= old_offset);

	uint64_t new_offset =
	    MIN(var->var_offset_pertxg[txgoff], var->var_failed_offset);
	uint64_t new_task =
	    MIN(var->var_task_pertxg[txgoff], var->var_failed_task);
	/*
	 * We should not have committed anything that failed.
	 */
	mutex_exit(&var->var_lock);

	vdev_t *vd = vdev_lookup_top(spa, var->var_vd);
	vdev_anyraid_t *va = vd->vdev_tsd;

	zfs_locked_range_t *lr = zfs_rangelock_enter(&va->vd_rangelock,
	    old_offset, new_offset - old_offset,
	    RL_WRITER);

	var->var_synced_offset = new_offset;
	var->var_synced_task = new_task;
	var->var_offset_pertxg[txgoff] = 0;
	var->var_task_pertxg[txgoff] = 0;
	zfs_rangelock_exit(lr);

	mutex_enter(&var->var_lock);
	var->var_bytes_copied += var->var_bytes_copied_pertxg[txgoff];
	var->var_bytes_copied_pertxg[txgoff] = 0;

	tasklist_write(spa, var, tx);
	mutex_exit(&var->var_lock);
}

static void
anyraid_scrub_done(spa_t *spa, dmu_tx_t *tx, void *arg)
{
	struct anyraid_done_arg *ada = arg;
	vdev_anyraid_t *va = ada->vd->vdev_tsd;
	vdev_anyraid_relocate_t *var = &va->vd_relocate;
	rw_enter(&va->vd_lock, RW_WRITER);
	boolean_t noop = (list_head(&var->var_done_list) == NULL);
	for (vdev_anyraid_relocate_task_t *task =
	    list_head(&var->var_done_list); task;
	    task = list_head(&var->var_done_list)) {
		anyraid_freelist_add(
		    &va->vd_children[task->vart_source_disk]->van_freelist,
		    task->vart_source_idx);
		list_remove(&var->var_done_list, task);
		kmem_free(task, sizeof (*task));
	}
	/*
	 * Usually there aren't any tasks left in the list, but this can happen
	 * if we finish our relocate in just the right way, and then export the
	 * pool and reimport during the scrub.
	 */
	for (vdev_anyraid_relocate_task_t *task =
	    list_head(&var->var_list); task;
	    task = list_head(&var->var_list)) {
		anyraid_freelist_add(
		    &va->vd_children[task->vart_source_disk]->van_freelist,
		    task->vart_source_idx);
		list_remove(&var->var_list, task);
		kmem_free(task, sizeof (*task));
	}

	objset_t *mos = spa->spa_meta_objset;

	uint64_t object;
	int res = zap_lookup(mos, DMU_POOL_DIRECTORY_OBJECT,
	    DMU_POOL_RELOCATE_OBJ, sizeof (uint64_t), 1, &object);
	if (res == 0) {
		ASSERT3U(object, ==, var->var_object);
		VERIFY0(dmu_object_free(mos, var->var_object, tx));
		VERIFY0(zap_remove(mos, DMU_POOL_DIRECTORY_OBJECT,
		    DMU_POOL_RELOCATE_OBJ, tx));
	} else {
		ASSERT(noop);
	}

	boolean_t contracting = va->vd_contracting_leaf != -1;
	if (!contracting) {
		vdev_update_nonallocating_space(ada->vd, var->var_nonalloc,
		    B_FALSE);
		var->var_state = ARS_FINISHED;
		var->var_synced_offset = var->var_offset = 0;
		var->var_synced_task = var->var_task = 0;
	} else {
		spa_async_request(spa, SPA_ASYNC_CONTRACTION_DONE);
		va->vd_relocate.var_state = ARS_CONTRACTING;
	}

	rw_exit(&va->vd_lock);

	spa_config_enter(spa, SCL_STATE_ALL, FTAG, RW_WRITER);
	if (!contracting) {
		ada->vd->vdev_expanding = B_TRUE;
		vdev_reopen(ada->vd);
		ada->vd->vdev_spa->spa_anyraid_relocate = NULL;
	}
	
	spa->spa_ccw_fail_time = 0;
	spa_async_request(spa, SPA_ASYNC_CONFIG_UPDATE);
	spa_config_exit(spa, SCL_STATE_ALL, FTAG);
	vdev_config_dirty(ada->vd);
	kmem_free(ada, sizeof (*ada));
}

static void
anyraid_relocate_complete_sync(void *arg, dmu_tx_t *tx)
{
	spa_t *spa = arg;
	vdev_anyraid_relocate_t *var = spa->spa_anyraid_relocate;
	vdev_t *vd = vdev_lookup_top(spa, var->var_vd);
	vdev_anyraid_t *va = vd->vdev_tsd;

	for (int i = 0; i < TXG_SIZE; i++) {
		VERIFY0(var->var_offset_pertxg[i]);
	}

	rw_enter(&va->vd_lock, RW_WRITER);
	/*
	 * will get written (based on vd_expand_txgs). TODO
	 */
	vdev_config_dirty(vd);

	var->var_end_time = gethrestime_sec();

	spa_history_log_internal(spa, "anyraid relocate completed",  tx,
	    "%s vdev %llu", spa_name(spa),
	    (unsigned long long)vd->vdev_id);

	rw_exit(&va->vd_lock);

	spa_async_request(spa, SPA_ASYNC_INITIALIZE_RESTART);
	spa_async_request(spa, SPA_ASYNC_TRIM_RESTART);
	spa_async_request(spa, SPA_ASYNC_AUTOTRIM_RESTART);

	spa_notify_waiters(spa);

	var->var_state = ARS_SCRUBBING;
	/*
	 * While we're in syncing context take the opportunity to
	 * setup a scrub. All the data has been sucessfully copied
	 * but we have not validated any checksums.
	 */
	struct anyraid_done_arg *ada = kmem_alloc(sizeof (*ada), KM_SLEEP);
	ada->vd = vd;
	setup_sync_arg_t setup_sync_arg = {
		.func = POOL_SCAN_SCRUB,
		.txgstart = 0,
		.txgend = 0,
		.done = anyraid_scrub_done,
		.done_arg = ada,
	};
	if (zfs_scrub_after_relocate &&
	    dsl_scan_setup_check(&setup_sync_arg.func, tx) == 0 &&
	    list_head(&var->var_done_list) != NULL) {
		dsl_scan_setup_sync(&setup_sync_arg, tx);
	} else {
		anyraid_scrub_done(spa, tx, ada);
	}
}

dsl_scan_done_func_t *
anyraid_setup_scan_done(spa_t *spa, uint64_t vd_id, void **arg)
{
	struct anyraid_done_arg *ada = kmem_alloc(sizeof (*ada), KM_SLEEP);

	spa_config_enter(spa, SCL_STATE, FTAG, RW_READER);
	ada->vd = vdev_lookup_top(spa, vd_id);
	spa_config_exit(spa, SCL_STATE, FTAG);
	*arg = ada;
	return (anyraid_scrub_done);
}


struct rebal_node {
	avl_node_t node1;
	avl_node_t node2;
	int cvd;
	int free; // number of free tiles
	int alloc; // number of allocated tiles
	int64_t *arr;
};

static int
rebal_cmp_free(const void *a, const void *b)
{
	const struct rebal_node *ra = a;
	const struct rebal_node *rb = b;
	int cmp = TREE_CMP(ra->free, rb->free);
	if (likely(cmp != 0))
		return (cmp);
	return (TREE_CMP(rb->cvd, ra->cvd));
}

static int
rebal_cmp_alloc(const void *a, const void *b)
{
	const struct rebal_node *ra = a;
	const struct rebal_node *rb = b;
	int cmp = TREE_CMP(ra->alloc, rb->alloc);
	if (likely(cmp != 0))
		return (cmp);
	return (TREE_CMP(rb->cvd, ra->cvd));
}

static void
populate_child_array(vdev_anyraid_t *va, int child, int64_t *arr, uint32_t cap)
{
	for (anyraid_tile_t *tile = avl_first(&va->vd_tile_map);
	    tile; tile = AVL_NEXT(&va->vd_tile_map, tile)) {
		for (anyraid_tile_node_t *atn = list_head(&tile->at_list);
		    atn; atn = list_next(&tile->at_list, atn)) {
			if (atn->atn_disk == child) {
				ASSERT3U(atn->atn_tile_idx, <, cap);
				arr[atn->atn_tile_idx] = tile->at_tile_id;
			}
		}
	}
}

static void
create_reloc_task(vdev_anyraid_t *va, struct rebal_node *donor, uint16_t offset,
    struct rebal_node *receiver, uint32_t *tid)
{
	vdev_anyraid_node_t *rvan = va->vd_children[receiver->cvd];
	vdev_anyraid_relocate_task_t *task =
	    kmem_zalloc(sizeof (*task), KM_SLEEP);
	task->vart_source_disk = (uint8_t)donor->cvd;
	task->vart_dest_disk = (uint8_t)receiver->cvd;
		task->vart_source_idx = offset;
	ASSERT(rvan->van_capacity -
	    anyraid_freelist_alloc(&rvan->van_freelist));
	task->vart_dest_idx = anyraid_freelist_pop(
	    &rvan->van_freelist);
	task->vart_tile = donor->arr[offset];
	task->vart_task = (*tid)++;
		list_insert_tail(&va->vd_relocate.var_list, task);
	receiver->arr[task->vart_dest_idx] = donor->arr[offset];
	donor->arr[offset] = -1LL;
}

static boolean_t
reloc_try_move_one(vdev_anyraid_t *va, struct rebal_node *donor,
    uint16_t offset, struct rebal_node *receiver, uint32_t *tid)
{
	vdev_anyraid_node_t *rvan = va->vd_children[receiver->cvd];

	boolean_t found = B_FALSE;
	for (int j = 0; j < rvan->van_freelist.af_next_off;
	    j++) {
		/*
		 * cause the total number of allocatable tiles to drop;
		 * if so, we have to skip it.
		 */
		if (donor->arr[offset] == receiver->arr[j]) {
			found = B_TRUE;
			break;
		}
	}
	if (found)
		return (B_FALSE);

	create_reloc_task(va, donor, offset, receiver, tid);
	return (B_TRUE);
}

static boolean_t
rebal_try_move(vdev_anyraid_t *va, struct rebal_node *donor,
    struct rebal_node *receiver, uint32_t *tid)
{
	vdev_anyraid_node_t *dvan = va->vd_children[donor->cvd];

	for (int i = 0; i < dvan->van_freelist.af_next_off; i++) {
		ASSERT3U(dvan->van_freelist.af_next_off, <=,
		    dvan->van_capacity);
		if (donor->arr[i] == -1LL)
			continue;
		if (reloc_try_move_one(va, donor, i, receiver, tid))
			return (B_TRUE);
	}
	return (B_FALSE);
}

void
vdev_anyraid_setup_rebalance(vdev_t *vd, dmu_tx_t *tx)
{
	(void) tx;
	ASSERT(vdev_is_anyraid(vd));
	vdev_anyraid_t *va = vd->vdev_tsd;

	vdev_config_dirty(vd);

	vdev_anyraid_relocate_t *var = &va->vd_relocate;
	var->var_start_time = gethrestime_sec();
	var->var_state = ARS_SCANNING;
	var->var_vd = vd->vdev_id;
	var->var_failed_offset = var->var_failed_task = UINT64_MAX;
	var->var_offset = 0;

	mutex_enter(&var->var_lock);
	vd->vdev_spa->spa_anyraid_relocate = var;

	rw_enter(&va->vd_lock, RW_WRITER);
	avl_tree_t ft;
	avl_create(&ft, rebal_cmp_free, sizeof (struct rebal_node),
	    offsetof(struct rebal_node, node1));
	avl_tree_t at;
	avl_create(&at, rebal_cmp_alloc, sizeof (struct rebal_node),
	    offsetof(struct rebal_node, node2));

	uint64_t *num_tiles = kmem_zalloc(vd->vdev_children *
	    sizeof (*num_tiles), KM_SLEEP);
	for (int c = 0; c < vd->vdev_children; c++)
		num_tiles[c] = va->vd_children[c]->van_capacity;

	for (int i = 0; i < vd->vdev_children; i++) {
		struct rebal_node *rn = kmem_zalloc(sizeof (*rn), KM_SLEEP);
		rn->cvd = i;
		vdev_anyraid_node_t *n = va->vd_children[i];
		uint32_t cap = n->van_capacity;
		rn->alloc = anyraid_freelist_alloc(&n->van_freelist);
		rn->free = cap - rn->alloc;
		rn->arr = kmem_alloc(sizeof (*rn->arr) * cap, KM_SLEEP);
		memset(rn->arr, -1, sizeof (*rn->arr) * cap);
		populate_child_array(va, i, rn->arr, cap);
		avl_add(&ft, rn);
		avl_add(&at, rn);
	}
	uint32_t tid = 0;
	for (;;) {
		struct rebal_node *donor = avl_last(&at);
		boolean_t moved = B_FALSE;
		while (donor && donor->alloc > 0) {
			struct rebal_node *prev_donor = AVL_PREV(&at, donor);
			struct rebal_node *receiver = avl_last(&ft);
			while (receiver && receiver->free > 0) {
				struct rebal_node *prev_rec =
				    AVL_PREV(&ft, receiver);
				if (receiver->free <= donor->free + 1)
					break;
				moved = rebal_try_move(va,
				    donor, receiver, &tid);
				if (!moved) {
					receiver = prev_rec;
					continue;
				}
				avl_remove(&ft, receiver);
				avl_remove(&at, receiver);
				receiver->free--;
				receiver->alloc++;
				avl_add(&ft, receiver);
				avl_add(&at, receiver);
				num_tiles[receiver->cvd]--;
				break;
			}
			if (moved)
				break;
			donor = prev_donor;
		}
		if (donor == NULL || donor->alloc == 0)
			break;
	}

	/*
	 * It's already balanced; clean up the state and report success
	 * immediately.
	 */
	if (tid == 0) {
		rw_exit(&va->vd_lock);
		kmem_free(num_tiles, vd->vdev_children * sizeof (*num_tiles));

		struct rebal_node *node;
		void *cookie = NULL;
		while ((node = avl_destroy_nodes(&ft, &cookie)) != NULL)
			;
		avl_destroy(&ft);
		cookie = NULL;
		while ((node = avl_destroy_nodes(&at, &cookie)) != NULL) {
			kmem_free(node->arr, sizeof (*node->arr) *
			    (node->free + node->alloc));
			kmem_free(node, sizeof (*node));
		}
		avl_destroy(&at);

		var->var_nonalloc = 0;
		var->var_state = ARS_FINISHED;
		mutex_exit(&var->var_lock);
		anyraid_relocate_complete_sync(vd->vdev_spa, tx);
		return;
	}

	uint64_t updated_asize = calculate_asize(vd, num_tiles);
	rw_exit(&va->vd_lock);
	kmem_free(num_tiles, vd->vdev_children * sizeof (*num_tiles));
	ASSERT3U(vd->vdev_asize, >=, updated_asize);
	var->var_nonalloc = vd->vdev_asize - updated_asize;
	vdev_update_nonallocating_space(vd, var->var_nonalloc, B_TRUE);

	objset_t *mos = vd->vdev_spa->spa_meta_objset;
	var->var_object = dmu_object_alloc(mos, DMU_OTN_UINT32_METADATA,
	    SPA_OLD_MAXBLOCKSIZE, DMU_OTN_UINT64_METADATA,
	    sizeof (relocate_phys_t), tx);
	VERIFY0(zap_add(mos, DMU_POOL_DIRECTORY_OBJECT, DMU_POOL_RELOCATE_OBJ,
	    sizeof (uint64_t), 1, &var->var_object, tx));

	tasklist_write(vd->vdev_spa, var, tx);
	mutex_exit(&var->var_lock);

	struct rebal_node *node;
	void *cookie = NULL;
	while ((node = avl_destroy_nodes(&ft, &cookie)) != NULL)
		;
	avl_destroy(&ft);
	cookie = NULL;
	while ((node = avl_destroy_nodes(&at, &cookie)) != NULL) {
		kmem_free(node->arr, sizeof (*node->arr) *
		    (node->free + node->alloc));
		kmem_free(node, sizeof (*node));
	}
	avl_destroy(&at);
	zthr_wakeup(vd->vdev_spa->spa_anyraid_relocate_zthr);
}

static boolean_t
spa_anyraid_relocate_thread_check(void *arg, zthr_t *zthr)
{
	(void) zthr;
	spa_t *spa = arg;
	vdev_anyraid_relocate_t *var = spa->spa_anyraid_relocate;

	return (var != NULL && var->var_state != ARS_SCRUBBING &&
	    !var->var_waiting_for_resilver);
}

/*
 * Write of the new location on one child is done.  Once all of them are done
 * we can unlock and free everything.
 */
static void
anyraid_relocate_write_done(zio_t *zio)
{
	anyraid_move_arg_t *ama = zio->io_private;
	vdev_anyraid_relocate_t *var = ama->ama_var;

	abd_free(zio->io_abd);

	mutex_enter(&var->var_lock);
	if (zio->io_error != 0) {
		/* Force a relocate pause on errors */
		var->var_failed_offset =
		    MIN(var->var_failed_offset, ama->ama_lr->lr_offset);
		var->var_failed_task = MIN(var->var_failed_task, ama->ama_tid);
	}
	ASSERT3U(var->var_outstanding_bytes, >=, zio->io_size);
	var->var_outstanding_bytes -= zio->io_size;
	if (ama->ama_lr->lr_offset + ama->ama_lr->lr_length <
	    var->var_failed_offset) {
		var->var_bytes_copied_pertxg[ama->ama_txg & TXG_MASK] +=
		    zio->io_size;
	}
	cv_signal(&var->var_cv);
	mutex_exit(&var->var_lock);

	spa_config_exit(zio->io_spa, SCL_STATE, zio->io_spa);
	zfs_rangelock_exit(ama->ama_lr);
	kmem_free(ama, sizeof (*ama));
}

/*
 * Read of the old location on one child is done.  Once all of them are done
 * writes should have all the data and we can issue them.
 */
static void
anyraid_relocate_read_done(zio_t *zio)
{
	anyraid_move_arg_t *ama = zio->io_private;
	vdev_anyraid_relocate_t *var = ama->ama_var;

	/*
	 * If the read failed, or if it was done on a vdev that is not fully
	 * healthy (e.g. a child that has a resilver in progress), we may not
	 * have the correct data.  Note that it's OK if the write proceeds.
	 * It may write garbage but the location is otherwise unused and we
	 * will retry later due to vre_failed_offset.
	 */
	if (zio->io_error != 0 || !vdev_dtl_empty(zio->io_vd, DTL_MISSING)) {
		zfs_dbgmsg("relocate read failed off=%llu size=%llu txg=%llu "
		    "err=%u partial_dtl_empty=%u missing_dtl_empty=%u",
		    (long long)ama->ama_lr->lr_offset,
		    (long long)ama->ama_lr->lr_length,
		    (long long)ama->ama_txg,
		    zio->io_error,
		    vdev_dtl_empty(zio->io_vd, DTL_PARTIAL),
		    vdev_dtl_empty(zio->io_vd, DTL_MISSING));
		mutex_enter(&var->var_lock);
		/* Force a relocate pause on errors */
		var->var_failed_offset =
		    MIN(var->var_failed_offset, ama->ama_lr->lr_offset);
		mutex_exit(&var->var_lock);
	}
	zio_nowait(ama->ama_zio);
}

static void
anyraid_relocate_record_progress(vdev_anyraid_relocate_t *var,
    uint64_t offset, uint64_t task, dmu_tx_t *tx)
{
	int txgoff = dmu_tx_get_txg(tx) & TXG_MASK;
	spa_t *spa = dmu_tx_pool(tx)->dp_spa;

	if (offset == 0)
		return;

	mutex_enter(&var->var_lock);
	var->var_offset = offset;

	if (var->var_offset_pertxg[txgoff] == 0) {
		dsl_sync_task_nowait(dmu_tx_pool(tx), anyraid_relocate_sync,
		    spa, tx);
	}
	var->var_offset_pertxg[txgoff] = offset;
	var->var_task_pertxg[txgoff] = task;
	mutex_exit(&var->var_lock);
}

static boolean_t
anyraid_relocate_impl(vdev_t *vd, vdev_anyraid_relocate_t *var,
    zfs_range_tree_t *rt, dmu_tx_t *tx)
{
	spa_t *spa = vd->vdev_spa;
	uint_t ashift = vd->vdev_top->vdev_ashift;
	vdev_anyraid_relocate_task_t *vart = list_head(&var->var_list);
	vdev_anyraid_t *va = vd->vdev_tsd;

	zfs_range_seg_t *rs = zfs_range_tree_first(rt);
	ASSERT(rs);
	uint64_t offset = zfs_rs_get_start(rs, rt);
	ASSERT(IS_P2ALIGNED(offset, 1 << ashift));
	uint64_t size = zfs_rs_get_end(rs, rt) - offset;
	ASSERT3U(size, >=, 1 << ashift);
	ASSERT(IS_P2ALIGNED(size, 1 << ashift));

	size = MIN(size, anyraid_relocate_max_move_bytes);
	size = MAX(size, 1 << ashift);

	zfs_range_tree_remove(rt, offset, size);

	anyraid_move_arg_t *ama = kmem_zalloc(sizeof (*ama), KM_SLEEP);
	ama->ama_var = var;
	ama->ama_lr = zfs_rangelock_enter(&va->vd_rangelock,
	    offset, size, RL_WRITER);
	ama->ama_txg = dmu_tx_get_txg(tx);
	ama->ama_size = size;
	ama->ama_tid = vart->vart_task;

	anyraid_relocate_record_progress(var, offset + size, vart->vart_task,
	    tx);

	/*
	 * SCL_STATE will be released when the read and write are done,
	 * by raidz_reflow_write_done().
	 */
	spa_config_enter(spa, SCL_STATE, spa, RW_READER);

	mutex_enter(&var->var_lock);
	var->var_outstanding_bytes += size;
	mutex_exit(&var->var_lock);

	/* Allocate ABD and ZIO for each child we write. */
	int txgoff = dmu_tx_get_txg(tx) & TXG_MASK;
	zio_t *pio = spa->spa_txg_zio[txgoff];
	abd_t *abd = abd_alloc_for_io(size, B_FALSE);
	vdev_t *source_vd = vd->vdev_child[vart->vart_source_disk];
	vdev_t *dest_vd = vd->vdev_child[vart->vart_dest_disk];
	uint64_t source_header =
	    VDEV_ANYRAID_START_OFFSET(source_vd->vdev_ashift);
	uint64_t dest_header =
	    VDEV_ANYRAID_START_OFFSET(dest_vd->vdev_ashift);
	uint64_t dest_off = dest_header +
	    vart->vart_dest_idx * va->vd_tile_size +
	    ((offset - source_header) % va->vd_tile_size);
	ama->ama_zio = zio_vdev_child_io(pio, NULL,
	    dest_vd, dest_off, abd, size,
	    ZIO_TYPE_WRITE, ZIO_PRIORITY_REMOVAL,
	    ZIO_FLAG_CANFAIL, anyraid_relocate_write_done, ama);

	zio_nowait(zio_vdev_child_io(pio, NULL,
	    vd->vdev_child[vart->vart_source_disk],
	    offset, abd, size, ZIO_TYPE_READ, ZIO_PRIORITY_REMOVAL,
	    ZIO_FLAG_CANFAIL, anyraid_relocate_read_done, ama));
	return (zfs_range_tree_numsegs(rt) == 0);
}

struct physify_arg {
	zfs_range_tree_t *rt;
	vdev_t *vd;
};

static void
anyraid_rt_physify(void *arg, uint64_t start, uint64_t size)
{
	struct physify_arg *pa = (struct physify_arg *)arg;
	zfs_range_tree_t *rt = pa->rt;
	vdev_t *vd = pa->vd;
	ASSERT3U(size, >, 0);

	zfs_range_seg64_t logical, physical, remain;
	logical.rs_start = start;
	logical.rs_end = start + size;
	vdev_xlate(vd, &logical, &physical, &remain);
	ASSERT3U(remain.rs_end, ==, remain.rs_start);
	/*
	 * This can happen if the tile has actually already been moved,
	 * but the synced state hasn't caught up.
	 */
	if (physical.rs_end == physical.rs_start)
		return;
	ASSERT(physical.rs_end - physical.rs_start);
	zfs_range_tree_add(rt, physical.rs_start,
	    physical.rs_end - physical.rs_start);
}

static vdev_t *
process_one_metaslab(spa_t *spa, metaslab_t *msp, vdev_t *pvd,
    vdev_anyraid_t *va, vdev_anyraid_relocate_task_t *vart, zthr_t *zthr)
{
	vdev_anyraid_relocate_t *var = spa->spa_anyraid_relocate;
	vdev_t *source_vd = pvd->vdev_child[vart->vart_source_disk];
	metaslab_disable_nowait(msp);
	mutex_enter(&msp->ms_lock);

	/*
	 * The metaslab may be newly created (for the expanded space), in which
	 * case its trees won't exist yet, so we need to bail out early.
	 */
	if (msp->ms_new) {
		mutex_exit(&msp->ms_lock);
		metaslab_enable(msp, B_FALSE, B_FALSE);
		if (vart->vart_dis_ms > 0) {
			vart->vart_dis_ms--;
			metaslab_enable(msp, B_FALSE, B_FALSE);
		}
		return (pvd);
	}

	VERIFY0(metaslab_load(msp));

	/*
	 * We want to copy everything except the free (allocatable) space.
	 * Note that there may be a little bit more free space (e.g. in
	 * ms_defer), and it's fine to copy that too.
	 */
	uint64_t shift, start;
	zfs_range_seg_type_t type = metaslab_calculate_range_tree_type(pvd,
	    msp, &start, &shift);
	zfs_range_tree_t *rt = zfs_range_tree_create_flags(NULL, type, NULL,
	    start, shift, ZFS_RT_F_DYN_NAME, metaslab_rt_name(msp->ms_group,
	    msp, "spa_anyraid_relocate_thread:rt"));
	zfs_range_tree_add(rt, msp->ms_start, msp->ms_size);
	zfs_range_tree_walk(msp->ms_allocatable, zfs_range_tree_remove, rt);
	mutex_exit(&msp->ms_lock);

	/*
	 * Now we need to convert the logical offsets of the metaslab into the
	 * physical offsets on disk. We also skip any extents that don't map to
	 * to the source tile.
	 */
	zfs_range_tree_t *phys = zfs_range_tree_create_flags(NULL,
	    ZFS_RANGE_SEG64, NULL, 0, pvd->vdev_ashift, ZFS_RT_F_DYN_NAME,
	    metaslab_rt_name(msp->ms_group, msp,
	    "spa_anyraid_relocate_thread2:rt"));
	struct physify_arg pa;
	pa.rt = phys;
	pa.vd = source_vd;
	zfs_range_tree_walk(rt, anyraid_rt_physify, &pa);
	zfs_range_tree_vacate(rt, NULL, NULL);
	zfs_range_tree_destroy(rt);

	/*
	 * When we are resuming from a paused relocate (i.e.
	 * when importing a pool with a relocate in progress),
	 * discard any state that we have already processed.
	 */
	if (vart->vart_task <= var->var_task) {
		uint64_t end = vart->vart_task == var->var_task ?
		    var->var_offset : P2ALIGN_TYPED(UINT64_MAX,
		    (1 << pvd->vdev_ashift), uint64_t);
		zfs_range_tree_clear(phys, 0, end);
	}

	while (!zthr_iscancelled(zthr) && !zfs_range_tree_is_empty(phys) &&
	    var->var_failed_offset == UINT64_MAX) {
		/*
		 * We need to periodically drop the config lock so that writers
		 * can get in.  Additionally, we can't wait for a txg to sync
		 * while holding a config lock (since a waiting writer could
		 * cause a 3-way deadlock with the sync thread, which also gets
		 * a config lock for reader).  So we can't hold the config lock
		 * while calling dmu_tx_assign().
		 */
		spa_config_exit(spa, SCL_CONFIG, FTAG);
		rw_exit(&va->vd_lock);

		/*
		 * If requested, pause the reflow when the amount specified by
		 * anyraid_relocate_max_bytes_pause is reached.
		 *
		 * This pause is only used during testing or debugging.
		 */
		while (anyraid_relocate_max_bytes_pause != 0 &&
		    anyraid_relocate_max_bytes_pause <= var->var_bytes_copied &&
		    !zthr_iscancelled(zthr)) {
			delay(hz);
		}

		mutex_enter(&var->var_lock);
		while (var->var_outstanding_bytes >
		    anyraid_relocate_max_move_bytes) {
			cv_wait(&var->var_cv, &var->var_lock);
		}
		mutex_exit(&var->var_lock);

		dmu_tx_t *tx = dmu_tx_create_dd(spa_get_dsl(spa)->dp_mos_dir);

		VERIFY0(dmu_tx_assign(tx, DMU_TX_WAIT | DMU_TX_SUSPEND));
		uint64_t txg = dmu_tx_get_txg(tx);

		/*
		 * Reacquire the vdev_config lock. Theoretically, the vdev_t
		 * that we're working on may have changed.
		 */
		rw_enter(&va->vd_lock, RW_READER);
		spa_config_enter(spa, SCL_CONFIG, FTAG, RW_READER);
		pvd = vdev_lookup_top(spa, var->var_vd);

		boolean_t needsync = anyraid_relocate_impl(pvd, var, phys, tx);

		dmu_tx_commit(tx);

		if (needsync) {
			spa_config_exit(spa, SCL_CONFIG, FTAG);
			rw_exit(&va->vd_lock);
			txg_wait_synced(spa->spa_dsl_pool, txg);
			rw_enter(&va->vd_lock, RW_READER);
			spa_config_enter(spa, SCL_CONFIG, FTAG,
			    RW_READER);
		}
	}

	spa_config_exit(spa, SCL_CONFIG, FTAG);

	metaslab_enable(msp, B_FALSE, B_FALSE);
	if (vart->vart_dis_ms > 0) {
		vart->vart_dis_ms--;
		metaslab_enable(msp, B_FALSE, B_FALSE);
	}
	zfs_range_tree_vacate(phys, NULL, NULL);
	zfs_range_tree_destroy(phys);

	spa_config_enter(spa, SCL_CONFIG, FTAG, RW_READER);
	return (vdev_lookup_top(spa, var->var_vd));
}

/*
 * AnyRAID relocate background thread
 */
static void
spa_anyraid_relocate_thread(void *arg, zthr_t *zthr)
{
	spa_t *spa = arg;
	vdev_anyraid_relocate_t *var = spa->spa_anyraid_relocate;
	ASSERT(var);
	spa_config_enter(spa, SCL_CONFIG, FTAG, RW_READER);
	vdev_t *pvd = vdev_lookup_top(spa, var->var_vd);
	vdev_anyraid_t *va = pvd->vdev_tsd;

	mutex_enter(&var->var_lock);
	/* Iterate over all the tasks */
	for (vdev_anyraid_relocate_task_t *vart =
	    list_head(&var->var_list);
	    vart != NULL && !zthr_iscancelled(zthr);
	    vart = list_head(&var->var_list)) {
		mutex_exit(&var->var_lock);
		rw_enter(&va->vd_lock, RW_READER);
		uint16_t ms_shift = pvd->vdev_ms_shift;
		uint64_t start = (va->vd_width * vart->vart_tile *
		    va->vd_tile_size) >> ms_shift;
		uint64_t starting_offset = var->var_offset;
		uint64_t end = start + ((va->vd_width * va->vd_tile_size) >>
		    ms_shift);
		for (uint64_t i = start; i < end && !zthr_iscancelled(zthr);
		    i++) {
			pvd = process_one_metaslab(spa, pvd->vdev_ms[i], pvd,
			    va, vart, zthr);
		}

		if (zthr_iscancelled(zthr) ||
		    var->var_failed_offset != UINT64_MAX) {
			rw_exit(&va->vd_lock);
			mutex_enter(&var->var_lock);
			break;
		}
		rw_exit(&va->vd_lock);
		rw_enter(&va->vd_lock, RW_WRITER);

		anyraid_tile_t search;
		search.at_tile_id = vart->vart_tile;
		anyraid_tile_t *tile = avl_find(&va->vd_tile_map, &search,
		    NULL);
		boolean_t found = B_FALSE;
		int count = 0;
		for (anyraid_tile_node_t *atn = list_head(&tile->at_list); atn;
		    atn = list_next(&tile->at_list, atn)) {
			ASSERT(atn);
			if (atn->atn_disk != vart->vart_source_disk) {
				count++;
				continue;
			}
			ASSERT3U(atn->atn_tile_idx, ==, vart->vart_source_idx);
			atn->atn_disk = vart->vart_dest_disk;
			atn->atn_tile_idx = vart->vart_dest_idx;
			found = B_TRUE;
			break;
		}
		IMPLY(!found, starting_offset >= end);
		mutex_enter(&var->var_lock);
		list_remove(&var->var_list, vart);
		list_insert_tail(&var->var_done_list, vart);
		rw_exit(&va->vd_lock);
	}
	spa_config_exit(spa, SCL_CONFIG, FTAG);
	mutex_exit(&var->var_lock);

	/*
	 * The txg_wait_synced() here ensures that all relocate zio's have
	 * completed, and var_failed_offset has been set if necessary.  It
	 * also ensures that the progress of the last anyraid_relocate_sync()
	 * is written to disk before anyraid_relocate_complete_sync() changes
	 * the in-memory var_state.  vdev_anyraid_io_start() uses var_state to
	 * determine if a relocate is in progress, in which case we may need to
	 * write to both old and new locations.  Therefore we can only change
	 * var_state once this is not necessary, which is once the on-disk
	 * progress (in spa_ubsync) has been set past any possible writes (to
	 * the end of the last metaslab).
	 */
	txg_wait_synced(spa->spa_dsl_pool, 0);

	if (!zthr_iscancelled(zthr) && list_head(&var->var_list) == NULL) {
		/*
		 * We are not being canceled or paused, so the reflow must be
		 * complete. In that case also mark it as completed on disk.
		 */
		ASSERT3U(var->var_failed_offset, ==, UINT64_MAX);
		ASSERT(spa->spa_anyraid_relocate);
		VERIFY0(dsl_sync_task(spa_name(spa), NULL,
		    anyraid_relocate_complete_sync, spa,
		    0, ZFS_SPACE_CHECK_NONE));
	} else {
		/*
		 * Wait for all copy zio's to complete and for all the
		 * raidz_reflow_sync() synctasks to be run.
		 */
		spa_history_log_internal(spa, "relocate pause",
		    NULL, "offset=%llu failed_offset=%lld/%lld",
		    (long long)var->var_offset,
		    (long long)var->var_failed_task,
		    (long long)var->var_failed_offset);
		if (var->var_failed_offset != UINT64_MAX) {
			/*
			 * Reset progress so that we will retry everything
			 * after the point that something failed.
			 */
			var->var_offset = var->var_failed_offset;
			var->var_task = var->var_failed_task;
			var->var_failed_offset = UINT64_MAX;
			var->var_failed_task = UINT64_MAX;
			var->var_waiting_for_resilver = B_TRUE;
		}
	}
}

void
spa_start_anyraid_relocate_thread(spa_t *spa)
{
	ASSERT0P(spa->spa_anyraid_relocate_zthr);
	spa->spa_anyraid_relocate_zthr = zthr_create("anyraid_relocate",
	    spa_anyraid_relocate_thread_check, spa_anyraid_relocate_thread,
	    spa, defclsyspri);
}

static boolean_t
vdev_anyraid_expand_child_replacing(vdev_t *anyraid_vd)
{
	for (int i = 0; i < anyraid_vd->vdev_children; i++) {
		/* Quick check if a child is being replaced */
		if (!anyraid_vd->vdev_child[i]->vdev_ops->vdev_op_leaf)
			return (B_TRUE);
	}
	return (B_FALSE);
}

void
anyraid_dtl_reassessed(vdev_t *vd)
{
	spa_t *spa = vd->vdev_spa;
	if (spa->spa_anyraid_relocate != NULL) {
		vdev_anyraid_relocate_t *var = spa->spa_anyraid_relocate;
		/*
		 * we get called often from vdev_dtl_reassess() so make
		 * sure it's our vdev and any replacing is complete
		 */
		if (vd->vdev_top->vdev_id == var->var_vd &&
		    !vdev_anyraid_expand_child_replacing(vd->vdev_top)) {
			mutex_enter(&var->var_lock);
			if (var->var_waiting_for_resilver) {
				vdev_dbgmsg(vd, "DTL reassessed, "
				    "continuing anyraid relocate");
				var->var_waiting_for_resilver = B_FALSE;
				zthr_wakeup(spa->spa_anyraid_relocate_zthr);
			}
			mutex_exit(&var->var_lock);
		}
	}
}

int
spa_anyraid_relocate_get_stats(spa_t *spa, pool_anyraid_relocate_stat_t *pars)
{
	vdev_anyraid_relocate_t *var = spa->spa_anyraid_relocate;

	if (var == NULL) {
		/* no removal in progress; find most recent completed */
		for (int c = 0; c < spa->spa_root_vdev->vdev_children; c++) {
			vdev_t *vd = spa->spa_root_vdev->vdev_child[c];
			if (vdev_is_anyraid(vd)) {
				vdev_anyraid_t *va = vd->vdev_tsd;

				if (va->vd_relocate.var_end_time != 0 &&
				    (var == NULL ||
				    va->vd_relocate.var_end_time >
				    var->var_end_time)) {
					var = &va->vd_relocate;
				}
			}
		}
	}

	if (var == NULL)
		return (SET_ERROR(ENOENT));

	pars->pars_state = var->var_state;
	pars->pars_relocating_vdev = var->var_vd;

	vdev_t *vd = vdev_lookup_top(spa, var->var_vd);
	pars->pars_to_move = vd->vdev_stat.vs_alloc;

	mutex_enter(&var->var_lock);
	pars->pars_moved = var->var_bytes_copied;
	for (int i = 0; i < TXG_SIZE; i++)
		pars->pars_moved += var->var_bytes_copied_pertxg[i];
	mutex_exit(&var->var_lock);

	pars->pars_start_time = var->var_start_time;
	pars->pars_end_time = var->var_end_time;
	pars->pars_waiting_for_resilver = var->var_waiting_for_resilver;

	return (0);
}

/*
 * ==========================================================================
 * CONTRACTION-SPECIFIC LOGIC
 * ==========================================================================
 */

static int
vdev_anyraid_check_contract_fast(vdev_t *tvd, vdev_t *lvd)
{
	vdev_anyraid_t *va = tvd->vdev_tsd;
	rw_enter(&va->vd_lock, RW_READER);
	const anyraid_freelist_t *af =
	    &va->vd_children[lvd->vdev_id]->van_freelist;
	uint16_t alloced = anyraid_freelist_alloc(af);
	uint32_t free = 0;
	for (int i = 0; i < tvd->vdev_children; i++) {
		if (i == lvd->vdev_id)
			continue;
		vdev_anyraid_node_t *van = va->vd_children[i];
		free += van->van_capacity -
		    anyraid_freelist_alloc(&van->van_freelist);
	}
	rw_exit(&va->vd_lock);
	return (free >= alloced ? 0 : ENOSPC);
}

int
vdev_anyraid_check_contract(vdev_t *tvd, vdev_t *lvd, dmu_tx_t *tx)
{
	vdev_anyraid_t *va = tvd->vdev_tsd;
	int error = 0;
	spa_t *spa = tvd->vdev_spa;
	if (spa_has_checkpoint(spa))
		return (SET_ERROR(EBUSY));
	if (spa->spa_anyraid_relocate != NULL)
		return (SET_ERROR(EALREADY));
	if (tvd->vdev_children == va->vd_width)
		return (SET_ERROR(ENODEV));

	if (!dmu_tx_is_syncing(tx))
		return (vdev_anyraid_check_contract_fast(tvd, lvd));

	vdev_anyraid_relocate_t *var = &va->vd_relocate;
	var->var_start_time = gethrestime_sec();
	var->var_state = ARS_SCANNING;
	var->var_vd = tvd->vdev_id;
	var->var_failed_offset = var->var_failed_task = UINT64_MAX;
	ASSERT3S(va->vd_contracting_leaf, ==, -1);
	va->vd_contracting_leaf = lvd->vdev_id;
	var->var_offset = 0;

	/*
	 * This is unlocked in the setup function, since we need the state to
	 * remain consistent between the two.
	 */
	mutex_enter(&var->var_lock);
	tvd->vdev_spa->spa_anyraid_relocate = var;

	rw_enter(&va->vd_lock, RW_WRITER);

	/*
	 * Step 1: Calculate a movement plan that would empty the selected leaf
	 * vdev of tiles
	 */
	avl_tree_t ft;
	avl_create(&ft, rebal_cmp_free, sizeof (struct rebal_node),
	    offsetof(struct rebal_node, node1));

	uint64_t *num_tiles = kmem_zalloc(tvd->vdev_children *
	    sizeof (*num_tiles), KM_SLEEP);
	for (int c = 0; c < tvd->vdev_children; c++)
		num_tiles[c] = (va->vd_children[c]->van_capacity);

	num_tiles[lvd->vdev_id] = 0;

	struct rebal_node *donor = NULL;
	for (int i = 0; i < tvd->vdev_children; i++) {
		struct rebal_node *rn = kmem_zalloc(sizeof (*rn), KM_SLEEP);
		rn->cvd = i;
		vdev_anyraid_node_t *n = va->vd_children[i];
		uint32_t cap = n->van_capacity;
		rn->alloc = anyraid_freelist_alloc(&n->van_freelist);
		rn->free = cap - rn->alloc;
		rn->arr = kmem_alloc(sizeof (*rn->arr) * cap, KM_SLEEP);
		memset(rn->arr, -1, sizeof (*rn->arr) * cap);
		populate_child_array(va, i, rn->arr, cap);
		avl_add(&ft, rn);
		if (i == lvd->vdev_id)
			donor = rn;
	}
	anyraid_freelist_t *af = &va->vd_children[lvd->vdev_id]->van_freelist;
	uint32_t tid = 0;
	for (uint16_t o = 0; o < af->af_next_off; o++) {
		if (anyraid_freelist_isfree(af, o))
			continue;
		boolean_t moved = B_FALSE;
		struct rebal_node *receiver = avl_last(&ft);
		while (receiver && receiver->free > 0) {
			struct rebal_node *prev_rec =
			    AVL_PREV(&ft, receiver);
			moved = reloc_try_move_one(va,
			    donor, o, receiver, &tid);
			if (!moved) {
				receiver = prev_rec;
				continue;
			}
			avl_remove(&ft, receiver);
			receiver->free--;
			receiver->alloc++;
			avl_add(&ft, receiver);
			break;
		}
		if (!moved) {
			/*
			 * We couldn't find anywhere to put this tile, we can't
			 * do contraction right now. It's possible that by
			 * redoing the plan generation we could make different
			 * choices earlier that would work; that feature is
			 * left for future implementation.
			 */
			error = SET_ERROR(EXFULL);
			goto out;
		}
	}

	/*
	 * Step 2: Calculate the new asize of the proposed movement plan
	 */
	uint64_t updated_asize = calculate_asize(tvd, num_tiles);

	/*
	 * Step 3: Verify that all the current data can fit in the proposed
	 * movement plan
	 */
	anyraid_tile_t *at = avl_last(&va->vd_tile_map);
	uint32_t highest_tile = at->at_tile_id;
	if (updated_asize / va->vd_tile_size <= highest_tile) {
		/*
		 * In this case we do have room to generate a full movement
		 * plan, but we end up with not enough tiles to actually back
		 * the whole space we would need to reach the highest-offset
		 * currently allocated block without having a hole in the vdev.
		 *
		 * This mostly should not happen, since we strongly prefer
		 * earlier metaslabs to ensure that tiles are allocated in
		 * ascending logical order. But we should have logic to handle
		 * it, just in case.
		 */
		error = SET_ERROR(EDOM);
		goto out;
	}

	/*
	 * Step 4: Disable all the metaslabs that will become unusable
	 */
	for (uint64_t m = updated_asize >> tvd->vdev_ms_shift;
	    m < tvd->vdev_ms_count; m++) {
		metaslab_disable_nowait(tvd->vdev_ms[m]);
	}

	va->vd_children[lvd->vdev_id]->van_capacity = 0;
	/*
	 * At this point, the relocation plan has been generated and everything
	 * else involved in setup is fail-proof. We leave the rest of the
	 * process to happen in the _sync function, aside from some cleanup.
	 */
out:
	if (error != 0) {
		vdev_anyraid_relocate_task_t *vart;
		while ((vart = list_remove_head(&var->var_list))) {
			vdev_anyraid_node_t *van =
			    va->vd_children[vart->vart_dest_disk];
			anyraid_freelist_add(&van->van_freelist,
			    vart->vart_dest_idx);
			kmem_free(vart, sizeof (*vart));
		}
		var->var_state = ARS_FINISHED;
		tvd->vdev_spa->spa_anyraid_relocate = NULL;
		va->vd_contracting_leaf = -1;
		mutex_exit(&var->var_lock);
	}
	rw_exit(&va->vd_lock);

	kmem_free(num_tiles, tvd->vdev_children * sizeof (*num_tiles));

	struct rebal_node *node;
	void *cookie = NULL;
	while ((node = avl_destroy_nodes(&ft, &cookie)) != NULL) {
		kmem_free(node->arr, sizeof (*node->arr) *
		    (node->free + node->alloc));
		kmem_free(node, sizeof (*node));
	}
	avl_destroy(&ft);
	return (error);
}

void
vdev_anyraid_setup_contract(vdev_t *tvd, dmu_tx_t *tx)
{
	vdev_anyraid_t *va = tvd->vdev_tsd;
	vdev_anyraid_relocate_t *var = &va->vd_relocate;
	ASSERT(MUTEX_HELD(&var->var_lock));
	spa_t *spa = tvd->vdev_spa;
	if (list_head(&var->var_list) == NULL) {
		mutex_exit(&var->var_lock);
		anyraid_relocate_complete_sync(spa, tx);
		return;
	}

	objset_t *mos = spa->spa_meta_objset;

	var->var_object = dmu_object_alloc(mos, DMU_OTN_UINT32_METADATA,
	    SPA_OLD_MAXBLOCKSIZE, DMU_OTN_UINT64_METADATA,
	    sizeof (relocate_phys_t), tx);
	VERIFY0(zap_add(mos, DMU_POOL_DIRECTORY_OBJECT, DMU_POOL_RELOCATE_OBJ,
	    sizeof (uint64_t), 1, &var->var_object, tx));

	tasklist_write(spa, var, tx);
	mutex_exit(&var->var_lock);
	spa_config_enter(spa, SCL_STATE_ALL, FTAG, RW_WRITER);
	vdev_reopen(tvd);
	spa_async_request(spa, SPA_ASYNC_CONFIG_UPDATE);
	spa_config_exit(spa, SCL_STATE_ALL, FTAG);
	zthr_wakeup(spa->spa_anyraid_relocate_zthr);
}

void
vdev_anyraid_compact_children(vdev_t *vd)
{
	vdev_anyraid_t *va = vd->vdev_tsd;
	vdev_anyraid_node_t **new_children = kmem_alloc(
	    sizeof (*new_children) * vd->vdev_children, KM_SLEEP);
	int idx = 0;
	for (int c = 0; c <= vd->vdev_children; c++) {
		vdev_anyraid_node_t *van = va->vd_children[c];
		if (c == va->vd_contracting_leaf) {
			zfs_dbgmsg("removing %px %d %d", van, van->van_id, van->van_capacity);
			avl_remove(&va->vd_children_tree, van);
			continue;
		}
		if (c > va->vd_contracting_leaf)
			van->van_id--;
		new_children[idx++] = van;
	}
	kmem_free(va->vd_children, sizeof (*va->vd_children) *
	    (vd->vdev_children + 1));
	va->vd_children = new_children;

	for (anyraid_tile_t *at = avl_first(&va->vd_tile_map); at;
	    at = AVL_NEXT(&va->vd_tile_map, at)) {
		int count = 0;
		for (anyraid_tile_node_t *atn = list_head(&at->at_list);
		    atn; atn = list_next(&at->at_list, atn)) {
			ASSERT3U(atn->atn_disk, !=, va->vd_contracting_leaf);
			if (atn->atn_disk > va->vd_contracting_leaf)
				atn->atn_disk--;
			count++;
		}
	}
}

ZFS_MODULE_PARAM(zfs_anyraid, zfs_anyraid_, min_tile_size, U64, ZMOD_RW,
	"Minimum tile size for anyraid");

ZFS_MODULE_PARAM(zfs_vdev, anyraid_, relocate_max_bytes_pause, ULONG, ZMOD_RW,
	"For testing, pause AnyRAID relocate after moving this many bytes");