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
 * Copyright (c) 2005, 2010, Oracle and/or its affiliates. All rights reserved.
 */

#include <ctype.h>
#include <errno.h>
#include <libintl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <unistd.h>
#include <stddef.h>
#include <zone.h>
#include <fcntl.h>
#include <sys/mntent.h>
#include <sys/mount.h>
#include <priv.h>
#include <pwd.h>
#include <grp.h>
#include <stddef.h>
#include <ucred.h>
#include <idmap.h>
#include <aclutils.h>
#include <directory.h>

#include <sys/dnode.h>
#include <sys/spa.h>
#include <sys/zap.h>
#include <libzfs.h>

#include "zfs_namecheck.h"
#include "zfs_prop.h"
#include "libzfs_impl.h"
#include "zfs_deleg.h"

static int userquota_propname_decode(const char *propname, boolean_t zoned,
    zfs_userquota_prop_t *typep, char *domain, int domainlen, uint64_t *ridp);

/*
 * Given a single type (not a mask of types), return the type in a human
 * readable form.
 */
const char *
zfs_type_to_name(zfs_type_t type)
{
	switch (type) {
	case ZFS_TYPE_FILESYSTEM:
		return (dgettext(TEXT_DOMAIN, "filesystem"));
	case ZFS_TYPE_SNAPSHOT:
		return (dgettext(TEXT_DOMAIN, "snapshot"));
	case ZFS_TYPE_VOLUME:
		return (dgettext(TEXT_DOMAIN, "volume"));
	}

	return (NULL);
}

/*
 * Given a path and mask of ZFS types, return a string describing this dataset.
 * This is used when we fail to open a dataset and we cannot get an exact type.
 * We guess what the type would have been based on the path and the mask of
 * acceptable types.
 */
static const char *
path_to_str(const char *path, int types)
{
	/*
	 * When given a single type, always report the exact type.
	 */
	if (types == ZFS_TYPE_SNAPSHOT)
		return (dgettext(TEXT_DOMAIN, "snapshot"));
	if (types == ZFS_TYPE_FILESYSTEM)
		return (dgettext(TEXT_DOMAIN, "filesystem"));
	if (types == ZFS_TYPE_VOLUME)
		return (dgettext(TEXT_DOMAIN, "volume"));

	/*
	 * The user is requesting more than one type of dataset.  If this is the
	 * case, consult the path itself.  If we're looking for a snapshot, and
	 * a '@' is found, then report it as "snapshot".  Otherwise, remove the
	 * snapshot attribute and try again.
	 */
	if (types & ZFS_TYPE_SNAPSHOT) {
		if (strchr(path, '@') != NULL)
			return (dgettext(TEXT_DOMAIN, "snapshot"));
		return (path_to_str(path, types & ~ZFS_TYPE_SNAPSHOT));
	}

	/*
	 * The user has requested either filesystems or volumes.
	 * We have no way of knowing a priori what type this would be, so always
	 * report it as "filesystem" or "volume", our two primitive types.
	 */
	if (types & ZFS_TYPE_FILESYSTEM)
		return (dgettext(TEXT_DOMAIN, "filesystem"));

	assert(types & ZFS_TYPE_VOLUME);
	return (dgettext(TEXT_DOMAIN, "volume"));
}

/*
 * Validate a ZFS path.  This is used even before trying to open the dataset, to
 * provide a more meaningful error message.  We call zfs_error_aux() to
 * explain exactly why the name was not valid.
 */
int
zfs_validate_name(libzfs_handle_t *hdl, const char *path, int type,
    boolean_t modifying)
{
	namecheck_err_t why;
	char what;

	if (dataset_namecheck(path, &why, &what) != 0) {
		if (hdl != NULL) {
			switch (why) {
			case NAME_ERR_TOOLONG:
				zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
				    "name is too long"));
				break;

			case NAME_ERR_LEADING_SLASH:
				zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
				    "leading slash in name"));
				break;

			case NAME_ERR_EMPTY_COMPONENT:
				zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
				    "empty component in name"));
				break;

			case NAME_ERR_TRAILING_SLASH:
				zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
				    "trailing slash in name"));
				break;

			case NAME_ERR_INVALCHAR:
				zfs_error_aux(hdl,
				    dgettext(TEXT_DOMAIN, "invalid character "
				    "'%c' in name"), what);
				break;

			case NAME_ERR_MULTIPLE_AT:
				zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
				    "multiple '@' delimiters in name"));
				break;

			case NAME_ERR_NOLETTER:
				zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
				    "pool doesn't begin with a letter"));
				break;

			case NAME_ERR_RESERVED:
				zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
				    "name is reserved"));
				break;

			case NAME_ERR_DISKLIKE:
				zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
				    "reserved disk name"));
				break;
			}
		}

		return (0);
	}

	if (!(type & ZFS_TYPE_SNAPSHOT) && strchr(path, '@') != NULL) {
		if (hdl != NULL)
			zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
			    "snapshot delimiter '@' in filesystem name"));
		return (0);
	}

	if (type == ZFS_TYPE_SNAPSHOT && strchr(path, '@') == NULL) {
		if (hdl != NULL)
			zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
			    "missing '@' delimiter in snapshot name"));
		return (0);
	}

	if (modifying && strchr(path, '%') != NULL) {
		if (hdl != NULL)
			zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
			    "invalid character %c in name"), '%');
		return (0);
	}

	return (-1);
}

int
zfs_name_valid(const char *name, zfs_type_t type)
{
	if (type == ZFS_TYPE_POOL)
		return (zpool_name_valid(NULL, B_FALSE, name));
	return (zfs_validate_name(NULL, name, type, B_FALSE));
}

/*
 * This function takes the raw DSL properties, and filters out the user-defined
 * properties into a separate nvlist.
 */
static nvlist_t *
process_user_props(zfs_handle_t *zhp, nvlist_t *props)
{
	libzfs_handle_t *hdl = zhp->zfs_hdl;
	nvpair_t *elem;
	nvlist_t *propval;
	nvlist_t *nvl;

	if (nvlist_alloc(&nvl, NV_UNIQUE_NAME, 0) != 0) {
		(void) no_memory(hdl);
		return (NULL);
	}

	elem = NULL;
	while ((elem = nvlist_next_nvpair(props, elem)) != NULL) {
		if (!zfs_prop_user(nvpair_name(elem)))
			continue;

		verify(nvpair_value_nvlist(elem, &propval) == 0);
		if (nvlist_add_nvlist(nvl, nvpair_name(elem), propval) != 0) {
			nvlist_free(nvl);
			(void) no_memory(hdl);
			return (NULL);
		}
	}

	return (nvl);
}

static zpool_handle_t *
zpool_add_handle(zfs_handle_t *zhp, const char *pool_name)
{
	libzfs_handle_t *hdl = zhp->zfs_hdl;
	zpool_handle_t *zph;

	if ((zph = zpool_open_canfail(hdl, pool_name)) != NULL) {
		if (hdl->libzfs_pool_handles != NULL)
			zph->zpool_next = hdl->libzfs_pool_handles;
		hdl->libzfs_pool_handles = zph;
	}
	return (zph);
}

static zpool_handle_t *
zpool_find_handle(zfs_handle_t *zhp, const char *pool_name, int len)
{
	libzfs_handle_t *hdl = zhp->zfs_hdl;
	zpool_handle_t *zph = hdl->libzfs_pool_handles;

	while ((zph != NULL) &&
	    (strncmp(pool_name, zpool_get_name(zph), len) != 0))
		zph = zph->zpool_next;
	return (zph);
}

/*
 * Returns a handle to the pool that contains the provided dataset.
 * If a handle to that pool already exists then that handle is returned.
 * Otherwise, a new handle is created and added to the list of handles.
 */
static zpool_handle_t *
zpool_handle(zfs_handle_t *zhp)
{
	char *pool_name;
	int len;
	zpool_handle_t *zph;

	len = strcspn(zhp->zfs_name, "/@") + 1;
	pool_name = zfs_alloc(zhp->zfs_hdl, len);
	(void) strlcpy(pool_name, zhp->zfs_name, len);

	zph = zpool_find_handle(zhp, pool_name, len);
	if (zph == NULL)
		zph = zpool_add_handle(zhp, pool_name);

	free(pool_name);
	return (zph);
}

void
zpool_free_handles(libzfs_handle_t *hdl)
{
	zpool_handle_t *next, *zph = hdl->libzfs_pool_handles;

	while (zph != NULL) {
		next = zph->zpool_next;
		zpool_close(zph);
		zph = next;
	}
	hdl->libzfs_pool_handles = NULL;
}

/*
 * Utility function to gather stats (objset and zpl) for the given object.
 */
static int
get_stats_ioctl(zfs_handle_t *zhp, zfs_cmd_t *zc)
{
	libzfs_handle_t *hdl = zhp->zfs_hdl;

	(void) strlcpy(zc->zc_name, zhp->zfs_name, sizeof (zc->zc_name));

	while (ioctl(hdl->libzfs_fd, ZFS_IOC_OBJSET_STATS, zc) != 0) {
		if (errno == ENOMEM) {
			if (zcmd_expand_dst_nvlist(hdl, zc) != 0) {
				return (-1);
			}
		} else {
			return (-1);
		}
	}
	return (0);
}

/*
 * Utility function to get the received properties of the given object.
 */
static int
get_recvd_props_ioctl(zfs_handle_t *zhp)
{
	libzfs_handle_t *hdl = zhp->zfs_hdl;
	nvlist_t *recvdprops;
	zfs_cmd_t zc = { 0 };
	int err;

	if (zcmd_alloc_dst_nvlist(hdl, &zc, 0) != 0)
		return (-1);

	(void) strlcpy(zc.zc_name, zhp->zfs_name, sizeof (zc.zc_name));

	while (ioctl(hdl->libzfs_fd, ZFS_IOC_OBJSET_RECVD_PROPS, &zc) != 0) {
		if (errno == ENOMEM) {
			if (zcmd_expand_dst_nvlist(hdl, &zc) != 0) {
				return (-1);
			}
		} else {
			zcmd_free_nvlists(&zc);
			return (-1);
		}
	}

	err = zcmd_read_dst_nvlist(zhp->zfs_hdl, &zc, &recvdprops);
	zcmd_free_nvlists(&zc);
	if (err != 0)
		return (-1);

	nvlist_free(zhp->zfs_recvd_props);
	zhp->zfs_recvd_props = recvdprops;

	return (0);
}

static int
put_stats_zhdl(zfs_handle_t *zhp, zfs_cmd_t *zc)
{
	nvlist_t *allprops, *userprops;

	zhp->zfs_dmustats = zc->zc_objset_stats; /* structure assignment */

	if (zcmd_read_dst_nvlist(zhp->zfs_hdl, zc, &allprops) != 0) {
		return (-1);
	}

	/*
	 * XXX Why do we store the user props separately, in addition to
	 * storing them in zfs_props?
	 */
	if ((userprops = process_user_props(zhp, allprops)) == NULL) {
		nvlist_free(allprops);
		return (-1);
	}

	nvlist_free(zhp->zfs_props);
	nvlist_free(zhp->zfs_user_props);

	zhp->zfs_props = allprops;
	zhp->zfs_user_props = userprops;

	return (0);
}

static int
get_stats(zfs_handle_t *zhp)
{
	int rc = 0;
	zfs_cmd_t zc = { 0 };

	if (zcmd_alloc_dst_nvlist(zhp->zfs_hdl, &zc, 0) != 0)
		return (-1);
	if (get_stats_ioctl(zhp, &zc) != 0)
		rc = -1;
	else if (put_stats_zhdl(zhp, &zc) != 0)
		rc = -1;
	zcmd_free_nvlists(&zc);
	return (rc);
}

/*
 * Refresh the properties currently stored in the handle.
 */
void
zfs_refresh_properties(zfs_handle_t *zhp)
{
	(void) get_stats(zhp);
}

/*
 * Makes a handle from the given dataset name.  Used by zfs_open() and
 * zfs_iter_* to create child handles on the fly.
 */
static int
make_dataset_handle_common(zfs_handle_t *zhp, zfs_cmd_t *zc)
{
	if (put_stats_zhdl(zhp, zc) != 0)
		return (-1);

	/*
	 * We've managed to open the dataset and gather statistics.  Determine
	 * the high-level type.
	 */
	if (zhp->zfs_dmustats.dds_type == DMU_OST_ZVOL)
		zhp->zfs_head_type = ZFS_TYPE_VOLUME;
	else if (zhp->zfs_dmustats.dds_type == DMU_OST_ZFS)
		zhp->zfs_head_type = ZFS_TYPE_FILESYSTEM;
	else
		abort();

	if (zhp->zfs_dmustats.dds_is_snapshot)
		zhp->zfs_type = ZFS_TYPE_SNAPSHOT;
	else if (zhp->zfs_dmustats.dds_type == DMU_OST_ZVOL)
		zhp->zfs_type = ZFS_TYPE_VOLUME;
	else if (zhp->zfs_dmustats.dds_type == DMU_OST_ZFS)
		zhp->zfs_type = ZFS_TYPE_FILESYSTEM;
	else
		abort();	/* we should never see any other types */

	if ((zhp->zpool_hdl = zpool_handle(zhp)) == NULL)
		return (-1);

	return (0);
}

zfs_handle_t *
make_dataset_handle(libzfs_handle_t *hdl, const char *path)
{
	zfs_cmd_t zc = { 0 };

	zfs_handle_t *zhp = calloc(sizeof (zfs_handle_t), 1);

	if (zhp == NULL)
		return (NULL);

	zhp->zfs_hdl = hdl;
	(void) strlcpy(zhp->zfs_name, path, sizeof (zhp->zfs_name));
	if (zcmd_alloc_dst_nvlist(hdl, &zc, 0) != 0) {
		free(zhp);
		return (NULL);
	}
	if (get_stats_ioctl(zhp, &zc) == -1) {
		zcmd_free_nvlists(&zc);
		free(zhp);
		return (NULL);
	}
	if (make_dataset_handle_common(zhp, &zc) == -1) {
		free(zhp);
		zhp = NULL;
	}
	zcmd_free_nvlists(&zc);
	return (zhp);
}

static zfs_handle_t *
make_dataset_handle_zc(libzfs_handle_t *hdl, zfs_cmd_t *zc)
{
	zfs_handle_t *zhp = calloc(sizeof (zfs_handle_t), 1);

	if (zhp == NULL)
		return (NULL);

	zhp->zfs_hdl = hdl;
	(void) strlcpy(zhp->zfs_name, zc->zc_name, sizeof (zhp->zfs_name));
	if (make_dataset_handle_common(zhp, zc) == -1) {
		free(zhp);
		return (NULL);
	}
	return (zhp);
}

/*
 * Opens the given snapshot, filesystem, or volume.   The 'types'
 * argument is a mask of acceptable types.  The function will print an
 * appropriate error message and return NULL if it can't be opened.
 */
zfs_handle_t *
zfs_open(libzfs_handle_t *hdl, const char *path, int types)
{
	zfs_handle_t *zhp;
	char errbuf[1024];

	(void) snprintf(errbuf, sizeof (errbuf),
	    dgettext(TEXT_DOMAIN, "cannot open '%s'"), path);

	/*
	 * Validate the name before we even try to open it.
	 */
	if (!zfs_validate_name(hdl, path, ZFS_TYPE_DATASET, B_FALSE)) {
		zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
		    "invalid dataset name"));
		(void) zfs_error(hdl, EZFS_INVALIDNAME, errbuf);
		return (NULL);
	}

	/*
	 * Try to get stats for the dataset, which will tell us if it exists.
	 */
	errno = 0;
	if ((zhp = make_dataset_handle(hdl, path)) == NULL) {
		(void) zfs_standard_error(hdl, errno, errbuf);
		return (NULL);
	}

	if (!(types & zhp->zfs_type)) {
		(void) zfs_error(hdl, EZFS_BADTYPE, errbuf);
		zfs_close(zhp);
		return (NULL);
	}

	return (zhp);
}

/*
 * Release a ZFS handle.  Nothing to do but free the associated memory.
 */
void
zfs_close(zfs_handle_t *zhp)
{
	if (zhp->zfs_mntopts)
		free(zhp->zfs_mntopts);
	nvlist_free(zhp->zfs_props);
	nvlist_free(zhp->zfs_user_props);
	nvlist_free(zhp->zfs_recvd_props);
	free(zhp);
}

