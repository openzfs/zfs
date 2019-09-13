
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

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <libintl.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <unistd.h>
#include <stddef.h>
#include <fcntl.h>
#include <sys/mount.h>
#include <sys/mntent.h>
#include <sys/mnttab.h>
#include <sys/avl.h>
#include <sys/debug.h>
#include <sys/stat.h>
#include <stddef.h>
#include <pthread.h>
#include <umem.h>
#include <time.h>
#include <libzutil.h>

int
zutil_set_pipe_max(int infd)
{
	struct stat sb;

	/*
	 * The only way fstat can fail is if we do not have a valid file
	 * descriptor.
	 */
	if (fstat(infd, &sb) == -1) {
		perror("fstat");
		return (-2);
	}

#ifndef F_SETPIPE_SZ
#define	F_SETPIPE_SZ (F_SETLEASE + 7)
#endif /* F_SETPIPE_SZ */

#ifndef F_GETPIPE_SZ
#define	F_GETPIPE_SZ (F_GETLEASE + 7)
#endif /* F_GETPIPE_SZ */

	/*
	 * It is not uncommon for gigabytes to be processed in zfs receive.
	 * Speculatively increase the buffer size via Linux-specific fcntl()
	 * call.
	 */
	if (S_ISFIFO(sb.st_mode)) {
		FILE *procf = fopen("/proc/sys/fs/pipe-max-size", "r");

		if (procf != NULL) {
			unsigned long max_psize;
			long cur_psize;
			if (fscanf(procf, "%lu", &max_psize) > 0) {
				cur_psize = fcntl(infd, F_GETPIPE_SZ);
				if (cur_psize > 0 &&
				    max_psize > (unsigned long) cur_psize)
					(void) fcntl(infd, F_SETPIPE_SZ,
					    max_psize);
			}
			fclose(procf);
		}
	}
	return (0);
}
