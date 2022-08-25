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
 * Copyright (c) 2022, SmartX Inc. All rights reserved.
 */

#include <sys/zfs_context.h>
#include <sys/spa.h>
#include <sys/dmu.h>
#include <sys/zap.h>
#include <sys/dmu_objset.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/zio.h>
#include <sys/zil.h>
#include <sys/zil_impl.h>
#include <sys/vdev_impl.h>
#include <sys/vdev_file.h>
#include <sys/spa_impl.h>
#include <sys/dsl_prop.h>
#include <sys/dsl_dataset.h>
#include <sys/dsl_destroy.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <umem.h>
#include <ctype.h>
#include <math.h>
#include <sys/fs/zfs.h>
#include <libnvpair.h>
#include <libzutil.h>

#include "libuzfs_impl.h"

enum libuzfs_object {
	LIBUZFS_META_DNODE = 0,
	LIBUZFS_DIROBJ,
	LIBUZFS_OBJECTS
};

static boolean_t change_zpool_cache_path = B_FALSE;

static void
dump_debug_buffer(void)
{
	ssize_t ret __attribute__((unused));

	/*
	 * We use write() instead of printf() so that this function
	 * is safe to call from a signal handler.
	 */
	ret = write(STDOUT_FILENO, "\n", 1);
	zfs_dbgmsg_print("libuzfs");
}

#define	FATAL_MSG_SZ	1024

char *fatal_msg;

static void
fatal(int do_perror, char *message, ...)
{
	va_list args;
	int save_errno = errno;
	char *buf = NULL;

	(void) fflush(stdout);
	buf = umem_alloc(FATAL_MSG_SZ, UMEM_NOFAIL);

	va_start(args, message);
	(void) sprintf(buf, "libuzfs: ");
	/* LINTED */
	(void) vsprintf(buf + strlen(buf), message, args);
	va_end(args);
	if (do_perror) {
		(void) snprintf(buf + strlen(buf), FATAL_MSG_SZ - strlen(buf),
		    ": %s", strerror(save_errno));
	}
	(void) fprintf(stderr, "%s\n", buf);
	fatal_msg = buf;			/* to ease debugging */

	dump_debug_buffer();

	exit(3);
}

static uint64_t
libuzfs_get_ashift(void)
{
	return (SPA_MINBLOCKSHIFT);
}

static nvlist_t *
make_vdev_file(const char *path, char *aux, const char *pool, size_t size,
    uint64_t ashift)
{
	char *pathbuf = NULL;
	nvlist_t *file = NULL;

	pathbuf = umem_alloc(MAXPATHLEN, UMEM_NOFAIL);

	if (ashift == 0)
		ashift = libuzfs_get_ashift();

	if (size != 0) {
		int fd = open(path, O_RDWR | O_CREAT | O_TRUNC, 0666);
		if (fd == -1)
			fatal(1, "can't open %s", path);
		if (ftruncate(fd, size) != 0)
			fatal(1, "can't ftruncate %s", path);
		(void) close(fd);
	}

	file = fnvlist_alloc();
	fnvlist_add_string(file, ZPOOL_CONFIG_TYPE, VDEV_TYPE_FILE);
	fnvlist_add_string(file, ZPOOL_CONFIG_PATH, path);
	fnvlist_add_uint64(file, ZPOOL_CONFIG_ASHIFT, ashift);

	umem_free(pathbuf, MAXPATHLEN);

	return (file);
}

static nvlist_t *
make_vdev_raid(const char *path, char *aux, const char *pool, size_t size,
    uint64_t ashift, int r)
{
	return (make_vdev_file(path, aux, pool, size, ashift));
}

static nvlist_t *
make_vdev_mirror(const char *path, char *aux, const char *pool, size_t size,
    uint64_t ashift, int r, int m)
{
	int c = 0;
	nvlist_t *mirror = NULL;
	nvlist_t **child = NULL;

	if (m < 1)
		return (make_vdev_raid(path, aux, pool, size, ashift, r));

	child = umem_alloc(m * sizeof (nvlist_t *), UMEM_NOFAIL);

	for (c = 0; c < m; c++)
		child[c] = make_vdev_raid(path, aux, pool, size, ashift, r);

	mirror = fnvlist_alloc();
	fnvlist_add_string(mirror, ZPOOL_CONFIG_TYPE, VDEV_TYPE_MIRROR);
	fnvlist_add_nvlist_array(mirror, ZPOOL_CONFIG_CHILDREN, child, m);

	for (c = 0; c < m; c++)
		fnvlist_free(child[c]);

	umem_free(child, m * sizeof (nvlist_t *));

	return (mirror);
}

