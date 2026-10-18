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
 * Copyright (c) 2026, Tigerblue77.
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/zfs_context.h>
#include <sys/zfs_file.h>

#include "unit.h"

/* ========== */

/*
 * zfs_file_off() returns the current offset of the file, and must not move
 * it, as the kernel implementations return f_pos (Linux) or f_offset
 * (FreeBSD) without touching them.  The stateful zfs_file_read() and
 * zfs_file_write() move the offset, the positional zfs_file_pread() and
 * zfs_file_pwrite() do not.  Errors are returned as errno values, as the
 * kernel implementations do.
 */

static const char testdata[] = "0123456789";

/* Open an unlinked temporary file, wrapped as a zfs_file_t. */
static zfs_file_t *
tmpfile_get(int *fdp)
{
	char path[] = "/tmp/test_zfs_file.XXXXXX";
	int fd = mkstemp(path);

	unit_ge(fd, 0);
	unit_ok(unlink(path));
	*fdp = fd;
	return (zfs_file_get(fd));
}

static void
tmpfile_put(zfs_file_t *fp, int fd)
{
	zfs_file_put(fp);
	unit_ok(close(fd));
}

/* A file holding testdata, positioned at the end of it. */
static zfs_file_t *
tmpfile_get_data(int *fdp)
{
	zfs_file_t *fp = tmpfile_get(fdp);

	unit_ok(zfs_file_write(fp, testdata, sizeof (testdata), NULL));
	return (fp);
}

/* An empty temporary directory, for the tests that open by path. */
static void
tmpdir_get(char *dir, size_t len)
{
	(void) strlcpy(dir, "/tmp/test_zfs_file.XXXXXX", len);
	unit_notnull(mkdtemp(dir));
}

static void
tmpdir_put(const char *dir)
{
	unit_ok(rmdir(dir));
}

/* A freshly opened file is at offset 0. */
static MunitResult
test_off_initial(const MunitParameter params[], void *data)
{
	(void) params, (void) data;
	int fd;
	zfs_file_t *fp = tmpfile_get(&fd);

	unit_eq(zfs_file_off(fp), 0);
	unit_eq(lseek(fd, 0, SEEK_CUR), 0);

	tmpfile_put(fp, fd);
	return (MUNIT_OK);
}

/* After a write, the offset is the end of the write, however often read. */
static MunitResult
test_off_after_write(const MunitParameter params[], void *data)
{
	(void) params, (void) data;
	int fd;
	zfs_file_t *fp = tmpfile_get(&fd);

	unit_ok(zfs_file_write(fp, testdata, sizeof (testdata), NULL));
	unit_eq(zfs_file_off(fp), sizeof (testdata));
	unit_eq(zfs_file_off(fp), sizeof (testdata));
	unit_eq(lseek(fd, 0, SEEK_CUR), sizeof (testdata));

	tmpfile_put(fp, fd);
	return (MUNIT_OK);
}

/* After a seek, the offset is where the seek went, and reads continue there. */
static MunitResult
test_off_after_seek(const MunitParameter params[], void *data)
{
	(void) params, (void) data;
	int fd;
	zfs_file_t *fp = tmpfile_get(&fd);
	loff_t off = 4;
	char buf[3];

	unit_ok(zfs_file_write(fp, testdata, sizeof (testdata), NULL));
	unit_ok(zfs_file_seek(fp, &off, SEEK_SET));
	unit_eq(off, 4);
	unit_eq(zfs_file_off(fp), 4);

	unit_ok(zfs_file_read(fp, buf, sizeof (buf), NULL));
	unit_eq(memcmp(buf, testdata + 4, sizeof (buf)), 0);
	unit_eq(zfs_file_off(fp), 4 + sizeof (buf));

	tmpfile_put(fp, fd);
	return (MUNIT_OK);
}

/* zfs_file_pwrite() writes at the given offset and leaves the file offset. */
static MunitResult
test_off_after_pwrite(const MunitParameter params[], void *data)
{
	(void) params, (void) data;
	int fd;
	zfs_file_t *fp = tmpfile_get_data(&fd);
	char buf[sizeof (testdata)];

	/* An ashift of 0 lets zfs_file_pwrite() split the write anywhere. */
	unit_ok(zfs_file_pwrite(fp, "abcd", 4, 2, 0, NULL));
	unit_eq(zfs_file_off(fp), sizeof (testdata));

	unit_eq(pread(fd, buf, sizeof (buf), 0), sizeof (buf));
	unit_eq(memcmp(buf, "01abcd6789", sizeof (buf)), 0);

	tmpfile_put(fp, fd);
	return (MUNIT_OK);
}

