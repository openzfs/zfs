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
 * Copyright 2016 ClusterHQ Inc. All rights reserved.
 * Use is subject to license terms.
 */

#include "zprop_conv.h"

static int
zprop_conv_zpool_to_strings_impl(const char *propname, nvpair_t *propval,
    nvpair_t *pair, nvlist_t *result) {
	zpool_prop_t prop = zpool_name_to_prop(propname);

	/* ZPROP_INVAL is typically a user property. Nothing to do here */
	if (prop == ZPROP_INVAL) {
		(void) nvlist_add_nvpair(result, pair);
		return (0);
	}

	switch (zpool_prop_get_type(prop)) {
	case PROP_TYPE_INDEX:
		if (nvpair_type(propval) == DATA_TYPE_UINT64) {
			const char *strval = NULL;
			if (zpool_prop_index_to_string(prop,
			    fnvpair_value_uint64(propval), &strval) != 0)
				return (EINVAL);
			if (nvpair_type(pair) == DATA_TYPE_NVLIST) {
				nvlist_t *nvl =
				    fnvlist_dup(fnvpair_value_nvlist(pair));
				fnvlist_remove(nvl, ZPROP_VALUE);
				fnvlist_add_string(nvl, ZPROP_VALUE, strval);
				fnvlist_add_nvlist(result, propname, nvl);
				fnvlist_free(nvl);
			} else {
				fnvlist_add_string(result, propname, strval);
			}
			break;
		}
	default:
		(void) nvlist_add_nvpair(result, pair);
		break;
	}
	return (0);
}

static int
zprop_conv_zpool_from_strings_impl(const char *propname, nvpair_t *propval,
    nvpair_t *pair, nvlist_t *result) {
	zpool_prop_t prop = zpool_name_to_prop(propname);

	/* ZPROP_INVAL is typically a user property. Nothing to do here */
	if (prop == ZPROP_INVAL) {
		(void) nvlist_add_nvpair(result, pair);
		return (0);
	}

	switch (zpool_prop_get_type(prop)) {
	case PROP_TYPE_INDEX:
		if (nvpair_type(propval) == DATA_TYPE_STRING) {
			uint64_t intval;
			if (zpool_prop_string_to_index(prop,
			    fnvpair_value_string(propval), &intval) != 0)
				return (EINVAL);
			if (nvpair_type(pair) == DATA_TYPE_NVLIST) {
				nvlist_t *nvl =
				    fnvlist_dup(fnvpair_value_nvlist(pair));
				fnvlist_remove(nvl, ZPROP_VALUE);
				fnvlist_add_uint64(nvl, ZPROP_VALUE, intval);
				fnvlist_add_nvlist(result, propname, nvl);
				fnvlist_free(nvl);
			} else {
				fnvlist_add_uint64(result, propname, intval);
			}
			break;
		}
	default:
		(void) nvlist_add_nvpair(result, pair);
		break;
	}
	return (0);
}

static int
zprop_conv_zfs_to_strings_impl(const char *propname, nvpair_t *propval,
    nvpair_t *pair, nvlist_t *result) {
	zfs_prop_t prop = zfs_name_to_prop(propname);

	/* ZPROP_INVAL is typically a user property. Nothing to do here */
	if (prop == ZPROP_INVAL) {
		(void) nvlist_add_nvpair(result, pair);
		return (0);
	}

	switch (zfs_prop_get_type(prop)) {
	case PROP_TYPE_INDEX:
		if (nvpair_type(propval) == DATA_TYPE_UINT64) {
			const char *strval = NULL;
			if (zfs_prop_index_to_string(prop,
			    fnvpair_value_uint64(propval), &strval) != 0)
				return (EINVAL);
			if (nvpair_type(pair) == DATA_TYPE_NVLIST) {
				nvlist_t *nvl =
				    fnvlist_dup(fnvpair_value_nvlist(pair));
				fnvlist_remove(nvl, ZPROP_VALUE);
				fnvlist_add_string(nvl, ZPROP_VALUE, strval);
				fnvlist_add_nvlist(result, propname, nvl);
				fnvlist_free(nvl);
			} else {
				fnvlist_add_string(result, propname, strval);
			}
			break;
		}
	default:
		(void) nvlist_add_nvpair(result, pair);
		break;
	}
	return (0);
}

