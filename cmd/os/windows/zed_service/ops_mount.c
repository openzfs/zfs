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
 * Copyright (c) 2025 Jorgen Lundman <lundman@lundman.net>.
 */

#define	_CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <libzfs.h>
#include <sys/nvpair.h>

#include "ops_common.h"
#include "ops_mount.h"

/* Provided by service main */
extern libzfs_handle_t *g_lzh;

/* ---------- result helpers ---------- */
// move/merge these into ops_common.c if reused elsewhere
static nvlist_t *
res_pool(const char *pool, int ok, const char *err)
{
	nvlist_t *nvl = fnvlist_alloc();
	fnvlist_add_boolean_value(nvl, "ok", ok ? B_TRUE : B_FALSE);
	if (pool) fnvlist_add_string(nvl, "pool", pool);
	if (!ok && err) fnvlist_add_string(nvl, "err", err);
	return (nvl);
}

static nvlist_t *
res_ds(const char *ds, int ok, const char *err)
{
	nvlist_t *nvl = fnvlist_alloc();
	fnvlist_add_boolean_value(nvl, "ok", ok ? B_TRUE : B_FALSE);
	if (ds) fnvlist_add_string(nvl, "dataset", ds);
	if (!ok && err) fnvlist_add_string(nvl, "err", err);
	return (nvl);
}

/* ---------- open pool by GUID or name ---------- */
typedef struct { uint64_t want; zpool_handle_t *out; } find_guid_ctx_t;

static int
find_guid_cb(zpool_handle_t *zhp, void *data)
{
	find_guid_ctx_t *ctx = (find_guid_ctx_t *)data;
	nvlist_t *cfg = zpool_get_config(zhp, NULL);
	uint64_t g = 0;
	if (cfg) (void) nvlist_lookup_uint64(cfg, ZPOOL_CONFIG_POOL_GUID, &g);
	if (g == ctx->want) {
		ctx->out = zhp;
		return (1);
	} /* keep handle, stop */

	zpool_close(zhp); /* close non-match */
	return (0);
}

static zpool_handle_t *
open_pool_by_guid_or_name(uint64_t guid, const char *name)
{
	if (!g_lzh)
		return (NULL);
	if (guid) {
		find_guid_ctx_t ctx = { guid, NULL };
		(void) zpool_iter(g_lzh, find_guid_cb, &ctx);
		if (ctx.out)
			return (ctx.out);
	}

	if (name && *name)
		return (zpool_open(g_lzh, name));
	return (NULL);
}

/* Pool-wide: enable datasets (mount all) */
nvlist_t *
zed_mount_pool_nvl(uint64_t pool_guid, const char *pool_name,
    const char *mntopts, uint32_t flags)
{
	(void) flags;

	zpool_handle_t *zhp = open_pool_by_guid_or_name(pool_guid, pool_name);
	const char *pname = zhp ? zpool_get_name(zhp) : pool_name;
	if (!zhp)
		return (res_pool(pname, 0, "pool not imported"));

	int rc = zpool_enable_datasets(zhp,
	    (char *)(uintptr_t)mntopts /* constness differs by tree */, 0, 0);

	if (rc != 0) {
		const char *desc = libzfs_error_description(g_lzh);
		zpool_close(zhp);
		return (res_pool(pname, 0, desc ? desc : "mount failed"));
	}

	zpool_close(zhp);
	return (res_pool(pname, 1, NULL));
}

/* Pool-wide: disable datasets (unmount all) */
nvlist_t *
zed_unmount_pool_nvl(uint64_t pool_guid, const char *pool_name,
    uint32_t flags)
{
	zpool_handle_t *zhp = open_pool_by_guid_or_name(pool_guid, pool_name);
	const char *pname = zhp ? zpool_get_name(zhp) : pool_name;
	if (!zhp)
		return (res_pool(pname, 0, "pool not imported"));

	int rc = -1;

	rc = zpool_disable_datasets(zhp,
	    (flags & ZUMNT_FORCE) ? B_TRUE : B_FALSE);

	if (rc != 0) {
		const char *desc = libzfs_error_description(g_lzh);
		zpool_close(zhp);
		return (res_pool(pname, 0, desc ? desc : "unmount failed"));
	}

	zpool_close(zhp);
	return (res_pool(pname, 1, NULL));
}

/* Dataset-level: mount one dataset */
nvlist_t *
zed_mount_dataset_nvl(const char *dataset,
    const char *mntpoint_opt, /* NULL = default */
    uint32_t flags)
{
	if (!g_lzh || !dataset || !*dataset)
		return (res_ds(dataset, 0, "invalid arguments"));

	zfs_handle_t *zhp = zfs_open(g_lzh, dataset, ZFS_TYPE_DATASET);
	if (!zhp)
		return (res_ds(dataset, 0, "dataset not found"));

/*
 * Optional: honor ZMNT_READONLY by building mntopts if your tree supports it.
 * Most libzfs trees ignore mntopts here; property-based RO is preferred.
 */
	(void) flags;

	int rc = zfs_mount(zhp, mntpoint_opt, 0);

	if (rc != 0) {
		const char *desc = libzfs_error_description(g_lzh);
		zfs_close(zhp);
		return (res_ds(dataset, 0, desc ? desc : "mount failed"));
	}

	zfs_close(zhp);
	return (res_ds(dataset, 1, NULL));
}

/* Dataset-level: unmount one dataset */
nvlist_t *
zed_unmount_dataset_nvl(const char *dataset,
    uint32_t flags)
{

	if (!g_lzh || !dataset || !*dataset)
		return (res_ds(dataset, 0, "invalid arguments"));

	zfs_handle_t *zhp = zfs_open(g_lzh, dataset, ZFS_TYPE_DATASET);
	if (!zhp)
		return (res_ds(dataset, 0, "dataset not found"));

	int rc = zfs_unmount(zhp, NULL /* all mountpoints for this ds */,
	    (flags & ZUMNT_FORCE) ? B_TRUE : B_FALSE);

	if (rc != 0) {
		const char *desc = libzfs_error_description(g_lzh);
		zfs_close(zhp);
		return (res_ds(dataset, 0, desc ? desc : "unmount failed"));
	}

	zfs_close(zhp);
	return (res_ds(dataset, 1, NULL));
}
