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

#include <alloca.h>
#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <devid.h>
#include <dirent.h>
#include <fcntl.h>
#include <libintl.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <unistd.h>
#include <zone.h>
#include <sys/efi_partition.h>
#include <sys/vtoc.h>
#include <sys/zfs_ioctl.h>
#include <sys/zio.h>
#include <strings.h>

#include "zfs_namecheck.h"
#include "zfs_prop.h"
#include "libzfs_impl.h"

static int read_efi_label(nvlist_t *config, diskaddr_t *sb);

#if defined(__i386) || defined(__amd64)
#define	BOOTCMD	"installgrub(1M)"
#else
#define	BOOTCMD	"installboot(1M)"
#endif

/*
 * ====================================================================
 *   zpool property functions
 * ====================================================================
 */

static int
zpool_get_all_props(zpool_handle_t *zhp)
{
	zfs_cmd_t zc = { 0 };
	libzfs_handle_t *hdl = zhp->zpool_hdl;

	(void) strlcpy(zc.zc_name, zhp->zpool_name, sizeof (zc.zc_name));

	if (zcmd_alloc_dst_nvlist(hdl, &zc, 0) != 0)
		return (-1);

	while (ioctl(hdl->libzfs_fd, ZFS_IOC_POOL_GET_PROPS, &zc) != 0) {
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

	if (zcmd_read_dst_nvlist(hdl, &zc, &zhp->zpool_props) != 0) {
		zcmd_free_nvlists(&zc);
		return (-1);
	}

	zcmd_free_nvlists(&zc);

	return (0);
}

static int
zpool_props_refresh(zpool_handle_t *zhp)
{
	nvlist_t *old_props;

	old_props = zhp->zpool_props;

	if (zpool_get_all_props(zhp) != 0)
		return (-1);

	nvlist_free(old_props);
	return (0);
}

static char *
zpool_get_prop_string(zpool_handle_t *zhp, zpool_prop_t prop,
    zprop_source_t *src)
{
	nvlist_t *nv, *nvl;
	uint64_t ival;
	char *value;
	zprop_source_t source;

	nvl = zhp->zpool_props;
	if (nvlist_lookup_nvlist(nvl, zpool_prop_to_name(prop), &nv) == 0) {
		verify(nvlist_lookup_uint64(nv, ZPROP_SOURCE, &ival) == 0);
		source = ival;
		verify(nvlist_lookup_string(nv, ZPROP_VALUE, &value) == 0);
	} else {
		source = ZPROP_SRC_DEFAULT;
		if ((value = (char *)zpool_prop_default_string(prop)) == NULL)
			value = "-";
	}

	if (src)
		*src = source;

	return (value);
}

uint64_t
zpool_get_prop_int(zpool_handle_t *zhp, zpool_prop_t prop, zprop_source_t *src)
{
	nvlist_t *nv, *nvl;
	uint64_t value;
	zprop_source_t source;

	if (zhp->zpool_props == NULL && zpool_get_all_props(zhp)) {
		/*
		 * zpool_get_all_props() has most likely failed because
		 * the pool is faulted, but if all we need is the top level
		 * vdev's guid then get it from the zhp config nvlist.
		 */
		if ((prop == ZPOOL_PROP_GUID) &&
		    (nvlist_lookup_nvlist(zhp->zpool_config,
		    ZPOOL_CONFIG_VDEV_TREE, &nv) == 0) &&
		    (nvlist_lookup_uint64(nv, ZPOOL_CONFIG_GUID, &value)
		    == 0)) {
			return (value);
		}
		return (zpool_prop_default_numeric(prop));
	}

	nvl = zhp->zpool_props;
	if (nvlist_lookup_nvlist(nvl, zpool_prop_to_name(prop), &nv) == 0) {
		verify(nvlist_lookup_uint64(nv, ZPROP_SOURCE, &value) == 0);
		source = value;
		verify(nvlist_lookup_uint64(nv, ZPROP_VALUE, &value) == 0);
	} else {
		source = ZPROP_SRC_DEFAULT;
		value = zpool_prop_default_numeric(prop);
	}

	if (src)
		*src = source;

	return (value);
}

/*
 * Map VDEV STATE to printed strings.
 */
char *
zpool_state_to_name(vdev_state_t state, vdev_aux_t aux)
{
	switch (state) {
	case VDEV_STATE_CLOSED:
	case VDEV_STATE_OFFLINE:
		return (gettext("OFFLINE"));
	case VDEV_STATE_REMOVED:
		return (gettext("REMOVED"));
	case VDEV_STATE_CANT_OPEN:
		if (aux == VDEV_AUX_CORRUPT_DATA || aux == VDEV_AUX_BAD_LOG)
			return (gettext("FAULTED"));
		else
			return (gettext("UNAVAIL"));
	case VDEV_STATE_FAULTED:
		return (gettext("FAULTED"));
	case VDEV_STATE_DEGRADED:
		return (gettext("DEGRADED"));
	case VDEV_STATE_HEALTHY:
		return (gettext("ONLINE"));
	}

	return (gettext("UNKNOWN"));
}

/*
 * Get a zpool property value for 'prop' and return the value in
 * a pre-allocated buffer.
 */
int
zpool_get_prop(zpool_handle_t *zhp, zpool_prop_t prop, char *buf, size_t len,
    zprop_source_t *srctype)
{
	uint64_t intval;
	const char *strval;
	zprop_source_t src = ZPROP_SRC_NONE;
	nvlist_t *nvroot;
	vdev_stat_t *vs;
	uint_t vsc;

	if (zpool_get_state(zhp) == POOL_STATE_UNAVAIL) {
		if (prop == ZPOOL_PROP_NAME)
			(void) strlcpy(buf, zpool_get_name(zhp), len);
		else if (prop == ZPOOL_PROP_HEALTH)
			(void) strlcpy(buf, "FAULTED", len);
		else
			(void) strlcpy(buf, "-", len);
		return (0);
	}

	if (zhp->zpool_props == NULL && zpool_get_all_props(zhp) &&
	    prop != ZPOOL_PROP_NAME)
		return (-1);

	switch (zpool_prop_get_type(prop)) {
	case PROP_TYPE_STRING:
		(void) strlcpy(buf, zpool_get_prop_string(zhp, prop, &src),
		    len);
		break;

	case PROP_TYPE_NUMBER:
		intval = zpool_get_prop_int(zhp, prop, &src);

		switch (prop) {
		case ZPOOL_PROP_SIZE:
		case ZPOOL_PROP_USED:
		case ZPOOL_PROP_AVAILABLE:
			(void) zfs_nicenum(intval, buf, len);
			break;

		case ZPOOL_PROP_CAPACITY:
			(void) snprintf(buf, len, "%llu%%",
			    (u_longlong_t)intval);
			break;

		case ZPOOL_PROP_HEALTH:
			verify(nvlist_lookup_nvlist(zpool_get_config(zhp, NULL),
			    ZPOOL_CONFIG_VDEV_TREE, &nvroot) == 0);
			verify(nvlist_lookup_uint64_array(nvroot,
			    ZPOOL_CONFIG_STATS, (uint64_t **)&vs, &vsc) == 0);

			(void) strlcpy(buf, zpool_state_to_name(intval,
			    vs->vs_aux), len);
			break;
		default:
			(void) snprintf(buf, len, "%llu", intval);
		}
		break;

	case PROP_TYPE_INDEX:
		intval = zpool_get_prop_int(zhp, prop, &src);
		if (zpool_prop_index_to_string(prop, intval, &strval)
		    != 0)
			return (-1);
		(void) strlcpy(buf, strval, len);
		break;

	default:
		abort();
	}

	if (srctype)
		*srctype = src;

	return (0);
}

/*
 * Check if the bootfs name has the same pool name as it is set to.
 * Assuming bootfs is a valid dataset name.
 */
static boolean_t
bootfs_name_valid(const char *pool, char *bootfs)
{
	int len = strlen(pool);

	if (!zfs_name_valid(bootfs, ZFS_TYPE_FILESYSTEM|ZFS_TYPE_SNAPSHOT))
		return (B_FALSE);

	if (strncmp(pool, bootfs, len) == 0 &&
	    (bootfs[len] == '/' || bootfs[len] == '\0'))
		return (B_TRUE);

	return (B_FALSE);
}

/*
 * Inspect the configuration to determine if any of the devices contain
 * an EFI label.
 */
static boolean_t
pool_uses_efi(nvlist_t *config)
{
	nvlist_t **child;
	uint_t c, children;

	if (nvlist_lookup_nvlist_array(config, ZPOOL_CONFIG_CHILDREN,
	    &child, &children) != 0)
		return (read_efi_label(config, NULL) >= 0);

	for (c = 0; c < children; c++) {
		if (pool_uses_efi(child[c]))
			return (B_TRUE);
	}
	return (B_FALSE);
}

static boolean_t
pool_is_bootable(zpool_handle_t *zhp)
{
	char bootfs[ZPOOL_MAXNAMELEN];

	return (zpool_get_prop(zhp, ZPOOL_PROP_BOOTFS, bootfs,
	    sizeof (bootfs), NULL) == 0 && strncmp(bootfs, "-",
	    sizeof (bootfs)) != 0);
}


/*
 * Given an nvlist of zpool properties to be set, validate that they are
 * correct, and parse any numeric properties (index, boolean, etc) if they are
 * specified as strings.
 */
static nvlist_t *
zpool_valid_proplist(libzfs_handle_t *hdl, const char *poolname,
    nvlist_t *props, uint64_t version, boolean_t create_or_import, char *errbuf)
{
	nvpair_t *elem;
	nvlist_t *retprops;
	zpool_prop_t prop;
	char *strval;
	uint64_t intval;
	char *slash;
	struct stat64 statbuf;
	zpool_handle_t *zhp;
	nvlist_t *nvroot;

	if (nvlist_alloc(&retprops, NV_UNIQUE_NAME, 0) != 0) {
		(void) no_memory(hdl);
		return (NULL);
	}

	elem = NULL;
	while ((elem = nvlist_next_nvpair(props, elem)) != NULL) {
		const char *propname = nvpair_name(elem);

		/*
		 * Make sure this property is valid and applies to this type.
		 */
		if ((prop = zpool_name_to_prop(propname)) == ZPROP_INVAL) {
			zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
			    "invalid property '%s'"), propname);
			(void) zfs_error(hdl, EZFS_BADPROP, errbuf);
			goto error;
		}

		if (zpool_prop_readonly(prop)) {
			zfs_error_aux(hdl, dgettext(TEXT_DOMAIN, "'%s' "
			    "is readonly"), propname);
			(void) zfs_error(hdl, EZFS_PROPREADONLY, errbuf);
			goto error;
		}

		if (zprop_parse_value(hdl, elem, prop, ZFS_TYPE_POOL, retprops,
		    &strval, &intval, errbuf) != 0)
			goto error;

		/*
		 * Perform additional checking for specific properties.
		 */
		switch (prop) {
		case ZPOOL_PROP_VERSION:
			if (intval < version || intval > SPA_VERSION) {
				zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
				    "property '%s' number %d is invalid."),
				    propname, intval);
				(void) zfs_error(hdl, EZFS_BADVERSION, errbuf);
				goto error;
			}
			break;

		case ZPOOL_PROP_BOOTFS:
			if (create_or_import) {
				zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
				    "property '%s' cannot be set at creation "
				    "or import time"), propname);
				(void) zfs_error(hdl, EZFS_BADPROP, errbuf);
				goto error;
			}

			if (version < SPA_VERSION_BOOTFS) {
				zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
				    "pool must be upgraded to support "
				    "'%s' property"), propname);
				(void) zfs_error(hdl, EZFS_BADVERSION, errbuf);
				goto error;
			}

			/*
			 * bootfs property value has to be a dataset name and
			 * the dataset has to be in the same pool as it sets to.
			 */
			if (strval[0] != '\0' && !bootfs_name_valid(poolname,
			    strval)) {
				zfs_error_aux(hdl, dgettext(TEXT_DOMAIN, "'%s' "
				    "is an invalid name"), strval);
				(void) zfs_error(hdl, EZFS_INVALIDNAME, errbuf);
				goto error;
			}

			if ((zhp = zpool_open_canfail(hdl, poolname)) == NULL) {
				zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
				    "could not open pool '%s'"), poolname);
				(void) zfs_error(hdl, EZFS_OPENFAILED, errbuf);
				goto error;
			}
			verify(nvlist_lookup_nvlist(zpool_get_config(zhp, NULL),
			    ZPOOL_CONFIG_VDEV_TREE, &nvroot) == 0);

			/*
			 * bootfs property cannot be set on a disk which has
			 * been EFI labeled.
			 */
			if (pool_uses_efi(nvroot)) {
				zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
				    "property '%s' not supported on "
				    "EFI labeled devices"), propname);
				(void) zfs_error(hdl, EZFS_POOL_NOTSUP, errbuf);
				zpool_close(zhp);
				goto error;
			}
			zpool_close(zhp);
			break;

		case ZPOOL_PROP_ALTROOT:
			if (!create_or_import) {
				zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
				    "property '%s' can only be set during pool "
				    "creation or import"), propname);
				(void) zfs_error(hdl, EZFS_BADPROP, errbuf);
				goto error;
			}

			if (strval[0] != '/') {
				zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
				    "bad alternate root '%s'"), strval);
				(void) zfs_error(hdl, EZFS_BADPATH, errbuf);
				goto error;
			}
			break;

		case ZPOOL_PROP_CACHEFILE:
			if (strval[0] == '\0')
				break;

			if (strcmp(strval, "none") == 0)
				break;

			if (strval[0] != '/') {
				zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
				    "property '%s' must be empty, an "
				    "absolute path, or 'none'"), propname);
				(void) zfs_error(hdl, EZFS_BADPATH, errbuf);
				goto error;
			}

			slash = strrchr(strval, '/');

			if (slash[1] == '\0' || strcmp(slash, "/.") == 0 ||
			    strcmp(slash, "/..") == 0) {
				zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
				    "'%s' is not a valid file"), strval);
				(void) zfs_error(hdl, EZFS_BADPATH, errbuf);
				goto error;
			}

			*slash = '\0';

			if (strval[0] != '\0' &&
			    (stat64(strval, &statbuf) != 0 ||
			    !S_ISDIR(statbuf.st_mode))) {
				zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
				    "'%s' is not a valid directory"),
				    strval);
				(void) zfs_error(hdl, EZFS_BADPATH, errbuf);
				goto error;
			}

			*slash = '/';
			break;
		}
	}

	return (retprops);
