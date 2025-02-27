// SPDX-License-Identifier: CDDL-1.0
/*
 * CDDL HEADER START
 *
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this
 * source.  A copy of the CDDL is also available via the Internet at
 * http://www.illumos.org/license/CDDL.
 *
 * CDDL HEADER END
 */

/*
 * Copyright (c) 2016 by Delphix. All rights reserved.
 */

#include <sys/lua/lua.h>
#include <sys/lua/lualib.h>
#include <sys/lua/lauxlib.h>

#include <zfs_prop.h>

#include <sys/dsl_prop.h>
#include <sys/dsl_synctask.h>
#include <sys/dsl_dataset.h>
#include <sys/dsl_dir.h>
#include <sys/dmu_objset.h>
#include <sys/mntent.h>
#include <sys/sunddi.h>
#include <sys/zap.h>
#include <sys/zcp.h>
#include <sys/zcp_iter.h>
#include <sys/zcp_global.h>
#include <sys/zcp_prop.h>
#include <sys/zfs_ioctl.h>
#include <sys/zfs_znode.h>
#include <sys/zvol.h>

#ifdef _KERNEL
#include <sys/zfs_quota.h>
#include <sys/zfs_vfsops.h>
#endif

static int
get_objset_type(dsl_dataset_t *ds, zfs_type_t *type)
{
	int error;
	objset_t *os;
	error = dmu_objset_from_ds(ds, &os);
	if (error != 0)
		return (error);
	if (ds->ds_is_snapshot) {
		*type = ZFS_TYPE_SNAPSHOT;
	} else {
		switch (os->os_phys->os_type) {
		case DMU_OST_ZFS:
			*type = ZFS_TYPE_FILESYSTEM;
			break;
		case DMU_OST_ZVOL:
			*type = ZFS_TYPE_VOLUME;
			break;
		default:
			return (EINVAL);
		}
	}
	return (0);
}

/*
 * Returns the string name of ds's type in str (a buffer which should be
 * at least 12 bytes long).
 */
static int
get_objset_type_name(dsl_dataset_t *ds, char *str)
{
	zfs_type_t type = ZFS_TYPE_INVALID;
	int error = get_objset_type(ds, &type);
	if (error != 0)
		return (error);
	switch (type) {
	case ZFS_TYPE_SNAPSHOT:
		(void) strlcpy(str, "snapshot", ZAP_MAXVALUELEN);
		break;
	case ZFS_TYPE_FILESYSTEM:
		(void) strlcpy(str, "filesystem", ZAP_MAXVALUELEN);
		break;
	case ZFS_TYPE_VOLUME:
		(void) strlcpy(str, "volume", ZAP_MAXVALUELEN);
		break;
	default:
		return (EINVAL);
	}
	return (0);
}

/*
 * Determines the source of a property given its setpoint and
 * property type. It pushes the source to the lua stack.
 */
static void
get_prop_src(lua_State *state, const char *setpoint, zfs_prop_t prop)
{
	if (zfs_prop_readonly(prop) || (prop == ZFS_PROP_VERSION)) {
		lua_pushnil(state);
	} else {
		const char *src;
		if (strcmp("", setpoint) == 0) {
			src = "default";
		} else {
			src = setpoint;
		}
		(void) lua_pushstring(state, src);
	}
}

/*
 * Given an error encountered while getting properties, either longjmp's for
 * a fatal error or pushes nothing to the stack for a non fatal one.
 */
static int
zcp_handle_error(lua_State *state, const char *dataset_name,
    const char *property_name, int error)
{
	ASSERT3S(error, !=, 0);
	if (error == ENOENT) {
		return (0);
	} else if (error == EINVAL) {
		return (luaL_error(state,
		    "property '%s' is not a valid property on dataset '%s'",
		    property_name, dataset_name));
	} else if (error == EIO) {
		return (luaL_error(state,
		    "I/O error while retrieving property '%s' on dataset '%s'",
		    property_name, dataset_name));
	} else {
		return (luaL_error(state, "unexpected error %d while "
		    "retrieving property '%s' on dataset '%s'",
		    error, property_name, dataset_name));
	}
}

