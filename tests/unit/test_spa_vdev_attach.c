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

#include <sys/zfs_context.h>
#include <sys/spa.h>
#include <sys/spa_impl.h>
#include <sys/vdev_impl.h>
#include <sys/nvpair.h>
#include <libzpool.h>
#include "unit.h"

static nvlist_t *
file_vdev(const char *path)
{
	nvlist_t *nvl = fnvlist_alloc();
	fnvlist_add_string(nvl, ZPOOL_CONFIG_TYPE, VDEV_TYPE_FILE);
	fnvlist_add_string(nvl, ZPOOL_CONFIG_PATH, path);
	return (nvl);
}

static MunitResult
test_replace(const MunitParameter params[], void *data)
{
	(void) data;
	const char *state = munit_parameters_get(params, "state");
	char cachefile[MAXPATHLEN];
	char dir[] = "/tmp/test_spa_vdev_attach.XXXXXX";
	char paths[3][MAXPATHLEN];
	nvlist_t *children[2];
	nvlist_t *root = fnvlist_alloc();
	nvlist_t *mirror = fnvlist_alloc();
	nvlist_t *props = fnvlist_alloc();
	spa_t *spa;

	unit_notnull(mkdtemp(dir));
	for (int i = 0; i < 3; i++) {
		(void) snprintf(paths[i], sizeof (paths[i]), "%s/%d", dir, i);
		int fd = open(paths[i], O_CREAT | O_EXCL | O_RDWR, 0600);
		munit_assert_int(fd, >=, 0);
		unit_ok(ftruncate(fd, 128 << 20));
		unit_ok(close(fd));
	}

	(void) snprintf(cachefile, sizeof (cachefile), "%s/cache", dir);
	spa_config_path = cachefile;
	kernel_init(SPA_MODE_READ | SPA_MODE_WRITE);
	for (int i = 0; i < 2; i++)
		children[i] = file_vdev(paths[i]);
	fnvlist_add_string(mirror, ZPOOL_CONFIG_TYPE, VDEV_TYPE_MIRROR);
	fnvlist_add_nvlist_array(mirror, ZPOOL_CONFIG_CHILDREN,
	    (const nvlist_t *const *)children, 2);
	fnvlist_add_string(root, ZPOOL_CONFIG_TYPE, VDEV_TYPE_ROOT);
	fnvlist_add_nvlist_array(root, ZPOOL_CONFIG_CHILDREN,
	    (const nvlist_t *const *)&mirror, 1);
	fnvlist_add_string(props, "cachefile", "none");
	unit_ok(spa_create("attach_test", root, props, NULL, NULL, NULL));
	unit_ok(spa_open("attach_test", &spa, FTAG));
	fnvlist_free(props);
	fnvlist_free(mirror);
	for (int i = 0; i < 2; i++)
		fnvlist_free(children[i]);

	vdev_t *vd = spa->spa_root_vdev->vdev_child[0]->vdev_child[0];
	uint64_t guid = vd->vdev_guid;
	spa_async_suspend(spa);
	if (strcmp(state, "recovered") == 0) {
		unit_ok(vdev_fault(spa, guid, VDEV_AUX_ERR_EXCEEDED));
		spa_vdev_state_enter(spa, SCL_NONE);
		munit_assert_uint64(vd->vdev_state, ==, VDEV_STATE_FAULTED);
		vdev_clear(spa, vd);
		unit_ok(spa_vdev_state_exit(spa, vd, 0));
	}
	spa_vdev_state_enter(spa, SCL_NONE);
	munit_assert_uint64(vd->vdev_state, ==, VDEV_STATE_HEALTHY);
	unit_true(vdev_readable(vd));
	unit_true(vdev_writeable(vd));
	if (strcmp(state, "removing") == 0)
		vd->vdev_remove_wanted = B_TRUE;
	unit_ok(spa_vdev_state_exit(spa, NULL, 0));

	nvlist_t *replacement = file_vdev(paths[2]);
	fnvlist_add_nvlist_array(root, ZPOOL_CONFIG_CHILDREN,
	    (const nvlist_t *const *)&replacement, 1);
	fnvlist_add_boolean(root, ZPOOL_CONFIG_REPLACE_IF_UNHEALTHY);
	int error = spa_vdev_attach(spa, guid, root, B_TRUE, B_FALSE);
	if (strcmp(state, "removing") != 0 && error == EBUSY) {
		spa_config_enter(spa, SCL_ALL, FTAG, RW_READER);
		munit_assert_uint64(vd->vdev_state, ==, VDEV_STATE_HEALTHY);
		munit_assert_uint64(vd->vdev_parent->vdev_children, ==, 2);
		unit_str_eq(vd->vdev_parent->vdev_ops->vdev_op_type,
		    VDEV_TYPE_MIRROR);
		spa_config_exit(spa, SCL_ALL, FTAG);
		fnvlist_remove(root, ZPOOL_CONFIG_REPLACE_IF_UNHEALTHY);
		unit_ok(spa_vdev_attach(spa, guid, root, B_TRUE, B_FALSE));
	}
	spa_async_resume(spa);
	fnvlist_free(replacement);
	fnvlist_free(root);
	spa_close(spa, FTAG);
	unit_ok(spa_destroy("attach_test"));
	kernel_fini();
	for (int i = 0; i < 3; i++)
		unit_ok(unlink(paths[i]));
	unit_ok(rmdir(dir));
	unit_err(error, strcmp(state, "removing") == 0 ? 0 : EBUSY);
	return (MUNIT_OK);
}

static MunitParameterEnum states[] = {
	UNIT_PARAM("state", "healthy", "recovered", "removing"),
	{ NULL, NULL }
};

static MunitTest tests[] = {
	UNIT_TEST("/replace_if_unhealthy", test_replace, states),
	{ NULL }
};

static MunitSuite suite = { "/spa_vdev_attach", tests, NULL, 1,
	MUNIT_SUITE_OPTION_NONE };

int
main(int argc, char *argv[])
{
	return (munit_suite_main(&suite, NULL, argc, argv));
}
