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
 * Check that a sub-page write racing POSIX_FADV_WILLNEED readahead does not
 * leave the rest of the written page stale in the page cache.
 *
 * usage: mmap_fadvise_readahead_subpage <file> <recordsize> <ready> <go>
 *            <cleared> <stale> <orig>
 *
 * The test keeps the file's data out of the ARC and delays reads, so the
 * writer, which writes WLEN bytes at WOFF inside the first page of record 1,
 * holds the range lock while it waits for that record.  If readahead gives
 * up on that page meanwhile, the page stays cached, not up to date and
 * unlocked.  update_pages() must not then copy in only the bytes written
 * and mark the whole page up to date.
 *
 * 1. Read the expected first page of record 1 from <orig>, a copy of the
 *    file outside the pool, so that ZFS never reads record 1 before the
 *    race, and patch in the bytes to be written.
 * 2. Fault in the first page of record 2 through a shared mapping.  No
 *    page of record 1 may be up to date in the page cache.  mincore()
 *    counts only up to date pages, so it cannot tell a page that is not
 *    cached from one that is cached but not up to date.
 * 3. Create <ready> and wait for <go>, while the test injects a read delay.
 * 4. Read <stale> (a file on another file system filled with 0xA5) and
 *    drop it from the page cache, so freed pages hold a non-zero pattern
 *    (unless init_on_alloc zeroes new pages).
 * 5. Fork the sub-page writer, a second later fadvise(WILLNEED) over
 *    record 1 and the cached page.
 * 6. Wait for the writer, then for <cleared> (the test cleared the delay).
 * 7. Compare the first page of record 1, through the mapping and through
 *    pread(), with the expected bytes, and print every differing range.
 *
 * Exit 0 if both match, 4 if either differs, 3 if the writer had already
 * finished before fadvise (the delay did not hold it) and 2 on any other
 * error.  Every wait here is bounded.
 */

#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define	GO_TIMEOUT_MS		30000
#define	CLEARED_TIMEOUT_MS	60000
#define	WRITER_SETTLE_MS	1000
#define	WRITER_TIMEOUT_MS	90000
#define	POLL_MS			10
#define	WOFF			1000
#define	WLEN			512
#define	WBYTE			0x5a