static nvlist_t *
make_vdev_root(const char *path, char *aux, const char *pool, size_t size,
    uint64_t ashift, const char *class, int r, int m, int t)
{
	int c = 0;
	boolean_t log = B_FALSE;
	nvlist_t *root = NULL;
	nvlist_t **child = NULL;

	ASSERT3S(t, >, 0);

	log = (class != NULL && strcmp(class, "log") == 0);

	child = umem_alloc(t * sizeof (nvlist_t *), UMEM_NOFAIL);

	for (c = 0; c < t; c++) {
		child[c] = make_vdev_mirror(path, aux, pool, size, ashift,
		    r, m);
		fnvlist_add_uint64(child[c], ZPOOL_CONFIG_IS_LOG, log);

		if (class != NULL && class[0] != '\0') {
			ASSERT(m > 1 || log);   /* expecting a mirror */
			fnvlist_add_string(child[c],
			    ZPOOL_CONFIG_ALLOCATION_BIAS, class);
		}
	}

	root = fnvlist_alloc();
	fnvlist_add_string(root, ZPOOL_CONFIG_TYPE, VDEV_TYPE_ROOT);
	fnvlist_add_nvlist_array(root, aux ? aux : ZPOOL_CONFIG_CHILDREN,
	    child, t);

	for (c = 0; c < t; c++)
		fnvlist_free(child[c]);

	umem_free(child, t * sizeof (nvlist_t *));

	return (root);
}

static int
libuzfs_dsl_prop_set_uint64(const char *osname, zfs_prop_t prop, uint64_t value,
    boolean_t inherit)
{
	int err = 0;
	char *setpoint = NULL;
	uint64_t curval = 0;
	const char *propname = zfs_prop_to_name(prop);

	err = dsl_prop_set_int(osname, propname,
	    (inherit ? ZPROP_SRC_NONE : ZPROP_SRC_LOCAL), value);

	if (err == ENOSPC)
		return (err);

	ASSERT0(err);

	setpoint = umem_alloc(MAXPATHLEN, UMEM_NOFAIL);
	VERIFY0(dsl_prop_get_integer(osname, propname, &curval, setpoint));
	umem_free(setpoint, MAXPATHLEN);

	return (err);
}

static int
libuzfs_spa_prop_set_uint64(spa_t *spa, zpool_prop_t prop, uint64_t value)
{
	int err = 0;
	nvlist_t *props = NULL;

	props = fnvlist_alloc();
	fnvlist_add_uint64(props, zpool_prop_to_name(prop), value);

	err = spa_prop_set(spa, props);

	fnvlist_free(props);

	if (err == ENOSPC)
		return (err);

	ASSERT0(err);

	return (err);
}

static int
libuzfs_dmu_objset_own(const char *name, dmu_objset_type_t type,
    boolean_t readonly, boolean_t decrypt, void *tag, objset_t **osp)
{
	int err = 0;
	char *cp = NULL;
	char ddname[ZFS_MAX_DATASET_NAME_LEN];

	strcpy(ddname, name);
	cp = strchr(ddname, '@');
	if (cp != NULL)
		*cp = '\0';

	err = dmu_objset_own(name, type, readonly, decrypt, tag, osp);
	return (err);
}

// TODO(hping): add zil support
zil_replay_func_t *libuzfs_replay_vector[TX_MAX_TYPE] = {
	NULL,			/* 0 no such transaction type */
	NULL,			/* TX_CREATE */
	NULL,			/* TX_MKDIR */
	NULL,			/* TX_MKXATTR */
	NULL,			/* TX_SYMLINK */
	NULL,			/* TX_REMOVE */
	NULL,			/* TX_RMDIR */
	NULL,			/* TX_LINK */
	NULL,			/* TX_RENAME */
	NULL,			/* TX_WRITE */
	NULL,			/* TX_TRUNCATE */
	NULL,			/* TX_SETATTR */
	NULL,			/* TX_ACL */
	NULL,			/* TX_CREATE_ACL */
	NULL,			/* TX_CREATE_ATTR */
	NULL,			/* TX_CREATE_ACL_ATTR */
	NULL,			/* TX_MKDIR_ACL */
	NULL,			/* TX_MKDIR_ATTR */
	NULL,			/* TX_MKDIR_ACL_ATTR */
	NULL,			/* TX_WRITE2 */
};

