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
 * Set several properties of one vdev in a single ZFS_IOC_VDEV_SET_PROPS
 * call, which "zpool set" cannot do.  A value made only of digits is passed
 * as a number and anything else as a string, without the checks that
 * "zpool set" makes.  The exit status is the error the call returned.
 */

#include <libzfs_core.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>

static void
usage(const char *name)
{
	(void) fprintf(stderr, "usage: %s pool vdev-guid prop=value ...\n",
	    name);
	exit(EX_USAGE);
}

int
main(int argc, char *argv[])
{
	nvlist_t *innvl, *props, *outnvl = NULL;
	uint64_t guid;
	char *end;
	int error;

	if (argc < 4)
		usage(argv[0]);

	errno = 0;
	guid = strtoull(argv[2], &end, 0);
	if (errno != 0 || *end != '\0')
		usage(argv[0]);

	props = fnvlist_alloc();
	for (int i = 3; i < argc; i++) {
		char *val = strchr(argv[i], '=');

		if (val == NULL || val == argv[i])
			usage(argv[0]);
		*val++ = '\0';
		if (*val != '\0' && val[strspn(val, "0123456789")] == '\0')
			fnvlist_add_uint64(props, argv[i],
			    strtoull(val, NULL, 10));
		else
			fnvlist_add_string(props, argv[i], val);
	}

	innvl = fnvlist_alloc();
	fnvlist_add_uint64(innvl, ZPOOL_VDEV_PROPS_SET_VDEV, guid);
	fnvlist_add_nvlist(innvl, ZPOOL_VDEV_PROPS_SET_PROPS, props);

	if ((error = libzfs_core_init()) != 0) {
		(void) fprintf(stderr, "libzfs_core_init: %s\n",
		    strerror(error));
		return (error);
	}
	error = lzc_set_vdev_prop(argv[1], innvl, &outnvl);
	if (error != 0)
		(void) fprintf(stderr, "%s\n", strerror(error));
	libzfs_core_fini();

	nvlist_free(outnvl);
	fnvlist_free(innvl);
	fnvlist_free(props);
	return (error);
}