static uint64_t
now_ms(void)
{
	struct timespec ts;

	(void) clock_gettime(CLOCK_MONOTONIC, &ts);
	return ((uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

static void
sleep_ms(long ms)
{
	struct timespec ts = { ms / 1000, (ms % 1000) * 1000000 };

	while (nanosleep(&ts, &ts) != 0 && errno == EINTR)
		;
}

static int
writer_running(pid_t pid, int *status)
{
	pid_t r = waitpid(pid, status, WNOHANG);

	if (r == 0)
		return (1);
	return (r == pid ? 0 : -1);
}

static int
fail(const char *what)
{
	(void) fprintf(stderr, "mmap_fadvise_readahead_subpage: %s: %s\n",
	    what, strerror(errno));
	return (2);
}

static int
wait_file(const char *path, int timeout_ms)
{
	uint64_t start = now_ms();

	while (access(path, F_OK) != 0) {
		if (now_ms() - start > (uint64_t)timeout_ms)
			return (-1);
		sleep_ms(POLL_MS);
	}
	return (0);
}

/* Read <stale> through the page cache, then drop it. */
static int
spray_stale(const char *stale)
{
	int fd = open(stale, O_RDONLY);
	if (fd < 0)
		return (-1);
	char *b = malloc(1 << 20);
	if (b == NULL) {
		(void) close(fd);
		return (-1);
	}
	while (read(fd, b, 1 << 20) > 0)
		;
	free(b);
	(void) posix_fadvise(fd, 0, 0, POSIX_FADV_DONTNEED);
	(void) close(fd);
	return (0);
}

/* Print differing ranges; return the number of differing bytes. */
static long
compare(const char *how, const unsigned char *got, const unsigned char *exp,
    long len)
{
	long ndiff = 0;

	for (long i = 0; i < len; ) {
		if (got[i] == exp[i]) {
			i++;
			continue;
		}
		long j = i;
		while (j < len && got[j] != exp[j])
			j++;
		(void) printf("  %s: differs at page offsets [%ld, %ld) "
		    "(%ld bytes), %s the written [%d, %d); got %02x %02x .. "
		    "expected %02x %02x ..\n", how, i, j, j - i,
		    (j <= WOFF || i >= WOFF + WLEN) ? "outside" : "overlapping",
		    WOFF, WOFF + WLEN, got[i], i + 1 < j ? got[i + 1] : 0,
		    exp[i], i + 1 < j ? exp[i + 1] : 0);
		ndiff += j - i;
		i = j;
	}
	(void) printf("%s: %ld of %ld bytes differ\n", how, ndiff, len);
	return (ndiff);
}

int
main(int argc, char **argv)
{
	if (argc != 8) {
		(void) fprintf(stderr, "usage: mmap_fadvise_readahead_subpage "
		    "<file> <recordsize> <ready> <go> <cleared> <stale> "
		    "<orig>\n");
		return (2);
	}

	const char *file = argv[1];
	long rsz = strtol(argv[2], NULL, 0);
	const char *ready = argv[3];
	const char *go = argv[4];
	const char *cleared = argv[5];
	const char *stale = argv[6];
	const char *orig = argv[7];
	long pg = sysconf(_SC_PAGESIZE);

	if (pg <= 0 || rsz < pg || rsz % pg != 0 || WOFF + WLEN > pg) {
		(void) fprintf(stderr, "mmap_fadvise_readahead_subpage: bad "
		    "recordsize %ld or page size %ld\n", rsz, pg);
		return (2);
	}

	int fd = open(file, O_RDWR);
	if (fd < 0)
		return (fail("open"));

	struct stat st;
	if (fstat(fd, &st) != 0)
		return (fail("fstat"));
	if (st.st_size < 2 * rsz + pg) {
		(void) fprintf(stderr, "mmap_fadvise_readahead_subpage: %s is "
		    "shorter than two records and a page\n", file);
		return (2);
	}

	unsigned char *exp = malloc(pg);
	unsigned char *got = malloc(pg);
	unsigned char *wbuf = malloc(WLEN);
	if (exp == NULL || got == NULL || wbuf == NULL)
		return (fail("malloc"));
	(void) memset(wbuf, WBYTE, WLEN);

	/* Expected content: the original page with the write applied. */
	int ofd = open(orig, O_RDONLY);
	if (ofd < 0)
		return (fail("open orig"));
	if (pread(ofd, exp, pg, rsz) != pg)
		return (fail("pread orig"));
	(void) close(ofd);
	(void) memcpy(exp + WOFF, wbuf, WLEN);

	size_t maplen = rsz + pg;
	size_t npages = maplen / pg;
	unsigned char *p = mmap(NULL, maplen, PROT_READ, MAP_SHARED, fd, rsz);
	if (p == MAP_FAILED)
		return (fail("mmap"));
	if (madvise(p, maplen, MADV_RANDOM) != 0)
		return (fail("madvise"));

	volatile unsigned char c = p[maplen - pg];
	(void) c;

	unsigned char *vec = calloc(npages, 1);
	if (vec == NULL)
		return (fail("calloc"));
	if (mincore(p, maplen, vec) != 0)
		return (fail("mincore"));
	for (size_t i = 0; i < npages; i++) {
		if (!!(vec[i] & 1) != (i == npages - 1)) {
			(void) fprintf(stderr, "mmap_fadvise_readahead_subpage:"
			    " page %zu of the range is %sup to date in the page"
			    " cache\n", i, (vec[i] & 1) ? "" : "not ");
			return (2);
		}
	}

	int rfd = open(ready, O_CREAT | O_WRONLY, 0644);
	if (rfd < 0)
		return (fail("create ready file"));
	(void) close(rfd);

	if (wait_file(go, GO_TIMEOUT_MS) != 0) {
		(void) fprintf(stderr, "mmap_fadvise_readahead_subpage: no go "
		    "file after %d ms\n", GO_TIMEOUT_MS);
		return (2);
	}

	if (spray_stale(stale) != 0)
		return (fail("stale spray"));

	uint64_t start = now_ms();
	pid_t pid = fork();
	if (pid < 0)
		return (fail("fork"));
	if (pid == 0)
		_exit(pwrite(fd, wbuf, WLEN, rsz + WOFF) == WLEN ? 0 : 1);

	sleep_ms(WRITER_SETTLE_MS);

	int status = 0;
	int held = writer_running(pid, &status);
	int reaped = (held == 0);

	uint64_t f0 = now_ms();
	int err = posix_fadvise(fd, rsz, rsz + pg, POSIX_FADV_WILLNEED);
	uint64_t f1 = now_ms();

	int after = reaped ? 0 : writer_running(pid, &status);
	reaped = reaped || after == 0;
	while (!reaped && after >= 0 &&
	    now_ms() - start < WRITER_TIMEOUT_MS) {
		sleep_ms(POLL_MS);
		after = writer_running(pid, &status);
		reaped = (after == 0);
	}
	uint64_t wdone = now_ms() - start;

	(void) printf("fadvise(WILLNEED) latency: %llu ms\n",
	    (unsigned long long)(f1 - f0));
	(void) printf("writer: %s when fadvise started, %s after %llu ms\n",
	    held == 1 ? "running" : "finished",
	    reaped ? "finished" : "still running",
	    (unsigned long long)wdone);

	if (err != 0) {
		errno = err;
		return (fail("posix_fadvise"));
	}
	if (held < 0 || after < 0)
		return (fail("waitpid"));
	if (!reaped) {
		(void) fprintf(stderr, "mmap_fadvise_readahead_subpage: writer "
		    "did not finish within %d ms\n", WRITER_TIMEOUT_MS);
		return (2);
	}
	if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
		(void) fprintf(stderr, "mmap_fadvise_readahead_subpage: writer "
		    "failed (status 0x%x)\n", status);
		return (2);
	}

	if (wait_file(cleared, CLEARED_TIMEOUT_MS) != 0) {
		(void) fprintf(stderr, "mmap_fadvise_readahead_subpage: no "
		    "cleared file after %d ms\n", CLEARED_TIMEOUT_MS);
		return (2);
	}

	if (mincore(p, maplen, vec) != 0)
		return (fail("mincore"));
	(void) printf("after the write: mincore() reports page 0 of record 1 "
	    "%s\n", (vec[0] & 1) ? "up to date in the page cache" :
	    "not up to date, or not cached");

	(void) memcpy(got, p, pg);
	long dmap = compare("mmap", got, exp, pg);
	if (pread(fd, got, pg, rsz) != pg)
		return (fail("pread"));
	long dread = compare("pread", got, exp, pg);

	if (held != 1)
		return (3);
	return (dmap == 0 && dread == 0 ? 0 : 4);
}
