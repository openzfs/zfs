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
 * Usage: corrupt_sa <pool> <dataset> <object> [<object> ...]
 *
 * Overwrite the SA magic in the bonus buffer of the given objects of an
 * exported pool, so that the objects can not be loaded as znodes any more.
 * This emulates semantic metadata damage which, unlike a checksum error,
 * is not detected by the DMU.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/zfs_context.h>
#include <sys/spa.h>
#include <sys/dmu.h>
#include <sys/dmu_tx.h>
#include <sys/dmu_objset.h>
#include <sys/sa_impl.h>
#include <libzutil.h>
#include <libzpool.h>

/*
 * Registering a callback keeps dnode_sync() from asserting about user
 * accounting of the dirtied dnode; reporting an error skips the accounting
 * of this object, as the kernel does for a damaged SA.
 */
static int
file_info_cb(dmu_object_type_t bonustype, const void *data,
    zfs_file_info_t *zoi)
{
	(void) bonustype, (void) data, (void) zoi;
	return (EINVAL);
}

int
main(int argc, char **argv)
{
	importargs_t ia = { 0 };
	char *path[1];
	nvlist_t *config;
	objset_t *os;
	int error;

	if (argc < 5) {
		(void) fprintf(stderr, "usage: %s <vdev-dir> <pool> <dataset> "
		    "<object>...\n", argv[0]);
		return (2);
	}
	path[0] = argv[1];
	ia.path = path;
	ia.paths = 1;

	kernel_init(SPA_MODE_READ | SPA_MODE_WRITE);
	dmu_objset_register_type(DMU_OST_ZFS, file_info_cb);

	libpc_handle_t lpch = {
		.lpc_lib_handle = NULL,
		.lpc_ops = &libzpool_config_ops,
		.lpc_printerr = B_TRUE
	};
	error = zpool_find_config(&lpch, argv[2], &config, &ia);
	if (error != 0) {
		(void) fprintf(stderr, "cannot find pool %s\n", argv[2]);
		return (1);
	}
	error = spa_import(argv[2], config, NULL, ZFS_IMPORT_NORMAL);
	if (error != 0) {
		(void) fprintf(stderr, "cannot import: %s\n", strerror(error));
		return (1);
	}
	error = dmu_objset_own(argv[3], DMU_OST_ZFS, B_FALSE, B_FALSE, FTAG,
	    &os);
	if (error != 0) {
		(void) fprintf(stderr, "cannot own %s: %s\n", argv[3],
		    strerror(error));
		return (1);
	}

	for (int i = 4; i < argc; i++) {
		uint64_t obj = strtoull(argv[i], NULL, 0);
		dmu_buf_t *db;
		dmu_tx_t *tx = dmu_tx_create(os);

		dmu_tx_hold_bonus(tx, obj);
		error = dmu_tx_assign(tx, DMU_TX_WAIT);
		if (error != 0) {
			dmu_tx_abort(tx);
			(void) fprintf(stderr, "tx assign: %s\n",
			    strerror(error));
			return (1);
		}
		error = dmu_bonus_hold(os, obj, FTAG, &db);
		if (error != 0) {
			dmu_tx_commit(tx);
			(void) fprintf(stderr, "bonus hold %llu: %s\n",
			    (u_longlong_t)obj, strerror(error));
			return (1);
		}
		dmu_buf_will_dirty(db, tx);
		((sa_hdr_phys_t *)db->db_data)->sa_magic = 0xdeadbeef;
		dmu_buf_rele(db, FTAG);
		dmu_tx_commit(tx);
		(void) printf("corrupted SA magic of object %llu\n",
		    (u_longlong_t)obj);
	}

	dmu_objset_disown(os, B_FALSE, FTAG);
	(void) spa_export(argv[2], NULL, B_TRUE, B_FALSE);
	kernel_fini();
	return (0);
}
