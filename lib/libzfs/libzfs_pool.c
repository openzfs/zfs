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
 * Copyright 2015 Nexenta Systems, Inc.  All rights reserved.
 * Copyright (c) 2005, 2010, Oracle and/or its affiliates. All rights reserved.
 * Copyright (c) 2011, 2020 by Delphix. All rights reserved.
 * Copyright 2016 Igor Kozhukhov <ikozhukhov@gmail.com>
 * Copyright (c) 2018 Datto Inc.
 * Copyright (c) 2017 Open-E, Inc. All Rights Reserved.
 * Copyright (c) 2017, Intel Corporation.
 * Copyright (c) 2018, loli10K <ezomori.nozomu@gmail.com>
 * Copyright (c) 2021, Colm Buckley <colm@tuatha.org>
 */

#include <errno.h>
#include <libintl.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <unistd.h>
#include <libgen.h>
#include <zone.h>
#include <sys/stat.h>
#include <sys/efi_partition.h>
#include <sys/systeminfo.h>
#include <sys/zfs_ioctl.h>
#include <sys/zfs_sysfs.h>
#include <sys/vdev_disk.h>
#include <sys/types.h>
#include <dlfcn.h>
#include <libzutil.h>
#include <fcntl.h>

#include "zfs_namecheck.h"
#include "zfs_prop.h"
#include "libzfs_impl.h"
#include "zfs_comutil.h"
#include "zfeature_common.h"

static boolean_t zpool_vdev_is_interior(const char *name);

typedef struct prop_flags {
	int create:1;	/* Validate property on creation */
	int import:1;	/* Validate property on import */
} prop_flags_t;

/*
 * ====================================================================
 *   zpool property functions
 * ====================================================================
 */

static int
zpool_get_all_props(zpool_handle_t *zhp)
{
	zfs_cmd_t zc = {"\0"};
	libzfs_handle_t *hdl = zhp->zpool_hdl;

	(void) strlcpy(zc.zc_name, zhp->zpool_name, sizeof (zc.zc_name));

	if (zcmd_alloc_dst_nvlist(hdl, &zc, 0) != 0)
		return (-1);

	while (zfs_ioctl(hdl, ZFS_IOC_POOL_GET_PROPS, &zc) != 0) {
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

int
zpool_props_refresh(zpool_handle_t *zhp)
{
	nvlist_t *old_props;

	old_props = zhp->zpool_props;

	if (zpool_get_all_props(zhp) != 0)
		return (-1);

	nvlist_free(old_props);
	return (0);
}

static const char *
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
const char *
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
		else if (aux == VDEV_AUX_SPLIT_POOL)
			return (gettext("SPLIT"));
		else
			return (gettext("UNAVAIL"));
	case VDEV_STATE_FAULTED:
		return (gettext("FAULTED"));
	case VDEV_STATE_DEGRADED:
		return (gettext("DEGRADED"));
	case VDEV_STATE_HEALTHY:
		return (gettext("ONLINE"));

	default:
		break;
	}

	return (gettext("UNKNOWN"));
}

/*
 * Map POOL STATE to printed strings.
 */
const char *
zpool_pool_state_to_name(pool_state_t state)
{
	switch (state) {
	default:
		break;
	case POOL_STATE_ACTIVE:
		return (gettext("ACTIVE"));
	case POOL_STATE_EXPORTED:
		return (gettext("EXPORTED"));
	case POOL_STATE_DESTROYED:
		return (gettext("DESTROYED"));
	case POOL_STATE_SPARE:
		return (gettext("SPARE"));
	case POOL_STATE_L2CACHE:
		return (gettext("L2CACHE"));
	case POOL_STATE_UNINITIALIZED:
		return (gettext("UNINITIALIZED"));
	case POOL_STATE_UNAVAIL:
		return (gettext("UNAVAIL"));
	case POOL_STATE_POTENTIALLY_ACTIVE:
		return (gettext("POTENTIALLY_ACTIVE"));
	}

	return (gettext("UNKNOWN"));
}

/*
 * Given a pool handle, return the pool health string ("ONLINE", "DEGRADED",
 * "SUSPENDED", etc).
 */
const char *
zpool_get_state_str(zpool_handle_t *zhp)
{
	zpool_errata_t errata;
	zpool_status_t status;
	nvlist_t *nvroot;
	vdev_stat_t *vs;
	uint_t vsc;
	const char *str;

	status = zpool_get_status(zhp, NULL, &errata);

	if (zpool_get_state(zhp) == POOL_STATE_UNAVAIL) {
		str = gettext("FAULTED");
	} else if (status == ZPOOL_STATUS_IO_FAILURE_WAIT ||
	    status == ZPOOL_STATUS_IO_FAILURE_MMP) {
		str = gettext("SUSPENDED");
	} else {
		verify(nvlist_lookup_nvlist(zpool_get_config(zhp, NULL),
		    ZPOOL_CONFIG_VDEV_TREE, &nvroot) == 0);
		verify(nvlist_lookup_uint64_array(nvroot,
		    ZPOOL_CONFIG_VDEV_STATS, (uint64_t **)&vs, &vsc)
		    == 0);
		str = zpool_state_to_name(vs->vs_state, vs->vs_aux);
	}
	return (str);
}

/*
 * Get a zpool property value for 'prop' and return the value in
 * a pre-allocated buffer.
 */
int
zpool_get_prop(zpool_handle_t *zhp, zpool_prop_t prop, char *buf,
    size_t len, zprop_source_t *srctype, boolean_t literal)
{
	uint64_t intval;
	const char *strval;
	zprop_source_t src = ZPROP_SRC_NONE;

	if (zpool_get_state(zhp) == POOL_STATE_UNAVAIL) {
		switch (prop) {
		case ZPOOL_PROP_NAME:
			(void) strlcpy(buf, zpool_get_name(zhp), len);
			break;

		case ZPOOL_PROP_HEALTH:
			(void) strlcpy(buf, zpool_get_state_str(zhp), len);
			break;

		case ZPOOL_PROP_GUID:
			intval = zpool_get_prop_int(zhp, prop, &src);
			(void) snprintf(buf, len, "%llu", (u_longlong_t)intval);
			break;

		case ZPOOL_PROP_ALTROOT:
		case ZPOOL_PROP_CACHEFILE:
		case ZPOOL_PROP_COMMENT:
		case ZPOOL_PROP_COMPATIBILITY:
			if (zhp->zpool_props != NULL ||
			    zpool_get_all_props(zhp) == 0) {
				(void) strlcpy(buf,
				    zpool_get_prop_string(zhp, prop, &src),
				    len);
				break;
			}
			fallthrough;
		default:
			(void) strlcpy(buf, "-", len);
			break;
		}

		if (srctype != NULL)
			*srctype = src;
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
		case ZPOOL_PROP_ALLOCATED:
		case ZPOOL_PROP_FREE:
		case ZPOOL_PROP_FREEING:
		case ZPOOL_PROP_LEAKED:
		case ZPOOL_PROP_ASHIFT:
			if (literal)
				(void) snprintf(buf, len, "%llu",
				    (u_longlong_t)intval);
			else
				(void) zfs_nicenum(intval, buf, len);
			break;

		case ZPOOL_PROP_EXPANDSZ:
		case ZPOOL_PROP_CHECKPOINT:
			if (intval == 0) {
				(void) strlcpy(buf, "-", len);
			} else if (literal) {
				(void) snprintf(buf, len, "%llu",
				    (u_longlong_t)intval);
			} else {
				(void) zfs_nicebytes(intval, buf, len);
			}
			break;

		case ZPOOL_PROP_CAPACITY:
			if (literal) {
				(void) snprintf(buf, len, "%llu",
				    (u_longlong_t)intval);
			} else {
				(void) snprintf(buf, len, "%llu%%",
				    (u_longlong_t)intval);
			}
			break;

		case ZPOOL_PROP_FRAGMENTATION:
			if (intval == UINT64_MAX) {
				(void) strlcpy(buf, "-", len);
			} else if (literal) {
				(void) snprintf(buf, len, "%llu",
				    (u_longlong_t)intval);
			} else {
				(void) snprintf(buf, len, "%llu%%",
				    (u_longlong_t)intval);
			}
			break;

		case ZPOOL_PROP_DEDUPRATIO:
			if (literal)
				(void) snprintf(buf, len, "%llu.%02llu",
				    (u_longlong_t)(intval / 100),
				    (u_longlong_t)(intval % 100));
			else
				(void) snprintf(buf, len, "%llu.%02llux",
				    (u_longlong_t)(intval / 100),
				    (u_longlong_t)(intval % 100));
			break;

		case ZPOOL_PROP_HEALTH:
			(void) strlcpy(buf, zpool_get_state_str(zhp), len);
			break;
		case ZPOOL_PROP_VERSION:
			if (intval >= SPA_VERSION_FEATURES) {
				(void) snprintf(buf, len, "-");
				break;
			}
			fallthrough;
		default:
			(void) snprintf(buf, len, "%llu", (u_longlong_t)intval);
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
bootfs_name_valid(const char *pool, const char *bootfs)
{
	int len = strlen(pool);
	if (bootfs[0] == '\0')
		return (B_TRUE);

	if (!zfs_name_valid(bootfs, ZFS_TYPE_FILESYSTEM|ZFS_TYPE_SNAPSHOT))
		return (B_FALSE);

	if (strncmp(pool, bootfs, len) == 0 &&
	    (bootfs[len] == '/' || bootfs[len] == '\0'))
		return (B_TRUE);

	return (B_FALSE);
}

/*
 * Given an nvlist of zpool properties to be set, validate that they are
 * correct, and parse any numeric properties (index, boolean, etc) if they are
 * specified as strings.
 */
static nvlist_t *
zpool_valid_proplist(libzfs_handle_t *hdl, const char *poolname,
    nvlist_t *props, uint64_t version, prop_flags_t flags, char *errbuf)
{
	nvpair_t *elem;
	nvlist_t *retprops;
	zpool_prop_t prop;
	char *strval;
	uint64_t intval;
	char *slash, *check;
	struct stat64 statbuf;
	zpool_handle_t *zhp;
	char report[1024];

	if (nvlist_alloc(&retprops, NV_UNIQUE_NAME, 0) != 0) {
		(void) no_memory(hdl);
		return (NULL);
	}

	elem = NULL;
	while ((elem = nvlist_next_nvpair(props, elem)) != NULL) {
		const char *propname = nvpair_name(elem);

		prop = zpool_name_to_prop(propname);
		if (prop == ZPOOL_PROP_INVAL && zpool_prop_feature(propname)) {
			int err;
			char *fname = strchr(propname, '@') + 1;

			err = zfeature_lookup_name(fname, NULL);
			if (err != 0) {
				ASSERT3U(err, ==, ENOENT);
				zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
				    "feature '%s' unsupported by kernel"),
				    fname);
				(void) zfs_error(hdl, EZFS_BADPROP, errbuf);
				goto error;
			}

			if (nvpair_type(elem) != DATA_TYPE_STRING) {
				zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
				    "'%s' must be a string"), propname);
				(void) zfs_error(hdl, EZFS_BADPROP, errbuf);
				goto error;
			}

			(void) nvpair_value_string(elem, &strval);
			if (strcmp(strval, ZFS_FEATURE_ENABLED) != 0 &&
			    strcmp(strval, ZFS_FEATURE_DISABLED) != 0) {
				zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
				    "property '%s' can only be set to "
				    "'enabled' or 'disabled'"), propname);
				(void) zfs_error(hdl, EZFS_BADPROP, errbuf);
				goto error;
			}

			if (!flags.create &&
			    strcmp(strval, ZFS_FEATURE_DISABLED) == 0) {
				zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
				    "property '%s' can only be set to "
				    "'disabled' at creation time"), propname);
				(void) zfs_error(hdl, EZFS_BADPROP, errbuf);
				goto error;
			}

			if (nvlist_add_uint64(retprops, propname, 0) != 0) {
				(void) no_memory(hdl);
				goto error;
			}
			continue;
		}

		/*
		 * Make sure this property is valid and applies to this type.
		 */
		if (prop == ZPOOL_PROP_INVAL) {
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

		if (!flags.create && zpool_prop_setonce(prop)) {
			zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
			    "property '%s' can only be set at "
			    "creation time"), propname);
			(void) zfs_error(hdl, EZFS_BADPROP, errbuf);
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
			if (intval < version ||
			    !SPA_VERSION_IS_SUPPORTED(intval)) {
				zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
				    "property '%s' number %llu is invalid."),
				    propname, (unsigned long long)intval);
				(void) zfs_error(hdl, EZFS_BADVERSION, errbuf);
				goto error;
			}
			break;

		case ZPOOL_PROP_ASHIFT:
			if (intval != 0 &&
			    (intval < ASHIFT_MIN || intval > ASHIFT_MAX)) {
				zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
				    "property '%s' number %llu is invalid, "
				    "only values between %" PRId32 " and %"
				    PRId32 " are allowed."),
				    propname, (unsigned long long)intval,
				    ASHIFT_MIN, ASHIFT_MAX);
				(void) zfs_error(hdl, EZFS_BADPROP, errbuf);
				goto error;
			}
			break;

		case ZPOOL_PROP_BOOTFS:
			if (flags.create || flags.import) {
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
			if (!bootfs_name_valid(poolname, strval)) {
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
			zpool_close(zhp);
			break;

		case ZPOOL_PROP_ALTROOT:
			if (!flags.create && !flags.import) {
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

		case ZPOOL_PROP_COMPATIBILITY:
			switch (zpool_load_compat(strval, NULL, report, 1024)) {
			case ZPOOL_COMPATIBILITY_OK:
			case ZPOOL_COMPATIBILITY_WARNTOKEN:
				break;
			case ZPOOL_COMPATIBILITY_BADFILE:
			case ZPOOL_COMPATIBILITY_BADTOKEN:
			case ZPOOL_COMPATIBILITY_NOFILES:
				zfs_error_aux(hdl, "%s", report);
				(void) zfs_error(hdl, EZFS_BADPROP, errbuf);
				goto error;
			}
			break;

		case ZPOOL_PROP_COMMENT:
			for (check = strval; *check != '\0'; check++) {
				if (!isprint(*check)) {
					zfs_error_aux(hdl,
					    dgettext(TEXT_DOMAIN,
					    "comment may only have printable "
					    "characters"));
					(void) zfs_error(hdl, EZFS_BADPROP,
					    errbuf);
					goto error;
				}
			}
			if (strlen(strval) > ZPROP_MAX_COMMENT) {
				zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
				    "comment must not exceed %d characters"),
				    ZPROP_MAX_COMMENT);
				(void) zfs_error(hdl, EZFS_BADPROP, errbuf);
				goto error;
			}
			break;
		case ZPOOL_PROP_READONLY:
			if (!flags.import) {
				zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
				    "property '%s' can only be set at "
				    "import time"), propname);
				(void) zfs_error(hdl, EZFS_BADPROP, errbuf);
				goto error;
			}
			break;
		case ZPOOL_PROP_MULTIHOST:
			if (get_system_hostid() == 0) {
				zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
				    "requires a non-zero system hostid"));
				(void) zfs_error(hdl, EZFS_BADPROP, errbuf);
				goto error;
			}
			break;
		case ZPOOL_PROP_DEDUPDITTO:
			printf("Note: property '%s' no longer has "
			    "any effect\n", propname);
			break;

		default:
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
	zfs_cmd_t zc = {"\0"};
	int ret = -1;
	char errbuf[1024];
	nvlist_t *nvl = NULL;
	nvlist_t *realprops;
	uint64_t version;
	prop_flags_t flags = { 0 };

	(void) snprintf(errbuf, sizeof (errbuf),
	    dgettext(TEXT_DOMAIN, "cannot set property for '%s'"),
	    zhp->zpool_name);

	if (nvlist_alloc(&nvl, NV_UNIQUE_NAME, 0) != 0)
		return (no_memory(zhp->zpool_hdl));

	if (nvlist_add_string(nvl, propname, propval) != 0) {
		nvlist_free(nvl);
		return (no_memory(zhp->zpool_hdl));
	}

	version = zpool_get_prop_int(zhp, ZPOOL_PROP_VERSION, NULL);
	if ((realprops = zpool_valid_proplist(zhp->zpool_hdl,
	    zhp->zpool_name, nvl, version, flags, errbuf)) == NULL) {
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
zpool_expand_proplist(zpool_handle_t *zhp, zprop_list_t **plp,
    boolean_t literal)
{
	libzfs_handle_t *hdl = zhp->zpool_hdl;
	zprop_list_t *entry;
	char buf[ZFS_MAXPROPLEN];
	nvlist_t *features = NULL;
	nvpair_t *nvp;
	zprop_list_t **last;
	boolean_t firstexpand = (NULL == *plp);
	int i;

	if (zprop_expand_list(hdl, plp, ZFS_TYPE_POOL) != 0)
		return (-1);

	last = plp;
	while (*last != NULL)
		last = &(*last)->pl_next;

	if ((*plp)->pl_all)
		features = zpool_get_features(zhp);

	if ((*plp)->pl_all && firstexpand) {
		for (i = 0; i < SPA_FEATURES; i++) {
			zprop_list_t *entry = zfs_alloc(hdl,
			    sizeof (zprop_list_t));
			entry->pl_prop = ZPROP_INVAL;
			entry->pl_user_prop = zfs_asprintf(hdl, "feature@%s",
			    spa_feature_table[i].fi_uname);
			entry->pl_width = strlen(entry->pl_user_prop);
			entry->pl_all = B_TRUE;

			*last = entry;
			last = &entry->pl_next;
		}
	}

	/* add any unsupported features */
	for (nvp = nvlist_next_nvpair(features, NULL);
	    nvp != NULL; nvp = nvlist_next_nvpair(features, nvp)) {
		char *propname;
		boolean_t found;
		zprop_list_t *entry;

		if (zfeature_is_supported(nvpair_name(nvp)))
			continue;

		propname = zfs_asprintf(hdl, "unsupported@%s",
		    nvpair_name(nvp));

		/*
		 * Before adding the property to the list make sure that no
		 * other pool already added the same property.
		 */
		found = B_FALSE;
		entry = *plp;
		while (entry != NULL) {
			if (entry->pl_user_prop != NULL &&
			    strcmp(propname, entry->pl_user_prop) == 0) {
				found = B_TRUE;
				break;
			}
			entry = entry->pl_next;
		}
		if (found) {
			free(propname);
			continue;
		}

		entry = zfs_alloc(hdl, sizeof (zprop_list_t));
		entry->pl_prop = ZPROP_INVAL;
		entry->pl_user_prop = propname;
		entry->pl_width = strlen(entry->pl_user_prop);
		entry->pl_all = B_TRUE;

		*last = entry;
		last = &entry->pl_next;
	}

	for (entry = *plp; entry != NULL; entry = entry->pl_next) {
		if (entry->pl_fixed && !literal)
			continue;

		if (entry->pl_prop != ZPROP_INVAL &&
		    zpool_get_prop(zhp, entry->pl_prop, buf, sizeof (buf),
		    NULL, literal) == 0) {
			if (strlen(buf) > entry->pl_width)
				entry->pl_width = strlen(buf);
		}
	}

	return (0);
}

/*
 * Get the state for the given feature on the given ZFS pool.
 */
int
zpool_prop_get_feature(zpool_handle_t *zhp, const char *propname, char *buf,
    size_t len)
{
	uint64_t refcount;
	boolean_t found = B_FALSE;
	nvlist_t *features = zpool_get_features(zhp);
	boolean_t supported;
	const char *feature = strchr(propname, '@') + 1;

	supported = zpool_prop_feature(propname);
	ASSERT(supported || zpool_prop_unsupported(propname));

	/*
	 * Convert from feature name to feature guid. This conversion is
	 * unnecessary for unsupported@... properties because they already
	 * use guids.
	 */
	if (supported) {
		int ret;
		spa_feature_t fid;

		ret = zfeature_lookup_name(feature, &fid);
		if (ret != 0) {
			(void) strlcpy(buf, "-", len);
			return (ENOTSUP);
		}
		feature = spa_feature_table[fid].fi_guid;
	}

	if (nvlist_lookup_uint64(features, feature, &refcount) == 0)
		found = B_TRUE;

	if (supported) {
		if (!found) {
			(void) strlcpy(buf, ZFS_FEATURE_DISABLED, len);
		} else  {
			if (refcount == 0)
				(void) strlcpy(buf, ZFS_FEATURE_ENABLED, len);
			else
				(void) strlcpy(buf, ZFS_FEATURE_ACTIVE, len);
		}
	} else {
		if (found) {
			if (refcount == 0) {
				(void) strcpy(buf, ZFS_UNSUPPORTED_INACTIVE);
			} else {
				(void) strcpy(buf, ZFS_UNSUPPORTED_READONLY);
			}
		} else {
			(void) strlcpy(buf, "-", len);
			return (ENOTSUP);
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
	    strncmp(pool, "draid", 5) == 0 ||
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

			case NAME_ERR_MULTIPLE_DELIMITERS:
				zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
				    "multiple '@' and/or '#' delimiters in "
				    "name"));
				break;

			case NAME_ERR_NO_AT:
				zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
				    "permission set is missing '@'"));
				break;

			default:
				zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
				    "(%d) not defined"), why);
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
	nvlist_free(zhp->zpool_config);
	nvlist_free(zhp->zpool_old_config);
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
 * Check if vdev list contains a special vdev
 */