typedef struct mnttab_node {
	struct mnttab mtn_mt;
	avl_node_t mtn_node;
} mnttab_node_t;

static int
libzfs_mnttab_cache_compare(const void *arg1, const void *arg2)
{
	const mnttab_node_t *mtn1 = arg1;
	const mnttab_node_t *mtn2 = arg2;
	int rv;

	rv = strcmp(mtn1->mtn_mt.mnt_special, mtn2->mtn_mt.mnt_special);

	if (rv == 0)
		return (0);
	return (rv > 0 ? 1 : -1);
}

void
libzfs_mnttab_init(libzfs_handle_t *hdl)
{
	assert(avl_numnodes(&hdl->libzfs_mnttab_cache) == 0);
	avl_create(&hdl->libzfs_mnttab_cache, libzfs_mnttab_cache_compare,
	    sizeof (mnttab_node_t), offsetof(mnttab_node_t, mtn_node));
}

void
libzfs_mnttab_update(libzfs_handle_t *hdl)
{
	struct mnttab entry;

	rewind(hdl->libzfs_mnttab);
	while (getmntent(hdl->libzfs_mnttab, &entry) == 0) {
		mnttab_node_t *mtn;

		if (strcmp(entry.mnt_fstype, MNTTYPE_ZFS) != 0)
			continue;
		mtn = zfs_alloc(hdl, sizeof (mnttab_node_t));
		mtn->mtn_mt.mnt_special = zfs_strdup(hdl, entry.mnt_special);
		mtn->mtn_mt.mnt_mountp = zfs_strdup(hdl, entry.mnt_mountp);
		mtn->mtn_mt.mnt_fstype = zfs_strdup(hdl, entry.mnt_fstype);
		mtn->mtn_mt.mnt_mntopts = zfs_strdup(hdl, entry.mnt_mntopts);
		avl_add(&hdl->libzfs_mnttab_cache, mtn);
	}
}

void
libzfs_mnttab_fini(libzfs_handle_t *hdl)
{
	void *cookie = NULL;
	mnttab_node_t *mtn;

	while (mtn = avl_destroy_nodes(&hdl->libzfs_mnttab_cache, &cookie)) {
		free(mtn->mtn_mt.mnt_special);
		free(mtn->mtn_mt.mnt_mountp);
		free(mtn->mtn_mt.mnt_fstype);
		free(mtn->mtn_mt.mnt_mntopts);
		free(mtn);
	}
	avl_destroy(&hdl->libzfs_mnttab_cache);
}

void
libzfs_mnttab_cache(libzfs_handle_t *hdl, boolean_t enable)
{
	hdl->libzfs_mnttab_enable = enable;
}

int
libzfs_mnttab_find(libzfs_handle_t *hdl, const char *fsname,
    struct mnttab *entry)
{
	mnttab_node_t find;
	mnttab_node_t *mtn;

	if (!hdl->libzfs_mnttab_enable) {
		struct mnttab srch = { 0 };

		if (avl_numnodes(&hdl->libzfs_mnttab_cache))
			libzfs_mnttab_fini(hdl);
		rewind(hdl->libzfs_mnttab);
		srch.mnt_special = (char *)fsname;
		srch.mnt_fstype = MNTTYPE_ZFS;
		if (getmntany(hdl->libzfs_mnttab, entry, &srch) == 0)
			return (0);
		else
			return (ENOENT);
	}

	if (avl_numnodes(&hdl->libzfs_mnttab_cache) == 0)
		libzfs_mnttab_update(hdl);

	find.mtn_mt.mnt_special = (char *)fsname;
	mtn = avl_find(&hdl->libzfs_mnttab_cache, &find, NULL);
	if (mtn) {
		*entry = mtn->mtn_mt;
		return (0);
	}
	return (ENOENT);
}

void
libzfs_mnttab_add(libzfs_handle_t *hdl, const char *special,
    const char *mountp, const char *mntopts)
{
	mnttab_node_t *mtn;

	if (avl_numnodes(&hdl->libzfs_mnttab_cache) == 0)
		return;
	mtn = zfs_alloc(hdl, sizeof (mnttab_node_t));
	mtn->mtn_mt.mnt_special = zfs_strdup(hdl, special);
	mtn->mtn_mt.mnt_mountp = zfs_strdup(hdl, mountp);
	mtn->mtn_mt.mnt_fstype = zfs_strdup(hdl, MNTTYPE_ZFS);
	mtn->mtn_mt.mnt_mntopts = zfs_strdup(hdl, mntopts);
	avl_add(&hdl->libzfs_mnttab_cache, mtn);
}

void
libzfs_mnttab_remove(libzfs_handle_t *hdl, const char *fsname)
{
	mnttab_node_t find;
	mnttab_node_t *ret;

	find.mtn_mt.mnt_special = (char *)fsname;
	if (ret = avl_find(&hdl->libzfs_mnttab_cache, (void *)&find, NULL)) {
		avl_remove(&hdl->libzfs_mnttab_cache, ret);
		free(ret->mtn_mt.mnt_special);
		free(ret->mtn_mt.mnt_mountp);
		free(ret->mtn_mt.mnt_fstype);
		free(ret->mtn_mt.mnt_mntopts);
		free(ret);
	}
}

int
zfs_spa_version(zfs_handle_t *zhp, int *spa_version)
{
	zpool_handle_t *zpool_handle = zhp->zpool_hdl;

	if (zpool_handle == NULL)
		return (-1);

	*spa_version = zpool_get_prop_int(zpool_handle,
	    ZPOOL_PROP_VERSION, NULL);
	return (0);
}

/*
 * The choice of reservation property depends on the SPA version.
 */
static int
zfs_which_resv_prop(zfs_handle_t *zhp, zfs_prop_t *resv_prop)
{
	int spa_version;

	if (zfs_spa_version(zhp, &spa_version) < 0)
		return (-1);

	if (spa_version >= SPA_VERSION_REFRESERVATION)
		*resv_prop = ZFS_PROP_REFRESERVATION;
	else
		*resv_prop = ZFS_PROP_RESERVATION;

	return (0);
}

/*
 * Given an nvlist of properties to set, validates that they are correct, and
 * parses any numeric properties (index, boolean, etc) if they are specified as
 * strings.
 */
nvlist_t *
zfs_valid_proplist(libzfs_handle_t *hdl, zfs_type_t type, nvlist_t *nvl,
    uint64_t zoned, zfs_handle_t *zhp, const char *errbuf)
{
	nvpair_t *elem;
	uint64_t intval;
	char *strval;
	zfs_prop_t prop;
	nvlist_t *ret;
	int chosen_normal = -1;
	int chosen_utf = -1;

	if (nvlist_alloc(&ret, NV_UNIQUE_NAME, 0) != 0) {
		(void) no_memory(hdl);
		return (NULL);
	}

	/*
	 * Make sure this property is valid and applies to this type.
	 */

	elem = NULL;
	while ((elem = nvlist_next_nvpair(nvl, elem)) != NULL) {
		const char *propname = nvpair_name(elem);

		prop = zfs_name_to_prop(propname);
		if (prop == ZPROP_INVAL && zfs_prop_user(propname)) {
			/*
			 * This is a user property: make sure it's a
			 * string, and that it's less than ZAP_MAXNAMELEN.
			 */
			if (nvpair_type(elem) != DATA_TYPE_STRING) {
				zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
				    "'%s' must be a string"), propname);
				(void) zfs_error(hdl, EZFS_BADPROP, errbuf);
				goto error;
			}

			if (strlen(nvpair_name(elem)) >= ZAP_MAXNAMELEN) {
				zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
				    "property name '%s' is too long"),
				    propname);
				(void) zfs_error(hdl, EZFS_BADPROP, errbuf);
				goto error;
			}

			(void) nvpair_value_string(elem, &strval);
			if (nvlist_add_string(ret, propname, strval) != 0) {
				(void) no_memory(hdl);
				goto error;
			}
			continue;
		}

		/*
		 * Currently, only user properties can be modified on
		 * snapshots.
		 */
		if (type == ZFS_TYPE_SNAPSHOT) {
			zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
			    "this property can not be modified for snapshots"));
			(void) zfs_error(hdl, EZFS_PROPTYPE, errbuf);
			goto error;
		}

		if (prop == ZPROP_INVAL && zfs_prop_userquota(propname)) {
			zfs_userquota_prop_t uqtype;
			char newpropname[128];
			char domain[128];
			uint64_t rid;
			uint64_t valary[3];

			if (userquota_propname_decode(propname, zoned,
			    &uqtype, domain, sizeof (domain), &rid) != 0) {
				zfs_error_aux(hdl,
				    dgettext(TEXT_DOMAIN,
				    "'%s' has an invalid user/group name"),
				    propname);
				(void) zfs_error(hdl, EZFS_BADPROP, errbuf);
				goto error;
			}

			if (uqtype != ZFS_PROP_USERQUOTA &&
			    uqtype != ZFS_PROP_GROUPQUOTA) {
				zfs_error_aux(hdl,
				    dgettext(TEXT_DOMAIN, "'%s' is readonly"),
				    propname);
				(void) zfs_error(hdl, EZFS_PROPREADONLY,
				    errbuf);
				goto error;
			}

			if (nvpair_type(elem) == DATA_TYPE_STRING) {
				(void) nvpair_value_string(elem, &strval);
				if (strcmp(strval, "none") == 0) {
					intval = 0;
				} else if (zfs_nicestrtonum(hdl,
				    strval, &intval) != 0) {
					(void) zfs_error(hdl,
					    EZFS_BADPROP, errbuf);
					goto error;
				}
			} else if (nvpair_type(elem) ==
			    DATA_TYPE_UINT64) {
				(void) nvpair_value_uint64(elem, &intval);
				if (intval == 0) {
					zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
					    "use 'none' to disable "
					    "userquota/groupquota"));
					goto error;
				}
			} else {
				zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
				    "'%s' must be a number"), propname);
				(void) zfs_error(hdl, EZFS_BADPROP, errbuf);
				goto error;
			}

			/*
			 * Encode the prop name as
			 * userquota@<hex-rid>-domain, to make it easy
			 * for the kernel to decode.
			 */
			(void) snprintf(newpropname, sizeof (newpropname),
			    "%s%llx-%s", zfs_userquota_prop_prefixes[uqtype],
			    (longlong_t)rid, domain);
			valary[0] = uqtype;
			valary[1] = rid;
			valary[2] = intval;
			if (nvlist_add_uint64_array(ret, newpropname,
			    valary, 3) != 0) {
				(void) no_memory(hdl);
				goto error;
			}
			continue;
		}

		if (prop == ZPROP_INVAL) {
			zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
			    "invalid property '%s'"), propname);
			(void) zfs_error(hdl, EZFS_BADPROP, errbuf);
			goto error;
		}

		if (!zfs_prop_valid_for_type(prop, type)) {
			zfs_error_aux(hdl,
			    dgettext(TEXT_DOMAIN, "'%s' does not "
			    "apply to datasets of this type"), propname);
			(void) zfs_error(hdl, EZFS_PROPTYPE, errbuf);
			goto error;
		}

		if (zfs_prop_readonly(prop) &&
		    (!zfs_prop_setonce(prop) || zhp != NULL)) {
			zfs_error_aux(hdl,
			    dgettext(TEXT_DOMAIN, "'%s' is readonly"),
			    propname);
			(void) zfs_error(hdl, EZFS_PROPREADONLY, errbuf);
			goto error;
		}

		if (zprop_parse_value(hdl, elem, prop, type, ret,
		    &strval, &intval, errbuf) != 0)
			goto error;

		/*
		 * Perform some additional checks for specific properties.
		 */
		switch (prop) {
		case ZFS_PROP_VERSION:
		{
			int version;

			if (zhp == NULL)
				break;
			version = zfs_prop_get_int(zhp, ZFS_PROP_VERSION);
			if (intval < version) {
				zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
				    "Can not downgrade; already at version %u"),
				    version);
				(void) zfs_error(hdl, EZFS_BADPROP, errbuf);
				goto error;
			}
			break;
		}

		case ZFS_PROP_RECORDSIZE:
		case ZFS_PROP_VOLBLOCKSIZE:
			/* must be power of two within SPA_{MIN,MAX}BLOCKSIZE */
			if (intval < SPA_MINBLOCKSIZE ||
			    intval > SPA_MAXBLOCKSIZE || !ISP2(intval)) {
				zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
				    "'%s' must be power of 2 from %u "
				    "to %uk"), propname,
				    (uint_t)SPA_MINBLOCKSIZE,
				    (uint_t)SPA_MAXBLOCKSIZE >> 10);
				(void) zfs_error(hdl, EZFS_BADPROP, errbuf);
				goto error;
			}
			break;

		case ZFS_PROP_MLSLABEL:
		{
			/*
			 * Verify the mlslabel string and convert to
			 * internal hex label string.
			 */

			m_label_t *new_sl;
			char *hex = NULL;	/* internal label string */

			/* Default value is already OK. */
			if (strcasecmp(strval, ZFS_MLSLABEL_DEFAULT) == 0)
				break;

			/* Verify the label can be converted to binary form */
			if (((new_sl = m_label_alloc(MAC_LABEL)) == NULL) ||
			    (str_to_label(strval, &new_sl, MAC_LABEL,
			    L_NO_CORRECTION, NULL) == -1)) {
				goto badlabel;
			}

			/* Now translate to hex internal label string */
			if (label_to_str(new_sl, &hex, M_INTERNAL,
			    DEF_NAMES) != 0) {
				if (hex)
					free(hex);
				goto badlabel;
			}
			m_label_free(new_sl);

			/* If string is already in internal form, we're done. */
			if (strcmp(strval, hex) == 0) {
				free(hex);
				break;
			}

			/* Replace the label string with the internal form. */
			(void) nvlist_remove(ret, zfs_prop_to_name(prop),
			    DATA_TYPE_STRING);
			verify(nvlist_add_string(ret, zfs_prop_to_name(prop),
			    hex) == 0);
			free(hex);

			break;

badlabel:
			zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
			    "invalid mlslabel '%s'"), strval);
			(void) zfs_error(hdl, EZFS_BADPROP, errbuf);
			m_label_free(new_sl);	/* OK if null */
			goto error;

		}

		case ZFS_PROP_MOUNTPOINT:
		{
			namecheck_err_t why;

			if (strcmp(strval, ZFS_MOUNTPOINT_NONE) == 0 ||
			    strcmp(strval, ZFS_MOUNTPOINT_LEGACY) == 0)
				break;

			if (mountpoint_namecheck(strval, &why)) {
				switch (why) {
				case NAME_ERR_LEADING_SLASH:
					zfs_error_aux(hdl,
					    dgettext(TEXT_DOMAIN,
					    "'%s' must be an absolute path, "
					    "'none', or 'legacy'"), propname);
					break;
				case NAME_ERR_TOOLONG:
					zfs_error_aux(hdl,
					    dgettext(TEXT_DOMAIN,
					    "component of '%s' is too long"),
					    propname);
					break;
				}
				(void) zfs_error(hdl, EZFS_BADPROP, errbuf);
				goto error;
			}
		}

			/*FALLTHRU*/

		case ZFS_PROP_SHARESMB:
		case ZFS_PROP_SHARENFS:
			/*
			 * For the mountpoint and sharenfs or sharesmb
			 * properties, check if it can be set in a
			 * global/non-global zone based on
			 * the zoned property value:
			 *
			 *		global zone	    non-global zone
			 * --------------------------------------------------
			 * zoned=on	mountpoint (no)	    mountpoint (yes)
			 *		sharenfs (no)	    sharenfs (no)
			 *		sharesmb (no)	    sharesmb (no)
			 *
			 * zoned=off	mountpoint (yes)	N/A
			 *		sharenfs (yes)
			 *		sharesmb (yes)
			 */
			if (zoned) {
				if (getzoneid() == GLOBAL_ZONEID) {
					zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
					    "'%s' cannot be set on "
					    "dataset in a non-global zone"),
					    propname);
					(void) zfs_error(hdl, EZFS_ZONED,
					    errbuf);
					goto error;
				} else if (prop == ZFS_PROP_SHARENFS ||
				    prop == ZFS_PROP_SHARESMB) {
					zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
					    "'%s' cannot be set in "
					    "a non-global zone"), propname);
					(void) zfs_error(hdl, EZFS_ZONED,
					    errbuf);
					goto error;
				}
			} else if (getzoneid() != GLOBAL_ZONEID) {
				/*
				 * If zoned property is 'off', this must be in
				 * a global zone. If not, something is wrong.
				 */
				zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
				    "'%s' cannot be set while dataset "
				    "'zoned' property is set"), propname);
				(void) zfs_error(hdl, EZFS_ZONED, errbuf);
				goto error;
			}

			/*
			 * At this point, it is legitimate to set the
			 * property. Now we want to make sure that the
			 * property value is valid if it is sharenfs.
			 */
			if ((prop == ZFS_PROP_SHARENFS ||
			    prop == ZFS_PROP_SHARESMB) &&
			    strcmp(strval, "on") != 0 &&
			    strcmp(strval, "off") != 0) {
				zfs_share_proto_t proto;

				if (prop == ZFS_PROP_SHARESMB)
					proto = PROTO_SMB;
				else
					proto = PROTO_NFS;

				/*
				 * Must be an valid sharing protocol
				 * option string so init the libshare
				 * in order to enable the parser and
				 * then parse the options. We use the
				 * control API since we don't care about
				 * the current configuration and don't
				 * want the overhead of loading it
				 * until we actually do something.
				 */

				if (zfs_init_libshare(hdl,
				    SA_INIT_CONTROL_API) != SA_OK) {
					/*
					 * An error occurred so we can't do
					 * anything
					 */
					zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
					    "'%s' cannot be set: problem "
					    "in share initialization"),
					    propname);
					(void) zfs_error(hdl, EZFS_BADPROP,
					    errbuf);
					goto error;
				}

				if (zfs_parse_options(strval, proto) != SA_OK) {
					/*
					 * There was an error in parsing so
					 * deal with it by issuing an error
					 * message and leaving after
					 * uninitializing the the libshare
					 * interface.
					 */
					zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
					    "'%s' cannot be set to invalid "
					    "options"), propname);
					(void) zfs_error(hdl, EZFS_BADPROP,
					    errbuf);
					zfs_uninit_libshare(hdl);
					goto error;
				}
				zfs_uninit_libshare(hdl);
			}

			break;
		case ZFS_PROP_UTF8ONLY:
			chosen_utf = (int)intval;
			break;
		case ZFS_PROP_NORMALIZE:
			chosen_normal = (int)intval;
			break;
		}

		/*
		 * For changes to existing volumes, we have some additional
		 * checks to enforce.
		 */
		if (type == ZFS_TYPE_VOLUME && zhp != NULL) {
			uint64_t volsize = zfs_prop_get_int(zhp,
			    ZFS_PROP_VOLSIZE);
			uint64_t blocksize = zfs_prop_get_int(zhp,
			    ZFS_PROP_VOLBLOCKSIZE);
			char buf[64];

			switch (prop) {
			case ZFS_PROP_RESERVATION:
			case ZFS_PROP_REFRESERVATION:
				if (intval > volsize) {
					zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
					    "'%s' is greater than current "
					    "volume size"), propname);
					(void) zfs_error(hdl, EZFS_BADPROP,
					    errbuf);
					goto error;
				}
				break;

			case ZFS_PROP_VOLSIZE:
				if (intval % blocksize != 0) {
					zfs_nicenum(blocksize, buf,
					    sizeof (buf));
					zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
					    "'%s' must be a multiple of "
					    "volume block size (%s)"),
					    propname, buf);
					(void) zfs_error(hdl, EZFS_BADPROP,
					    errbuf);
					goto error;
				}

				if (intval == 0) {
					zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
					    "'%s' cannot be zero"),
					    propname);
					(void) zfs_error(hdl, EZFS_BADPROP,
					    errbuf);
					goto error;
				}
				break;
			}
		}
	}

	/*
	 * If normalization was chosen, but no UTF8 choice was made,
	 * enforce rejection of non-UTF8 names.
	 *
	 * If normalization was chosen, but rejecting non-UTF8 names
	 * was explicitly not chosen, it is an error.
	 */
	if (chosen_normal > 0 && chosen_utf < 0) {
		if (nvlist_add_uint64(ret,
		    zfs_prop_to_name(ZFS_PROP_UTF8ONLY), 1) != 0) {
			(void) no_memory(hdl);
			goto error;
		}
	} else if (chosen_normal > 0 && chosen_utf == 0) {
		zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
		    "'%s' must be set 'on' if normalization chosen"),
		    zfs_prop_to_name(ZFS_PROP_UTF8ONLY));
		(void) zfs_error(hdl, EZFS_BADPROP, errbuf);
		goto error;
	}
	return (ret);

