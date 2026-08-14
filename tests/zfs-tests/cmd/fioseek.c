// SPDX-License-Identifier: CDDL-1.0
/*
 * NetBSD FIOSEEKDATA / FIOSEEKHOLE helper for ZTS.
 *
 * lseek(SEEK_HOLE/SEEK_DATA) is EINVAL on NetBSD 11 -- hole find is
 * ioctl-only, matching in-tree ZFS.  This binary is the userland
 * stand-in for that ioctl.
 */

#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/filio.h>
#include <sys/unistd.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int fail;

static off_t
do_seek(int fd, unsigned long cmd, off_t off, int *errp)
{
	int e;

	errno = 0;
	if (ioctl(fd, cmd, &off) == -1) {
		e = errno;
		if (errp != NULL)
			*errp = e;
		return ((off_t)-1);
	}
	if (errp != NULL)
		*errp = 0;
	return (off);
}

static void
expect_off(int fd, unsigned long cmd, off_t from, off_t want, const char *tag)
{
	int e = 0;
	off_t got;

	got = do_seek(fd, cmd, from, &e);
	if (got == want && e == 0)
		return;
	fprintf(stderr,
	    "FAIL %s: ioctl(%s, %lld) = %lld errno=%d (want %lld)\n",
	    tag, cmd == FIOSEEKHOLE ? "HOLE" : "DATA",
	    (long long)from, (long long)got, e, (long long)want);
	fail = 1;
}

static void
expect_err(int fd, unsigned long cmd, off_t from, int want, const char *tag)
{
	int e = 0;
	off_t got;

	got = do_seek(fd, cmd, from, &e);
	if (got == (off_t)-1 && e == want)
		return;
	fprintf(stderr,
	    "FAIL %s: ioctl(%s, %lld) = %lld errno=%d (want errno %d)\n",
	    tag, cmd == FIOSEEKHOLE ? "HOLE" : "DATA",
	    (long long)from, (long long)got, e, want);
	fail = 1;
}

int
main(int argc, char **argv)
{
	const char *path;
	char *buf;
	off_t bs, filesz;
	int fd;
	long minhole;

	if (argc != 3) {
		fprintf(stderr, "usage: %s <file> <block_size>\n", argv[0]);
		return (1);
	}
	path = argv[1];
	bs = (off_t)strtoll(argv[2], NULL, 0);
	if (bs < 512) {
		fprintf(stderr, "block_size must be >= 512\n");
		return (1);
	}
	filesz = bs * 3;

	fd = open(path, O_RDWR | O_CREAT | O_TRUNC, 0644);
	if (fd < 0)
		err(1, "open %s", path);

	buf = malloc((size_t)bs);
	if (buf == NULL)
		err(1, "malloc");
	memset(buf, 0x5a, (size_t)bs);

	/* [data][hole][data] */
	if (write(fd, buf, (size_t)bs) != bs)
		err(1, "write data0");
	if (lseek(fd, bs * 2, SEEK_SET) == (off_t)-1)
		err(1, "lseek hole");
	if (write(fd, buf, (size_t)bs) != bs)
		err(1, "write data1");
	if (fsync(fd) != 0)
		err(1, "fsync");

#ifdef _PC_MIN_HOLE_SIZE
	minhole = fpathconf(fd, _PC_MIN_HOLE_SIZE);
	if (minhole < 1) {
		fprintf(stderr, "FAIL PC_MIN_HOLE_SIZE=%ld\n", minhole);
		fail = 1;
	}
#else
	(void) minhole;
#endif

	expect_off(fd, FIOSEEKDATA, 0, 0, "data@0");
	expect_off(fd, FIOSEEKHOLE, 0, bs, "hole@0");
	expect_off(fd, FIOSEEKDATA, bs, bs * 2, "data@hole");
	expect_off(fd, FIOSEEKHOLE, bs * 2, filesz, "hole@eof");
	expect_err(fd, FIOSEEKDATA, filesz, ENXIO, "data@eof");
	expect_err(fd, FIOSEEKHOLE, filesz, ENXIO, "hole@eof");

	/* Fully sparse: no data, hole at 0. */
	if (ftruncate(fd, 0) != 0)
		err(1, "ftruncate 0");
	if (ftruncate(fd, filesz) != 0)
		err(1, "ftruncate sparse");
	if (fsync(fd) != 0)
		err(1, "fsync sparse");
	expect_err(fd, FIOSEEKDATA, 0, ENXIO, "sparse-data");
	expect_off(fd, FIOSEEKHOLE, 0, 0, "sparse-hole");

	/* Fully dense: hole is the virtual hole at EOF. */
	if (lseek(fd, 0, SEEK_SET) == (off_t)-1)
		err(1, "lseek 0");
	if (write(fd, buf, (size_t)bs) != bs ||
	    write(fd, buf, (size_t)bs) != bs ||
	    write(fd, buf, (size_t)bs) != bs)
		err(1, "write dense");
	if (fsync(fd) != 0)
		err(1, "fsync dense");
	expect_off(fd, FIOSEEKDATA, 0, 0, "dense-data");
	expect_off(fd, FIOSEEKHOLE, 0, filesz, "dense-hole");

	free(buf);
	close(fd);
	return (fail ? 2 : 0);
}