static boolean_t
zpool_has_special_vdev(nvlist_t *nvroot)
{
	nvlist_t **child;
	uint_t children;

	if (nvlist_lookup_nvlist_array(nvroot, ZPOOL_CONFIG_CHILDREN, &child,
	    &children) == 0) {
		for (uint_t c = 0; c < children; c++) {
			char *bias;

			if (nvlist_lookup_string(child[c],
			    ZPOOL_CONFIG_ALLOCATION_BIAS, &bias) == 0 &&
			    strcmp(bias, VDEV_ALLOC_BIAS_SPECIAL) == 0) {
				return (B_TRUE);
			}
		}
	}
	return (B_FALSE);
}

/*
 * Check if vdev list contains a dRAID vdev
 */
static boolean_t
zpool_has_draid_vdev(nvlist_t *nvroot)
{
	nvlist_t **child;
	uint_t children;

	if (nvlist_lookup_nvlist_array(nvroot, ZPOOL_CONFIG_CHILDREN,
	    &child, &children) == 0) {
		for (uint_t c = 0; c < children; c++) {
			char *type;

			if (nvlist_lookup_string(child[c],
			    ZPOOL_CONFIG_TYPE, &type) == 0 &&
			    strcmp(type, VDEV_TYPE_DRAID) == 0) {
				return (B_TRUE);
			}
		}
	}
	return (B_FALSE);
}

/*
 * Output a dRAID top-level vdev name in to the provided buffer.
 */
static char *
zpool_draid_name(char *name, int len, uint64_t data, uint64_t parity,
    uint64_t spares, uint64_t children)
{
	snprintf(name, len, "%s%llu:%llud:%lluc:%llus",
	    VDEV_TYPE_DRAID, (u_longlong_t)parity, (u_longlong_t)data,
	    (u_longlong_t)children, (u_longlong_t)spares);

	return (name);
}

/*
 * Return B_TRUE if the provided name is a dRAID spare name.
 */
boolean_t
zpool_is_draid_spare(const char *name)
{
	uint64_t spare_id, parity, vdev_id;

	if (sscanf(name, VDEV_TYPE_DRAID "%llu-%llu-%llu",
	    (u_longlong_t *)&parity, (u_longlong_t *)&vdev_id,
	    (u_longlong_t *)&spare_id) == 3) {
		return (B_TRUE);
	}

	return (B_FALSE);
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
	zfs_cmd_t zc = {"\0"};
	nvlist_t *zc_fsprops = NULL;
	nvlist_t *zc_props = NULL;
	nvlist_t *hidden_args = NULL;
	uint8_t *wkeydata = NULL;
	uint_t wkeylen = 0;
	char msg[1024];
	int ret = -1;

	(void) snprintf(msg, sizeof (msg), dgettext(TEXT_DOMAIN,
	    "cannot create '%s'"), pool);

	if (!zpool_name_valid(hdl, B_FALSE, pool))
		return (zfs_error(hdl, EZFS_INVALIDNAME, msg));

	if (zcmd_write_conf_nvlist(hdl, &zc, nvroot) != 0)
		return (-1);

	if (props) {
		prop_flags_t flags = { .create = B_TRUE, .import = B_FALSE };

		if ((zc_props = zpool_valid_proplist(hdl, pool, props,
		    SPA_VERSION_1, flags, msg)) == NULL) {
			goto create_failed;
		}
	}

	if (fsprops) {
		uint64_t zoned;
		char *zonestr;

		zoned = ((nvlist_lookup_string(fsprops,
		    zfs_prop_to_name(ZFS_PROP_ZONED), &zonestr) == 0) &&
		    strcmp(zonestr, "on") == 0);

		if ((zc_fsprops = zfs_valid_proplist(hdl, ZFS_TYPE_FILESYSTEM,
		    fsprops, zoned, NULL, NULL, B_TRUE, msg)) == NULL) {
			goto create_failed;
		}

		if (nvlist_exists(zc_fsprops,
		    zfs_prop_to_name(ZFS_PROP_SPECIAL_SMALL_BLOCKS)) &&
		    !zpool_has_special_vdev(nvroot)) {
			zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
			    "%s property requires a special vdev"),
			    zfs_prop_to_name(ZFS_PROP_SPECIAL_SMALL_BLOCKS));
			(void) zfs_error(hdl, EZFS_BADPROP, msg);
			goto create_failed;
		}

		if (!zc_props &&
		    (nvlist_alloc(&zc_props, NV_UNIQUE_NAME, 0) != 0)) {
			goto create_failed;
		}
		if (zfs_crypto_create(hdl, NULL, zc_fsprops, props, B_TRUE,
		    &wkeydata, &wkeylen) != 0) {
			zfs_error(hdl, EZFS_CRYPTOFAILED, msg);
			goto create_failed;
		}
		if (nvlist_add_nvlist(zc_props,
		    ZPOOL_ROOTFS_PROPS, zc_fsprops) != 0) {
			goto create_failed;
		}
		if (wkeydata != NULL) {
			if (nvlist_alloc(&hidden_args, NV_UNIQUE_NAME, 0) != 0)
				goto create_failed;

			if (nvlist_add_uint8_array(hidden_args, "wkeydata",
			    wkeydata, wkeylen) != 0)
				goto create_failed;

			if (nvlist_add_nvlist(zc_props, ZPOOL_HIDDEN_ARGS,
			    hidden_args) != 0)
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
		nvlist_free(hidden_args);
		if (wkeydata != NULL)
			free(wkeydata);

		switch (errno) {
		case EBUSY:
			/*
			 * This can happen if the user has specified the same
			 * device multiple times.  We can't reliably detect this
			 * until we try to add it and see we already have a
			 * label.  This can also happen under if the device is
			 * part of an active md or lvm device.
			 */
			zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
			    "one or more vdevs refer to the same device, or "
			    "one of\nthe devices is part of an active md or "
			    "lvm device"));
			return (zfs_error(hdl, EZFS_BADDEV, msg));

		case ERANGE:
			/*
			 * This happens if the record size is smaller or larger
			 * than the allowed size range, or not a power of 2.
			 *
			 * NOTE: although zfs_valid_proplist is called earlier,
			 * this case may have slipped through since the
			 * pool does not exist yet and it is therefore
			 * impossible to read properties e.g. max blocksize
			 * from the pool.
			 */
			zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
			    "record size invalid"));
			return (zfs_error(hdl, EZFS_BADPROP, msg));

		case EOVERFLOW:
			/*
			 * This occurs when one of the devices is below
			 * SPA_MINDEVSIZE.  Unfortunately, we can't detect which
			 * device was the problem device since there's no
			 * reliable way to determine device size from userland.
			 */
			{
				char buf[64];

				zfs_nicebytes(SPA_MINDEVSIZE, buf,
				    sizeof (buf));

				zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
				    "one or more devices is less than the "
				    "minimum size (%s)"), buf);
			}
			return (zfs_error(hdl, EZFS_BADDEV, msg));

		case ENOSPC:
			zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
			    "one or more devices is out of space"));
			return (zfs_error(hdl, EZFS_BADDEV, msg));

		case EINVAL:
			if (zpool_has_draid_vdev(nvroot) &&
			    zfeature_lookup_name("draid", NULL) != 0) {
				zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
				    "dRAID vdevs are unsupported by the "
				    "kernel"));
				return (zfs_error(hdl, EZFS_BADDEV, msg));
			} else {
				return (zpool_standard_error(hdl, errno, msg));
			}

		default:
			return (zpool_standard_error(hdl, errno, msg));
		}
	}

create_failed:
	zcmd_free_nvlists(&zc);
	nvlist_free(zc_props);
	nvlist_free(zc_fsprops);
	nvlist_free(hidden_args);
	if (wkeydata != NULL)
		free(wkeydata);
	return (ret);
}

/*
 * Destroy the given pool.  It is up to the caller to ensure that there are no
 * datasets left in the pool.
 */
