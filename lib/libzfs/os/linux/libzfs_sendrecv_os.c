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


#include <libzfs.h>

#include "libzfs_impl.h"

#ifndef F_SETPIPE_SZ
#define	F_SETPIPE_SZ (F_SETLEASE + 7)
#endif /* F_SETPIPE_SZ */

#ifndef F_GETPIPE_SZ
#define	F_GETPIPE_SZ (F_GETLEASE + 7)
#endif /* F_GETPIPE_SZ */

void
libzfs_set_pipe_max(int infd)
{
	FILE *procf = fopen("/proc/sys/fs/pipe-max-size", "r");

	if (procf != NULL) {
		unsigned long max_psize;
		long cur_psize;
		if (fscanf(procf, "%lu", &max_psize) > 0) {
			cur_psize = fcntl(infd, F_GETPIPE_SZ);
			if (cur_psize > 0 &&
			    max_psize > (unsigned long) cur_psize)
				fcntl(infd, F_SETPIPE_SZ,
				    max_psize);
		}
		fclose(procf);
	}
}
