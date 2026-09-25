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
#include <stdint.h>
#include <stdio.h>

#include <libzfs.h>
#include <sys/nvpair.h>

#include "memfile.h"
#include "ops_export.h"
#include "pipe_rpc.h"

/* Global libzfs handle provided by the service */
extern libzfs_handle_t *g_lzh;

static void
add_ok_err(nvlist_t *dst, int ok, const char *name, const char *err)
{
	fnvlist_add_boolean_value(dst, "ok", ok ? B_TRUE : B_FALSE);
	if (name) fnvlist_add_string(dst, "name", name);
	if (!ok && err) fnvlist_add_string(dst, "err", err);
}

/* Export one pool handle with flags mapped to your tree's API */
static int
export_one_handle(zpool_handle_t *zhp, uint32_t flags)
{
	int rc = -1;

//	rc = zpool_disable_datasets(zhp, B_FALSE);
//	if (rc)
//		return (rc);

	rc = zpool_export(zhp,
	    (flags & ZEXP_FORCE) ? B_TRUE : B_FALSE,
	    (flags & ZEXP_HARD) ? B_TRUE : B_FALSE);

	return (rc);
}

/* ---------- zpool_iter callbacks (pure C) ---------- */

typedef struct {
    uint64_t want_guid;
    zpool_handle_t *found; /* OUT: matching handle (do not close) */
} find_by_guid_ctx_t;

static int
find_by_guid_cb(zpool_handle_t *zhp, void *data)
{
	find_by_guid_ctx_t *ctx = (find_by_guid_ctx_t *)data;
	nvlist_t *cfg = zpool_get_config(zhp, NULL);
	uint64_t g = 0;

	if (cfg != NULL)
		(void) nvlist_lookup_uint64(cfg, ZPOOL_CONFIG_POOL_GUID, &g);

	if (g == ctx->want_guid) {
		ctx->found = zhp;
		return (1); /* stop iteration */
	}

	/* not a match, close and continue */
	zpool_close(zhp);
	return (0);
}

/* ---------- Public JSON builders ---------- */

char *
zed_export_one_json(uint32_t flags, uint64_t guid, size_t *out_len)
{
	*out_len = 0;
	nvlist_t *res = fnvlist_alloc();
	add_ok_err(res, 0, NULL, NULL);

	find_by_guid_ctx_t ctx;
	ctx.want_guid = guid;
	ctx.found = NULL;

	(void) zpool_iter(g_lzh, find_by_guid_cb, &ctx);

	if (ctx.found == NULL) {
		add_ok_err(res, 0, NULL, "not imported");
	} else {
		const char *name = zpool_get_name(ctx.found);
		dprintf("export_one: exporting guid=%llu name=%s flags=0x%x\n",
		    (unsigned long long)guid, name ? name : "(null)", flags);

		int rc = export_one_handle(ctx.found, flags);

		/*
		 * In many trees, zpool_export*() closes the handle internally.
		 * If your tree returns with a still-open handle, you can call:
		 *   zpool_close(ctx.found);
		 * here safely.
		 */

		if (rc == 0) {
			add_ok_err(res, 1, name, NULL);
		} else {
			const char *desc = libzfs_error_description(g_lzh);
			add_ok_err(res, 0, name, desc ? desc : "export failed");
		}
	}

	memfile_t mf;
	FILE *fp = memfile_open(&mf);
	nvlist_print_json(fp, res);
	fclose(fp);
	fnvlist_free(res);
	return (memfile_take(&mf, out_len));
}

typedef struct {
    libzfs_handle_t *lzh;
    nvlist_t *root; /* holds "exported" and "errors" */
    uint32_t flags;
} export_all_ctx_t;

static int
export_all_cb(zpool_handle_t *zhp, void *data)
{
	export_all_ctx_t *ctx = (export_all_ctx_t *)data;
	const char *name = zpool_get_name(zhp);

	dprintf("export_all: exporting %s flags=0x%x\n",
	    name ? name : "(null)", ctx->flags);

	int rc = export_one_handle(zhp, ctx->flags);

	if (rc == 0) {
		if (name) {
			const char *one[1] = { name };
			fnvlist_add_string_array(ctx->root, "exported", one, 1);
		}
	} else {
		nvlist_t *e = fnvlist_alloc();
		if (name) fnvlist_add_string(e, "name", name);
		{
			const char *desc = libzfs_error_description(ctx->lzh);
			fnvlist_add_string(e, "err",
			    desc ? desc : "export failed");
		}
		const nvlist_t *onee[1] = { e };
		fnvlist_add_nvlist_array(ctx->root, "errors", onee, 1);
		fnvlist_free(e);
	}

	/* Keep iterating; export typically handles closing as needed. */
	return (0);
}

char *
zed_export_all_json(uint32_t flags, size_t *out_len)
{
	*out_len = 0;
	nvlist_t *root = fnvlist_alloc();
	fnvlist_add_string_array(root, "exported", NULL, 0);
	fnvlist_add_nvlist_array(root, "errors", NULL, 0);

	export_all_ctx_t ctx;
	ctx.lzh = g_lzh;
	ctx.root = root;
	ctx.flags = flags;

	(void) zpool_iter(g_lzh, export_all_cb, &ctx);

	memfile_t mf;
	FILE *fp = memfile_open(&mf);
	nvlist_print_json(fp, root);
	fclose(fp);
	fnvlist_free(root);
	return (memfile_take(&mf, out_len));
}
