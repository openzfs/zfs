/*
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this
 * source.  A copy of the CDDL is also available via the Internet at
 * http://www.illumos.org/license/CDDL.
 */
/*
 * Copyright 2020 Toomas Soome <tsoome@me.com>
 */

#include <sys/types.h>
#include <string.h>
#include <libzfs.h>
#include <libzfsbootenv.h>
#include <sys/zfs_bootenv.h>
#include <sys/vdev_impl.h>

/*
 * Get or create nvlist. If key is not NULL, get nvlist from bootenv,
 * otherwise return bootenv.
 */
int
lzbe_nvlist_get(const char *pool, const char *key, void **ptr)
{
	libzfs_handle_t *hdl;
	zpool_handle_t *zphdl;
	nvlist_t *nv;
	int rv = -1;

	if (pool == NULL || *pool == '\0')
		return (rv);

	if ((hdl = libzfs_init()) == NULL) {
		return (rv);
	}

	zphdl = zpool_open(hdl, pool);
	if (zphdl == NULL) {
		libzfs_fini(hdl);
		return (rv);
	}

	rv = zpool_get_bootenv(zphdl, &nv);
	if (rv == 0) {
		nvlist_t *nvl, *dup;

		if (key != NULL) {
			rv = nvlist_lookup_nvlist(nv, key, &nvl);
			if (rv == 0) {
				rv = nvlist_dup(nvl, &dup, 0);
				nvlist_free(nv);
				if (rv == 0)
					nv = dup;
				else
					nv = NULL;
			} else {
				nvlist_free(nv);
				rv = nvlist_alloc(&nv, NV_UNIQUE_NAME, 0);
			}
		}
		*ptr = nv;
	}

	zpool_close(zphdl);
	libzfs_fini(hdl);
	return (rv);
}

int
lzbe_nvlist_set(const char *pool, const char *key, void *ptr)
{
	libzfs_handle_t *hdl;
	zpool_handle_t *zphdl;
	nvlist_t *nv;
	uint64_t version;
	int rv = -1;

	if (pool == NULL || *pool == '\0')
		return (rv);

	if ((hdl = libzfs_init()) == NULL) {
		return (rv);
	}

	zphdl = zpool_open(hdl, pool);
	if (zphdl == NULL) {
		libzfs_fini(hdl);
		return (rv);
	}

	if (key != NULL) {
		rv = zpool_get_bootenv(zphdl, &nv);
		if (rv == 0) {
			/*
			 * We got the nvlist, check for version.
			 * if version is missing or is not VB_NVLIST,
			 * create new list.
			 */
			rv = nvlist_lookup_uint64(nv, BOOTENV_VERSION,
			    &version);
			if (rv != 0 || version != VB_NVLIST) {
				/* Drop this nvlist */
				fnvlist_free(nv);
				/* Create and prepare new nvlist */
				nv = fnvlist_alloc();
				fnvlist_add_uint64(nv, BOOTENV_VERSION,
				    VB_NVLIST);
			}
			rv = nvlist_add_nvlist(nv, key, ptr);
			if (rv == 0)
				rv = zpool_set_bootenv(zphdl, nv);
			nvlist_free(nv);
		}
	} else {
		rv = zpool_set_bootenv(zphdl, ptr);
	}

	zpool_close(zphdl);
	libzfs_fini(hdl);
	return (rv);
}

/*
 * free nvlist we got via lzbe_nvlist_get()
 */
void
lzbe_nvlist_free(void *ptr)
{
	nvlist_free(ptr);
}

static const char *typenames[] = {
	"DATA_TYPE_UNKNOWN",
	"DATA_TYPE_BOOLEAN",
	"DATA_TYPE_BYTE",
	"DATA_TYPE_INT16",
	"DATA_TYPE_UINT16",
	"DATA_TYPE_INT32",
	"DATA_TYPE_UINT32",
	"DATA_TYPE_INT64",
	"DATA_TYPE_UINT64",
	"DATA_TYPE_STRING",
	"DATA_TYPE_BYTE_ARRAY",
	"DATA_TYPE_INT16_ARRAY",
	"DATA_TYPE_UINT16_ARRAY",
	"DATA_TYPE_INT32_ARRAY",
	"DATA_TYPE_UINT32_ARRAY",
	"DATA_TYPE_INT64_ARRAY",
	"DATA_TYPE_UINT64_ARRAY",
	"DATA_TYPE_STRING_ARRAY",
	"DATA_TYPE_HRTIME",
	"DATA_TYPE_NVLIST",
	"DATA_TYPE_NVLIST_ARRAY",
	"DATA_TYPE_BOOLEAN_VALUE",
	"DATA_TYPE_INT8",
	"DATA_TYPE_UINT8",
	"DATA_TYPE_BOOLEAN_ARRAY",
	"DATA_TYPE_INT8_ARRAY",
	"DATA_TYPE_UINT8_ARRAY"
};

