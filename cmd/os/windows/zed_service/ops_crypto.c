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
#include <string.h>
#include <stdio.h>

#include <libzfs.h>
#include <libzfs_core.h>

#include <sys/nvpair.h>

#include "ops_common.h"   // dprintf, RESP helpers if needed
#include "ops_crypto.h"
#include "pipe_rpc.h"

/* --- small helpers ----------------------------------------------------- */

static nvlist_t *
res_scope_pool(const char *pool)
{
	nvlist_t *n = fnvlist_alloc();
	fnvlist_add_boolean_value(n, "ok", B_TRUE);
	fnvlist_add_string(n, "scope", "pool");
	if (pool) fnvlist_add_string(n, "pool", pool);
	/* seed empty array so clients can rely on the key existing */
	fnvlist_add_nvlist_array(n, "locked", NULL, 0);
	return (n);
}

static nvlist_t *
res_scope_ds(const char *ds)
{
	nvlist_t *n = fnvlist_alloc();
	fnvlist_add_boolean_value(n, "ok", B_TRUE);
	fnvlist_add_string(n, "scope", "dataset");
	if (ds) fnvlist_add_string(n, "dataset", ds);
	fnvlist_add_nvlist_array(n, "locked", NULL, 0);
	return (n);
}

static int
get_num_prop(zfs_handle_t *zhp, zfs_prop_t prop, uint64_t *out)
{
	nvlist_t *user = NULL;
	char buf[ZFS_MAXPROPLEN];
	if (zfs_prop_get_numeric(zhp, prop, out,
	    (zprop_source_t *)buf, NULL, 0) == 0)
		return (0);
	return (-1);
}

static int
get_str_prop(zfs_handle_t *zhp, zfs_prop_t prop, char *dst, size_t dstsz)
{
	zprop_source_t src;
	if (zfs_prop_get(zhp, prop, dst, dstsz, &src, NULL, 0, B_TRUE) == 0)
		return (0);
	dst[0] = '\0';
	return (-1);
}

static int
keylocation_is_prompt(const char *kloc)
{
	if (!kloc || !kloc[0])
		return (0);
	/* upstream uses literal "prompt" */
	return (_stricmp(kloc, "prompt") == 0);
}

/* Resolve encryption root into eroot[]; returns 1 if name */
static int
get_encryption_root_str(zfs_handle_t *zhp, char *eroot, size_t eroot_sz)
{
	eroot[0] = '\0';

	boolean_t isroot = B_FALSE;
	if (zfs_crypto_get_encryption_root(zhp, &isroot, eroot) == 0) {
		if (eroot[0] != '\0')
			return (1);
	}
	return (0);
}

/* Append one {"name","keyformat","keylocation"} to out["locked"] */
static void
append_locked(nvlist_t *out, const char *name, const char *keyformat,
    const char *keylocation)
{
	nvlist_t *one = fnvlist_alloc();
	fnvlist_add_string(one, "name", name);
	if (keyformat) fnvlist_add_string(one, "keyformat", keyformat);
	if (keylocation) fnvlist_add_string(one, "keylocation", keylocation);

	const nvlist_t *arr[1] = { one };
	fnvlist_add_nvlist_array(out, "locked", arr, 1);
	fnvlist_free(one);
}

/* Decide if an enc root needs a passphrase prompt */
static int
encroot_needs_prompt(zfs_handle_t *rhp)
{
	uint64_t enc = 0, ks = 0, kf = 0;
	char kloc[ZFS_MAXPROPLEN] = { 0 };

	if (get_num_prop(rhp, ZFS_PROP_ENCRYPTION, &enc) != 0)
		return (0);
	if (enc == 0)
		return (0); /* not encrypted */

	(void) get_num_prop(rhp, ZFS_PROP_KEYSTATUS, &ks);
	(void) get_num_prop(rhp, ZFS_PROP_KEYFORMAT, &kf);
	(void) get_str_prop(rhp, ZFS_PROP_KEYLOCATION, kloc, sizeof (kloc));

	/*
	 * Needs prompt when:
	 * locked (UNAVAILABLE) &&
	 * passphrase &&
	 * keylocation=prompt
	 */
	if (ks == ZFS_KEYSTATUS_UNAVAILABLE &&
	    kf == ZFS_KEYFORMAT_PASSPHRASE &&
	    keylocation_is_prompt(kloc))
		return (1);

	// Check if we should load keys that are not prompt. (file:// etc)
	if (ks == ZFS_KEYSTATUS_UNAVAILABLE &&
	    !keylocation_is_prompt(kloc)) {
		int rc;
		rc = zfs_crypto_load_key(rhp, B_FALSE, NULL);

		if (rc != 0) {
			const char *desc = libzfs_error_description(
			    zfs_get_handle(rhp));
			dprintf("failure %s\n", desc ? desc : "");
		}

	}

	return (0);
}

/* --- dataset-scope ----------------------------------------------------- */