error:
	nvlist_free(retprops);
	return (NULL);
}

/*
 * Set zpool property : propname=propval.
 */
int
zpool_set_prop(zpool_handle_t *zhp, const char *propname, const char *propval)
{
	zfs_cmd_t zc = { 0 };
	int ret = -1;
	char errbuf[1024];
	nvlist_t *nvl = NULL;
	nvlist_t *realprops;
	uint64_t version;

	(void) snprintf(errbuf, sizeof (errbuf),
	    dgettext(TEXT_DOMAIN, "cannot set property for '%s'"),
	    zhp->zpool_name);

	if (zhp->zpool_props == NULL && zpool_get_all_props(zhp))
		return (zfs_error(zhp->zpool_hdl, EZFS_POOLPROPS, errbuf));

	if (nvlist_alloc(&nvl, NV_UNIQUE_NAME, 0) != 0)
		return (no_memory(zhp->zpool_hdl));

	if (nvlist_add_string(nvl, propname, propval) != 0) {
		nvlist_free(nvl);
		return (no_memory(zhp->zpool_hdl));
	}

	version = zpool_get_prop_int(zhp, ZPOOL_PROP_VERSION, NULL);
	if ((realprops = zpool_valid_proplist(zhp->zpool_hdl,
	    zhp->zpool_name, nvl, version, B_FALSE, errbuf)) == NULL) {
		nvlist_free(nvl);
		return (-1);
	}

	nvlist_free(nvl);
	nvl = realprops;

	/*
	 * Execute the corresponding ioctl() to set this property.
	 */
	(void) strlcpy(zc.zc_name, zhp->zpool_name, sizeof (zc.zc_name));

	if (zcmd_write_src_nvlist(zhp->zpool_hdl, &zc, nvl) != 0) {
		nvlist_free(nvl);
		return (-1);
	}

	ret = zfs_ioctl(zhp->zpool_hdl, ZFS_IOC_POOL_SET_PROPS, &zc);

	zcmd_free_nvlists(&zc);
	nvlist_free(nvl);

	if (ret)
		(void) zpool_standard_error(zhp->zpool_hdl, errno, errbuf);
	else
		(void) zpool_props_refresh(zhp);

	return (ret);
}

int
zpool_expand_proplist(zpool_handle_t *zhp, zprop_list_t **plp)
{
	libzfs_handle_t *hdl = zhp->zpool_hdl;
	zprop_list_t *entry;
	char buf[ZFS_MAXPROPLEN];

	if (zprop_expand_list(hdl, plp, ZFS_TYPE_POOL) != 0)
		return (-1);

	for (entry = *plp; entry != NULL; entry = entry->pl_next) {

		if (entry->pl_fixed)
			continue;

		if (entry->pl_prop != ZPROP_INVAL &&
		    zpool_get_prop(zhp, entry->pl_prop, buf, sizeof (buf),
		    NULL) == 0) {
			if (strlen(buf) > entry->pl_width)
				entry->pl_width = strlen(buf);
		}
	}

	return (0);
}


/*
 * Validate the given pool name, optionally putting an extended error message in
 * 'buf'.
 */
boolean_t
zpool_name_valid(libzfs_handle_t *hdl, boolean_t isopen, const char *pool)
{
	namecheck_err_t why;
	char what;
	int ret;

	ret = pool_namecheck(pool, &why, &what);

	/*
	 * The rules for reserved pool names were extended at a later point.
	 * But we need to support users with existing pools that may now be
	 * invalid.  So we only check for this expanded set of names during a
	 * create (or import), and only in userland.
	 */
	if (ret == 0 && !isopen &&
	    (strncmp(pool, "mirror", 6) == 0 ||
	    strncmp(pool, "raidz", 5) == 0 ||
	    strncmp(pool, "spare", 5) == 0 ||
	    strcmp(pool, "log") == 0)) {
		if (hdl != NULL)
			zfs_error_aux(hdl,
			    dgettext(TEXT_DOMAIN, "name is reserved"));
		return (B_FALSE);
	}


	if (ret != 0) {
		if (hdl != NULL) {
			switch (why) {
			case NAME_ERR_TOOLONG:
				zfs_error_aux(hdl,
				    dgettext(TEXT_DOMAIN, "name is too long"));
				break;

			case NAME_ERR_INVALCHAR:
				zfs_error_aux(hdl,
				    dgettext(TEXT_DOMAIN, "invalid character "
				    "'%c' in pool name"), what);
				break;

			case NAME_ERR_NOLETTER:
				zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
				    "name must begin with a letter"));
				break;

			case NAME_ERR_RESERVED:
				zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
				    "name is reserved"));
				break;

			case NAME_ERR_DISKLIKE:
				zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
				    "pool name is reserved"));
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

			case NAME_ERR_MULTIPLE_AT:
				zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
				    "multiple '@' delimiters in name"));
				break;

			}
		}
		return (B_FALSE);
	}

	return (B_TRUE);
}

/*
 * Open a handle to the given pool, even if the pool is currently in the FAULTED
 * state.
 */
zpool_handle_t *
zpool_open_canfail(libzfs_handle_t *hdl, const char *pool)
{
	zpool_handle_t *zhp;
	boolean_t missing;

	/*
	 * Make sure the pool name is valid.
	 */
	if (!zpool_name_valid(hdl, B_TRUE, pool)) {
		(void) zfs_error_fmt(hdl, EZFS_INVALIDNAME,
		    dgettext(TEXT_DOMAIN, "cannot open '%s'"),
		    pool);
		return (NULL);
	}

	if ((zhp = zfs_alloc(hdl, sizeof (zpool_handle_t))) == NULL)
		return (NULL);

	zhp->zpool_hdl = hdl;
	(void) strlcpy(zhp->zpool_name, pool, sizeof (zhp->zpool_name));

	if (zpool_refresh_stats(zhp, &missing) != 0) {
		zpool_close(zhp);
		return (NULL);
	}

	if (missing) {
		zfs_error_aux(hdl, dgettext(TEXT_DOMAIN, "no such pool"));
		(void) zfs_error_fmt(hdl, EZFS_NOENT,
		    dgettext(TEXT_DOMAIN, "cannot open '%s'"), pool);
		zpool_close(zhp);
		return (NULL);
	}

	return (zhp);
}

/*
 * Like the above, but silent on error.  Used when iterating over pools (because
 * the configuration cache may be out of date).
 */
int
zpool_open_silent(libzfs_handle_t *hdl, const char *pool, zpool_handle_t **ret)
{
	zpool_handle_t *zhp;
	boolean_t missing;

	if ((zhp = zfs_alloc(hdl, sizeof (zpool_handle_t))) == NULL)
		return (-1);

	zhp->zpool_hdl = hdl;
	(void) strlcpy(zhp->zpool_name, pool, sizeof (zhp->zpool_name));

	if (zpool_refresh_stats(zhp, &missing) != 0) {
		zpool_close(zhp);
		return (-1);
	}

	if (missing) {
		zpool_close(zhp);
		*ret = NULL;
		return (0);
	}

	*ret = zhp;
	return (0);
}

/*
 * Similar to zpool_open_canfail(), but refuses to open pools in the faulted
 * state.
 */
zpool_handle_t *
zpool_open(libzfs_handle_t *hdl, const char *pool)
{
	zpool_handle_t *zhp;

	if ((zhp = zpool_open_canfail(hdl, pool)) == NULL)
		return (NULL);

	if (zhp->zpool_state == POOL_STATE_UNAVAIL) {
		(void) zfs_error_fmt(hdl, EZFS_POOLUNAVAIL,
		    dgettext(TEXT_DOMAIN, "cannot open '%s'"), zhp->zpool_name);
		zpool_close(zhp);
		return (NULL);
	}

	return (zhp);
}

/*
 * Close the handle.  Simply frees the memory associated with the handle.
 */
void
zpool_close(zpool_handle_t *zhp)
{
	if (zhp->zpool_config)
		nvlist_free(zhp->zpool_config);
	if (zhp->zpool_old_config)
		nvlist_free(zhp->zpool_old_config);
	if (zhp->zpool_props)
		nvlist_free(zhp->zpool_props);
	free(zhp);
}

/*
 * Return the name of the pool.
 */
const char *
zpool_get_name(zpool_handle_t *zhp)
{
	return (zhp->zpool_name);
}


/*
 * Return the state of the pool (ACTIVE or UNAVAILABLE)
 */
int
zpool_get_state(zpool_handle_t *zhp)
{
	return (zhp->zpool_state);
}

/*
 * Create the named pool, using the provided vdev list.  It is assumed
 * that the consumer has already validated the contents of the nvlist, so we
 * don't have to worry about error semantics.
 */
