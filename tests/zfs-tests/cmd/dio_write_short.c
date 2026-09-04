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
 * Copyright 2026, tiehexue <tiehexue@hotmail.com>. All rights reserved.
 */

/*
 * Verify that a Direct I/O write which makes progress and then hits a
 * quota/space error is reported as a positive short write whose prefix
 * matches the buffer, and that the file ends at exactly the reported count.
 *
 * zfs_write_impl() writes the request in per-iteration transactions.  For a
 * request to a new file the first record is served through the ARC so the
 * file blocksize can grow, and the rest of the request is a separate
 * transaction; when the second transaction fails (e.g. EDQUOT because the
 * dataset quota was crossed) the first record has already been committed.
 * The write must then be reported as a successful short write with the
 * count of the committed bytes, never as an error that hides how much data
 * landed.
 *
 * usage: dio_write_short <file> <sync|async> <bytes>
 *   sync  - one blocking write(2) with O_DIRECT
 *   async - one IOCB_CMD_PWRITE submitted with libaio (queued to the ZFS
 *           async Direct I/O taskq when it is enabled)
 * exit: 0 = short write reported and prefix verified
 *       1 = wrong result (full write, error, or bad prefix)
 *       2 = usage or setup error
 */

#include <errno.h>
#include <fcntl.h>
#include <libaio.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>

/* Wait this long for an async completion before failing. */
#define	WAIT_SECONDS	60

static unsigned char
pattern(size_t off)
{
	return ((unsigned char)((off * 31) + (off >> 8) * 7 + 1));
}

/*
 * Verify that the file contains exactly 'written' bytes, all matching the
 * pattern prefix.
 */
static int
verify_prefix(const char *path, size_t written)
{
	struct stat st;
	unsigned char *rbuf;
	int fd;
	ssize_t got;

	if (posix_memalign((void **)&rbuf, (size_t)getpagesize(),
	    written) != 0) {
		perror("posix_memalign");
		return (1);
	}

	fd = open(path, O_RDONLY | O_DIRECT);
	if (fd < 0) {
		perror("open for read");
		free(rbuf);
		return (1);
	}
	if (fstat(fd, &st) != 0) {
		perror("fstat");
		(void) close(fd);
		free(rbuf);
		return (1);
	}
	if ((uint64_t)st.st_size != (uint64_t)written) {
		(void) fprintf(stderr,
		    "file size %llu does not match reported write %zu\n",
		    (unsigned long long)st.st_size, written);
		(void) close(fd);
		free(rbuf);
		return (1);
	}
	got = pread(fd, rbuf, written, 0);
	(void) close(fd);
	if (got < 0) {
		perror("pread");
		free(rbuf);
		return (1);
	}
	if ((size_t)got != written) {
		(void) fprintf(stderr,
		    "read back %zd of %zu reported bytes\n", got, written);
		free(rbuf);
		return (1);
	}
	for (size_t i = 0; i < written; i++) {
		if (rbuf[i] != pattern(i)) {
			(void) fprintf(stderr,
			    "byte %zu is 0x%02x, expected 0x%02x\n", i,
			    rbuf[i], pattern(i));
			free(rbuf);
			return (1);
		}
	}
	free(rbuf);

	return (0);
}

int
main(int argc, char **argv)
{
	io_context_t ctx = 0;
	struct timespec ts = { WAIT_SECONDS, 0 };
	struct io_event ev;
	struct iocb cb;
	struct iocb *cbp = &cb;
	const char *path, *mode;
	unsigned char *buf;
	size_t len, pagesize;
	long written;
	int fd, ret, got;

	if (argc != 4) {
		(void) fprintf(stderr,
		    "usage: %s <file> <sync|async> <bytes>\n", argv[0]);
		return (2);
	}

	path = argv[1];
	mode = argv[2];
	len = (size_t)strtoul(argv[3], NULL, 0);
	if (strcmp(mode, "sync") != 0 && strcmp(mode, "async") != 0) {
		(void) fprintf(stderr, "unknown mode: %s\n", mode);
		return (2);
	}
	if (len == 0 || len % (size_t)getpagesize() != 0) {
		(void) fprintf(stderr, "bytes must be a non-zero page "
		    "multiple\n");
		return (2);
	}

	pagesize = (size_t)getpagesize();
	if (posix_memalign((void **)&buf, pagesize, len) != 0) {
		perror("posix_memalign");
		return (2);
	}
	for (size_t i = 0; i < len; i++)
		buf[i] = pattern(i);

	fd = open(path, O_RDWR | O_CREAT | O_TRUNC | O_DIRECT, 0644);
	if (fd < 0) {
		perror("open");
		return (2);
	}

	if (strcmp(mode, "sync") == 0) {
		written = write(fd, buf, len);
		if (written < 0) {
			(void) fprintf(stderr,
			    "write failed with %s instead of a positive "
			    "short count\n", strerror(errno));
			(void) close(fd);
			free(buf);
			return (1);
		}
	} else {
		/* io_setup() returns a negative errno rather than errno. */
		ret = io_setup(1, &ctx);
		if (ret != 0) {
			(void) fprintf(stderr, "io_setup: %s\n",
			    strerror(-ret));
			(void) close(fd);
			free(buf);
			return (2);
		}

		io_prep_pwrite(&cb, fd, buf, len, 0);
		got = io_submit(ctx, 1, &cbp);
		if (got != 1) {
			(void) fprintf(stderr, "io_submit: %s\n",
			    strerror(got < 0 ? -got : EIO));
			(void) io_destroy(ctx);
			(void) close(fd);
			free(buf);
			return (2);
		}
		got = io_getevents(ctx, 1, 1, &ev, &ts);
		if (got != 1) {
			(void) fprintf(stderr,
			    "no async completion within %d seconds\n",
			    WAIT_SECONDS);
			(void) io_destroy(ctx);
			(void) close(fd);
			free(buf);
			return (1);
		}
		written = (long)ev.res;
		if (written < 0) {
			(void) fprintf(stderr,
			    "async write failed with %s instead of a "
			    "positive short count\n",
			    strerror((int)-written));
			(void) io_destroy(ctx);
			(void) close(fd);
			free(buf);
			return (1);
		}
		(void) io_destroy(ctx);
	}

	(void) close(fd);

	if ((size_t)written >= len) {
		(void) fprintf(stderr,
		    "write reported %ld of %zu bytes; expected a positive "
		    "short write\n", written, len);
		free(buf);
		return (1);
	}

	(void) printf("short write: %ld of %zu bytes\n", written, len);
	ret = verify_prefix(path, (size_t)written);
	free(buf);
	return (ret);
}
