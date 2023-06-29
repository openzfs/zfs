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
 * Copyright (c) 2011, 2018 by Delphix. All rights reserved.
 * Copyright (c) 2013 by Saso Kiselkov. All rights reserved.
 * Copyright (c) 2013, Joyent, Inc. All rights reserved.
 * Copyright (c) 2014, Nexenta Systems, Inc. All rights reserved.
 * Copyright (c) 2017, Intel Corporation.
 * Copyright (c) 2019, Klara Inc.
 * Copyright (c) 2019, Allan Jude
 */

#ifndef _KERNEL
#include <errno.h>
#include <string.h>
#include <dirent.h>
#include <search.h>
#include <sys/stat.h>
#endif
#include <sys/debug.h>
#include <sys/fs/zfs.h>
#include <sys/inttypes.h>
#include <sys/types.h>
#include <sys/param.h>
#include <sys/zfs_sysfs.h>
#include "zfeature_common.h"

/*
 * Set to disable all feature checks while opening pools, allowing pools with
 * unsupported features to be opened. Set for testing only.
 */
boolean_t zfeature_checks_disable = B_FALSE;

zfeature_info_t spa_feature_table[SPA_FEATURES];

/*
 * Valid characters for feature guids. This list is mainly for aesthetic
 * purposes and could be expanded in the future. There are different allowed
 * characters in the guids reverse dns portion (before the colon) and its
 * short name (after the colon).
 */
static int
valid_char(char c, boolean_t after_colon)
{
	return ((c >= 'a' && c <= 'z') ||
	    (c >= '0' && c <= '9') ||
	    (after_colon && c == '_') ||
	    (!after_colon && (c == '.' || c == '-')));
}

/*
 * Every feature guid must contain exactly one colon which separates a reverse
 * dns organization name from the feature's "short" name (e.g.
 * "com.company:feature_name").
 */
boolean_t
zfeature_is_valid_guid(const char *name)
{
	int i;
	boolean_t has_colon = B_FALSE;

	i = 0;
	while (name[i] != '\0') {
		char c = name[i++];
		if (c == ':') {
			if (has_colon)
				return (B_FALSE);
			has_colon = B_TRUE;
			continue;
		}
		if (!valid_char(c, has_colon))
			return (B_FALSE);
	}

	return (has_colon);
}

boolean_t
zfeature_is_supported(const char *guid)
{
	if (zfeature_checks_disable)
		return (B_TRUE);

	for (spa_feature_t i = 0; i < SPA_FEATURES; i++) {
		zfeature_info_t *feature = &spa_feature_table[i];
		if (!feature->fi_zfs_mod_supported)
			continue;
		if (strcmp(guid, feature->fi_guid) == 0)
			return (B_TRUE);
	}
	return (B_FALSE);
}

int
zfeature_lookup_guid(const char *guid, spa_feature_t *res)
{
	for (spa_feature_t i = 0; i < SPA_FEATURES; i++) {
		zfeature_info_t *feature = &spa_feature_table[i];
		if (!feature->fi_zfs_mod_supported)
			continue;
		if (strcmp(guid, feature->fi_guid) == 0) {
			if (res != NULL)
				*res = i;
			return (0);
		}
	}

	return (ENOENT);
}

int
zfeature_lookup_name(const char *name, spa_feature_t *res)
{
	for (spa_feature_t i = 0; i < SPA_FEATURES; i++) {
		zfeature_info_t *feature = &spa_feature_table[i];
		if (!feature->fi_zfs_mod_supported)
			continue;
		if (strcmp(name, feature->fi_uname) == 0) {
			if (res != NULL)
				*res = i;
			return (0);
		}
	}

	return (ENOENT);
}

boolean_t
zfeature_depends_on(spa_feature_t fid, spa_feature_t check)
{
	zfeature_info_t *feature = &spa_feature_table[fid];

	for (int i = 0; feature->fi_depends[i] != SPA_FEATURE_NONE; i++) {
		if (feature->fi_depends[i] == check)
			return (B_TRUE);
	}
	return (B_FALSE);
}