nvlist_t *
zed_mount_preflight_dataset_nvl(libzfs_handle_t *lzh, const char *dataset)
{
	nvlist_t *n = res_scope_ds(dataset);

	if (!lzh || !dataset || !dataset[0]) {
		return (n); /* ok:true, locked:[] */
	}

	zfs_handle_t *zhp = zfs_open(lzh, dataset, ZFS_TYPE_DATASET);
	if (!zhp) {
		/*
		 * dataset missing or not imported;
		 * keep ok:true with empty locked
		 */
		return (n);
	}

	/* Report ds_type for UI */
	zfs_type_t t = zfs_get_type(zhp);
	if (t == ZFS_TYPE_VOLUME) {
		fnvlist_add_string(n, "ds_type", "volume");
		zfs_close(zhp);
		return (n);
	} else if (t == ZFS_TYPE_SNAPSHOT) {
		fnvlist_add_string(n, "ds_type", "snapshot");
		zfs_close(zhp);
		return (n);
	} else {
		fnvlist_add_string(n, "ds_type", "filesystem");
	}

	/* Find encryption root name for this dataset */
	char eroot[ZFS_MAX_DATASET_NAME_LEN] = { 0 };
	(void) get_encryption_root_str(zhp, eroot, sizeof (eroot));
	if (eroot[0]) fnvlist_add_string(n, "encroot", eroot);

	zfs_handle_t *rhp = zfs_open(lzh,
	    eroot[0] ? eroot : dataset, ZFS_TYPE_FILESYSTEM);
	if (rhp) {
		if (encroot_needs_prompt(rhp)) {
			append_locked(n, zfs_get_name(rhp),
			    "passphrase", "prompt");
		}
		zfs_close(rhp);
	}

	zfs_close(zhp);
	return (n);
}

/* --- pool-scope walk --------------------------------------------------- */

typedef struct lockscan_ctx {
    libzfs_handle_t *lzh;
    nvlist_t *out;  /* has "locked" array */
    int count;
} lockscan_ctx_t;

static int
lockscan_cb(zfs_handle_t *zhp, void *arg)
{
	lockscan_ctx_t *ctx = (lockscan_ctx_t *)arg;

	/* Only filesystems matter for mounting */
	if (zfs_get_type(zhp) != ZFS_TYPE_FILESYSTEM) {
		return (0);
	}

	char eroot[ZFS_MAX_DATASET_NAME_LEN] = { 0 };
	(void) get_encryption_root_str(zhp, eroot, sizeof (eroot));

	zfs_handle_t *rhp = zfs_open(ctx->lzh,
	    eroot[0] ? eroot : zfs_get_name(zhp), ZFS_TYPE_FILESYSTEM);
	if (rhp) {
		if (encroot_needs_prompt(rhp)) {
			append_locked(ctx->out, zfs_get_name(rhp),
			    "passphrase", "prompt");
			ctx->count++;
			dprintf("lockscan_cb: found locked encroot %s\n",
			    zfs_get_name(rhp));
		}
		zfs_close(rhp);
	}

	/* Recurse into children */
	(void) zfs_iter_filesystems(zhp, lockscan_cb, ctx);
	return (0);
}

nvlist_t *
zed_mount_preflight_pool_nvl(libzfs_handle_t *lzh, const char *pool_name)
{
	nvlist_t *out = res_scope_pool(pool_name);

	if (!lzh || !pool_name || !pool_name[0]) {
		return (out);
	}

	/* Open the pool's root filesystem (pool name itself) */
	zfs_handle_t *rootfs = zfs_open(lzh, pool_name, ZFS_TYPE_FILESYSTEM);
	if (!rootfs) {
		/* If the root dataset isn't a filesystem?? */
		return (out);
	}

	lockscan_ctx_t ctx = { lzh, out, 0 };
	(void) lockscan_cb(rootfs, &ctx); /* walk tree */
	zfs_close(rootfs);

	return (out);
}

/* result helpers */
static nvlist_t *
res_ok(const char *ds)
{
	nvlist_t *n = fnvlist_alloc();
	fnvlist_add_boolean_value(n, "ok", B_TRUE);
	if (ds) fnvlist_add_string(n, "dataset", ds);
	return (n);
}

static nvlist_t *
res_err(libzfs_handle_t *lzh, const char *ds, const char *fallback)
{
	nvlist_t *n = fnvlist_alloc();
	fnvlist_add_boolean_value(n, "ok", B_FALSE);
	if (ds) fnvlist_add_string(n, "dataset", ds);
	const char *desc = lzh ? libzfs_error_description(lzh) : NULL;
	fnvlist_add_string(n, "err", desc ? desc :
	    (fallback ? fallback : "operation failed"));
	return (n);
}

/*
 * Some trees require a pass buffer; others can resolve
 * keylocation=file:// and ignore pass.
 * We try the "with buffer" form first, then fall back
 * to "no buffer" if available.
 */
nvlist_t *
zed_load_key_one_nvl(libzfs_handle_t *lzh,
    const char *dataset_utf8,
    const uint8_t *pass,
    uint32_t passlen)
{
	if (!lzh || !dataset_utf8 || !dataset_utf8[0])
		return (res_err(lzh, dataset_utf8, "invalid arguments"));

	zfs_handle_t *zhp = zfs_open(lzh, dataset_utf8,
	    ZFS_TYPE_FILESYSTEM | ZFS_TYPE_VOLUME);
	if (!zhp)
		return (res_err(lzh, dataset_utf8,
		    "dataset not found or not a filesystem"));

	int rc = -1;

	rc = zfs_crypto_load_key_direct(zhp, B_FALSE, pass, passlen);
	dprintf("zfs_crypto_load_key_direct said %d\n", rc);
	if (rc != 0) {
		const char *desc = libzfs_error_description(lzh);
		dprintf("failure %s\n", desc ? desc : "");
		nvlist_t *err = res_err(lzh, dataset_utf8, "load-key failed");
		zfs_close(zhp);
		return (err);
	}

	zfs_close(zhp);
	return (res_ok(dataset_utf8));
}
