// SPDX-License-Identifier: CDDL-1.0
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

/* Test for zfs channel program support */

#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/nvpair.h>
#include <sys/zfs_ioctl.h>
#include <sys/fs/zfs.h>


int
main(int argc, const char *const *argv)
{
	if (argc != 2)
		errx(EXIT_FAILURE, "usage: %s pool", argv[0]);

	zfs_cmd_t zc = {};
	(void) strlcpy(zc.zc_name, argv[1], sizeof (zc.zc_name));

	char prog[8192] = {};
	snprintf(prog, 8192, "zfs.exists(\"%s\")", zc.zc_name);

	int fd = open(ZFS_DEV, O_RDWR);
	if (fd == -1)
		err(EXIT_FAILURE, "%s", ZFS_DEV);


	nvlist_t *zc_args = fnvlist_alloc();
	fnvlist_add_string(zc_args, ZCP_ARG_PROGRAM, prog);

	size_t sz = 0;
	char *packed_nvl = fnvlist_pack(zc_args, &sz);
	zc.zc_nvlist_src = (uint64_t)(uintptr_t)packed_nvl;
	zc.zc_nvlist_src_size = sz;

	int rc = 0;

#if defined(DISABLE_ZCP)
	/*
	 * EXIT_FAILURE so channel program tests can be skipped
	 * but make sure the correct error code was returned
	 */
	if (ioctl(fd, ZFS_IOC_CHANNEL_PROGRAM, &zc) == ZFS_ERR_IOC_CMD_UNAVAIL)
		rc = EXIT_FAILURE;
#else
	/*
	 * Depending on zone, disks might not been prepared yet.
	 * Channel program may return any value other than CMD_UNAVAIL
	 */
	if (ioctl(fd, ZFS_IOC_CHANNEL_PROGRAM, &zc) != ZFS_ERR_IOC_CMD_UNAVAIL)
		rc = EXIT_SUCCESS;
#endif

	close(fd);
	fnvlist_free(zc_args);
	fnvlist_pack_free(packed_nvl, sz);
	return (rc);
}