static boolean_t
deps_contains_feature(const spa_feature_t *deps, const spa_feature_t feature)
{
	for (int i = 0; deps[i] != SPA_FEATURE_NONE; i++)
		if (deps[i] == feature)
			return (B_TRUE);

	return (B_FALSE);
}

#define	STRCMP ((int(*)(const void *, const void *))&strcmp)
struct zfs_mod_supported_features {
	void *tree;
	boolean_t all_features;
};

struct zfs_mod_supported_features *
zfs_mod_list_supported(const char *scope)
{
#if defined(__FreeBSD__) || defined(_KERNEL) || defined(LIB_ZPOOL_BUILD)
	(void) scope;
	return (NULL);
#else
	struct zfs_mod_supported_features *ret = calloc(1, sizeof (*ret));
	if (ret == NULL)
		return (NULL);

	DIR *sysfs_dir = NULL;
	char path[128];

	if (snprintf(path, sizeof (path), "%s/%s",
	    ZFS_SYSFS_DIR, scope) < sizeof (path))
		sysfs_dir = opendir(path);
	if (sysfs_dir == NULL && errno == ENOENT) {
		if (snprintf(path, sizeof (path), "%s/%s",
		    ZFS_SYSFS_ALT_DIR, scope) < sizeof (path))
			sysfs_dir = opendir(path);
	}
	if (sysfs_dir == NULL) {
		ret->all_features = errno == ENOENT &&
		    (access(ZFS_SYSFS_DIR, F_OK) == 0 ||
		    access(ZFS_SYSFS_ALT_DIR, F_OK) == 0);
		return (ret);
	}

	struct dirent *node;
	while ((node = readdir(sysfs_dir)) != NULL) {
		if (strcmp(node->d_name, ".") == 0 ||
		    strcmp(node->d_name, "..") == 0)
			continue;

		char *name = strdup(node->d_name);
		if (name == NULL) {
			goto nomem;
		}

		if (tsearch(name, &ret->tree, STRCMP) == NULL) {
			/*
			 * Don't bother checking for duplicate entries:
			 * we're iterating a single directory.
			 */
			free(name);
			goto nomem;
		}
	}

end:
	closedir(sysfs_dir);
	return (ret);

nomem:
	zfs_mod_list_supported_free(ret);
	ret = NULL;
	goto end;
#endif
}

void
zfs_mod_list_supported_free(struct zfs_mod_supported_features *list)
{
#if !defined(__FreeBSD__) && !defined(_KERNEL) && !defined(LIB_ZPOOL_BUILD)
	if (list) {
		tdestroy(list->tree, free);
		free(list);
	}
#else
	(void) list;
#endif
}

#if !defined(_KERNEL) && !defined(LIB_ZPOOL_BUILD)
static boolean_t
zfs_mod_supported_impl(const char *scope, const char *name, const char *sysfs)
{
	char path[128];
	if (snprintf(path, sizeof (path), "%s%s%s%s%s", sysfs,
	    scope == NULL ? "" : "/", scope ?: "",
	    name == NULL ? "" : "/", name ?: "") < sizeof (path))
		return (access(path, F_OK) == 0);
	else
		return (B_FALSE);
}

