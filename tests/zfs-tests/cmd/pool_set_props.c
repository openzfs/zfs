// SPDX-License-Identifier: CDDL-1.0
/*
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this
 * source.  A copy of the CDDL is also available via the Internet at
 * https://opensource.org/license/CDDL-1.0.
 */

/*
 * Pass a list of pool properties to ZFS_IOC_POOL_SET_PROPS as it is given,
 * without the checks that "zpool set" makes, to check that the kernel makes
 * its own.
 *
 * pool_set_props POOL NAME=TYPE[:VALUE] ...
 *	TYPE is string, uint64, or boolean, which takes no value.
 *
 * Prints the name of the error the call returned, if any, and exits with
 * 0 only when there was none.
 */

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <libzfs_core.h>
#include <sys/zfs_ioctl.h>

static void
usage(void)
{
	(void) fprintf(stderr,
	    "usage: pool_set_props POOL NAME=TYPE[:VALUE] ...\n"
	    "       TYPE: string, uint64 or boolean (no value)\n");
	exit(2);
}

/* The number of an error depends on the platform, its name does not. */
static const char *
errname(int err)
{
	switch (err) {
	case EINVAL:
		return ("EINVAL");
	case E2BIG:
		return ("E2BIG");
	case ENAMETOOLONG:
		return ("ENAMETOOLONG");
	default:
		return (strerror(err));
	}
}

static void
add_prop(nvlist_t *props, char *name)
{
	char *type, *val, *end;
	uint64_t num;

	if ((type = strchr(name, '=')) == NULL || type == name)
		usage();
	*type++ = '\0';
	if ((val = strchr(type, ':')) != NULL)
		*val++ = '\0';

	if (strcmp(type, "boolean") == 0 && val == NULL) {
		fnvlist_add_boolean(props, name);
	} else if (strcmp(type, "string") == 0 && val != NULL) {
		fnvlist_add_string(props, name, val);
	} else if (strcmp(type, "uint64") == 0 && val != NULL) {
		errno = 0;
		num = strtoull(val, &end, 0);
		if (errno != 0 || end == val || *end != '\0')
			usage();
		fnvlist_add_uint64(props, name, num);
	} else {
		usage();
	}
}

int
main(int argc, char *argv[])
{
	zfs_cmd_t zc;
	nvlist_t *props;
	char *packed;
	size_t size;
	int fd, err;

	if (argc < 3)
		usage();

	props = fnvlist_alloc();
	for (int i = 2; i < argc; i++)
		add_prop(props, argv[i]);
	packed = fnvlist_pack(props, &size);

	memset(&zc, 0, sizeof (zc));
	(void) strlcpy(zc.zc_name, argv[1], sizeof (zc.zc_name));
	zc.zc_nvlist_src = (uint64_t)(uintptr_t)packed;
	zc.zc_nvlist_src_size = size;

	if ((fd = open(ZFS_DEV, O_RDWR)) < 0) {
		(void) fprintf(stderr, "open(%s): %s\n", ZFS_DEV,
		    strerror(errno));
		return (2);
	}
	err = lzc_ioctl_fd(fd, ZFS_IOC_POOL_SET_PROPS, &zc);
	if (err != 0)
		err = errno;
	(void) close(fd);

	fnvlist_pack_free(packed, size);
	fnvlist_free(props);

	if (err != 0) {
		(void) printf("%s\n", errname(err));
		return (1);
	}
	return (0);
}
