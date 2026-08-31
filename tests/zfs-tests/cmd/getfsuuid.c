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
 * Print the filesystem UUID of the given path, from the Linux
 * FS_IOC_GETFSUUID ioctl.  The output is one line with three fields:
 *
 *   <uuid as 32 hex digits> <pool guid decimal> <dataset guid decimal>
 *
 * The two guid fields decode the two 64-bit big-endian halves of the
 * UUID, which is how ZFS constructs it.
 */

#include <sys/ioctl.h>
#include <fcntl.h>
#include <linux/fs.h>
#include <err.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

/*
 * FS_IOC_GETFSUUID was added in Linux 6.9.  Supply the definitions when
 * the installed kernel headers do not have them.
 */
#ifndef FS_IOC_GETFSUUID
struct fsuuid2 {
	__u8	len;
	__u8	uuid[16];
};

#define	FS_IOC_GETFSUUID	_IOR(0x15, 0, struct fsuuid2)
#endif

int
main(int argc, const char * const argv[])
{
	struct fsuuid2 fu;
	uint64_t half[2] = { 0, 0 };

	if (argc != 2)
		errx(EXIT_FAILURE, "usage: %s <path>", argv[0]);

	int fd = open(argv[1], O_RDONLY);
	if (fd == -1)
		err(EXIT_FAILURE, "failed to open %s", argv[1]);

	if (ioctl(fd, FS_IOC_GETFSUUID, &fu) == -1)
		err(EXIT_FAILURE, "FS_IOC_GETFSUUID failed");

	(void) close(fd);

	if (fu.len != sizeof (fu.uuid))
		errx(EXIT_FAILURE, "unexpected uuid length %d", fu.len);

	for (int i = 0; i < 16; i++) {
		(void) printf("%02x", fu.uuid[i]);
		half[i / 8] = (half[i / 8] << 8) | fu.uuid[i];
	}

	(void) printf(" %" PRIu64 " %" PRIu64 "\n", half[0], half[1]);

	return (EXIT_SUCCESS);
}