boolean_t
zfs_mod_supported(const char *scope, const char *name,
    const struct zfs_mod_supported_features *sfeatures)
{
	boolean_t supported;

	if (sfeatures != NULL)
		return (sfeatures->all_features ||
		    tfind(name, &sfeatures->tree, STRCMP));

	/*
	 * Check both the primary and alternate sysfs locations to determine
	 * if the required functionality is supported.
	 */
	supported = (zfs_mod_supported_impl(scope, name, ZFS_SYSFS_DIR) ||
	    zfs_mod_supported_impl(scope, name, ZFS_SYSFS_ALT_DIR));

	/*
	 * For backwards compatibility with kernel modules that predate
	 * supported feature/property checking.  Report the feature/property
	 * as supported if the kernel module is loaded but the requested
	 * scope directory does not exist.
	 */
	if (supported == B_FALSE) {
		if ((access(ZFS_SYSFS_DIR, F_OK) == 0 &&
		    !zfs_mod_supported_impl(scope, NULL, ZFS_SYSFS_DIR)) ||
		    (access(ZFS_SYSFS_ALT_DIR, F_OK) == 0 &&
		    !zfs_mod_supported_impl(scope, NULL, ZFS_SYSFS_ALT_DIR))) {
			supported = B_TRUE;
		}
	}

	return (supported);
}
#endif

static boolean_t
zfs_mod_supported_feature(const char *name,
    const struct zfs_mod_supported_features *sfeatures)
{
	/*
	 * The zfs module spa_feature_table[], whether in-kernel or in
	 * libzpool, always supports all the features. libzfs needs to
	 * query the running module, via sysfs, to determine which
	 * features are supported.
	 *
	 * The equivalent _can_ be done on FreeBSD by way of the sysctl
	 * tree, but this has not been done yet.  Therefore, we return
	 * that all features are supported.
	 */

#if defined(_KERNEL) || defined(LIB_ZPOOL_BUILD) || defined(__FreeBSD__)
	(void) name, (void) sfeatures;
	return (B_TRUE);
#else
	return (zfs_mod_supported(ZFS_SYSFS_POOL_FEATURES, name, sfeatures));
#endif
}

static void
zfeature_register(spa_feature_t fid, const char *guid, const char *name,
    const char *desc, zfeature_flags_t flags, zfeature_type_t type,
    const spa_feature_t *deps,
    const struct zfs_mod_supported_features *sfeatures)
{
	zfeature_info_t *feature = &spa_feature_table[fid];
	static const spa_feature_t nodeps[] = { SPA_FEATURE_NONE };

	ASSERT(name != NULL);
	ASSERT(desc != NULL);
	ASSERT((flags & ZFEATURE_FLAG_READONLY_COMPAT) == 0 ||
	    (flags & ZFEATURE_FLAG_MOS) == 0);
	ASSERT3U(fid, <, SPA_FEATURES);
	ASSERT(zfeature_is_valid_guid(guid));

	if (deps == NULL)
		deps = nodeps;

	VERIFY(((flags & ZFEATURE_FLAG_PER_DATASET) == 0) ||
	    (deps_contains_feature(deps, SPA_FEATURE_EXTENSIBLE_DATASET)));

	feature->fi_feature = fid;
	feature->fi_guid = guid;
	feature->fi_uname = name;
	feature->fi_desc = desc;
	feature->fi_flags = flags;
	feature->fi_type = type;
	feature->fi_depends = deps;
	feature->fi_zfs_mod_supported =
	    zfs_mod_supported_feature(guid, sfeatures);
}

/*
 * Every feature has a GUID of the form com.example:feature_name.  The
 * reversed DNS name ensures that the feature's GUID is unique across all ZFS
 * implementations.  This allows companies to independently develop and
 * release features.  Examples include org.delphix and org.datto.  Previously,
 * features developed on one implementation have used that implementation's
 * domain name (e.g. org.illumos and org.zfsonlinux).  Use of the org.openzfs
 * domain name is recommended for new features which are developed by the
 * OpenZFS community and its platforms.  This domain may optionally be used by
 * companies developing features for initial release through an OpenZFS
 * implementation.  Use of the org.openzfs domain requires reserving the
 * feature name in advance with the OpenZFS project.
 */
