// SPDX-License-Identifier: CDDL-1.0
/*
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this
 * source. A copy of the CDDL is also available via the Internet at
 * https://opensource.org/license/CDDL-1.0.
 */

#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <libzpool.h>
#include <sys/abd.h>
#include <sys/dbuf.h>
#include <sys/dmu.h>
#include <sys/dmu_objset.h>
#include <sys/dsl_pool.h>
#include <sys/dsl_prop.h>
#include <sys/spa_impl.h>
#include <sys/vdev.h>
#include <sys/vdev_impl.h>
#include <sys/zfs_ioctl.h>

#define	DATA_SIZE	(3 * 4096)

static nvlist_t *
file_vdev(const char *dir, int index)
{
	char path[MAXPATHLEN];
	(void) snprintf(path, sizeof (path), "%s/disk%d", dir, index);
	int fd = open(path, O_CREAT | O_TRUNC | O_RDWR, 0600);
	VERIFY3S(fd, >=, 0);
	VERIFY0(ftruncate(fd, 512ULL << 20));
	VERIFY0(close(fd));
	nvlist_t *nv = fnvlist_alloc();
	fnvlist_add_string(nv, ZPOOL_CONFIG_TYPE, VDEV_TYPE_FILE);
	fnvlist_add_string(nv, ZPOOL_CONFIG_PATH, path);
	fnvlist_add_uint64(nv, ZPOOL_CONFIG_ASHIFT, 12);
	return (nv);
}

static nvlist_t *
root_vdev(nvlist_t *child)
{
	nvlist_t *nv = fnvlist_alloc();
	fnvlist_add_string(nv, ZPOOL_CONFIG_TYPE, VDEV_TYPE_ROOT);
	fnvlist_add_nvlist_array(nv, ZPOOL_CONFIG_CHILDREN,
	    (const nvlist_t **)&child, 1);
	return (nv);
}

typedef struct {
	int source;
	int target;
	off_t first_offset;
	uint64_t copied;
} copy_column_t;

static void
copy_column(void *arg, zfs_range_seg64_t *range)
{
	copy_column_t *copy = arg;
	char buf[4096];
	for (uint64_t p = range->rs_start; p < range->rs_end;
	    p += sizeof (buf)) {
		size_t size = MIN(sizeof (buf), range->rs_end - p);
		off_t offset = p + VDEV_LABEL_START_SIZE;
		if (copy->copied == 0)
			copy->first_offset = offset;
		VERIFY3S(pread(copy->source, buf, size, offset), ==, size);
		VERIFY3S(pwrite(copy->target, buf, size, offset), ==, size);
		copy->copied += size;
	}
}

static int
fail_reads(const char *pool, vdev_t *vd)
{
	zinject_record_t fault = {
		.zi_guid = vd->vdev_guid,
		.zi_error = EFAULT,
		.zi_freq = ZI_PERCENTAGE_MAX,
		.zi_iotype = ZINJECT_IOTYPE_READ,
		.zi_cmd = ZINJECT_DEVICE_FAULT
	};
	int id;
	VERIFY0(zio_inject_fault((char *)pool, 0, &id, &fault));
	return (id);
}

static void
verify_fault(int wanted)
{
	int id = 0;
	char pool[MAXNAMELEN];
	zinject_record_t fault;
	do {
		VERIFY0(zio_inject_list_next(&id, pool, sizeof (pool), &fault));
	} while (id < wanted);
	VERIFY3S(id, ==, wanted);
	VERIFY3U(fault.zi_inject_count, >, 0);
}

static int
scrub_read(spa_t *spa, const blkptr_t *bp, char *buf)
{
	abd_t *abd = abd_alloc_for_io(DATA_SIZE, B_FALSE);
	abd_zero(abd, DATA_SIZE);
	int error = zio_wait(zio_read(NULL, spa, bp, abd, DATA_SIZE,
	    NULL, NULL, ZIO_PRIORITY_SCRUB,
	    ZIO_FLAG_SCRUB | ZIO_FLAG_RAW | ZIO_FLAG_CANFAIL |
	    ZIO_FLAG_DONT_RETRY, NULL));
	abd_copy_to_buf(buf, abd, DATA_SIZE);
	abd_free(abd);
	return (error);
}