error:
	nvlist_free(ret);
	return (NULL);
}

int
zfs_add_synthetic_resv(zfs_handle_t *zhp, nvlist_t *nvl)
{
	uint64_t old_volsize;
	uint64_t new_volsize;
	uint64_t old_reservation;
	uint64_t new_reservation;
	zfs_prop_t resv_prop;

	/*
	 * If this is an existing volume, and someone is setting the volsize,
	 * make sure that it matches the reservation, or add it if necessary.
	 */
	old_volsize = zfs_prop_get_int(zhp, ZFS_PROP_VOLSIZE);
	if (zfs_which_resv_prop(zhp, &resv_prop) < 0)
		return (-1);
	old_reservation = zfs_prop_get_int(zhp, resv_prop);
	if ((zvol_volsize_to_reservation(old_volsize, zhp->zfs_props) !=
	    old_reservation) || nvlist_lookup_uint64(nvl,
	    zfs_prop_to_name(resv_prop), &new_reservation) != ENOENT) {
		return (0);
	}
	if (nvlist_lookup_uint64(nvl, zfs_prop_to_name(ZFS_PROP_VOLSIZE),
	    &new_volsize) != 0)
		return (-1);
	new_reservation = zvol_volsize_to_reservation(new_volsize,
	    zhp->zfs_props);
	if (nvlist_add_uint64(nvl, zfs_prop_to_name(resv_prop),
	    new_reservation) != 0) {
		(void) no_memory(zhp->zfs_hdl);
		return (-1);
	}
	return (1);
}

void
zfs_setprop_error(libzfs_handle_t *hdl, zfs_prop_t prop, int err,
    char *errbuf)
{
	switch (err) {

	case ENOSPC:
		/*
		 * For quotas and reservations, ENOSPC indicates
		 * something different; setting a quota or reservation
		 * doesn't use any disk space.
		 */
		switch (prop) {
		case ZFS_PROP_QUOTA:
		case ZFS_PROP_REFQUOTA:
			zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
			    "size is less than current used or "
			    "reserved space"));
			(void) zfs_error(hdl, EZFS_PROPSPACE, errbuf);
			break;

		case ZFS_PROP_RESERVATION:
		case ZFS_PROP_REFRESERVATION:
			zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
			    "size is greater than available space"));
			(void) zfs_error(hdl, EZFS_PROPSPACE, errbuf);
			break;

		default:
			(void) zfs_standard_error(hdl, err, errbuf);
			break;
		}
		break;

	case EBUSY:
		(void) zfs_standard_error(hdl, EBUSY, errbuf);
		break;

	case EROFS:
		(void) zfs_error(hdl, EZFS_DSREADONLY, errbuf);
		break;

	case ENOTSUP:
		zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
		    "pool and or dataset must be upgraded to set this "
		    "property or value"));
		(void) zfs_error(hdl, EZFS_BADVERSION, errbuf);
		break;

	case ERANGE:
		if (prop == ZFS_PROP_COMPRESSION) {
			(void) zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
			    "property setting is not allowed on "
			    "bootable datasets"));
			(void) zfs_error(hdl, EZFS_NOTSUP, errbuf);
		} else {
			(void) zfs_standard_error(hdl, err, errbuf);
		}
		break;

	case EINVAL:
		if (prop == ZPROP_INVAL) {
			(void) zfs_error(hdl, EZFS_BADPROP, errbuf);
		} else {
			(void) zfs_standard_error(hdl, err, errbuf);
		}
		break;

	case EOVERFLOW:
		/*
		 * This platform can't address a volume this big.
		 */
#ifdef _ILP32
		if (prop == ZFS_PROP_VOLSIZE) {
			(void) zfs_error(hdl, EZFS_VOLTOOBIG, errbuf);
			break;
		}
#endif
		/* FALLTHROUGH */
	default:
		(void) zfs_standard_error(hdl, err, errbuf);
	}
}

/*
 * Given a property name and value, set the property for the given dataset.
 */
int
zfs_prop_set(zfs_handle_t *zhp, const char *propname, const char *propval)
{
	zfs_cmd_t zc = { 0 };
	int ret = -1;
	prop_changelist_t *cl = NULL;
	char errbuf[1024];
	libzfs_handle_t *hdl = zhp->zfs_hdl;
	nvlist_t *nvl = NULL, *realprops;
	zfs_prop_t prop;
	boolean_t do_prefix;
	uint64_t idx;
	int added_resv;

	(void) snprintf(errbuf, sizeof (errbuf),
	    dgettext(TEXT_DOMAIN, "cannot set property for '%s'"),
	    zhp->zfs_name);

	if (nvlist_alloc(&nvl, NV_UNIQUE_NAME, 0) != 0 ||
	    nvlist_add_string(nvl, propname, propval) != 0) {
		(void) no_memory(hdl);
		goto error;
	}

	if ((realprops = zfs_valid_proplist(hdl, zhp->zfs_type, nvl,
	    zfs_prop_get_int(zhp, ZFS_PROP_ZONED), zhp, errbuf)) == NULL)
		goto error;

	nvlist_free(nvl);
	nvl = realprops;

	prop = zfs_name_to_prop(propname);

	if (prop == ZFS_PROP_VOLSIZE) {
		if ((added_resv = zfs_add_synthetic_resv(zhp, nvl)) == -1)
			goto error;
	}

	if ((cl = changelist_gather(zhp, prop, 0, 0)) == NULL)
		goto error;

	if (prop == ZFS_PROP_MOUNTPOINT && changelist_haszonedchild(cl)) {
		zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
		    "child dataset with inherited mountpoint is used "
		    "in a non-global zone"));
		ret = zfs_error(hdl, EZFS_ZONED, errbuf);
		goto error;
	}

	/*
	 * If the dataset's canmount property is being set to noauto,
	 * then we want to prevent unmounting & remounting it.
	 */
	do_prefix = !((prop == ZFS_PROP_CANMOUNT) &&
	    (zprop_string_to_index(prop, propval, &idx,
	    ZFS_TYPE_DATASET) == 0) && (idx == ZFS_CANMOUNT_NOAUTO));

	if (do_prefix && (ret = changelist_prefix(cl)) != 0)
		goto error;

	/*
	 * Execute the corresponding ioctl() to set this property.
	 */
	(void) strlcpy(zc.zc_name, zhp->zfs_name, sizeof (zc.zc_name));

	if (zcmd_write_src_nvlist(hdl, &zc, nvl) != 0)
		goto error;

	ret = zfs_ioctl(hdl, ZFS_IOC_SET_PROP, &zc);

	if (ret != 0) {
		zfs_setprop_error(hdl, prop, errno, errbuf);
		if (added_resv && errno == ENOSPC) {
			/* clean up the volsize property we tried to set */
			uint64_t old_volsize = zfs_prop_get_int(zhp,
			    ZFS_PROP_VOLSIZE);
			nvlist_free(nvl);
			zcmd_free_nvlists(&zc);
			if (nvlist_alloc(&nvl, NV_UNIQUE_NAME, 0) != 0)
				goto error;
			if (nvlist_add_uint64(nvl,
			    zfs_prop_to_name(ZFS_PROP_VOLSIZE),
			    old_volsize) != 0)
				goto error;
			if (zcmd_write_src_nvlist(hdl, &zc, nvl) != 0)
				goto error;
			(void) zfs_ioctl(hdl, ZFS_IOC_SET_PROP, &zc);
		}
	} else {
		if (do_prefix)
			ret = changelist_postfix(cl);

		/*
		 * Refresh the statistics so the new property value
		 * is reflected.
		 */
		if (ret == 0)
			(void) get_stats(zhp);
	}

error:
	nvlist_free(nvl);
	zcmd_free_nvlists(&zc);
	if (cl)
		changelist_free(cl);
	return (ret);
}

/*
 * Given a property, inherit the value from the parent dataset, or if received
 * is TRUE, revert to the received value, if any.
 */
int
zfs_prop_inherit(zfs_handle_t *zhp, const char *propname, boolean_t received)
{
	zfs_cmd_t zc = { 0 };
	int ret;
	prop_changelist_t *cl;
	libzfs_handle_t *hdl = zhp->zfs_hdl;
	char errbuf[1024];
	zfs_prop_t prop;

	(void) snprintf(errbuf, sizeof (errbuf), dgettext(TEXT_DOMAIN,
	    "cannot inherit %s for '%s'"), propname, zhp->zfs_name);

	zc.zc_cookie = received;
	if ((prop = zfs_name_to_prop(propname)) == ZPROP_INVAL) {
		/*
		 * For user properties, the amount of work we have to do is very
		 * small, so just do it here.
		 */
		if (!zfs_prop_user(propname)) {
			zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
			    "invalid property"));
			return (zfs_error(hdl, EZFS_BADPROP, errbuf));
		}

		(void) strlcpy(zc.zc_name, zhp->zfs_name, sizeof (zc.zc_name));
		(void) strlcpy(zc.zc_value, propname, sizeof (zc.zc_value));

		if (zfs_ioctl(zhp->zfs_hdl, ZFS_IOC_INHERIT_PROP, &zc) != 0)
			return (zfs_standard_error(hdl, errno, errbuf));

		return (0);
	}

	/*
	 * Verify that this property is inheritable.
	 */
	if (zfs_prop_readonly(prop))
		return (zfs_error(hdl, EZFS_PROPREADONLY, errbuf));

	if (!zfs_prop_inheritable(prop) && !received)
		return (zfs_error(hdl, EZFS_PROPNONINHERIT, errbuf));

	/*
	 * Check to see if the value applies to this type
	 */
	if (!zfs_prop_valid_for_type(prop, zhp->zfs_type))
		return (zfs_error(hdl, EZFS_PROPTYPE, errbuf));

	/*
	 * Normalize the name, to get rid of shorthand abbreviations.
	 */
	propname = zfs_prop_to_name(prop);
	(void) strlcpy(zc.zc_name, zhp->zfs_name, sizeof (zc.zc_name));
	(void) strlcpy(zc.zc_value, propname, sizeof (zc.zc_value));

	if (prop == ZFS_PROP_MOUNTPOINT && getzoneid() == GLOBAL_ZONEID &&
	    zfs_prop_get_int(zhp, ZFS_PROP_ZONED)) {
		zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
		    "dataset is used in a non-global zone"));
		return (zfs_error(hdl, EZFS_ZONED, errbuf));
	}

	/*
	 * Determine datasets which will be affected by this change, if any.
	 */
	if ((cl = changelist_gather(zhp, prop, 0, 0)) == NULL)
		return (-1);

	if (prop == ZFS_PROP_MOUNTPOINT && changelist_haszonedchild(cl)) {
		zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
		    "child dataset with inherited mountpoint is used "
		    "in a non-global zone"));
		ret = zfs_error(hdl, EZFS_ZONED, errbuf);
		goto error;
	}

	if ((ret = changelist_prefix(cl)) != 0)
		goto error;

	if ((ret = zfs_ioctl(zhp->zfs_hdl, ZFS_IOC_INHERIT_PROP, &zc)) != 0) {
		return (zfs_standard_error(hdl, errno, errbuf));
	} else {

		if ((ret = changelist_postfix(cl)) != 0)
			goto error;

		/*
		 * Refresh the statistics so the new property is reflected.
		 */
		(void) get_stats(zhp);
	}

error:
	changelist_free(cl);
	return (ret);
}

/*
 * True DSL properties are stored in an nvlist.  The following two functions
 * extract them appropriately.
 */
static uint64_t
getprop_uint64(zfs_handle_t *zhp, zfs_prop_t prop, char **source)
{
	nvlist_t *nv;
	uint64_t value;

	*source = NULL;
	if (nvlist_lookup_nvlist(zhp->zfs_props,
	    zfs_prop_to_name(prop), &nv) == 0) {
		verify(nvlist_lookup_uint64(nv, ZPROP_VALUE, &value) == 0);
		(void) nvlist_lookup_string(nv, ZPROP_SOURCE, source);
	} else {
		verify(!zhp->zfs_props_table ||
		    zhp->zfs_props_table[prop] == B_TRUE);
		value = zfs_prop_default_numeric(prop);
		*source = "";
	}

	return (value);
}