int
zpool_create(libzfs_handle_t *hdl, const char *pool, nvlist_t *nvroot,
    nvlist_t *props, nvlist_t *fsprops)
{
	zfs_cmd_t zc = { 0 };
	nvlist_t *zc_fsprops = NULL;
	nvlist_t *zc_props = NULL;
	char msg[1024];
	char *altroot;
	int ret = -1;

	(void) snprintf(msg, sizeof (msg), dgettext(TEXT_DOMAIN,
	    "cannot create '%s'"), pool);

	if (!zpool_name_valid(hdl, B_FALSE, pool))
		return (zfs_error(hdl, EZFS_INVALIDNAME, msg));

	if (zcmd_write_conf_nvlist(hdl, &zc, nvroot) != 0)
		return (-1);

	if (props) {
		if ((zc_props = zpool_valid_proplist(hdl, pool, props,
		    SPA_VERSION_1, B_TRUE, msg)) == NULL) {
			goto create_failed;
		}
	}

	if (fsprops) {
		uint64_t zoned;
		char *zonestr;

		zoned = ((nvlist_lookup_string(fsprops,
		    zfs_prop_to_name(ZFS_PROP_ZONED), &zonestr) == 0) &&
		    strcmp(zonestr, "on") == 0);

		if ((zc_fsprops = zfs_valid_proplist(hdl,
		    ZFS_TYPE_FILESYSTEM, fsprops, zoned, NULL, msg)) == NULL) {
			goto create_failed;
		}
		if (!zc_props &&
		    (nvlist_alloc(&zc_props, NV_UNIQUE_NAME, 0) != 0)) {
			goto create_failed;
		}
		if (nvlist_add_nvlist(zc_props,
		    ZPOOL_ROOTFS_PROPS, zc_fsprops) != 0) {
			goto create_failed;
		}
	}

	if (zc_props && zcmd_write_src_nvlist(hdl, &zc, zc_props) != 0)
		goto create_failed;

	(void) strlcpy(zc.zc_name, pool, sizeof (zc.zc_name));

	if ((ret = zfs_ioctl(hdl, ZFS_IOC_POOL_CREATE, &zc)) != 0) {

		zcmd_free_nvlists(&zc);
		nvlist_free(zc_props);
		nvlist_free(zc_fsprops);

		switch (errno) {
		case EBUSY:
			/*
			 * This can happen if the user has specified the same
			 * device multiple times.  We can't reliably detect this
			 * until we try to add it and see we already have a
			 * label.
			 */
			zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
			    "one or more vdevs refer to the same device"));
			return (zfs_error(hdl, EZFS_BADDEV, msg));

		case EOVERFLOW:
			/*
			 * This occurs when one of the devices is below
			 * SPA_MINDEVSIZE.  Unfortunately, we can't detect which
			 * device was the problem device since there's no
			 * reliable way to determine device size from userland.
			 */
			{
				char buf[64];

				zfs_nicenum(SPA_MINDEVSIZE, buf, sizeof (buf));

				zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
				    "one or more devices is less than the "
				    "minimum size (%s)"), buf);
			}
			return (zfs_error(hdl, EZFS_BADDEV, msg));

		case ENOSPC:
			zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
			    "one or more devices is out of space"));
			return (zfs_error(hdl, EZFS_BADDEV, msg));

		case ENOTBLK:
			zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
			    "cache device must be a disk or disk slice"));
			return (zfs_error(hdl, EZFS_BADDEV, msg));

		default:
			return (zpool_standard_error(hdl, errno, msg));
		}
	}

	/*
	 * If this is an alternate root pool, then we automatically set the
	 * mountpoint of the root dataset to be '/'.
	 */
	if (nvlist_lookup_string(props, zpool_prop_to_name(ZPOOL_PROP_ALTROOT),
	    &altroot) == 0) {
		zfs_handle_t *zhp;

		verify((zhp = zfs_open(hdl, pool, ZFS_TYPE_DATASET)) != NULL);
		verify(zfs_prop_set(zhp, zfs_prop_to_name(ZFS_PROP_MOUNTPOINT),
		    "/") == 0);

		zfs_close(zhp);
	}

create_failed:
	zcmd_free_nvlists(&zc);
	nvlist_free(zc_props);
	nvlist_free(zc_fsprops);
	return (ret);
}

/*
 * Destroy the given pool.  It is up to the caller to ensure that there are no
 * datasets left in the pool.
 */
int
zpool_destroy(zpool_handle_t *zhp)
{
	zfs_cmd_t zc = { 0 };
	zfs_handle_t *zfp = NULL;
	libzfs_handle_t *hdl = zhp->zpool_hdl;
	char msg[1024];

	if (zhp->zpool_state == POOL_STATE_ACTIVE &&
	    (zfp = zfs_open(zhp->zpool_hdl, zhp->zpool_name,
	    ZFS_TYPE_FILESYSTEM)) == NULL)
		return (-1);

	if (zpool_remove_zvol_links(zhp) != 0)
		return (-1);

	(void) strlcpy(zc.zc_name, zhp->zpool_name, sizeof (zc.zc_name));

	if (zfs_ioctl(zhp->zpool_hdl, ZFS_IOC_POOL_DESTROY, &zc) != 0) {
		(void) snprintf(msg, sizeof (msg), dgettext(TEXT_DOMAIN,
		    "cannot destroy '%s'"), zhp->zpool_name);

		if (errno == EROFS) {
			zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
			    "one or more devices is read only"));
			(void) zfs_error(hdl, EZFS_BADDEV, msg);
		} else {
			(void) zpool_standard_error(hdl, errno, msg);
		}

		if (zfp)
			zfs_close(zfp);
		return (-1);
	}

	if (zfp) {
		remove_mountpoint(zfp);
		zfs_close(zfp);
	}

	return (0);
}

/*
 * Add the given vdevs to the pool.  The caller must have already performed the
 * necessary verification to ensure that the vdev specification is well-formed.
 */
int
zpool_add(zpool_handle_t *zhp, nvlist_t *nvroot)
{
	zfs_cmd_t zc = { 0 };
	int ret;
	libzfs_handle_t *hdl = zhp->zpool_hdl;
	char msg[1024];
	nvlist_t **spares, **l2cache;
	uint_t nspares, nl2cache;

	(void) snprintf(msg, sizeof (msg), dgettext(TEXT_DOMAIN,
	    "cannot add to '%s'"), zhp->zpool_name);

	if (zpool_get_prop_int(zhp, ZPOOL_PROP_VERSION, NULL) <
	    SPA_VERSION_SPARES &&
	    nvlist_lookup_nvlist_array(nvroot, ZPOOL_CONFIG_SPARES,
	    &spares, &nspares) == 0) {
		zfs_error_aux(hdl, dgettext(TEXT_DOMAIN, "pool must be "
		    "upgraded to add hot spares"));
		return (zfs_error(hdl, EZFS_BADVERSION, msg));
	}

	if (pool_is_bootable(zhp) && nvlist_lookup_nvlist_array(nvroot,
	    ZPOOL_CONFIG_SPARES, &spares, &nspares) == 0) {
		uint64_t s;

		for (s = 0; s < nspares; s++) {
			char *path;

			if (nvlist_lookup_string(spares[s], ZPOOL_CONFIG_PATH,
			    &path) == 0 && pool_uses_efi(spares[s])) {
				zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
				    "device '%s' contains an EFI label and "
				    "cannot be used on root pools."),
				    zpool_vdev_name(hdl, NULL, spares[s]));
				return (zfs_error(hdl, EZFS_POOL_NOTSUP, msg));
			}
		}
	}

	if (zpool_get_prop_int(zhp, ZPOOL_PROP_VERSION, NULL) <
	    SPA_VERSION_L2CACHE &&
	    nvlist_lookup_nvlist_array(nvroot, ZPOOL_CONFIG_L2CACHE,
	    &l2cache, &nl2cache) == 0) {
		zfs_error_aux(hdl, dgettext(TEXT_DOMAIN, "pool must be "
		    "upgraded to add cache devices"));
		return (zfs_error(hdl, EZFS_BADVERSION, msg));
	}

	if (zcmd_write_conf_nvlist(hdl, &zc, nvroot) != 0)
		return (-1);
	(void) strlcpy(zc.zc_name, zhp->zpool_name, sizeof (zc.zc_name));

	if (zfs_ioctl(zhp->zpool_hdl, ZFS_IOC_VDEV_ADD, &zc) != 0) {
		switch (errno) {
		case EBUSY:
			/*
			 * This can happen if the user has specified the same
			 * device multiple times.  We can't reliably detect this
			 * until we try to add it and see we already have a
			 * label.
			 */
			zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
			    "one or more vdevs refer to the same device"));
			(void) zfs_error(hdl, EZFS_BADDEV, msg);
			break;

		case EOVERFLOW:
			/*
			 * This occurrs when one of the devices is below
			 * SPA_MINDEVSIZE.  Unfortunately, we can't detect which
			 * device was the problem device since there's no
			 * reliable way to determine device size from userland.
			 */
			{
				char buf[64];

				zfs_nicenum(SPA_MINDEVSIZE, buf, sizeof (buf));

				zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
				    "device is less than the minimum "
				    "size (%s)"), buf);
			}
			(void) zfs_error(hdl, EZFS_BADDEV, msg);
			break;

		case ENOTSUP:
			zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
			    "pool must be upgraded to add these vdevs"));
			(void) zfs_error(hdl, EZFS_BADVERSION, msg);
			break;

		case EDOM:
			zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
			    "root pool can not have multiple vdevs"
			    " or separate logs"));
			(void) zfs_error(hdl, EZFS_POOL_NOTSUP, msg);
			break;

		case ENOTBLK:
			zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
			    "cache device must be a disk or disk slice"));
			(void) zfs_error(hdl, EZFS_BADDEV, msg);
			break;

		default:
			(void) zpool_standard_error(hdl, errno, msg);
		}

		ret = -1;
	} else {
		ret = 0;
	}

	zcmd_free_nvlists(&zc);

	return (ret);
}

/*
 * Exports the pool from the system.  The caller must ensure that there are no
 * mounted datasets in the pool.
 */
int
zpool_export_common(zpool_handle_t *zhp, boolean_t force, boolean_t hardforce)
{
	zfs_cmd_t zc = { 0 };
	char msg[1024];

	if (zpool_remove_zvol_links(zhp) != 0)
		return (-1);

	(void) snprintf(msg, sizeof (msg), dgettext(TEXT_DOMAIN,
	    "cannot export '%s'"), zhp->zpool_name);

	(void) strlcpy(zc.zc_name, zhp->zpool_name, sizeof (zc.zc_name));
	zc.zc_cookie = force;
	zc.zc_guid = hardforce;

	if (zfs_ioctl(zhp->zpool_hdl, ZFS_IOC_POOL_EXPORT, &zc) != 0) {
		switch (errno) {
		case EXDEV:
			zfs_error_aux(zhp->zpool_hdl, dgettext(TEXT_DOMAIN,
			    "use '-f' to override the following errors:\n"
			    "'%s' has an active shared spare which could be"
			    " used by other pools once '%s' is exported."),
			    zhp->zpool_name, zhp->zpool_name);
			return (zfs_error(zhp->zpool_hdl, EZFS_ACTIVE_SPARE,
			    msg));
		default:
			return (zpool_standard_error_fmt(zhp->zpool_hdl, errno,
			    msg));
		}
	}

	return (0);
}

int
zpool_export(zpool_handle_t *zhp, boolean_t force)
{
	return (zpool_export_common(zhp, force, B_FALSE));
}

int
zpool_export_force(zpool_handle_t *zhp)
{
	return (zpool_export_common(zhp, B_TRUE, B_TRUE));
}

/*
 * zpool_import() is a contracted interface. Should be kept the same
 * if possible.
 *
 * Applications should use zpool_import_props() to import a pool with
 * new properties value to be set.
 */
int
zpool_import(libzfs_handle_t *hdl, nvlist_t *config, const char *newname,
    char *altroot)
{
	nvlist_t *props = NULL;
	int ret;

	if (altroot != NULL) {
		if (nvlist_alloc(&props, NV_UNIQUE_NAME, 0) != 0) {
			return (zfs_error_fmt(hdl, EZFS_NOMEM,
			    dgettext(TEXT_DOMAIN, "cannot import '%s'"),
			    newname));
		}

		if (nvlist_add_string(props,
		    zpool_prop_to_name(ZPOOL_PROP_ALTROOT), altroot) != 0 ||
		    nvlist_add_string(props,
		    zpool_prop_to_name(ZPOOL_PROP_CACHEFILE), "none") != 0) {
			nvlist_free(props);
			return (zfs_error_fmt(hdl, EZFS_NOMEM,
			    dgettext(TEXT_DOMAIN, "cannot import '%s'"),
			    newname));
		}
	}

	ret = zpool_import_props(hdl, config, newname, props, B_FALSE);
	if (props)
		nvlist_free(props);
	return (ret);
}

/*
 * Import the given pool using the known configuration and a list of
 * properties to be set. The configuration should have come from
 * zpool_find_import(). The 'newname' parameters control whether the pool
 * is imported with a different name.
 */
