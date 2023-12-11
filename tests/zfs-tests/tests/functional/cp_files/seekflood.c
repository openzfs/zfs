/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright (c) 2023, Rob Norris <robn@despairlabs.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 */

#ifndef	_GNU_SOURCE
#define	_GNU_SOURCE
#endif

#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/wait.h>

#define	DATASIZE	(4096)
char data[DATASIZE];

static int
_open_file(int n, int wr)
{
	char buf[256];
	int fd;

	snprintf(buf, sizeof (buf), "testdata_%d_%d", getpid(), n);

	if ((fd = open(buf, wr ? (O_WRONLY | O_CREAT) : O_RDONLY,
	    wr ? (S_IRUSR | S_IWUSR) : 0)) < 0) {
		fprintf(stderr, "Error: open '%s' (%s): %s\n",
		    buf, wr ? "write" : "read", strerror(errno));
		exit(1);
	}

	return (fd);
}

static void
_write_file(int n, int fd)
{
	/* write a big ball of stuff */
	ssize_t nwr = write(fd, data, DATASIZE);
	if (nwr < 0) {
		fprintf(stderr, "Error: write '%d_%d': %s\n",
		    getpid(), n, strerror(errno));
		exit(1);
	} else if (nwr < DATASIZE) {
		fprintf(stderr, "Error: write '%d_%d': short write\n", getpid(),
		    n);
		exit(1);
	}
}

static int
_seek_file(int n, int fd)
{
	struct stat st;
	if (fstat(fd, &st) < 0) {
		fprintf(stderr, "Error: fstat '%d_%d': %s\n", getpid(), n,
		    strerror(errno));
		exit(1);
	}

	/*
	 * A zero-sized file correctly has no data, so seeking the file is
	 * pointless.
	 */
	if (st.st_size == 0)
		return (0);

	/* size is real, and we only write, so SEEK_DATA must find something */
	if (lseek(fd, 0, SEEK_DATA) < 0) {
		if (errno == ENXIO)
			return (1);
		fprintf(stderr, "Error: lseek '%d_%d': %s\n",
		    getpid(), n, strerror(errno));
		exit(2);
	}

	return (0);
}

int
main(int argc, char **argv)
{
	int nfiles = 0;
	int nthreads = 0;

	if (argc < 3 || (nfiles = atoi(argv[1])) == 0 ||
	    (nthreads = atoi(argv[2])) == 0) {
		printf("usage: seekflood <nfiles> <threads>\n");
		exit(1);
	}

	memset(data, 0x5a, DATASIZE);

	/* fork off some flood threads */
	for (int i = 0; i < nthreads; i++) {
		if (!fork()) {
			/* thread main */

			/* create zero file */
			int fd = _open_file(0, 1);
			_write_file(0, fd);
			close(fd);

			int count = 0;

			int h = 0, i, j, rfd, wfd;
			for (i = 0; i < nfiles; i += 2, h++) {
				j = i+1;

				/* seek h, write i */
				rfd = _open_file(h, 0);
				wfd = _open_file(i, 1);
				count += _seek_file(h, rfd);
				_write_file(i, wfd);
				close(rfd);
				close(wfd);

				/* seek i, write j */
				rfd = _open_file(i, 0);
				wfd = _open_file(j, 1);
				count += _seek_file(i, rfd);
				_write_file(j, wfd);
				close(rfd);
				close(wfd);
			}

			/* return count of failed seeks to parent */
			exit(count < 256 ? count : 255);
		}
	}

	/* wait for threads, take their seek fail counts from exit code */
	int count = 0, crashed = 0;
	for (int i = 0; i < nthreads; i++) {
		int wstatus;
		wait(&wstatus);
		if (WIFEXITED(wstatus))
			count += WEXITSTATUS(wstatus);
		else
			crashed++;
	}

	if (crashed) {
		fprintf(stderr, "Error: child crashed; test failed\n");
		exit(1);
	}

	if (count) {
		fprintf(stderr, "Error: %d seek failures; test failed\n",
		    count);
		exit(1);
	}

	exit(0);
}
