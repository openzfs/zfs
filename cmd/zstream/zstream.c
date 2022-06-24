/*
 * CDDL HEADER START
 *
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this
 * source.  A copy of the CDDL is also available via the Internet at
 * http://www.illumos.org/license/CDDL.
 *
 * CDDL HEADER END
 */

/*
 * Copyright (c) 2020 by Delphix. All rights reserved.
 * Copyright (c) 2020 by Datto Inc. All rights reserved.
 */
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <libintl.h>
#include <stddef.h>
#include <libzfs.h>
#include "zstream.h"

void
zstream_usage(void)
{
	(void) fprintf(stderr,
	    "usage: zstream command args ...\n"
	    "Available commands are:\n"
	    "\n"
	    "\tzstream dump [-vCd] FILE\n"
	    "\t... | zstream dump [-vCd]\n"
	    "\n"
	    "\tzstream decompress [-v] [OBJECT,OFFSET[,TYPE]] ...\n"
	    "\n"
	    "\tzstream token resume_token\n"
	    "\n"
	    "\tzstream redup [-v] FILE | ...\n");
	exit(1);
}

int
main(int argc, char *argv[])
{
	char *basename = strrchr(argv[0], '/');
	basename = basename ? (basename + 1) : argv[0];
	if (argc >= 1 && strcmp(basename, "zstreamdump") == 0)
		return (zstream_do_dump(argc, argv));

	if (argc < 2)
		zstream_usage();

	char *subcommand = argv[1];

	if (strcmp(subcommand, "dump") == 0) {
		return (zstream_do_dump(argc - 1, argv + 1));
	} else if (strcmp(subcommand, "decompress") == 0) {
		return (zstream_do_decompress(argc - 1, argv + 1));
	} else if (strcmp(subcommand, "token") == 0) {
		return (zstream_do_token(argc - 1, argv + 1));
	} else if (strcmp(subcommand, "redup") == 0) {
		return (zstream_do_redup(argc - 1, argv + 1));
	} else {
		zstream_usage();
	}
}