int
zpool_import_props(libzfs_handle_t *hdl, nvlist_t *config, const char *newname,
    nvlist_t *props, boolean_t importfaulted)
{
	zfs_cmd_t zc = { 0 };
	char *thename;
	char *origname;
	int ret;
	char errbuf[1024];

	verify(nvlist_lookup_string(config, ZPOOL_CONFIG_POOL_NAME,
	    &origname) == 0);

	(void) snprintf(errbuf, sizeof (errbuf), dgettext(TEXT_DOMAIN,
	    "cannot import pool '%s'"), origname);

	if (newname != NULL) {
		if (!zpool_name_valid(hdl, B_FALSE, newname))
			return (zfs_error_fmt(hdl, EZFS_INVALIDNAME,
			    dgettext(TEXT_DOMAIN, "cannot import '%s'"),
			    newname));
		thename = (char *)newname;
	} else {
		thename = origname;
	}

	if (props) {
		uint64_t version;

		verify(nvlist_lookup_uint64(config, ZPOOL_CONFIG_VERSION,
		    &version) == 0);

		if ((props = zpool_valid_proplist(hdl, origname,
		    props, version, B_TRUE, errbuf)) == NULL) {
			return (-1);
		} else if (zcmd_write_src_nvlist(hdl, &zc, props) != 0) {
			nvlist_free(props);
			return (-1);
		}
	}

	(void) strlcpy(zc.zc_name, thename, sizeof (zc.zc_name));

	verify(nvlist_lookup_uint64(config, ZPOOL_CONFIG_POOL_GUID,
	    &zc.zc_guid) == 0);

	if (zcmd_write_conf_nvlist(hdl, &zc, config) != 0) {
		nvlist_free(props);
		return (-1);
	}

	zc.zc_cookie = (uint64_t)importfaulted;
	ret = 0;
	if (zfs_ioctl(hdl, ZFS_IOC_POOL_IMPORT, &zc) != 0) {
		char desc[1024];
		if (newname == NULL)
			(void) snprintf(desc, sizeof (desc),
			    dgettext(TEXT_DOMAIN, "cannot import '%s'"),
			    thename);
		else
			(void) snprintf(desc, sizeof (desc),
			    dgettext(TEXT_DOMAIN, "cannot import '%s' as '%s'"),
			    origname, thename);

		switch (errno) {
		case ENOTSUP:
			/*
			 * Unsupported version.
			 */
			(void) zfs_error(hdl, EZFS_BADVERSION, desc);
			break;

		case EINVAL:
			(void) zfs_error(hdl, EZFS_INVALCONFIG, desc);
			break;

		default:
			(void) zpool_standard_error(hdl, errno, desc);
		}

		ret = -1;
	} else {
		zpool_handle_t *zhp;

		/*
		 * This should never fail, but play it safe anyway.
		 */
		if (zpool_open_silent(hdl, thename, &zhp) != 0) {
			ret = -1;
		} else if (zhp != NULL) {
			ret = zpool_create_zvol_links(zhp);
			zpool_close(zhp);
		}

	}

	zcmd_free_nvlists(&zc);
	nvlist_free(props);

	return (ret);
}

/*
 * Scrub the pool.
 */
int
zpool_scrub(zpool_handle_t *zhp, pool_scrub_type_t type)
{
	zfs_cmd_t zc = { 0 };
	char msg[1024];
	libzfs_handle_t *hdl = zhp->zpool_hdl;

	(void) strlcpy(zc.zc_name, zhp->zpool_name, sizeof (zc.zc_name));
	zc.zc_cookie = type;

	if (zfs_ioctl(zhp->zpool_hdl, ZFS_IOC_POOL_SCRUB, &zc) == 0)
		return (0);

	(void) snprintf(msg, sizeof (msg),
	    dgettext(TEXT_DOMAIN, "cannot scrub %s"), zc.zc_name);

	if (errno == EBUSY)
		return (zfs_error(hdl, EZFS_RESILVERING, msg));
	else
		return (zpool_standard_error(hdl, errno, msg));
}

/*
 * 'avail_spare' is set to TRUE if the provided guid refers to an AVAIL
 * spare; but FALSE if its an INUSE spare.
 */
static nvlist_t *
vdev_to_nvlist_iter(nvlist_t *nv, const char *search, uint64_t guid,
    boolean_t *avail_spare, boolean_t *l2cache, boolean_t *log)
{
	uint_t c, children;
	nvlist_t **child;
	uint64_t theguid, present;
	char *path;
	uint64_t wholedisk = 0;
	nvlist_t *ret;
	uint64_t is_log;

	verify(nvlist_lookup_uint64(nv, ZPOOL_CONFIG_GUID, &theguid) == 0);

	if (search == NULL &&
	    nvlist_lookup_uint64(nv, ZPOOL_CONFIG_NOT_PRESENT, &present) == 0) {
		/*
		 * If the device has never been present since import, the only
		 * reliable way to match the vdev is by GUID.
		 */
		if (theguid == guid)
			return (nv);
	} else if (search != NULL &&
	    nvlist_lookup_string(nv, ZPOOL_CONFIG_PATH, &path) == 0) {
		(void) nvlist_lookup_uint64(nv, ZPOOL_CONFIG_WHOLE_DISK,
		    &wholedisk);
		if (wholedisk) {
			/*
			 * For whole disks, the internal path has 's0', but the
			 * path passed in by the user doesn't.
			 */
			if (strlen(search) == strlen(path) - 2 &&
			    strncmp(search, path, strlen(search)) == 0)
				return (nv);
		} else if (strcmp(search, path) == 0) {
			return (nv);
		}
	}

	if (nvlist_lookup_nvlist_array(nv, ZPOOL_CONFIG_CHILDREN,
	    &child, &children) != 0)
		return (NULL);

	for (c = 0; c < children; c++) {
		if ((ret = vdev_to_nvlist_iter(child[c], search, guid,
		    avail_spare, l2cache, NULL)) != NULL) {
			/*
			 * The 'is_log' value is only set for the toplevel
			 * vdev, not the leaf vdevs.  So we always lookup the
			 * log device from the root of the vdev tree (where
			 * 'log' is non-NULL).
			 */
			if (log != NULL &&
			    nvlist_lookup_uint64(child[c],
			    ZPOOL_CONFIG_IS_LOG, &is_log) == 0 &&
			    is_log) {
				*log = B_TRUE;
			}
			return (ret);
		}
	}

	if (nvlist_lookup_nvlist_array(nv, ZPOOL_CONFIG_SPARES,
	    &child, &children) == 0) {
		for (c = 0; c < children; c++) {
			if ((ret = vdev_to_nvlist_iter(child[c], search, guid,
			    avail_spare, l2cache, NULL)) != NULL) {
				*avail_spare = B_TRUE;
				return (ret);
			}
		}
	}

	if (nvlist_lookup_nvlist_array(nv, ZPOOL_CONFIG_L2CACHE,
	    &child, &children) == 0) {
		for (c = 0; c < children; c++) {
			if ((ret = vdev_to_nvlist_iter(child[c], search, guid,
			    avail_spare, l2cache, NULL)) != NULL) {
				*l2cache = B_TRUE;
				return (ret);
			}
		}
	}

	return (NULL);
}

nvlist_t *
zpool_find_vdev(zpool_handle_t *zhp, const char *path, boolean_t *avail_spare,
    boolean_t *l2cache, boolean_t *log)
{
	char buf[MAXPATHLEN];
	const char *search;
	char *end;
	nvlist_t *nvroot;
	uint64_t guid;

	guid = strtoull(path, &end, 10);
	if (guid != 0 && *end == '\0') {
		search = NULL;
	} else if (path[0] != '/') {
		(void) snprintf(buf, sizeof (buf), "%s%s", "/dev/dsk/", path);
		search = buf;
	} else {
		search = path;
	}

	verify(nvlist_lookup_nvlist(zhp->zpool_config, ZPOOL_CONFIG_VDEV_TREE,
	    &nvroot) == 0);

	*avail_spare = B_FALSE;
	*l2cache = B_FALSE;
	if (log != NULL)
		*log = B_FALSE;
	return (vdev_to_nvlist_iter(nvroot, search, guid, avail_spare,
	    l2cache, log));
}

static int
vdev_online(nvlist_t *nv)
{
	uint64_t ival;

	if (nvlist_lookup_uint64(nv, ZPOOL_CONFIG_OFFLINE, &ival) == 0 ||
	    nvlist_lookup_uint64(nv, ZPOOL_CONFIG_FAULTED, &ival) == 0 ||
	    nvlist_lookup_uint64(nv, ZPOOL_CONFIG_REMOVED, &ival) == 0)
		return (0);

	return (1);
}

/*
 * Get phys_path for a root pool
 * Return 0 on success; non-zeron on failure.
 */
int
zpool_get_physpath(zpool_handle_t *zhp, char *physpath)
{
	nvlist_t *vdev_root;
	nvlist_t **child;
	uint_t count;
	int i;

	/*
	 * Make sure this is a root pool, as phys_path doesn't mean
	 * anything to a non-root pool.
	 */
	if (!pool_is_bootable(zhp))
		return (-1);

	verify(nvlist_lookup_nvlist(zhp->zpool_config,
	    ZPOOL_CONFIG_VDEV_TREE, &vdev_root) == 0);

	if (nvlist_lookup_nvlist_array(vdev_root, ZPOOL_CONFIG_CHILDREN,
	    &child, &count) != 0)
		return (-2);

	for (i = 0; i < count; i++) {
		nvlist_t **child2;
		uint_t count2;
		char *type;
		char *tmppath;
		int j;

		if (nvlist_lookup_string(child[i], ZPOOL_CONFIG_TYPE, &type)
		    != 0)
			return (-3);

		if (strcmp(type, VDEV_TYPE_DISK) == 0) {
			if (!vdev_online(child[i]))
				return (-8);
			verify(nvlist_lookup_string(child[i],
			    ZPOOL_CONFIG_PHYS_PATH, &tmppath) == 0);
			(void) strncpy(physpath, tmppath, strlen(tmppath));
		} else if (strcmp(type, VDEV_TYPE_MIRROR) == 0) {
			if (nvlist_lookup_nvlist_array(child[i],
			    ZPOOL_CONFIG_CHILDREN, &child2, &count2) != 0)
				return (-4);

			for (j = 0; j < count2; j++) {
				if (!vdev_online(child2[j]))
					return (-8);
				if (nvlist_lookup_string(child2[j],
				    ZPOOL_CONFIG_PHYS_PATH, &tmppath) != 0)
					return (-5);

				if ((strlen(physpath) + strlen(tmppath)) >
				    MAXNAMELEN)
					return (-6);

				if (strlen(physpath) == 0) {
					(void) strncpy(physpath, tmppath,
					    strlen(tmppath));
				} else {
					(void) strcat(physpath, " ");
					(void) strcat(physpath, tmppath);
				}
			}
		} else {
			return (-7);
		}
	}

	return (0);
}

/*
 * Returns TRUE if the given guid corresponds to the given type.
 * This is used to check for hot spares (INUSE or not), and level 2 cache
 * devices.
 */
static boolean_t
is_guid_type(zpool_handle_t *zhp, uint64_t guid, const char *type)
{
	uint64_t target_guid;
	nvlist_t *nvroot;
	nvlist_t **list;
	uint_t count;
	int i;

	verify(nvlist_lookup_nvlist(zhp->zpool_config, ZPOOL_CONFIG_VDEV_TREE,
	    &nvroot) == 0);
	if (nvlist_lookup_nvlist_array(nvroot, type, &list, &count) == 0) {
		for (i = 0; i < count; i++) {
			verify(nvlist_lookup_uint64(list[i], ZPOOL_CONFIG_GUID,
			    &target_guid) == 0);
			if (guid == target_guid)
				return (B_TRUE);
		}
	}

	return (B_FALSE);
}

/*
 * Bring the specified vdev online.   The 'flags' parameter is a set of the
 * ZFS_ONLINE_* flags.
 */
