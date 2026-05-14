// SPDX-License-Identifier: CDDL-1.0
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
#include <signal.h>
#include <sys/backtrace.h>
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
	    "\tzstream drop_record [-v] [OBJECT,OFFSET] ...\n"
	    "\n"
	    "\tzstream recompress [ -l level] TYPE\n"
	    "\n"
	    "\tzstream token resume_token\n"
	    "\n"
	    "\tzstream redup [-v] FILE | ...\n");
	exit(1);
}

static void sig_handler(int signo)
{
	struct sigaction action;
	libspl_backtrace(STDERR_FILENO);

	/*
	 * Restore default action and re-raise signal so SIGSEGV and
	 * SIGABRT can trigger a core dump.
	 */
	action.sa_handler = SIG_DFL;
	sigemptyset(&action.sa_mask);
	action.sa_flags = 0;
	(void) sigaction(signo, &action, NULL);
	raise(signo);
}


int
main(int argc, char *argv[])
{
	/*
	 * Set up signal handlers, so if we crash due to bad data in the stream
	 * we can get more info. Unlike ztest, we don't bail out if we can't
	 * set up signal handlers, because zstream is very useful without them.
	 */
	struct sigaction action = { .sa_handler = sig_handler };
	sigemptyset(&action.sa_mask);
	action.sa_flags = 0;
	if (sigaction(SIGSEGV, &action, NULL) < 0) {
		(void) fprintf(stderr, "zstream: cannot catch SIGSEGV: %s\n",
		    strerror(errno));
	}
	if (sigaction(SIGABRT, &action, NULL) < 0) {
		(void) fprintf(stderr, "zstream: cannot catch SIGABRT: %s\n",
		    strerror(errno));
	}

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
	} else if (strcmp(subcommand, "drop_record") == 0) {
		return (zstream_do_drop_record(argc - 1, argv + 1));
	} else if (strcmp(subcommand, "recompress") == 0) {
		return (zstream_do_recompress(argc - 1, argv + 1));
	} else if (strcmp(subcommand, "token") == 0) {
		return (zstream_do_token(argc - 1, argv + 1));
	} else if (strcmp(subcommand, "redup") == 0) {
		return (zstream_do_redup(argc - 1, argv + 1));
	} else {
		zstream_usage();
	}
}
