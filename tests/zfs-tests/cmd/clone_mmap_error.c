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
 * usage: clone_mmap_error [-w] <src> <dst> <inject> <clear> <sync>
 *
 * dst must use 1 KiB records, and src and dst must hold at least a page.
 * Map dst, read its first page through the mapping, and with -w store to
 * it.  Run the inject command, which should make reads of dst fail, then
 * clone the first record of src over dst, so that the refresh of the
 * mapped page after the clone fails.  Run the clear command, write into
 * another record of the page, and run the sync command.  Then check that
 * the mapping, read(), and the file once the page is evicted all hold
 * the cloned record, the write, and with -w the store.
 *
 * Exit 0 if so, 1 if any of them shows other data, 2 on error.
 */

#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <linux/fs.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define	REC_SIZE	1024
#define	WRITE_OFF	2048
#define	WRITE_LEN	10
#define	STORE_OFF	3000

static int store;

static char
expected(long i)
{
	if (i < REC_SIZE)
		return ('S');
	if (i >= WRITE_OFF && i < WRITE_OFF + WRITE_LEN)
		return ('W');
	if (store && i == STORE_OFF)
		return ('M');
	return ('D');
}

static int
check(const char *what, const volatile char *p, long len)
{
	for (long i = 0; i < len; i++) {
		if (p[i] != expected(i)) {
			(void) printf("%s: byte %ld is %c, expected %c\n",
			    what, i, p[i], expected(i));
			return (1);
		}
	}
	return (0);
}

static void
run(const char *cmd)
{
	int status = system(cmd);

	if (status == -1 || !WIFEXITED(status) || WEXITSTATUS(status) != 0) {
		(void) fprintf(stderr, "command failed: %s\n", cmd);
		exit(2);
	}
}

static void
usage(void)
{
	(void) fprintf(stderr, "usage: clone_mmap_error [-w] <src> <dst> "
	    "<inject> <clear> <sync>\n");
	exit(2);
}

static void
fail(const char *what)
{
	(void) fprintf(stderr, "%s failed: %s\n", what, strerror(errno));
	exit(2);
}

int
main(int argc, char *argv[])
{
	long pgsize = sysconf(_SC_PAGESIZE);
	struct stat sb;
	char *buf;
	int c;

	while ((c = getopt(argc, argv, "w")) != -1) {
		if (c != 'w')
			usage();
		store = 1;
	}
	argc -= optind;
	argv += optind;
	if (argc != 5)
		usage();

	int sfd = open(argv[0], O_RDONLY);
	if (sfd < 0)
		fail("open src");
	int dfd = open(argv[1], O_RDWR);
	if (dfd < 0)
		fail("open dst");
	if (fstat(dfd, &sb) != 0)
		fail("fstat");
	if (pgsize < STORE_OFF + 1 || sb.st_size < pgsize) {
		(void) fprintf(stderr, "dst must hold at least a page\n");
		return (2);
	}
	if ((buf = malloc(pgsize)) == NULL)
		fail("malloc");

	volatile char *m = mmap(NULL, sb.st_size,
	    PROT_READ | (store ? PROT_WRITE : 0), MAP_SHARED, dfd, 0);
	if (m == MAP_FAILED)
		fail("mmap");
	if (m[0] != 'D') {
		(void) printf("dst starts with %c, expected D\n", m[0]);
		return (2);
	}
	if (store)
		m[STORE_OFF] = 'M';

	run(argv[2]);
	struct file_clone_range fcr = {
		.src_fd = sfd,
		.src_offset = 0,
		.src_length = REC_SIZE,
		.dest_offset = 0,
	};
	int err = ioctl(dfd, FICLONERANGE, &fcr);
	int clone_errno = errno;
	run(argv[3]);
	if (err != 0) {
		errno = clone_errno;
		fail("FICLONERANGE");
	}

	memset(buf, 'W', WRITE_LEN);
	if (pwrite(dfd, buf, WRITE_LEN, WRITE_OFF) != WRITE_LEN)
		fail("pwrite");
	run(argv[4]);

	int bad = check("mapping", m, pgsize);
	if (pread(dfd, buf, pgsize, 0) != pgsize)
		fail("pread");
	bad |= check("read()", buf, pgsize);
	if (store && msync((void *)m, sb.st_size, MS_SYNC) != 0)
		fail("msync");
	if (munmap((void *)m, sb.st_size) != 0)
		fail("munmap");
	if (fsync(dfd) != 0)
		fail("fsync");
	(void) posix_fadvise(dfd, 0, 0, POSIX_FADV_DONTNEED);
	if (pread(dfd, buf, pgsize, 0) != pgsize)
		fail("pread");
	bad |= check("file", buf, pgsize);

	free(buf);
	(void) close(dfd);
	(void) close(sfd);
	return (bad);
}