/*
 * ZIL get_data callbacks
 */

static int
libuzfs_get_data(void *arg, uint64_t arg2, lr_write_t *lr, char *buf,
    struct lwb *lwb, zio_t *zio)
{
	return (0);
}

void
libuzfs_init()
{
	kernel_init(SPA_MODE_READ | SPA_MODE_WRITE);
}

void
libuzfs_fini()
{
	kernel_fini();
	if (change_zpool_cache_path) {
		free(spa_config_path);
	}
}

void
libuzfs_set_zpool_cache_path(const char *zpool_cache)
{
	spa_config_path = strndup(zpool_cache, MAXPATHLEN);
	change_zpool_cache_path = B_TRUE;
}

// for now, only support one device per pool
int
libuzfs_zpool_create(const char *zpool, const char *path, nvlist_t *props,
    nvlist_t *fsprops)
{
	int err = 0;
	nvlist_t *nvroot = NULL;

	nvroot = make_vdev_root(path, NULL, zpool, 0, 0, NULL, 1, 0, 1);

	err = spa_create(zpool, nvroot, props, NULL, NULL);
	if (err) {
		goto out;
	}

out:
	fnvlist_free(nvroot);
	return (err);
}

int
libuzfs_zpool_destroy(const char *zpool)
{
	return (spa_destroy(zpool));
}

libuzfs_zpool_handle_t *
libuzfs_zpool_open(const char *zpool)
{
	int err = 0;
	spa_t *spa = NULL;

	err = spa_open(zpool, &spa, FTAG);
	if (err)
		return (NULL);

	libuzfs_zpool_handle_t *zhp;
	zhp = umem_alloc(sizeof (libuzfs_zpool_handle_t), UMEM_NOFAIL);
	zhp->spa = spa;
	(void) strlcpy(zhp->zpool_name, zpool, sizeof (zhp->zpool_name));

	return (zhp);
}

void
libuzfs_zpool_close(libuzfs_zpool_handle_t *zhp)
{
	spa_close(zhp->spa, FTAG);
	free(zhp);
}

void
libuzfs_zpool_prop_set(libuzfs_zpool_handle_t *zhp, zpool_prop_t prop,
    uint64_t value)
{
	libuzfs_spa_prop_set_uint64(zhp->spa, prop, value);
}

int
libuzfs_zpool_prop_get(libuzfs_zpool_handle_t *zhp, zpool_prop_t prop,
    uint64_t *value)
{
	int err = 0;
	nvlist_t *props = NULL;
	nvlist_t *propval = NULL;

	VERIFY0(spa_prop_get(zhp->spa, &props));

	err = nvlist_lookup_nvlist(props, zpool_prop_to_name(prop), &propval);
	if (err) {
		goto out;
	}

	*value = fnvlist_lookup_uint64(propval, ZPROP_VALUE);

out:
	fnvlist_free(props);
	return (err);
}

static void
libuzfs_objset_create_cb(objset_t *os, void *arg, cred_t *cr, dmu_tx_t *tx)
{
	/*
	 * Create the objects common to all libuzfs datasets.
	 */
	VERIFY0(zap_create_claim(os, LIBUZFS_DIROBJ, DMU_OT_ZAP_OTHER,
	    DMU_OT_NONE, 0, tx));
}

int
libuzfs_dataset_create(const char *dsname)
{
	int err = 0;

	err = dmu_objset_create(dsname, DMU_OST_ZFS, 0, NULL,
	    libuzfs_objset_create_cb, NULL);
	if (err)
		return (err);

	return (libuzfs_dsl_prop_set_uint64(dsname, ZFS_PROP_SYNC,
	    ZFS_SYNC_ALWAYS, B_FALSE));
}

static int
libuzfs_objset_destroy_cb(const char *name, void *arg)
{
	int err = 0;
	objset_t *os = NULL;
	dmu_object_info_t doi;

	memset(&doi, 0, sizeof (doi));

	/*
	 * Verify that the dataset contains a directory object.
	 */
	VERIFY0(libuzfs_dmu_objset_own(name, DMU_OST_ZFS, B_TRUE, B_TRUE, FTAG,
	    &os));
	err = dmu_object_info(os, LIBUZFS_DIROBJ, &doi);
	if (err != ENOENT) {
		/* We could have crashed in the middle of destroying it */
		ASSERT0(err);
		ASSERT3U(doi.doi_type, ==, DMU_OT_ZAP_OTHER);
		ASSERT3S(doi.doi_physical_blocks_512, >=, 0);
	}
	dmu_objset_disown(os, B_TRUE, FTAG);

	/*
	 * Destroy the dataset.
	 */
	if (strchr(name, '@') != NULL) {
		VERIFY0(dsl_destroy_snapshot(name, B_TRUE));
	} else {
		err = dsl_destroy_head(name);
		if (err != EBUSY) {
			/* There could be a hold on this dataset */
			ASSERT0(err);
		}
	}
	return (0);
}