/* zfs_file_read() reads from the file offset and moves it past the data. */
static MunitResult
test_off_after_read(const MunitParameter params[], void *data)
{
	(void) params, (void) data;
	int fd;
	zfs_file_t *fp = tmpfile_get_data(&fd);
	loff_t off = 0;
	char buf[4];

	unit_ok(zfs_file_seek(fp, &off, SEEK_SET));
	unit_ok(zfs_file_read(fp, buf, 4, NULL));
	unit_eq(memcmp(buf, "0123", 4), 0);
	unit_eq(zfs_file_off(fp), 4);

	unit_ok(zfs_file_read(fp, buf, 3, NULL));
	unit_eq(memcmp(buf, "456", 3), 0);
	unit_eq(zfs_file_off(fp), 7);

	tmpfile_put(fp, fd);
	return (MUNIT_OK);
}

/* zfs_file_pread() reads at the given offset and leaves the file offset. */
static MunitResult
test_off_after_pread(const MunitParameter params[], void *data)
{
	(void) params, (void) data;
	int fd;
	zfs_file_t *fp = tmpfile_get_data(&fd);
	loff_t off = 2;
	char buf[3];

	unit_ok(zfs_file_seek(fp, &off, SEEK_SET));
	unit_ok(zfs_file_pread(fp, buf, sizeof (buf), 6, NULL));
	unit_eq(memcmp(buf, "678", sizeof (buf)), 0);
	unit_eq(zfs_file_off(fp), 2);

	tmpfile_put(fp, fd);
	return (MUNIT_OK);
}

/*
 * A read that reaches the end of the file reports what it could not read
 * in resid, or fails with EIO without one.
 */
static MunitResult
test_read_short(const MunitParameter params[], void *data)
{
	(void) params, (void) data;
	int fd;
	zfs_file_t *fp = tmpfile_get_data(&fd);
	loff_t off = 8;
	ssize_t resid;
	char buf[8];

	unit_ok(zfs_file_seek(fp, &off, SEEK_SET));
	unit_ok(zfs_file_read(fp, buf, sizeof (buf), &resid));
	unit_eq(resid, sizeof (buf) - (sizeof (testdata) - 8));

	off = 8;
	unit_ok(zfs_file_seek(fp, &off, SEEK_SET));
	unit_err(zfs_file_read(fp, buf, sizeof (buf), NULL), EIO);

	unit_ok(zfs_file_pread(fp, buf, sizeof (buf), 8, &resid));
	unit_eq(resid, sizeof (buf) - (sizeof (testdata) - 8));
	unit_err(zfs_file_pread(fp, buf, sizeof (buf), 8, NULL), EIO);

	tmpfile_put(fp, fd);
	return (MUNIT_OK);
}

/* zfs_file_seek() returns the new offset, and errno values on failure. */
static MunitResult
test_seek(const MunitParameter params[], void *data)
{
	(void) params, (void) data;
	int fd;
	zfs_file_t *fp = tmpfile_get_data(&fd);
	loff_t off = 0;
	int pfd[2];

	unit_ok(zfs_file_seek(fp, &off, SEEK_END));
	unit_eq(off, sizeof (testdata));

	off = -1;
	unit_err(zfs_file_seek(fp, &off, SEEK_SET), EINVAL);
	unit_eq(zfs_file_off(fp), sizeof (testdata));

	unit_ok(pipe(pfd));
	zfs_file_t *pfp = zfs_file_get(pfd[0]);
	off = 0;
	unit_err(zfs_file_seek(pfp, &off, SEEK_SET), ESPIPE);
	zfs_file_put(pfp);
	unit_ok(close(pfd[0]));
	unit_ok(close(pfd[1]));

	tmpfile_put(fp, fd);
	return (MUNIT_OK);
}

/*
 * zfs_file_open() creates a file with exactly the mode given, and
 * zfs_file_getattr() reports its mode and size.
 */
