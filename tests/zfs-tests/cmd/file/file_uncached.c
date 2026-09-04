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
 * Copyright (c) 2026, TrueNAS.
 */

/*
 * Read or write a file with a hint that the data will not be reused, writing
 * a per-block pattern that the read side verifies.  The hint is either
 * RWF_DONTCACHE on each I/O (-d, Linux only) or POSIX_FADV_NOREUSE on the
 * descriptor (-a).  With neither, the same I/O is done unhinted, so a test
 * can check that all paths see the same data.  Exits 2 if RWF_DONTCACHE is
 * not supported, which the caller reports as an unsupported test.
 */

#include "file_common.h"
#include <sys/uio.h>

/*
 * The RWF_DONTCACHE flag reaches the kernel through preadv2()/pwritev2().
 * Every kernel version ZFS supports has those syscalls, but some libcs do not
 * wrap them, so the flag can only be offered where the wrappers exist.
 */
#if defined(HAVE_PREADV2) && defined(HAVE_PWRITEV2)
#define	HAVE_DONTCACHE
#ifndef RWF_DONTCACHE
#define	RWF_DONTCACHE	0x00000080
#endif
#endif

#define	EXIT_UNSUPPORTED	2

static const char *execname = "file_uncached";

static void
usage(void)
{
	(void) fprintf(stderr,
	    "usage: %s [-a] [-d] [-r] -f filename -b blocksize -c count\n",
	    execname);
}

static ssize_t
do_io(int fd, int doread, int dontcache, void *buf, size_t len, off_t off)
{
#ifdef HAVE_DONTCACHE
	if (dontcache) {
		struct iovec iov = { .iov_base = buf, .iov_len = len };

		return (doread ?
		    preadv2(fd, &iov, 1, off, RWF_DONTCACHE) :
		    pwritev2(fd, &iov, 1, off, RWF_DONTCACHE));
	}
#else
	(void) dontcache;
#endif
	return (doread ? pread(fd, buf, len, off) :
	    pwrite(fd, buf, len, off));
}

int
main(int argc, char *argv[])
{
	const char *filename = NULL;
	size_t blocksize = BLOCKSZ;
	long count = 0, i;
	int doread = 0, dontcache = 0, noreuse = 0, ch, fd;
	char *buf;

	while ((ch = getopt(argc, argv, "ab:c:df:r")) != EOF) {
		switch (ch) {
		case 'a':
			noreuse = 1;
			break;
		case 'b':
			blocksize = strtoul(optarg, NULL, 0);
			break;
		case 'c':
			count = strtol(optarg, NULL, 0);
			break;
		case 'd':
			dontcache = 1;
			break;
		case 'f':
			filename = optarg;
			break;
		case 'r':
			doread = 1;
			break;
		default:
			usage();
			return (1);
		}
	}

	if (filename == NULL || blocksize == 0 || count <= 0) {
		usage();
		return (1);
	}

#ifndef HAVE_DONTCACHE
	if (dontcache) {
		(void) fprintf(stderr, "RWF_DONTCACHE is not available\n");
		return (EXIT_UNSUPPORTED);
	}
#endif

	if ((buf = malloc(blocksize)) == NULL) {
		perror("malloc");
		return (1);
	}

	fd = open(filename, doread ? O_RDONLY : O_WRONLY | O_CREAT, 0666);
	if (fd < 0) {
		perror("open");
		return (1);
	}

	if (noreuse && posix_fadvise(fd, 0, 0, POSIX_FADV_NOREUSE) != 0) {
		perror("posix_fadvise");
		return (1);
	}

	for (i = 0; i < count; i++) {
		off_t off = (off_t)i * blocksize;
		unsigned char pattern = i & 0xff;
		ssize_t n;
		size_t j;

		if (!doread)
			(void) memset(buf, pattern, blocksize);

		n = do_io(fd, doread, dontcache, buf, blocksize, off);
		if (n < 0) {
			/*
			 * Both a kernel without RWF_DONTCACHE and a filesystem
			 * that has not set FOP_DONTCACHE report EOPNOTSUPP.
			 */
			if (dontcache &&
			    (errno == EOPNOTSUPP || errno == ENOSYS)) {
				(void) fprintf(stderr,
				    "RWF_DONTCACHE is not supported\n");
				return (EXIT_UNSUPPORTED);
			}
			perror(doread ? "pread" : "pwrite");
			return (1);
		}
		if ((size_t)n != blocksize) {
			(void) fprintf(stderr, "short %s of %zd of %zu bytes "
			    "at block %ld\n", doread ? "read" : "write", n,
			    blocksize, i);
			return (1);
		}
		if (!doread)
			continue;

		for (j = 0; j < blocksize; j++) {
			if ((unsigned char)buf[j] != pattern) {
				(void) fprintf(stderr, "block %ld offset %zu: "
				    "expected 0x%02x, got 0x%02x\n", i, j,
				    pattern, (unsigned char)buf[j]);
				return (1);
			}
		}
	}

	if (close(fd) != 0) {
		perror("close");
		return (1);
	}
	free(buf);

	return (0);
}