int
zpool_destroy(zpool_handle_t *zhp, const char *log_str)
{
	zfs_cmd_t zc = {"\0"};
	zfs_handle_t *zfp = NULL;
	libzfs_handle_t *hdl = zhp->zpool_hdl;
	char msg[1024];

	if (zhp->zpool_state == POOL_STATE_ACTIVE &&
	    (zfp = zfs_open(hdl, zhp->zpool_name, ZFS_TYPE_FILESYSTEM)) == NULL)
		return (-1);

	(void) strlcpy(zc.zc_name, zhp->zpool_name, sizeof (zc.zc_name));
	zc.zc_history = (uint64_t)(uintptr_t)log_str;

	if (zfs_ioctl(hdl, ZFS_IOC_POOL_DESTROY, &zc) != 0) {
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
 * Create a checkpoint in the given pool.
 */
int
zpool_checkpoint(zpool_handle_t *zhp)
{
	libzfs_handle_t *hdl = zhp->zpool_hdl;
	char msg[1024];
	int error;

	error = lzc_pool_checkpoint(zhp->zpool_name);
	if (error != 0) {
		(void) snprintf(msg, sizeof (msg), dgettext(TEXT_DOMAIN,
		    "cannot checkpoint '%s'"), zhp->zpool_name);
		(void) zpool_standard_error(hdl, error, msg);
		return (-1);
	}

	return (0);
}

/*
 * Discard the checkpoint from the given pool.
 */
int
zpool_discard_checkpoint(zpool_handle_t *zhp)
{
	libzfs_handle_t *hdl = zhp->zpool_hdl;
	char msg[1024];
	int error;

	error = lzc_pool_checkpoint_discard(zhp->zpool_name);
	if (error != 0) {
		(void) snprintf(msg, sizeof (msg), dgettext(TEXT_DOMAIN,
		    "cannot discard checkpoint in '%s'"), zhp->zpool_name);
		(void) zpool_standard_error(hdl, error, msg);
		return (-1);
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
	zfs_cmd_t zc = {"\0"};
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

	if (zfs_ioctl(hdl, ZFS_IOC_VDEV_ADD, &zc) != 0) {
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

		case EINVAL:

			if (zpool_has_draid_vdev(nvroot) &&
			    zfeature_lookup_name("draid", NULL) != 0) {
				zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
				    "dRAID vdevs are unsupported by the "
				    "kernel"));
			} else {
				zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
				    "invalid config; a pool with removing/"
				    "removed vdevs does not support adding "
				    "raidz or dRAID vdevs"));
			}

			(void) zfs_error(hdl, EZFS_BADDEV, msg);
			break;

		case EOVERFLOW:
			/*
			 * This occurs when one of the devices is below
			 * SPA_MINDEVSIZE.  Unfortunately, we can't detect which
			 * device was the problem device since there's no
			 * reliable way to determine device size from userland.
			 */
			{
				char buf[64];

				zfs_nicebytes(SPA_MINDEVSIZE, buf,
				    sizeof (buf));

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
static int
zpool_export_common(zpool_handle_t *zhp, boolean_t force, boolean_t hardforce,
    const char *log_str)
{
	zfs_cmd_t zc = {"\0"};

	(void) strlcpy(zc.zc_name, zhp->zpool_name, sizeof (zc.zc_name));
	zc.zc_cookie = force;
	zc.zc_guid = hardforce;
	zc.zc_history = (uint64_t)(uintptr_t)log_str;

	if (zfs_ioctl(zhp->zpool_hdl, ZFS_IOC_POOL_EXPORT, &zc) != 0) {
		switch (errno) {
		case EXDEV:
			zfs_error_aux(zhp->zpool_hdl, dgettext(TEXT_DOMAIN,
			    "use '-f' to override the following errors:\n"
			    "'%s' has an active shared spare which could be"
			    " used by other pools once '%s' is exported."),
			    zhp->zpool_name, zhp->zpool_name);
			return (zfs_error_fmt(zhp->zpool_hdl, EZFS_ACTIVE_SPARE,
			    dgettext(TEXT_DOMAIN, "cannot export '%s'"),
			    zhp->zpool_name));
		default:
			return (zpool_standard_error_fmt(zhp->zpool_hdl, errno,
			    dgettext(TEXT_DOMAIN, "cannot export '%s'"),
			    zhp->zpool_name));
		}
	}

	return (0);
}

int
zpool_export(zpool_handle_t *zhp, boolean_t force, const char *log_str)
{
	return (zpool_export_common(zhp, force, B_FALSE, log_str));
}

int
zpool_export_force(zpool_handle_t *zhp, const char *log_str)
{
	return (zpool_export_common(zhp, B_TRUE, B_TRUE, log_str));
}

static void
zpool_rewind_exclaim(libzfs_handle_t *hdl, const char *name, boolean_t dryrun,
    nvlist_t *config)
{
	nvlist_t *nv = NULL;
	uint64_t rewindto;
	int64_t loss = -1;
	struct tm t;
	char timestr[128];

	if (!hdl->libzfs_printerr || config == NULL)
		return;

	if (nvlist_lookup_nvlist(config, ZPOOL_CONFIG_LOAD_INFO, &nv) != 0 ||
	    nvlist_lookup_nvlist(nv, ZPOOL_CONFIG_REWIND_INFO, &nv) != 0) {
		return;
	}

	if (nvlist_lookup_uint64(nv, ZPOOL_CONFIG_LOAD_TIME, &rewindto) != 0)
		return;
	(void) nvlist_lookup_int64(nv, ZPOOL_CONFIG_REWIND_TIME, &loss);

	if (localtime_r((time_t *)&rewindto, &t) != NULL &&
	    strftime(timestr, 128, "%c", &t) != 0) {
		if (dryrun) {
			(void) printf(dgettext(TEXT_DOMAIN,
			    "Would be able to return %s "
			    "to its state as of %s.\n"),
			    name, timestr);
		} else {
			(void) printf(dgettext(TEXT_DOMAIN,
			    "Pool %s returned to its state as of %s.\n"),
			    name, timestr);
		}
		if (loss > 120) {
			(void) printf(dgettext(TEXT_DOMAIN,
			    "%s approximately %lld "),
			    dryrun ? "Would discard" : "Discarded",
			    ((longlong_t)loss + 30) / 60);
			(void) printf(dgettext(TEXT_DOMAIN,
			    "minutes of transactions.\n"));
		} else if (loss > 0) {
			(void) printf(dgettext(TEXT_DOMAIN,
			    "%s approximately %lld "),
			    dryrun ? "Would discard" : "Discarded",
			    (longlong_t)loss);
			(void) printf(dgettext(TEXT_DOMAIN,
			    "seconds of transactions.\n"));
		}
	}
}

void
zpool_explain_recover(libzfs_handle_t *hdl, const char *name, int reason,
    nvlist_t *config)
{
	nvlist_t *nv = NULL;
	int64_t loss = -1;
	uint64_t edata = UINT64_MAX;
	uint64_t rewindto;
	struct tm t;
	char timestr[128];

	if (!hdl->libzfs_printerr)
		return;

	if (reason >= 0)
		(void) printf(dgettext(TEXT_DOMAIN, "action: "));
	else
		(void) printf(dgettext(TEXT_DOMAIN, "\t"));

	/* All attempted rewinds failed if ZPOOL_CONFIG_LOAD_TIME missing */
	if (nvlist_lookup_nvlist(config, ZPOOL_CONFIG_LOAD_INFO, &nv) != 0 ||
	    nvlist_lookup_nvlist(nv, ZPOOL_CONFIG_REWIND_INFO, &nv) != 0 ||
	    nvlist_lookup_uint64(nv, ZPOOL_CONFIG_LOAD_TIME, &rewindto) != 0)
		goto no_info;

	(void) nvlist_lookup_int64(nv, ZPOOL_CONFIG_REWIND_TIME, &loss);
	(void) nvlist_lookup_uint64(nv, ZPOOL_CONFIG_LOAD_DATA_ERRORS,
	    &edata);

	(void) printf(dgettext(TEXT_DOMAIN,
	    "Recovery is possible, but will result in some data loss.\n"));

	if (localtime_r((time_t *)&rewindto, &t) != NULL &&
	    strftime(timestr, 128, "%c", &t) != 0) {
		(void) printf(dgettext(TEXT_DOMAIN,
		    "\tReturning the pool to its state as of %s\n"
		    "\tshould correct the problem.  "),
		    timestr);
	} else {
		(void) printf(dgettext(TEXT_DOMAIN,
		    "\tReverting the pool to an earlier state "
		    "should correct the problem.\n\t"));
	}

	if (loss > 120) {
		(void) printf(dgettext(TEXT_DOMAIN,
		    "Approximately %lld minutes of data\n"
		    "\tmust be discarded, irreversibly.  "),
		    ((longlong_t)loss + 30) / 60);
	} else if (loss > 0) {
		(void) printf(dgettext(TEXT_DOMAIN,
		    "Approximately %lld seconds of data\n"
		    "\tmust be discarded, irreversibly.  "),
		    (longlong_t)loss);
	}
	if (edata != 0 && edata != UINT64_MAX) {
		if (edata == 1) {
			(void) printf(dgettext(TEXT_DOMAIN,
			    "After rewind, at least\n"
			    "\tone persistent user-data error will remain.  "));
		} else {
			(void) printf(dgettext(TEXT_DOMAIN,
			    "After rewind, several\n"
			    "\tpersistent user-data errors will remain.  "));
		}
	}
	(void) printf(dgettext(TEXT_DOMAIN,
	    "Recovery can be attempted\n\tby executing 'zpool %s -F %s'.  "),
	    reason >= 0 ? "clear" : "import", name);

	(void) printf(dgettext(TEXT_DOMAIN,
	    "A scrub of the pool\n"
	    "\tis strongly recommended after recovery.\n"));
	return;

no_info:
	(void) printf(dgettext(TEXT_DOMAIN,
	    "Destroy and re-create the pool from\n\ta backup source.\n"));
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

	ret = zpool_import_props(hdl, config, newname, props,
	    ZFS_IMPORT_NORMAL);
	nvlist_free(props);
	return (ret);
}

static void
print_vdev_tree(libzfs_handle_t *hdl, const char *name, nvlist_t *nv,
    int indent)
{
	nvlist_t **child;
	uint_t c, children;
	char *vname;
	uint64_t is_log = 0;

	(void) nvlist_lookup_uint64(nv, ZPOOL_CONFIG_IS_LOG,
	    &is_log);

	if (name != NULL)
		(void) printf("\t%*s%s%s\n", indent, "", name,
		    is_log ? " [log]" : "");

	if (nvlist_lookup_nvlist_array(nv, ZPOOL_CONFIG_CHILDREN,
	    &child, &children) != 0)
		return;

	for (c = 0; c < children; c++) {
		vname = zpool_vdev_name(hdl, NULL, child[c], VDEV_NAME_TYPE_ID);
		print_vdev_tree(hdl, vname, child[c], indent + 2);
		free(vname);
	}
}

void
zpool_print_unsup_feat(nvlist_t *config)
{
	nvlist_t *nvinfo, *unsup_feat;
	nvpair_t *nvp;

	verify(nvlist_lookup_nvlist(config, ZPOOL_CONFIG_LOAD_INFO, &nvinfo) ==
	    0);
	verify(nvlist_lookup_nvlist(nvinfo, ZPOOL_CONFIG_UNSUP_FEAT,
	    &unsup_feat) == 0);

	for (nvp = nvlist_next_nvpair(unsup_feat, NULL); nvp != NULL;
	    nvp = nvlist_next_nvpair(unsup_feat, nvp)) {
		char *desc;

		verify(nvpair_type(nvp) == DATA_TYPE_STRING);
		verify(nvpair_value_string(nvp, &desc) == 0);

		if (strlen(desc) > 0)
			(void) printf("\t%s (%s)\n", nvpair_name(nvp), desc);
		else
			(void) printf("\t%s\n", nvpair_name(nvp));
	}
}

/*
 * Import the given pool using the known configuration and a list of
 * properties to be set. The configuration should have come from
 * zpool_find_import(). The 'newname' parameters control whether the pool
 * is imported with a different name.
 */
int
zpool_import_props(libzfs_handle_t *hdl, nvlist_t *config, const char *newname,
    nvlist_t *props, int flags)
{
	zfs_cmd_t zc = {"\0"};
	zpool_load_policy_t policy;
	nvlist_t *nv = NULL;
	nvlist_t *nvinfo = NULL;
	nvlist_t *missing = NULL;
	char *thename;
	char *origname;
	int ret;
	int error = 0;
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

	if (props != NULL) {
		uint64_t version;
		prop_flags_t flags = { .create = B_FALSE, .import = B_TRUE };

		verify(nvlist_lookup_uint64(config, ZPOOL_CONFIG_VERSION,
		    &version) == 0);

		if ((props = zpool_valid_proplist(hdl, origname,
		    props, version, flags, errbuf)) == NULL)
			return (-1);
		if (zcmd_write_src_nvlist(hdl, &zc, props) != 0) {
			nvlist_free(props);
			return (-1);
		}
		nvlist_free(props);
	}

	(void) strlcpy(zc.zc_name, thename, sizeof (zc.zc_name));

	verify(nvlist_lookup_uint64(config, ZPOOL_CONFIG_POOL_GUID,
	    &zc.zc_guid) == 0);

	if (zcmd_write_conf_nvlist(hdl, &zc, config) != 0) {
		zcmd_free_nvlists(&zc);
		return (-1);
	}
	if (zcmd_alloc_dst_nvlist(hdl, &zc, zc.zc_nvlist_conf_size * 2) != 0) {
		zcmd_free_nvlists(&zc);
		return (-1);
	}

	zc.zc_cookie = flags;
	while ((ret = zfs_ioctl(hdl, ZFS_IOC_POOL_IMPORT, &zc)) != 0 &&
	    errno == ENOMEM) {
		if (zcmd_expand_dst_nvlist(hdl, &zc) != 0) {
			zcmd_free_nvlists(&zc);
			return (-1);
		}
	}
	if (ret != 0)
		error = errno;

	(void) zcmd_read_dst_nvlist(hdl, &zc, &nv);

	zcmd_free_nvlists(&zc);

	zpool_get_load_policy(config, &policy);

	if (error) {
		char desc[1024];
		char aux[256];

		/*
		 * Dry-run failed, but we print out what success
		 * looks like if we found a best txg
		 */
		if (policy.zlp_rewind & ZPOOL_TRY_REWIND) {
			zpool_rewind_exclaim(hdl, newname ? origname : thename,
			    B_TRUE, nv);
			nvlist_free(nv);
			return (-1);
		}

		if (newname == NULL)
			(void) snprintf(desc, sizeof (desc),
			    dgettext(TEXT_DOMAIN, "cannot import '%s'"),
			    thename);
		else
			(void) snprintf(desc, sizeof (desc),
			    dgettext(TEXT_DOMAIN, "cannot import '%s' as '%s'"),
			    origname, thename);

		switch (error) {
		case ENOTSUP:
			if (nv != NULL && nvlist_lookup_nvlist(nv,
			    ZPOOL_CONFIG_LOAD_INFO, &nvinfo) == 0 &&
			    nvlist_exists(nvinfo, ZPOOL_CONFIG_UNSUP_FEAT)) {
				(void) printf(dgettext(TEXT_DOMAIN, "This "
				    "pool uses the following feature(s) not "
				    "supported by this system:\n"));
				zpool_print_unsup_feat(nv);
				if (nvlist_exists(nvinfo,
				    ZPOOL_CONFIG_CAN_RDONLY)) {
					(void) printf(dgettext(TEXT_DOMAIN,
					    "All unsupported features are only "
					    "required for writing to the pool."
					    "\nThe pool can be imported using "
					    "'-o readonly=on'.\n"));
				}
			}
			/*
			 * Unsupported version.
			 */
			(void) zfs_error(hdl, EZFS_BADVERSION, desc);
			break;

		case EREMOTEIO:
			if (nv != NULL && nvlist_lookup_nvlist(nv,
			    ZPOOL_CONFIG_LOAD_INFO, &nvinfo) == 0) {
				char *hostname = "<unknown>";
				uint64_t hostid = 0;
				mmp_state_t mmp_state;

				mmp_state = fnvlist_lookup_uint64(nvinfo,
				    ZPOOL_CONFIG_MMP_STATE);

				if (nvlist_exists(nvinfo,
				    ZPOOL_CONFIG_MMP_HOSTNAME))
					hostname = fnvlist_lookup_string(nvinfo,
					    ZPOOL_CONFIG_MMP_HOSTNAME);

				if (nvlist_exists(nvinfo,
				    ZPOOL_CONFIG_MMP_HOSTID))
					hostid = fnvlist_lookup_uint64(nvinfo,
					    ZPOOL_CONFIG_MMP_HOSTID);

				if (mmp_state == MMP_STATE_ACTIVE) {
					(void) snprintf(aux, sizeof (aux),
					    dgettext(TEXT_DOMAIN, "pool is imp"
					    "orted on host '%s' (hostid=%lx).\n"
					    "Export the pool on the other "
					    "system, then run 'zpool import'."),
					    hostname, (unsigned long) hostid);
				} else if (mmp_state == MMP_STATE_NO_HOSTID) {
					(void) snprintf(aux, sizeof (aux),
					    dgettext(TEXT_DOMAIN, "pool has "
					    "the multihost property on and "
					    "the\nsystem's hostid is not set. "
					    "Set a unique system hostid with "
					    "the zgenhostid(8) command.\n"));
				}

				(void) zfs_error_aux(hdl, "%s", aux);
			}
			(void) zfs_error(hdl, EZFS_ACTIVE_POOL, desc);
			break;

		case EINVAL:
			(void) zfs_error(hdl, EZFS_INVALCONFIG, desc);
			break;

		case EROFS:
			zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
			    "one or more devices is read only"));
			(void) zfs_error(hdl, EZFS_BADDEV, desc);
			break;

		case ENXIO:
			if (nv && nvlist_lookup_nvlist(nv,
			    ZPOOL_CONFIG_LOAD_INFO, &nvinfo) == 0 &&
			    nvlist_lookup_nvlist(nvinfo,
			    ZPOOL_CONFIG_MISSING_DEVICES, &missing) == 0) {
				(void) printf(dgettext(TEXT_DOMAIN,
				    "The devices below are missing or "
				    "corrupted, use '-m' to import the pool "
				    "anyway:\n"));
				print_vdev_tree(hdl, NULL, missing, 2);
				(void) printf("\n");
			}
			(void) zpool_standard_error(hdl, error, desc);
			break;

		case EEXIST:
			(void) zpool_standard_error(hdl, error, desc);
			break;

		case EBUSY:
			zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
			    "one or more devices are already in use\n"));
			(void) zfs_error(hdl, EZFS_BADDEV, desc);
			break;
		case ENAMETOOLONG:
			zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
			    "new name of at least one dataset is longer than "
			    "the maximum allowable length"));
			(void) zfs_error(hdl, EZFS_NAMETOOLONG, desc);
			break;
		default:
			(void) zpool_standard_error(hdl, error, desc);
			zpool_explain_recover(hdl,
			    newname ? origname : thename, -error, nv);
			break;
		}

		nvlist_free(nv);
		ret = -1;
	} else {
		zpool_handle_t *zhp;

		/*
		 * This should never fail, but play it safe anyway.
		 */
		if (zpool_open_silent(hdl, thename, &zhp) != 0)
			ret = -1;
		else if (zhp != NULL)
			zpool_close(zhp);
		if (policy.zlp_rewind &
		    (ZPOOL_DO_REWIND | ZPOOL_TRY_REWIND)) {
			zpool_rewind_exclaim(hdl, newname ? origname : thename,
			    ((policy.zlp_rewind & ZPOOL_TRY_REWIND) != 0), nv);
		}
		nvlist_free(nv);
		return (0);
	}

	return (ret);
}

/*
 * Translate vdev names to guids.  If a vdev_path is determined to be
 * unsuitable then a vd_errlist is allocated and the vdev path and errno
 * are added to it.
 */
static int
zpool_translate_vdev_guids(zpool_handle_t *zhp, nvlist_t *vds,
    nvlist_t *vdev_guids, nvlist_t *guids_to_paths, nvlist_t **vd_errlist)
{
	nvlist_t *errlist = NULL;
	int error = 0;

	for (nvpair_t *elem = nvlist_next_nvpair(vds, NULL); elem != NULL;
	    elem = nvlist_next_nvpair(vds, elem)) {
		boolean_t spare, cache;

		char *vd_path = nvpair_name(elem);
		nvlist_t *tgt = zpool_find_vdev(zhp, vd_path, &spare, &cache,
		    NULL);

		if ((tgt == NULL) || cache || spare) {
			if (errlist == NULL) {
				errlist = fnvlist_alloc();
				error = EINVAL;
			}

			uint64_t err = (tgt == NULL) ? EZFS_NODEVICE :
			    (spare ? EZFS_ISSPARE : EZFS_ISL2CACHE);
			fnvlist_add_int64(errlist, vd_path, err);
			continue;
		}

		uint64_t guid = fnvlist_lookup_uint64(tgt, ZPOOL_CONFIG_GUID);
		fnvlist_add_uint64(vdev_guids, vd_path, guid);

		char msg[MAXNAMELEN];
		(void) snprintf(msg, sizeof (msg), "%llu", (u_longlong_t)guid);
		fnvlist_add_string(guids_to_paths, msg, vd_path);
	}

	if (error != 0) {
		verify(errlist != NULL);
		if (vd_errlist != NULL)
			*vd_errlist = errlist;
		else
			fnvlist_free(errlist);
	}

	return (error);
}