int
zpool_vdev_online(zpool_handle_t *zhp, const char *path, int flags,
    vdev_state_t *newstate)
{
	zfs_cmd_t zc = { 0 };
	char msg[1024];
	nvlist_t *tgt;
	boolean_t avail_spare, l2cache;
	libzfs_handle_t *hdl = zhp->zpool_hdl;

	(void) snprintf(msg, sizeof (msg),
	    dgettext(TEXT_DOMAIN, "cannot online %s"), path);

	(void) strlcpy(zc.zc_name, zhp->zpool_name, sizeof (zc.zc_name));
	if ((tgt = zpool_find_vdev(zhp, path, &avail_spare, &l2cache,
	    NULL)) == NULL)
		return (zfs_error(hdl, EZFS_NODEVICE, msg));

	verify(nvlist_lookup_uint64(tgt, ZPOOL_CONFIG_GUID, &zc.zc_guid) == 0);

	if (avail_spare ||
	    is_guid_type(zhp, zc.zc_guid, ZPOOL_CONFIG_SPARES) == B_TRUE)
		return (zfs_error(hdl, EZFS_ISSPARE, msg));

	zc.zc_cookie = VDEV_STATE_ONLINE;
	zc.zc_obj = flags;

	if (zfs_ioctl(zhp->zpool_hdl, ZFS_IOC_VDEV_SET_STATE, &zc) != 0)
		return (zpool_standard_error(hdl, errno, msg));

	*newstate = zc.zc_cookie;
	return (0);
}

/*
 * Take the specified vdev offline
 */
int
zpool_vdev_offline(zpool_handle_t *zhp, const char *path, boolean_t istmp)
{
	zfs_cmd_t zc = { 0 };
	char msg[1024];
	nvlist_t *tgt;
	boolean_t avail_spare, l2cache;
	libzfs_handle_t *hdl = zhp->zpool_hdl;

	(void) snprintf(msg, sizeof (msg),
	    dgettext(TEXT_DOMAIN, "cannot offline %s"), path);

	(void) strlcpy(zc.zc_name, zhp->zpool_name, sizeof (zc.zc_name));
	if ((tgt = zpool_find_vdev(zhp, path, &avail_spare, &l2cache,
	    NULL)) == NULL)
		return (zfs_error(hdl, EZFS_NODEVICE, msg));

	verify(nvlist_lookup_uint64(tgt, ZPOOL_CONFIG_GUID, &zc.zc_guid) == 0);

	if (avail_spare ||
	    is_guid_type(zhp, zc.zc_guid, ZPOOL_CONFIG_SPARES) == B_TRUE)
		return (zfs_error(hdl, EZFS_ISSPARE, msg));

	zc.zc_cookie = VDEV_STATE_OFFLINE;
	zc.zc_obj = istmp ? ZFS_OFFLINE_TEMPORARY : 0;

	if (zfs_ioctl(zhp->zpool_hdl, ZFS_IOC_VDEV_SET_STATE, &zc) == 0)
		return (0);

	switch (errno) {
	case EBUSY:

		/*
		 * There are no other replicas of this device.
		 */
		return (zfs_error(hdl, EZFS_NOREPLICAS, msg));

	default:
		return (zpool_standard_error(hdl, errno, msg));
	}
}

/*
 * Mark the given vdev faulted.
 */
int
zpool_vdev_fault(zpool_handle_t *zhp, uint64_t guid)
{
	zfs_cmd_t zc = { 0 };
	char msg[1024];
	libzfs_handle_t *hdl = zhp->zpool_hdl;

	(void) snprintf(msg, sizeof (msg),
	    dgettext(TEXT_DOMAIN, "cannot fault %llu"), guid);

	(void) strlcpy(zc.zc_name, zhp->zpool_name, sizeof (zc.zc_name));
	zc.zc_guid = guid;
	zc.zc_cookie = VDEV_STATE_FAULTED;

	if (ioctl(zhp->zpool_hdl->libzfs_fd, ZFS_IOC_VDEV_SET_STATE, &zc) == 0)
		return (0);

	switch (errno) {
	case EBUSY:

		/*
		 * There are no other replicas of this device.
		 */
		return (zfs_error(hdl, EZFS_NOREPLICAS, msg));

	default:
		return (zpool_standard_error(hdl, errno, msg));
	}

}

/*
 * Mark the given vdev degraded.
 */
int
zpool_vdev_degrade(zpool_handle_t *zhp, uint64_t guid)
{
	zfs_cmd_t zc = { 0 };
	char msg[1024];
	libzfs_handle_t *hdl = zhp->zpool_hdl;

	(void) snprintf(msg, sizeof (msg),
	    dgettext(TEXT_DOMAIN, "cannot degrade %llu"), guid);

	(void) strlcpy(zc.zc_name, zhp->zpool_name, sizeof (zc.zc_name));
	zc.zc_guid = guid;
	zc.zc_cookie = VDEV_STATE_DEGRADED;

	if (ioctl(zhp->zpool_hdl->libzfs_fd, ZFS_IOC_VDEV_SET_STATE, &zc) == 0)
		return (0);

	return (zpool_standard_error(hdl, errno, msg));
}

/*
 * Returns TRUE if the given nvlist is a vdev that was originally swapped in as
 * a hot spare.
 */
static boolean_t
is_replacing_spare(nvlist_t *search, nvlist_t *tgt, int which)
{
	nvlist_t **child;
	uint_t c, children;
	char *type;

	if (nvlist_lookup_nvlist_array(search, ZPOOL_CONFIG_CHILDREN, &child,
	    &children) == 0) {
		verify(nvlist_lookup_string(search, ZPOOL_CONFIG_TYPE,
		    &type) == 0);

		if (strcmp(type, VDEV_TYPE_SPARE) == 0 &&
		    children == 2 && child[which] == tgt)
			return (B_TRUE);

		for (c = 0; c < children; c++)
			if (is_replacing_spare(child[c], tgt, which))
				return (B_TRUE);
	}

	return (B_FALSE);
}

/*
 * Attach new_disk (fully described by nvroot) to old_disk.
 * If 'replacing' is specified, the new disk will replace the old one.
 */
int
zpool_vdev_attach(zpool_handle_t *zhp,
    const char *old_disk, const char *new_disk, nvlist_t *nvroot, int replacing)
{
	zfs_cmd_t zc = { 0 };
	char msg[1024];
	int ret;
	nvlist_t *tgt;
	boolean_t avail_spare, l2cache, islog;
	uint64_t val;
	char *path, *newname;
	nvlist_t **child;
	uint_t children;
	nvlist_t *config_root;
	libzfs_handle_t *hdl = zhp->zpool_hdl;
	boolean_t rootpool = pool_is_bootable(zhp);

	if (replacing)
		(void) snprintf(msg, sizeof (msg), dgettext(TEXT_DOMAIN,
		    "cannot replace %s with %s"), old_disk, new_disk);
	else
		(void) snprintf(msg, sizeof (msg), dgettext(TEXT_DOMAIN,
		    "cannot attach %s to %s"), new_disk, old_disk);

	/*
	 * If this is a root pool, make sure that we're not attaching an
	 * EFI labeled device.
	 */
	if (rootpool && pool_uses_efi(nvroot)) {
		zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
		    "EFI labeled devices are not supported on root pools."));
		return (zfs_error(hdl, EZFS_POOL_NOTSUP, msg));
	}

	(void) strlcpy(zc.zc_name, zhp->zpool_name, sizeof (zc.zc_name));
	if ((tgt = zpool_find_vdev(zhp, old_disk, &avail_spare, &l2cache,
	    &islog)) == 0)
		return (zfs_error(hdl, EZFS_NODEVICE, msg));

	if (avail_spare)
		return (zfs_error(hdl, EZFS_ISSPARE, msg));

	if (l2cache)
		return (zfs_error(hdl, EZFS_ISL2CACHE, msg));

	verify(nvlist_lookup_uint64(tgt, ZPOOL_CONFIG_GUID, &zc.zc_guid) == 0);
	zc.zc_cookie = replacing;

	if (nvlist_lookup_nvlist_array(nvroot, ZPOOL_CONFIG_CHILDREN,
	    &child, &children) != 0 || children != 1) {
		zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
		    "new device must be a single disk"));
		return (zfs_error(hdl, EZFS_INVALCONFIG, msg));
	}

	verify(nvlist_lookup_nvlist(zpool_get_config(zhp, NULL),
	    ZPOOL_CONFIG_VDEV_TREE, &config_root) == 0);

	if ((newname = zpool_vdev_name(NULL, NULL, child[0])) == NULL)
		return (-1);

	/*
	 * If the target is a hot spare that has been swapped in, we can only
	 * replace it with another hot spare.
	 */
	if (replacing &&
	    nvlist_lookup_uint64(tgt, ZPOOL_CONFIG_IS_SPARE, &val) == 0 &&
	    (zpool_find_vdev(zhp, newname, &avail_spare, &l2cache,
	    NULL) == NULL || !avail_spare) &&
	    is_replacing_spare(config_root, tgt, 1)) {
		zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
		    "can only be replaced by another hot spare"));
		free(newname);
		return (zfs_error(hdl, EZFS_BADTARGET, msg));
	}

	/*
	 * If we are attempting to replace a spare, it canot be applied to an
	 * already spared device.
	 */
	if (replacing &&
	    nvlist_lookup_string(child[0], ZPOOL_CONFIG_PATH, &path) == 0 &&
	    zpool_find_vdev(zhp, newname, &avail_spare,
	    &l2cache, NULL) != NULL && avail_spare &&
	    is_replacing_spare(config_root, tgt, 0)) {
		zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
		    "device has already been replaced with a spare"));
		free(newname);
		return (zfs_error(hdl, EZFS_BADTARGET, msg));
	}

	free(newname);

	if (zcmd_write_conf_nvlist(hdl, &zc, nvroot) != 0)
		return (-1);

	ret = zfs_ioctl(zhp->zpool_hdl, ZFS_IOC_VDEV_ATTACH, &zc);

	zcmd_free_nvlists(&zc);

	if (ret == 0) {
		if (rootpool) {
			/*
			 * XXX - This should be removed once we can
			 * automatically install the bootblocks on the
			 * newly attached disk.
			 */
			(void) fprintf(stderr, dgettext(TEXT_DOMAIN, "Please "
			    "be sure to invoke %s to make '%s' bootable.\n"),
			    BOOTCMD, new_disk);
		}
		return (0);
	}

	switch (errno) {
	case ENOTSUP:
		/*
		 * Can't attach to or replace this type of vdev.
		 */
		if (replacing) {
			if (islog)
				zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
				    "cannot replace a log with a spare"));
			else
				zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
				    "cannot replace a replacing device"));
		} else {
			zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
			    "can only attach to mirrors and top-level "
			    "disks"));
		}
		(void) zfs_error(hdl, EZFS_BADTARGET, msg);
		break;

	case EINVAL:
		/*
		 * The new device must be a single disk.
		 */
		zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
		    "new device must be a single disk"));
		(void) zfs_error(hdl, EZFS_INVALCONFIG, msg);
		break;

	case EBUSY:
		zfs_error_aux(hdl, dgettext(TEXT_DOMAIN, "%s is busy"),
		    new_disk);
		(void) zfs_error(hdl, EZFS_BADDEV, msg);
		break;

	case EOVERFLOW:
		/*
		 * The new device is too small.
		 */
		zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
		    "device is too small"));
		(void) zfs_error(hdl, EZFS_BADDEV, msg);
		break;

	case EDOM:
		/*
		 * The new device has a different alignment requirement.
		 */
		zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
		    "devices have different sector alignment"));
		(void) zfs_error(hdl, EZFS_BADDEV, msg);
		break;

	case ENAMETOOLONG:
		/*
		 * The resulting top-level vdev spec won't fit in the label.
		 */
		(void) zfs_error(hdl, EZFS_DEVOVERFLOW, msg);
		break;

	default:
		(void) zpool_standard_error(hdl, errno, msg);
	}

	return (-1);
}

/*
 * Detach the specified device.
 */