static int
nvpair_type_from_name(const char *name)
{
	unsigned i;

	for (i = 0; i < ARRAY_SIZE(typenames); i++) {
		if (strcmp(name, typenames[i]) == 0)
			return (i);
	}
	return (0);
}

/*
 * Add pair defined by key, type and value into nvlist.
 */
int
lzbe_add_pair(void *ptr, const char *key, const char *type, void *value,
    size_t size)
{
	nvlist_t *nv = ptr;
	data_type_t dt;
	int rv = 0;

	if (ptr == NULL || key == NULL || value == NULL)
		return (rv);

	if (type == NULL)
		type = "DATA_TYPE_STRING";
	dt = nvpair_type_from_name(type);
	if (dt == DATA_TYPE_UNKNOWN)
		return (EINVAL);

	switch (dt) {
	case DATA_TYPE_BYTE:
		if (size != sizeof (uint8_t)) {
			rv = EINVAL;
			break;
		}
		rv = nvlist_add_byte(nv, key, *(uint8_t *)value);
		break;

	case DATA_TYPE_INT16:
		if (size != sizeof (int16_t)) {
			rv = EINVAL;
			break;
		}
		rv = nvlist_add_int16(nv, key, *(int16_t *)value);
		break;

	case DATA_TYPE_UINT16:
		if (size != sizeof (uint16_t)) {
			rv = EINVAL;
			break;
		}
		rv = nvlist_add_uint16(nv, key, *(uint16_t *)value);
		break;

	case DATA_TYPE_INT32:
		if (size != sizeof (int32_t)) {
			rv = EINVAL;
			break;
		}
		rv = nvlist_add_int32(nv, key, *(int32_t *)value);
		break;

	case DATA_TYPE_UINT32:
		if (size != sizeof (uint32_t)) {
			rv = EINVAL;
			break;
		}
		rv = nvlist_add_uint32(nv, key, *(uint32_t *)value);
		break;

	case DATA_TYPE_INT64:
		if (size != sizeof (int64_t)) {
			rv = EINVAL;
			break;
		}
		rv = nvlist_add_int64(nv, key, *(int64_t *)value);
		break;

	case DATA_TYPE_UINT64:
		if (size != sizeof (uint64_t)) {
			rv = EINVAL;
			break;
		}
		rv = nvlist_add_uint64(nv, key, *(uint64_t *)value);
		break;

	case DATA_TYPE_STRING:
		rv = nvlist_add_string(nv, key, value);
		break;

	case DATA_TYPE_BYTE_ARRAY:
		rv = nvlist_add_byte_array(nv, key, value, size);
		break;

	case DATA_TYPE_INT16_ARRAY:
		rv = nvlist_add_int16_array(nv, key, value, size);
		break;

	case DATA_TYPE_UINT16_ARRAY:
		rv = nvlist_add_uint16_array(nv, key, value, size);
		break;

	case DATA_TYPE_INT32_ARRAY:
		rv = nvlist_add_int32_array(nv, key, value, size);
		break;

	case DATA_TYPE_UINT32_ARRAY:
		rv = nvlist_add_uint32_array(nv, key, value, size);
		break;

	case DATA_TYPE_INT64_ARRAY:
		rv = nvlist_add_int64_array(nv, key, value, size);
		break;

	case DATA_TYPE_UINT64_ARRAY:
		rv = nvlist_add_uint64_array(nv, key, value, size);
		break;

	case DATA_TYPE_STRING_ARRAY:
		rv = nvlist_add_string_array(nv, key, value, size);
		break;

	case DATA_TYPE_NVLIST:
		rv = nvlist_add_nvlist(nv, key, (nvlist_t *)value);
		break;

	case DATA_TYPE_NVLIST_ARRAY:
		rv = nvlist_add_nvlist_array(nv, key, (const nvlist_t **)value,
		    size);
		break;

	case DATA_TYPE_BOOLEAN_VALUE:
		if (size != sizeof (boolean_t)) {
			rv = EINVAL;
			break;
		}
		rv = nvlist_add_boolean_value(nv, key, *(boolean_t *)value);
		break;

	case DATA_TYPE_INT8:
		if (size != sizeof (int8_t)) {
			rv = EINVAL;
			break;
		}
		rv = nvlist_add_int8(nv, key, *(int8_t *)value);
		break;

	case DATA_TYPE_UINT8:
		if (size != sizeof (uint8_t)) {
			rv = EINVAL;
			break;
		}
		rv = nvlist_add_uint8(nv, key, *(uint8_t *)value);
		break;

	case DATA_TYPE_BOOLEAN_ARRAY:
		rv = nvlist_add_boolean_array(nv, key, value, size);
		break;

	case DATA_TYPE_INT8_ARRAY:
		rv = nvlist_add_int8_array(nv, key, value, size);
		break;

	case DATA_TYPE_UINT8_ARRAY:
		rv = nvlist_add_uint8_array(nv, key, value, size);
		break;

	default:
		return (ENOTSUP);
	}

	return (rv);
}

int
lzbe_remove_pair(void *ptr, const char *key)
{

	return (nvlist_remove_all(ptr, key));
}