static int
xlate_init_err(int err)
{
	switch (err) {
	case ENODEV:
		return (EZFS_NODEVICE);
	case EINVAL:
	case EROFS:
		return (EZFS_BADDEV);
	case EBUSY:
		return (EZFS_INITIALIZING);
	case ESRCH:
		return (EZFS_NO_INITIALIZE);
	}
	return (err);
}

/*
 * Begin, suspend, or cancel the initialization (initializing of all free
 * blocks) for the given vdevs in the given pool.
 */
static int
zpool_initialize_impl(zpool_handle_t *zhp, pool_initialize_func_t cmd_type,
    nvlist_t *vds, boolean_t wait)
{
	int err;

	nvlist_t *vdev_guids = fnvlist_alloc();
	nvlist_t *guids_to_paths = fnvlist_alloc();
	nvlist_t *vd_errlist = NULL;
	nvlist_t *errlist;
	nvpair_t *elem;

	err = zpool_translate_vdev_guids(zhp, vds, vdev_guids,
	    guids_to_paths, &vd_errlist);

	if (err != 0) {
		verify(vd_errlist != NULL);
		goto list_errors;
	}

	err = lzc_initialize(zhp->zpool_name, cmd_type,
	    vdev_guids, &errlist);

	if (err != 0) {
		if (errlist != NULL) {
			vd_errlist = fnvlist_lookup_nvlist(errlist,
			    ZPOOL_INITIALIZE_VDEVS);
			goto list_errors;
		}
		(void) zpool_standard_error(zhp->zpool_hdl, err,
		    dgettext(TEXT_DOMAIN, "operation failed"));
		goto out;
	}

	if (wait) {
		for (elem = nvlist_next_nvpair(vdev_guids, NULL); elem != NULL;
		    elem = nvlist_next_nvpair(vdev_guids, elem)) {

			uint64_t guid = fnvpair_value_uint64(elem);

			err = lzc_wait_tag(zhp->zpool_name,
			    ZPOOL_WAIT_INITIALIZE, guid, NULL);
			if (err != 0) {
				(void) zpool_standard_error_fmt(zhp->zpool_hdl,
				    err, dgettext(TEXT_DOMAIN, "error "
				    "waiting for '%s' to initialize"),
				    nvpair_name(elem));

				goto out;
			}
		}
	}
	goto out;

list_errors:
	for (elem = nvlist_next_nvpair(vd_errlist, NULL); elem != NULL;
	    elem = nvlist_next_nvpair(vd_errlist, elem)) {
		int64_t vd_error = xlate_init_err(fnvpair_value_int64(elem));
		char *path;

		if (nvlist_lookup_string(guids_to_paths, nvpair_name(elem),
		    &path) != 0)
			path = nvpair_name(elem);

		(void) zfs_error_fmt(zhp->zpool_hdl, vd_error,
		    "cannot initialize '%s'", path);
	}

out:
	fnvlist_free(vdev_guids);
	fnvlist_free(guids_to_paths);

	if (vd_errlist != NULL)
		fnvlist_free(vd_errlist);

	return (err == 0 ? 0 : -1);
}

int
zpool_initialize(zpool_handle_t *zhp, pool_initialize_func_t cmd_type,
    nvlist_t *vds)
{
	return (zpool_initialize_impl(zhp, cmd_type, vds, B_FALSE));
}

int
zpool_initialize_wait(zpool_handle_t *zhp, pool_initialize_func_t cmd_type,
    nvlist_t *vds)
{
	return (zpool_initialize_impl(zhp, cmd_type, vds, B_TRUE));
}

static int
xlate_trim_err(int err)
{
	switch (err) {
	case ENODEV:
		return (EZFS_NODEVICE);
	case EINVAL:
	case EROFS:
		return (EZFS_BADDEV);
	case EBUSY:
		return (EZFS_TRIMMING);
	case ESRCH:
		return (EZFS_NO_TRIM);
	case EOPNOTSUPP:
		return (EZFS_TRIM_NOTSUP);
	}
	return (err);
}

static int
zpool_trim_wait(zpool_handle_t *zhp, nvlist_t *vdev_guids)
{
	int err;
	nvpair_t *elem;

	for (elem = nvlist_next_nvpair(vdev_guids, NULL); elem != NULL;
	    elem = nvlist_next_nvpair(vdev_guids, elem)) {

		uint64_t guid = fnvpair_value_uint64(elem);

		err = lzc_wait_tag(zhp->zpool_name,
		    ZPOOL_WAIT_TRIM, guid, NULL);
		if (err != 0) {
			(void) zpool_standard_error_fmt(zhp->zpool_hdl,
			    err, dgettext(TEXT_DOMAIN, "error "
			    "waiting to trim '%s'"), nvpair_name(elem));

			return (err);
		}
	}
	return (0);
}

/*
 * Check errlist and report any errors, omitting ones which should be
 * suppressed. Returns B_TRUE if any errors were reported.
 */
static boolean_t
check_trim_errs(zpool_handle_t *zhp, trimflags_t *trim_flags,
    nvlist_t *guids_to_paths, nvlist_t *vds, nvlist_t *errlist)
{
	nvpair_t *elem;
	boolean_t reported_errs = B_FALSE;
	int num_vds = 0;
	int num_suppressed_errs = 0;

	for (elem = nvlist_next_nvpair(vds, NULL);
	    elem != NULL; elem = nvlist_next_nvpair(vds, elem)) {
		num_vds++;
	}

	for (elem = nvlist_next_nvpair(errlist, NULL);
	    elem != NULL; elem = nvlist_next_nvpair(errlist, elem)) {
		int64_t vd_error = xlate_trim_err(fnvpair_value_int64(elem));
		char *path;

		/*
		 * If only the pool was specified, and it was not a secure
		 * trim then suppress warnings for individual vdevs which
		 * do not support trimming.
		 */
		if (vd_error == EZFS_TRIM_NOTSUP &&
		    trim_flags->fullpool &&
		    !trim_flags->secure) {
			num_suppressed_errs++;
			continue;
		}

		reported_errs = B_TRUE;
		if (nvlist_lookup_string(guids_to_paths, nvpair_name(elem),
		    &path) != 0)
			path = nvpair_name(elem);

		(void) zfs_error_fmt(zhp->zpool_hdl, vd_error,
		    "cannot trim '%s'", path);
	}

	if (num_suppressed_errs == num_vds) {
		(void) zfs_error_aux(zhp->zpool_hdl, dgettext(TEXT_DOMAIN,
		    "no devices in pool support trim operations"));
		(void) (zfs_error(zhp->zpool_hdl, EZFS_TRIM_NOTSUP,
		    dgettext(TEXT_DOMAIN, "cannot trim")));
		reported_errs = B_TRUE;
	}

	return (reported_errs);
}

/*
 * Begin, suspend, or cancel the TRIM (discarding of all free blocks) for
 * the given vdevs in the given pool.
 */
int
zpool_trim(zpool_handle_t *zhp, pool_trim_func_t cmd_type, nvlist_t *vds,
    trimflags_t *trim_flags)
{
	int err;
	int retval = 0;

	nvlist_t *vdev_guids = fnvlist_alloc();
	nvlist_t *guids_to_paths = fnvlist_alloc();
	nvlist_t *errlist = NULL;

	err = zpool_translate_vdev_guids(zhp, vds, vdev_guids,
	    guids_to_paths, &errlist);
	if (err != 0) {
		check_trim_errs(zhp, trim_flags, guids_to_paths, vds, errlist);
		retval = -1;
		goto out;
	}

	err = lzc_trim(zhp->zpool_name, cmd_type, trim_flags->rate,
	    trim_flags->secure, vdev_guids, &errlist);
	if (err != 0) {
		nvlist_t *vd_errlist;
		if (errlist != NULL && nvlist_lookup_nvlist(errlist,
		    ZPOOL_TRIM_VDEVS, &vd_errlist) == 0) {
			if (check_trim_errs(zhp, trim_flags, guids_to_paths,
			    vds, vd_errlist)) {
				retval = -1;
				goto out;
			}
		} else {
			char msg[1024];

			(void) snprintf(msg, sizeof (msg),
			    dgettext(TEXT_DOMAIN, "operation failed"));
			zpool_standard_error(zhp->zpool_hdl, err, msg);
			retval = -1;
			goto out;
		}
	}


	if (trim_flags->wait)
		retval = zpool_trim_wait(zhp, vdev_guids);

out:
	if (errlist != NULL)
		fnvlist_free(errlist);
	fnvlist_free(vdev_guids);
	fnvlist_free(guids_to_paths);
	return (retval);
}

/*
 * Scan the pool.
 */
int
zpool_scan(zpool_handle_t *zhp, pool_scan_func_t func, pool_scrub_cmd_t cmd)
{
	zfs_cmd_t zc = {"\0"};
	char msg[1024];
	int err;
	libzfs_handle_t *hdl = zhp->zpool_hdl;

	(void) strlcpy(zc.zc_name, zhp->zpool_name, sizeof (zc.zc_name));
	zc.zc_cookie = func;
	zc.zc_flags = cmd;

	if (zfs_ioctl(hdl, ZFS_IOC_POOL_SCAN, &zc) == 0)
		return (0);

	err = errno;

	/* ECANCELED on a scrub means we resumed a paused scrub */
	if (err == ECANCELED && func == POOL_SCAN_SCRUB &&
	    cmd == POOL_SCRUB_NORMAL)
		return (0);

	if (err == ENOENT && func != POOL_SCAN_NONE && cmd == POOL_SCRUB_NORMAL)
		return (0);

	if (func == POOL_SCAN_SCRUB) {
		if (cmd == POOL_SCRUB_PAUSE) {
			(void) snprintf(msg, sizeof (msg), dgettext(TEXT_DOMAIN,
			    "cannot pause scrubbing %s"), zc.zc_name);
		} else {
			assert(cmd == POOL_SCRUB_NORMAL);
			(void) snprintf(msg, sizeof (msg), dgettext(TEXT_DOMAIN,
			    "cannot scrub %s"), zc.zc_name);
		}
	} else if (func == POOL_SCAN_RESILVER) {
		assert(cmd == POOL_SCRUB_NORMAL);
		(void) snprintf(msg, sizeof (msg), dgettext(TEXT_DOMAIN,
		    "cannot restart resilver on %s"), zc.zc_name);
	} else if (func == POOL_SCAN_NONE) {
		(void) snprintf(msg, sizeof (msg),
		    dgettext(TEXT_DOMAIN, "cannot cancel scrubbing %s"),
		    zc.zc_name);
	} else {
		assert(!"unexpected result");
	}

	if (err == EBUSY) {
		nvlist_t *nvroot;
		pool_scan_stat_t *ps = NULL;
		uint_t psc;

		verify(nvlist_lookup_nvlist(zhp->zpool_config,
		    ZPOOL_CONFIG_VDEV_TREE, &nvroot) == 0);
		(void) nvlist_lookup_uint64_array(nvroot,
		    ZPOOL_CONFIG_SCAN_STATS, (uint64_t **)&ps, &psc);
		if (ps && ps->pss_func == POOL_SCAN_SCRUB &&
		    ps->pss_state == DSS_SCANNING) {
			if (cmd == POOL_SCRUB_PAUSE)
				return (zfs_error(hdl, EZFS_SCRUB_PAUSED, msg));
			else
				return (zfs_error(hdl, EZFS_SCRUBBING, msg));
		} else {
			return (zfs_error(hdl, EZFS_RESILVERING, msg));
		}
	} else if (err == ENOENT) {
		return (zfs_error(hdl, EZFS_NO_SCRUB, msg));
	} else if (err == ENOTSUP && func == POOL_SCAN_RESILVER) {
		return (zfs_error(hdl, EZFS_NO_RESILVER_DEFER, msg));
	} else {
		return (zpool_standard_error(hdl, err, msg));
	}
}

/*
 * Find a vdev that matches the search criteria specified. We use the
 * the nvpair name to determine how we should look for the device.
 * 'avail_spare' is set to TRUE if the provided guid refers to an AVAIL
 * spare; but FALSE if its an INUSE spare.
 */
