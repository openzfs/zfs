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
 * Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

/*
 * Copyright (c) 2013 by Delphix. All rights reserved.
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <pthread.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>

/*
 * The size of the output file, "go.out", should be 80*8192*2 = 1310720
 *
 * $ cd /tmp; go; ls -l go.out
 * done.
 * -rwxr-xr-x	1 jdm	staff	1310720 Apr 13 19:45 go.out
 * $ cd /zfs; go; ls -l go.out
 * done.
 * -rwxr-xr-x	1 jdm	staff	663552 Apr 13 19:45 go.out
 *
 * The file on zfs is short as it does not appear that zfs is making the
 * implicit seek to EOF and the actual write atomic. From the SUSv3
 * interface spec, behavior is undefined if concurrent writes are performed
 * from multi-processes to a single file. So I don't know if this is a
 * standards violation, but I cannot find any such disclaimers in our
 * man pages. This issue came up at a customer site in another context, and
 * the suggestion was to open the file with O_APPEND, but that wouldn't
 * help with zfs(see 4977529). Also see bug# 5031301.
 */

static int outfd = 0;

static void *
go(void *data)
{
	int ret, i = 0, n = *(int *)data;
	char buf[8192] = {0};
	(void) memset(buf, n, sizeof (buf));

	for (i = 0; i < 80; i++) {
		ret = write(outfd, buf, sizeof (buf));
		if (ret != sizeof (buf))
			perror("write");
	}
	return (NULL);
}

static void
usage(void)
{
	(void) fprintf(stderr,
	    "usage: zfs_threadsappend <file name>\n");
	exit(1);
}

int
main(int argc, char **argv)
{
	pthread_t tid[2];
	int	ret = 0;
	long	ncpus = 0;
	int	i;

	if (argc != 2) {
		usage();
	}

	ncpus = sysconf(_SC_NPROCESSORS_ONLN);
	if (ncpus < 0) {
		(void) fprintf(stderr,
		    "Invalid return from sysconf(_SC_NPROCESSORS_ONLN)"
		    " : errno (decimal)=%d\n", errno);
		exit(1);
	}
	if (ncpus < 2) {
		(void) fprintf(stderr,
		    "Must execute this binary on a multi-processor system\n");
		exit(1);
	}

	outfd = open(argv[optind++], O_RDWR|O_CREAT|O_APPEND|O_TRUNC, 0777);
	if (outfd == -1) {
		(void) fprintf(stderr,
		    "zfs_threadsappend: "
		    "open(%s, O_RDWR|O_CREAT|O_APPEND|O_TRUNC, 0777)"
		    " failed\n", argv[optind]);
		perror("open");
		exit(1);
	}

	for (i = 0; i < 2; i++) {
		ret = pthread_create(&tid[i], NULL, go, (void *)&i);
		if (ret != 0) {
			(void) fprintf(stderr,
			    "zfs_threadsappend: thr_create(#%d) "
			    "failed error=%d\n", i+1, ret);
			exit(1);
		}
	}

	for (i = 0; i < 2; i++)
		(void) pthread_join(tid[i], NULL);

	return (0);
}