/*
 * Look up a user defined property in the zap object. If it exists, push it
 * and the setpoint onto the stack, otherwise don't push anything.
 */
static int
zcp_get_user_prop(lua_State *state, dsl_pool_t *dp, const char *dataset_name,
    const char *property_name)
{
	int error;
	char *buf;
	char setpoint[ZFS_MAX_DATASET_NAME_LEN];
	/*
	 * zcp_dataset_hold will either successfully return the requested
	 * dataset or throw a lua error and longjmp out of the zfs.get_prop call
	 * without returning.
	 */
	dsl_dataset_t *ds = zcp_dataset_hold(state, dp, dataset_name, FTAG);
	if (ds == NULL)
		return (1); /* not reached; zcp_dataset_hold() longjmp'd */

	buf = kmem_alloc(ZAP_MAXVALUELEN, KM_SLEEP);
	error = dsl_prop_get_ds(ds, property_name, 1, ZAP_MAXVALUELEN,
	    buf, setpoint);
	dsl_dataset_rele(ds, FTAG);

	if (error != 0) {
		kmem_free(buf, ZAP_MAXVALUELEN);
		return (zcp_handle_error(state, dataset_name, property_name,
		    error));
	}
	(void) lua_pushstring(state, buf);
	(void) lua_pushstring(state, setpoint);
	kmem_free(buf, ZAP_MAXVALUELEN);
	return (2);
}

/*
 * Check if the property we're looking for is stored in the ds_dir. If so,
 * return it in the 'val' argument. Return 0 on success and ENOENT and if
 * the property is not present.
 */
static int
get_dsl_dir_prop(dsl_dataset_t *ds, zfs_prop_t zfs_prop,
    uint64_t *val)
{
	dsl_dir_t *dd = ds->ds_dir;
	mutex_enter(&dd->dd_lock);
	switch (zfs_prop) {
	case ZFS_PROP_USEDSNAP:
		*val = dsl_dir_get_usedsnap(dd);
		break;
	case ZFS_PROP_USEDCHILD:
		*val = dsl_dir_get_usedchild(dd);
		break;
	case ZFS_PROP_USEDDS:
		*val = dsl_dir_get_usedds(dd);
		break;
	case ZFS_PROP_USEDREFRESERV:
		*val = dsl_dir_get_usedrefreserv(dd);
		break;
	case ZFS_PROP_LOGICALUSED:
		*val = dsl_dir_get_logicalused(dd);
		break;
	default:
		mutex_exit(&dd->dd_lock);
		return (SET_ERROR(ENOENT));
	}
	mutex_exit(&dd->dd_lock);
	return (0);
}

/*
 * Check if the property we're looking for is stored at the dsl_dataset or
 * dsl_dir level. If so, push the property value and source onto the lua stack
 * and return 0. If it is not present or a failure occurs in lookup, return a
 * non-zero error value.
 */
