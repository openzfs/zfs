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
 * Store data past the end of a file through a shared mapping of its last
 * page, extend the file, and check that the bytes past the old end of file
 * read back as zeros.  POSIX requires that modifications of the partial
 * page beyond the end of an object are never written out (openzfs #19220).
 *
 * usage: mmap_eof_extend [-s] truncate|write|fallocate|clone <file>
 *
 *   truncate	extend the file with ftruncate()
 *   write	extend the file with a pwrite() past the end of the file
 *   fallocate	extend the file with fallocate()
 *   clone	extend the file by cloning <file>.src (one 4k block, which
 *		must exist and be on disk) past the end of the file
 *		(FICLONERANGE); the file system must use 4k records
 *
 * With -s the page is written back with msync() before the file is
 * extended, otherwise it is still dirty.
 *
 * The file is written up to 10 bytes into its last page: it is 10 bytes
 * long, or a page and 10 bytes for clone.  The file is fsync()ed before
 * exiting, so the caller can check its contents on disk too.  Exits 0 if
 * the bytes past the old end of file read back as zeros, 1 if they don't,
 * and 2 on errors.
 */

#ifndef _GNU_SOURCE
#define	_GNU_SOURCE
#endif
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <linux/fs.h>

#define	STALE		"STALE-DATA"
#define	STALE_LEN	(sizeof (STALE) - 1)
#define	CLONE_BLKSZ	4096

static void
fail(const char *what)
{
	perror(what);
	exit(2);
}

static void
usage(void)
{
	(void) fprintf(stderr, "usage: mmap_eof_extend [-s] "
	    "truncate|write|fallocate|clone <file>\n");
	exit(2);
}

int
main(int argc, char *argv[])
{
	long psz = sysconf(_SC_PAGESIZE);
	int sync = 0;
	int c;

	while ((c = getopt(argc, argv, "s")) != -1) {
		if (c != 's')
			usage();
		sync = 1;
	}
	if (argc - optind != 2)
		usage();
	const char *mode = argv[optind];
	const char *path = argv[optind + 1];
	int clone = (strcmp(mode, "clone") == 0);

	/*
	 * Cloning needs a file of several blocks, so start one page further
	 * in for it.
	 */
	off_t pgoff = clone ? psz : 0;
	off_t old_size = pgoff + 10;
	off_t new_size;

	int fd = open(path, O_RDWR | O_CREAT | O_TRUNC, 0644);
	if (fd < 0)
		fail("open");
	char *buf = malloc(old_size);
	if (buf == NULL)
		fail("malloc");
	memset(buf, 'D', old_size);
	if (pwrite(fd, buf, old_size, 0) != old_size)
		fail("pwrite");
	free(buf);

	char *p = mmap(NULL, psz, PROT_READ | PROT_WRITE, MAP_SHARED, fd,
	    pgoff);
	if (p == MAP_FAILED)
		fail("mmap");
	memcpy(p + (old_size - pgoff), STALE, STALE_LEN);
	if (sync && msync(p, psz, MS_SYNC) != 0)
		fail("msync");
	if (munmap(p, psz) != 0)
		fail("munmap");

	if (strcmp(mode, "truncate") == 0) {
		new_size = old_size + 1024;
		if (ftruncate(fd, new_size) != 0)
			fail("ftruncate");
	} else if (strcmp(mode, "write") == 0) {
		new_size = old_size + 1024;
		if (pwrite(fd, "X", 1, new_size - 1) != 1)
			fail("pwrite");
	} else if (strcmp(mode, "fallocate") == 0) {
		new_size = old_size + 1024;
		if (fallocate(fd, 0, 0, new_size) != 0)
			fail("fallocate");
	} else if (clone) {
		char src[4096];
		struct file_clone_range fcr;

		(void) snprintf(src, sizeof (src), "%s.src", path);
		int sfd = open(src, O_RDONLY);
		if (sfd < 0)
			fail(src);
		fcr.src_fd = sfd;
		fcr.src_offset = 0;
		fcr.src_length = CLONE_BLKSZ;
		fcr.dest_offset = 2 * psz;
		if (ioctl(fd, FICLONERANGE, &fcr) != 0)
			fail("FICLONERANGE");
		(void) close(sfd);
		new_size = fcr.dest_offset + CLONE_BLKSZ;
	} else {
		usage();
	}

	char rbuf[STALE_LEN];
	if (pread(fd, rbuf, STALE_LEN, old_size) != (ssize_t)STALE_LEN)
		fail("pread");
	if (fsync(fd) != 0)
		fail("fsync");
	(void) close(fd);

	int stale = 0;
	(void) printf("%s%s: bytes %lld-%lld after extending to %lld:",
	    mode, sync ? " -s" : "", (long long)old_size,
	    (long long)(old_size + STALE_LEN - 1), (long long)new_size);
	for (size_t i = 0; i < STALE_LEN; i++) {
		(void) printf(" %02x", (unsigned char)rbuf[i]);
		if (rbuf[i] != 0)
			stale = 1;
	}
	(void) printf("\n");

	return (stale);
}