static char *
getprop_string(zfs_handle_t *zhp, zfs_prop_t prop, char **source)
{
	nvlist_t *nv;
	char *value;

	*source = NULL;
	if (nvlist_lookup_nvlist(zhp->zfs_props,
	    zfs_prop_to_name(prop), &nv) == 0) {
		verify(nvlist_lookup_string(nv, ZPROP_VALUE, &value) == 0);
		(void) nvlist_lookup_string(nv, ZPROP_SOURCE, source);
	} else {
		verify(!zhp->zfs_props_table ||
		    zhp->zfs_props_table[prop] == B_TRUE);
		if ((value = (char *)zfs_prop_default_string(prop)) == NULL)
			value = "";
		*source = "";
	}

	return (value);
}

static boolean_t
zfs_is_recvd_props_mode(zfs_handle_t *zhp)
{
	return (zhp->zfs_props == zhp->zfs_recvd_props);
}

static void
zfs_set_recvd_props_mode(zfs_handle_t *zhp, uint64_t *cookie)
{
	*cookie = (uint64_t)(uintptr_t)zhp->zfs_props;
	zhp->zfs_props = zhp->zfs_recvd_props;
}

static void
zfs_unset_recvd_props_mode(zfs_handle_t *zhp, uint64_t *cookie)
{
	zhp->zfs_props = (nvlist_t *)(uintptr_t)*cookie;
	*cookie = 0;
}

/*
 * Internal function for getting a numeric property.  Both zfs_prop_get() and
 * zfs_prop_get_int() are built using this interface.
 *
 * Certain properties can be overridden using 'mount -o'.  In this case, scan
 * the contents of the /etc/mnttab entry, searching for the appropriate options.
 * If they differ from the on-disk values, report the current values and mark
 * the source "temporary".
 */
static int
get_numeric_property(zfs_handle_t *zhp, zfs_prop_t prop, zprop_source_t *src,
    char **source, uint64_t *val)
{
	zfs_cmd_t zc = { 0 };
	nvlist_t *zplprops = NULL;
	struct mnttab mnt;
	char *mntopt_on = NULL;
	char *mntopt_off = NULL;
	boolean_t received = zfs_is_recvd_props_mode(zhp);

	*source = NULL;

	switch (prop) {
	case ZFS_PROP_ATIME:
		mntopt_on = MNTOPT_ATIME;
		mntopt_off = MNTOPT_NOATIME;
		break;

	case ZFS_PROP_DEVICES:
		mntopt_on = MNTOPT_DEVICES;
		mntopt_off = MNTOPT_NODEVICES;
		break;

	case ZFS_PROP_EXEC:
		mntopt_on = MNTOPT_EXEC;
		mntopt_off = MNTOPT_NOEXEC;
		break;

	case ZFS_PROP_READONLY:
		mntopt_on = MNTOPT_RO;
		mntopt_off = MNTOPT_RW;
		break;

	case ZFS_PROP_SETUID:
		mntopt_on = MNTOPT_SETUID;
		mntopt_off = MNTOPT_NOSETUID;
		break;

	case ZFS_PROP_XATTR:
		mntopt_on = MNTOPT_XATTR;
		mntopt_off = MNTOPT_NOXATTR;
		break;

	case ZFS_PROP_NBMAND:
		mntopt_on = MNTOPT_NBMAND;
		mntopt_off = MNTOPT_NONBMAND;
		break;
	}

	/*
	 * Because looking up the mount options is potentially expensive
	 * (iterating over all of /etc/mnttab), we defer its calculation until
	 * we're looking up a property which requires its presence.
	 */
	if (!zhp->zfs_mntcheck &&
	    (mntopt_on != NULL || prop == ZFS_PROP_MOUNTED)) {
		libzfs_handle_t *hdl = zhp->zfs_hdl;
		struct mnttab entry;

		if (libzfs_mnttab_find(hdl, zhp->zfs_name, &entry) == 0) {
			zhp->zfs_mntopts = zfs_strdup(hdl,
			    entry.mnt_mntopts);
			if (zhp->zfs_mntopts == NULL)
				return (-1);
		}

		zhp->zfs_mntcheck = B_TRUE;
	}

	if (zhp->zfs_mntopts == NULL)
		mnt.mnt_mntopts = "";
	else
		mnt.mnt_mntopts = zhp->zfs_mntopts;

	switch (prop) {
	case ZFS_PROP_ATIME:
	case ZFS_PROP_DEVICES:
	case ZFS_PROP_EXEC:
	case ZFS_PROP_READONLY:
	case ZFS_PROP_SETUID:
	case ZFS_PROP_XATTR:
	case ZFS_PROP_NBMAND:
		*val = getprop_uint64(zhp, prop, source);

		if (received)
			break;

		if (hasmntopt(&mnt, mntopt_on) && !*val) {
			*val = B_TRUE;
			if (src)
				*src = ZPROP_SRC_TEMPORARY;
		} else if (hasmntopt(&mnt, mntopt_off) && *val) {
			*val = B_FALSE;
			if (src)
				*src = ZPROP_SRC_TEMPORARY;
		}
		break;

	case ZFS_PROP_CANMOUNT:
	case ZFS_PROP_VOLSIZE:
	case ZFS_PROP_QUOTA:
	case ZFS_PROP_REFQUOTA:
	case ZFS_PROP_RESERVATION:
	case ZFS_PROP_REFRESERVATION:
		*val = getprop_uint64(zhp, prop, source);

		if (*source == NULL) {
			/* not default, must be local */
			*source = zhp->zfs_name;
		}
		break;

	case ZFS_PROP_MOUNTED:
		*val = (zhp->zfs_mntopts != NULL);
		break;

	case ZFS_PROP_NUMCLONES:
		*val = zhp->zfs_dmustats.dds_num_clones;
		break;

	case ZFS_PROP_VERSION:
	case ZFS_PROP_NORMALIZE:
	case ZFS_PROP_UTF8ONLY:
	case ZFS_PROP_CASE:
		if (!zfs_prop_valid_for_type(prop, zhp->zfs_head_type) ||
		    zcmd_alloc_dst_nvlist(zhp->zfs_hdl, &zc, 0) != 0)
			return (-1);
		(void) strlcpy(zc.zc_name, zhp->zfs_name, sizeof (zc.zc_name));
		if (zfs_ioctl(zhp->zfs_hdl, ZFS_IOC_OBJSET_ZPLPROPS, &zc)) {
			zcmd_free_nvlists(&zc);
			return (-1);
		}
		if (zcmd_read_dst_nvlist(zhp->zfs_hdl, &zc, &zplprops) != 0 ||
		    nvlist_lookup_uint64(zplprops, zfs_prop_to_name(prop),
		    val) != 0) {
			zcmd_free_nvlists(&zc);
			return (-1);
		}
		if (zplprops)
			nvlist_free(zplprops);
		zcmd_free_nvlists(&zc);
		break;

	default:
		switch (zfs_prop_get_type(prop)) {
		case PROP_TYPE_NUMBER:
		case PROP_TYPE_INDEX:
			*val = getprop_uint64(zhp, prop, source);
			/*
			 * If we tried to use a default value for a
			 * readonly property, it means that it was not
			 * present.
			 */
			if (zfs_prop_readonly(prop) &&
			    *source != NULL && (*source)[0] == '\0') {
				*source = NULL;
			}
			break;

		case PROP_TYPE_STRING:
		default:
			zfs_error_aux(zhp->zfs_hdl, dgettext(TEXT_DOMAIN,
			    "cannot get non-numeric property"));
			return (zfs_error(zhp->zfs_hdl, EZFS_BADPROP,
			    dgettext(TEXT_DOMAIN, "internal error")));
		}
	}

	return (0);
}

/*
 * Calculate the source type, given the raw source string.
 */
static void
get_source(zfs_handle_t *zhp, zprop_source_t *srctype, char *source,
    char *statbuf, size_t statlen)
{
	if (statbuf == NULL || *srctype == ZPROP_SRC_TEMPORARY)
		return;

	if (source == NULL) {
		*srctype = ZPROP_SRC_NONE;
	} else if (source[0] == '\0') {
		*srctype = ZPROP_SRC_DEFAULT;
	} else if (strstr(source, ZPROP_SOURCE_VAL_RECVD) != NULL) {
		*srctype = ZPROP_SRC_RECEIVED;
	} else {
		if (strcmp(source, zhp->zfs_name) == 0) {
			*srctype = ZPROP_SRC_LOCAL;
		} else {
			(void) strlcpy(statbuf, source, statlen);
			*srctype = ZPROP_SRC_INHERITED;
		}
	}

}

int
zfs_prop_get_recvd(zfs_handle_t *zhp, const char *propname, char *propbuf,
    size_t proplen, boolean_t literal)
{
	zfs_prop_t prop;
	int err = 0;

	if (zhp->zfs_recvd_props == NULL)
		if (get_recvd_props_ioctl(zhp) != 0)
			return (-1);

	prop = zfs_name_to_prop(propname);

	if (prop != ZPROP_INVAL) {
		uint64_t cookie;
		if (!nvlist_exists(zhp->zfs_recvd_props, propname))
			return (-1);
		zfs_set_recvd_props_mode(zhp, &cookie);
		err = zfs_prop_get(zhp, prop, propbuf, proplen,
		    NULL, NULL, 0, literal);
		zfs_unset_recvd_props_mode(zhp, &cookie);
	} else if (zfs_prop_userquota(propname)) {
		return (-1);
	} else {
		nvlist_t *propval;
		char *recvdval;
		if (nvlist_lookup_nvlist(zhp->zfs_recvd_props,
		    propname, &propval) != 0)
			return (-1);
		verify(nvlist_lookup_string(propval, ZPROP_VALUE,
		    &recvdval) == 0);
		(void) strlcpy(propbuf, recvdval, proplen);
	}

	return (err == 0 ? 0 : -1);
}

/*
 * Retrieve a property from the given object.  If 'literal' is specified, then
 * numbers are left as exact values.  Otherwise, numbers are converted to a
 * human-readable form.
 *
 * Returns 0 on success, or -1 on error.
 */
int
zfs_prop_get(zfs_handle_t *zhp, zfs_prop_t prop, char *propbuf, size_t proplen,
    zprop_source_t *src, char *statbuf, size_t statlen, boolean_t literal)
{
	char *source = NULL;
	uint64_t val;
	char *str;
	const char *strval;
	boolean_t received = zfs_is_recvd_props_mode(zhp);

	/*
	 * Check to see if this property applies to our object
	 */
	if (!zfs_prop_valid_for_type(prop, zhp->zfs_type))
		return (-1);

	if (received && zfs_prop_readonly(prop))
		return (-1);

	if (src)
		*src = ZPROP_SRC_NONE;

	switch (prop) {
	case ZFS_PROP_CREATION:
		/*
		 * 'creation' is a time_t stored in the statistics.  We convert
		 * this into a string unless 'literal' is specified.
		 */
		{
			val = getprop_uint64(zhp, prop, &source);
			time_t time = (time_t)val;
			struct tm t;

			if (literal ||
			    localtime_r(&time, &t) == NULL ||
			    strftime(propbuf, proplen, "%a %b %e %k:%M %Y",
			    &t) == 0)
				(void) snprintf(propbuf, proplen, "%llu", val);
		}
		break;

	case ZFS_PROP_MOUNTPOINT:
		/*
		 * Getting the precise mountpoint can be tricky.
		 *
		 *  - for 'none' or 'legacy', return those values.
		 *  - for inherited mountpoints, we want to take everything
		 *    after our ancestor and append it to the inherited value.
		 *
		 * If the pool has an alternate root, we want to prepend that
		 * root to any values we return.
		 */

		str = getprop_string(zhp, prop, &source);

		if (str[0] == '/') {
			char buf[MAXPATHLEN];
			char *root = buf;
			const char *relpath;

			/*
			 * If we inherit the mountpoint, even from a dataset
			 * with a received value, the source will be the path of
			 * the dataset we inherit from. If source is
			 * ZPROP_SOURCE_VAL_RECVD, the received value is not
			 * inherited.
			 */
			if (strcmp(source, ZPROP_SOURCE_VAL_RECVD) == 0) {
				relpath = "";
			} else {
				relpath = zhp->zfs_name + strlen(source);
				if (relpath[0] == '/')
					relpath++;
			}

			if ((zpool_get_prop(zhp->zpool_hdl,
			    ZPOOL_PROP_ALTROOT, buf, MAXPATHLEN, NULL)) ||
			    (strcmp(root, "-") == 0))
				root[0] = '\0';
			/*
			 * Special case an alternate root of '/'. This will
			 * avoid having multiple leading slashes in the
			 * mountpoint path.
			 */
			if (strcmp(root, "/") == 0)
				root++;

			/*
			 * If the mountpoint is '/' then skip over this
			 * if we are obtaining either an alternate root or
			 * an inherited mountpoint.
			 */
			if (str[1] == '\0' && (root[0] != '\0' ||
			    relpath[0] != '\0'))
				str++;

			if (relpath[0] == '\0')
				(void) snprintf(propbuf, proplen, "%s%s",
				    root, str);
			else
				(void) snprintf(propbuf, proplen, "%s%s%s%s",
				    root, str, relpath[0] == '@' ? "" : "/",
				    relpath);
		} else {
			/* 'legacy' or 'none' */
			(void) strlcpy(propbuf, str, proplen);
		}

		break;

	case ZFS_PROP_ORIGIN:
		(void) strlcpy(propbuf, getprop_string(zhp, prop, &source),
		    proplen);
		/*
		 * If there is no parent at all, return failure to indicate that
		 * it doesn't apply to this dataset.
		 */
		if (propbuf[0] == '\0')
			return (-1);
		break;

	case ZFS_PROP_QUOTA:
	case ZFS_PROP_REFQUOTA:
	case ZFS_PROP_RESERVATION:
	case ZFS_PROP_REFRESERVATION:

		if (get_numeric_property(zhp, prop, src, &source, &val) != 0)
			return (-1);

		/*
		 * If quota or reservation is 0, we translate this into 'none'
		 * (unless literal is set), and indicate that it's the default
		 * value.  Otherwise, we print the number nicely and indicate
		 * that its set locally.
		 */
		if (val == 0) {
			if (literal)
				(void) strlcpy(propbuf, "0", proplen);
			else
				(void) strlcpy(propbuf, "none", proplen);
		} else {
			if (literal)
				(void) snprintf(propbuf, proplen, "%llu",
				    (u_longlong_t)val);
			else
				zfs_nicenum(val, propbuf, proplen);
		}
		break;

	case ZFS_PROP_COMPRESSRATIO:
		if (get_numeric_property(zhp, prop, src, &source, &val) != 0)
			return (-1);
		(void) snprintf(propbuf, proplen, "%llu.%02llux",
		    (u_longlong_t)(val / 100),
		    (u_longlong_t)(val % 100));
		break;

	case ZFS_PROP_TYPE:
		switch (zhp->zfs_type) {
		case ZFS_TYPE_FILESYSTEM:
			str = "filesystem";
			break;
		case ZFS_TYPE_VOLUME:
			str = "volume";
			break;
		case ZFS_TYPE_SNAPSHOT:
			str = "snapshot";
			break;
		default:
			abort();
		}
		(void) snprintf(propbuf, proplen, "%s", str);
		break;

	case ZFS_PROP_MOUNTED:
		/*
		 * The 'mounted' property is a pseudo-property that described
		 * whether the filesystem is currently mounted.  Even though
		 * it's a boolean value, the typical values of "on" and "off"
		 * don't make sense, so we translate to "yes" and "no".
		 */
		if (get_numeric_property(zhp, ZFS_PROP_MOUNTED,
		    src, &source, &val) != 0)
			return (-1);
		if (val)
			(void) strlcpy(propbuf, "yes", proplen);
		else
			(void) strlcpy(propbuf, "no", proplen);
		break;

	case ZFS_PROP_NAME:
		/*
		 * The 'name' property is a pseudo-property derived from the
		 * dataset name.  It is presented as a real property to simplify
		 * consumers.
		 */
		(void) strlcpy(propbuf, zhp->zfs_name, proplen);
		break;

	case ZFS_PROP_MLSLABEL:
		{
			m_label_t *new_sl = NULL;
			char *ascii = NULL;	/* human readable label */

			(void) strlcpy(propbuf,
			    getprop_string(zhp, prop, &source), proplen);

			if (literal || (strcasecmp(propbuf,
			    ZFS_MLSLABEL_DEFAULT) == 0))
				break;

			/*
			 * Try to translate the internal hex string to
			 * human-readable output.  If there are any
			 * problems just use the hex string.
			 */

			if (str_to_label(propbuf, &new_sl, MAC_LABEL,
			    L_NO_CORRECTION, NULL) == -1) {
				m_label_free(new_sl);
				break;
			}

			if (label_to_str(new_sl, &ascii, M_LABEL,
			    DEF_NAMES) != 0) {
				if (ascii)
					free(ascii);
				m_label_free(new_sl);
				break;
			}
			m_label_free(new_sl);

			(void) strlcpy(propbuf, ascii, proplen);
			free(ascii);
		}
		break;

	default:
		switch (zfs_prop_get_type(prop)) {
		case PROP_TYPE_NUMBER:
			if (get_numeric_property(zhp, prop, src,
			    &source, &val) != 0)
				return (-1);
			if (literal)
				(void) snprintf(propbuf, proplen, "%llu",
				    (u_longlong_t)val);
			else
				zfs_nicenum(val, propbuf, proplen);
			break;

		case PROP_TYPE_STRING:
			(void) strlcpy(propbuf,
			    getprop_string(zhp, prop, &source), proplen);
			break;

		case PROP_TYPE_INDEX:
			if (get_numeric_property(zhp, prop, src,
			    &source, &val) != 0)
				return (-1);
			if (zfs_prop_index_to_string(prop, val, &strval) != 0)
				return (-1);
			(void) strlcpy(propbuf, strval, proplen);
			break;

		default:
			abort();
		}
	}

	get_source(zhp, src, source, statbuf, statlen);

	return (0);
}