static nvlist_t *
vdev_to_nvlist_iter(nvlist_t *nv, nvlist_t *search, boolean_t *avail_spare,
    boolean_t *l2cache, boolean_t *log)
{
	uint_t c, children;
	nvlist_t **child;
	nvlist_t *ret;
	uint64_t is_log;
	char *srchkey;
	nvpair_t *pair = nvlist_next_nvpair(search, NULL);

	/* Nothing to look for */
	if (search == NULL || pair == NULL)
		return (NULL);

	/* Obtain the key we will use to search */
	srchkey = nvpair_name(pair);

	switch (nvpair_type(pair)) {
	case DATA_TYPE_UINT64:
		if (strcmp(srchkey, ZPOOL_CONFIG_GUID) == 0) {
			uint64_t srchval, theguid;

			verify(nvpair_value_uint64(pair, &srchval) == 0);
			verify(nvlist_lookup_uint64(nv, ZPOOL_CONFIG_GUID,
			    &theguid) == 0);
			if (theguid == srchval)
				return (nv);
		}
		break;

	case DATA_TYPE_STRING: {
		char *srchval, *val;

		verify(nvpair_value_string(pair, &srchval) == 0);
		if (nvlist_lookup_string(nv, srchkey, &val) != 0)
			break;

		/*
		 * Search for the requested value. Special cases:
		 *
		 * - ZPOOL_CONFIG_PATH for whole disk entries.  These end in
		 *   "-part1", or "p1".  The suffix is hidden from the user,
		 *   but included in the string, so this matches around it.
		 * - ZPOOL_CONFIG_PATH for short names zfs_strcmp_shortname()
		 *   is used to check all possible expanded paths.
		 * - looking for a top-level vdev name (i.e. ZPOOL_CONFIG_TYPE).
		 *
		 * Otherwise, all other searches are simple string compares.
		 */
		if (strcmp(srchkey, ZPOOL_CONFIG_PATH) == 0) {
			uint64_t wholedisk = 0;

			(void) nvlist_lookup_uint64(nv, ZPOOL_CONFIG_WHOLE_DISK,
			    &wholedisk);
			if (zfs_strcmp_pathname(srchval, val, wholedisk) == 0)
				return (nv);

		} else if (strcmp(srchkey, ZPOOL_CONFIG_TYPE) == 0 && val) {
			char *type, *idx, *end, *p;
			uint64_t id, vdev_id;

			/*
			 * Determine our vdev type, keeping in mind
			 * that the srchval is composed of a type and
			 * vdev id pair (i.e. mirror-4).
			 */
			if ((type = strdup(srchval)) == NULL)
				return (NULL);

			if ((p = strrchr(type, '-')) == NULL) {
				free(type);
				break;
			}
			idx = p + 1;
			*p = '\0';

			/*
			 * If the types don't match then keep looking.
			 */
			if (strncmp(val, type, strlen(val)) != 0) {
				free(type);
				break;
			}

			verify(zpool_vdev_is_interior(type));
			verify(nvlist_lookup_uint64(nv, ZPOOL_CONFIG_ID,
			    &id) == 0);

			errno = 0;
			vdev_id = strtoull(idx, &end, 10);

			/*
			 * If we are looking for a raidz and a parity is
			 * specified, make sure it matches.
			 */
			int rzlen = strlen(VDEV_TYPE_RAIDZ);
			assert(rzlen == strlen(VDEV_TYPE_DRAID));
			int typlen = strlen(type);
			if ((strncmp(type, VDEV_TYPE_RAIDZ, rzlen) == 0 ||
			    strncmp(type, VDEV_TYPE_DRAID, rzlen) == 0) &&
			    typlen != rzlen) {
				uint64_t vdev_parity;
				int parity = *(type + rzlen) - '0';

				if (parity <= 0 || parity > 3 ||
				    (typlen - rzlen) != 1) {
					/*
					 * Nonsense parity specified, can
					 * never match
					 */
					free(type);
					return (NULL);
				}
				verify(nvlist_lookup_uint64(nv,
				    ZPOOL_CONFIG_NPARITY, &vdev_parity) == 0);
				if ((int)vdev_parity != parity) {
					free(type);
					break;
				}
			}

			free(type);
			if (errno != 0)
				return (NULL);

			/*
			 * Now verify that we have the correct vdev id.
			 */
			if (vdev_id == id)
				return (nv);
		}

		/*
		 * Common case
		 */
		if (strcmp(srchval, val) == 0)
			return (nv);
		break;
	}

	default:
		break;
	}

	if (nvlist_lookup_nvlist_array(nv, ZPOOL_CONFIG_CHILDREN,
	    &child, &children) != 0)
		return (NULL);

	for (c = 0; c < children; c++) {
		if ((ret = vdev_to_nvlist_iter(child[c], search,
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
			if ((ret = vdev_to_nvlist_iter(child[c], search,
			    avail_spare, l2cache, NULL)) != NULL) {
				*avail_spare = B_TRUE;
				return (ret);
			}
		}
	}

	if (nvlist_lookup_nvlist_array(nv, ZPOOL_CONFIG_L2CACHE,
	    &child, &children) == 0) {
		for (c = 0; c < children; c++) {
			if ((ret = vdev_to_nvlist_iter(child[c], search,
			    avail_spare, l2cache, NULL)) != NULL) {
				*l2cache = B_TRUE;
				return (ret);
			}
		}
	}

	return (NULL);
}

/*
 * Given a physical path or guid, find the associated vdev.
 */
nvlist_t *
zpool_find_vdev_by_physpath(zpool_handle_t *zhp, const char *ppath,
    boolean_t *avail_spare, boolean_t *l2cache, boolean_t *log)
{
	nvlist_t *search, *nvroot, *ret;
	uint64_t guid;
	char *end;

	verify(nvlist_alloc(&search, NV_UNIQUE_NAME, KM_SLEEP) == 0);

	guid = strtoull(ppath, &end, 0);
	if (guid != 0 && *end == '\0') {
		verify(nvlist_add_uint64(search, ZPOOL_CONFIG_GUID, guid) == 0);
	} else {
		verify(nvlist_add_string(search, ZPOOL_CONFIG_PHYS_PATH,
		    ppath) == 0);
	}

	verify(nvlist_lookup_nvlist(zhp->zpool_config, ZPOOL_CONFIG_VDEV_TREE,
	    &nvroot) == 0);

	*avail_spare = B_FALSE;
	*l2cache = B_FALSE;
	if (log != NULL)
		*log = B_FALSE;
	ret = vdev_to_nvlist_iter(nvroot, search, avail_spare, l2cache, log);
	nvlist_free(search);

	return (ret);
}

/*
 * Determine if we have an "interior" top-level vdev (i.e mirror/raidz).
 */
static boolean_t
zpool_vdev_is_interior(const char *name)
{
	if (strncmp(name, VDEV_TYPE_RAIDZ, strlen(VDEV_TYPE_RAIDZ)) == 0 ||
	    strncmp(name, VDEV_TYPE_SPARE, strlen(VDEV_TYPE_SPARE)) == 0 ||
	    strncmp(name,
	    VDEV_TYPE_REPLACING, strlen(VDEV_TYPE_REPLACING)) == 0 ||
	    strncmp(name, VDEV_TYPE_MIRROR, strlen(VDEV_TYPE_MIRROR)) == 0)
		return (B_TRUE);

	if (strncmp(name, VDEV_TYPE_DRAID, strlen(VDEV_TYPE_DRAID)) == 0 &&
	    !zpool_is_draid_spare(name))
		return (B_TRUE);

	return (B_FALSE);
}

nvlist_t *
zpool_find_vdev(zpool_handle_t *zhp, const char *path, boolean_t *avail_spare,
    boolean_t *l2cache, boolean_t *log)
{
	char *end;
	nvlist_t *nvroot, *search, *ret;
	uint64_t guid;

	verify(nvlist_alloc(&search, NV_UNIQUE_NAME, KM_SLEEP) == 0);

	guid = strtoull(path, &end, 0);
	if (guid != 0 && *end == '\0') {
		verify(nvlist_add_uint64(search, ZPOOL_CONFIG_GUID, guid) == 0);
	} else if (zpool_vdev_is_interior(path)) {
		verify(nvlist_add_string(search, ZPOOL_CONFIG_TYPE, path) == 0);
	} else {
		verify(nvlist_add_string(search, ZPOOL_CONFIG_PATH, path) == 0);
	}

	verify(nvlist_lookup_nvlist(zhp->zpool_config, ZPOOL_CONFIG_VDEV_TREE,
	    &nvroot) == 0);

	*avail_spare = B_FALSE;
	*l2cache = B_FALSE;
	if (log != NULL)
		*log = B_FALSE;
	ret = vdev_to_nvlist_iter(nvroot, search, avail_spare, l2cache, log);
	nvlist_free(search);

	return (ret);
}

static int
vdev_is_online(nvlist_t *nv)
{
	uint64_t ival;

	if (nvlist_lookup_uint64(nv, ZPOOL_CONFIG_OFFLINE, &ival) == 0 ||
	    nvlist_lookup_uint64(nv, ZPOOL_CONFIG_FAULTED, &ival) == 0 ||
	    nvlist_lookup_uint64(nv, ZPOOL_CONFIG_REMOVED, &ival) == 0)
		return (0);

	return (1);
}

/*
 * Helper function for zpool_get_physpaths().
 */
static int
vdev_get_one_physpath(nvlist_t *config, char *physpath, size_t physpath_size,
    size_t *bytes_written)
{
	size_t bytes_left, pos, rsz;
	char *tmppath;
	const char *format;

	if (nvlist_lookup_string(config, ZPOOL_CONFIG_PHYS_PATH,
	    &tmppath) != 0)
		return (EZFS_NODEVICE);

	pos = *bytes_written;
	bytes_left = physpath_size - pos;
	format = (pos == 0) ? "%s" : " %s";

	rsz = snprintf(physpath + pos, bytes_left, format, tmppath);
	*bytes_written += rsz;

	if (rsz >= bytes_left) {
		/* if physpath was not copied properly, clear it */
		if (bytes_left != 0) {
			physpath[pos] = 0;
		}
		return (EZFS_NOSPC);
	}
	return (0);
}

static int
vdev_get_physpaths(nvlist_t *nv, char *physpath, size_t phypath_size,
    size_t *rsz, boolean_t is_spare)
{
	char *type;
	int ret;

	if (nvlist_lookup_string(nv, ZPOOL_CONFIG_TYPE, &type) != 0)
		return (EZFS_INVALCONFIG);

	if (strcmp(type, VDEV_TYPE_DISK) == 0) {
		/*
		 * An active spare device has ZPOOL_CONFIG_IS_SPARE set.
		 * For a spare vdev, we only want to boot from the active
		 * spare device.
		 */
		if (is_spare) {
			uint64_t spare = 0;
			(void) nvlist_lookup_uint64(nv, ZPOOL_CONFIG_IS_SPARE,
			    &spare);
			if (!spare)
				return (EZFS_INVALCONFIG);
		}

		if (vdev_is_online(nv)) {
			if ((ret = vdev_get_one_physpath(nv, physpath,
			    phypath_size, rsz)) != 0)
				return (ret);
		}
	} else if (strcmp(type, VDEV_TYPE_MIRROR) == 0 ||
	    strcmp(type, VDEV_TYPE_RAIDZ) == 0 ||
	    strcmp(type, VDEV_TYPE_REPLACING) == 0 ||
	    (is_spare = (strcmp(type, VDEV_TYPE_SPARE) == 0))) {
		nvlist_t **child;
		uint_t count;
		int i, ret;

		if (nvlist_lookup_nvlist_array(nv,
		    ZPOOL_CONFIG_CHILDREN, &child, &count) != 0)
			return (EZFS_INVALCONFIG);

		for (i = 0; i < count; i++) {
			ret = vdev_get_physpaths(child[i], physpath,
			    phypath_size, rsz, is_spare);
			if (ret == EZFS_NOSPC)
				return (ret);
		}
	}

	return (EZFS_POOL_INVALARG);
}

/*
 * Get phys_path for a root pool config.
 * Return 0 on success; non-zero on failure.
 */
static int
zpool_get_config_physpath(nvlist_t *config, char *physpath, size_t phypath_size)
{
	size_t rsz;
	nvlist_t *vdev_root;
	nvlist_t **child;
	uint_t count;
	char *type;

	rsz = 0;

	if (nvlist_lookup_nvlist(config, ZPOOL_CONFIG_VDEV_TREE,
	    &vdev_root) != 0)
		return (EZFS_INVALCONFIG);

	if (nvlist_lookup_string(vdev_root, ZPOOL_CONFIG_TYPE, &type) != 0 ||
	    nvlist_lookup_nvlist_array(vdev_root, ZPOOL_CONFIG_CHILDREN,
	    &child, &count) != 0)
		return (EZFS_INVALCONFIG);

	/*
	 * root pool can only have a single top-level vdev.
	 */
	if (strcmp(type, VDEV_TYPE_ROOT) != 0 || count != 1)
		return (EZFS_POOL_INVALARG);

	(void) vdev_get_physpaths(child[0], physpath, phypath_size, &rsz,
	    B_FALSE);

	/* No online devices */
	if (rsz == 0)
		return (EZFS_NODEVICE);

	return (0);
}

/*
 * Get phys_path for a root pool
 * Return 0 on success; non-zero on failure.
 */
int
zpool_get_physpath(zpool_handle_t *zhp, char *physpath, size_t phypath_size)
{
	return (zpool_get_config_physpath(zhp->zpool_config, physpath,
	    phypath_size));
}

/*
 * Convert a vdev path to a GUID.  Returns GUID or 0 on error.
 *
 * If is_spare, is_l2cache, or is_log is non-NULL, then store within it
 * if the VDEV is a spare, l2cache, or log device.  If they're NULL then
 * ignore them.
 */
static uint64_t
zpool_vdev_path_to_guid_impl(zpool_handle_t *zhp, const char *path,
    boolean_t *is_spare, boolean_t *is_l2cache, boolean_t *is_log)
{
	uint64_t guid;
	boolean_t spare = B_FALSE, l2cache = B_FALSE, log = B_FALSE;
	nvlist_t *tgt;

	if ((tgt = zpool_find_vdev(zhp, path, &spare, &l2cache,
	    &log)) == NULL)
		return (0);

	verify(nvlist_lookup_uint64(tgt, ZPOOL_CONFIG_GUID, &guid) == 0);
	if (is_spare != NULL)
		*is_spare = spare;
	if (is_l2cache != NULL)
		*is_l2cache = l2cache;
	if (is_log != NULL)
		*is_log = log;

	return (guid);
}

/* Convert a vdev path to a GUID.  Returns GUID or 0 on error. */
uint64_t
zpool_vdev_path_to_guid(zpool_handle_t *zhp, const char *path)
{
	return (zpool_vdev_path_to_guid_impl(zhp, path, NULL, NULL, NULL));
}

/*
 * Bring the specified vdev online.   The 'flags' parameter is a set of the
 * ZFS_ONLINE_* flags.
 */
int
zpool_vdev_online(zpool_handle_t *zhp, const char *path, int flags,
    vdev_state_t *newstate)
{
	zfs_cmd_t zc = {"\0"};
	char msg[1024];
	char *pathname;
	nvlist_t *tgt;
	boolean_t avail_spare, l2cache, islog;
	libzfs_handle_t *hdl = zhp->zpool_hdl;
	int error;

	if (flags & ZFS_ONLINE_EXPAND) {
		(void) snprintf(msg, sizeof (msg),
		    dgettext(TEXT_DOMAIN, "cannot expand %s"), path);
	} else {
		(void) snprintf(msg, sizeof (msg),
		    dgettext(TEXT_DOMAIN, "cannot online %s"), path);
	}

	(void) strlcpy(zc.zc_name, zhp->zpool_name, sizeof (zc.zc_name));
	if ((tgt = zpool_find_vdev(zhp, path, &avail_spare, &l2cache,
	    &islog)) == NULL)
		return (zfs_error(hdl, EZFS_NODEVICE, msg));

	verify(nvlist_lookup_uint64(tgt, ZPOOL_CONFIG_GUID, &zc.zc_guid) == 0);

	if (avail_spare)
		return (zfs_error(hdl, EZFS_ISSPARE, msg));

	if ((flags & ZFS_ONLINE_EXPAND ||
	    zpool_get_prop_int(zhp, ZPOOL_PROP_AUTOEXPAND, NULL)) &&
	    nvlist_lookup_string(tgt, ZPOOL_CONFIG_PATH, &pathname) == 0) {
		uint64_t wholedisk = 0;

		(void) nvlist_lookup_uint64(tgt, ZPOOL_CONFIG_WHOLE_DISK,
		    &wholedisk);

		/*
		 * XXX - L2ARC 1.0 devices can't support expansion.
		 */
		if (l2cache) {
			zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
			    "cannot expand cache devices"));
			return (zfs_error(hdl, EZFS_VDEVNOTSUP, msg));
		}

		if (wholedisk) {
			const char *fullpath = path;
			char buf[MAXPATHLEN];

			if (path[0] != '/') {
				error = zfs_resolve_shortname(path, buf,
				    sizeof (buf));
				if (error != 0)
					return (zfs_error(hdl, EZFS_NODEVICE,
					    msg));

				fullpath = buf;
			}

			error = zpool_relabel_disk(hdl, fullpath, msg);
			if (error != 0)
				return (error);
		}
	}

	zc.zc_cookie = VDEV_STATE_ONLINE;
	zc.zc_obj = flags;

	if (zfs_ioctl(hdl, ZFS_IOC_VDEV_SET_STATE, &zc) != 0) {
		if (errno == EINVAL) {
			zfs_error_aux(hdl, dgettext(TEXT_DOMAIN, "was split "
			    "from this pool into a new one.  Use '%s' "
			    "instead"), "zpool detach");
			return (zfs_error(hdl, EZFS_POSTSPLIT_ONLINE, msg));
		}
		return (zpool_standard_error(hdl, errno, msg));
	}

	*newstate = zc.zc_cookie;
	return (0);
}

/*
 * Take the specified vdev offline
 */
int
zpool_vdev_offline(zpool_handle_t *zhp, const char *path, boolean_t istmp)
{
	zfs_cmd_t zc = {"\0"};
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

	if (avail_spare)
		return (zfs_error(hdl, EZFS_ISSPARE, msg));

	zc.zc_cookie = VDEV_STATE_OFFLINE;
	zc.zc_obj = istmp ? ZFS_OFFLINE_TEMPORARY : 0;

	if (zfs_ioctl(hdl, ZFS_IOC_VDEV_SET_STATE, &zc) == 0)
		return (0);

	switch (errno) {
	case EBUSY:

		/*
		 * There are no other replicas of this device.
		 */
		return (zfs_error(hdl, EZFS_NOREPLICAS, msg));

	case EEXIST:
		/*
		 * The log device has unplayed logs
		 */
		return (zfs_error(hdl, EZFS_UNPLAYED_LOGS, msg));

	default:
		return (zpool_standard_error(hdl, errno, msg));
	}
}

/*
 * Mark the given vdev faulted.
 */