void
zpool_feature_init(void)
{
	struct zfs_mod_supported_features *sfeatures =
	    zfs_mod_list_supported(ZFS_SYSFS_POOL_FEATURES);

	zfeature_register(SPA_FEATURE_ASYNC_DESTROY,
	    "com.delphix:async_destroy", "async_destroy",
	    "Destroy filesystems asynchronously.",
	    ZFEATURE_FLAG_READONLY_COMPAT, ZFEATURE_TYPE_BOOLEAN, NULL,
	    sfeatures);

	zfeature_register(SPA_FEATURE_EMPTY_BPOBJ,
	    "com.delphix:empty_bpobj", "empty_bpobj",
	    "Snapshots use less space.",
	    ZFEATURE_FLAG_READONLY_COMPAT, ZFEATURE_TYPE_BOOLEAN, NULL,
	    sfeatures);

	zfeature_register(SPA_FEATURE_LZ4_COMPRESS,
	    "org.illumos:lz4_compress", "lz4_compress",
	    "LZ4 compression algorithm support.",
	    ZFEATURE_FLAG_ACTIVATE_ON_ENABLE, ZFEATURE_TYPE_BOOLEAN, NULL,
	    sfeatures);

	zfeature_register(SPA_FEATURE_MULTI_VDEV_CRASH_DUMP,
	    "com.joyent:multi_vdev_crash_dump", "multi_vdev_crash_dump",
	    "Crash dumps to multiple vdev pools.",
	    0, ZFEATURE_TYPE_BOOLEAN, NULL, sfeatures);

	zfeature_register(SPA_FEATURE_SPACEMAP_HISTOGRAM,
	    "com.delphix:spacemap_histogram", "spacemap_histogram",
	    "Spacemaps maintain space histograms.",
	    ZFEATURE_FLAG_READONLY_COMPAT, ZFEATURE_TYPE_BOOLEAN, NULL,
	    sfeatures);

	zfeature_register(SPA_FEATURE_ENABLED_TXG,
	    "com.delphix:enabled_txg", "enabled_txg",
	    "Record txg at which a feature is enabled",
	    ZFEATURE_FLAG_READONLY_COMPAT, ZFEATURE_TYPE_BOOLEAN, NULL,
	    sfeatures);

	{
		static const spa_feature_t hole_birth_deps[] = {
			SPA_FEATURE_ENABLED_TXG,
			SPA_FEATURE_NONE
		};
		zfeature_register(SPA_FEATURE_HOLE_BIRTH,
		    "com.delphix:hole_birth", "hole_birth",
		    "Retain hole birth txg for more precise zfs send",
		    ZFEATURE_FLAG_MOS | ZFEATURE_FLAG_ACTIVATE_ON_ENABLE,
		    ZFEATURE_TYPE_BOOLEAN, hole_birth_deps, sfeatures);
	}

	zfeature_register(SPA_FEATURE_POOL_CHECKPOINT,
	    "com.delphix:zpool_checkpoint", "zpool_checkpoint",
	    "Pool state can be checkpointed, allowing rewind later.",
	    ZFEATURE_FLAG_READONLY_COMPAT, ZFEATURE_TYPE_BOOLEAN, NULL,
	    sfeatures);

	zfeature_register(SPA_FEATURE_SPACEMAP_V2,
	    "com.delphix:spacemap_v2", "spacemap_v2",
	    "Space maps representing large segments are more efficient.",
	    ZFEATURE_FLAG_READONLY_COMPAT | ZFEATURE_FLAG_ACTIVATE_ON_ENABLE,
	    ZFEATURE_TYPE_BOOLEAN, NULL, sfeatures);

	zfeature_register(SPA_FEATURE_EXTENSIBLE_DATASET,
	    "com.delphix:extensible_dataset", "extensible_dataset",
	    "Enhanced dataset functionality, used by other features.",
	    0, ZFEATURE_TYPE_BOOLEAN, NULL, sfeatures);

	{
		static const spa_feature_t bookmarks_deps[] = {
			SPA_FEATURE_EXTENSIBLE_DATASET,
			SPA_FEATURE_NONE
		};

		zfeature_register(SPA_FEATURE_BOOKMARKS,
		    "com.delphix:bookmarks", "bookmarks",
		    "\"zfs bookmark\" command",
		    ZFEATURE_FLAG_READONLY_COMPAT, ZFEATURE_TYPE_BOOLEAN,
		    bookmarks_deps, sfeatures);
	}

	{
		static const spa_feature_t filesystem_limits_deps[] = {
			SPA_FEATURE_EXTENSIBLE_DATASET,
			SPA_FEATURE_NONE
		};
		zfeature_register(SPA_FEATURE_FS_SS_LIMIT,
		    "com.joyent:filesystem_limits", "filesystem_limits",
		    "Filesystem and snapshot limits.",
		    ZFEATURE_FLAG_READONLY_COMPAT, ZFEATURE_TYPE_BOOLEAN,
		    filesystem_limits_deps, sfeatures);
	}

	zfeature_register(SPA_FEATURE_EMBEDDED_DATA,
	    "com.delphix:embedded_data", "embedded_data",
	    "Blocks which compress very well use even less space.",
	    ZFEATURE_FLAG_MOS | ZFEATURE_FLAG_ACTIVATE_ON_ENABLE,
	    ZFEATURE_TYPE_BOOLEAN, NULL, sfeatures);

	{
		static const spa_feature_t livelist_deps[] = {
			SPA_FEATURE_EXTENSIBLE_DATASET,
			SPA_FEATURE_NONE
		};
		zfeature_register(SPA_FEATURE_LIVELIST,
		    "com.delphix:livelist", "livelist",
		    "Improved clone deletion performance.",
		    ZFEATURE_FLAG_READONLY_COMPAT, ZFEATURE_TYPE_BOOLEAN,
		    livelist_deps, sfeatures);
	}

	{
		static const spa_feature_t log_spacemap_deps[] = {
			SPA_FEATURE_SPACEMAP_V2,
			SPA_FEATURE_NONE
		};
		zfeature_register(SPA_FEATURE_LOG_SPACEMAP,
		    "com.delphix:log_spacemap", "log_spacemap",
		    "Log metaslab changes on a single spacemap and "
		    "flush them periodically.",
		    ZFEATURE_FLAG_READONLY_COMPAT, ZFEATURE_TYPE_BOOLEAN,
		    log_spacemap_deps, sfeatures);
	}

	{
		static const spa_feature_t large_blocks_deps[] = {
			SPA_FEATURE_EXTENSIBLE_DATASET,
			SPA_FEATURE_NONE
		};
		zfeature_register(SPA_FEATURE_LARGE_BLOCKS,
		    "org.open-zfs:large_blocks", "large_blocks",
		    "Support for blocks larger than 128KB.",
		    ZFEATURE_FLAG_PER_DATASET, ZFEATURE_TYPE_BOOLEAN,
		    large_blocks_deps, sfeatures);
	}

	{
		static const spa_feature_t large_dnode_deps[] = {
			SPA_FEATURE_EXTENSIBLE_DATASET,
			SPA_FEATURE_NONE
		};
		zfeature_register(SPA_FEATURE_LARGE_DNODE,
		    "org.zfsonlinux:large_dnode", "large_dnode",
		    "Variable on-disk size of dnodes.",
		    ZFEATURE_FLAG_PER_DATASET, ZFEATURE_TYPE_BOOLEAN,
		    large_dnode_deps, sfeatures);
	}

	{
		static const spa_feature_t sha512_deps[] = {
			SPA_FEATURE_EXTENSIBLE_DATASET,
			SPA_FEATURE_NONE
		};
		zfeature_register(SPA_FEATURE_SHA512,
		    "org.illumos:sha512", "sha512",
		    "SHA-512/256 hash algorithm.",
		    ZFEATURE_FLAG_PER_DATASET, ZFEATURE_TYPE_BOOLEAN,
		    sha512_deps, sfeatures);
	}

	{
		static const spa_feature_t skein_deps[] = {
			SPA_FEATURE_EXTENSIBLE_DATASET,
			SPA_FEATURE_NONE
		};
		zfeature_register(SPA_FEATURE_SKEIN,
		    "org.illumos:skein", "skein",
		    "Skein hash algorithm.",
		    ZFEATURE_FLAG_PER_DATASET, ZFEATURE_TYPE_BOOLEAN,
		    skein_deps, sfeatures);
	}

	{
		static const spa_feature_t edonr_deps[] = {
			SPA_FEATURE_EXTENSIBLE_DATASET,
			SPA_FEATURE_NONE
		};
		zfeature_register(SPA_FEATURE_EDONR,
		    "org.illumos:edonr", "edonr",
		    "Edon-R hash algorithm.",
		    ZFEATURE_FLAG_PER_DATASET, ZFEATURE_TYPE_BOOLEAN,
		    edonr_deps, sfeatures);
	}

	{
		static const spa_feature_t redact_books_deps[] = {
			SPA_FEATURE_BOOKMARK_V2,
			SPA_FEATURE_EXTENSIBLE_DATASET,
			SPA_FEATURE_BOOKMARKS,
			SPA_FEATURE_NONE
		};
		zfeature_register(SPA_FEATURE_REDACTION_BOOKMARKS,
		    "com.delphix:redaction_bookmarks", "redaction_bookmarks",
		    "Support for bookmarks which store redaction lists for zfs "
		    "redacted send/recv.", 0, ZFEATURE_TYPE_BOOLEAN,
		    redact_books_deps, sfeatures);
	}

	{
		static const spa_feature_t redact_datasets_deps[] = {
			SPA_FEATURE_EXTENSIBLE_DATASET,
			SPA_FEATURE_NONE
		};
		zfeature_register(SPA_FEATURE_REDACTED_DATASETS,
		    "com.delphix:redacted_datasets", "redacted_datasets",
		    "Support for redacted datasets, produced by receiving "
		    "a redacted zfs send stream.",
		    ZFEATURE_FLAG_PER_DATASET, ZFEATURE_TYPE_UINT64_ARRAY,
		    redact_datasets_deps, sfeatures);
	}

	{
		static const spa_feature_t bookmark_written_deps[] = {
			SPA_FEATURE_BOOKMARK_V2,
			SPA_FEATURE_EXTENSIBLE_DATASET,
			SPA_FEATURE_BOOKMARKS,
			SPA_FEATURE_NONE
		};
		zfeature_register(SPA_FEATURE_BOOKMARK_WRITTEN,
		    "com.delphix:bookmark_written", "bookmark_written",
		    "Additional accounting, enabling the written#<bookmark> "
		    "property (space written since a bookmark), "
		    "and estimates of send stream sizes for incrementals from "
		    "bookmarks.",
		    0, ZFEATURE_TYPE_BOOLEAN, bookmark_written_deps, sfeatures);
	}

	zfeature_register(SPA_FEATURE_DEVICE_REMOVAL,
	    "com.delphix:device_removal", "device_removal",
	    "Top-level vdevs can be removed, reducing logical pool size.",
	    ZFEATURE_FLAG_MOS, ZFEATURE_TYPE_BOOLEAN, NULL, sfeatures);

	{
		static const spa_feature_t obsolete_counts_deps[] = {
			SPA_FEATURE_EXTENSIBLE_DATASET,
			SPA_FEATURE_DEVICE_REMOVAL,
			SPA_FEATURE_NONE
		};
		zfeature_register(SPA_FEATURE_OBSOLETE_COUNTS,
		    "com.delphix:obsolete_counts", "obsolete_counts",
		    "Reduce memory used by removed devices when their blocks "
		    "are freed or remapped.",
		    ZFEATURE_FLAG_READONLY_COMPAT, ZFEATURE_TYPE_BOOLEAN,
		    obsolete_counts_deps, sfeatures);
	}

	{
		static const spa_feature_t userobj_accounting_deps[] = {
			SPA_FEATURE_EXTENSIBLE_DATASET,
			SPA_FEATURE_NONE
		};
		zfeature_register(SPA_FEATURE_USEROBJ_ACCOUNTING,
		    "org.zfsonlinux:userobj_accounting", "userobj_accounting",
		    "User/Group object accounting.",
		    ZFEATURE_FLAG_READONLY_COMPAT | ZFEATURE_FLAG_PER_DATASET,
		    ZFEATURE_TYPE_BOOLEAN, userobj_accounting_deps, sfeatures);
	}

	{
		static const spa_feature_t bookmark_v2_deps[] = {
			SPA_FEATURE_EXTENSIBLE_DATASET,
			SPA_FEATURE_BOOKMARKS,
			SPA_FEATURE_NONE
		};
		zfeature_register(SPA_FEATURE_BOOKMARK_V2,
		    "com.datto:bookmark_v2", "bookmark_v2",
		    "Support for larger bookmarks",
		    0, ZFEATURE_TYPE_BOOLEAN, bookmark_v2_deps, sfeatures);
	}

	{
		static const spa_feature_t encryption_deps[] = {
			SPA_FEATURE_EXTENSIBLE_DATASET,
			SPA_FEATURE_BOOKMARK_V2,
			SPA_FEATURE_NONE
		};
		zfeature_register(SPA_FEATURE_ENCRYPTION,
		    "com.datto:encryption", "encryption",
		    "Support for dataset level encryption",
		    ZFEATURE_FLAG_PER_DATASET, ZFEATURE_TYPE_BOOLEAN,
		    encryption_deps, sfeatures);
	}

	{
		static const spa_feature_t project_quota_deps[] = {
			SPA_FEATURE_EXTENSIBLE_DATASET,
			SPA_FEATURE_NONE
		};
		zfeature_register(SPA_FEATURE_PROJECT_QUOTA,
		    "org.zfsonlinux:project_quota", "project_quota",
		    "space/object accounting based on project ID.",
		    ZFEATURE_FLAG_READONLY_COMPAT | ZFEATURE_FLAG_PER_DATASET,
		    ZFEATURE_TYPE_BOOLEAN, project_quota_deps, sfeatures);
	}

	zfeature_register(SPA_FEATURE_ALLOCATION_CLASSES,
	    "org.zfsonlinux:allocation_classes", "allocation_classes",
	    "Support for separate allocation classes.",
	    ZFEATURE_FLAG_READONLY_COMPAT, ZFEATURE_TYPE_BOOLEAN, NULL,
	    sfeatures);

	zfeature_register(SPA_FEATURE_RESILVER_DEFER,
	    "com.datto:resilver_defer", "resilver_defer",
	    "Support for deferring new resilvers when one is already running.",
	    ZFEATURE_FLAG_READONLY_COMPAT, ZFEATURE_TYPE_BOOLEAN, NULL,
	    sfeatures);

	zfeature_register(SPA_FEATURE_DEVICE_REBUILD,
	    "org.openzfs:device_rebuild", "device_rebuild",
	    "Support for sequential mirror/dRAID device rebuilds",
	    ZFEATURE_FLAG_READONLY_COMPAT, ZFEATURE_TYPE_BOOLEAN, NULL,
	    sfeatures);

	{
		static const spa_feature_t zstd_deps[] = {
			SPA_FEATURE_EXTENSIBLE_DATASET,
			SPA_FEATURE_NONE
		};
		zfeature_register(SPA_FEATURE_ZSTD_COMPRESS,
		    "org.freebsd:zstd_compress", "zstd_compress",
		    "zstd compression algorithm support.",
		    ZFEATURE_FLAG_PER_DATASET, ZFEATURE_TYPE_BOOLEAN, zstd_deps,
		    sfeatures);
	}

	zfeature_register(SPA_FEATURE_DRAID,
	    "org.openzfs:draid", "draid", "Support for distributed spare RAID",
	    ZFEATURE_FLAG_MOS, ZFEATURE_TYPE_BOOLEAN, NULL, sfeatures);

	{
		static const spa_feature_t zilsaxattr_deps[] = {
			SPA_FEATURE_EXTENSIBLE_DATASET,
			SPA_FEATURE_NONE
		};
		zfeature_register(SPA_FEATURE_ZILSAXATTR,
		    "org.openzfs:zilsaxattr", "zilsaxattr",
		    "Support for xattr=sa extended attribute logging in ZIL.",
		    ZFEATURE_FLAG_PER_DATASET | ZFEATURE_FLAG_READONLY_COMPAT,
		    ZFEATURE_TYPE_BOOLEAN, zilsaxattr_deps, sfeatures);
	}

	zfeature_register(SPA_FEATURE_HEAD_ERRLOG,
	    "com.delphix:head_errlog", "head_errlog",
	    "Support for per-dataset on-disk error logs.",
	    ZFEATURE_FLAG_ACTIVATE_ON_ENABLE, ZFEATURE_TYPE_BOOLEAN, NULL,
	    sfeatures);

	{
		static const spa_feature_t blake3_deps[] = {
			SPA_FEATURE_EXTENSIBLE_DATASET,
			SPA_FEATURE_NONE
		};
		zfeature_register(SPA_FEATURE_BLAKE3,
		    "org.openzfs:blake3", "blake3",
		    "BLAKE3 hash algorithm.",
		    ZFEATURE_FLAG_PER_DATASET, ZFEATURE_TYPE_BOOLEAN,
		    blake3_deps, sfeatures);
	}

	zfeature_register(SPA_FEATURE_BLOCK_CLONING,
	    "com.fudosecurity:block_cloning", "block_cloning",
	    "Support for block cloning via Block Reference Table.",
	    ZFEATURE_FLAG_READONLY_COMPAT, ZFEATURE_TYPE_BOOLEAN, NULL,
	    sfeatures);

	zfeature_register(SPA_FEATURE_AVZ_V2,
	    "com.klarasystems:vdev_zaps_v2", "vdev_zaps_v2",
	    "Support for root vdev ZAP.",
	    ZFEATURE_FLAG_MOS, ZFEATURE_TYPE_BOOLEAN, NULL,
	    sfeatures);

	{
		static const spa_feature_t redact_list_spill_deps[] = {
			SPA_FEATURE_REDACTION_BOOKMARKS,
			SPA_FEATURE_NONE
		};
		zfeature_register(SPA_FEATURE_REDACTION_LIST_SPILL,
		    "com.delphix:redaction_list_spill", "redaction_list_spill",
		    "Support for increased number of redaction_snapshot "
		    "arguments in zfs redact.", 0, ZFEATURE_TYPE_BOOLEAN,
		    redact_list_spill_deps, sfeatures);
	}

	zfeature_register(SPA_FEATURE_RAIDZ_EXPANSION,
	    "org.openzfs:raidz_expansion", "raidz_expansion",
	    "Support for raidz expansion",
	    ZFEATURE_FLAG_MOS, ZFEATURE_TYPE_BOOLEAN, NULL, sfeatures);

	zfs_mod_list_supported_free(sfeatures);
}

#if defined(_KERNEL)
EXPORT_SYMBOL(zfeature_lookup_guid);
EXPORT_SYMBOL(zfeature_lookup_name);
EXPORT_SYMBOL(zfeature_is_supported);
EXPORT_SYMBOL(zfeature_is_valid_guid);
EXPORT_SYMBOL(zfeature_depends_on);
EXPORT_SYMBOL(zpool_feature_init);
EXPORT_SYMBOL(spa_feature_table);
#endif