int
main(int argc, char **argv)
{
	if (argc != 2)
		return (2);
	const char *dir = argv[1];
	char pool[] = "scrub_rebuild_recovery";
	char cachefile[MAXPATHLEN];
	(void) snprintf(cachefile, sizeof (cachefile), "%s/zpool.cache", dir);
	/* A missing cache makes libzpool fall back to the boot pool cache. */
	nvlist_t *empty = fnvlist_alloc();
	size_t cachelen;
	char *cache = fnvlist_pack(empty, &cachelen);
	int fd = open(cachefile, O_CREAT | O_TRUNC | O_WRONLY, 0600);
	VERIFY3S(fd, >=, 0);
	VERIFY3S(write(fd, cache, cachelen), ==, cachelen);
	VERIFY0(close(fd));
	fnvlist_pack_free(cache, cachelen);
	fnvlist_free(empty);
	spa_config_path = cachefile;
	kernel_init(SPA_MODE_READ | SPA_MODE_WRITE);
	VERIFY0(handle_tunable_option("zfs_scan_suspend_progress=1", B_TRUE));
	VERIFY0(handle_tunable_option("zfs_rebuild_scrub_enabled=0", B_TRUE));

	nvlist_t *leaves[4];
	for (int i = 0; i < 4; i++)
		leaves[i] = file_vdev(dir, i);
	nvlist_t *draid = fnvlist_alloc();
	fnvlist_add_string(draid, ZPOOL_CONFIG_TYPE, VDEV_TYPE_DRAID);
	fnvlist_add_uint64(draid, ZPOOL_CONFIG_NPARITY, 1);
	fnvlist_add_uint64(draid, ZPOOL_CONFIG_DRAID_NDATA, 3);
	fnvlist_add_uint64(draid, ZPOOL_CONFIG_DRAID_NSPARES, 0);
	fnvlist_add_uint64(draid, ZPOOL_CONFIG_DRAID_NGROUPS, 1);
	fnvlist_add_nvlist_array(draid, ZPOOL_CONFIG_CHILDREN,
	    (const nvlist_t **)leaves, 4);
	nvlist_t *root = root_vdev(draid);
	nvlist_t *props = fnvlist_alloc();
	fnvlist_add_uint64(props, "feature@draid", 0);
	fnvlist_add_uint64(props, "feature@device_rebuild", 0);
	VERIFY0(spa_create(pool, root, props, NULL, NULL, NULL));
	fnvlist_free(props);
	fnvlist_free(root);
	fnvlist_free(draid);
	for (int i = 0; i < 4; i++)
		fnvlist_free(leaves[i]);

	spa_t *spa;
	VERIFY0(spa_open(pool, &spa, FTAG));
	objset_t *os;
	VERIFY0(dmu_objset_own(pool, DMU_OST_ZFS, B_FALSE, B_TRUE, FTAG, &os));
	VERIFY0(dsl_prop_set_int(pool, "compression", ZPROP_SRC_LOCAL,
	    ZIO_COMPRESS_OFF));
	VERIFY0(dsl_prop_set_int(pool, "checksum", ZPROP_SRC_LOCAL,
	    ZIO_CHECKSUM_SHA256));
	VERIFY0(dsl_prop_set_int(pool, "copies", ZPROP_SRC_LOCAL, 1));
	char expected[DATA_SIZE];
	(void) memset(expected, 0x41, 4096);
	(void) memset(expected + 4096, 0x52, 4096);
	(void) memset(expected + 8192, 0x67, 4096);
	dmu_tx_t *tx = dmu_tx_create(os);
	dmu_tx_hold_bonus(tx, DMU_NEW_OBJECT);
	VERIFY0(dmu_tx_assign(tx, DMU_TX_WAIT));
	uint64_t object = dmu_object_alloc(os, DMU_OT_UINT64_OTHER,
	    sizeof (expected), DMU_OT_NONE, 0, tx);
	dmu_tx_commit(tx);
	tx = dmu_tx_create(os);
	dmu_tx_hold_write(tx, object, 0, sizeof (expected));
	VERIFY0(dmu_tx_assign(tx, DMU_TX_WAIT));
	dmu_write(os, object, 0, sizeof (expected), expected, tx, 0);
	dmu_tx_commit(tx);
	txg_wait_synced(spa_get_dsl(spa), 0);
	dmu_buf_t *db;
	VERIFY0(dmu_buf_hold(os, object, 0, FTAG, &db, 0));
	dmu_buf_impl_t *dbuf = (dmu_buf_impl_t *)db;
	db_lock_type_t lock = dmu_buf_lock_parent(dbuf, RW_READER, FTAG);
	blkptr_t bp = *dbuf->db_blkptr;
	dmu_buf_unlock_parent(dbuf, lock, FTAG);
	dmu_buf_rele(db, FTAG);
	VERIFY3U(BP_GET_NDVAS(&bp), ==, 1);
	VERIFY3U(BP_GET_CHECKSUM(&bp), !=, ZIO_CHECKSUM_OFF);
	VERIFY3U(BP_GET_LSIZE(&bp), ==, sizeof (expected));
	VERIFY3U(BP_GET_PSIZE(&bp), ==, sizeof (expected));

	vdev_t *top = spa->spa_root_vdev->vdev_child[0];
	vdev_t *original = top->vdev_child[2];
	nvlist_t *replacement_nv = file_vdev(dir, 4);
	root = root_vdev(replacement_nv);
	VERIFY0(spa_vdev_attach(spa, original->vdev_guid, root,
	    B_TRUE, B_TRUE));
	fnvlist_free(root);
	fnvlist_free(replacement_nv);
	txg_wait_synced(spa_get_dsl(spa), 0);
	vdev_t *replacement = original->vdev_parent->vdev_child[1];
	VERIFY(top->vdev_rebuilding);
	VERIFY(vdev_dtl_contains(replacement, DTL_MISSING,
	    BP_GET_PHYSICAL_BIRTH(&bp), 1));

	/* Install one already-copied column without retiring its broad DTL. */
	copy_column_t copy = {
		.source = open(original->vdev_path, O_RDONLY),
		.target = open(replacement->vdev_path, O_RDWR)
	};
	VERIFY3S(copy.source, >=, 0);
	VERIFY3S(copy.target, >=, 0);
	zfs_range_seg64_t logical = {
		.rs_start = DVA_GET_OFFSET(&bp.blk_dva[0]),
		.rs_end = DVA_GET_OFFSET(&bp.blk_dva[0]) +
		    DVA_GET_ASIZE(&bp.blk_dva[0])
	};
	vdev_xlate_walk(original, &logical, copy_column, &copy);
	VERIFY3U(copy.copied, >, 0);
	VERIFY0(fsync(copy.target));

	int original_fault = fail_reads(pool, original);
	vdev_t *other = top->vdev_child[0];
	int other_fault = fail_reads(pool, other);
	vdev_stat_t before, after;
	vdev_get_stats_ex(other, &before, NULL);
	char actual[DATA_SIZE];
	int error = scrub_read(spa, &bp, actual);
	(void) printf("raw scrub recovery: %d, rebuild active: %d\n",
	    error, top->vdev_rebuilding);
	(void) fflush(stdout);
	VERIFY0(error);
	VERIFY0(memcmp(expected, actual, sizeof (actual)));
	VERIFY(top->vdev_rebuilding);
	verify_fault(original_fault);
	verify_fault(other_fault);
	vdev_get_stats_ex(other, &after, NULL);
	VERIFY3U(after.vs_self_healed, >, before.vs_self_healed);

	/*
	 * A bad candidate must fail the parent's checksum without changing the
	 * original. Successful raw device I/O cannot authorize that repair.
	 */
	uint8_t original_byte, changed_byte, after_byte;
	VERIFY3S(pread(copy.source, &original_byte, 1,
	    copy.first_offset), ==, 1);
	changed_byte = original_byte ^ 0x80;
	VERIFY3S(pwrite(copy.target, &changed_byte, 1,
	    copy.first_offset), ==, 1);
	VERIFY0(fsync(copy.target));
	error = scrub_read(spa, &bp, actual);
	VERIFY3S(error, !=, 0);
	VERIFY3S(pread(copy.source, &after_byte, 1, copy.first_offset), ==, 1);
	(void) printf("invalid candidate: %d, original unchanged: %d\n",
	    error, original_byte == after_byte);
	(void) fflush(stdout);
	VERIFY3U(original_byte, ==, after_byte);
	VERIFY(top->vdev_rebuilding);
	VERIFY0(close(copy.source));
	VERIFY0(close(copy.target));
	VERIFY0(zio_clear_fault(original_fault));
	VERIFY0(zio_clear_fault(other_fault));

	dmu_objset_disown(os, B_TRUE, FTAG);
	spa_close(spa, FTAG);
	VERIFY0(spa_export(pool, NULL, B_FALSE, B_FALSE));
	kernel_fini();
	return (0);
}