/*
 * Utility function to get the given numeric property.  Does no validation that
 * the given property is the appropriate type; should only be used with
 * hard-coded property types.
 */
uint64_t
zfs_prop_get_int(zfs_handle_t *zhp, zfs_prop_t prop)
{
	char *source;
	uint64_t val;

	(void) get_numeric_property(zhp, prop, NULL, &source, &val);

	return (val);
}

int
zfs_prop_set_int(zfs_handle_t *zhp, zfs_prop_t prop, uint64_t val)
{
	char buf[64];

	(void) snprintf(buf, sizeof (buf), "%llu", (longlong_t)val);
	return (zfs_prop_set(zhp, zfs_prop_to_name(prop), buf));
}

/*
 * Similar to zfs_prop_get(), but returns the value as an integer.
 */
int
zfs_prop_get_numeric(zfs_handle_t *zhp, zfs_prop_t prop, uint64_t *value,
    zprop_source_t *src, char *statbuf, size_t statlen)
{
	char *source;

	/*
	 * Check to see if this property applies to our object
	 */
	if (!zfs_prop_valid_for_type(prop, zhp->zfs_type)) {
		return (zfs_error_fmt(zhp->zfs_hdl, EZFS_PROPTYPE,
		    dgettext(TEXT_DOMAIN, "cannot get property '%s'"),
		    zfs_prop_to_name(prop)));
	}

	if (src)
		*src = ZPROP_SRC_NONE;

	if (get_numeric_property(zhp, prop, src, &source, value) != 0)
		return (-1);

	get_source(zhp, src, source, statbuf, statlen);

	return (0);
}

static int
idmap_id_to_numeric_domain_rid(uid_t id, boolean_t isuser,
    char **domainp, idmap_rid_t *ridp)
{
	idmap_get_handle_t *get_hdl = NULL;
	idmap_stat status;
	int err = EINVAL;

	if (idmap_get_create(&get_hdl) != IDMAP_SUCCESS)
		goto out;

	if (isuser) {
		err = idmap_get_sidbyuid(get_hdl, id,
		    IDMAP_REQ_FLG_USE_CACHE, domainp, ridp, &status);
	} else {
		err = idmap_get_sidbygid(get_hdl, id,
		    IDMAP_REQ_FLG_USE_CACHE, domainp, ridp, &status);
	}
	if (err == IDMAP_SUCCESS &&
	    idmap_get_mappings(get_hdl) == IDMAP_SUCCESS &&
	    status == IDMAP_SUCCESS)
		err = 0;
	else
		err = EINVAL;
out:
	if (get_hdl)
		idmap_get_destroy(get_hdl);
	return (err);
}

/*
 * convert the propname into parameters needed by kernel
 * Eg: userquota@ahrens -> ZFS_PROP_USERQUOTA, "", 126829
 * Eg: userused@matt@domain -> ZFS_PROP_USERUSED, "S-1-123-456", 789
 */
static int
userquota_propname_decode(const char *propname, boolean_t zoned,
    zfs_userquota_prop_t *typep, char *domain, int domainlen, uint64_t *ridp)
{
	zfs_userquota_prop_t type;
	char *cp, *end;
	char *numericsid = NULL;
	boolean_t isuser;

	domain[0] = '\0';

	/* Figure out the property type ({user|group}{quota|space}) */
	for (type = 0; type < ZFS_NUM_USERQUOTA_PROPS; type++) {
		if (strncmp(propname, zfs_userquota_prop_prefixes[type],
		    strlen(zfs_userquota_prop_prefixes[type])) == 0)
			break;
	}
	if (type == ZFS_NUM_USERQUOTA_PROPS)
		return (EINVAL);
	*typep = type;

	isuser = (type == ZFS_PROP_USERQUOTA ||
	    type == ZFS_PROP_USERUSED);

	cp = strchr(propname, '@') + 1;

	if (strchr(cp, '@')) {
		/*
		 * It's a SID name (eg "user@domain") that needs to be
		 * turned into S-1-domainID-RID.
		 */
		directory_error_t e;
		if (zoned && getzoneid() == GLOBAL_ZONEID)
			return (ENOENT);
		if (isuser) {
			e = directory_sid_from_user_name(NULL,
			    cp, &numericsid);
		} else {
			e = directory_sid_from_group_name(NULL,
			    cp, &numericsid);
		}
		if (e != NULL) {
			directory_error_free(e);
			return (ENOENT);
		}
		if (numericsid == NULL)
			return (ENOENT);
		cp = numericsid;
		/* will be further decoded below */
	}

	if (strncmp(cp, "S-1-", 4) == 0) {
		/* It's a numeric SID (eg "S-1-234-567-89") */
		(void) strlcpy(domain, cp, domainlen);
		cp = strrchr(domain, '-');
		*cp = '\0';
		cp++;

		errno = 0;
		*ridp = strtoull(cp, &end, 10);
		if (numericsid) {
			free(numericsid);
			numericsid = NULL;
		}
		if (errno != 0 || *end != '\0')
			return (EINVAL);
	} else if (!isdigit(*cp)) {
		/*
		 * It's a user/group name (eg "user") that needs to be
		 * turned into a uid/gid
		 */
		if (zoned && getzoneid() == GLOBAL_ZONEID)
			return (ENOENT);
		if (isuser) {
			struct passwd *pw;
			pw = getpwnam(cp);
			if (pw == NULL)
				return (ENOENT);
			*ridp = pw->pw_uid;
		} else {
			struct group *gr;
			gr = getgrnam(cp);
			if (gr == NULL)
				return (ENOENT);
			*ridp = gr->gr_gid;
		}
	} else {
		/* It's a user/group ID (eg "12345"). */
		uid_t id = strtoul(cp, &end, 10);
		idmap_rid_t rid;
		char *mapdomain;

		if (*end != '\0')
			return (EINVAL);
		if (id > MAXUID) {
			/* It's an ephemeral ID. */
			if (idmap_id_to_numeric_domain_rid(id, isuser,
			    &mapdomain, &rid) != 0)
				return (ENOENT);
			(void) strlcpy(domain, mapdomain, domainlen);
			*ridp = rid;
		} else {
			*ridp = id;
		}
	}

	ASSERT3P(numericsid, ==, NULL);
	return (0);
}

static int
zfs_prop_get_userquota_common(zfs_handle_t *zhp, const char *propname,
    uint64_t *propvalue, zfs_userquota_prop_t *typep)
{
	int err;
	zfs_cmd_t zc = { 0 };

	(void) strncpy(zc.zc_name, zhp->zfs_name, sizeof (zc.zc_name));

	err = userquota_propname_decode(propname,
	    zfs_prop_get_int(zhp, ZFS_PROP_ZONED),
	    typep, zc.zc_value, sizeof (zc.zc_value), &zc.zc_guid);
	zc.zc_objset_type = *typep;
	if (err)
		return (err);

	err = ioctl(zhp->zfs_hdl->libzfs_fd, ZFS_IOC_USERSPACE_ONE, &zc);
	if (err)
		return (err);

	*propvalue = zc.zc_cookie;
	return (0);
}

int
zfs_prop_get_userquota_int(zfs_handle_t *zhp, const char *propname,
    uint64_t *propvalue)
{
	zfs_userquota_prop_t type;

	return (zfs_prop_get_userquota_common(zhp, propname, propvalue,
	    &type));
}

int
zfs_prop_get_userquota(zfs_handle_t *zhp, const char *propname,
    char *propbuf, int proplen, boolean_t literal)
{
	int err;
	uint64_t propvalue;
	zfs_userquota_prop_t type;

	err = zfs_prop_get_userquota_common(zhp, propname, &propvalue,
	    &type);

	if (err)
		return (err);

	if (literal) {
		(void) snprintf(propbuf, proplen, "%llu", propvalue);
	} else if (propvalue == 0 &&
	    (type == ZFS_PROP_USERQUOTA || type == ZFS_PROP_GROUPQUOTA)) {
		(void) strlcpy(propbuf, "none", proplen);
	} else {
		zfs_nicenum(propvalue, propbuf, proplen);
	}
	return (0);
}

/*
 * Returns the name of the given zfs handle.
 */
const char *
zfs_get_name(const zfs_handle_t *zhp)
{
	return (zhp->zfs_name);
}

/*
 * Returns the type of the given zfs handle.
 */
zfs_type_t
zfs_get_type(const zfs_handle_t *zhp)
{
	return (zhp->zfs_type);
}

static int
zfs_do_list_ioctl(zfs_handle_t *zhp, int arg, zfs_cmd_t *zc)
{
	int rc;
	uint64_t	orig_cookie;

	orig_cookie = zc->zc_cookie;
top:
	(void) strlcpy(zc->zc_name, zhp->zfs_name, sizeof (zc->zc_name));
	rc = ioctl(zhp->zfs_hdl->libzfs_fd, arg, zc);

	if (rc == -1) {
		switch (errno) {
		case ENOMEM:
			/* expand nvlist memory and try again */
			if (zcmd_expand_dst_nvlist(zhp->zfs_hdl, zc) != 0) {
				zcmd_free_nvlists(zc);
				return (-1);
			}
			zc->zc_cookie = orig_cookie;
			goto top;
		/*
		 * An errno value of ESRCH indicates normal completion.
		 * If ENOENT is returned, then the underlying dataset
		 * has been removed since we obtained the handle.
		 */
		case ESRCH:
		case ENOENT:
			rc = 1;
			break;
		default:
			rc = zfs_standard_error(zhp->zfs_hdl, errno,
			    dgettext(TEXT_DOMAIN,
			    "cannot iterate filesystems"));
			break;
		}
	}
	return (rc);
}

/*
 * Iterate over all child filesystems
 */
int
zfs_iter_filesystems(zfs_handle_t *zhp, zfs_iter_f func, void *data)
{
	zfs_cmd_t zc = { 0 };
	zfs_handle_t *nzhp;
	int ret;

	if (zhp->zfs_type != ZFS_TYPE_FILESYSTEM)
		return (0);

	if (zcmd_alloc_dst_nvlist(zhp->zfs_hdl, &zc, 0) != 0)
		return (-1);

	while ((ret = zfs_do_list_ioctl(zhp, ZFS_IOC_DATASET_LIST_NEXT,
	    &zc)) == 0) {
		/*
		 * Silently ignore errors, as the only plausible explanation is
		 * that the pool has since been removed.
		 */
		if ((nzhp = make_dataset_handle_zc(zhp->zfs_hdl,
		    &zc)) == NULL) {
			continue;
		}

		if ((ret = func(nzhp, data)) != 0) {
			zcmd_free_nvlists(&zc);
			return (ret);
		}
	}
	zcmd_free_nvlists(&zc);
	return ((ret < 0) ? ret : 0);
}

/*
 * Iterate over all snapshots
 */
int
zfs_iter_snapshots(zfs_handle_t *zhp, zfs_iter_f func, void *data)
{
	zfs_cmd_t zc = { 0 };
	zfs_handle_t *nzhp;
	int ret;

	if (zhp->zfs_type == ZFS_TYPE_SNAPSHOT)
		return (0);

	if (zcmd_alloc_dst_nvlist(zhp->zfs_hdl, &zc, 0) != 0)
		return (-1);
	while ((ret = zfs_do_list_ioctl(zhp, ZFS_IOC_SNAPSHOT_LIST_NEXT,
	    &zc)) == 0) {

		if ((nzhp = make_dataset_handle_zc(zhp->zfs_hdl,
		    &zc)) == NULL) {
			continue;
		}

		if ((ret = func(nzhp, data)) != 0) {
			zcmd_free_nvlists(&zc);
			return (ret);
		}
	}
	zcmd_free_nvlists(&zc);
	return ((ret < 0) ? ret : 0);
}

/*
 * Iterate over all children, snapshots and filesystems
 */
int
zfs_iter_children(zfs_handle_t *zhp, zfs_iter_f func, void *data)
{
	int ret;

	if ((ret = zfs_iter_filesystems(zhp, func, data)) != 0)
		return (ret);

	return (zfs_iter_snapshots(zhp, func, data));
}

/*
 * Is one dataset name a child dataset of another?
 *
 * Needs to handle these cases:
 * Dataset 1	"a/foo"		"a/foo"		"a/foo"		"a/foo"
 * Dataset 2	"a/fo"		"a/foobar"	"a/bar/baz"	"a/foo/bar"
 * Descendant?	No.		No.		No.		Yes.
 */
static boolean_t
is_descendant(const char *ds1, const char *ds2)
{
	size_t d1len = strlen(ds1);

	/* ds2 can't be a descendant if it's smaller */
	if (strlen(ds2) < d1len)
		return (B_FALSE);

	/* otherwise, compare strings and verify that there's a '/' char */
	return (ds2[d1len] == '/' && (strncmp(ds1, ds2, d1len) == 0));
}

/*
 * Given a complete name, return just the portion that refers to the parent.
 * Can return NULL if this is a pool.
 */
static int
parent_name(const char *path, char *buf, size_t buflen)
{
	char *loc;

	if ((loc = strrchr(path, '/')) == NULL)
		return (-1);

	(void) strncpy(buf, path, MIN(buflen, loc - path));
	buf[loc - path] = '\0';

	return (0);
}

/*
 * If accept_ancestor is false, then check to make sure that the given path has
 * a parent, and that it exists.  If accept_ancestor is true, then find the
 * closest existing ancestor for the given path.  In prefixlen return the
 * length of already existing prefix of the given path.  We also fetch the
 * 'zoned' property, which is used to validate property settings when creating
 * new datasets.
 */