static MunitResult
test_open_create(const MunitParameter params[], void *data)
{
	(void) params, (void) data;
	char dir[64], path[80];
	zfs_file_t *fp;
	zfs_file_attr_t za;
	char buf[sizeof (testdata)];

	tmpdir_get(dir, sizeof (dir));
	(void) snprintf(path, sizeof (path), "%s/file", dir);

	unit_ok(zfs_file_open(path, O_RDWR | O_CREAT | O_EXCL, 0640, kcred,
	    &fp));
	unit_ok(zfs_file_getattr(fp, &za));
	unit_true(S_ISREG(za.zfa_mode));
	unit_eq(za.zfa_mode & 07777, 0640);
	unit_eq(za.zfa_size, 0);

	unit_ok(zfs_file_write(fp, testdata, sizeof (testdata), NULL));
	unit_ok(zfs_file_fsync(fp, O_SYNC));
	unit_ok(zfs_file_getattr(fp, &za));
	unit_eq(za.zfa_size, sizeof (testdata));
	zfs_file_close(fp);

	unit_err(zfs_file_open(path, O_RDWR | O_CREAT | O_EXCL, 0640, kcred,
	    &fp), EEXIST);

	unit_ok(zfs_file_open(path, O_RDONLY, 0, kcred, &fp));
	unit_ok(zfs_file_read(fp, buf, sizeof (buf), NULL));
	unit_eq(memcmp(buf, testdata, sizeof (buf)), 0);
	zfs_file_close(fp);

	unit_ok(unlink(path));
	tmpdir_put(dir);
	return (MUNIT_OK);
}

/*
 * zfs_file_deallocate() punches a hole that reads back as zeros and keeps
 * the file size, and returns errno values on failure.
 */
static MunitResult
test_deallocate(const MunitParameter params[], void *data)
{
	(void) params, (void) data;
	char dir[64], path[80];
	zfs_file_t *fp;
	zfs_file_attr_t za;
	static char buf[3 * 4096], zeros[4096];
	int err;

	tmpdir_get(dir, sizeof (dir));
	(void) snprintf(path, sizeof (path), "%s/file", dir);
	memset(buf, 0xaa, sizeof (buf));

	unit_ok(zfs_file_open(path, O_RDWR | O_CREAT, 0644, kcred, &fp));
	unit_ok(zfs_file_pwrite(fp, buf, sizeof (buf), 0, 12, NULL));
	err = zfs_file_deallocate(fp, 4096, 4096);
	if (err == EOPNOTSUPP) {
		zfs_file_close(fp);
		unit_ok(unlink(path));
		tmpdir_put(dir);
		return (MUNIT_SKIP);
	}
	unit_ok(err);

	unit_ok(zfs_file_getattr(fp, &za));
	unit_eq(za.zfa_size, sizeof (buf));
	unit_ok(zfs_file_pread(fp, buf, sizeof (buf), 0, NULL));
	unit_eq(memcmp(buf + 4096, zeros, 4096), 0);
	unit_eq((uint8_t)buf[4095], 0xaa);
	unit_eq((uint8_t)buf[2 * 4096], 0xaa);
	zfs_file_close(fp);

	/* Deallocation needs a file open for writing. */
	unit_ok(zfs_file_open(path, O_RDONLY, 0, kcred, &fp));
	unit_err(zfs_file_deallocate(fp, 0, 4096), EBADF);
	zfs_file_close(fp);

	unit_ok(unlink(path));
	tmpdir_put(dir);
	return (MUNIT_OK);
}

/* zfs_file_unlink() removes a file, and fails with ENOENT once it is gone. */
static MunitResult
test_unlink(const MunitParameter params[], void *data)
{
	(void) params, (void) data;
	char dir[64], path[80];
	zfs_file_t *fp;
	struct stat st;

	tmpdir_get(dir, sizeof (dir));
	(void) snprintf(path, sizeof (path), "%s/file", dir);

	unit_ok(zfs_file_open(path, O_RDWR | O_CREAT, 0644, kcred, &fp));
	zfs_file_close(fp);

	unit_ok(zfs_file_unlink(path));
	unit_eq(stat(path, &st), -1);
	unit_eq(errno, ENOENT);
	unit_err(zfs_file_unlink(path), ENOENT);

	tmpdir_put(dir);
	return (MUNIT_OK);
}

/* ========== */

static const MunitTest zfs_file_tests[] = {
	UNIT_TEST("off_initial",	test_off_initial),
	UNIT_TEST("off_after_write",	test_off_after_write),
	UNIT_TEST("off_after_seek",	test_off_after_seek),
	UNIT_TEST("off_after_pwrite",	test_off_after_pwrite),
	UNIT_TEST("off_after_read",	test_off_after_read),
	UNIT_TEST("off_after_pread",	test_off_after_pread),
	UNIT_TEST("read_short",		test_read_short),
	UNIT_TEST("seek",		test_seek),
	UNIT_TEST("open_create",	test_open_create),
	UNIT_TEST("deallocate",		test_deallocate),
	UNIT_TEST("unlink",		test_unlink),
	{ 0 },
};

static const MunitSuite zfs_file_test_suite = {
	"zfs_file.",
	zfs_file_tests,
	NULL,
	1,
	MUNIT_SUITE_OPTION_NONE,
};

int
main(int argc, char **argv)
{
	return (munit_suite_main(&zfs_file_test_suite, NULL, argc, argv));
}
