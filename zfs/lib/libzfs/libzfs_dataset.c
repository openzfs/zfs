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
 * Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <libdevinfo.h>
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
#include <sys/mnttab.h>
#include <sys/mount.h>
#include <sys/avl.h>
#include <priv.h>
#include <pwd.h>
#include <grp.h>
#include <stddef.h>
#include <ucred.h>

#include <sys/spa.h>
#include <sys/zap.h>
#include <libzfs.h>

#include "zfs_namecheck.h"
#include "zfs_prop.h"
#include "libzfs_impl.h"
#include "zfs_deleg.h"

static int zvol_create_link_common(libzfs_handle_t *, const char *, int);

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
 * provide a more meaningful error message.  We place a more useful message in
 * 'buf' detailing exactly why the name was not valid.
 */
static int
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
get_stats(zfs_handle_t *zhp)
{
	zfs_cmd_t zc = { 0 };
	libzfs_handle_t *hdl = zhp->zfs_hdl;
	nvlist_t *allprops, *userprops;

	(void) strlcpy(zc.zc_name, zhp->zfs_name, sizeof (zc.zc_name));

	if (zcmd_alloc_dst_nvlist(hdl, &zc, 0) != 0)
		return (-1);

	while (ioctl(zhp->zfs_hdl->libzfs_fd, ZFS_IOC_OBJSET_STATS, &zc) != 0) {
		if (errno == ENOMEM) {
			if (zcmd_expand_dst_nvlist(hdl, &zc) != 0) {
				zcmd_free_nvlists(&zc);
				return (-1);
			}
		} else {
			zcmd_free_nvlists(&zc);
			return (-1);
		}
	}

	zhp->zfs_dmustats = zc.zc_objset_stats; /* structure assignment */

	if (zcmd_read_dst_nvlist(hdl, &zc, &allprops) != 0) {
		zcmd_free_nvlists(&zc);
		return (-1);
	}

	zcmd_free_nvlists(&zc);

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
zfs_handle_t *
make_dataset_handle(libzfs_handle_t *hdl, const char *path)
{
	zfs_handle_t *zhp = calloc(sizeof (zfs_handle_t), 1);
	char *logstr;

	if (zhp == NULL)
		return (NULL);

	zhp->zfs_hdl = hdl;

	/*
	 * Preserve history log string.
	 * any changes performed here will be
	 * logged as an internal event.
	 */
	logstr = zhp->zfs_hdl->libzfs_log_str;
	zhp->zfs_hdl->libzfs_log_str = NULL;
top:
	(void) strlcpy(zhp->zfs_name, path, sizeof (zhp->zfs_name));

	if (get_stats(zhp) != 0) {
		zhp->zfs_hdl->libzfs_log_str = logstr;
		free(zhp);
		return (NULL);
	}

	if (zhp->zfs_dmustats.dds_inconsistent) {
		zfs_cmd_t zc = { 0 };

		/*
		 * If it is dds_inconsistent, then we've caught it in
		 * the middle of a 'zfs receive' or 'zfs destroy', and
		 * it is inconsistent from the ZPL's point of view, so
		 * can't be mounted.  However, it could also be that we
		 * have crashed in the middle of one of those
		 * operations, in which case we need to get rid of the
		 * inconsistent state.  We do that by either rolling
		 * back to the previous snapshot (which will fail if
		 * there is none), or destroying the filesystem.  Note
		 * that if we are still in the middle of an active
		 * 'receive' or 'destroy', then the rollback and destroy
		 * will fail with EBUSY and we will drive on as usual.
		 */

		(void) strlcpy(zc.zc_name, zhp->zfs_name, sizeof (zc.zc_name));

		if (zhp->zfs_dmustats.dds_type == DMU_OST_ZVOL) {
			(void) zvol_remove_link(hdl, zhp->zfs_name);
			zc.zc_objset_type = DMU_OST_ZVOL;
		} else {
			zc.zc_objset_type = DMU_OST_ZFS;
		}

		/*
		 * If we can successfully destroy it, pretend that it
		 * never existed.
		 */
		if (ioctl(hdl->libzfs_fd, ZFS_IOC_DESTROY, &zc) == 0) {
			zhp->zfs_hdl->libzfs_log_str = logstr;
			free(zhp);
			errno = ENOENT;
			return (NULL);
		}
		/* If we can successfully roll it back, reget the stats */
		if (ioctl(hdl->libzfs_fd, ZFS_IOC_ROLLBACK, &zc) == 0)
			goto top;
	}

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

	zhp->zfs_hdl->libzfs_log_str = logstr;
	zhp->zpool_hdl = zpool_handle(zhp);
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
	free(zhp);
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

	elem = NULL;
	while ((elem = nvlist_next_nvpair(nvl, elem)) != NULL) {
		const char *propname = nvpair_name(elem);

		/*
		 * Make sure this property is valid and applies to this type.
		 */
		if ((prop = zfs_name_to_prop(propname)) == ZPROP_INVAL) {
			if (!zfs_prop_user(propname)) {
				zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
				    "invalid property '%s'"), propname);
				(void) zfs_error(hdl, EZFS_BADPROP, errbuf);
				goto error;
			}

			/*
			 * If this is a user property, make sure it's a
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

		if (type == ZFS_TYPE_SNAPSHOT) {
			zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
			    "this property can not be modified for snapshots"));
			(void) zfs_error(hdl, EZFS_PROPTYPE, errbuf);
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

		case ZFS_PROP_SHAREISCSI:
			if (strcmp(strval, "off") != 0 &&
			    strcmp(strval, "on") != 0 &&
			    strcmp(strval, "type=disk") != 0) {
				zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
				    "'%s' must be 'on', 'off', or 'type=disk'"),
				    propname);
				(void) zfs_error(hdl, EZFS_BADPROP, errbuf);
				goto error;
			}

			break;

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
				 * a globle zone. If not, something is wrong.
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

	/*
	 * If this is an existing volume, and someone is setting the volsize,
	 * make sure that it matches the reservation, or add it if necessary.
	 */
	if (zhp != NULL && type == ZFS_TYPE_VOLUME &&
	    nvlist_lookup_uint64(ret, zfs_prop_to_name(ZFS_PROP_VOLSIZE),
	    &intval) == 0) {
		uint64_t old_volsize = zfs_prop_get_int(zhp,
		    ZFS_PROP_VOLSIZE);
		uint64_t old_reservation;
		uint64_t new_reservation;
		zfs_prop_t resv_prop;

		if (zfs_which_resv_prop(zhp, &resv_prop) < 0)
			goto error;
		old_reservation = zfs_prop_get_int(zhp, resv_prop);

		if (old_volsize == old_reservation &&
		    nvlist_lookup_uint64(ret, zfs_prop_to_name(resv_prop),
		    &new_reservation) != 0) {
			if (nvlist_add_uint64(ret,
			    zfs_prop_to_name(resv_prop), intval) != 0) {
				(void) no_memory(hdl);
				goto error;
			}
		}
	}
	return (ret);

error:
	nvlist_free(ret);
	return (NULL);
}

static int
zfs_get_perm_who(const char *who, zfs_deleg_who_type_t *who_type,
    uint64_t *ret_who)
{
	struct passwd *pwd;
	struct group *grp;
	uid_t id;

	if (*who_type == ZFS_DELEG_EVERYONE || *who_type == ZFS_DELEG_CREATE ||
	    *who_type == ZFS_DELEG_NAMED_SET) {
		*ret_who = -1;
		return (0);
	}
	if (who == NULL && !(*who_type == ZFS_DELEG_EVERYONE))
		return (EZFS_BADWHO);

	if (*who_type == ZFS_DELEG_WHO_UNKNOWN &&
	    strcmp(who, "everyone") == 0) {
		*ret_who = -1;
		*who_type = ZFS_DELEG_EVERYONE;
		return (0);
	}

	pwd = getpwnam(who);
	grp = getgrnam(who);

	if ((*who_type == ZFS_DELEG_USER) && pwd) {
		*ret_who = pwd->pw_uid;
	} else if ((*who_type == ZFS_DELEG_GROUP) && grp) {
		*ret_who = grp->gr_gid;
	} else if (pwd) {
		*ret_who = pwd->pw_uid;
		*who_type = ZFS_DELEG_USER;
	} else if (grp) {
		*ret_who = grp->gr_gid;
		*who_type = ZFS_DELEG_GROUP;
	} else {
		char *end;

		id = strtol(who, &end, 10);
		if (errno != 0 || *end != '\0') {
			return (EZFS_BADWHO);
		} else {
			*ret_who = id;
			if (*who_type == ZFS_DELEG_WHO_UNKNOWN)
				*who_type = ZFS_DELEG_USER;
		}
	}

	return (0);
}

static void
zfs_perms_add_to_nvlist(nvlist_t *who_nvp, char *name, nvlist_t *perms_nvp)
{
	if (perms_nvp != NULL) {
		verify(nvlist_add_nvlist(who_nvp,
		    name, perms_nvp) == 0);
	} else {
		verify(nvlist_add_boolean(who_nvp, name) == 0);
	}
}

static void
helper(zfs_deleg_who_type_t who_type, uint64_t whoid, char *whostr,
    zfs_deleg_inherit_t inherit, nvlist_t *who_nvp, nvlist_t *perms_nvp,
    nvlist_t *sets_nvp)
{
	boolean_t do_perms, do_sets;
	char name[ZFS_MAX_DELEG_NAME];

	do_perms = (nvlist_next_nvpair(perms_nvp, NULL) != NULL);
	do_sets = (nvlist_next_nvpair(sets_nvp, NULL) != NULL);

	if (!do_perms && !do_sets)
		do_perms = do_sets = B_TRUE;

	if (do_perms) {
		zfs_deleg_whokey(name, who_type, inherit,
		    (who_type == ZFS_DELEG_NAMED_SET) ?
		    whostr : (void *)&whoid);
		zfs_perms_add_to_nvlist(who_nvp, name, perms_nvp);
	}
	if (do_sets) {
		zfs_deleg_whokey(name, toupper(who_type), inherit,
		    (who_type == ZFS_DELEG_NAMED_SET) ?
		    whostr : (void *)&whoid);
		zfs_perms_add_to_nvlist(who_nvp, name, sets_nvp);
	}
}

static void
zfs_perms_add_who_nvlist(nvlist_t *who_nvp, uint64_t whoid, void *whostr,
    nvlist_t *perms_nvp, nvlist_t *sets_nvp,
    zfs_deleg_who_type_t who_type, zfs_deleg_inherit_t inherit)
{
	if (who_type == ZFS_DELEG_NAMED_SET || who_type == ZFS_DELEG_CREATE) {
		helper(who_type, whoid, whostr, 0,
		    who_nvp, perms_nvp, sets_nvp);
	} else {
		if (inherit & ZFS_DELEG_PERM_LOCAL) {
			helper(who_type, whoid, whostr, ZFS_DELEG_LOCAL,
			    who_nvp, perms_nvp, sets_nvp);
		}
		if (inherit & ZFS_DELEG_PERM_DESCENDENT) {
			helper(who_type, whoid, whostr, ZFS_DELEG_DESCENDENT,
			    who_nvp, perms_nvp, sets_nvp);
		}
	}
}

/*
 * Construct nvlist to pass down to kernel for setting/removing permissions.
 *
 * The nvlist is constructed as a series of nvpairs with an optional embedded
 * nvlist of permissions to remove or set.  The topmost nvpairs are the actual
 * base attribute named stored in the dsl.
 * Arguments:
 *
 * whostr:   is a comma separated list of users, groups, or a single set name.
 *           whostr may be null for everyone or create perms.
 * who_type: is the type of entry in whostr.  Typically this will be
 *           ZFS_DELEG_WHO_UNKNOWN.
 * perms:    common separated list of permissions.  May be null if user
 *           is requested to remove permissions by who.
 * inherit:  Specifies the inheritance of the permissions.  Will be either
 *           ZFS_DELEG_PERM_LOCAL and/or  ZFS_DELEG_PERM_DESCENDENT.
 * nvp       The constructed nvlist to pass to zfs_perm_set().
 *           The output nvp will look something like this.
 *              ul$1234 -> {create ; destroy }
 *              Ul$1234 -> { @myset }
 *              s-$@myset - { snapshot; checksum; compression }
 */
int
zfs_build_perms(zfs_handle_t *zhp, char *whostr, char *perms,
    zfs_deleg_who_type_t who_type, zfs_deleg_inherit_t inherit, nvlist_t **nvp)
{
	nvlist_t *who_nvp;
	nvlist_t *perms_nvp = NULL;
	nvlist_t *sets_nvp = NULL;
	char errbuf[1024];
	char *who_tok, *perm;
	int error;

	*nvp = NULL;

	if (perms) {
		if ((error = nvlist_alloc(&perms_nvp,
		    NV_UNIQUE_NAME, 0)) != 0) {
			return (1);
		}
		if ((error = nvlist_alloc(&sets_nvp,
		    NV_UNIQUE_NAME, 0)) != 0) {
			nvlist_free(perms_nvp);
			return (1);
		}
	}

	if ((error = nvlist_alloc(&who_nvp, NV_UNIQUE_NAME, 0)) != 0) {
		if (perms_nvp)
			nvlist_free(perms_nvp);
		if (sets_nvp)
			nvlist_free(sets_nvp);
		return (1);
	}

	if (who_type == ZFS_DELEG_NAMED_SET) {
		namecheck_err_t why;
		char what;

		if ((error = permset_namecheck(whostr, &why, &what)) != 0) {
			nvlist_free(who_nvp);
			if (perms_nvp)
				nvlist_free(perms_nvp);
			if (sets_nvp)
				nvlist_free(sets_nvp);

			switch (why) {
			case NAME_ERR_NO_AT:
				zfs_error_aux(zhp->zfs_hdl,
				    dgettext(TEXT_DOMAIN,
				    "set definition must begin with an '@' "
				    "character"));
			}
			return (zfs_error(zhp->zfs_hdl,
			    EZFS_BADPERMSET, whostr));
		}
	}

	/*
	 * Build up nvlist(s) of permissions.  Two nvlists are maintained.
	 * The first nvlist perms_nvp will have normal permissions and the
	 * other sets_nvp will have only permssion set names in it.
	 */
	for (perm = strtok(perms, ","); perm; perm = strtok(NULL, ",")) {
		const char *perm_canonical = zfs_deleg_canonicalize_perm(perm);

		if (perm_canonical) {
			verify(nvlist_add_boolean(perms_nvp,
			    perm_canonical) == 0);
		} else if (perm[0] == '@') {
			verify(nvlist_add_boolean(sets_nvp, perm) == 0);
		} else {
			nvlist_free(who_nvp);
			nvlist_free(perms_nvp);
			nvlist_free(sets_nvp);
			return (zfs_error(zhp->zfs_hdl, EZFS_BADPERM, perm));
		}
	}

	if (whostr && who_type != ZFS_DELEG_CREATE) {
		who_tok = strtok(whostr, ",");
		if (who_tok == NULL) {
			nvlist_free(who_nvp);
			if (perms_nvp)
				nvlist_free(perms_nvp);
			if (sets_nvp)
				nvlist_free(sets_nvp);
			(void) snprintf(errbuf, sizeof (errbuf),
			    dgettext(TEXT_DOMAIN, "Who string is NULL"),
			    whostr);
			return (zfs_error(zhp->zfs_hdl, EZFS_BADWHO, errbuf));
		}
	}

	/*
	 * Now create the nvlist(s)
	 */
	do {
		uint64_t who_id;

		error = zfs_get_perm_who(who_tok, &who_type,
		    &who_id);
		if (error) {
			nvlist_free(who_nvp);
			if (perms_nvp)
				nvlist_free(perms_nvp);
			if (sets_nvp)
				nvlist_free(sets_nvp);
			(void) snprintf(errbuf, sizeof (errbuf),
			    dgettext(TEXT_DOMAIN,
			    "Unable to determine uid/gid for "
			    "%s "), who_tok);
			return (zfs_error(zhp->zfs_hdl, EZFS_BADWHO, errbuf));
		}

		/*
		 * add entries for both local and descendent when required
		 */
		zfs_perms_add_who_nvlist(who_nvp, who_id, who_tok,
		    perms_nvp, sets_nvp, who_type, inherit);

	} while (who_tok = strtok(NULL, ","));
	*nvp = who_nvp;
	return (0);
}

static int
zfs_perm_set_common(zfs_handle_t *zhp, nvlist_t *nvp, boolean_t unset)
{
	zfs_cmd_t zc = { 0 };
	int error;
	char errbuf[1024];

	(void) snprintf(errbuf, sizeof (errbuf),
	    dgettext(TEXT_DOMAIN, "Cannot update 'allows' for '%s'"),
	    zhp->zfs_name);

	if (zcmd_write_src_nvlist(zhp->zfs_hdl, &zc, nvp))
		return (-1);

	(void) strlcpy(zc.zc_name, zhp->zfs_name, sizeof (zc.zc_name));
	zc.zc_perm_action = unset;

	error = zfs_ioctl(zhp->zfs_hdl, ZFS_IOC_SET_FSACL, &zc);
	if (error && errno == ENOTSUP) {
		(void) snprintf(errbuf, sizeof (errbuf),
		    gettext("Pool must be upgraded to use 'allow/unallow'"));
		zcmd_free_nvlists(&zc);
		return (zfs_error(zhp->zfs_hdl, EZFS_BADVERSION, errbuf));
	} else if (error) {
		return (zfs_standard_error(zhp->zfs_hdl, errno, errbuf));
	}
	zcmd_free_nvlists(&zc);

	return (error);
}

int
zfs_perm_set(zfs_handle_t *zhp, nvlist_t *nvp)
{
	return (zfs_perm_set_common(zhp, nvp, B_FALSE));
}

int
zfs_perm_remove(zfs_handle_t *zhp, nvlist_t *perms)
{
	return (zfs_perm_set_common(zhp, perms, B_TRUE));
}

static int
perm_compare(const void *arg1, const void *arg2)
{
	const zfs_perm_node_t *node1 = arg1;
	const zfs_perm_node_t *node2 = arg2;
	int ret;

	ret = strcmp(node1->z_pname, node2->z_pname);

	if (ret > 0)
		return (1);
	if (ret < 0)
		return (-1);
	else
		return (0);
}

static void
zfs_destroy_perm_tree(avl_tree_t *tree)
{
	zfs_perm_node_t *permnode;
	void *cookie = NULL;

	while ((permnode = avl_destroy_nodes(tree,  &cookie)) != NULL)
		free(permnode);
	avl_destroy(tree);
}

static void
zfs_destroy_tree(avl_tree_t *tree)
{
	zfs_allow_node_t *allownode;
	void *cookie = NULL;

	while ((allownode = avl_destroy_nodes(tree, &cookie)) != NULL) {
		zfs_destroy_perm_tree(&allownode->z_localdescend);
		zfs_destroy_perm_tree(&allownode->z_local);
		zfs_destroy_perm_tree(&allownode->z_descend);
		free(allownode);
	}
	avl_destroy(tree);
}

void
zfs_free_allows(zfs_allow_t *allow)
{
	zfs_allow_t *allownext;
	zfs_allow_t *freeallow;

	allownext = allow;
	while (allownext) {
		zfs_destroy_tree(&allownext->z_sets);
		zfs_destroy_tree(&allownext->z_crperms);
		zfs_destroy_tree(&allownext->z_user);
		zfs_destroy_tree(&allownext->z_group);
		zfs_destroy_tree(&allownext->z_everyone);
		freeallow = allownext;
		allownext = allownext->z_next;
		free(freeallow);
	}
}

static zfs_allow_t *
zfs_alloc_perm_tree(zfs_handle_t *zhp, zfs_allow_t *prev, char *setpoint)
{
	zfs_allow_t *ptree;

	if ((ptree = zfs_alloc(zhp->zfs_hdl,
	    sizeof (zfs_allow_t))) == NULL) {
		return (NULL);
	}

	(void) strlcpy(ptree->z_setpoint, setpoint, sizeof (ptree->z_setpoint));
	avl_create(&ptree->z_sets,
	    perm_compare, sizeof (zfs_allow_node_t),
	    offsetof(zfs_allow_node_t, z_node));
	avl_create(&ptree->z_crperms,
	    perm_compare, sizeof (zfs_allow_node_t),
	    offsetof(zfs_allow_node_t, z_node));
	avl_create(&ptree->z_user,
	    perm_compare, sizeof (zfs_allow_node_t),
	    offsetof(zfs_allow_node_t, z_node));
	avl_create(&ptree->z_group,
	    perm_compare, sizeof (zfs_allow_node_t),
	    offsetof(zfs_allow_node_t, z_node));
	avl_create(&ptree->z_everyone,
	    perm_compare, sizeof (zfs_allow_node_t),
	    offsetof(zfs_allow_node_t, z_node));

	if (prev)
		prev->z_next = ptree;
	ptree->z_next = NULL;
	return (ptree);
}

/*
 * Add permissions to the appropriate AVL permission tree.
 * The appropriate tree may not be the requested tree.
 * For example if ld indicates a local permission, but
 * same permission also exists as a descendent permission
 * then the permission will be removed from the descendent
 * tree and add the the local+descendent tree.
 */
static int
zfs_coalesce_perm(zfs_handle_t *zhp, zfs_allow_node_t *allownode,
    char *perm, char ld)
{
	zfs_perm_node_t pnode, *permnode, *permnode2;
	zfs_perm_node_t *newnode;
	avl_index_t where, where2;
	avl_tree_t *tree, *altree;

	(void) strlcpy(pnode.z_pname, perm, sizeof (pnode.z_pname));

	if (ld == ZFS_DELEG_NA) {
		tree =  &allownode->z_localdescend;
		altree = &allownode->z_descend;
	} else if (ld == ZFS_DELEG_LOCAL) {
		tree = &allownode->z_local;
		altree = &allownode->z_descend;
	} else {
		tree = &allownode->z_descend;
		altree = &allownode->z_local;
	}
	permnode = avl_find(tree, &pnode, &where);
	permnode2 = avl_find(altree, &pnode, &where2);

	if (permnode2) {
		avl_remove(altree, permnode2);
		free(permnode2);
		if (permnode == NULL) {
			tree =  &allownode->z_localdescend;
		}
	}

	/*
	 * Now insert new permission in either requested location
	 * local/descendent or into ld when perm will exist in both.
	 */
	if (permnode == NULL) {
		if ((newnode = zfs_alloc(zhp->zfs_hdl,
		    sizeof (zfs_perm_node_t))) == NULL) {
			return (-1);
		}
		*newnode = pnode;
		avl_add(tree, newnode);
	}
	return (0);
}

/*
 * Uggh, this is going to be a bit complicated.
 * we have an nvlist coming out of the kernel that
 * will indicate where the permission is set and then
 * it will contain allow of the various "who's", and what
 * their permissions are.  To further complicate this
 * we will then have to coalesce the local,descendent
 * and local+descendent permissions where appropriate.
 * The kernel only knows about a permission as being local
 * or descendent, but not both.
 *
 * In order to make this easier for zfs_main to deal with
 * a series of AVL trees will be used to maintain
 * all of this, primarily for sorting purposes as well
 * as the ability to quickly locate a specific entry.
 *
 * What we end up with are tree's for sets, create perms,
 * user, groups and everyone.  With each of those trees
 * we have subtrees for local, descendent and local+descendent
 * permissions.
 */
int
zfs_perm_get(zfs_handle_t *zhp, zfs_allow_t **zfs_perms)
{
	zfs_cmd_t zc = { 0 };
	int error;
	nvlist_t *nvlist;
	nvlist_t *permnv, *sourcenv;
	nvpair_t *who_pair, *source_pair;
	nvpair_t *perm_pair;
	char errbuf[1024];
	zfs_allow_t *zallowp, *newallowp;
	char  ld;
	char *nvpname;
	uid_t	uid;
	gid_t	gid;
	avl_tree_t *tree;
	avl_index_t where;

	(void) strlcpy(zc.zc_name, zhp->zfs_name, sizeof (zc.zc_name));

	if (zcmd_alloc_dst_nvlist(zhp->zfs_hdl, &zc, 0) != 0)
		return (-1);

	while (ioctl(zhp->zfs_hdl->libzfs_fd, ZFS_IOC_GET_FSACL, &zc) != 0) {
		if (errno == ENOMEM) {
			if (zcmd_expand_dst_nvlist(zhp->zfs_hdl, &zc) != 0) {
				zcmd_free_nvlists(&zc);
				return (-1);
			}
		} else if (errno == ENOTSUP) {
			zcmd_free_nvlists(&zc);
			(void) snprintf(errbuf, sizeof (errbuf),
			    gettext("Pool must be upgraded to use 'allow'"));
			return (zfs_error(zhp->zfs_hdl,
			    EZFS_BADVERSION, errbuf));
		} else {
			zcmd_free_nvlists(&zc);
			return (-1);
		}
	}

	if (zcmd_read_dst_nvlist(zhp->zfs_hdl, &zc, &nvlist) != 0) {
		zcmd_free_nvlists(&zc);
		return (-1);
	}

	zcmd_free_nvlists(&zc);

	source_pair = nvlist_next_nvpair(nvlist, NULL);

	if (source_pair == NULL) {
		*zfs_perms = NULL;
		return (0);
	}

	*zfs_perms = zfs_alloc_perm_tree(zhp, NULL, nvpair_name(source_pair));
	if (*zfs_perms == NULL) {
		return (0);
	}

	zallowp = *zfs_perms;

	for (;;) {
		struct passwd *pwd;
		struct group *grp;
		zfs_allow_node_t *allownode;
		zfs_allow_node_t  findallownode;
		zfs_allow_node_t *newallownode;

		(void) strlcpy(zallowp->z_setpoint,
		    nvpair_name(source_pair),
		    sizeof (zallowp->z_setpoint));

		if ((error = nvpair_value_nvlist(source_pair, &sourcenv)) != 0)
			goto abort;

		/*
		 * Make sure nvlist is composed correctly
		 */
		if (zfs_deleg_verify_nvlist(sourcenv)) {
			goto abort;
		}

		who_pair = nvlist_next_nvpair(sourcenv, NULL);
		if (who_pair == NULL) {
			goto abort;
		}

		do {
			error = nvpair_value_nvlist(who_pair, &permnv);
			if (error) {
				goto abort;
			}

			/*
			 * First build up the key to use
			 * for looking up in the various
			 * who trees.
			 */
			ld = nvpair_name(who_pair)[1];
			nvpname = nvpair_name(who_pair);
			switch (nvpair_name(who_pair)[0]) {
			case ZFS_DELEG_USER:
			case ZFS_DELEG_USER_SETS:
				tree = &zallowp->z_user;
				uid = atol(&nvpname[3]);
				pwd = getpwuid(uid);
				(void) snprintf(findallownode.z_key,
				    sizeof (findallownode.z_key), "user %s",
				    (pwd) ? pwd->pw_name :
				    &nvpair_name(who_pair)[3]);
				break;
			case ZFS_DELEG_GROUP:
			case ZFS_DELEG_GROUP_SETS:
				tree = &zallowp->z_group;
				gid = atol(&nvpname[3]);
				grp = getgrgid(gid);
				(void) snprintf(findallownode.z_key,
				    sizeof (findallownode.z_key), "group %s",
				    (grp) ? grp->gr_name :
				    &nvpair_name(who_pair)[3]);
				break;
			case ZFS_DELEG_CREATE:
			case ZFS_DELEG_CREATE_SETS:
				tree = &zallowp->z_crperms;
				(void) strlcpy(findallownode.z_key, "",
				    sizeof (findallownode.z_key));
				break;
			case ZFS_DELEG_EVERYONE:
			case ZFS_DELEG_EVERYONE_SETS:
				(void) snprintf(findallownode.z_key,
				    sizeof (findallownode.z_key), "everyone");
				tree = &zallowp->z_everyone;
				break;
			case ZFS_DELEG_NAMED_SET:
			case ZFS_DELEG_NAMED_SET_SETS:
				(void) snprintf(findallownode.z_key,
				    sizeof (findallownode.z_key), "%s",
				    &nvpair_name(who_pair)[3]);
				tree = &zallowp->z_sets;
				break;
			}

			/*
			 * Place who in tree
			 */
			allownode = avl_find(tree, &findallownode, &where);
			if (allownode == NULL) {
				if ((newallownode = zfs_alloc(zhp->zfs_hdl,
				    sizeof (zfs_allow_node_t))) == NULL) {
					goto abort;
				}
				avl_create(&newallownode->z_localdescend,
				    perm_compare,
				    sizeof (zfs_perm_node_t),
				    offsetof(zfs_perm_node_t, z_node));
				avl_create(&newallownode->z_local,
				    perm_compare,
				    sizeof (zfs_perm_node_t),
				    offsetof(zfs_perm_node_t, z_node));
				avl_create(&newallownode->z_descend,
				    perm_compare,
				    sizeof (zfs_perm_node_t),
				    offsetof(zfs_perm_node_t, z_node));
				(void) strlcpy(newallownode->z_key,
				    findallownode.z_key,
				    sizeof (findallownode.z_key));
				avl_insert(tree, newallownode, where);
				allownode = newallownode;
			}

			/*
			 * Now iterate over the permissions and
			 * place them in the appropriate local,
			 * descendent or local+descendent tree.
			 *
			 * The permissions are added to the tree
			 * via zfs_coalesce_perm().
			 */
			perm_pair = nvlist_next_nvpair(permnv, NULL);
			if (perm_pair == NULL)
				goto abort;
			do {
				if (zfs_coalesce_perm(zhp, allownode,
				    nvpair_name(perm_pair), ld) != 0)
					goto abort;
			} while (perm_pair = nvlist_next_nvpair(permnv,
			    perm_pair));
		} while (who_pair = nvlist_next_nvpair(sourcenv, who_pair));

		source_pair = nvlist_next_nvpair(nvlist, source_pair);
		if (source_pair == NULL)
			break;

		/*
		 * allocate another node from the link list of
		 * zfs_allow_t structures
		 */
		newallowp = zfs_alloc_perm_tree(zhp, zallowp,
		    nvpair_name(source_pair));
		if (newallowp == NULL) {
			goto abort;
		}
		zallowp = newallowp;
	}
	nvlist_free(nvlist);
	return (0);
abort:
	zfs_free_allows(*zfs_perms);
	nvlist_free(nvlist);
	return (-1);
}

static char *
zfs_deleg_perm_note(zfs_deleg_note_t note)
{
	/*
	 * Don't put newlines on end of lines
	 */
	switch (note) {
	case ZFS_DELEG_NOTE_CREATE:
		return (dgettext(TEXT_DOMAIN,
		    "Must also have the 'mount' ability"));
	case ZFS_DELEG_NOTE_DESTROY:
		return (dgettext(TEXT_DOMAIN,
		    "Must also have the 'mount' ability"));
	case ZFS_DELEG_NOTE_SNAPSHOT:
		return (dgettext(TEXT_DOMAIN,
		    "Must also have the 'mount' ability"));
	case ZFS_DELEG_NOTE_ROLLBACK:
		return (dgettext(TEXT_DOMAIN,
		    "Must also have the 'mount' ability"));
	case ZFS_DELEG_NOTE_CLONE:
		return (dgettext(TEXT_DOMAIN, "Must also have the 'create' "
		    "ability and 'mount'\n"
		    "\t\t\t\tability in the origin file system"));
	case ZFS_DELEG_NOTE_PROMOTE:
		return (dgettext(TEXT_DOMAIN, "Must also have the 'mount'\n"
		    "\t\t\t\tand 'promote' ability in the origin file system"));
	case ZFS_DELEG_NOTE_RENAME:
		return (dgettext(TEXT_DOMAIN, "Must also have the 'mount' "
		    "and 'create' \n\t\t\t\tability in the new parent"));
	case ZFS_DELEG_NOTE_RECEIVE:
		return (dgettext(TEXT_DOMAIN, "Must also have the 'mount'"
		    " and 'create' ability"));
	case ZFS_DELEG_NOTE_USERPROP:
		return (dgettext(TEXT_DOMAIN,
		    "Allows changing any user property"));
	case ZFS_DELEG_NOTE_ALLOW:
		return (dgettext(TEXT_DOMAIN,
		    "Must also have the permission that is being\n"
		    "\t\t\t\tallowed"));
	case ZFS_DELEG_NOTE_MOUNT:
		return (dgettext(TEXT_DOMAIN,
		    "Allows mount/umount of ZFS datasets"));
	case ZFS_DELEG_NOTE_SHARE:
		return (dgettext(TEXT_DOMAIN,
		    "Allows sharing file systems over NFS or SMB\n"
		    "\t\t\t\tprotocols"));
	case ZFS_DELEG_NOTE_NONE:
	default:
		return (dgettext(TEXT_DOMAIN, ""));
	}
}

typedef enum {
	ZFS_DELEG_SUBCOMMAND,
	ZFS_DELEG_PROP,
	ZFS_DELEG_OTHER
} zfs_deleg_perm_type_t;

/*
 * is the permission a subcommand or other?
 */
zfs_deleg_perm_type_t
zfs_deleg_perm_type(const char *perm)
{
	if (strcmp(perm, "userprop") == 0)
		return (ZFS_DELEG_OTHER);
	else
		return (ZFS_DELEG_SUBCOMMAND);
}

static char *
zfs_deleg_perm_type_str(zfs_deleg_perm_type_t type)
{
	switch (type) {
	case ZFS_DELEG_SUBCOMMAND:
		return (dgettext(TEXT_DOMAIN, "subcommand"));
	case ZFS_DELEG_PROP:
		return (dgettext(TEXT_DOMAIN, "property"));
	case ZFS_DELEG_OTHER:
		return (dgettext(TEXT_DOMAIN, "other"));
	}
	return ("");
}

/*ARGSUSED*/
static int
zfs_deleg_prop_cb(int prop, void *cb)
{
	if (zfs_prop_delegatable(prop))
		(void) fprintf(stderr, "%-15s %-15s\n", zfs_prop_to_name(prop),
		    zfs_deleg_perm_type_str(ZFS_DELEG_PROP));

	return (ZPROP_CONT);
}

void
zfs_deleg_permissions(void)
{
	int i;

	(void) fprintf(stderr, "\n%-15s %-15s\t%s\n\n", "NAME",
	    "TYPE", "NOTES");

	/*
	 * First print out the subcommands
	 */
	for (i = 0; zfs_deleg_perm_tab[i].z_perm != NULL; i++) {
		(void) fprintf(stderr, "%-15s %-15s\t%s\n",
		    zfs_deleg_perm_tab[i].z_perm,
		    zfs_deleg_perm_type_str(
		    zfs_deleg_perm_type(zfs_deleg_perm_tab[i].z_perm)),
		    zfs_deleg_perm_note(zfs_deleg_perm_tab[i].z_note));
	}

	(void) zprop_iter(zfs_deleg_prop_cb, NULL, B_FALSE, B_TRUE,
	    ZFS_TYPE_DATASET|ZFS_TYPE_VOLUME);
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
		switch (errno) {

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
				(void) zfs_standard_error(hdl, errno, errbuf);
				break;
			}
			break;

		case EBUSY:
			if (prop == ZFS_PROP_VOLBLOCKSIZE)
				(void) zfs_error(hdl, EZFS_VOLHASDATA, errbuf);
			else
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
				(void) zfs_standard_error(hdl, errno, errbuf);
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
			(void) zfs_standard_error(hdl, errno, errbuf);
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
 * Given a property, inherit the value from the parent dataset.
 */
int
zfs_prop_inherit(zfs_handle_t *zhp, const char *propname)
{
	zfs_cmd_t zc = { 0 };
	int ret;
	prop_changelist_t *cl;
	libzfs_handle_t *hdl = zhp->zfs_hdl;
	char errbuf[1024];
	zfs_prop_t prop;

	(void) snprintf(errbuf, sizeof (errbuf), dgettext(TEXT_DOMAIN,
	    "cannot inherit %s for '%s'"), propname, zhp->zfs_name);

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

	if (!zfs_prop_inheritable(prop))
		return (zfs_error(hdl, EZFS_PROPNONINHERIT, errbuf));

	/*
	 * Check to see if the value applies to this type
	 */
	if (!zfs_prop_valid_for_type(prop, zhp->zfs_type))
		return (zfs_error(hdl, EZFS_PROPTYPE, errbuf));

	/*
	 * Normalize the name, to get rid of shorthand abbrevations.
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
		if ((value = (char *)zfs_prop_default_string(prop)) == NULL)
			value = "";
		*source = "";
	}

	return (value);
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
		struct mnttab entry, search = { 0 };
		FILE *mnttab = zhp->zfs_hdl->libzfs_mnttab;

		search.mnt_special = (char *)zhp->zfs_name;
		search.mnt_fstype = MNTTYPE_ZFS;
		rewind(mnttab);

		if (getmntany(mnttab, &entry, &search) == 0) {
			zhp->zfs_mntopts = zfs_strdup(zhp->zfs_hdl,
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
		*val = getprop_uint64(zhp, prop, source);
		if (*val != ZFS_CANMOUNT_ON)
			*source = zhp->zfs_name;
		else
			*source = "";	/* default */
		break;

	case ZFS_PROP_QUOTA:
	case ZFS_PROP_REFQUOTA:
	case ZFS_PROP_RESERVATION:
	case ZFS_PROP_REFRESERVATION:
		*val = getprop_uint64(zhp, prop, source);
		if (*val == 0)
			*source = "";	/* default */
		else
			*source = zhp->zfs_name;
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
			zfs_error_aux(zhp->zfs_hdl, dgettext(TEXT_DOMAIN,
			    "unable to get %s property"),
			    zfs_prop_to_name(prop));
			return (zfs_error(zhp->zfs_hdl, EZFS_BADVERSION,
			    dgettext(TEXT_DOMAIN, "internal error")));
		}
		if (zcmd_read_dst_nvlist(zhp->zfs_hdl, &zc, &zplprops) != 0 ||
		    nvlist_lookup_uint64(zplprops, zfs_prop_to_name(prop),
		    val) != 0) {
			zcmd_free_nvlists(&zc);
			zfs_error_aux(zhp->zfs_hdl, dgettext(TEXT_DOMAIN,
			    "unable to get %s property"),
			    zfs_prop_to_name(prop));
			return (zfs_error(zhp->zfs_hdl, EZFS_NOMEM,
			    dgettext(TEXT_DOMAIN, "internal error")));
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
			 * If we tried to use a defalut value for a
			 * readonly property, it means that it was not
			 * present; return an error.
			 */
			if (zfs_prop_readonly(prop) &&
			    *source && (*source)[0] == '\0') {
				return (-1);
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
	} else {
		if (strcmp(source, zhp->zfs_name) == 0) {
			*srctype = ZPROP_SRC_LOCAL;
		} else {
			(void) strlcpy(statbuf, source, statlen);
			*srctype = ZPROP_SRC_INHERITED;
		}
	}

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

	/*
	 * Check to see if this property applies to our object
	 */
	if (!zfs_prop_valid_for_type(prop, zhp->zfs_type))
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
			const char *relpath = zhp->zfs_name + strlen(source);

			if (relpath[0] == '/')
				relpath++;

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
		(void) snprintf(propbuf, proplen, "%lld.%02lldx", (longlong_t)
		    val / 100, (longlong_t)val % 100);
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

	zfs_nicenum(val, buf, sizeof (buf));
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

	for ((void) strlcpy(zc.zc_name, zhp->zfs_name, sizeof (zc.zc_name));
	    ioctl(zhp->zfs_hdl->libzfs_fd, ZFS_IOC_DATASET_LIST_NEXT, &zc) == 0;
	    (void) strlcpy(zc.zc_name, zhp->zfs_name, sizeof (zc.zc_name))) {
		/*
		 * Ignore private dataset names.
		 */
		if (dataset_name_hidden(zc.zc_name))
			continue;

		/*
		 * Silently ignore errors, as the only plausible explanation is
		 * that the pool has since been removed.
		 */
		if ((nzhp = make_dataset_handle(zhp->zfs_hdl,
		    zc.zc_name)) == NULL)
			continue;

		if ((ret = func(nzhp, data)) != 0)
			return (ret);
	}

	/*
	 * An errno value of ESRCH indicates normal completion.  If ENOENT is
	 * returned, then the underlying dataset has been removed since we
	 * obtained the handle.
	 */
	if (errno != ESRCH && errno != ENOENT)
		return (zfs_standard_error(zhp->zfs_hdl, errno,
		    dgettext(TEXT_DOMAIN, "cannot iterate filesystems")));

	return (0);
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

	for ((void) strlcpy(zc.zc_name, zhp->zfs_name, sizeof (zc.zc_name));
	    ioctl(zhp->zfs_hdl->libzfs_fd, ZFS_IOC_SNAPSHOT_LIST_NEXT,
	    &zc) == 0;
	    (void) strlcpy(zc.zc_name, zhp->zfs_name, sizeof (zc.zc_name))) {

		if ((nzhp = make_dataset_handle(zhp->zfs_hdl,
		    zc.zc_name)) == NULL)
			continue;

		if ((ret = func(nzhp, data)) != 0)
			return (ret);
	}

	/*
	 * An errno value of ESRCH indicates normal completion.  If ENOENT is
	 * returned, then the underlying dataset has been removed since we
	 * obtained the handle.  Silently ignore this case, and return success.
	 */
	if (errno != ESRCH && errno != ENOENT)
		return (zfs_standard_error(zhp->zfs_hdl, errno,
		    dgettext(TEXT_DOMAIN, "cannot iterate filesystems")));

	return (0);
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

	(void) snprintf(errbuf, sizeof (errbuf), "cannot create '%s'",
	    path);

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

	*zoned = zfs_prop_get_int(zhp, ZFS_PROP_ZONED);
	/* we are in a non-global zone, but parent is in the global zone */
	if (getzoneid() != GLOBAL_ZONEID && !(*zoned)) {
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
	uint64_t zoned;
	char *path_copy;
	int rc;

	if (check_parents(hdl, path, &zoned, B_TRUE, &prefix) != 0)
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

	if (ret == 0 && type == ZFS_TYPE_VOLUME) {
		ret = zvol_create_link(hdl, path);
		if (ret) {
			(void) zfs_standard_error(hdl, errno,
			    dgettext(TEXT_DOMAIN,
			    "Volume successfully created, but device links "
			    "were not created"));
			zcmd_free_nvlists(&zc);
			return (-1);
		}
	}

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
zfs_destroy(zfs_handle_t *zhp)
{
	zfs_cmd_t zc = { 0 };

	(void) strlcpy(zc.zc_name, zhp->zfs_name, sizeof (zc.zc_name));

	if (ZFS_IS_VOLUME(zhp)) {
		/*
		 * If user doesn't have permissions to unshare volume, then
		 * abort the request.  This would only happen for a
		 * non-privileged user.
		 */
		if (zfs_unshare_iscsi(zhp) != 0) {
			return (-1);
		}

		if (zvol_remove_link(zhp->zfs_hdl, zhp->zfs_name) != 0)
			return (-1);

		zc.zc_objset_type = DMU_OST_ZVOL;
	} else {
		zc.zc_objset_type = DMU_OST_ZFS;
	}

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
zfs_remove_link_cb(zfs_handle_t *zhp, void *arg)
{
	struct destroydata *dd = arg;
	zfs_handle_t *szhp;
	char name[ZFS_MAXNAMELEN];
	boolean_t closezhp = dd->closezhp;
	int rv;

	(void) strlcpy(name, zhp->zfs_name, sizeof (name));
	(void) strlcat(name, "@", sizeof (name));
	(void) strlcat(name, dd->snapname, sizeof (name));

	szhp = make_dataset_handle(zhp->zfs_hdl, name);
	if (szhp) {
		dd->gotone = B_TRUE;
		zfs_close(szhp);
	}

	if (zhp->zfs_type == ZFS_TYPE_VOLUME) {
		(void) zvol_remove_link(zhp->zfs_hdl, name);
		/*
		 * NB: this is simply a best-effort.  We don't want to
		 * return an error, because then we wouldn't visit all
		 * the volumes.
		 */
	}

	dd->closezhp = B_TRUE;
	rv = zfs_iter_filesystems(zhp, zfs_remove_link_cb, arg);
	if (closezhp)
		zfs_close(zhp);
	return (rv);
}

/*
 * Destroys all snapshots with the given name in zhp & descendants.
 */
int
zfs_destroy_snaps(zfs_handle_t *zhp, char *snapname)
{
	zfs_cmd_t zc = { 0 };
	int ret;
	struct destroydata dd = { 0 };

	dd.snapname = snapname;
	(void) zfs_remove_link_cb(zhp, &dd);

	if (!dd.gotone) {
		return (zfs_standard_error_fmt(zhp->zfs_hdl, ENOENT,
		    dgettext(TEXT_DOMAIN, "cannot destroy '%s@%s'"),
		    zhp->zfs_name, snapname));
	}

	(void) strlcpy(zc.zc_name, zhp->zfs_name, sizeof (zc.zc_name));
	(void) strlcpy(zc.zc_value, snapname, sizeof (zc.zc_value));

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
	} else if (ZFS_IS_VOLUME(zhp)) {
		ret = zvol_create_link(zhp->zfs_hdl, target);
	}

	return (ret);
}

typedef struct promote_data {
	char cb_mountpoint[MAXPATHLEN];
	const char *cb_target;
	const char *cb_errbuf;
	uint64_t cb_pivot_txg;
} promote_data_t;

static int
promote_snap_cb(zfs_handle_t *zhp, void *data)
{
	promote_data_t *pd = data;
	zfs_handle_t *szhp;
	char snapname[MAXPATHLEN];
	int rv = 0;

	/* We don't care about snapshots after the pivot point */
	if (zfs_prop_get_int(zhp, ZFS_PROP_CREATETXG) > pd->cb_pivot_txg) {
		zfs_close(zhp);
		return (0);
	}

	/* Remove the device link if it's a zvol. */
	if (ZFS_IS_VOLUME(zhp))
		(void) zvol_remove_link(zhp->zfs_hdl, zhp->zfs_name);

	/* Check for conflicting names */
	(void) strlcpy(snapname, pd->cb_target, sizeof (snapname));
	(void) strlcat(snapname, strchr(zhp->zfs_name, '@'), sizeof (snapname));
	szhp = make_dataset_handle(zhp->zfs_hdl, snapname);
	if (szhp != NULL) {
		zfs_close(szhp);
		zfs_error_aux(zhp->zfs_hdl, dgettext(TEXT_DOMAIN,
		    "snapshot name '%s' from origin \n"
		    "conflicts with '%s' from target"),
		    zhp->zfs_name, snapname);
		rv = zfs_error(zhp->zfs_hdl, EZFS_EXISTS, pd->cb_errbuf);
	}
	zfs_close(zhp);
	return (rv);
}

static int
promote_snap_done_cb(zfs_handle_t *zhp, void *data)
{
	promote_data_t *pd = data;

	/* We don't care about snapshots after the pivot point */
	if (zfs_prop_get_int(zhp, ZFS_PROP_CREATETXG) <= pd->cb_pivot_txg) {
		/* Create the device link if it's a zvol. */
		if (ZFS_IS_VOLUME(zhp))
			(void) zvol_create_link(zhp->zfs_hdl, zhp->zfs_name);
	}

	zfs_close(zhp);
	return (0);
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
	char *cp;
	int ret;
	zfs_handle_t *pzhp;
	promote_data_t pd;
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
	cp = strchr(parent, '@');
	*cp = '\0';

	/* Walk the snapshots we will be moving */
	pzhp = zfs_open(hdl, zhp->zfs_dmustats.dds_origin, ZFS_TYPE_SNAPSHOT);
	if (pzhp == NULL)
		return (-1);
	pd.cb_pivot_txg = zfs_prop_get_int(pzhp, ZFS_PROP_CREATETXG);
	zfs_close(pzhp);
	pd.cb_target = zhp->zfs_name;
	pd.cb_errbuf = errbuf;
	pzhp = zfs_open(hdl, parent, ZFS_TYPE_DATASET);
	if (pzhp == NULL)
		return (-1);
	(void) zfs_prop_get(pzhp, ZFS_PROP_MOUNTPOINT, pd.cb_mountpoint,
	    sizeof (pd.cb_mountpoint), NULL, NULL, 0, FALSE);
	ret = zfs_iter_snapshots(pzhp, promote_snap_cb, &pd);
	if (ret != 0) {
		zfs_close(pzhp);
		return (-1);
	}

	/* issue the ioctl */
	(void) strlcpy(zc.zc_value, zhp->zfs_dmustats.dds_origin,
	    sizeof (zc.zc_value));
	(void) strlcpy(zc.zc_name, zhp->zfs_name, sizeof (zc.zc_name));
	ret = zfs_ioctl(hdl, ZFS_IOC_PROMOTE, &zc);

	if (ret != 0) {
		int save_errno = errno;

		(void) zfs_iter_snapshots(pzhp, promote_snap_done_cb, &pd);
		zfs_close(pzhp);

		switch (save_errno) {
		case EEXIST:
			/*
			 * There is a conflicting snapshot name.  We
			 * should have caught this above, but they could
			 * have renamed something in the mean time.
			 */
			zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
			    "conflicting snapshot name from parent '%s'"),
			    parent);
			return (zfs_error(hdl, EZFS_EXISTS, errbuf));

		default:
			return (zfs_standard_error(hdl, save_errno, errbuf));
		}
	} else {
		(void) zfs_iter_snapshots(zhp, promote_snap_done_cb, &pd);
	}

	zfs_close(pzhp);
	return (ret);
}

struct createdata {
	const char *cd_snapname;
	int cd_ifexists;
};

static int
zfs_create_link_cb(zfs_handle_t *zhp, void *arg)
{
	struct createdata *cd = arg;
	int ret;

	if (zhp->zfs_type == ZFS_TYPE_VOLUME) {
		char name[MAXPATHLEN];

		(void) strlcpy(name, zhp->zfs_name, sizeof (name));
		(void) strlcat(name, "@", sizeof (name));
		(void) strlcat(name, cd->cd_snapname, sizeof (name));
		(void) zvol_create_link_common(zhp->zfs_hdl, name,
		    cd->cd_ifexists);
		/*
		 * NB: this is simply a best-effort.  We don't want to
		 * return an error, because then we wouldn't visit all
		 * the volumes.
		 */
	}

	ret = zfs_iter_filesystems(zhp, zfs_create_link_cb, cd);

	zfs_close(zhp);

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
	if (ret != 0)
		(void) snprintf(errbuf, sizeof (errbuf), dgettext(TEXT_DOMAIN,
		    "cannot create snapshot '%s@%s'"), zc.zc_name, zc.zc_value);

	if (ret == 0 && recursive) {
		struct createdata cd;

		cd.cd_snapname = delim + 1;
		cd.cd_ifexists = B_FALSE;
		(void) zfs_iter_filesystems(zhp, zfs_create_link_cb, &cd);
	}
	if (ret == 0 && zhp->zfs_type == ZFS_TYPE_VOLUME) {
		ret = zvol_create_link(zhp->zfs_hdl, path);
		if (ret != 0) {
			(void) zfs_standard_error(hdl, errno,
			    dgettext(TEXT_DOMAIN,
			    "Volume successfully snapshotted, but device links "
			    "were not created"));
			zfs_close(zhp);
			return (-1);
		}
	}

	if (ret != 0)
		(void) zfs_standard_error(hdl, errno, errbuf);

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
			cbp->cb_error |= zfs_destroy(zhp);
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
		if (zfs_destroy(zhp) != 0)
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
		if (zvol_remove_link(zhp->zfs_hdl, zhp->zfs_name) != 0)
			return (-1);
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
		if (err = zvol_create_link(zhp->zfs_hdl, zhp->zfs_name)) {
			zfs_close(zhp);
			return (err);
		}
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
		uint64_t unused;

		/* validate parents */
		if (check_parents(hdl, target, &unused, B_FALSE, NULL) != 0)
			return (-1);

		(void) parent_name(target, parent, sizeof (parent));

		/* make sure we're in the same pool */
		verify((delim = strchr(target, '/')) != NULL);
		if (strncmp(zhp->zfs_name, target, delim - target) != 0 ||
		    zhp->zfs_name[delim - target] != '/') {
			zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
			    "datasets must be within same pool"));
			return (zfs_error(hdl, EZFS_CROSSTARGET, errbuf));
		}

		/* new name cannot be a child of the current dataset name */
		if (strncmp(parent, zhp->zfs_name,
		    strlen(zhp->zfs_name)) == 0) {
			zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
			    "New dataset name cannot be a descendent of "
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
		struct destroydata dd;

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

		dd.snapname = delim + 1;
		dd.gotone = B_FALSE;
		dd.closezhp = B_TRUE;

		/* We remove any zvol links prior to renaming them */
		ret = zfs_iter_filesystems(zhrp, zfs_remove_link_cb, &dd);
		if (ret) {
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
		if (recursive) {
			struct createdata cd;

			/* only create links for datasets that had existed */
			cd.cd_snapname = delim + 1;
			cd.cd_ifexists = B_TRUE;
			(void) zfs_iter_filesystems(zhrp, zfs_create_link_cb,
			    &cd);
		} else {
			(void) changelist_postfix(cl);
		}
	} else {
		if (recursive) {
			struct createdata cd;

			/* only create links for datasets that had existed */
			cd.cd_snapname = strchr(target, '@') + 1;
			cd.cd_ifexists = B_TRUE;
			ret = zfs_iter_filesystems(zhrp, zfs_create_link_cb,
			    &cd);
		} else {
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

/*
 * Given a zvol dataset, issue the ioctl to create the appropriate minor node,
 * poke devfsadm to create the /dev link, and then wait for the link to appear.
 */
int
zvol_create_link(libzfs_handle_t *hdl, const char *dataset)
{
	return (zvol_create_link_common(hdl, dataset, B_FALSE));
}

static int
zvol_create_link_common(libzfs_handle_t *hdl, const char *dataset, int ifexists)
{
	zfs_cmd_t zc = { 0 };
	di_devlink_handle_t dhdl;
	priv_set_t *priv_effective;
	int privileged;

	(void) strlcpy(zc.zc_name, dataset, sizeof (zc.zc_name));

	/*
	 * Issue the appropriate ioctl.
	 */
	if (ioctl(hdl->libzfs_fd, ZFS_IOC_CREATE_MINOR, &zc) != 0) {
		switch (errno) {
		case EEXIST:
			/*
			 * Silently ignore the case where the link already
			 * exists.  This allows 'zfs volinit' to be run multiple
			 * times without errors.
			 */
			return (0);

		case ENOENT:
			/*
			 * Dataset does not exist in the kernel.  If we
			 * don't care (see zfs_rename), then ignore the
			 * error quietly.
			 */
			if (ifexists) {
				return (0);
			}

			/* FALLTHROUGH */

		default:
			return (zfs_standard_error_fmt(hdl, errno,
			    dgettext(TEXT_DOMAIN, "cannot create device links "
			    "for '%s'"), dataset));
		}
	}

	/*
	 * If privileged call devfsadm and wait for the links to
	 * magically appear.
	 * Otherwise, print out an informational message.
	 */

	priv_effective = priv_allocset();
	(void) getppriv(PRIV_EFFECTIVE, priv_effective);
	privileged = (priv_isfullset(priv_effective) == B_TRUE);
	priv_freeset(priv_effective);

	if (privileged) {
		if ((dhdl = di_devlink_init(ZFS_DRIVER,
		    DI_MAKE_LINK)) == NULL) {
			zfs_error_aux(hdl, strerror(errno));
			(void) zfs_error_fmt(hdl, errno,
			    dgettext(TEXT_DOMAIN, "cannot create device links "
			    "for '%s'"), dataset);
			(void) ioctl(hdl->libzfs_fd, ZFS_IOC_REMOVE_MINOR, &zc);
			return (-1);
		} else {
			(void) di_devlink_fini(&dhdl);
		}
	} else {
		char pathname[MAXPATHLEN];
		struct stat64 statbuf;
		int i;

#define	MAX_WAIT	10

		/*
		 * This is the poor mans way of waiting for the link
		 * to show up.  If after 10 seconds we still don't
		 * have it, then print out a message.
		 */
		(void) snprintf(pathname, sizeof (pathname), "/dev/zvol/dsk/%s",
		    dataset);

		for (i = 0; i != MAX_WAIT; i++) {
			if (stat64(pathname, &statbuf) == 0)
				break;
			(void) sleep(1);
		}
		if (i == MAX_WAIT)
			(void) printf(gettext("%s may not be immediately "
			    "available\n"), pathname);
	}

	return (0);
}

/*
 * Remove a minor node for the given zvol and the associated /dev links.
 */
int
zvol_remove_link(libzfs_handle_t *hdl, const char *dataset)
{
	zfs_cmd_t zc = { 0 };

	(void) strlcpy(zc.zc_name, dataset, sizeof (zc.zc_name));

	if (ioctl(hdl->libzfs_fd, ZFS_IOC_REMOVE_MINOR, &zc) != 0) {
		switch (errno) {
		case ENXIO:
			/*
			 * Silently ignore the case where the link no longer
			 * exists, so that 'zfs volfini' can be run multiple
			 * times without errors.
			 */
			return (0);

		default:
			return (zfs_standard_error_fmt(hdl, errno,
			    dgettext(TEXT_DOMAIN, "cannot remove device "
			    "links for '%s'"), dataset));
		}
	}

	return (0);
}

nvlist_t *
zfs_get_user_props(zfs_handle_t *zhp)
{
	return (zhp->zfs_user_props);
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
 *        so that we can size the column appropriately.
 */
int
zfs_expand_proplist(zfs_handle_t *zhp, zprop_list_t **plp)
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
		} else if (nvlist_lookup_nvlist(userprops,
		    entry->pl_user_prop, &propval)  == 0) {
			verify(nvlist_lookup_string(propval,
			    ZPROP_VALUE, &strval) == 0);
			if (strlen(strval) > entry->pl_width)
				entry->pl_width = strlen(strval);
		}
	}

	return (0);
}

int
zfs_iscsi_perm_check(libzfs_handle_t *hdl, char *dataset, ucred_t *cred)
{
	zfs_cmd_t zc = { 0 };
	nvlist_t *nvp;
	gid_t gid;
	uid_t uid;
	const gid_t *groups;
	int group_cnt;
	int error;

	if (nvlist_alloc(&nvp, NV_UNIQUE_NAME, 0) != 0)
		return (no_memory(hdl));

	uid = ucred_geteuid(cred);
	gid = ucred_getegid(cred);
	group_cnt = ucred_getgroups(cred, &groups);

	if (uid == (uid_t)-1 || gid == (uid_t)-1 || group_cnt == (uid_t)-1)
		return (1);

	if (nvlist_add_uint32(nvp, ZFS_DELEG_PERM_UID, uid) != 0) {
		nvlist_free(nvp);
		return (1);
	}

	if (nvlist_add_uint32(nvp, ZFS_DELEG_PERM_GID, gid) != 0) {
		nvlist_free(nvp);
		return (1);
	}

	if (nvlist_add_uint32_array(nvp,
	    ZFS_DELEG_PERM_GROUPS, (uint32_t *)groups, group_cnt) != 0) {
		nvlist_free(nvp);
		return (1);
	}
	(void) strlcpy(zc.zc_name, dataset, sizeof (zc.zc_name));

	if (zcmd_write_src_nvlist(hdl, &zc, nvp))
		return (-1);

	error = ioctl(hdl->libzfs_fd, ZFS_IOC_ISCSI_PERM_CHECK, &zc);
	nvlist_free(nvp);
	return (error);
}

int
zfs_deleg_share_nfs(libzfs_handle_t *hdl, char *dataset, char *path,
    void *export, void *sharetab, int sharemax, zfs_share_op_t operation)
{
	zfs_cmd_t zc = { 0 };
	int error;

	(void) strlcpy(zc.zc_name, dataset, sizeof (zc.zc_name));
	(void) strlcpy(zc.zc_value, path, sizeof (zc.zc_value));
	zc.zc_share.z_sharedata = (uint64_t)(uintptr_t)sharetab;
	zc.zc_share.z_exportdata = (uint64_t)(uintptr_t)export;
	zc.zc_share.z_sharetype = operation;
	zc.zc_share.z_sharemax = sharemax;

	error = ioctl(hdl->libzfs_fd, ZFS_IOC_SHARE, &zc);
	return (error);
}