static int
check_parents(libzfs_handle_t *hdl, const char *path, uint64_t *zoned,
    boolean_t accept_ancestor, int *prefixlen)
{
	zfs_cmd_t zc = { 0 };
	char parent[ZFS_MAXNAMELEN];
	char *slash;
	zfs_handle_t *zhp;
	char errbuf[1024];
	uint64_t is_zoned;

	(void) snprintf(errbuf, sizeof (errbuf),
	    dgettext(TEXT_DOMAIN, "cannot create '%s'"), path);

	/* get parent, and check to see if this is just a pool */
	if (parent_name(path, parent, sizeof (parent)) != 0) {
		zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
		    "missing dataset name"));
		return (zfs_error(hdl, EZFS_INVALIDNAME, errbuf));
	}

	/* check to see if the pool exists */
	if ((slash = strchr(parent, '/')) == NULL)
		slash = parent + strlen(parent);
	(void) strncpy(zc.zc_name, parent, slash - parent);
	zc.zc_name[slash - parent] = '\0';
	if (ioctl(hdl->libzfs_fd, ZFS_IOC_OBJSET_STATS, &zc) != 0 &&
	    errno == ENOENT) {
		zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
		    "no such pool '%s'"), zc.zc_name);
		return (zfs_error(hdl, EZFS_NOENT, errbuf));
	}

	/* check to see if the parent dataset exists */
	while ((zhp = make_dataset_handle(hdl, parent)) == NULL) {
		if (errno == ENOENT && accept_ancestor) {
			/*
			 * Go deeper to find an ancestor, give up on top level.
			 */
			if (parent_name(parent, parent, sizeof (parent)) != 0) {
				zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
				    "no such pool '%s'"), zc.zc_name);
				return (zfs_error(hdl, EZFS_NOENT, errbuf));
			}
		} else if (errno == ENOENT) {
			zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
			    "parent does not exist"));
			return (zfs_error(hdl, EZFS_NOENT, errbuf));
		} else
			return (zfs_standard_error(hdl, errno, errbuf));
	}

	is_zoned = zfs_prop_get_int(zhp, ZFS_PROP_ZONED);
	if (zoned != NULL)
		*zoned = is_zoned;

	/* we are in a non-global zone, but parent is in the global zone */
	if (getzoneid() != GLOBAL_ZONEID && !is_zoned) {
		(void) zfs_standard_error(hdl, EPERM, errbuf);
		zfs_close(zhp);
		return (-1);
	}

	/* make sure parent is a filesystem */
	if (zfs_get_type(zhp) != ZFS_TYPE_FILESYSTEM) {
		zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
		    "parent is not a filesystem"));
		(void) zfs_error(hdl, EZFS_BADTYPE, errbuf);
		zfs_close(zhp);
		return (-1);
	}

	zfs_close(zhp);
	if (prefixlen != NULL)
		*prefixlen = strlen(parent);
	return (0);
}

/*
 * Finds whether the dataset of the given type(s) exists.
 */
boolean_t
zfs_dataset_exists(libzfs_handle_t *hdl, const char *path, zfs_type_t types)
{
	zfs_handle_t *zhp;

	if (!zfs_validate_name(hdl, path, types, B_FALSE))
		return (B_FALSE);

	/*
	 * Try to get stats for the dataset, which will tell us if it exists.
	 */
	if ((zhp = make_dataset_handle(hdl, path)) != NULL) {
		int ds_type = zhp->zfs_type;

		zfs_close(zhp);
		if (types & ds_type)
			return (B_TRUE);
	}
	return (B_FALSE);
}

/*
 * Given a path to 'target', create all the ancestors between
 * the prefixlen portion of the path, and the target itself.
 * Fail if the initial prefixlen-ancestor does not already exist.
 */
int
create_parents(libzfs_handle_t *hdl, char *target, int prefixlen)
{
	zfs_handle_t *h;
	char *cp;
	const char *opname;

	/* make sure prefix exists */
	cp = target + prefixlen;
	if (*cp != '/') {
		assert(strchr(cp, '/') == NULL);
		h = zfs_open(hdl, target, ZFS_TYPE_FILESYSTEM);
	} else {
		*cp = '\0';
		h = zfs_open(hdl, target, ZFS_TYPE_FILESYSTEM);
		*cp = '/';
	}
	if (h == NULL)
		return (-1);
	zfs_close(h);

	/*
	 * Attempt to create, mount, and share any ancestor filesystems,
	 * up to the prefixlen-long one.
	 */
	for (cp = target + prefixlen + 1;
	    cp = strchr(cp, '/'); *cp = '/', cp++) {
		char *logstr;

		*cp = '\0';

		h = make_dataset_handle(hdl, target);
		if (h) {
			/* it already exists, nothing to do here */
			zfs_close(h);
			continue;
		}

		logstr = hdl->libzfs_log_str;
		hdl->libzfs_log_str = NULL;
		if (zfs_create(hdl, target, ZFS_TYPE_FILESYSTEM,
		    NULL) != 0) {
			hdl->libzfs_log_str = logstr;
			opname = dgettext(TEXT_DOMAIN, "create");
			goto ancestorerr;
		}

		hdl->libzfs_log_str = logstr;
		h = zfs_open(hdl, target, ZFS_TYPE_FILESYSTEM);
		if (h == NULL) {
			opname = dgettext(TEXT_DOMAIN, "open");
			goto ancestorerr;
		}

		if (zfs_mount(h, NULL, 0) != 0) {
			opname = dgettext(TEXT_DOMAIN, "mount");
			goto ancestorerr;
		}

		if (zfs_share(h) != 0) {
			opname = dgettext(TEXT_DOMAIN, "share");
			goto ancestorerr;
		}

		zfs_close(h);
	}

	return (0);

ancestorerr:
	zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
	    "failed to %s ancestor '%s'"), opname, target);
	return (-1);
}

/*
 * Creates non-existing ancestors of the given path.
 */
int
zfs_create_ancestors(libzfs_handle_t *hdl, const char *path)
{
	int prefix;
	char *path_copy;
	int rc;

	if (check_parents(hdl, path, NULL, B_TRUE, &prefix) != 0)
		return (-1);

	if ((path_copy = strdup(path)) != NULL) {
		rc = create_parents(hdl, path_copy, prefix);
		free(path_copy);
	}
	if (path_copy == NULL || rc != 0)
		return (-1);

	return (0);
}

/*
 * Create a new filesystem or volume.
 */
int
zfs_create(libzfs_handle_t *hdl, const char *path, zfs_type_t type,
    nvlist_t *props)
{
	zfs_cmd_t zc = { 0 };
	int ret;
	uint64_t size = 0;
	uint64_t blocksize = zfs_prop_default_numeric(ZFS_PROP_VOLBLOCKSIZE);
	char errbuf[1024];
	uint64_t zoned;

	(void) snprintf(errbuf, sizeof (errbuf), dgettext(TEXT_DOMAIN,
	    "cannot create '%s'"), path);

	/* validate the path, taking care to note the extended error message */
	if (!zfs_validate_name(hdl, path, type, B_TRUE))
		return (zfs_error(hdl, EZFS_INVALIDNAME, errbuf));

	/* validate parents exist */
	if (check_parents(hdl, path, &zoned, B_FALSE, NULL) != 0)
		return (-1);

	/*
	 * The failure modes when creating a dataset of a different type over
	 * one that already exists is a little strange.  In particular, if you
	 * try to create a dataset on top of an existing dataset, the ioctl()
	 * will return ENOENT, not EEXIST.  To prevent this from happening, we
	 * first try to see if the dataset exists.
	 */
	(void) strlcpy(zc.zc_name, path, sizeof (zc.zc_name));
	if (zfs_dataset_exists(hdl, zc.zc_name, ZFS_TYPE_DATASET)) {
		zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
		    "dataset already exists"));
		return (zfs_error(hdl, EZFS_EXISTS, errbuf));
	}

	if (type == ZFS_TYPE_VOLUME)
		zc.zc_objset_type = DMU_OST_ZVOL;
	else
		zc.zc_objset_type = DMU_OST_ZFS;

	if (props && (props = zfs_valid_proplist(hdl, type, props,
	    zoned, NULL, errbuf)) == 0)
		return (-1);

	if (type == ZFS_TYPE_VOLUME) {
		/*
		 * If we are creating a volume, the size and block size must
		 * satisfy a few restraints.  First, the blocksize must be a
		 * valid block size between SPA_{MIN,MAX}BLOCKSIZE.  Second, the
		 * volsize must be a multiple of the block size, and cannot be
		 * zero.
		 */
		if (props == NULL || nvlist_lookup_uint64(props,
		    zfs_prop_to_name(ZFS_PROP_VOLSIZE), &size) != 0) {
			nvlist_free(props);
			zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
			    "missing volume size"));
			return (zfs_error(hdl, EZFS_BADPROP, errbuf));
		}

		if ((ret = nvlist_lookup_uint64(props,
		    zfs_prop_to_name(ZFS_PROP_VOLBLOCKSIZE),
		    &blocksize)) != 0) {
			if (ret == ENOENT) {
				blocksize = zfs_prop_default_numeric(
				    ZFS_PROP_VOLBLOCKSIZE);
			} else {
				nvlist_free(props);
				zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
				    "missing volume block size"));
				return (zfs_error(hdl, EZFS_BADPROP, errbuf));
			}
		}

		if (size == 0) {
			nvlist_free(props);
			zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
			    "volume size cannot be zero"));
			return (zfs_error(hdl, EZFS_BADPROP, errbuf));
		}

		if (size % blocksize != 0) {
			nvlist_free(props);
			zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
			    "volume size must be a multiple of volume block "
			    "size"));
			return (zfs_error(hdl, EZFS_BADPROP, errbuf));
		}
	}

	if (props && zcmd_write_src_nvlist(hdl, &zc, props) != 0)
		return (-1);
	nvlist_free(props);

	/* create the dataset */
	ret = zfs_ioctl(hdl, ZFS_IOC_CREATE, &zc);

	zcmd_free_nvlists(&zc);

	/* check for failure */
	if (ret != 0) {
		char parent[ZFS_MAXNAMELEN];
		(void) parent_name(path, parent, sizeof (parent));

		switch (errno) {
		case ENOENT:
			zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
			    "no such parent '%s'"), parent);
			return (zfs_error(hdl, EZFS_NOENT, errbuf));

		case EINVAL:
			zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
			    "parent '%s' is not a filesystem"), parent);
			return (zfs_error(hdl, EZFS_BADTYPE, errbuf));

		case EDOM:
			zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
			    "volume block size must be power of 2 from "
			    "%u to %uk"),
			    (uint_t)SPA_MINBLOCKSIZE,
			    (uint_t)SPA_MAXBLOCKSIZE >> 10);

			return (zfs_error(hdl, EZFS_BADPROP, errbuf));

		case ENOTSUP:
			zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
			    "pool must be upgraded to set this "
			    "property or value"));
			return (zfs_error(hdl, EZFS_BADVERSION, errbuf));
#ifdef _ILP32
		case EOVERFLOW:
			/*
			 * This platform can't address a volume this big.
			 */
			if (type == ZFS_TYPE_VOLUME)
				return (zfs_error(hdl, EZFS_VOLTOOBIG,
				    errbuf));
#endif
			/* FALLTHROUGH */
		default:
			return (zfs_standard_error(hdl, errno, errbuf));
		}
	}

	return (0);
}

/*
 * Destroys the given dataset.  The caller must make sure that the filesystem
 * isn't mounted, and that there are no active dependents.
 */
int
zfs_destroy(zfs_handle_t *zhp, boolean_t defer)
{
	zfs_cmd_t zc = { 0 };

	(void) strlcpy(zc.zc_name, zhp->zfs_name, sizeof (zc.zc_name));

	if (ZFS_IS_VOLUME(zhp)) {
		zc.zc_objset_type = DMU_OST_ZVOL;
	} else {
		zc.zc_objset_type = DMU_OST_ZFS;
	}

	zc.zc_defer_destroy = defer;
	if (zfs_ioctl(zhp->zfs_hdl, ZFS_IOC_DESTROY, &zc) != 0) {
		return (zfs_standard_error_fmt(zhp->zfs_hdl, errno,
		    dgettext(TEXT_DOMAIN, "cannot destroy '%s'"),
		    zhp->zfs_name));
	}

	remove_mountpoint(zhp);

	return (0);
}

struct destroydata {
	char *snapname;
	boolean_t gotone;
	boolean_t closezhp;
};

static int
zfs_check_snap_cb(zfs_handle_t *zhp, void *arg)
{
	struct destroydata *dd = arg;
	zfs_handle_t *szhp;
	char name[ZFS_MAXNAMELEN];
	boolean_t closezhp = dd->closezhp;
	int rv = 0;

	(void) strlcpy(name, zhp->zfs_name, sizeof (name));
	(void) strlcat(name, "@", sizeof (name));
	(void) strlcat(name, dd->snapname, sizeof (name));

	szhp = make_dataset_handle(zhp->zfs_hdl, name);
	if (szhp) {
		dd->gotone = B_TRUE;
		zfs_close(szhp);
	}

	dd->closezhp = B_TRUE;
	if (!dd->gotone)
		rv = zfs_iter_filesystems(zhp, zfs_check_snap_cb, arg);
	if (closezhp)
		zfs_close(zhp);
	return (rv);
}

/*
 * Destroys all snapshots with the given name in zhp & descendants.
 */
int
zfs_destroy_snaps(zfs_handle_t *zhp, char *snapname, boolean_t defer)
{
	zfs_cmd_t zc = { 0 };
	int ret;
	struct destroydata dd = { 0 };

	dd.snapname = snapname;
	(void) zfs_check_snap_cb(zhp, &dd);

	if (!dd.gotone) {
		return (zfs_standard_error_fmt(zhp->zfs_hdl, ENOENT,
		    dgettext(TEXT_DOMAIN, "cannot destroy '%s@%s'"),
		    zhp->zfs_name, snapname));
	}

	(void) strlcpy(zc.zc_name, zhp->zfs_name, sizeof (zc.zc_name));
	(void) strlcpy(zc.zc_value, snapname, sizeof (zc.zc_value));
	zc.zc_defer_destroy = defer;

	ret = zfs_ioctl(zhp->zfs_hdl, ZFS_IOC_DESTROY_SNAPS, &zc);
	if (ret != 0) {
		char errbuf[1024];

		(void) snprintf(errbuf, sizeof (errbuf), dgettext(TEXT_DOMAIN,
		    "cannot destroy '%s@%s'"), zc.zc_name, snapname);

		switch (errno) {
		case EEXIST:
			zfs_error_aux(zhp->zfs_hdl, dgettext(TEXT_DOMAIN,
			    "snapshot is cloned"));
			return (zfs_error(zhp->zfs_hdl, EZFS_EXISTS, errbuf));

		default:
			return (zfs_standard_error(zhp->zfs_hdl, errno,
			    errbuf));
		}
	}

	return (0);
}

/*
 * Clones the given dataset.  The target must be of the same type as the source.
 */
int
zfs_clone(zfs_handle_t *zhp, const char *target, nvlist_t *props)
{
	zfs_cmd_t zc = { 0 };
	char parent[ZFS_MAXNAMELEN];
	int ret;
	char errbuf[1024];
	libzfs_handle_t *hdl = zhp->zfs_hdl;
	zfs_type_t type;
	uint64_t zoned;

	assert(zhp->zfs_type == ZFS_TYPE_SNAPSHOT);

	(void) snprintf(errbuf, sizeof (errbuf), dgettext(TEXT_DOMAIN,
	    "cannot create '%s'"), target);

	/* validate the target name */
	if (!zfs_validate_name(hdl, target, ZFS_TYPE_FILESYSTEM, B_TRUE))
		return (zfs_error(hdl, EZFS_INVALIDNAME, errbuf));

	/* validate parents exist */
	if (check_parents(hdl, target, &zoned, B_FALSE, NULL) != 0)
		return (-1);

	(void) parent_name(target, parent, sizeof (parent));

	/* do the clone */
	if (ZFS_IS_VOLUME(zhp)) {
		zc.zc_objset_type = DMU_OST_ZVOL;
		type = ZFS_TYPE_VOLUME;
	} else {
		zc.zc_objset_type = DMU_OST_ZFS;
		type = ZFS_TYPE_FILESYSTEM;
	}

	if (props) {
		if ((props = zfs_valid_proplist(hdl, type, props, zoned,
		    zhp, errbuf)) == NULL)
			return (-1);

		if (zcmd_write_src_nvlist(hdl, &zc, props) != 0) {
			nvlist_free(props);
			return (-1);
		}

		nvlist_free(props);
	}

	(void) strlcpy(zc.zc_name, target, sizeof (zc.zc_name));
	(void) strlcpy(zc.zc_value, zhp->zfs_name, sizeof (zc.zc_value));
	ret = zfs_ioctl(zhp->zfs_hdl, ZFS_IOC_CREATE, &zc);

	zcmd_free_nvlists(&zc);

	if (ret != 0) {
		switch (errno) {

		case ENOENT:
			/*
			 * The parent doesn't exist.  We should have caught this
			 * above, but there may a race condition that has since
			 * destroyed the parent.
			 *
			 * At this point, we don't know whether it's the source
			 * that doesn't exist anymore, or whether the target
			 * dataset doesn't exist.
			 */
			zfs_error_aux(zhp->zfs_hdl, dgettext(TEXT_DOMAIN,
			    "no such parent '%s'"), parent);
			return (zfs_error(zhp->zfs_hdl, EZFS_NOENT, errbuf));

		case EXDEV:
			zfs_error_aux(zhp->zfs_hdl, dgettext(TEXT_DOMAIN,
			    "source and target pools differ"));
			return (zfs_error(zhp->zfs_hdl, EZFS_CROSSTARGET,
			    errbuf));

		default:
			return (zfs_standard_error(zhp->zfs_hdl, errno,
			    errbuf));
		}
	}

	return (ret);
}

