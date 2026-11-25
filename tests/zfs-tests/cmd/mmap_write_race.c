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
 * Write small records to a file with write() while another thread reads
 * each one back through a shared mapping as soon as its write returns,
 * and check that the mapping never shows anything but the records written.
 * Page faults racing with the writes used to leave pages in the page cache
 * with zeros in place of data already written (openzfs #14548).
 *
 * usage: mmap_write_race [-a | -A] <file> <size_mb> <seconds>
 *
 * The file is recreated and filled with records up to size_mb megabytes,
 * over and over until the given number of seconds have passed.  It is
 * first extended to its full size with ftruncate(), or with -a it grows
 * with each write, as when a file is appended to.  -A grows it the same
 * way through a descriptor opened with O_APPEND.  Exits 0 if every record
 * read back correctly, 1 if one did not, and 2 on errors.
 */

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

static const char record[] = "mmap write race test\n";
#define	RECSZ	(sizeof (record))

typedef struct {
	char *base;
	atomic_long written;	/* records written; -n-1 when n are done */
	atomic_long bad;	/* first bad record, or -1 */
} race_t;

static __attribute__((noreturn)) void
fail(const char *what)
{
	perror(what);
	exit(2);
}

static __attribute__((noreturn)) void
usage(void)
{
	(void) fprintf(stderr,
	    "usage: mmap_write_race [-a | -A] <file> <size_mb> <seconds>\n");
	exit(2);
}

static void *
reader(void *arg)
{
	race_t *r = arg;
	long i = 0;

	for (;;) {
		long n = atomic_load(&r->written);
		int done = (n < 0);

		if (done)
			n = -n - 1;
		for (; i < n; i++) {
			if (memcmp(r->base + i * RECSZ, record, RECSZ) != 0) {
				atomic_store(&r->bad, i);
				return (NULL);
			}
		}
		if (done)
			return (NULL);
	}
}

int
main(int argc, char *argv[])
{
	int append = 0;
	int oflags = O_RDWR | O_CREAT | O_TRUNC;
	int c;

	while ((c = getopt(argc, argv, "aA")) != -1) {
		if (c == 'A')
			oflags |= O_APPEND;
		else if (c != 'a')
			usage();
		append = 1;
	}
	if (argc - optind != 3)
		usage();
	const char *path = argv[optind];
	size_t size = strtoul(argv[optind + 1], NULL, 10) << 20;
	long seconds = strtol(argv[optind + 2], NULL, 10);
	long nrec = size / RECSZ;
	if (nrec == 0 || seconds <= 0)
		usage();

	time_t end = time(NULL) + seconds;
	int iters = 0;
	do {
		race_t r;
		pthread_t thr;

		int fd = open(path, oflags, 0644);
		if (fd < 0)
			fail("open");
		if (!append && ftruncate(fd, size) != 0)
			fail("ftruncate");
		r.base = mmap(NULL, size, PROT_READ, MAP_SHARED, fd, 0);
		if (r.base == MAP_FAILED)
			fail("mmap");
		atomic_store(&r.written, 0);
		atomic_store(&r.bad, -1);
		if ((errno = pthread_create(&thr, NULL, reader, &r)) != 0)
			fail("pthread_create");

		long w;
		for (w = 0; w < nrec && atomic_load(&r.bad) < 0; w++) {
			if (write(fd, record, RECSZ) != (ssize_t)RECSZ)
				fail("write");
			atomic_store(&r.written, w + 1);
		}
		/* Let the reader check every record written, then stop. */
		atomic_store(&r.written, -w - 1);
		if ((errno = pthread_join(thr, NULL)) != 0)
			fail("pthread_join");

		long bad = atomic_load(&r.bad);
		if (bad >= 0) {
			off_t off = bad * RECSZ;
			char buf[RECSZ];

			(void) printf("iteration %d: record %ld at offset %lld "
			    "reads back wrong through the mapping:", iters,
			    bad, (long long)off);
			for (size_t i = 0; i < RECSZ; i++)
				(void) printf(" %02x",
				    (unsigned char)r.base[off + i]);
			if (pread(fd, buf, RECSZ, off) != (ssize_t)RECSZ)
				fail("pread");
			(void) printf("\nread() returns:");
			for (size_t i = 0; i < RECSZ; i++)
				(void) printf(" %02x", (unsigned char)buf[i]);
			(void) printf("\n");
			return (1);
		}
		if (munmap(r.base, size) != 0)
			fail("munmap");
		(void) close(fd);
		iters++;
	} while (time(NULL) < end);

	(void) printf("%d iterations of %ld records read back correctly\n",
	    iters, nrec);
	return (0);
}
