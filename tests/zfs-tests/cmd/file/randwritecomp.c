/*
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this
 * source.  A copy of the CDDL is also available via the Internet at
 * http://www.illumos.org/license/CDDL.
 */

/*
 * Copyright (c) 2017 by Delphix. All rights reserved.
 */

#include <stdint.h>
#include <string.h>
#include "file_common.h"

/*
 * The following sample was derived from real-world data
 * of a production Oracle database.
 */
static const uint64_t size_distribution[] = {
	0,
	1499018,
	352084,
	1503485,
	4206227,
	5626657,
	5387001,
	3733756,
	2233094,
	874652,
	238635,
	81434,
	33357,
	13106,
	2009,
	1,
	23660,
};


static uint64_t distribution_n;

static uint8_t randbuf[BLOCKSZ];

static void
rwc_pwrite(int fd, const void *buf, size_t nbytes, off_t offset)
{
	size_t nleft = nbytes;
	ssize_t nwrite = 0;

	nwrite = pwrite(fd, buf, nbytes, offset);
	if (nwrite < 0) {
		perror("pwrite");
		exit(EXIT_FAILURE);
	}

	nleft -= nwrite;
	if (nleft != 0) {
		(void) fprintf(stderr, "warning: pwrite: "
		    "wrote %zu out of %zu bytes\n",
		    (nbytes - nleft), nbytes);
	}
}

static void
fillbuf(char *buf)
{
	uint64_t rv = lrand48() % distribution_n;
	uint64_t sum = 0;

	uint64_t i;
	for (i = 0;
	    i < sizeof (size_distribution) / sizeof (size_distribution[0]);
	    i++) {
		sum += size_distribution[i];
		if (rv < sum)
			break;
	}

	memcpy(buf, randbuf, BLOCKSZ);
	if (i == 0)
		memset(buf, 0, BLOCKSZ - 10);
	else if (i < 16)
		memset(buf, 0, BLOCKSZ - i * 512 + 256);
	/*LINTED: E_BAD_PTR_CAST_ALIGN*/
	((uint32_t *)buf)[0] = lrand48();
}

static void
exit_usage(void)
{
	(void) puts("usage: randwritecomp [-s] file [nwrites]");
	exit(EXIT_FAILURE);
}

static void
sequential_writes(int fd, char *buf, uint64_t nblocks, int64_t n)
{
	for (int64_t i = 0; n == -1 || i < n; i++) {
		fillbuf(buf);

		static uint64_t j = 0;
		if (j == 0)
			j = lrand48() % nblocks;
		rwc_pwrite(fd, buf, BLOCKSZ, j * BLOCKSZ);
		j++;
		if (j >= nblocks)
			j = 0;
	}
}

static void
random_writes(int fd, char *buf, uint64_t nblocks, int64_t n)
{
	for (int64_t i = 0; n == -1 || i < n; i++) {
		fillbuf(buf);
		rwc_pwrite(fd, buf, BLOCKSZ, (lrand48() % nblocks) * BLOCKSZ);
	}
}

int
main(int argc, char *argv[])
{
	int fd, err;
	char *filename = NULL;
	char buf[BLOCKSZ];
	struct stat ss;
	uint64_t nblocks;
	int64_t n = -1;
	int sequential = 0;

	if (argc < 2)
		exit_usage();

	argv++;
	if (strcmp("-s", argv[0]) == 0) {
		sequential = 1;
		argv++;
	}

	if (argv[0] == NULL)
		exit_usage();
	else
		filename = argv[0];

	argv++;
	if (argv[0] != NULL)
		n = strtoull(argv[0], NULL, 0);

	fd = open(filename, O_RDWR|O_CREAT, 0666);
	if (fd == -1) {
		(void) fprintf(stderr, "open(%s) failed: %s\n", filename,
		    strerror(errno));
		exit(EXIT_FAILURE);
	}
	err = fstat(fd, &ss);
	if (err != 0) {
		(void) fprintf(stderr,
		    "error: fstat returned error code %d\n", err);
		exit(EXIT_FAILURE);
	}

	nblocks = ss.st_size / BLOCKSZ;
	if (nblocks == 0) {
		(void) fprintf(stderr, "error: "
		    "file is too small (min allowed size is %d bytes)\n",
		    BLOCKSZ);
		exit(EXIT_FAILURE);
	}

	srand48(getpid());
	for (int i = 0; i < BLOCKSZ; i++)
		randbuf[i] = lrand48();

	distribution_n = 0;
	for (uint64_t i = 0;
	    i < sizeof (size_distribution) / sizeof (size_distribution[0]);
	    i++) {
		distribution_n += size_distribution[i];
	}

	if (sequential)
		sequential_writes(fd, buf, nblocks, n);
	else
		random_writes(fd, buf, nblocks, n);

	return (0);
}