static int
get_special_prop(lua_State *state, dsl_dataset_t *ds, const char *dsname,
    zfs_prop_t zfs_prop)
{
	int error = 0;
	objset_t *os;
	uint64_t numval = 0;
	char *strval = kmem_alloc(ZAP_MAXVALUELEN, KM_SLEEP);
	char setpoint[ZFS_MAX_DATASET_NAME_LEN] =
	    "Internal error - setpoint not determined";
	zfs_type_t ds_type = ZFS_TYPE_INVALID;
	zprop_type_t prop_type = zfs_prop_get_type(zfs_prop);
	(void) get_objset_type(ds, &ds_type);

	switch (zfs_prop) {
	case ZFS_PROP_REFRATIO:
		numval = dsl_get_refratio(ds);
		break;
	case ZFS_PROP_USED:
		numval = dsl_get_used(ds);
		break;
	case ZFS_PROP_CLONES: {
		nvlist_t *clones = fnvlist_alloc();
		error = get_clones_stat_impl(ds, clones);
		if (error == 0) {
			/* push list to lua stack */
			VERIFY0(zcp_nvlist_to_lua(state, clones, NULL, 0ULL));
			/* source */
			(void) lua_pushnil(state);
		}
		nvlist_free(clones);
		kmem_free(strval, ZAP_MAXVALUELEN);
		return (error);
	}
	case ZFS_PROP_COMPRESSRATIO:
		numval = dsl_get_compressratio(ds);
		break;
	case ZFS_PROP_CREATION:
		numval = dsl_get_creation(ds);
		break;
	case ZFS_PROP_REFERENCED:
		numval = dsl_get_referenced(ds);
		break;
	case ZFS_PROP_AVAILABLE:
		numval = dsl_get_available(ds);
		break;
	case ZFS_PROP_LOGICALREFERENCED:
		numval = dsl_get_logicalreferenced(ds);
		break;
	case ZFS_PROP_CREATETXG:
		numval = dsl_get_creationtxg(ds);
		break;
	case ZFS_PROP_GUID:
		numval = dsl_get_guid(ds);
		break;
	case ZFS_PROP_UNIQUE:
		numval = dsl_get_unique(ds);
		break;
	case ZFS_PROP_OBJSETID:
		numval = dsl_get_objsetid(ds);
		break;
	case ZFS_PROP_ORIGIN:
		dsl_dir_get_origin(ds->ds_dir, strval);
		break;
	case ZFS_PROP_USERACCOUNTING:
		error = dmu_objset_from_ds(ds, &os);
		if (error == 0)
			numval = dmu_objset_userspace_present(os);
		break;
	case ZFS_PROP_WRITTEN:
		error = dsl_get_written(ds, &numval);
		break;
	case ZFS_PROP_TYPE:
		error = get_objset_type_name(ds, strval);
		break;
	case ZFS_PROP_PREV_SNAP:
		error = dsl_get_prev_snap(ds, strval);
		break;
	case ZFS_PROP_NAME:
		dsl_dataset_name(ds, strval);
		break;
	case ZFS_PROP_MOUNTPOINT:
		error = dsl_get_mountpoint(ds, dsname, strval, setpoint);
		break;
	case ZFS_PROP_VERSION:
		/* should be a snapshot or filesystem */
		ASSERT(ds_type != ZFS_TYPE_VOLUME);
		error = dmu_objset_from_ds(ds, &os);
		/* look in the master node for the version */
		if (error == 0) {
			error = zap_lookup(os, MASTER_NODE_OBJ, ZPL_VERSION_STR,
			    sizeof (numval), 1, &numval);
		}
		break;
	case ZFS_PROP_DEFER_DESTROY:
		numval = dsl_get_defer_destroy(ds);
		break;
	case ZFS_PROP_USERREFS:
		numval = dsl_get_userrefs(ds);
		break;
	case ZFS_PROP_FILESYSTEM_COUNT:
		error = dsl_dir_get_filesystem_count(ds->ds_dir, &numval);
		(void) strlcpy(setpoint, "", ZFS_MAX_DATASET_NAME_LEN);
		break;
	case ZFS_PROP_SNAPSHOT_COUNT:
		error = dsl_dir_get_snapshot_count(ds->ds_dir, &numval);
		(void) strlcpy(setpoint, "", ZFS_MAX_DATASET_NAME_LEN);
		break;
	case ZFS_PROP_NUMCLONES:
		numval = dsl_get_numclones(ds);
		break;
	case ZFS_PROP_INCONSISTENT:
		numval = dsl_get_inconsistent(ds);
		break;
	case ZFS_PROP_IVSET_GUID:
		if (dsl_dataset_is_zapified(ds)) {
			error = zap_lookup(ds->ds_dir->dd_pool->dp_meta_objset,
			    ds->ds_object, DS_FIELD_IVSET_GUID,
			    sizeof (numval), 1, &numval);
		} else {
			error = ENOENT;
		}
		break;
	case ZFS_PROP_RECEIVE_RESUME_TOKEN: {
		char *token = get_receive_resume_token(ds);
		if (token != NULL) {
			(void) strlcpy(strval, token, ZAP_MAXVALUELEN);
			kmem_strfree(token);
		} else {
			error = ENOENT;
		}
		break;
	}
	case ZFS_PROP_VOLSIZE:
		ASSERT(ds_type == ZFS_TYPE_VOLUME ||
		    ds_type == ZFS_TYPE_SNAPSHOT);
		error = dmu_objset_from_ds(ds, &os);
		if (error == 0) {
			error = zap_lookup(os, ZVOL_ZAP_OBJ, "size",
			    sizeof (numval), 1, &numval);
		}
		if (error == 0)
			(void) strlcpy(setpoint, dsname,
			    ZFS_MAX_DATASET_NAME_LEN);

		break;
	case ZFS_PROP_VOLBLOCKSIZE: {
		ASSERT(ds_type == ZFS_TYPE_VOLUME);
		dmu_object_info_t doi;
		error = dmu_objset_from_ds(ds, &os);
		if (error == 0) {
			error = dmu_object_info(os, ZVOL_OBJ, &doi);
			if (error == 0)
				numval = doi.doi_data_block_size;
		}
		break;
	}

	case ZFS_PROP_KEYSTATUS:
	case ZFS_PROP_KEYFORMAT: {
		/* provide defaults in case no crypto obj exists */
		setpoint[0] = '\0';
		if (zfs_prop == ZFS_PROP_KEYSTATUS)
			numval = ZFS_KEYSTATUS_NONE;
		else
			numval = ZFS_KEYFORMAT_NONE;

		nvlist_t *nvl, *propval;
		nvl = fnvlist_alloc();
		dsl_dataset_crypt_stats(ds, nvl);
		if (nvlist_lookup_nvlist(nvl, zfs_prop_to_name(zfs_prop),
		    &propval) == 0) {
			const char *source;

			(void) nvlist_lookup_uint64(propval, ZPROP_VALUE,
			    &numval);
			if (nvlist_lookup_string(propval, ZPROP_SOURCE,
			    &source) == 0)
				strlcpy(setpoint, source, sizeof (setpoint));
		}
		nvlist_free(nvl);
		break;
	}

	case ZFS_PROP_SNAPSHOTS_CHANGED:
		numval = dsl_dir_snap_cmtime(ds->ds_dir).tv_sec;
		break;

	default:
		/* Did not match these props, check in the dsl_dir */
		error = get_dsl_dir_prop(ds, zfs_prop, &numval);
	}
	if (error != 0) {
		kmem_free(strval, ZAP_MAXVALUELEN);
		return (error);
	}

	switch (prop_type) {
	case PROP_TYPE_NUMBER: {
		(void) lua_pushnumber(state, numval);
		break;
	}
	case PROP_TYPE_STRING: {
		(void) lua_pushstring(state, strval);
		break;
	}
	case PROP_TYPE_INDEX: {
		const char *propval;
		error = zfs_prop_index_to_string(zfs_prop, numval, &propval);
		if (error != 0) {
			kmem_free(strval, ZAP_MAXVALUELEN);
			return (error);
		}
		(void) lua_pushstring(state, propval);
		break;
	}
	}
	kmem_free(strval, ZAP_MAXVALUELEN);

	/* Push the source to the stack */
	get_prop_src(state, setpoint, zfs_prop);
	return (0);
}