int
zpool_vdev_detach(zpool_handle_t *zhp, const char *path)
{
	zfs_cmd_t zc = { 0 };
	char msg[1024];
	nvlist_t *tgt;
	boolean_t avail_spare, l2cache;
	libzfs_handle_t *hdl = zhp->zpool_hdl;

	(void) snprintf(msg, sizeof (msg),
	    dgettext(TEXT_DOMAIN, "cannot detach %s"), path);

	(void) strlcpy(zc.zc_name, zhp->zpool_name, sizeof (zc.zc_name));
	if ((tgt = zpool_find_vdev(zhp, path, &avail_spare, &l2cache,
	    NULL)) == 0)
		return (zfs_error(hdl, EZFS_NODEVICE, msg));

	if (avail_spare)
		return (zfs_error(hdl, EZFS_ISSPARE, msg));

	if (l2cache)
		return (zfs_error(hdl, EZFS_ISL2CACHE, msg));

	verify(nvlist_lookup_uint64(tgt, ZPOOL_CONFIG_GUID, &zc.zc_guid) == 0);

	if (zfs_ioctl(hdl, ZFS_IOC_VDEV_DETACH, &zc) == 0)
		return (0);

	switch (errno) {

	case ENOTSUP:
		/*
		 * Can't detach from this type of vdev.
		 */
		zfs_error_aux(hdl, dgettext(TEXT_DOMAIN, "only "
		    "applicable to mirror and replacing vdevs"));
		(void) zfs_error(zhp->zpool_hdl, EZFS_BADTARGET, msg);
		break;

	case EBUSY:
		/*
		 * There are no other replicas of this device.
		 */
		(void) zfs_error(hdl, EZFS_NOREPLICAS, msg);
		break;

	default:
		(void) zpool_standard_error(hdl, errno, msg);
	}

	return (-1);
}

/*
 * Remove the given device.  Currently, this is supported only for hot spares
 * and level 2 cache devices.
 */
int
zpool_vdev_remove(zpool_handle_t *zhp, const char *path)
{
	zfs_cmd_t zc = { 0 };
	char msg[1024];
	nvlist_t *tgt;
	boolean_t avail_spare, l2cache;
	libzfs_handle_t *hdl = zhp->zpool_hdl;

	(void) snprintf(msg, sizeof (msg),
	    dgettext(TEXT_DOMAIN, "cannot remove %s"), path);

	(void) strlcpy(zc.zc_name, zhp->zpool_name, sizeof (zc.zc_name));
	if ((tgt = zpool_find_vdev(zhp, path, &avail_spare, &l2cache,
	    NULL)) == 0)
		return (zfs_error(hdl, EZFS_NODEVICE, msg));

	if (!avail_spare && !l2cache) {
		zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
		    "only inactive hot spares or cache devices "
		    "can be removed"));
		return (zfs_error(hdl, EZFS_NODEVICE, msg));
	}

	verify(nvlist_lookup_uint64(tgt, ZPOOL_CONFIG_GUID, &zc.zc_guid) == 0);

	if (zfs_ioctl(hdl, ZFS_IOC_VDEV_REMOVE, &zc) == 0)
		return (0);

	return (zpool_standard_error(hdl, errno, msg));
}

/*
 * Clear the errors for the pool, or the particular device if specified.
 */
int
zpool_clear(zpool_handle_t *zhp, const char *path)
{
	zfs_cmd_t zc = { 0 };
	char msg[1024];
	nvlist_t *tgt;
	boolean_t avail_spare, l2cache;
	libzfs_handle_t *hdl = zhp->zpool_hdl;

	if (path)
		(void) snprintf(msg, sizeof (msg),
		    dgettext(TEXT_DOMAIN, "cannot clear errors for %s"),
		    path);
	else
		(void) snprintf(msg, sizeof (msg),
		    dgettext(TEXT_DOMAIN, "cannot clear errors for %s"),
		    zhp->zpool_name);

	(void) strlcpy(zc.zc_name, zhp->zpool_name, sizeof (zc.zc_name));
	if (path) {
		if ((tgt = zpool_find_vdev(zhp, path, &avail_spare,
		    &l2cache, NULL)) == 0)
			return (zfs_error(hdl, EZFS_NODEVICE, msg));

		/*
		 * Don't allow error clearing for hot spares.  Do allow
		 * error clearing for l2cache devices.
		 */
		if (avail_spare)
			return (zfs_error(hdl, EZFS_ISSPARE, msg));

		verify(nvlist_lookup_uint64(tgt, ZPOOL_CONFIG_GUID,
		    &zc.zc_guid) == 0);
	}

	if (zfs_ioctl(hdl, ZFS_IOC_CLEAR, &zc) == 0)
		return (0);

	return (zpool_standard_error(hdl, errno, msg));
}

/*
 * Similar to zpool_clear(), but takes a GUID (used by fmd).
 */
int
zpool_vdev_clear(zpool_handle_t *zhp, uint64_t guid)
{
	zfs_cmd_t zc = { 0 };
	char msg[1024];
	libzfs_handle_t *hdl = zhp->zpool_hdl;

	(void) snprintf(msg, sizeof (msg),
	    dgettext(TEXT_DOMAIN, "cannot clear errors for %llx"),
	    guid);

	(void) strlcpy(zc.zc_name, zhp->zpool_name, sizeof (zc.zc_name));
	zc.zc_guid = guid;

	if (ioctl(hdl->libzfs_fd, ZFS_IOC_CLEAR, &zc) == 0)
		return (0);

	return (zpool_standard_error(hdl, errno, msg));
}

/*
 * Iterate over all zvols in a given pool by walking the /dev/zvol/dsk/<pool>
 * hierarchy.
 */
int
zpool_iter_zvol(zpool_handle_t *zhp, int (*cb)(const char *, void *),
    void *data)
{
	libzfs_handle_t *hdl = zhp->zpool_hdl;
	char (*paths)[MAXPATHLEN];
	size_t size = 4;
	int curr, fd, base, ret = 0;
	DIR *dirp;
	struct dirent *dp;
	struct stat st;

	if ((base = open("/dev/zvol/dsk", O_RDONLY)) < 0)
		return (errno == ENOENT ? 0 : -1);

	if (fstatat(base, zhp->zpool_name, &st, 0) != 0) {
		int err = errno;
		(void) close(base);
		return (err == ENOENT ? 0 : -1);
	}

	/*
	 * Oddly this wasn't a directory -- ignore that failure since we
	 * know there are no links lower in the (non-existant) hierarchy.
	 */
	if (!S_ISDIR(st.st_mode)) {
		(void) close(base);
		return (0);
	}

	if ((paths = zfs_alloc(hdl, size * sizeof (paths[0]))) == NULL) {
		(void) close(base);
		return (-1);
	}

	(void) strlcpy(paths[0], zhp->zpool_name, sizeof (paths[0]));
	curr = 0;

	while (curr >= 0) {
		if (fstatat(base, paths[curr], &st, AT_SYMLINK_NOFOLLOW) != 0)
			goto err;

		if (S_ISDIR(st.st_mode)) {
			if ((fd = openat(base, paths[curr], O_RDONLY)) < 0)
				goto err;

			if ((dirp = fdopendir(fd)) == NULL) {
				(void) close(fd);
				goto err;
			}

			while ((dp = readdir(dirp)) != NULL) {
				if (dp->d_name[0] == '.')
					continue;

				if (curr + 1 == size) {
					paths = zfs_realloc(hdl, paths,
					    size * sizeof (paths[0]),
					    size * 2 * sizeof (paths[0]));
					if (paths == NULL) {
						(void) closedir(dirp);
						(void) close(fd);
						goto err;
					}

					size *= 2;
				}

				(void) strlcpy(paths[curr + 1], paths[curr],
				    sizeof (paths[curr + 1]));
				(void) strlcat(paths[curr], "/",
				    sizeof (paths[curr]));
				(void) strlcat(paths[curr], dp->d_name,
				    sizeof (paths[curr]));
				curr++;
			}

			(void) closedir(dirp);

		} else {
			if ((ret = cb(paths[curr], data)) != 0)
				break;
		}

		curr--;
	}

	free(paths);
	(void) close(base);

	return (ret);

err:
	free(paths);
	(void) close(base);
	return (-1);
}

typedef struct zvol_cb {
	zpool_handle_t *zcb_pool;
	boolean_t zcb_create;
} zvol_cb_t;

/*ARGSUSED*/
static int
do_zvol_create(zfs_handle_t *zhp, void *data)
{
	int ret = 0;

	if (ZFS_IS_VOLUME(zhp)) {
		(void) zvol_create_link(zhp->zfs_hdl, zhp->zfs_name);
		ret = zfs_iter_snapshots(zhp, do_zvol_create, NULL);
	}

	if (ret == 0)
		ret = zfs_iter_filesystems(zhp, do_zvol_create, NULL);

	zfs_close(zhp);

	return (ret);
}

/*
 * Iterate over all zvols in the pool and make any necessary minor nodes.
 */
int
zpool_create_zvol_links(zpool_handle_t *zhp)
{
	zfs_handle_t *zfp;
	int ret;

	/*
	 * If the pool is unavailable, just return success.
	 */
	if ((zfp = make_dataset_handle(zhp->zpool_hdl,
	    zhp->zpool_name)) == NULL)
		return (0);

	ret = zfs_iter_filesystems(zfp, do_zvol_create, NULL);

	zfs_close(zfp);
	return (ret);
}

static int
do_zvol_remove(const char *dataset, void *data)
{
	zpool_handle_t *zhp = data;

	return (zvol_remove_link(zhp->zpool_hdl, dataset));
}

/*
 * Iterate over all zvols in the pool and remove any minor nodes.  We iterate
 * by examining the /dev links so that a corrupted pool doesn't impede this
 * operation.
 */
int
zpool_remove_zvol_links(zpool_handle_t *zhp)
{
	return (zpool_iter_zvol(zhp, do_zvol_remove, zhp));
}

/*
 * Convert from a devid string to a path.
 */
static char *
devid_to_path(char *devid_str)
{
	ddi_devid_t devid;
	char *minor;
	char *path;
	devid_nmlist_t *list = NULL;
	int ret;

	if (devid_str_decode(devid_str, &devid, &minor) != 0)
		return (NULL);

	ret = devid_deviceid_to_nmlist("/dev", devid, minor, &list);

	devid_str_free(minor);
	devid_free(devid);

	if (ret != 0)
		return (NULL);

	if ((path = strdup(list[0].devname)) == NULL)
		return (NULL);

	devid_free_nmlist(list);

	return (path);
}

/*
 * Convert from a path to a devid string.
 */
static char *
path_to_devid(const char *path)
{
	int fd;
	ddi_devid_t devid;
	char *minor, *ret;

	if ((fd = open(path, O_RDONLY)) < 0)
		return (NULL);

	minor = NULL;
	ret = NULL;
	if (devid_get(fd, &devid) == 0) {
		if (devid_get_minor_name(fd, &minor) == 0)
			ret = devid_str_encode(devid, minor);
		if (minor != NULL)
			devid_str_free(minor);
		devid_free(devid);
	}
	(void) close(fd);

	return (ret);
}

/*
 * Issue the necessary ioctl() to update the stored path value for the vdev.  We
 * ignore any failure here, since a common case is for an unprivileged user to
 * type 'zpool status', and we'll display the correct information anyway.
 */
static void
set_path(zpool_handle_t *zhp, nvlist_t *nv, const char *path)
{
	zfs_cmd_t zc = { 0 };

	(void) strncpy(zc.zc_name, zhp->zpool_name, sizeof (zc.zc_name));
	(void) strncpy(zc.zc_value, path, sizeof (zc.zc_value));
	verify(nvlist_lookup_uint64(nv, ZPOOL_CONFIG_GUID,
	    &zc.zc_guid) == 0);

	(void) ioctl(zhp->zpool_hdl->libzfs_fd, ZFS_IOC_VDEV_SETPATH, &zc);
}

/*
 * Given a vdev, return the name to display in iostat.  If the vdev has a path,
 * we use that, stripping off any leading "/dev/dsk/"; if not, we use the type.
 * We also check if this is a whole disk, in which case we strip off the
 * trailing 's0' slice name.
 *
 * This routine is also responsible for identifying when disks have been
 * reconfigured in a new location.  The kernel will have opened the device by
 * devid, but the path will still refer to the old location.  To catch this, we
 * first do a path -> devid translation (which is fast for the common case).  If
 * the devid matches, we're done.  If not, we do a reverse devid -> path
 * translation and issue the appropriate ioctl() to update the path of the vdev.
 * If 'zhp' is NULL, then this is an exported pool, and we don't need to do any
 * of these checks.
 */
