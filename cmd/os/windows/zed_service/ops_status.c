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
 * Copyright (c) 2025 Jorgen Lundman <lundman@lundman.net>.
 */

#define	_CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <stdio.h>
#include <string.h>

#include <libzfs.h>
#include <sys/nvpair.h>
#include "memfile.h"

// Include after libzfs for dprintf
#include "pipe_rpc.h"

extern libzfs_handle_t *g_lzh;

#define	POOL_NAME_ONLY (1<<0)
#define	POOL_NAME_GUID (1<<1)
#define	POOL_INCLUDE_VDEVS (1<<2)

typedef struct {
    nvlist_t *root;
    uint64_t flags;
    uint64_t  guid_filter;   // 0 = no filter
    int count;
} iter_ctx_t;

static void
add_prop_str(zpool_handle_t *zhp, zpool_prop_t prop, const char *key,
    nvlist_t *dst)
{
	char buf[128] = {0};
	(void) zpool_get_prop(zhp, prop, buf, sizeof (buf), NULL, B_FALSE);
	fnvlist_add_string(dst, key, buf);
}

static nvlist_t *
emit_pool_nv(zpool_handle_t *zhp, unsigned flags)
{
	nvlist_t *p = fnvlist_alloc();

	const char *name = zpool_get_name(zhp);
	fnvlist_add_string(p, "name", name);

	if (flags & POOL_NAME_ONLY)
		return (p);  // done

	uint64_t pg = 0;
	nvlist_t *cfg = zpool_get_config(zhp, NULL);
	if (cfg) (void)nvlist_lookup_uint64(cfg, ZPOOL_CONFIG_POOL_GUID, &pg);

	// emit GUID as string for safety when consumed elsewhere
	char guid_str[32];
	_snprintf_s(guid_str, sizeof (guid_str), _TRUNCATE, "%llu",
	    (unsigned long long)pg);
	fnvlist_add_string(p, "guid", guid_str);

	if (flags & POOL_NAME_GUID)
		return (p);  // done

	if (!(flags & POOL_NAME_ONLY)) {
		char buf[128];
		zpool_get_prop(zhp, ZPOOL_PROP_HEALTH, buf, sizeof (buf), NULL,
		    B_FALSE);
		fnvlist_add_string(p, "health", buf);
		zpool_get_prop(zhp, ZPOOL_PROP_SIZE, buf, sizeof (buf), NULL,
		    B_FALSE);
		fnvlist_add_string(p, "size", buf);
		zpool_get_prop(zhp, ZPOOL_PROP_ALLOCATED, buf, sizeof (buf),
		    NULL, B_FALSE);
		fnvlist_add_string(p, "alloc", buf);
		zpool_get_prop(zhp, ZPOOL_PROP_FREE, buf, sizeof (buf), NULL,
		    B_FALSE);
		fnvlist_add_string(p, "free", buf);
		zpool_get_prop(zhp, ZPOOL_PROP_CAPACITY, buf, sizeof (buf),
		    NULL, B_FALSE);
		fnvlist_add_string(p, "capacity_pct", buf);

		if ((flags & POOL_INCLUDE_VDEVS) && cfg) {
			nvlist_t *vdt = NULL;
			if (nvlist_lookup_nvlist(cfg,
			    ZPOOL_CONFIG_VDEV_TREE, &vdt) == 0)
				fnvlist_add_nvlist(p, "vdev_tree",
				    fnvlist_dup(vdt));
		}
	}
	return (p);
}

static int
add_pool_cb(zpool_handle_t *zhp, void *cookie)
{
	iter_ctx_t *ctx = (iter_ctx_t *)cookie;

	// guid filter (fast)
	if (ctx->guid_filter != 0) {
		uint64_t have = 0;
		nvlist_t *cfg = zpool_get_config(zhp, NULL);
		if (cfg)
			(void) nvlist_lookup_uint64(cfg,
			    ZPOOL_CONFIG_POOL_GUID, &have);
		if (have != ctx->guid_filter) {
			zpool_close(zhp);
			return (0);
		}
	}

	nvlist_t *p = emit_pool_nv(zhp, ctx->flags);
	const nvlist_t *arr[1] = { p };
	fnvlist_add_nvlist_array(ctx->root, "pools", arr, 1);
	fnvlist_free(p);

	ctx->count++;
	zpool_close(zhp);
	return ((ctx->guid_filter != 0) ? 1 : 0);
}

static nvlist_t *
build_status_nvlist(uint64_t flags)
{
	nvlist_t *root = fnvlist_alloc();
	// Initialize empty array so appends work
	fnvlist_add_nvlist_array(root, "pools", NULL, 0);

	if (g_lzh) {
		iter_ctx_t ctx = { root, flags, 0, 0 };
		(void) zpool_iter(g_lzh, add_pool_cb, &ctx);
	}
	return (root);
}

char *
zed_status_json_build_by_guid(uint64_t guid, zfs_status_verbosity_t verb,
    size_t *out_len)
{
	nvlist_t *root = fnvlist_alloc();
	fnvlist_add_nvlist_array(root, "pools", NULL, 0);

	unsigned flags = 0;
	if (verb >= ZFSV_INCLUDE_VDEVS)
		flags |= POOL_INCLUDE_VDEVS;

	if (g_lzh) {
		iter_ctx_t ctx = { root, flags, guid, 0 };
		(void) zpool_iter(g_lzh, add_pool_cb, &ctx);
	}

	memfile_t mf;
	FILE *fp = memfile_open(&mf);
	if (!fp) {
		fnvlist_free(root);
		return (NULL);
	}
	nvlist_print_json(fp, root);
	fclose(fp);
	fnvlist_free(root);
	return (memfile_take(&mf, out_len));
}

char *
zed_status_json_build(size_t *out_len)
{
	nvlist_t *root = build_status_nvlist(0);
	if (!root)
		return (NULL);

	memfile_t mf;
	FILE *fp = memfile_open(&mf);

	if (!fp) {
		fnvlist_free(root);
		return (NULL);
	}

	nvlist_print_json(fp, root); // emits JSON into mf

	fclose(fp); // triggers memfile_close -> NUL-terminate

	fnvlist_free(root);

	char *json = memfile_take(&mf, out_len);
	return (json); // HeapAlloc’d; caller frees
}

char *
zed_list_json_build(size_t *out_len)
{

	dprintf("%s: \n", __func__);
	nvlist_t *root = build_status_nvlist(POOL_NAME_GUID);
	if (!root)
		return (NULL);
	dprintf("%s: 2\n", __func__);

	memfile_t mf;
	FILE *fp = memfile_open(&mf);

	if (!fp) {
		fnvlist_free(root);
		return (NULL);
	}

	nvlist_print_json(fp, root); // emits JSON into mf

	fclose(fp); // triggers memfile_close -> NUL-terminate

	fnvlist_free(root);

	char *json = memfile_take(&mf, out_len);
	return (json); // HeapAlloc’d; caller frees
}
