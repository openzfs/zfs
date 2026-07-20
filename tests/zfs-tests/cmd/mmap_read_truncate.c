// SPDX-License-Identifier: CDDL-1.0
/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://opensource.org/licenses/CDDL-1.0.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * CDDL HEADER END
 */

/*
 * Copyright (c) 2026, Michael Heller.
 */

/*
 * Regression exerciser for a mmap read racing ftruncate (openzfs #18715).
 *
 * When a page is faulted in for read after the file has been truncated below
 * that page, zfs_fillpage() sees io_off >= i_size. On an unfixed build the
 * unsigned io_len = i_size - io_off underflows and dmu_read() zero-fills far
 * past the single page, trampling memory (a physical-page sweep). The fix
 * simply zero-fills the page and returns.
 *
 * Several reader processes repeatedly mmap() the file and fault every page,
 * while a truncator process churns the file size between 0 and <size>. A read
 * that lands entirely beyond EOF legitimately raises SIGBUS; the reader
 * tolerates that and keeps going. The test just has to survive the race for
 * the configured duration -- on an unfixed module the underflow corrupts
 * memory and takes the run (or the kernel) down.
 *
 * usage: mmap_read_truncate <file> <size> <seconds> [nreaders]
 */

#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <setjmp.h>
#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define	_pdfail(f, l, s)	\
	do { perror("[" f "#" #l "] " s); exit(2); } while (0)
#define	pdfail(str)	_pdfail(__FILE__, __LINE__, str)

static sigjmp_buf jb;
static volatile sig_atomic_t in_probe;

static void
on_bus(int sig)
{
	(void) sig;
	if (in_probe)
		siglongjmp(jb, 1);
	_exit(4);
}

static void
reader_loop(const char *file, off_t sz, time_t end)
{
	long pg = sysconf(_SC_PAGESIZE);
	int fd = open(file, O_RDONLY);
	if (fd < 0)
		pdfail("reader open");

	struct sigaction sa;
	memset(&sa, 0, sizeof (sa));
	sa.sa_handler = on_bus;
	sigaction(SIGBUS, &sa, NULL);

	volatile unsigned long acc = 0;
	while (time(NULL) < end) {
		char *p = mmap(NULL, sz, PROT_READ, MAP_SHARED, fd, 0);
		if (p == MAP_FAILED)
			continue;
		/* volatile: must survive the SIGBUS siglongjmp */
		for (volatile off_t off = 0; off < sz; off += pg) {
			in_probe = 1;
			if (sigsetjmp(jb, 1) == 0)
				acc += (unsigned char)p[off];
			in_probe = 0;
		}
		(void) munmap(p, sz);
	}
	close(fd);
	(void) acc;
	_exit(0);
}

int
main(int argc, char **argv)
{
	if (argc < 4 || argc > 5) {
		fprintf(stderr, "usage: mmap_read_truncate "
		    "<file> <size> <secs> [nreaders]\n");
		exit(2);
	}
	const char *file = argv[1];
	off_t sz = (off_t)strtoull(argv[2], NULL, 0);
	long secs = strtol(argv[3], NULL, 0);
	int nreaders = (argc == 5) ? atoi(argv[4]) : 4;
	if (sz <= 0 || secs <= 0 || nreaders <= 0) {
		fprintf(stderr, "E: invalid args\n");
		exit(2);
	}

	int fd = open(file, O_CREAT|O_RDWR, S_IRUSR|S_IWUSR);
	if (fd < 0)
		pdfail("open");
	if (ftruncate(fd, sz) < 0)
		pdfail("ftruncate init");
	close(fd);

	time_t end = time(NULL) + secs;

	pid_t kids[64];
	int nk = 0;

	for (int i = 0; i < nreaders; i++) {
		pid_t c = fork();
		if (c < 0)
			pdfail("fork reader");
		if (c == 0)
			reader_loop(file, sz, end);
		kids[nk++] = c;
	}

	pid_t t = fork();
	if (t < 0)
		pdfail("fork truncator");
	if (t == 0) {
		int tfd = open(file, O_RDWR);
		if (tfd < 0)
			pdfail("truncator open");
		while (time(NULL) < end) {
			if (ftruncate(tfd, 0) < 0)
				_exit(3);
			if (ftruncate(tfd, sz) < 0)
				_exit(3);
		}
		_exit(0);
	}
	kids[nk++] = t;

	int rc = 0;
	for (int i = 0; i < nk; i++) {
		int status;
		if (waitpid(kids[i], &status, 0) < 0)
			pdfail("waitpid");
		if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
			fprintf(stderr, "child %d abnormal (status=0x%x)\n",
			    kids[i], status);
			rc = 1;
		}
	}
	return (rc);
}