/*
 * Promotes the given clone fs to be the clone parent.
 */
int
zfs_promote(zfs_handle_t *zhp)
{
	libzfs_handle_t *hdl = zhp->zfs_hdl;
	zfs_cmd_t zc = { 0 };
	char parent[MAXPATHLEN];
	int ret;
	char errbuf[1024];

	(void) snprintf(errbuf, sizeof (errbuf), dgettext(TEXT_DOMAIN,
	    "cannot promote '%s'"), zhp->zfs_name);

	if (zhp->zfs_type == ZFS_TYPE_SNAPSHOT) {
		zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
		    "snapshots can not be promoted"));
		return (zfs_error(hdl, EZFS_BADTYPE, errbuf));
	}

	(void) strlcpy(parent, zhp->zfs_dmustats.dds_origin, sizeof (parent));
	if (parent[0] == '\0') {
		zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
		    "not a cloned filesystem"));
		return (zfs_error(hdl, EZFS_BADTYPE, errbuf));
	}

	(void) strlcpy(zc.zc_value, zhp->zfs_dmustats.dds_origin,
	    sizeof (zc.zc_value));
	(void) strlcpy(zc.zc_name, zhp->zfs_name, sizeof (zc.zc_name));
	ret = zfs_ioctl(hdl, ZFS_IOC_PROMOTE, &zc);

	if (ret != 0) {
		int save_errno = errno;

		switch (save_errno) {
		case EEXIST:
			/* There is a conflicting snapshot name. */
			zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
			    "conflicting snapshot '%s' from parent '%s'"),
			    zc.zc_string, parent);
			return (zfs_error(hdl, EZFS_EXISTS, errbuf));

		default:
			return (zfs_standard_error(hdl, save_errno, errbuf));
		}
	}
	return (ret);
}

/*
 * Takes a snapshot of the given dataset.
 */
int
zfs_snapshot(libzfs_handle_t *hdl, const char *path, boolean_t recursive,
    nvlist_t *props)
{
	const char *delim;
	char parent[ZFS_MAXNAMELEN];
	zfs_handle_t *zhp;
	zfs_cmd_t zc = { 0 };
	int ret;
	char errbuf[1024];

	(void) snprintf(errbuf, sizeof (errbuf), dgettext(TEXT_DOMAIN,
	    "cannot snapshot '%s'"), path);

	/* validate the target name */
	if (!zfs_validate_name(hdl, path, ZFS_TYPE_SNAPSHOT, B_TRUE))
		return (zfs_error(hdl, EZFS_INVALIDNAME, errbuf));

	if (props) {
		if ((props = zfs_valid_proplist(hdl, ZFS_TYPE_SNAPSHOT,
		    props, B_FALSE, NULL, errbuf)) == NULL)
			return (-1);

		if (zcmd_write_src_nvlist(hdl, &zc, props) != 0) {
			nvlist_free(props);
			return (-1);
		}

		nvlist_free(props);
	}

	/* make sure the parent exists and is of the appropriate type */
	delim = strchr(path, '@');
	(void) strncpy(parent, path, delim - path);
	parent[delim - path] = '\0';

	if ((zhp = zfs_open(hdl, parent, ZFS_TYPE_FILESYSTEM |
	    ZFS_TYPE_VOLUME)) == NULL) {
		zcmd_free_nvlists(&zc);
		return (-1);
	}

	(void) strlcpy(zc.zc_name, zhp->zfs_name, sizeof (zc.zc_name));
	(void) strlcpy(zc.zc_value, delim+1, sizeof (zc.zc_value));
	if (ZFS_IS_VOLUME(zhp))
		zc.zc_objset_type = DMU_OST_ZVOL;
	else
		zc.zc_objset_type = DMU_OST_ZFS;
	zc.zc_cookie = recursive;
	ret = zfs_ioctl(zhp->zfs_hdl, ZFS_IOC_SNAPSHOT, &zc);

	zcmd_free_nvlists(&zc);

	/*
	 * if it was recursive, the one that actually failed will be in
	 * zc.zc_name.
	 */
	if (ret != 0) {
		(void) snprintf(errbuf, sizeof (errbuf), dgettext(TEXT_DOMAIN,
		    "cannot create snapshot '%s@%s'"), zc.zc_name, zc.zc_value);
		(void) zfs_standard_error(hdl, errno, errbuf);
	}

	zfs_close(zhp);

	return (ret);
}

/*
 * Destroy any more recent snapshots.  We invoke this callback on any dependents
 * of the snapshot first.  If the 'cb_dependent' member is non-zero, then this
 * is a dependent and we should just destroy it without checking the transaction
 * group.
 */
typedef struct rollback_data {
	const char	*cb_target;		/* the snapshot */
	uint64_t	cb_create;		/* creation time reference */
	boolean_t	cb_error;
	boolean_t	cb_dependent;
	boolean_t	cb_force;
} rollback_data_t;

static int
rollback_destroy(zfs_handle_t *zhp, void *data)
{
	rollback_data_t *cbp = data;

	if (!cbp->cb_dependent) {
		if (strcmp(zhp->zfs_name, cbp->cb_target) != 0 &&
		    zfs_get_type(zhp) == ZFS_TYPE_SNAPSHOT &&
		    zfs_prop_get_int(zhp, ZFS_PROP_CREATETXG) >
		    cbp->cb_create) {
			char *logstr;

			cbp->cb_dependent = B_TRUE;
			cbp->cb_error |= zfs_iter_dependents(zhp, B_FALSE,
			    rollback_destroy, cbp);
			cbp->cb_dependent = B_FALSE;

			logstr = zhp->zfs_hdl->libzfs_log_str;
			zhp->zfs_hdl->libzfs_log_str = NULL;
			cbp->cb_error |= zfs_destroy(zhp, B_FALSE);
			zhp->zfs_hdl->libzfs_log_str = logstr;
		}
	} else {
		/* We must destroy this clone; first unmount it */
		prop_changelist_t *clp;

		clp = changelist_gather(zhp, ZFS_PROP_NAME, 0,
		    cbp->cb_force ? MS_FORCE: 0);
		if (clp == NULL || changelist_prefix(clp) != 0) {
			cbp->cb_error = B_TRUE;
			zfs_close(zhp);
			return (0);
		}
		if (zfs_destroy(zhp, B_FALSE) != 0)
			cbp->cb_error = B_TRUE;
		else
			changelist_remove(clp, zhp->zfs_name);
		(void) changelist_postfix(clp);
		changelist_free(clp);
	}

	zfs_close(zhp);
	return (0);
}

/*
 * Given a dataset, rollback to a specific snapshot, discarding any
 * data changes since then and making it the active dataset.
 *
 * Any snapshots more recent than the target are destroyed, along with
 * their dependents.
 */
int
zfs_rollback(zfs_handle_t *zhp, zfs_handle_t *snap, boolean_t force)
{
	rollback_data_t cb = { 0 };
	int err;
	zfs_cmd_t zc = { 0 };
	boolean_t restore_resv = 0;
	uint64_t old_volsize, new_volsize;
	zfs_prop_t resv_prop;

	assert(zhp->zfs_type == ZFS_TYPE_FILESYSTEM ||
	    zhp->zfs_type == ZFS_TYPE_VOLUME);

	/*
	 * Destroy all recent snapshots and its dependends.
	 */
	cb.cb_force = force;
	cb.cb_target = snap->zfs_name;
	cb.cb_create = zfs_prop_get_int(snap, ZFS_PROP_CREATETXG);
	(void) zfs_iter_children(zhp, rollback_destroy, &cb);

	if (cb.cb_error)
		return (-1);

	/*
	 * Now that we have verified that the snapshot is the latest,
	 * rollback to the given snapshot.
	 */

	if (zhp->zfs_type == ZFS_TYPE_VOLUME) {
		if (zfs_which_resv_prop(zhp, &resv_prop) < 0)
			return (-1);
		old_volsize = zfs_prop_get_int(zhp, ZFS_PROP_VOLSIZE);
		restore_resv =
		    (old_volsize == zfs_prop_get_int(zhp, resv_prop));
	}

	(void) strlcpy(zc.zc_name, zhp->zfs_name, sizeof (zc.zc_name));

	if (ZFS_IS_VOLUME(zhp))
		zc.zc_objset_type = DMU_OST_ZVOL;
	else
		zc.zc_objset_type = DMU_OST_ZFS;

	/*
	 * We rely on zfs_iter_children() to verify that there are no
	 * newer snapshots for the given dataset.  Therefore, we can
	 * simply pass the name on to the ioctl() call.  There is still
	 * an unlikely race condition where the user has taken a
	 * snapshot since we verified that this was the most recent.
	 *
	 */
	if ((err = zfs_ioctl(zhp->zfs_hdl, ZFS_IOC_ROLLBACK, &zc)) != 0) {
		(void) zfs_standard_error_fmt(zhp->zfs_hdl, errno,
		    dgettext(TEXT_DOMAIN, "cannot rollback '%s'"),
		    zhp->zfs_name);
		return (err);
	}

	/*
	 * For volumes, if the pre-rollback volsize matched the pre-
	 * rollback reservation and the volsize has changed then set
	 * the reservation property to the post-rollback volsize.
	 * Make a new handle since the rollback closed the dataset.
	 */
	if ((zhp->zfs_type == ZFS_TYPE_VOLUME) &&
	    (zhp = make_dataset_handle(zhp->zfs_hdl, zhp->zfs_name))) {
		if (restore_resv) {
			new_volsize = zfs_prop_get_int(zhp, ZFS_PROP_VOLSIZE);
			if (old_volsize != new_volsize)
				err = zfs_prop_set_int(zhp, resv_prop,
				    new_volsize);
		}
		zfs_close(zhp);
	}
	return (err);
}

/*
 * Iterate over all dependents for a given dataset.  This includes both
 * hierarchical dependents (children) and data dependents (snapshots and
 * clones).  The bulk of the processing occurs in get_dependents() in
 * libzfs_graph.c.
 */
int
zfs_iter_dependents(zfs_handle_t *zhp, boolean_t allowrecursion,
    zfs_iter_f func, void *data)
{
	char **dependents;
	size_t count;
	int i;
	zfs_handle_t *child;
	int ret = 0;

	if (get_dependents(zhp->zfs_hdl, allowrecursion, zhp->zfs_name,
	    &dependents, &count) != 0)
		return (-1);

	for (i = 0; i < count; i++) {
		if ((child = make_dataset_handle(zhp->zfs_hdl,
		    dependents[i])) == NULL)
			continue;

		if ((ret = func(child, data)) != 0)
			break;
	}

	for (i = 0; i < count; i++)
		free(dependents[i]);
	free(dependents);

	return (ret);
}

/*
 * Renames the given dataset.
 */
int
zfs_rename(zfs_handle_t *zhp, const char *target, boolean_t recursive)
{
	int ret;
	zfs_cmd_t zc = { 0 };
	char *delim;
	prop_changelist_t *cl = NULL;
	zfs_handle_t *zhrp = NULL;
	char *parentname = NULL;
	char parent[ZFS_MAXNAMELEN];
	libzfs_handle_t *hdl = zhp->zfs_hdl;
	char errbuf[1024];

	/* if we have the same exact name, just return success */
	if (strcmp(zhp->zfs_name, target) == 0)
		return (0);

	(void) snprintf(errbuf, sizeof (errbuf), dgettext(TEXT_DOMAIN,
	    "cannot rename to '%s'"), target);

	/*
	 * Make sure the target name is valid
	 */
	if (zhp->zfs_type == ZFS_TYPE_SNAPSHOT) {
		if ((strchr(target, '@') == NULL) ||
		    *target == '@') {
			/*
			 * Snapshot target name is abbreviated,
			 * reconstruct full dataset name
			 */
			(void) strlcpy(parent, zhp->zfs_name,
			    sizeof (parent));
			delim = strchr(parent, '@');
			if (strchr(target, '@') == NULL)
				*(++delim) = '\0';
			else
				*delim = '\0';
			(void) strlcat(parent, target, sizeof (parent));
			target = parent;
		} else {
			/*
			 * Make sure we're renaming within the same dataset.
			 */
			delim = strchr(target, '@');
			if (strncmp(zhp->zfs_name, target, delim - target)
			    != 0 || zhp->zfs_name[delim - target] != '@') {
				zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
				    "snapshots must be part of same "
				    "dataset"));
				return (zfs_error(hdl, EZFS_CROSSTARGET,
				    errbuf));
			}
		}
		if (!zfs_validate_name(hdl, target, zhp->zfs_type, B_TRUE))
			return (zfs_error(hdl, EZFS_INVALIDNAME, errbuf));
	} else {
		if (recursive) {
			zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
			    "recursive rename must be a snapshot"));
			return (zfs_error(hdl, EZFS_BADTYPE, errbuf));
		}

		if (!zfs_validate_name(hdl, target, zhp->zfs_type, B_TRUE))
			return (zfs_error(hdl, EZFS_INVALIDNAME, errbuf));

		/* validate parents */
		if (check_parents(hdl, target, NULL, B_FALSE, NULL) != 0)
			return (-1);

		/* make sure we're in the same pool */
		verify((delim = strchr(target, '/')) != NULL);
		if (strncmp(zhp->zfs_name, target, delim - target) != 0 ||
		    zhp->zfs_name[delim - target] != '/') {
			zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
			    "datasets must be within same pool"));
			return (zfs_error(hdl, EZFS_CROSSTARGET, errbuf));
		}

		/* new name cannot be a child of the current dataset name */
		if (is_descendant(zhp->zfs_name, target)) {
			zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
			    "New dataset name cannot be a descendant of "
			    "current dataset name"));
			return (zfs_error(hdl, EZFS_INVALIDNAME, errbuf));
		}
	}

	(void) snprintf(errbuf, sizeof (errbuf),
	    dgettext(TEXT_DOMAIN, "cannot rename '%s'"), zhp->zfs_name);

	if (getzoneid() == GLOBAL_ZONEID &&
	    zfs_prop_get_int(zhp, ZFS_PROP_ZONED)) {
		zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
		    "dataset is used in a non-global zone"));
		return (zfs_error(hdl, EZFS_ZONED, errbuf));
	}

	if (recursive) {

		parentname = zfs_strdup(zhp->zfs_hdl, zhp->zfs_name);
		if (parentname == NULL) {
			ret = -1;
			goto error;
		}
		delim = strchr(parentname, '@');
		*delim = '\0';
		zhrp = zfs_open(zhp->zfs_hdl, parentname, ZFS_TYPE_DATASET);
		if (zhrp == NULL) {
			ret = -1;
			goto error;
		}

	} else {
		if ((cl = changelist_gather(zhp, ZFS_PROP_NAME, 0, 0)) == NULL)
			return (-1);

		if (changelist_haszonedchild(cl)) {
			zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
			    "child dataset with inherited mountpoint is used "
			    "in a non-global zone"));
			(void) zfs_error(hdl, EZFS_ZONED, errbuf);
			goto error;
		}

		if ((ret = changelist_prefix(cl)) != 0)
			goto error;
	}

	if (ZFS_IS_VOLUME(zhp))
		zc.zc_objset_type = DMU_OST_ZVOL;
	else
		zc.zc_objset_type = DMU_OST_ZFS;

	(void) strlcpy(zc.zc_name, zhp->zfs_name, sizeof (zc.zc_name));
	(void) strlcpy(zc.zc_value, target, sizeof (zc.zc_value));

	zc.zc_cookie = recursive;

	if ((ret = zfs_ioctl(zhp->zfs_hdl, ZFS_IOC_RENAME, &zc)) != 0) {
		/*
		 * if it was recursive, the one that actually failed will
		 * be in zc.zc_name
		 */
		(void) snprintf(errbuf, sizeof (errbuf), dgettext(TEXT_DOMAIN,
		    "cannot rename '%s'"), zc.zc_name);

		if (recursive && errno == EEXIST) {
			zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
			    "a child dataset already has a snapshot "
			    "with the new name"));
			(void) zfs_error(hdl, EZFS_EXISTS, errbuf);
		} else {
			(void) zfs_standard_error(zhp->zfs_hdl, errno, errbuf);
		}

		/*
		 * On failure, we still want to remount any filesystems that
		 * were previously mounted, so we don't alter the system state.
		 */
		if (!recursive)
			(void) changelist_postfix(cl);
	} else {
		if (!recursive) {
			changelist_rename(cl, zfs_get_name(zhp), target);
			ret = changelist_postfix(cl);
		}
	}

