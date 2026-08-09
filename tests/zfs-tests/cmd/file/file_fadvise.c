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
 * Copyright (c) 2022 by Information2 Software, Inc. All rights reserved.
 */

#include "file_common.h"
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>

/*
 * Call fadvise to prefetch data
 */
static const char *execname = "file_fadvise";

static void
usage(void)
{
	(void) fprintf(stderr,
	    "usage: %s -f filename -a advice \n", execname);
}

typedef struct advice_name {
	const char *name;
	int value;
} advice_name;

static const struct advice_name table[] = {
#define	ADV(name) {#name, name}
	ADV(POSIX_FADV_NORMAL),
	ADV(POSIX_FADV_RANDOM),
	ADV(POSIX_FADV_SEQUENTIAL),
	ADV(POSIX_FADV_WILLNEED),
	ADV(POSIX_FADV_DONTNEED),
	ADV(POSIX_FADV_NOREUSE),
	{NULL}
};

int
main(int argc, char *argv[])
{
	char *filename = NULL;
	int advice = POSIX_FADV_NORMAL;
	int fd, ch;
	int	err = 0;

	while ((ch = getopt(argc, argv, "a:f:")) != EOF) {
		switch (ch) {
		case 'a':
			advice = -1;
			for (const advice_name *p = table; p->name; ++p) {
				if (strcmp(p->name, optarg) == 0)
					advice = p->value;
			}
			break;
		case 'f':
			filename = optarg;
			break;
		case '?':
			(void) printf("unknown arg %c\n", optopt);
			usage();
			break;
		}
	}

	if (!filename) {
		(void) printf("Filename not specified (-f <file>)\n");
		err++;
	}

	if (advice == -1) {
		(void) printf("advice is invalid\n");
		err++;
	}

	if (err) {
		usage(); /* no return */
		return (1);
	}

	if ((fd = open(filename, O_RDWR, 0666)) < 0) {
		perror("open");
		return (1);
	}

	if (posix_fadvise(fd, 0, 0, advice) != 0) {
		perror("posix_fadvise");
		close(fd);
		return (1);
	}

	close(fd);

	return (0);
}
