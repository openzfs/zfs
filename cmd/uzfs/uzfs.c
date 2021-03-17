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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <libuzfs.h>
#include <sys/zil.h>
#include <sys/zvol.h>

/*
 * uzfs utility act like a kernel to the zpool and zfs
 * commands.
 * All it has to do is call libuzfs_ioctl_init(),
 * which will take care of everything.
 * Make sure you have done kernel_init before calling this.
 */
int
main(int argc, char *argv[])
{
	/*
	 * take a lock of LOCK_FILE to prevent parallel run
	 * of uzfs server
	 */
	int fd = open(LOCK_FILE, O_CREAT | O_RDWR, 0644);
	if (fd < 0) {
		fprintf(stderr, "%s open failed: %s\n", LOCK_FILE,
		    strerror(errno));
		return (-1);
	}

	if (flock(fd, LOCK_EX) < 0) {
		fprintf(stderr, "flock failed: %s\n", strerror(errno));
		return (-1);
	}

	kernel_init(SPA_MODE_READ | SPA_MODE_WRITE);

	if (libuzfs_ioctl_init() < 0) {
		(void) fprintf(stderr, "%s",
		    "failed to initialize libuzfs ioctl\n");
		goto err;
	}

	while (1) {
		sleep(5);
		/* other stuffs */
	}

err:
	kernel_fini();
	return (0);
}