error:
	if (parentname) {
		free(parentname);
	}
	if (zhrp) {
		zfs_close(zhrp);
	}
	if (cl) {
		changelist_free(cl);
	}
	return (ret);
}

nvlist_t *
zfs_get_user_props(zfs_handle_t *zhp)
{
	return (zhp->zfs_user_props);
}

nvlist_t *
zfs_get_recvd_props(zfs_handle_t *zhp)
{
	if (zhp->zfs_recvd_props == NULL)
		if (get_recvd_props_ioctl(zhp) != 0)
			return (NULL);
	return (zhp->zfs_recvd_props);
}

/*
 * This function is used by 'zfs list' to determine the exact set of columns to
 * display, and their maximum widths.  This does two main things:
 *
 *      - If this is a list of all properties, then expand the list to include
 *        all native properties, and set a flag so that for each dataset we look
 *        for new unique user properties and add them to the list.
 *
 *      - For non fixed-width properties, keep track of the maximum width seen
 *        so that we can size the column appropriately. If the user has
 *        requested received property values, we also need to compute the width
 *        of the RECEIVED column.
 */
int
zfs_expand_proplist(zfs_handle_t *zhp, zprop_list_t **plp, boolean_t received)
{
	libzfs_handle_t *hdl = zhp->zfs_hdl;
	zprop_list_t *entry;
	zprop_list_t **last, **start;
	nvlist_t *userprops, *propval;
	nvpair_t *elem;
	char *strval;
	char buf[ZFS_MAXPROPLEN];

	if (zprop_expand_list(hdl, plp, ZFS_TYPE_DATASET) != 0)
		return (-1);

	userprops = zfs_get_user_props(zhp);

	entry = *plp;
	if (entry->pl_all && nvlist_next_nvpair(userprops, NULL) != NULL) {
		/*
		 * Go through and add any user properties as necessary.  We
		 * start by incrementing our list pointer to the first
		 * non-native property.
		 */
		start = plp;
		while (*start != NULL) {
			if ((*start)->pl_prop == ZPROP_INVAL)
				break;
			start = &(*start)->pl_next;
		}

		elem = NULL;
		while ((elem = nvlist_next_nvpair(userprops, elem)) != NULL) {
			/*
			 * See if we've already found this property in our list.
			 */
			for (last = start; *last != NULL;
			    last = &(*last)->pl_next) {
				if (strcmp((*last)->pl_user_prop,
				    nvpair_name(elem)) == 0)
					break;
			}

			if (*last == NULL) {
				if ((entry = zfs_alloc(hdl,
				    sizeof (zprop_list_t))) == NULL ||
				    ((entry->pl_user_prop = zfs_strdup(hdl,
				    nvpair_name(elem)))) == NULL) {
					free(entry);
					return (-1);
				}

				entry->pl_prop = ZPROP_INVAL;
				entry->pl_width = strlen(nvpair_name(elem));
				entry->pl_all = B_TRUE;
				*last = entry;
			}
		}
	}

	/*
	 * Now go through and check the width of any non-fixed columns
	 */
	for (entry = *plp; entry != NULL; entry = entry->pl_next) {
		if (entry->pl_fixed)
			continue;

		if (entry->pl_prop != ZPROP_INVAL) {
			if (zfs_prop_get(zhp, entry->pl_prop,
			    buf, sizeof (buf), NULL, NULL, 0, B_FALSE) == 0) {
				if (strlen(buf) > entry->pl_width)
					entry->pl_width = strlen(buf);
			}
			if (received && zfs_prop_get_recvd(zhp,
			    zfs_prop_to_name(entry->pl_prop),
			    buf, sizeof (buf), B_FALSE) == 0)
				if (strlen(buf) > entry->pl_recvd_width)
					entry->pl_recvd_width = strlen(buf);
		} else {
			if (nvlist_lookup_nvlist(userprops, entry->pl_user_prop,
			    &propval) == 0) {
				verify(nvlist_lookup_string(propval,
				    ZPROP_VALUE, &strval) == 0);
				if (strlen(strval) > entry->pl_width)
					entry->pl_width = strlen(strval);
			}
			if (received && zfs_prop_get_recvd(zhp,
			    entry->pl_user_prop,
			    buf, sizeof (buf), B_FALSE) == 0)
				if (strlen(buf) > entry->pl_recvd_width)
					entry->pl_recvd_width = strlen(buf);
		}
	}

	return (0);
}

int
zfs_deleg_share_nfs(libzfs_handle_t *hdl, char *dataset, char *path,
    char *resource, void *export, void *sharetab,
    int sharemax, zfs_share_op_t operation)
{
	zfs_cmd_t zc = { 0 };
	int error;

	(void) strlcpy(zc.zc_name, dataset, sizeof (zc.zc_name));
	(void) strlcpy(zc.zc_value, path, sizeof (zc.zc_value));
	if (resource)
		(void) strlcpy(zc.zc_string, resource, sizeof (zc.zc_string));
	zc.zc_share.z_sharedata = (uint64_t)(uintptr_t)sharetab;
	zc.zc_share.z_exportdata = (uint64_t)(uintptr_t)export;
	zc.zc_share.z_sharetype = operation;
	zc.zc_share.z_sharemax = sharemax;
	error = ioctl(hdl->libzfs_fd, ZFS_IOC_SHARE, &zc);
	return (error);
}

void
zfs_prune_proplist(zfs_handle_t *zhp, uint8_t *props)
{
	nvpair_t *curr;

	/*
	 * Keep a reference to the props-table against which we prune the
	 * properties.
	 */
	zhp->zfs_props_table = props;

	curr = nvlist_next_nvpair(zhp->zfs_props, NULL);

	while (curr) {
		zfs_prop_t zfs_prop = zfs_name_to_prop(nvpair_name(curr));
		nvpair_t *next = nvlist_next_nvpair(zhp->zfs_props, curr);

		/*
		 * User properties will result in ZPROP_INVAL, and since we
		 * only know how to prune standard ZFS properties, we always
		 * leave these in the list.  This can also happen if we
		 * encounter an unknown DSL property (when running older
		 * software, for example).
		 */
		if (zfs_prop != ZPROP_INVAL && props[zfs_prop] == B_FALSE)
			(void) nvlist_remove(zhp->zfs_props,
			    nvpair_name(curr), nvpair_type(curr));
		curr = next;
	}
}

static int
zfs_smb_acl_mgmt(libzfs_handle_t *hdl, char *dataset, char *path,
    zfs_smb_acl_op_t cmd, char *resource1, char *resource2)
{
	zfs_cmd_t zc = { 0 };
	nvlist_t *nvlist = NULL;
	int error;

	(void) strlcpy(zc.zc_name, dataset, sizeof (zc.zc_name));
	(void) strlcpy(zc.zc_value, path, sizeof (zc.zc_value));
	zc.zc_cookie = (uint64_t)cmd;

	if (cmd == ZFS_SMB_ACL_RENAME) {
		if (nvlist_alloc(&nvlist, NV_UNIQUE_NAME, 0) != 0) {
			(void) no_memory(hdl);
			return (NULL);
		}
	}

	switch (cmd) {
	case ZFS_SMB_ACL_ADD:
	case ZFS_SMB_ACL_REMOVE:
		(void) strlcpy(zc.zc_string, resource1, sizeof (zc.zc_string));
		break;
	case ZFS_SMB_ACL_RENAME:
		if (nvlist_add_string(nvlist, ZFS_SMB_ACL_SRC,
		    resource1) != 0) {
				(void) no_memory(hdl);
				return (-1);
		}
		if (nvlist_add_string(nvlist, ZFS_SMB_ACL_TARGET,
		    resource2) != 0) {
				(void) no_memory(hdl);
				return (-1);
		}
		if (zcmd_write_src_nvlist(hdl, &zc, nvlist) != 0) {
			nvlist_free(nvlist);
			return (-1);
		}
		break;
	case ZFS_SMB_ACL_PURGE:
		break;
	default:
		return (-1);
	}
	error = ioctl(hdl->libzfs_fd, ZFS_IOC_SMB_ACL, &zc);
	if (nvlist)
		nvlist_free(nvlist);
	return (error);
}

int
zfs_smb_acl_add(libzfs_handle_t *hdl, char *dataset,
    char *path, char *resource)
{
	return (zfs_smb_acl_mgmt(hdl, dataset, path, ZFS_SMB_ACL_ADD,
	    resource, NULL));
}

int
zfs_smb_acl_remove(libzfs_handle_t *hdl, char *dataset,
    char *path, char *resource)
{
	return (zfs_smb_acl_mgmt(hdl, dataset, path, ZFS_SMB_ACL_REMOVE,
	    resource, NULL));
}

int
zfs_smb_acl_purge(libzfs_handle_t *hdl, char *dataset, char *path)
{
	return (zfs_smb_acl_mgmt(hdl, dataset, path, ZFS_SMB_ACL_PURGE,
	    NULL, NULL));
}

int
zfs_smb_acl_rename(libzfs_handle_t *hdl, char *dataset, char *path,
    char *oldname, char *newname)
{
	return (zfs_smb_acl_mgmt(hdl, dataset, path, ZFS_SMB_ACL_RENAME,
	    oldname, newname));
}

int
zfs_userspace(zfs_handle_t *zhp, zfs_userquota_prop_t type,
    zfs_userspace_cb_t func, void *arg)
{
	zfs_cmd_t zc = { 0 };
	int error;
	zfs_useracct_t buf[100];

	(void) strncpy(zc.zc_name, zhp->zfs_name, sizeof (zc.zc_name));

	zc.zc_objset_type = type;
	zc.zc_nvlist_dst = (uintptr_t)buf;

	/* CONSTCOND */
	while (1) {
		zfs_useracct_t *zua = buf;

		zc.zc_nvlist_dst_size = sizeof (buf);
		error = ioctl(zhp->zfs_hdl->libzfs_fd,
		    ZFS_IOC_USERSPACE_MANY, &zc);
		if (error || zc.zc_nvlist_dst_size == 0)
			break;

		while (zc.zc_nvlist_dst_size > 0) {
			error = func(arg, zua->zu_domain, zua->zu_rid,
			    zua->zu_space);
			if (error != 0)
				return (error);
			zua++;
			zc.zc_nvlist_dst_size -= sizeof (zfs_useracct_t);
		}
	}

	return (error);
}

int
zfs_hold(zfs_handle_t *zhp, const char *snapname, const char *tag,
    boolean_t recursive, boolean_t temphold, boolean_t enoent_ok,
    int cleanup_fd, uint64_t dsobj, uint64_t createtxg)
{
	zfs_cmd_t zc = { 0 };
	libzfs_handle_t *hdl = zhp->zfs_hdl;

	ASSERT(!recursive || dsobj == 0);

	(void) strlcpy(zc.zc_name, zhp->zfs_name, sizeof (zc.zc_name));
	(void) strlcpy(zc.zc_value, snapname, sizeof (zc.zc_value));
	if (strlcpy(zc.zc_string, tag, sizeof (zc.zc_string))
	    >= sizeof (zc.zc_string))
		return (zfs_error(hdl, EZFS_TAGTOOLONG, tag));
	zc.zc_cookie = recursive;
	zc.zc_temphold = temphold;
	zc.zc_cleanup_fd = cleanup_fd;
	zc.zc_sendobj = dsobj;
	zc.zc_createtxg = createtxg;

	if (zfs_ioctl(hdl, ZFS_IOC_HOLD, &zc) != 0) {
		char errbuf[ZFS_MAXNAMELEN+32];

		/*
		 * if it was recursive, the one that actually failed will be in
		 * zc.zc_name.
		 */
		(void) snprintf(errbuf, sizeof (errbuf), dgettext(TEXT_DOMAIN,
		    "cannot hold '%s@%s'"), zc.zc_name, snapname);
		switch (errno) {
		case E2BIG:
			/*
			 * Temporary tags wind up having the ds object id
			 * prepended. So even if we passed the length check
			 * above, it's still possible for the tag to wind
			 * up being slightly too long.
			 */
			return (zfs_error(hdl, EZFS_TAGTOOLONG, errbuf));
		case ENOTSUP:
			zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
			    "pool must be upgraded"));
			return (zfs_error(hdl, EZFS_BADVERSION, errbuf));
		case EINVAL:
			return (zfs_error(hdl, EZFS_BADTYPE, errbuf));
		case EEXIST:
			return (zfs_error(hdl, EZFS_REFTAG_HOLD, errbuf));
		case ENOENT:
			if (enoent_ok)
				return (ENOENT);
			/* FALLTHROUGH */
		default:
			return (zfs_standard_error_fmt(hdl, errno, errbuf));
		}
	}

	return (0);
}

int
zfs_release(zfs_handle_t *zhp, const char *snapname, const char *tag,
    boolean_t recursive)
{
	zfs_cmd_t zc = { 0 };
	libzfs_handle_t *hdl = zhp->zfs_hdl;

	(void) strlcpy(zc.zc_name, zhp->zfs_name, sizeof (zc.zc_name));
	(void) strlcpy(zc.zc_value, snapname, sizeof (zc.zc_value));
	if (strlcpy(zc.zc_string, tag, sizeof (zc.zc_string))
	    >= sizeof (zc.zc_string))
		return (zfs_error(hdl, EZFS_TAGTOOLONG, tag));
	zc.zc_cookie = recursive;

	if (zfs_ioctl(hdl, ZFS_IOC_RELEASE, &zc) != 0) {
		char errbuf[ZFS_MAXNAMELEN+32];

		/*
		 * if it was recursive, the one that actually failed will be in
		 * zc.zc_name.
		 */
		(void) snprintf(errbuf, sizeof (errbuf), dgettext(TEXT_DOMAIN,
		    "cannot release '%s' from '%s@%s'"), tag, zc.zc_name,
		    snapname);
		switch (errno) {
		case ESRCH:
			return (zfs_error(hdl, EZFS_REFTAG_RELE, errbuf));
		case ENOTSUP:
			zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
			    "pool must be upgraded"));
			return (zfs_error(hdl, EZFS_BADVERSION, errbuf));
		case EINVAL:
			return (zfs_error(hdl, EZFS_BADTYPE, errbuf));
		default:
			return (zfs_standard_error_fmt(hdl, errno, errbuf));
		}
	}

	return (0);
}

uint64_t
zvol_volsize_to_reservation(uint64_t volsize, nvlist_t *props)
{
	uint64_t numdb;
	uint64_t nblocks, volblocksize;
	int ncopies;
	char *strval;

	if (nvlist_lookup_string(props,
	    zfs_prop_to_name(ZFS_PROP_COPIES), &strval) == 0)
		ncopies = atoi(strval);
	else
		ncopies = 1;
	if (nvlist_lookup_uint64(props,
	    zfs_prop_to_name(ZFS_PROP_VOLBLOCKSIZE),
	    &volblocksize) != 0)
		volblocksize = ZVOL_DEFAULT_BLOCKSIZE;
	nblocks = volsize/volblocksize;
	/* start with metadnode L0-L6 */
	numdb = 7;
	/* calculate number of indirects */
	while (nblocks > 1) {
		nblocks += DNODES_PER_LEVEL - 1;
		nblocks /= DNODES_PER_LEVEL;
		numdb += nblocks;
	}
	numdb *= MIN(SPA_DVAS_PER_BP, ncopies + 1);
	volsize *= ncopies;
	/*
	 * this is exactly DN_MAX_INDBLKSHIFT when metadata isn't
	 * compressed, but in practice they compress down to about
	 * 1100 bytes
	 */
	numdb *= 1ULL << DN_MAX_INDBLKSHIFT;
	volsize += numdb;
	return (volsize);
}