char *
zpool_vdev_name(libzfs_handle_t *hdl, zpool_handle_t *zhp, nvlist_t *nv)
{
	char *path, *devid;
	uint64_t value;
	char buf[64];
	vdev_stat_t *vs;
	uint_t vsc;

	if (nvlist_lookup_uint64(nv, ZPOOL_CONFIG_NOT_PRESENT,
	    &value) == 0) {
		verify(nvlist_lookup_uint64(nv, ZPOOL_CONFIG_GUID,
		    &value) == 0);
		(void) snprintf(buf, sizeof (buf), "%llu",
		    (u_longlong_t)value);
		path = buf;
	} else if (nvlist_lookup_string(nv, ZPOOL_CONFIG_PATH, &path) == 0) {

		/*
		 * If the device is dead (faulted, offline, etc) then don't
		 * bother opening it.  Otherwise we may be forcing the user to
		 * open a misbehaving device, which can have undesirable
		 * effects.
		 */
		if ((nvlist_lookup_uint64_array(nv, ZPOOL_CONFIG_STATS,
		    (uint64_t **)&vs, &vsc) != 0 ||
		    vs->vs_state >= VDEV_STATE_DEGRADED) &&
		    zhp != NULL &&
		    nvlist_lookup_string(nv, ZPOOL_CONFIG_DEVID, &devid) == 0) {
			/*
			 * Determine if the current path is correct.
			 */
			char *newdevid = path_to_devid(path);

			if (newdevid == NULL ||
			    strcmp(devid, newdevid) != 0) {
				char *newpath;

				if ((newpath = devid_to_path(devid)) != NULL) {
					/*
					 * Update the path appropriately.
					 */
					set_path(zhp, nv, newpath);
					if (nvlist_add_string(nv,
					    ZPOOL_CONFIG_PATH, newpath) == 0)
						verify(nvlist_lookup_string(nv,
						    ZPOOL_CONFIG_PATH,
						    &path) == 0);
					free(newpath);
				}
			}

			if (newdevid)
				devid_str_free(newdevid);
		}

		if (strncmp(path, "/dev/dsk/", 9) == 0)
			path += 9;

		if (nvlist_lookup_uint64(nv, ZPOOL_CONFIG_WHOLE_DISK,
		    &value) == 0 && value) {
			char *tmp = zfs_strdup(hdl, path);
			if (tmp == NULL)
				return (NULL);
			tmp[strlen(path) - 2] = '\0';
			return (tmp);
		}
	} else {
		verify(nvlist_lookup_string(nv, ZPOOL_CONFIG_TYPE, &path) == 0);

		/*
		 * If it's a raidz device, we need to stick in the parity level.
		 */
		if (strcmp(path, VDEV_TYPE_RAIDZ) == 0) {
			verify(nvlist_lookup_uint64(nv, ZPOOL_CONFIG_NPARITY,
			    &value) == 0);
			(void) snprintf(buf, sizeof (buf), "%s%llu", path,
			    (u_longlong_t)value);
			path = buf;
		}
	}

	return (zfs_strdup(hdl, path));
}

static int
zbookmark_compare(const void *a, const void *b)
{
	return (memcmp(a, b, sizeof (zbookmark_t)));
}

/*
 * Retrieve the persistent error log, uniquify the members, and return to the
 * caller.
 */
int
zpool_get_errlog(zpool_handle_t *zhp, nvlist_t **nverrlistp)
{
	zfs_cmd_t zc = { 0 };
	uint64_t count;
	zbookmark_t *zb = NULL;
	int i;

	/*
	 * Retrieve the raw error list from the kernel.  If the number of errors
	 * has increased, allocate more space and continue until we get the
	 * entire list.
	 */
	verify(nvlist_lookup_uint64(zhp->zpool_config, ZPOOL_CONFIG_ERRCOUNT,
	    &count) == 0);
	if (count == 0)
		return (0);
	if ((zc.zc_nvlist_dst = (uintptr_t)zfs_alloc(zhp->zpool_hdl,
	    count * sizeof (zbookmark_t))) == (uintptr_t)NULL)
		return (-1);
	zc.zc_nvlist_dst_size = count;
	(void) strcpy(zc.zc_name, zhp->zpool_name);
	for (;;) {
		if (ioctl(zhp->zpool_hdl->libzfs_fd, ZFS_IOC_ERROR_LOG,
		    &zc) != 0) {
			free((void *)(uintptr_t)zc.zc_nvlist_dst);
			if (errno == ENOMEM) {
				count = zc.zc_nvlist_dst_size;
				if ((zc.zc_nvlist_dst = (uintptr_t)
				    zfs_alloc(zhp->zpool_hdl, count *
				    sizeof (zbookmark_t))) == (uintptr_t)NULL)
					return (-1);
			} else {
				return (-1);
			}
		} else {
			break;
		}
	}

	/*
	 * Sort the resulting bookmarks.  This is a little confusing due to the
	 * implementation of ZFS_IOC_ERROR_LOG.  The bookmarks are copied last
	 * to first, and 'zc_nvlist_dst_size' indicates the number of boomarks
	 * _not_ copied as part of the process.  So we point the start of our
	 * array appropriate and decrement the total number of elements.
	 */
	zb = ((zbookmark_t *)(uintptr_t)zc.zc_nvlist_dst) +
	    zc.zc_nvlist_dst_size;
	count -= zc.zc_nvlist_dst_size;

	qsort(zb, count, sizeof (zbookmark_t), zbookmark_compare);

	verify(nvlist_alloc(nverrlistp, 0, KM_SLEEP) == 0);

	/*
	 * Fill in the nverrlistp with nvlist's of dataset and object numbers.
	 */
	for (i = 0; i < count; i++) {
		nvlist_t *nv;

		/* ignoring zb_blkid and zb_level for now */
		if (i > 0 && zb[i-1].zb_objset == zb[i].zb_objset &&
		    zb[i-1].zb_object == zb[i].zb_object)
			continue;

		if (nvlist_alloc(&nv, NV_UNIQUE_NAME, KM_SLEEP) != 0)
			goto nomem;
		if (nvlist_add_uint64(nv, ZPOOL_ERR_DATASET,
		    zb[i].zb_objset) != 0) {
			nvlist_free(nv);
			goto nomem;
		}
		if (nvlist_add_uint64(nv, ZPOOL_ERR_OBJECT,
		    zb[i].zb_object) != 0) {
			nvlist_free(nv);
			goto nomem;
		}
		if (nvlist_add_nvlist(*nverrlistp, "ejk", nv) != 0) {
			nvlist_free(nv);
			goto nomem;
		}
		nvlist_free(nv);
	}

	free((void *)(uintptr_t)zc.zc_nvlist_dst);
	return (0);

nomem:
	free((void *)(uintptr_t)zc.zc_nvlist_dst);
	return (no_memory(zhp->zpool_hdl));
}

/*
 * Upgrade a ZFS pool to the latest on-disk version.
 */
int
zpool_upgrade(zpool_handle_t *zhp, uint64_t new_version)
{
	zfs_cmd_t zc = { 0 };
	libzfs_handle_t *hdl = zhp->zpool_hdl;

	(void) strcpy(zc.zc_name, zhp->zpool_name);
	zc.zc_cookie = new_version;

	if (zfs_ioctl(hdl, ZFS_IOC_POOL_UPGRADE, &zc) != 0)
		return (zpool_standard_error_fmt(hdl, errno,
		    dgettext(TEXT_DOMAIN, "cannot upgrade '%s'"),
		    zhp->zpool_name));
	return (0);
}

void
zpool_set_history_str(const char *subcommand, int argc, char **argv,
    char *history_str)
{
	int i;

	(void) strlcpy(history_str, subcommand, HIS_MAX_RECORD_LEN);
	for (i = 1; i < argc; i++) {
		if (strlen(history_str) + 1 + strlen(argv[i]) >
		    HIS_MAX_RECORD_LEN)
			break;
		(void) strlcat(history_str, " ", HIS_MAX_RECORD_LEN);
		(void) strlcat(history_str, argv[i], HIS_MAX_RECORD_LEN);
	}
}

/*
 * Stage command history for logging.
 */
int
zpool_stage_history(libzfs_handle_t *hdl, const char *history_str)
{
	if (history_str == NULL)
		return (EINVAL);

	if (strlen(history_str) > HIS_MAX_RECORD_LEN)
		return (EINVAL);

	if (hdl->libzfs_log_str != NULL)
		free(hdl->libzfs_log_str);

	if ((hdl->libzfs_log_str = strdup(history_str)) == NULL)
		return (no_memory(hdl));

	return (0);
}

/*
 * Perform ioctl to get some command history of a pool.
 *
 * 'buf' is the buffer to fill up to 'len' bytes.  'off' is the
 * logical offset of the history buffer to start reading from.
 *
 * Upon return, 'off' is the next logical offset to read from and
 * 'len' is the actual amount of bytes read into 'buf'.
 */
static int
get_history(zpool_handle_t *zhp, char *buf, uint64_t *off, uint64_t *len)
{
	zfs_cmd_t zc = { 0 };
	libzfs_handle_t *hdl = zhp->zpool_hdl;

	(void) strlcpy(zc.zc_name, zhp->zpool_name, sizeof (zc.zc_name));

	zc.zc_history = (uint64_t)(uintptr_t)buf;
	zc.zc_history_len = *len;
	zc.zc_history_offset = *off;

	if (ioctl(hdl->libzfs_fd, ZFS_IOC_POOL_GET_HISTORY, &zc) != 0) {
		switch (errno) {
		case EPERM:
			return (zfs_error_fmt(hdl, EZFS_PERM,
			    dgettext(TEXT_DOMAIN,
			    "cannot show history for pool '%s'"),
			    zhp->zpool_name));
		case ENOENT:
			return (zfs_error_fmt(hdl, EZFS_NOHISTORY,
			    dgettext(TEXT_DOMAIN, "cannot get history for pool "
			    "'%s'"), zhp->zpool_name));
		case ENOTSUP:
			return (zfs_error_fmt(hdl, EZFS_BADVERSION,
			    dgettext(TEXT_DOMAIN, "cannot get history for pool "
			    "'%s', pool must be upgraded"), zhp->zpool_name));
		default:
			return (zpool_standard_error_fmt(hdl, errno,
			    dgettext(TEXT_DOMAIN,
			    "cannot get history for '%s'"), zhp->zpool_name));
		}
	}

	*len = zc.zc_history_len;
	*off = zc.zc_history_offset;

	return (0);
}

/*
 * Process the buffer of nvlists, unpacking and storing each nvlist record
 * into 'records'.  'leftover' is set to the number of bytes that weren't
 * processed as there wasn't a complete record.
 */
static int
zpool_history_unpack(char *buf, uint64_t bytes_read, uint64_t *leftover,
    nvlist_t ***records, uint_t *numrecords)
{
	uint64_t reclen;
	nvlist_t *nv;
	int i;

	while (bytes_read > sizeof (reclen)) {

		/* get length of packed record (stored as little endian) */
		for (i = 0, reclen = 0; i < sizeof (reclen); i++)
			reclen += (uint64_t)(((uchar_t *)buf)[i]) << (8*i);

		if (bytes_read < sizeof (reclen) + reclen)
			break;

		/* unpack record */
		if (nvlist_unpack(buf + sizeof (reclen), reclen, &nv, 0) != 0)
			return (ENOMEM);
		bytes_read -= sizeof (reclen) + reclen;
		buf += sizeof (reclen) + reclen;

		/* add record to nvlist array */
		(*numrecords)++;
		if (ISP2(*numrecords + 1)) {
			*records = realloc(*records,
			    *numrecords * 2 * sizeof (nvlist_t *));
		}
		(*records)[*numrecords - 1] = nv;
	}

	*leftover = bytes_read;
	return (0);
}

#define	HIS_BUF_LEN	(128*1024)

/*
 * Retrieve the command history of a pool.
 */
int
zpool_get_history(zpool_handle_t *zhp, nvlist_t **nvhisp)
{
	char buf[HIS_BUF_LEN];
	uint64_t off = 0;
	nvlist_t **records = NULL;
	uint_t numrecords = 0;
	int err, i;

	do {
		uint64_t bytes_read = sizeof (buf);
		uint64_t leftover;

		if ((err = get_history(zhp, buf, &off, &bytes_read)) != 0)
			break;

		/* if nothing else was read in, we're at EOF, just return */
		if (!bytes_read)
			break;

		if ((err = zpool_history_unpack(buf, bytes_read,
		    &leftover, &records, &numrecords)) != 0)
			break;
		off -= leftover;

		/* CONSTCOND */
	} while (1);

	if (!err) {
		verify(nvlist_alloc(nvhisp, NV_UNIQUE_NAME, 0) == 0);
		verify(nvlist_add_nvlist_array(*nvhisp, ZPOOL_HIST_RECORD,
		    records, numrecords) == 0);
	}
	for (i = 0; i < numrecords; i++)
		nvlist_free(records[i]);
	free(records);

	return (err);
}