/*
 * Look up a property and its source in the zap object. If the value is
 * present and successfully retrieved, push the value and source on the
 * lua stack and return 0. On failure, return a non-zero error value.
 */
static int
get_zap_prop(lua_State *state, dsl_dataset_t *ds, zfs_prop_t zfs_prop)
{
	int error = 0;
	char setpoint[ZFS_MAX_DATASET_NAME_LEN];
	char *strval = kmem_alloc(ZAP_MAXVALUELEN, KM_SLEEP);
	uint64_t numval;
	const char *prop_name = zfs_prop_to_name(zfs_prop);
	zprop_type_t prop_type = zfs_prop_get_type(zfs_prop);

	if (prop_type == PROP_TYPE_STRING) {
		/* Push value to lua stack */
		error = dsl_prop_get_ds(ds, prop_name, 1,
		    ZAP_MAXVALUELEN, strval, setpoint);
		if (error == 0)
			(void) lua_pushstring(state, strval);
	} else {
		error = dsl_prop_get_ds(ds, prop_name, sizeof (numval),
		    1, &numval, setpoint);
		if (error != 0)
			goto out;
#ifdef _KERNEL
		/* Fill in temporary value for prop, if applicable */
		(void) zfs_get_temporary_prop(ds, zfs_prop, &numval, setpoint);
#else
		kmem_free(strval, ZAP_MAXVALUELEN);
		return (luaL_error(state,
		    "temporary properties only supported in kernel mode",
		    prop_name));
#endif
		/* Push value to lua stack */
		if (prop_type == PROP_TYPE_INDEX) {
			const char *propval;
			error = zfs_prop_index_to_string(zfs_prop, numval,
			    &propval);
			if (error == 0)
				(void) lua_pushstring(state, propval);
		} else {
			if (error == 0)
				(void) lua_pushnumber(state, numval);
		}
	}
out:
	kmem_free(strval, ZAP_MAXVALUELEN);
	if (error == 0)
		get_prop_src(state, setpoint, zfs_prop);
	return (error);
}

