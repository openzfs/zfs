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
/*
 * Copyright (c) 2020 by Delphix. All rights reserved.
 */
#include <libintl.h>
#include <unistd.h>
#include <time.h>
#include <sys/types.h>
#include <stdint.h>
#include <libzfs.h>
#include <libzutil.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

boolean_t parseable = B_FALSE;

static void
usage(int err)
{
	fprintf(stderr, "Usage: [-p] zfs_file <filename> ...\n");
	exit(err);
}

static void
zfs_do_file(const char *filename)
{
	zfs_access_info_t zai;
	int err = zfs_get_access_info(filename, &zai);
	if (err != 0) {
		fprintf(stderr, "zfs_get_access_info failed for '%s': %s\n",
		    filename, strerror(err));
		if (err == ENOENT)
			return;
		exit(1);
	}
	time_t start = zai.zai_start;
	if (parseable) {
		printf("%llu %llu %llu %s\n",
		    (long long)start,
		    (long long)zai.zai_accessed_bytes,
		    (long long)zai.zai_total_bytes,
		    filename);
	} else {
		char accessed_buf[32];
		char total_buf[32];
		zfs_nicenum(zai.zai_accessed_bytes,
		    accessed_buf, sizeof (accessed_buf));
		zfs_nicenum(zai.zai_total_bytes, total_buf, sizeof (total_buf));
		printf("%sB (out of %sB, %u%%) of file '%s' "
		    "has been accessed since %s",
		    accessed_buf,
		    total_buf,
		    zai.zai_total_bytes == 0 ? 100 :
		    (int)(zai.zai_accessed_bytes * 100 /
		    zai.zai_total_bytes),
		    filename,
		    ctime(&start));
	}
}

int
main(int argc, char **argv)
{
	char c;
	while ((c = getopt(argc, argv, "p")) != -1) {
		switch (c) {
		case 'p':
			parseable = B_TRUE;
			break;
		}
	}
	argc -= optind;
	argv += optind;

	if (argc < 1) {
		(void) fprintf(stderr, "Missing filename argument\n");
		usage(1);
	}

	for (int i = 0; i < argc; i++) {
		zfs_do_file(argv[i]);
	}

	return (0);
}