void
zpool_obj_to_path(zpool_handle_t *zhp, uint64_t dsobj, uint64_t obj,
    char *pathname, size_t len)
{
	zfs_cmd_t zc = { 0 };
	boolean_t mounted = B_FALSE;
	char *mntpnt = NULL;
	char dsname[MAXNAMELEN];

	if (dsobj == 0) {
		/* special case for the MOS */
		(void) snprintf(pathname, len, "<metadata>:<0x%llx>", obj);
		return;
	}

	/* get the dataset's name */
	(void) strlcpy(zc.zc_name, zhp->zpool_name, sizeof (zc.zc_name));
	zc.zc_obj = dsobj;
	if (ioctl(zhp->zpool_hdl->libzfs_fd,
	    ZFS_IOC_DSOBJ_TO_DSNAME, &zc) != 0) {
		/* just write out a path of two object numbers */
		(void) snprintf(pathname, len, "<0x%llx>:<0x%llx>",
		    dsobj, obj);
		return;
	}
	(void) strlcpy(dsname, zc.zc_value, sizeof (dsname));

	/* find out if the dataset is mounted */
	mounted = is_mounted(zhp->zpool_hdl, dsname, &mntpnt);

	/* get the corrupted object's path */
	(void) strlcpy(zc.zc_name, dsname, sizeof (zc.zc_name));
	zc.zc_obj = obj;
	if (ioctl(zhp->zpool_hdl->libzfs_fd, ZFS_IOC_OBJ_TO_PATH,
	    &zc) == 0) {
		if (mounted) {
			(void) snprintf(pathname, len, "%s%s", mntpnt,
			    zc.zc_value);
		} else {
			(void) snprintf(pathname, len, "%s:%s",
			    dsname, zc.zc_value);
		}
	} else {
		(void) snprintf(pathname, len, "%s:<0x%llx>", dsname, obj);
	}
	free(mntpnt);
}

#define	RDISK_ROOT	"/dev/rdsk"
#define	BACKUP_SLICE	"s2"
/*
 * Don't start the slice at the default block of 34; many storage
 * devices will use a stripe width of 128k, so start there instead.
 */
#define	NEW_START_BLOCK	256

/*
 * Read the EFI label from the config, if a label does not exist then
 * pass back the error to the caller. If the caller has passed a non-NULL
 * diskaddr argument then we set it to the starting address of the EFI
 * partition.
 */
static int
read_efi_label(nvlist_t *config, diskaddr_t *sb)
{
	char *path;
	int fd;
	char diskname[MAXPATHLEN];
	int err = -1;

	if (nvlist_lookup_string(config, ZPOOL_CONFIG_PATH, &path) != 0)
		return (err);

	(void) snprintf(diskname, sizeof (diskname), "%s%s", RDISK_ROOT,
	    strrchr(path, '/'));
	if ((fd = open(diskname, O_RDONLY|O_NDELAY)) >= 0) {
		struct dk_gpt *vtoc;

		if ((err = efi_alloc_and_read(fd, &vtoc)) >= 0) {
			if (sb != NULL)
				*sb = vtoc->efi_parts[0].p_start;
			efi_free(vtoc);
		}
		(void) close(fd);
	}
	return (err);
}

/*
 * determine where a partition starts on a disk in the current
 * configuration
 */
static diskaddr_t
find_start_block(nvlist_t *config)
{
	nvlist_t **child;
	uint_t c, children;
	diskaddr_t sb = MAXOFFSET_T;
	uint64_t wholedisk;

	if (nvlist_lookup_nvlist_array(config,
	    ZPOOL_CONFIG_CHILDREN, &child, &children) != 0) {
		if (nvlist_lookup_uint64(config,
		    ZPOOL_CONFIG_WHOLE_DISK,
		    &wholedisk) != 0 || !wholedisk) {
			return (MAXOFFSET_T);
		}
		if (read_efi_label(config, &sb) < 0)
			sb = MAXOFFSET_T;
		return (sb);
	}

	for (c = 0; c < children; c++) {
		sb = find_start_block(child[c]);
		if (sb != MAXOFFSET_T) {
			return (sb);
		}
	}
	return (MAXOFFSET_T);
}

/*
 * Label an individual disk.  The name provided is the short name,
 * stripped of any leading /dev path.
 */
int
zpool_label_disk(libzfs_handle_t *hdl, zpool_handle_t *zhp, char *name)
{
	char path[MAXPATHLEN];
	struct dk_gpt *vtoc;
	int fd;
	size_t resv = EFI_MIN_RESV_SIZE;
	uint64_t slice_size;
	diskaddr_t start_block;
	char errbuf[1024];

	/* prepare an error message just in case */
	(void) snprintf(errbuf, sizeof (errbuf),
	    dgettext(TEXT_DOMAIN, "cannot label '%s'"), name);

	if (zhp) {
		nvlist_t *nvroot;

		if (pool_is_bootable(zhp)) {
			zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
			    "EFI labeled devices are not supported on root "
			    "pools."));
			return (zfs_error(hdl, EZFS_POOL_NOTSUP, errbuf));
		}

		verify(nvlist_lookup_nvlist(zhp->zpool_config,
		    ZPOOL_CONFIG_VDEV_TREE, &nvroot) == 0);

		if (zhp->zpool_start_block == 0)
			start_block = find_start_block(nvroot);
		else
			start_block = zhp->zpool_start_block;
		zhp->zpool_start_block = start_block;
	} else {
		/* new pool */
		start_block = NEW_START_BLOCK;
	}

	(void) snprintf(path, sizeof (path), "%s/%s%s", RDISK_ROOT, name,
	    BACKUP_SLICE);

	if ((fd = open(path, O_RDWR | O_NDELAY)) < 0) {
		/*
		 * This shouldn't happen.  We've long since verified that this
		 * is a valid device.
		 */
		zfs_error_aux(hdl,
		    dgettext(TEXT_DOMAIN, "unable to open device"));
		return (zfs_error(hdl, EZFS_OPENFAILED, errbuf));
	}

	if (efi_alloc_and_init(fd, EFI_NUMPAR, &vtoc) != 0) {
		/*
		 * The only way this can fail is if we run out of memory, or we
		 * were unable to read the disk's capacity
		 */
		if (errno == ENOMEM)
			(void) no_memory(hdl);

		(void) close(fd);
		zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
		    "unable to read disk capacity"), name);

		return (zfs_error(hdl, EZFS_NOCAP, errbuf));
	}

	slice_size = vtoc->efi_last_u_lba + 1;
	slice_size -= EFI_MIN_RESV_SIZE;
	if (start_block == MAXOFFSET_T)
		start_block = NEW_START_BLOCK;
	slice_size -= start_block;

	vtoc->efi_parts[0].p_start = start_block;
	vtoc->efi_parts[0].p_size = slice_size;

	/*
	 * Why we use V_USR: V_BACKUP confuses users, and is considered
	 * disposable by some EFI utilities (since EFI doesn't have a backup
	 * slice).  V_UNASSIGNED is supposed to be used only for zero size
	 * partitions, and efi_write() will fail if we use it.  V_ROOT, V_BOOT,
	 * etc. were all pretty specific.  V_USR is as close to reality as we
	 * can get, in the absence of V_OTHER.
	 */
	vtoc->efi_parts[0].p_tag = V_USR;
	(void) strcpy(vtoc->efi_parts[0].p_name, "zfs");

	vtoc->efi_parts[8].p_start = slice_size + start_block;
	vtoc->efi_parts[8].p_size = resv;
	vtoc->efi_parts[8].p_tag = V_RESERVED;

	if (efi_write(fd, vtoc) != 0) {
		/*
		 * Some block drivers (like pcata) may not support EFI
		 * GPT labels.  Print out a helpful error message dir-
		 * ecting the user to manually label the disk and give
		 * a specific slice.
		 */
		(void) close(fd);
		efi_free(vtoc);

		zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
		    "try using fdisk(1M) and then provide a specific slice"));
		return (zfs_error(hdl, EZFS_LABELFAILED, errbuf));
	}

	(void) close(fd);
	efi_free(vtoc);
	return (0);
}

static boolean_t
supported_dump_vdev_type(libzfs_handle_t *hdl, nvlist_t *config, char *errbuf)
{
	char *type;
	nvlist_t **child;
	uint_t children, c;

	verify(nvlist_lookup_string(config, ZPOOL_CONFIG_TYPE, &type) == 0);
	if (strcmp(type, VDEV_TYPE_RAIDZ) == 0 ||
	    strcmp(type, VDEV_TYPE_FILE) == 0 ||
	    strcmp(type, VDEV_TYPE_LOG) == 0 ||
	    strcmp(type, VDEV_TYPE_MISSING) == 0) {
		zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
		    "vdev type '%s' is not supported"), type);
		(void) zfs_error(hdl, EZFS_VDEVNOTSUP, errbuf);
		return (B_FALSE);
	}
	if (nvlist_lookup_nvlist_array(config, ZPOOL_CONFIG_CHILDREN,
	    &child, &children) == 0) {
		for (c = 0; c < children; c++) {
			if (!supported_dump_vdev_type(hdl, child[c], errbuf))
				return (B_FALSE);
		}
	}
	return (B_TRUE);
}

/*
 * check if this zvol is allowable for use as a dump device; zero if
 * it is, > 0 if it isn't, < 0 if it isn't a zvol
 */
int
zvol_check_dump_config(char *arg)
{
	zpool_handle_t *zhp = NULL;
	nvlist_t *config, *nvroot;
	char *p, *volname;
	nvlist_t **top;
	uint_t toplevels;
	libzfs_handle_t *hdl;
	char errbuf[1024];
	char poolname[ZPOOL_MAXNAMELEN];
	int pathlen = strlen(ZVOL_FULL_DEV_DIR);
	int ret = 1;

	if (strncmp(arg, ZVOL_FULL_DEV_DIR, pathlen)) {
		return (-1);
	}

	(void) snprintf(errbuf, sizeof (errbuf), dgettext(TEXT_DOMAIN,
	    "dump is not supported on device '%s'"), arg);

	if ((hdl = libzfs_init()) == NULL)
		return (1);
	libzfs_print_on_error(hdl, B_TRUE);

	volname = arg + pathlen;

	/* check the configuration of the pool */
	if ((p = strchr(volname, '/')) == NULL) {
		zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
		    "malformed dataset name"));
		(void) zfs_error(hdl, EZFS_INVALIDNAME, errbuf);
		return (1);
	} else if (p - volname >= ZFS_MAXNAMELEN) {
		zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
		    "dataset name is too long"));
		(void) zfs_error(hdl, EZFS_NAMETOOLONG, errbuf);
		return (1);
	} else {
		(void) strncpy(poolname, volname, p - volname);
		poolname[p - volname] = '\0';
	}

	if ((zhp = zpool_open(hdl, poolname)) == NULL) {
		zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
		    "could not open pool '%s'"), poolname);
		(void) zfs_error(hdl, EZFS_OPENFAILED, errbuf);
		goto out;
	}
	config = zpool_get_config(zhp, NULL);
	if (nvlist_lookup_nvlist(config, ZPOOL_CONFIG_VDEV_TREE,
	    &nvroot) != 0) {
		zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
		    "could not obtain vdev configuration for  '%s'"), poolname);
		(void) zfs_error(hdl, EZFS_INVALCONFIG, errbuf);
		goto out;
	}

	verify(nvlist_lookup_nvlist_array(nvroot, ZPOOL_CONFIG_CHILDREN,
	    &top, &toplevels) == 0);
	if (toplevels != 1) {
		zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
		    "'%s' has multiple top level vdevs"), poolname);
		(void) zfs_error(hdl, EZFS_DEVOVERFLOW, errbuf);
		goto out;
	}

	if (!supported_dump_vdev_type(hdl, top[0], errbuf)) {
		goto out;
	}
	ret = 0;

out:
	if (zhp)
		zpool_close(zhp);
	libzfs_fini(hdl);
	return (ret);
}