void
libuzfs_dataset_destroy(const char *dsname)
{
	(void) dmu_objset_find(dsname, libuzfs_objset_destroy_cb, NULL,
	    DS_FIND_SNAPSHOTS | DS_FIND_CHILDREN);
}

static void
libuzfs_dhp_init(libuzfs_dataset_handle_t *dhp, objset_t *os)
{
	dhp->os = os;
	dhp->zilog = dmu_objset_zil(os);
	dmu_objset_name(os, dhp->name);
}

static void
libuzfs_dhp_fini(libuzfs_dataset_handle_t *dhp)
{
}

static void
libuzfs_dataset_dirobj_verify(libuzfs_dataset_handle_t *dhp)
{
	uint64_t usedobjs = 0;
	uint64_t dirobjs = 0;
	uint64_t scratch = 0;

	/*
	 * LIBUZFS_DIROBJ is the object directory for the entire dataset.
	 * Therefore, the number of objects in use should equal the
	 * number of LIBUZFS_DIROBJ entries, +1 for LIBUZFS_DIROBJ itself.
	 * If not, we have an object leak.
	 *
	 * Note that we can only check this in libuzfs_dataset_open(),
	 * when the open-context and syncing-context values agree.
	 * That's because zap_count() returns the open-context value,
	 * while dmu_objset_space() returns the rootbp fill count.
	 */
	VERIFY0(zap_count(dhp->os, LIBUZFS_DIROBJ, &dirobjs));
	dmu_objset_space(dhp->os, &scratch, &scratch, &usedobjs, &scratch);
	ASSERT3U(dirobjs + 1, <=, usedobjs);
}

libuzfs_dataset_handle_t *
libuzfs_dataset_open(const char *dsname)
{
	libuzfs_dataset_handle_t *dhp = NULL;
	objset_t *os = NULL;
	zilog_t *zilog = NULL;

	dhp = umem_alloc(sizeof (libuzfs_dataset_handle_t), UMEM_NOFAIL);

	VERIFY0(libuzfs_dmu_objset_own(dsname, DMU_OST_ZFS, B_FALSE, B_TRUE,
	    dhp, &os));

	libuzfs_dhp_init(dhp, os);

	zilog = dhp->zilog;

	libuzfs_dataset_dirobj_verify(dhp);

	zil_replay(os, dhp, libuzfs_replay_vector);

	libuzfs_dataset_dirobj_verify(dhp);

	zilog = zil_open(os, libuzfs_get_data);

	return (dhp);
}

void
libuzfs_dataset_close(libuzfs_dataset_handle_t *dhp)
{
	zil_close(dhp->zilog);
	dmu_objset_disown(dhp->os, B_TRUE, dhp);
	libuzfs_dhp_fini(dhp);
	free(dhp);
}

int
libuzfs_object_stat(libuzfs_dataset_handle_t *dhp, uint64_t obj,
    dmu_object_info_t *doi)
{
	int err = 0;
	dmu_buf_t *db = NULL;
	objset_t *os = dhp->os;

	err = dmu_bonus_hold(os, obj, FTAG, &db);
	if (err)
		return (err);

	dmu_object_info_from_db(db, doi);

	dmu_buf_rele(db, FTAG);
	return (0);
}

int
libuzfs_object_create(libuzfs_dataset_handle_t *dhp, uint64_t *obj)
{
	int err = 0;
	dmu_tx_t *tx = NULL;
	dmu_buf_t *db = NULL;
	objset_t *os = dhp->os;

	tx = dmu_tx_create(os);

	dmu_tx_hold_bonus(tx, DMU_NEW_OBJECT);

	err = dmu_tx_assign(tx, TXG_WAIT);
	if (err) {
		dmu_tx_abort(tx);
		goto out;
	}

	int dnodesize = dmu_objset_dnodesize(os);
	int bonuslen = DN_BONUS_SIZE(dnodesize);
	int blocksize = 0;
	int ibshift = 0;

	*obj = dmu_object_alloc_dnsize(os, DMU_OT_UINT64_OTHER, 0,
	    DMU_OT_UINT64_OTHER, bonuslen, dnodesize, tx);

	VERIFY0(dmu_object_set_blocksize(os, *obj, blocksize, ibshift, tx));
	VERIFY0(dmu_bonus_hold(os, *obj, FTAG, &db));
	dmu_buf_will_dirty(db, tx);
	dmu_buf_rele(db, FTAG);
	dmu_tx_commit(tx);
	txg_wait_synced(spa_get_dsl(os->os_spa), 0);

out:
	return (err);
}