int
zpool_vdev_fault(zpool_handle_t *zhp, uint64_t guid, vdev_aux_t aux)
{
	zfs_cmd_t zc = {"\0"};
	char msg[1024];
	libzfs_handle_t *hdl = zhp->zpool_hdl;

	(void) snprintf(msg, sizeof (msg),
	    dgettext(TEXT_DOMAIN, "cannot fault %llu"), (u_longlong_t)guid);

	(void) strlcpy(zc.zc_name, zhp->zpool_name, sizeof (zc.zc_name));
	zc.zc_guid = guid;
	zc.zc_cookie = VDEV_STATE_FAULTED;
	zc.zc_obj = aux;

	if (zfs_ioctl(hdl, ZFS_IOC_VDEV_SET_STATE, &zc) == 0)
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
zpool_vdev_degrade(zpool_handle_t *zhp, uint64_t guid, vdev_aux_t aux)
{
	zfs_cmd_t zc = {"\0"};
	char msg[1024];
	libzfs_handle_t *hdl = zhp->zpool_hdl;

	(void) snprintf(msg, sizeof (msg),
	    dgettext(TEXT_DOMAIN, "cannot degrade %llu"), (u_longlong_t)guid);

	(void) strlcpy(zc.zc_name, zhp->zpool_name, sizeof (zc.zc_name));
	zc.zc_guid = guid;
	zc.zc_cookie = VDEV_STATE_DEGRADED;
	zc.zc_obj = aux;

	if (zfs_ioctl(hdl, ZFS_IOC_VDEV_SET_STATE, &zc) == 0)
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

		if ((strcmp(type, VDEV_TYPE_SPARE) == 0 ||
		    strcmp(type, VDEV_TYPE_DRAID_SPARE) == 0) &&
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
zpool_vdev_attach(zpool_handle_t *zhp, const char *old_disk,
    const char *new_disk, nvlist_t *nvroot, int replacing, boolean_t rebuild)
{
	zfs_cmd_t zc = {"\0"};
	char msg[1024];
	int ret;
	nvlist_t *tgt;
	boolean_t avail_spare, l2cache, islog;
	uint64_t val;
	char *newname;
	nvlist_t **child;
	uint_t children;
	nvlist_t *config_root;
	libzfs_handle_t *hdl = zhp->zpool_hdl;

	if (replacing)
		(void) snprintf(msg, sizeof (msg), dgettext(TEXT_DOMAIN,
		    "cannot replace %s with %s"), old_disk, new_disk);
	else
		(void) snprintf(msg, sizeof (msg), dgettext(TEXT_DOMAIN,
		    "cannot attach %s to %s"), new_disk, old_disk);

	(void) strlcpy(zc.zc_name, zhp->zpool_name, sizeof (zc.zc_name));
	if ((tgt = zpool_find_vdev(zhp, old_disk, &avail_spare, &l2cache,
	    &islog)) == NULL)
		return (zfs_error(hdl, EZFS_NODEVICE, msg));

	if (avail_spare)
		return (zfs_error(hdl, EZFS_ISSPARE, msg));

	if (l2cache)
		return (zfs_error(hdl, EZFS_ISL2CACHE, msg));

	verify(nvlist_lookup_uint64(tgt, ZPOOL_CONFIG_GUID, &zc.zc_guid) == 0);
	zc.zc_cookie = replacing;
	zc.zc_simple = rebuild;

	if (rebuild &&
	    zfeature_lookup_guid("org.openzfs:device_rebuild", NULL) != 0) {
		zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
		    "the loaded zfs module doesn't support device rebuilds"));
		return (zfs_error(hdl, EZFS_POOL_NOTSUP, msg));
	}

	if (nvlist_lookup_nvlist_array(nvroot, ZPOOL_CONFIG_CHILDREN,
	    &child, &children) != 0 || children != 1) {
		zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
		    "new device must be a single disk"));
		return (zfs_error(hdl, EZFS_INVALCONFIG, msg));
	}

	verify(nvlist_lookup_nvlist(zpool_get_config(zhp, NULL),
	    ZPOOL_CONFIG_VDEV_TREE, &config_root) == 0);

	if ((newname = zpool_vdev_name(NULL, NULL, child[0], 0)) == NULL)
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

	free(newname);

	if (zcmd_write_conf_nvlist(hdl, &zc, nvroot) != 0)
		return (-1);

	ret = zfs_ioctl(hdl, ZFS_IOC_VDEV_ATTACH, &zc);

	zcmd_free_nvlists(&zc);

	if (ret == 0)
		return (0);

	switch (errno) {
	case ENOTSUP:
		/*
		 * Can't attach to or replace this type of vdev.
		 */
		if (replacing) {
			uint64_t version = zpool_get_prop_int(zhp,
			    ZPOOL_PROP_VERSION, NULL);

			if (islog) {
				zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
				    "cannot replace a log with a spare"));
			} else if (rebuild) {
				zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
				    "only mirror and dRAID vdevs support "
				    "sequential reconstruction"));
			} else if (zpool_is_draid_spare(new_disk)) {
				zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
				    "dRAID spares can only replace child "
				    "devices in their parent's dRAID vdev"));
			} else if (version >= SPA_VERSION_MULTI_REPLACE) {
				zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
				    "already in replacing/spare config; wait "
				    "for completion or use 'zpool detach'"));
			} else {
				zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
				    "cannot replace a replacing device"));
			}
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
		zfs_error_aux(hdl, dgettext(TEXT_DOMAIN, "%s is busy, "
		    "or device removal is in progress"),
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
		 * The new device has a different optimal sector size.
		 */
		zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
		    "new device has a different optimal sector size; use the "
		    "option '-o ashift=N' to override the optimal size"));
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
	zfs_cmd_t zc = {"\0"};
	char msg[1024];
	nvlist_t *tgt;
	boolean_t avail_spare, l2cache;
	libzfs_handle_t *hdl = zhp->zpool_hdl;

	(void) snprintf(msg, sizeof (msg),
	    dgettext(TEXT_DOMAIN, "cannot detach %s"), path);

	(void) strlcpy(zc.zc_name, zhp->zpool_name, sizeof (zc.zc_name));
	if ((tgt = zpool_find_vdev(zhp, path, &avail_spare, &l2cache,
	    NULL)) == NULL)
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
		(void) zfs_error(hdl, EZFS_BADTARGET, msg);
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
 * Find a mirror vdev in the source nvlist.
 *
 * The mchild array contains a list of disks in one of the top-level mirrors
 * of the source pool.  The schild array contains a list of disks that the
 * user specified on the command line.  We loop over the mchild array to
 * see if any entry in the schild array matches.
 *
 * If a disk in the mchild array is found in the schild array, we return
 * the index of that entry.  Otherwise we return -1.
 */
static int
find_vdev_entry(zpool_handle_t *zhp, nvlist_t **mchild, uint_t mchildren,
    nvlist_t **schild, uint_t schildren)
{
	uint_t mc;

	for (mc = 0; mc < mchildren; mc++) {
		uint_t sc;
		char *mpath = zpool_vdev_name(zhp->zpool_hdl, zhp,
		    mchild[mc], 0);

		for (sc = 0; sc < schildren; sc++) {
			char *spath = zpool_vdev_name(zhp->zpool_hdl, zhp,
			    schild[sc], 0);
			boolean_t result = (strcmp(mpath, spath) == 0);

			free(spath);
			if (result) {
				free(mpath);
				return (mc);
			}
		}

		free(mpath);
	}

	return (-1);
}

/*
 * Split a mirror pool.  If newroot points to null, then a new nvlist
 * is generated and it is the responsibility of the caller to free it.
 */
int
zpool_vdev_split(zpool_handle_t *zhp, char *newname, nvlist_t **newroot,
    nvlist_t *props, splitflags_t flags)
{
	zfs_cmd_t zc = {"\0"};
	char msg[1024], *bias;
	nvlist_t *tree, *config, **child, **newchild, *newconfig = NULL;
	nvlist_t **varray = NULL, *zc_props = NULL;
	uint_t c, children, newchildren, lastlog = 0, vcount, found = 0;
	libzfs_handle_t *hdl = zhp->zpool_hdl;
	uint64_t vers, readonly = B_FALSE;
	boolean_t freelist = B_FALSE, memory_err = B_TRUE;
	int retval = 0;

	(void) snprintf(msg, sizeof (msg),
	    dgettext(TEXT_DOMAIN, "Unable to split %s"), zhp->zpool_name);

	if (!zpool_name_valid(hdl, B_FALSE, newname))
		return (zfs_error(hdl, EZFS_INVALIDNAME, msg));

	if ((config = zpool_get_config(zhp, NULL)) == NULL) {
		(void) fprintf(stderr, gettext("Internal error: unable to "
		    "retrieve pool configuration\n"));
		return (-1);
	}

	verify(nvlist_lookup_nvlist(config, ZPOOL_CONFIG_VDEV_TREE, &tree)
	    == 0);
	verify(nvlist_lookup_uint64(config, ZPOOL_CONFIG_VERSION, &vers) == 0);

	if (props) {
		prop_flags_t flags = { .create = B_FALSE, .import = B_TRUE };
		if ((zc_props = zpool_valid_proplist(hdl, zhp->zpool_name,
		    props, vers, flags, msg)) == NULL)
			return (-1);
		(void) nvlist_lookup_uint64(zc_props,
		    zpool_prop_to_name(ZPOOL_PROP_READONLY), &readonly);
		if (readonly) {
			zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
			    "property %s can only be set at import time"),
			    zpool_prop_to_name(ZPOOL_PROP_READONLY));
			return (-1);
		}
	}

	if (nvlist_lookup_nvlist_array(tree, ZPOOL_CONFIG_CHILDREN, &child,
	    &children) != 0) {
		zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
		    "Source pool is missing vdev tree"));
		nvlist_free(zc_props);
		return (-1);
	}

	varray = zfs_alloc(hdl, children * sizeof (nvlist_t *));
	vcount = 0;

	if (*newroot == NULL ||
	    nvlist_lookup_nvlist_array(*newroot, ZPOOL_CONFIG_CHILDREN,
	    &newchild, &newchildren) != 0)
		newchildren = 0;

	for (c = 0; c < children; c++) {
		uint64_t is_log = B_FALSE, is_hole = B_FALSE;
		boolean_t is_special = B_FALSE, is_dedup = B_FALSE;
		char *type;
		nvlist_t **mchild, *vdev;
		uint_t mchildren;
		int entry;

		/*
		 * Unlike cache & spares, slogs are stored in the
		 * ZPOOL_CONFIG_CHILDREN array.  We filter them out here.
		 */
		(void) nvlist_lookup_uint64(child[c], ZPOOL_CONFIG_IS_LOG,
		    &is_log);
		(void) nvlist_lookup_uint64(child[c], ZPOOL_CONFIG_IS_HOLE,
		    &is_hole);
		if (is_log || is_hole) {
			/*
			 * Create a hole vdev and put it in the config.
			 */
			if (nvlist_alloc(&vdev, NV_UNIQUE_NAME, 0) != 0)
				goto out;
			if (nvlist_add_string(vdev, ZPOOL_CONFIG_TYPE,
			    VDEV_TYPE_HOLE) != 0)
				goto out;
			if (nvlist_add_uint64(vdev, ZPOOL_CONFIG_IS_HOLE,
			    1) != 0)
				goto out;
			if (lastlog == 0)
				lastlog = vcount;
			varray[vcount++] = vdev;
			continue;
		}
		lastlog = 0;
		verify(nvlist_lookup_string(child[c], ZPOOL_CONFIG_TYPE, &type)
		    == 0);

		if (strcmp(type, VDEV_TYPE_INDIRECT) == 0) {
			vdev = child[c];
			if (nvlist_dup(vdev, &varray[vcount++], 0) != 0)
				goto out;
			continue;
		} else if (strcmp(type, VDEV_TYPE_MIRROR) != 0) {
			zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
			    "Source pool must be composed only of mirrors\n"));
			retval = zfs_error(hdl, EZFS_INVALCONFIG, msg);
			goto out;
		}

		if (nvlist_lookup_string(child[c],
		    ZPOOL_CONFIG_ALLOCATION_BIAS, &bias) == 0) {
			if (strcmp(bias, VDEV_ALLOC_BIAS_SPECIAL) == 0)
				is_special = B_TRUE;
			else if (strcmp(bias, VDEV_ALLOC_BIAS_DEDUP) == 0)
				is_dedup = B_TRUE;
		}
		verify(nvlist_lookup_nvlist_array(child[c],
		    ZPOOL_CONFIG_CHILDREN, &mchild, &mchildren) == 0);

		/* find or add an entry for this top-level vdev */
		if (newchildren > 0 &&
		    (entry = find_vdev_entry(zhp, mchild, mchildren,
		    newchild, newchildren)) >= 0) {
			/* We found a disk that the user specified. */
			vdev = mchild[entry];
			++found;
		} else {
			/* User didn't specify a disk for this vdev. */
			vdev = mchild[mchildren - 1];
		}

		if (nvlist_dup(vdev, &varray[vcount++], 0) != 0)
			goto out;

		if (flags.dryrun != 0) {
			if (is_dedup == B_TRUE) {
				if (nvlist_add_string(varray[vcount - 1],
				    ZPOOL_CONFIG_ALLOCATION_BIAS,
				    VDEV_ALLOC_BIAS_DEDUP) != 0)
					goto out;
			} else if (is_special == B_TRUE) {
				if (nvlist_add_string(varray[vcount - 1],
				    ZPOOL_CONFIG_ALLOCATION_BIAS,
				    VDEV_ALLOC_BIAS_SPECIAL) != 0)
					goto out;
			}
		}
	}

	/* did we find every disk the user specified? */
	if (found != newchildren) {
		zfs_error_aux(hdl, dgettext(TEXT_DOMAIN, "Device list must "
		    "include at most one disk from each mirror"));
		retval = zfs_error(hdl, EZFS_INVALCONFIG, msg);
		goto out;
	}

	/* Prepare the nvlist for populating. */
	if (*newroot == NULL) {
		if (nvlist_alloc(newroot, NV_UNIQUE_NAME, 0) != 0)
			goto out;
		freelist = B_TRUE;
		if (nvlist_add_string(*newroot, ZPOOL_CONFIG_TYPE,
		    VDEV_TYPE_ROOT) != 0)
			goto out;
	} else {
		verify(nvlist_remove_all(*newroot, ZPOOL_CONFIG_CHILDREN) == 0);
	}

	/* Add all the children we found */
	if (nvlist_add_nvlist_array(*newroot, ZPOOL_CONFIG_CHILDREN, varray,
	    lastlog == 0 ? vcount : lastlog) != 0)
		goto out;

	/*
	 * If we're just doing a dry run, exit now with success.
	 */
	if (flags.dryrun) {
		memory_err = B_FALSE;
		freelist = B_FALSE;
		goto out;
	}

	/* now build up the config list & call the ioctl */
	if (nvlist_alloc(&newconfig, NV_UNIQUE_NAME, 0) != 0)
		goto out;

	if (nvlist_add_nvlist(newconfig,
	    ZPOOL_CONFIG_VDEV_TREE, *newroot) != 0 ||
	    nvlist_add_string(newconfig,
	    ZPOOL_CONFIG_POOL_NAME, newname) != 0 ||
	    nvlist_add_uint64(newconfig, ZPOOL_CONFIG_VERSION, vers) != 0)
		goto out;

	/*
	 * The new pool is automatically part of the namespace unless we
	 * explicitly export it.
	 */
	if (!flags.import)
		zc.zc_cookie = ZPOOL_EXPORT_AFTER_SPLIT;
	(void) strlcpy(zc.zc_name, zhp->zpool_name, sizeof (zc.zc_name));
	(void) strlcpy(zc.zc_string, newname, sizeof (zc.zc_string));
	if (zcmd_write_conf_nvlist(hdl, &zc, newconfig) != 0)
		goto out;
	if (zc_props != NULL && zcmd_write_src_nvlist(hdl, &zc, zc_props) != 0)
		goto out;

	if (zfs_ioctl(hdl, ZFS_IOC_VDEV_SPLIT, &zc) != 0) {
		retval = zpool_standard_error(hdl, errno, msg);
		goto out;
	}

	freelist = B_FALSE;
	memory_err = B_FALSE;

out:
	if (varray != NULL) {
		int v;

		for (v = 0; v < vcount; v++)
			nvlist_free(varray[v]);
		free(varray);
	}
	zcmd_free_nvlists(&zc);
	nvlist_free(zc_props);
	nvlist_free(newconfig);
	if (freelist) {
		nvlist_free(*newroot);
		*newroot = NULL;
	}

	if (retval != 0)
		return (retval);

	if (memory_err)
		return (no_memory(hdl));

	return (0);
}

/*
 * Remove the given device.
 */
int
zpool_vdev_remove(zpool_handle_t *zhp, const char *path)
{
	zfs_cmd_t zc = {"\0"};
	char msg[1024];
	nvlist_t *tgt;
	boolean_t avail_spare, l2cache, islog;
	libzfs_handle_t *hdl = zhp->zpool_hdl;
	uint64_t version;

	(void) snprintf(msg, sizeof (msg),
	    dgettext(TEXT_DOMAIN, "cannot remove %s"), path);

	if (zpool_is_draid_spare(path)) {
		zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
		    "dRAID spares cannot be removed"));
		return (zfs_error(hdl, EZFS_NODEVICE, msg));
	}

	(void) strlcpy(zc.zc_name, zhp->zpool_name, sizeof (zc.zc_name));
	if ((tgt = zpool_find_vdev(zhp, path, &avail_spare, &l2cache,
	    &islog)) == NULL)
		return (zfs_error(hdl, EZFS_NODEVICE, msg));

	version = zpool_get_prop_int(zhp, ZPOOL_PROP_VERSION, NULL);
	if (islog && version < SPA_VERSION_HOLES) {
		zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
		    "pool must be upgraded to support log removal"));
		return (zfs_error(hdl, EZFS_BADVERSION, msg));
	}

	zc.zc_guid = fnvlist_lookup_uint64(tgt, ZPOOL_CONFIG_GUID);

	if (zfs_ioctl(hdl, ZFS_IOC_VDEV_REMOVE, &zc) == 0)
		return (0);

	switch (errno) {

	case EINVAL:
		zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
		    "invalid config; all top-level vdevs must "
		    "have the same sector size and not be raidz."));
		(void) zfs_error(hdl, EZFS_INVALCONFIG, msg);
		break;

	case EBUSY:
		if (islog) {
			zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
			    "Mount encrypted datasets to replay logs."));
		} else {
			zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
			    "Pool busy; removal may already be in progress"));
		}
		(void) zfs_error(hdl, EZFS_BUSY, msg);
		break;

	case EACCES:
		if (islog) {
			zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
			    "Mount encrypted datasets to replay logs."));
			(void) zfs_error(hdl, EZFS_BUSY, msg);
		} else {
			(void) zpool_standard_error(hdl, errno, msg);
		}
		break;

	default:
		(void) zpool_standard_error(hdl, errno, msg);
	}
	return (-1);
}

