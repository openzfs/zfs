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
 * Copyright (c) 2012 by Delphix. All rights reserved.
 */

#include "file_common.h"
#include <sys/param.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/stdtypes.h>
#include <unistd.h>

/*
 * --------------------------------------------------------------
 *
 *	Assertion:
 *		The last byte of the largest file size can be
 *		accessed without any errors.  Also, the writing
 *		beyond the last byte of the largest file size
 *		will produce an errno of EFBIG.
 *
 * --------------------------------------------------------------
 *	If the write() system call below returns a "1",
 *	then the last byte can be accessed.
 * --------------------------------------------------------------
 */
static void	sigxfsz(int);
static void	usage(char *);

int
main(int argc, char **argv)
{
	int		fd = 0;
	offset_t	offset = (MAXOFFSET_T - 1);
	offset_t	llseek_ret = 0;
	int		write_ret = 0;
	int		err = 0;
	char		mybuf[5] = "aaaa";
	char		*testfile;
	mode_t		mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;
	struct sigaction sa;

	if (argc != 2) {
		usage(argv[0]);
	}

	if (sigemptyset(&sa.sa_mask) == -1)
		return (errno);
	sa.sa_flags = 0;
	sa.sa_handler = sigxfsz;
	if (sigaction(SIGXFSZ, &sa, NULL) == -1)
		return (errno);

	testfile = strdup(argv[1]);
	if (testfile == NULL)
		return (errno);

	fd = open(testfile, O_CREAT | O_RDWR, mode);
	if (fd < 0) {
		err = errno;
		perror("Failed to create testfile");
		free(testfile);
		return (err);
	}

	llseek_ret = lseek64(fd, offset, SEEK_SET);
	if (llseek_ret < 0) {
		err = errno;
		perror("Failed to seek to end of testfile");
		goto out;
	}

	write_ret = write(fd, mybuf, 1);
	if (write_ret < 0) {
		err = errno;
		perror("Failed to write to end of file");
		goto out;
	}

	offset = 0;
	llseek_ret = lseek64(fd, offset, SEEK_CUR);
	if (llseek_ret < 0) {
		err = errno;
		perror("Failed to seek to end of file");
		goto out;
	}

	write_ret = write(fd, mybuf, 1);
	if (write_ret < 0) {
		if (errno == EFBIG || errno == EINVAL) {
			(void) printf("write errno=EFBIG|EINVAL: success\n");
			err = 0;
		} else {
			err = errno;
			perror("Did not receive EFBIG");
		}
	} else {
		(void) printf("write completed successfully, test failed\n");
		err = 1;
	}

out:
	(void) unlink(testfile);
	free(testfile);
	close(fd);
	return (err);
}

static void
usage(char *name)
{
	(void) printf("%s <testfile>\n", name);
	exit(1);
}

static void
sigxfsz(int signo)
{
	(void) signo;
	(void) printf("\nlargest_file: sigxfsz() caught SIGXFSZ\n");
}
