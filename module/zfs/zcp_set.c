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
 * Copyrigh 2020 Joyent, Inc.
 */

#include <sys/lua/lua.h>
#include <sys/lua/lualib.h>
#include <sys/lua/lauxlib.h>

#include <sys/dsl_prop.h>
#include <sys/dsl_dir.h>
#include <sys/dsl_synctask.h>
#include <sys/dsl_dataset.h>
#include <sys/zcp.h>
#include <sys/zcp_set.h>
#include <sys/zcp_iter.h>
#include <sys/zcp_global.h>
#include <sys/zvol.h>

#include <zfs_prop.h>

static void
zcp_set_user_prop(lua_State *state, dsl_pool_t *dp, const char *dsname,
    const char *prop_name, const char *prop_val, dmu_tx_t *tx)
{
	dsl_dataset_t *ds = zcp_dataset_hold(state, dp, dsname, FTAG);
	if (ds == NULL)
		return; /* not reached; zcp_dataset_hold() longjmp'd */

	nvlist_t *nvl = fnvlist_alloc();
	fnvlist_add_string(nvl, prop_name, prop_val);

	dsl_props_set_sync_impl(ds, ZPROP_SRC_LOCAL, nvl, tx);

	fnvlist_free(nvl);
	dsl_dataset_rele(ds, FTAG);
}

int
zcp_set_prop_check(void *arg, dmu_tx_t *tx)
{
	zcp_set_prop_arg_t *args = arg;
	const char *prop_name = args->prop;
	dsl_props_set_arg_t dpsa = {
		.dpsa_dsname = args->dsname,
		.dpsa_source = ZPROP_SRC_LOCAL,
	};
	nvlist_t *nvl = NULL;
	int ret = 0;

	/*
	 * Only user properties are currently supported. When non-user
	 * properties are supported, we will want to use
	 * zfs_valid_proplist() to verify the properties.
	 */
	if (!zfs_prop_user(prop_name)) {
		return (EINVAL);
	}

	nvl = fnvlist_alloc();
	fnvlist_add_string(nvl, args->prop, args->val);
	dpsa.dpsa_props = nvl;

	ret = dsl_props_set_check(&dpsa, tx);
	nvlist_free(nvl);

	return (ret);
}

void
zcp_set_prop_sync(void *arg, dmu_tx_t *tx)
{
	zcp_set_prop_arg_t *args = arg;
	zcp_run_info_t *ri = zcp_run_info(args->state);
	dsl_pool_t *dp = ri->zri_pool;

	const char *dsname = args->dsname;
	const char *prop_name = args->prop;
	const char *prop_val = args->val;

	if (zfs_prop_user(prop_name)) {
		zcp_set_user_prop(args->state, dp, dsname, prop_name,
		    prop_val, tx);
	}
}