int
zpool_vdev_remove_cancel(zpool_handle_t *zhp)
{
	zfs_cmd_t zc;
	char msg[1024];
	libzfs_handle_t *hdl = zhp->zpool_hdl;

	(void) snprintf(msg, sizeof (msg),
	    dgettext(TEXT_DOMAIN, "cannot cancel removal"));

	bzero(&zc, sizeof (zc));
	(void) strlcpy(zc.zc_name, zhp->zpool_name, sizeof (zc.zc_name));
	zc.zc_cookie = 1;

	if (zfs_ioctl(hdl, ZFS_IOC_VDEV_REMOVE, &zc) == 0)
		return (0);

	return (zpool_standard_error(hdl, errno, msg));
}

int
zpool_vdev_indirect_size(zpool_handle_t *zhp, const char *path,
    uint64_t *sizep)
{
	char msg[1024];
	nvlist_t *tgt;
	boolean_t avail_spare, l2cache, islog;
	libzfs_handle_t *hdl = zhp->zpool_hdl;

	(void) snprintf(msg, sizeof (msg),
	    dgettext(TEXT_DOMAIN, "cannot determine indirect size of %s"),
	    path);

	if ((tgt = zpool_find_vdev(zhp, path, &avail_spare, &l2cache,
	    &islog)) == NULL)
		return (zfs_error(hdl, EZFS_NODEVICE, msg));

	if (avail_spare || l2cache || islog) {
		*sizep = 0;
		return (0);
	}

	if (nvlist_lookup_uint64(tgt, ZPOOL_CONFIG_INDIRECT_SIZE, sizep) != 0) {
		zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
		    "indirect size not available"));
		return (zfs_error(hdl, EINVAL, msg));
	}
	return (0);
}

/*
 * Clear the errors for the pool, or the particular device if specified.
 */
int
zpool_clear(zpool_handle_t *zhp, const char *path, nvlist_t *rewindnvl)
{
	zfs_cmd_t zc = {"\0"};
	char msg[1024];
	nvlist_t *tgt;
	zpool_load_policy_t policy;
	boolean_t avail_spare, l2cache;
	libzfs_handle_t *hdl = zhp->zpool_hdl;
	nvlist_t *nvi = NULL;
	int error;

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
		    &l2cache, NULL)) == NULL)
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

	zpool_get_load_policy(rewindnvl, &policy);
	zc.zc_cookie = policy.zlp_rewind;

	if (zcmd_alloc_dst_nvlist(hdl, &zc, zhp->zpool_config_size * 2) != 0)
		return (-1);

	if (zcmd_write_src_nvlist(hdl, &zc, rewindnvl) != 0)
		return (-1);

	while ((error = zfs_ioctl(hdl, ZFS_IOC_CLEAR, &zc)) != 0 &&
	    errno == ENOMEM) {
		if (zcmd_expand_dst_nvlist(hdl, &zc) != 0) {
			zcmd_free_nvlists(&zc);
			return (-1);
		}
	}

	if (!error || ((policy.zlp_rewind & ZPOOL_TRY_REWIND) &&
	    errno != EPERM && errno != EACCES)) {
		if (policy.zlp_rewind &
		    (ZPOOL_DO_REWIND | ZPOOL_TRY_REWIND)) {
			(void) zcmd_read_dst_nvlist(hdl, &zc, &nvi);
			zpool_rewind_exclaim(hdl, zc.zc_name,
			    ((policy.zlp_rewind & ZPOOL_TRY_REWIND) != 0),
			    nvi);
			nvlist_free(nvi);
		}
		zcmd_free_nvlists(&zc);
		return (0);
	}

	zcmd_free_nvlists(&zc);
	return (zpool_standard_error(hdl, errno, msg));
}

/*
 * Similar to zpool_clear(), but takes a GUID (used by fmd).
 */
int
zpool_vdev_clear(zpool_handle_t *zhp, uint64_t guid)
{
	zfs_cmd_t zc = {"\0"};
	char msg[1024];
	libzfs_handle_t *hdl = zhp->zpool_hdl;

	(void) snprintf(msg, sizeof (msg),
	    dgettext(TEXT_DOMAIN, "cannot clear errors for %llx"),
	    (u_longlong_t)guid);

	(void) strlcpy(zc.zc_name, zhp->zpool_name, sizeof (zc.zc_name));
	zc.zc_guid = guid;
	zc.zc_cookie = ZPOOL_NO_REWIND;

	if (zfs_ioctl(hdl, ZFS_IOC_CLEAR, &zc) == 0)
		return (0);

	return (zpool_standard_error(hdl, errno, msg));
}

/*
 * Change the GUID for a pool.
 */
int
zpool_reguid(zpool_handle_t *zhp)
{
	char msg[1024];
	libzfs_handle_t *hdl = zhp->zpool_hdl;
	zfs_cmd_t zc = {"\0"};

	(void) snprintf(msg, sizeof (msg),
	    dgettext(TEXT_DOMAIN, "cannot reguid '%s'"), zhp->zpool_name);

	(void) strlcpy(zc.zc_name, zhp->zpool_name, sizeof (zc.zc_name));
	if (zfs_ioctl(hdl, ZFS_IOC_POOL_REGUID, &zc) == 0)
		return (0);

	return (zpool_standard_error(hdl, errno, msg));
}

/*
 * Reopen the pool.
 */
int
zpool_reopen_one(zpool_handle_t *zhp, void *data)
{
	libzfs_handle_t *hdl = zpool_get_handle(zhp);
	const char *pool_name = zpool_get_name(zhp);
	boolean_t *scrub_restart = data;
	int error;

	error = lzc_reopen(pool_name, *scrub_restart);
	if (error) {
		return (zpool_standard_error_fmt(hdl, error,
		    dgettext(TEXT_DOMAIN, "cannot reopen '%s'"), pool_name));
	}

	return (0);
}

/* call into libzfs_core to execute the sync IOCTL per pool */
int
zpool_sync_one(zpool_handle_t *zhp, void *data)
{
	int ret;
	libzfs_handle_t *hdl = zpool_get_handle(zhp);
	const char *pool_name = zpool_get_name(zhp);
	boolean_t *force = data;
	nvlist_t *innvl = fnvlist_alloc();

	fnvlist_add_boolean_value(innvl, "force", *force);
	if ((ret = lzc_sync(pool_name, innvl, NULL)) != 0) {
		nvlist_free(innvl);
		return (zpool_standard_error_fmt(hdl, ret,
		    dgettext(TEXT_DOMAIN, "sync '%s' failed"), pool_name));
	}
	nvlist_free(innvl);

	return (0);
}

#define	PATH_BUF_LEN	64

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
zpool_vdev_name(libzfs_handle_t *hdl, zpool_handle_t *zhp, nvlist_t *nv,
    int name_flags)
{
	char *path, *type, *env;
	uint64_t value;
	char buf[PATH_BUF_LEN];
	char tmpbuf[PATH_BUF_LEN];

	/*
	 * vdev_name will be "root"/"root-0" for the root vdev, but it is the
	 * zpool name that will be displayed to the user.
	 */
	verify(nvlist_lookup_string(nv, ZPOOL_CONFIG_TYPE, &type) == 0);
	if (zhp != NULL && strcmp(type, "root") == 0)
		return (zfs_strdup(hdl, zpool_get_name(zhp)));

	env = getenv("ZPOOL_VDEV_NAME_PATH");
	if (env && (strtoul(env, NULL, 0) > 0 ||
	    !strncasecmp(env, "YES", 3) || !strncasecmp(env, "ON", 2)))
		name_flags |= VDEV_NAME_PATH;

	env = getenv("ZPOOL_VDEV_NAME_GUID");
	if (env && (strtoul(env, NULL, 0) > 0 ||
	    !strncasecmp(env, "YES", 3) || !strncasecmp(env, "ON", 2)))
		name_flags |= VDEV_NAME_GUID;

	env = getenv("ZPOOL_VDEV_NAME_FOLLOW_LINKS");
	if (env && (strtoul(env, NULL, 0) > 0 ||
	    !strncasecmp(env, "YES", 3) || !strncasecmp(env, "ON", 2)))
		name_flags |= VDEV_NAME_FOLLOW_LINKS;

	if (nvlist_lookup_uint64(nv, ZPOOL_CONFIG_NOT_PRESENT, &value) == 0 ||
	    name_flags & VDEV_NAME_GUID) {
		(void) nvlist_lookup_uint64(nv, ZPOOL_CONFIG_GUID, &value);
		(void) snprintf(buf, sizeof (buf), "%llu", (u_longlong_t)value);
		path = buf;
	} else if (nvlist_lookup_string(nv, ZPOOL_CONFIG_PATH, &path) == 0) {
		if (name_flags & VDEV_NAME_FOLLOW_LINKS) {
			char *rp = realpath(path, NULL);
			if (rp) {
				strlcpy(buf, rp, sizeof (buf));
				path = buf;
				free(rp);
			}
		}

		/*
		 * For a block device only use the name.
		 */
		if ((strcmp(type, VDEV_TYPE_DISK) == 0) &&
		    !(name_flags & VDEV_NAME_PATH)) {
			path = zfs_strip_path(path);
		}

		/*
		 * Remove the partition from the path if this is a whole disk.
		 */
		if (strcmp(type, VDEV_TYPE_DRAID_SPARE) != 0 &&
		    nvlist_lookup_uint64(nv, ZPOOL_CONFIG_WHOLE_DISK, &value)
		    == 0 && value && !(name_flags & VDEV_NAME_PATH)) {
			return (zfs_strip_partition(path));
		}
	} else {
		path = type;

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

		/*
		 * If it's a dRAID device, we add parity, groups, and spares.
		 */
		if (strcmp(path, VDEV_TYPE_DRAID) == 0) {
			uint64_t ndata, nparity, nspares;
			nvlist_t **child;
			uint_t children;

			verify(nvlist_lookup_nvlist_array(nv,
			    ZPOOL_CONFIG_CHILDREN, &child, &children) == 0);
			verify(nvlist_lookup_uint64(nv,
			    ZPOOL_CONFIG_NPARITY, &nparity) == 0);
			verify(nvlist_lookup_uint64(nv,
			    ZPOOL_CONFIG_DRAID_NDATA, &ndata) == 0);
			verify(nvlist_lookup_uint64(nv,
			    ZPOOL_CONFIG_DRAID_NSPARES, &nspares) == 0);

			path = zpool_draid_name(buf, sizeof (buf), ndata,
			    nparity, nspares, children);
		}

		/*
		 * We identify each top-level vdev by using a <type-id>
		 * naming convention.
		 */
		if (name_flags & VDEV_NAME_TYPE_ID) {
			uint64_t id;
			verify(nvlist_lookup_uint64(nv, ZPOOL_CONFIG_ID,
			    &id) == 0);
			(void) snprintf(tmpbuf, sizeof (tmpbuf), "%s-%llu",
			    path, (u_longlong_t)id);
			path = tmpbuf;
		}
	}

	return (zfs_strdup(hdl, path));
}

static int
zbookmark_mem_compare(const void *a, const void *b)
{
	return (memcmp(a, b, sizeof (zbookmark_phys_t)));
}

/*
 * Retrieve the persistent error log, uniquify the members, and return to the
 * caller.
 */
