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
 * Store device name to zpool label bootenv area.
 * This call will set bootenv version to VB_NVLIST, if bootenv currently
 * does contain other version, then old data will be replaced.
 */
int
lzbe_set_boot_device(const char *pool, lzbe_flags_t flag, const char *device)
{
	libzfs_handle_t *hdl;
	zpool_handle_t *zphdl;
	nvlist_t *nv;
	char *descriptor;
	uint64_t version;
	int rv = -1;

	if (pool == NULL || *pool == '\0')
		return (rv);

	if ((hdl = libzfs_init()) == NULL)
		return (rv);

	zphdl = zpool_open(hdl, pool);
	if (zphdl == NULL) {
		libzfs_fini(hdl);
		return (rv);
	}

	switch (flag) {
	case lzbe_add:
		rv = zpool_get_bootenv(zphdl, &nv);
		if (rv == 0) {
			/*
			 * We got the nvlist, check for version.
			 * if version is missing or is not VB_NVLIST,
			 * create new list.
			 */
			rv = nvlist_lookup_uint64(nv, BOOTENV_VERSION,
			    &version);
			if (rv == 0 && version == VB_NVLIST)
				break;

			/* Drop this nvlist */
			fnvlist_free(nv);
		}
		fallthrough;
	case lzbe_replace:
		nv = fnvlist_alloc();
		break;
	default:
		return (rv);
	}

	/* version is mandatory */
	fnvlist_add_uint64(nv, BOOTENV_VERSION, VB_NVLIST);

	/*
	 * If device name is empty, remove boot device configuration.
	 */
	if ((device == NULL || *device == '\0')) {
		if (nvlist_exists(nv, OS_BOOTONCE))
			fnvlist_remove(nv, OS_BOOTONCE);
	} else {
		/*
		 * Use device name directly if it does start with
		 * prefix "zfs:". Otherwise, add prefix and suffix.
		 */
		if (strncmp(device, "zfs:", 4) == 0) {
			fnvlist_add_string(nv, OS_BOOTONCE, device);
		} else {
			if (asprintf(&descriptor, "zfs:%s:", device) > 0) {
				fnvlist_add_string(nv, OS_BOOTONCE, descriptor);
				free(descriptor);
			} else
				rv = ENOMEM;
		}
	}

	rv = zpool_set_bootenv(zphdl, nv);
	if (rv != 0)
		fprintf(stderr, "%s\n", libzfs_error_description(hdl));

	fnvlist_free(nv);
	zpool_close(zphdl);
	libzfs_fini(hdl);
	return (rv);
}

/*
 * Return boot device name from bootenv, if set.
 */
int
lzbe_get_boot_device(const char *pool, char **device)
{
	libzfs_handle_t *hdl;
	zpool_handle_t *zphdl;
	nvlist_t *nv;
	char *val;
	int rv = -1;

	if (pool == NULL || *pool == '\0' || device == NULL)
		return (rv);

	if ((hdl = libzfs_init()) == NULL)
		return (rv);

	zphdl = zpool_open(hdl, pool);
	if (zphdl == NULL) {
		libzfs_fini(hdl);
		return (rv);
	}

	rv = zpool_get_bootenv(zphdl, &nv);
	if (rv == 0) {
		rv = nvlist_lookup_string(nv, OS_BOOTONCE, &val);
		if (rv == 0) {
			/*
			 * zfs device descriptor is in form of "zfs:dataset:",
			 * we only do need dataset name.
			 */
			if (strncmp(val, "zfs:", 4) == 0) {
				val += 4;
				val = strdup(val);
				if (val != NULL) {
					size_t len = strlen(val);

					if (val[len - 1] == ':')
						val[len - 1] = '\0';
					*device = val;
				} else {
					rv = ENOMEM;
				}
			} else {
				rv = EINVAL;
			}
		}
		nvlist_free(nv);
	}

	zpool_close(zphdl);
	libzfs_fini(hdl);
	return (rv);
}