/*
 * Determine whether property is valid for a given dataset
 */
boolean_t
prop_valid_for_ds(dsl_dataset_t *ds, zfs_prop_t zfs_prop)
{
	zfs_type_t zfs_type = ZFS_TYPE_INVALID;

	/* properties not supported */
	if ((zfs_prop == ZFS_PROP_ISCSIOPTIONS) ||
	    (zfs_prop == ZFS_PROP_MOUNTED))
		return (B_FALSE);

	/* if we want the origin prop, ds must be a clone */
	if ((zfs_prop == ZFS_PROP_ORIGIN) && (!dsl_dir_is_clone(ds->ds_dir)))
		return (B_FALSE);

	int error = get_objset_type(ds, &zfs_type);
	if (error != 0)
		return (B_FALSE);
	return (zfs_prop_valid_for_type(zfs_prop, zfs_type, B_FALSE));
}

/*
 * Look up a given dataset property. On success return 2, the number of
 * values pushed to the lua stack (property value and source). On a fatal
 * error, longjmp. On a non fatal error push nothing.
 */
static int
zcp_get_system_prop(lua_State *state, dsl_pool_t *dp, const char *dataset_name,
    zfs_prop_t zfs_prop)
{
	int error;
	/*
	 * zcp_dataset_hold will either successfully return the requested
	 * dataset or throw a lua error and longjmp out of the zfs.get_prop call
	 * without returning.
	 */
	dsl_dataset_t *ds = zcp_dataset_hold(state, dp, dataset_name, FTAG);
	if (ds == NULL)
		return (1); /* not reached; zcp_dataset_hold() longjmp'd */

	/* Check that the property is valid for the given dataset */
	const char *prop_name = zfs_prop_to_name(zfs_prop);
	if (!prop_valid_for_ds(ds, zfs_prop)) {
		dsl_dataset_rele(ds, FTAG);
		return (0);
	}

	/* Check if the property can be accessed directly */
	error = get_special_prop(state, ds, dataset_name, zfs_prop);
	if (error == 0) {
		dsl_dataset_rele(ds, FTAG);
		/* The value and source have been pushed by get_special_prop */
		return (2);
	}
	if (error != ENOENT) {
		dsl_dataset_rele(ds, FTAG);
		return (zcp_handle_error(state, dataset_name,
		    prop_name, error));
	}

	/* If we were unable to find it, look in the zap object */
	error = get_zap_prop(state, ds, zfs_prop);
	dsl_dataset_rele(ds, FTAG);
	if (error != 0) {
		return (zcp_handle_error(state, dataset_name,
		    prop_name, error));
	}
	/* The value and source have been pushed by get_zap_prop */
	return (2);
}