int
zpool_get_errlog(zpool_handle_t *zhp, nvlist_t **nverrlistp)
{
	zfs_cmd_t zc = {"\0"};
	libzfs_handle_t *hdl = zhp->zpool_hdl;
	uint64_t count;
	zbookmark_phys_t *zb = NULL;
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
	zc.zc_nvlist_dst = (uintptr_t)zfs_alloc(zhp->zpool_hdl,
	    count * sizeof (zbookmark_phys_t));
	zc.zc_nvlist_dst_size = count;
	(void) strcpy(zc.zc_name, zhp->zpool_name);
	for (;;) {
		if (zfs_ioctl(zhp->zpool_hdl, ZFS_IOC_ERROR_LOG,
		    &zc) != 0) {
			free((void *)(uintptr_t)zc.zc_nvlist_dst);
			if (errno == ENOMEM) {
				void *dst;

				count = zc.zc_nvlist_dst_size;
				dst = zfs_alloc(zhp->zpool_hdl, count *
				    sizeof (zbookmark_phys_t));
				zc.zc_nvlist_dst = (uintptr_t)dst;
			} else {
				return (zpool_standard_error_fmt(hdl, errno,
				    dgettext(TEXT_DOMAIN, "errors: List of "
				    "errors unavailable")));
			}
		} else {
			break;
		}
	}

	/*
	 * Sort the resulting bookmarks.  This is a little confusing due to the
	 * implementation of ZFS_IOC_ERROR_LOG.  The bookmarks are copied last
	 * to first, and 'zc_nvlist_dst_size' indicates the number of bookmarks
	 * _not_ copied as part of the process.  So we point the start of our
	 * array appropriate and decrement the total number of elements.
	 */
	zb = ((zbookmark_phys_t *)(uintptr_t)zc.zc_nvlist_dst) +
	    zc.zc_nvlist_dst_size;
	count -= zc.zc_nvlist_dst_size;

	qsort(zb, count, sizeof (zbookmark_phys_t), zbookmark_mem_compare);

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
	zfs_cmd_t zc = {"\0"};
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
zfs_save_arguments(int argc, char **argv, char *string, int len)
{
	int i;

	(void) strlcpy(string, basename(argv[0]), len);
	for (i = 1; i < argc; i++) {
		(void) strlcat(string, " ", len);
		(void) strlcat(string, argv[i], len);
	}
}

int
zpool_log_history(libzfs_handle_t *hdl, const char *message)
{
	zfs_cmd_t zc = {"\0"};
	nvlist_t *args;
	int err;

	args = fnvlist_alloc();
	fnvlist_add_string(args, "message", message);
	err = zcmd_write_src_nvlist(hdl, &zc, args);
	if (err == 0)
		err = zfs_ioctl(hdl, ZFS_IOC_LOG_HISTORY, &zc);
	nvlist_free(args);
	zcmd_free_nvlists(&zc);
	return (err);
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
	zfs_cmd_t zc = {"\0"};
	libzfs_handle_t *hdl = zhp->zpool_hdl;

	(void) strlcpy(zc.zc_name, zhp->zpool_name, sizeof (zc.zc_name));

	zc.zc_history = (uint64_t)(uintptr_t)buf;
	zc.zc_history_len = *len;
	zc.zc_history_offset = *off;

	if (zfs_ioctl(hdl, ZFS_IOC_POOL_GET_HISTORY, &zc) != 0) {
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
 * Retrieve the command history of a pool.
 */
int
zpool_get_history(zpool_handle_t *zhp, nvlist_t **nvhisp, uint64_t *off,
    boolean_t *eof)
{
	char *buf;
	int buflen = 128 * 1024;
	nvlist_t **records = NULL;
	uint_t numrecords = 0;
	int err, i;
	uint64_t start = *off;

	buf = malloc(buflen);
	if (buf == NULL)
		return (ENOMEM);
	/* process about 1MB a time */
	while (*off - start < 1024 * 1024) {
		uint64_t bytes_read = buflen;
		uint64_t leftover;

		if ((err = get_history(zhp, buf, off, &bytes_read)) != 0)
			break;

		/* if nothing else was read in, we're at EOF, just return */
		if (!bytes_read) {
			*eof = B_TRUE;
			break;
		}

		if ((err = zpool_history_unpack(buf, bytes_read,
		    &leftover, &records, &numrecords)) != 0)
			break;
		*off -= leftover;
		if (leftover == bytes_read) {
			/*
			 * no progress made, because buffer is not big enough
			 * to hold this record; resize and retry.
			 */
			buflen *= 2;
			free(buf);
			buf = malloc(buflen);
			if (buf == NULL)
				return (ENOMEM);
		}
	}

	free(buf);

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

/*
 * Retrieve the next event given the passed 'zevent_fd' file descriptor.
 * If there is a new event available 'nvp' will contain a newly allocated
 * nvlist and 'dropped' will be set to the number of missed events since
 * the last call to this function.  When 'nvp' is set to NULL it indicates
 * no new events are available.  In either case the function returns 0 and
 * it is up to the caller to free 'nvp'.  In the case of a fatal error the
 * function will return a non-zero value.  When the function is called in
 * blocking mode (the default, unless the ZEVENT_NONBLOCK flag is passed),
 * it will not return until a new event is available.
 */
int
zpool_events_next(libzfs_handle_t *hdl, nvlist_t **nvp,
    int *dropped, unsigned flags, int zevent_fd)
{
	zfs_cmd_t zc = {"\0"};
	int error = 0;

	*nvp = NULL;
	*dropped = 0;
	zc.zc_cleanup_fd = zevent_fd;

	if (flags & ZEVENT_NONBLOCK)
		zc.zc_guid = ZEVENT_NONBLOCK;

	if (zcmd_alloc_dst_nvlist(hdl, &zc, ZEVENT_SIZE) != 0)
		return (-1);

retry:
	if (zfs_ioctl(hdl, ZFS_IOC_EVENTS_NEXT, &zc) != 0) {
		switch (errno) {
		case ESHUTDOWN:
			error = zfs_error_fmt(hdl, EZFS_POOLUNAVAIL,
			    dgettext(TEXT_DOMAIN, "zfs shutdown"));
			goto out;
		case ENOENT:
			/* Blocking error case should not occur */
			if (!(flags & ZEVENT_NONBLOCK))
				error = zpool_standard_error_fmt(hdl, errno,
				    dgettext(TEXT_DOMAIN, "cannot get event"));

			goto out;
		case ENOMEM:
			if (zcmd_expand_dst_nvlist(hdl, &zc) != 0) {
				error = zfs_error_fmt(hdl, EZFS_NOMEM,
				    dgettext(TEXT_DOMAIN, "cannot get event"));
				goto out;
			} else {
				goto retry;
			}
		default:
			error = zpool_standard_error_fmt(hdl, errno,
			    dgettext(TEXT_DOMAIN, "cannot get event"));
			goto out;
		}
	}

	error = zcmd_read_dst_nvlist(hdl, &zc, nvp);
	if (error != 0)
		goto out;

	*dropped = (int)zc.zc_cookie;
out:
	zcmd_free_nvlists(&zc);

	return (error);
}

/*
 * Clear all events.
 */
int
zpool_events_clear(libzfs_handle_t *hdl, int *count)
{
	zfs_cmd_t zc = {"\0"};

	if (zfs_ioctl(hdl, ZFS_IOC_EVENTS_CLEAR, &zc) != 0)
		return (zpool_standard_error(hdl, errno,
		    dgettext(TEXT_DOMAIN, "cannot clear events")));

	if (count != NULL)
		*count = (int)zc.zc_cookie; /* # of events cleared */

	return (0);
}

/*
 * Seek to a specific EID, ZEVENT_SEEK_START, or ZEVENT_SEEK_END for
 * the passed zevent_fd file handle.  On success zero is returned,
 * otherwise -1 is returned and hdl->libzfs_error is set to the errno.
 */
int
zpool_events_seek(libzfs_handle_t *hdl, uint64_t eid, int zevent_fd)
{
	zfs_cmd_t zc = {"\0"};
	int error = 0;

	zc.zc_guid = eid;
	zc.zc_cleanup_fd = zevent_fd;

	if (zfs_ioctl(hdl, ZFS_IOC_EVENTS_SEEK, &zc) != 0) {
		switch (errno) {
		case ENOENT:
			error = zfs_error_fmt(hdl, EZFS_NOENT,
			    dgettext(TEXT_DOMAIN, "cannot get event"));
			break;

		case ENOMEM:
			error = zfs_error_fmt(hdl, EZFS_NOMEM,
			    dgettext(TEXT_DOMAIN, "cannot get event"));
			break;

		default:
			error = zpool_standard_error_fmt(hdl, errno,
			    dgettext(TEXT_DOMAIN, "cannot get event"));
			break;
		}
	}

	return (error);
}

static void
zpool_obj_to_path_impl(zpool_handle_t *zhp, uint64_t dsobj, uint64_t obj,
    char *pathname, size_t len, boolean_t always_unmounted)
{
	zfs_cmd_t zc = {"\0"};
	boolean_t mounted = B_FALSE;
	char *mntpnt = NULL;
	char dsname[ZFS_MAX_DATASET_NAME_LEN];

	if (dsobj == 0) {
		/* special case for the MOS */
		(void) snprintf(pathname, len, "<metadata>:<0x%llx>",
		    (longlong_t)obj);
		return;
	}

	/* get the dataset's name */
	(void) strlcpy(zc.zc_name, zhp->zpool_name, sizeof (zc.zc_name));
	zc.zc_obj = dsobj;
	if (zfs_ioctl(zhp->zpool_hdl,
	    ZFS_IOC_DSOBJ_TO_DSNAME, &zc) != 0) {
		/* just write out a path of two object numbers */
		(void) snprintf(pathname, len, "<0x%llx>:<0x%llx>",
		    (longlong_t)dsobj, (longlong_t)obj);
		return;
	}
	(void) strlcpy(dsname, zc.zc_value, sizeof (dsname));

	/* find out if the dataset is mounted */
	mounted = !always_unmounted && is_mounted(zhp->zpool_hdl, dsname,
	    &mntpnt);

	/* get the corrupted object's path */
	(void) strlcpy(zc.zc_name, dsname, sizeof (zc.zc_name));
	zc.zc_obj = obj;
	if (zfs_ioctl(zhp->zpool_hdl, ZFS_IOC_OBJ_TO_PATH,
	    &zc) == 0) {
		if (mounted) {
			(void) snprintf(pathname, len, "%s%s", mntpnt,
			    zc.zc_value);
		} else {
			(void) snprintf(pathname, len, "%s:%s",
			    dsname, zc.zc_value);
		}
	} else {
		(void) snprintf(pathname, len, "%s:<0x%llx>", dsname,
		    (longlong_t)obj);
	}
	free(mntpnt);
}

void
zpool_obj_to_path(zpool_handle_t *zhp, uint64_t dsobj, uint64_t obj,
    char *pathname, size_t len)
{
	zpool_obj_to_path_impl(zhp, dsobj, obj, pathname, len, B_FALSE);
}

void
zpool_obj_to_path_ds(zpool_handle_t *zhp, uint64_t dsobj, uint64_t obj,
    char *pathname, size_t len)
{
	zpool_obj_to_path_impl(zhp, dsobj, obj, pathname, len, B_TRUE);
}
/*
 * Wait while the specified activity is in progress in the pool.
 */
int
zpool_wait(zpool_handle_t *zhp, zpool_wait_activity_t activity)
{
	boolean_t missing;

	int error = zpool_wait_status(zhp, activity, &missing, NULL);

	if (missing) {
		(void) zpool_standard_error_fmt(zhp->zpool_hdl, ENOENT,
		    dgettext(TEXT_DOMAIN, "error waiting in pool '%s'"),
		    zhp->zpool_name);
		return (ENOENT);
	} else {
		return (error);
	}
}

/*
 * Wait for the given activity and return the status of the wait (whether or not
 * any waiting was done) in the 'waited' parameter. Non-existent pools are
 * reported via the 'missing' parameter, rather than by printing an error
 * message. This is convenient when this function is called in a loop over a
 * long period of time (as it is, for example, by zpool's wait cmd). In that
 * scenario, a pool being exported or destroyed should be considered a normal
 * event, so we don't want to print an error when we find that the pool doesn't
 * exist.
 */
int
zpool_wait_status(zpool_handle_t *zhp, zpool_wait_activity_t activity,
    boolean_t *missing, boolean_t *waited)
{
	int error = lzc_wait(zhp->zpool_name, activity, waited);
	*missing = (error == ENOENT);
	if (*missing)
		return (0);

	if (error != 0) {
		(void) zpool_standard_error_fmt(zhp->zpool_hdl, error,
		    dgettext(TEXT_DOMAIN, "error waiting in pool '%s'"),
		    zhp->zpool_name);
	}

	return (error);
}

int
zpool_set_bootenv(zpool_handle_t *zhp, const nvlist_t *envmap)
{
	int error = lzc_set_bootenv(zhp->zpool_name, envmap);
	if (error != 0) {
		(void) zpool_standard_error_fmt(zhp->zpool_hdl, error,
		    dgettext(TEXT_DOMAIN,
		    "error setting bootenv in pool '%s'"), zhp->zpool_name);
	}

	return (error);
}

int
zpool_get_bootenv(zpool_handle_t *zhp, nvlist_t **nvlp)
{
	nvlist_t *nvl;
	int error;

	nvl = NULL;
	error = lzc_get_bootenv(zhp->zpool_name, &nvl);
	if (error != 0) {
		(void) zpool_standard_error_fmt(zhp->zpool_hdl, error,
		    dgettext(TEXT_DOMAIN,
		    "error getting bootenv in pool '%s'"), zhp->zpool_name);
	} else {
		*nvlp = nvl;
	}

	return (error);
}

/*
 * Attempt to read and parse feature file(s) (from "compatibility" property).
 * Files contain zpool feature names, comma or whitespace-separated.
 * Comments (# character to next newline) are discarded.
 *
 * Arguments:
 *  compatibility : string containing feature filenames
 *  features : either NULL or pointer to array of boolean
 *  report : either NULL or pointer to string buffer
 *  rlen : length of "report" buffer
 *
 * compatibility is NULL (unset), "", "off", "legacy", or list of
 * comma-separated filenames. filenames should either be absolute,
 * or relative to:
 *   1) ZPOOL_SYSCONF_COMPAT_D (eg: /etc/zfs/compatibility.d) or
 *   2) ZPOOL_DATA_COMPAT_D (eg: /usr/share/zfs/compatibility.d).
 * (Unset), "" or "off" => enable all features
 * "legacy" => disable all features
 *
 * Any feature names read from files which match unames in spa_feature_table
 * will have the corresponding boolean set in the features array (if non-NULL).
 * If more than one feature set specified, only features present in *all* of
 * them will be set.
 *
 * "report" if not NULL will be populated with a suitable status message.
 *
 * Return values:
 *   ZPOOL_COMPATIBILITY_OK : files read and parsed ok
 *   ZPOOL_COMPATIBILITY_BADFILE : file too big or not a text file
 *   ZPOOL_COMPATIBILITY_BADTOKEN : SYSCONF file contains invalid feature name
 *   ZPOOL_COMPATIBILITY_WARNTOKEN : DATA file contains invalid feature name
 *   ZPOOL_COMPATIBILITY_NOFILES : no feature files found
 */
zpool_compat_status_t
zpool_load_compat(const char *compat, boolean_t *features, char *report,
    size_t rlen)
{
	int sdirfd, ddirfd, featfd;
	struct stat fs;
	char *fc;
	char *ps, *ls, *ws;
	char *file, *line, *word;

	char l_compat[ZFS_MAXPROPLEN];

	boolean_t ret_nofiles = B_TRUE;
	boolean_t ret_badfile = B_FALSE;
	boolean_t ret_badtoken = B_FALSE;
	boolean_t ret_warntoken = B_FALSE;

	/* special cases (unset), "" and "off" => enable all features */
	if (compat == NULL || compat[0] == '\0' ||
	    strcmp(compat, ZPOOL_COMPAT_OFF) == 0) {
		if (features != NULL)
			for (uint_t i = 0; i < SPA_FEATURES; i++)
				features[i] = B_TRUE;
		if (report != NULL)
			strlcpy(report, gettext("all features enabled"), rlen);
		return (ZPOOL_COMPATIBILITY_OK);
	}

	/* Final special case "legacy" => disable all features */
	if (strcmp(compat, ZPOOL_COMPAT_LEGACY) == 0) {
		if (features != NULL)
			for (uint_t i = 0; i < SPA_FEATURES; i++)
				features[i] = B_FALSE;
		if (report != NULL)
			strlcpy(report, gettext("all features disabled"), rlen);
		return (ZPOOL_COMPATIBILITY_OK);
	}

	/*
	 * Start with all true; will be ANDed with results from each file
	 */
	if (features != NULL)
		for (uint_t i = 0; i < SPA_FEATURES; i++)
			features[i] = B_TRUE;

	char err_badfile[1024] = "";
	char err_badtoken[1024] = "";

	/*
	 * We ignore errors from the directory open()
	 * as they're only needed if the filename is relative
	 * which will be checked during the openat().
	 */

/* O_PATH safer than O_RDONLY if system allows it */
#if defined(O_PATH)
#define	ZC_DIR_FLAGS (O_DIRECTORY | O_CLOEXEC | O_PATH)
#else
#define	ZC_DIR_FLAGS (O_DIRECTORY | O_CLOEXEC | O_RDONLY)
#endif

	sdirfd = open(ZPOOL_SYSCONF_COMPAT_D, ZC_DIR_FLAGS);
	ddirfd = open(ZPOOL_DATA_COMPAT_D, ZC_DIR_FLAGS);

	(void) strlcpy(l_compat, compat, ZFS_MAXPROPLEN);

	for (file = strtok_r(l_compat, ",", &ps);
	    file != NULL;
	    file = strtok_r(NULL, ",", &ps)) {

		boolean_t l_features[SPA_FEATURES];

		enum { Z_SYSCONF, Z_DATA } source;

		/* try sysconfdir first, then datadir */
		source = Z_SYSCONF;
		if ((featfd = openat(sdirfd, file, O_RDONLY | O_CLOEXEC)) < 0) {
			featfd = openat(ddirfd, file, O_RDONLY | O_CLOEXEC);
			source = Z_DATA;
		}

		/* File readable and correct size? */
		if (featfd < 0 ||
		    fstat(featfd, &fs) < 0 ||
		    fs.st_size < 1 ||
		    fs.st_size > ZPOOL_COMPAT_MAXSIZE) {
			(void) close(featfd);
			strlcat(err_badfile, file, ZFS_MAXPROPLEN);
			strlcat(err_badfile, " ", ZFS_MAXPROPLEN);
			ret_badfile = B_TRUE;
			continue;
		}

/* Prefault the file if system allows */
#if defined(MAP_POPULATE)
#define	ZC_MMAP_FLAGS (MAP_PRIVATE | MAP_POPULATE)
#elif defined(MAP_PREFAULT_READ)
#define	ZC_MMAP_FLAGS (MAP_PRIVATE | MAP_PREFAULT_READ)
#else
#define	ZC_MMAP_FLAGS (MAP_PRIVATE)
#endif

		/* private mmap() so we can strtok safely */
		fc = (char *)mmap(NULL, fs.st_size, PROT_READ | PROT_WRITE,
		    ZC_MMAP_FLAGS, featfd, 0);
		(void) close(featfd);

		/* map ok, and last character == newline? */
		if (fc == MAP_FAILED || fc[fs.st_size - 1] != '\n') {
			(void) munmap((void *) fc, fs.st_size);
			strlcat(err_badfile, file, ZFS_MAXPROPLEN);
			strlcat(err_badfile, " ", ZFS_MAXPROPLEN);
			ret_badfile = B_TRUE;
			continue;
		}

		ret_nofiles = B_FALSE;

		for (uint_t i = 0; i < SPA_FEATURES; i++)
			l_features[i] = B_FALSE;

		/* replace final newline with NULL to ensure string ends */
		fc[fs.st_size - 1] = '\0';

		for (line = strtok_r(fc, "\n", &ls);
		    line != NULL;
		    line = strtok_r(NULL, "\n", &ls)) {
			/* discard comments */
			char *r = strchr(line, '#');
			if (r != NULL)
				*r = '\0';

			for (word = strtok_r(line, ", \t", &ws);
			    word != NULL;
			    word = strtok_r(NULL, ", \t", &ws)) {
				/* Find matching feature name */
				uint_t f;
				for (f = 0; f < SPA_FEATURES; f++) {
					zfeature_info_t *fi =
					    &spa_feature_table[f];
					if (strcmp(word, fi->fi_uname) == 0) {
						l_features[f] = B_TRUE;
						break;
					}
				}
				if (f < SPA_FEATURES)
					continue;

				/* found an unrecognized word */
				/* lightly sanitize it */
				if (strlen(word) > 32)
					word[32] = '\0';
				for (char *c = word; *c != '\0'; c++)
					if (!isprint(*c))
						*c = '?';

				strlcat(err_badtoken, word, ZFS_MAXPROPLEN);
				strlcat(err_badtoken, " ", ZFS_MAXPROPLEN);
				if (source == Z_SYSCONF)
					ret_badtoken = B_TRUE;
				else
					ret_warntoken = B_TRUE;
			}
		}
		(void) munmap((void *) fc, fs.st_size);

		if (features != NULL)
			for (uint_t i = 0; i < SPA_FEATURES; i++)
				features[i] &= l_features[i];
	}
	(void) close(sdirfd);
	(void) close(ddirfd);

	/* Return the most serious error */
	if (ret_badfile) {
		if (report != NULL)
			snprintf(report, rlen, gettext("could not read/"
			    "parse feature file(s): %s"), err_badfile);
		return (ZPOOL_COMPATIBILITY_BADFILE);
	}
	if (ret_nofiles) {
		if (report != NULL)
			strlcpy(report,
			    gettext("no valid compatibility files specified"),
			    rlen);
		return (ZPOOL_COMPATIBILITY_NOFILES);
	}
	if (ret_badtoken) {
		if (report != NULL)
			snprintf(report, rlen, gettext("invalid feature "
			    "name(s) in local compatibility files: %s"),
			    err_badtoken);
		return (ZPOOL_COMPATIBILITY_BADTOKEN);
	}
	if (ret_warntoken) {
		if (report != NULL)
			snprintf(report, rlen, gettext("unrecognized feature "
			    "name(s) in distribution compatibility files: %s"),
			    err_badtoken);
		return (ZPOOL_COMPATIBILITY_WARNTOKEN);
	}
	if (report != NULL)
		strlcpy(report, gettext("compatibility set ok"), rlen);
	return (ZPOOL_COMPATIBILITY_OK);
}