static int
zprop_conv_zfs_from_strings_impl(const char *propname, nvpair_t *propval,
    nvpair_t *pair, nvlist_t *result) {
	zfs_prop_t prop = zfs_name_to_prop(propname);

	/* ZPROP_INVAL is typically a user property. Nothing to do here */
	if (prop == ZPROP_INVAL) {
		(void) nvlist_add_nvpair(result, pair);
		return (0);
	}

	switch (zfs_prop_get_type(prop)) {
	case PROP_TYPE_INDEX:
		if (nvpair_type(propval) == DATA_TYPE_STRING) {
			uint64_t intval;
			if (zfs_prop_string_to_index(prop,
			    fnvpair_value_string(propval), &intval) != 0)
				return (EINVAL);
			if (nvpair_type(pair) == DATA_TYPE_NVLIST) {
				nvlist_t *nvl =
				    fnvlist_dup(fnvpair_value_nvlist(pair));
				fnvlist_remove(nvl, ZPROP_VALUE);
				fnvlist_add_uint64(nvl, ZPROP_VALUE, intval);
				fnvlist_add_nvlist(result, propname, nvl);
				fnvlist_free(nvl);
			} else {
				fnvlist_add_uint64(result, propname, intval);
			}
			break;
		}
	default:
		(void) nvlist_add_nvpair(result, pair);
		break;
	}
	return (0);
}

static nvlist_t *
zprop_conv_common(nvlist_t *nvl, int (*converter)(const char *, nvpair_t *,
    nvpair_t *, nvlist_t *)) {
	nvlist_t *result;
	nvpair_t *pair;

	if (nvl == NULL)
		return (NULL);

	if (nvlist_empty(nvl))
		return (fnvlist_dup(nvl));

	result = fnvlist_alloc();

	pair = NULL;
	while ((pair = nvlist_next_nvpair(nvl, pair)) != NULL) {
		const char *propname = nvpair_name(pair);
		nvpair_t *propval;

		/* decode the property value */
		propval = pair;
		if (nvpair_type(pair) == DATA_TYPE_NVLIST) {
			nvlist_t *attrs;
			attrs = fnvpair_value_nvlist(pair);
			if (nvlist_lookup_nvpair(attrs, ZPROP_VALUE,
			    &propval) != 0) {
				fnvlist_free(result);
				return (NULL);
			}
		}


		if ((*converter)(propname, propval, pair, result) != 0) {
			fnvlist_free(result);
			return (NULL);
		}

	}

	return (result);
}

nvlist_t *
zprop_conv_zfs_from_strings(nvlist_t *nvl) {
	return (zprop_conv_common(nvl, &zprop_conv_zfs_from_strings_impl));
}

nvlist_t *
zprop_conv_zfs_to_strings(nvlist_t *nvl) {
	return (zprop_conv_common(nvl, &zprop_conv_zfs_to_strings_impl));
}

nvlist_t *
zprop_conv_zpool_from_strings(nvlist_t *nvl) {
	return (zprop_conv_common(nvl, &zprop_conv_zpool_from_strings_impl));
}

nvlist_t *
zprop_conv_zpool_to_strings(nvlist_t *nvl) {
	return (zprop_conv_common(nvl, &zprop_conv_zpool_to_strings_impl));
}

#if defined(_KERNEL) && defined(HAVE_SPL)
EXPORT_SYMBOL(zprop_conv_zfs_from_strings);
EXPORT_SYMBOL(zprop_conv_zfs_to_strings);
EXPORT_SYMBOL(zprop_conv_zpool_to_strings);
EXPORT_SYMBOL(zprop_conv_zpool_from_strings);
#endif