int
libuzfs_object_delete(libuzfs_dataset_handle_t *dhp, uint64_t obj)
{
	int err = 0;
	dmu_tx_t *tx = NULL;
	objset_t *os = dhp->os;

	tx = dmu_tx_create(os);

	dmu_tx_hold_free(tx, obj, 0, DMU_OBJECT_END);

	err = dmu_tx_assign(tx, TXG_WAIT);
	if (err) {
		dmu_tx_abort(tx);
		goto out;
	}

	VERIFY0(dmu_object_free(os, obj, tx));

	dmu_tx_commit(tx);
	txg_wait_synced(spa_get_dsl(os->os_spa), 0);

out:
	return (err);
}

int
libuzfs_object_claim(libuzfs_dataset_handle_t *dhp, uint64_t obj)
{
	int err = 0;
	dmu_tx_t *tx = NULL;
	dmu_buf_t *db = NULL;
	objset_t *os = dhp->os;

	int dnodesize = dmu_objset_dnodesize(os);
	int bonuslen = DN_BONUS_SIZE(dnodesize);
	int type = DMU_OT_UINT64_OTHER;
	int bonus_type = DMU_OT_UINT64_OTHER;
	int blocksize = 0;
	int ibs = 0;

	tx = dmu_tx_create(os);

	dmu_tx_hold_bonus(tx, DMU_NEW_OBJECT);

	err = dmu_tx_assign(tx, TXG_WAIT);
	if (err) {
		dmu_tx_abort(tx);
		goto out;
	}

	err = dmu_object_claim_dnsize(os, obj, type, 0, bonus_type, bonuslen,
	    dnodesize, tx);
	if (err)
		goto out;

	VERIFY0(dmu_object_set_blocksize(os, obj, blocksize, ibs, tx));
	VERIFY0(dmu_bonus_hold(os, obj, FTAG, &db));
	dmu_buf_will_dirty(db, tx);
	dmu_buf_rele(db, FTAG);
	dmu_tx_commit(tx);
	txg_wait_synced(spa_get_dsl(os->os_spa), 0);

out:
	return (err);
}

int
libuzfs_object_list(libuzfs_dataset_handle_t *dhp)
{
	int err = 0;
	int i = 0;
	uint64_t obj = 0;
	objset_t *os = dhp->os;
	dmu_object_info_t doi;

	memset(&doi, 0, sizeof (doi));

	for (obj = 0; err == 0; err = dmu_object_next(os, &obj, B_FALSE, 0)) {
		if (libuzfs_object_stat(dhp, obj, &doi)) {
			printf("skip obj w/o bonus buf: %ld\n", obj);
			continue;
		} else {
			printf("object: %ld\n", obj);
		}
		i++;
	}

	return (i);
}

int
libuzfs_object_write(libuzfs_dataset_handle_t *dhp, uint64_t obj,
    uint64_t offset, uint64_t size, const char *buf)
{
	int err = 0;
	objset_t *os = dhp->os;
	dmu_tx_t *tx = NULL;

	tx = dmu_tx_create(os);

	dmu_tx_hold_write(tx, obj, offset, size);

	err = dmu_tx_assign(tx, TXG_WAIT);
	if (err) {
		dmu_tx_abort(tx);
		goto out;
	}

	dmu_write(os, obj, offset, size, buf, tx);

	dmu_tx_commit(tx);
	txg_wait_synced(spa_get_dsl(os->os_spa), 0);

out:
	return (err);
}

int
libuzfs_object_read(libuzfs_dataset_handle_t *dhp, uint64_t obj,
    uint64_t offset, uint64_t size, char *buf)
{
	int err = 0;
	objset_t *os = dhp->os;
	dmu_object_info_t doi;

	memset(&doi, 0, sizeof (doi));

	err = libuzfs_object_stat(dhp, obj, &doi);
	if (err)
		return (err);

	err = dmu_read(os, obj, offset, size, buf, DMU_READ_NO_PREFETCH);
	if (err)
		return (err);

	return (0);
}
