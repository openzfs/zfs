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
#include <fcntl.h>
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
 * (FreeBSD) without touching them.
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

/* ========== */

static const MunitTest zfs_file_tests[] = {
	UNIT_TEST("off_initial",	test_off_initial),
	UNIT_TEST("off_after_write",	test_off_after_write),
	UNIT_TEST("off_after_seek",	test_off_after_seek),
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