#ifdef _KERNEL
static zfs_userquota_prop_t
get_userquota_prop(const char *prop_name)
{
	zfs_userquota_prop_t type;
	/* Figure out the property type ({user|group}{quota|used}) */
	for (type = 0; type < ZFS_NUM_USERQUOTA_PROPS; type++) {
		if (strncmp(prop_name, zfs_userquota_prop_prefixes[type],
		    strlen(zfs_userquota_prop_prefixes[type])) == 0)
			break;
	}
	return (type);
}

/*
 * Given the name of a zfs_userquota_prop, this function determines the
 * prop type as well as the numeric group/user ids based on the string
 * following the '@' in the property name. On success, returns 0. On failure,
 * returns a non-zero error.
 * 'domain' must be free'd by caller using kmem_strfree()
 */
static int
parse_userquota_prop(const char *prop_name, zfs_userquota_prop_t *type,
    char **domain, uint64_t *rid)
{
	char *cp, *end, *domain_val;

	*type = get_userquota_prop(prop_name);
	if (*type >= ZFS_NUM_USERQUOTA_PROPS)
		return (EINVAL);

	*rid = 0;
	cp = strchr(prop_name, '@') + 1;
	if (strncmp(cp, "S-1-", 4) == 0) {
		/*
		 * It's a numeric SID (eg "S-1-234-567-89") and we want to
		 * separate the domain id and the rid
		 */
		int domain_len = strrchr(cp, '-') - cp;
		domain_val = kmem_alloc(domain_len + 1, KM_SLEEP);
		(void) strlcpy(domain_val, cp, domain_len + 1);
		cp += domain_len + 1;

		(void) ddi_strtoll(cp, &end, 10, (longlong_t *)rid);
		if (*end != '\0') {
			kmem_strfree(domain_val);
			return (EINVAL);
		}
	} else {
		/* It's only a user/group ID (eg "12345"), just get the rid */
		domain_val = NULL;
		(void) ddi_strtoll(cp, &end, 10, (longlong_t *)rid);
		if (*end != '\0')
			return (EINVAL);
	}
	*domain = domain_val;
	return (0);
}

/*
 * Look up {user|group}{quota|used} property for given dataset. On success
 * push the value (quota or used amount) and the setpoint. On failure, push
 * a lua error.
 */
static int
zcp_get_userquota_prop(lua_State *state, dsl_pool_t *dp,
    const char *dataset_name, const char *prop_name)
{
	zfsvfs_t *zfvp;
	zfsvfs_t *zfsvfs;
	int error;
	zfs_userquota_prop_t type;
	char *domain;
	uint64_t rid, value = 0;
	objset_t *os;

	dsl_dataset_t *ds = zcp_dataset_hold(state, dp, dataset_name, FTAG);
	if (ds == NULL)
		return (1); /* not reached; zcp_dataset_hold() longjmp'd */

	error = parse_userquota_prop(prop_name, &type, &domain, &rid);
	if (error == 0) {
		error = dmu_objset_from_ds(ds, &os);
		if (error == 0) {
			zfsvfs = kmem_zalloc(sizeof (zfsvfs_t), KM_SLEEP);
			error = zfsvfs_create_impl(&zfvp, zfsvfs, os);
			if (error == 0) {
				error = zfs_userspace_one(zfvp, type, domain,
				    rid, &value);
				zfsvfs_free(zfvp);
			}
		}
		if (domain != NULL)
			kmem_strfree(domain);
	}
	dsl_dataset_rele(ds, FTAG);

	if ((value == 0) && ((type == ZFS_PROP_USERQUOTA) ||
	    (type == ZFS_PROP_GROUPQUOTA)))
		error = SET_ERROR(ENOENT);
	if (error != 0) {
		return (zcp_handle_error(state, dataset_name,
		    prop_name, error));
	}

	(void) lua_pushnumber(state, value);
	(void) lua_pushstring(state, dataset_name);
	return (2);
}
#endif

/*
 * Determines the name of the snapshot referenced in the written property
 * name. Returns snapshot name in snap_name, a buffer that must be at least
 * as large as ZFS_MAX_DATASET_NAME_LEN
 */
static void
parse_written_prop(const char *dataset_name, const char *prop_name,
    char *snap_name)
{
	ASSERT(zfs_prop_written(prop_name));
	const char *name = prop_name + ZFS_WRITTEN_PROP_PREFIX_LEN;
	if (strchr(name, '@') == NULL) {
		(void) snprintf(snap_name, ZFS_MAX_DATASET_NAME_LEN, "%s@%s",
		    dataset_name, name);
	} else {
		(void) strlcpy(snap_name, name, ZFS_MAX_DATASET_NAME_LEN);
	}
}

/*
 * Look up written@ property for given dataset. On success
 * push the value and the setpoint. If error is fatal, we will
 * longjmp, otherwise push nothing.
 */
static int
zcp_get_written_prop(lua_State *state, dsl_pool_t *dp,
    const char *dataset_name, const char *prop_name)
{
	char snap_name[ZFS_MAX_DATASET_NAME_LEN];
	uint64_t used, comp, uncomp;
	dsl_dataset_t *old;
	int error = 0;

	parse_written_prop(dataset_name, prop_name, snap_name);
	dsl_dataset_t *new = zcp_dataset_hold(state, dp, dataset_name, FTAG);
	if (new == NULL)
		return (1); /* not reached; zcp_dataset_hold() longjmp'd */

	error = dsl_dataset_hold(dp, snap_name, FTAG, &old);
	if (error != 0) {
		dsl_dataset_rele(new, FTAG);
		return (zcp_dataset_hold_error(state, dp, snap_name,
		    error));
	}
	error = dsl_dataset_space_written(old, new,
	    &used, &comp, &uncomp);

	dsl_dataset_rele(old, FTAG);
	dsl_dataset_rele(new, FTAG);

	if (error != 0) {
		return (zcp_handle_error(state, dataset_name,
		    snap_name, error));
	}
	(void) lua_pushnumber(state, used);
	(void) lua_pushstring(state, dataset_name);
	return (2);
}

static int zcp_get_prop(lua_State *state);
static const zcp_lib_info_t zcp_get_prop_info = {
	.name = "get_prop",
	.func = zcp_get_prop,
	.pargs = {
	    { .za_name = "dataset", .za_lua_type = LUA_TSTRING },
	    { .za_name = "property", .za_lua_type =  LUA_TSTRING },
	    {NULL, 0}
	},
	.kwargs = {
	    {NULL, 0}
	}
};

static int
zcp_get_prop(lua_State *state)
{
	const char *dataset_name;
	const char *property_name;
	dsl_pool_t *dp = zcp_run_info(state)->zri_pool;
	const zcp_lib_info_t *libinfo = &zcp_get_prop_info;

	zcp_parse_args(state, libinfo->name, libinfo->pargs, libinfo->kwargs);

	dataset_name = lua_tostring(state, 1);
	property_name = lua_tostring(state, 2);

	/* User defined property */
	if (zfs_prop_user(property_name)) {
		return (zcp_get_user_prop(state, dp,
		    dataset_name, property_name));
	}
	/* userspace property */
	if (zfs_prop_userquota(property_name)) {
#ifdef _KERNEL
		return (zcp_get_userquota_prop(state, dp,
		    dataset_name, property_name));
#else
		return (luaL_error(state,
		    "user quota properties only supported in kernel mode",
		    property_name));
#endif
	}
	/* written@ property */
	if (zfs_prop_written(property_name)) {
		return (zcp_get_written_prop(state, dp,
		    dataset_name, property_name));
	}

	zfs_prop_t zfs_prop = zfs_name_to_prop(property_name);
	/* Valid system property */
	if (zfs_prop != ZPROP_INVAL) {
		return (zcp_get_system_prop(state, dp, dataset_name,
		    zfs_prop));
	}

	/* Invalid property name */
	return (luaL_error(state,
	    "'%s' is not a valid property", property_name));
}

int
zcp_load_get_lib(lua_State *state)
{
	lua_pushcclosure(state, zcp_get_prop_info.func, 0);
	lua_setfield(state, -2, zcp_get_prop_info.name);

	return (1);
}
