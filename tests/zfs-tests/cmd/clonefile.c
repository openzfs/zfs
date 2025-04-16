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
 */

/*
 * This program is to test the availability and behaviour of copy_file_range,
 * FICLONE, FICLONERANGE and FIDEDUPERANGE in the Linux kernel. It should
 * compile and run even if these features aren't exposed through the libc.
 */

#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <stdlib.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

#ifndef __NR_copy_file_range
#if defined(__x86_64__)
#define	__NR_copy_file_range (326)
#elif defined(__i386__)
#define	__NR_copy_file_range (377)
#elif defined(__s390__)
#define	__NR_copy_file_range (375)
#elif defined(__arm__)
#define	__NR_copy_file_range (391)
#elif defined(__aarch64__)
#define	__NR_copy_file_range (285)
#elif defined(__powerpc__)
#define	__NR_copy_file_range (379)
#else
#error "no definition of __NR_copy_file_range for this platform"
#endif
#endif /* __NR_copy_file_range */

#ifdef __FreeBSD__
#define	loff_t	off_t
#endif

ssize_t
copy_file_range(int, loff_t *, int, loff_t *, size_t, unsigned int)
    __attribute__((weak));

static inline ssize_t
cf_copy_file_range(int sfd, loff_t *soff, int dfd, loff_t *doff,
    size_t len, unsigned int flags)
{
	if (copy_file_range)
		return (copy_file_range(sfd, soff, dfd, doff, len, flags));
	return (
	    syscall(__NR_copy_file_range, sfd, soff, dfd, doff, len, flags));
}

/* Define missing FICLONE */
#ifdef FICLONE
#define	CF_FICLONE	FICLONE
#else
#define	CF_FICLONE	_IOW(0x94, 9, int)
#endif

/* Define missing FICLONERANGE and support structs */
#ifdef FICLONERANGE
#define	CF_FICLONERANGE	FICLONERANGE
typedef struct file_clone_range cf_file_clone_range_t;
#else
typedef struct {
	int64_t		src_fd;
	uint64_t	src_offset;
	uint64_t	src_length;
	uint64_t	dest_offset;
} cf_file_clone_range_t;
#define	CF_FICLONERANGE	_IOW(0x94, 13, cf_file_clone_range_t)
#endif

/* Define missing FIDEDUPERANGE and support structs */
#ifdef FIDEDUPERANGE
#define	CF_FIDEDUPERANGE		FIDEDUPERANGE
#define	CF_FILE_DEDUPE_RANGE_SAME	FILE_DEDUPE_RANGE_SAME
#define	CF_FILE_DEDUPE_RANGE_DIFFERS	FILE_DEDUPE_RANGE_DIFFERS
typedef struct file_dedupe_range_info	cf_file_dedupe_range_info_t;
typedef struct file_dedupe_range	cf_file_dedupe_range_t;
#else
typedef struct {
	int64_t dest_fd;
	uint64_t dest_offset;
	uint64_t bytes_deduped;
	int32_t status;
	uint32_t reserved;
} cf_file_dedupe_range_info_t;
typedef struct {
	uint64_t src_offset;
	uint64_t src_length;
	uint16_t dest_count;
	uint16_t reserved1;
	uint32_t reserved2;
	cf_file_dedupe_range_info_t info[0];
} cf_file_dedupe_range_t;
#define	CF_FIDEDUPERANGE		_IOWR(0x94, 54, cf_file_dedupe_range_t)
#define	CF_FILE_DEDUPE_RANGE_SAME	(0)
#define	CF_FILE_DEDUPE_RANGE_DIFFERS	(1)
#endif

typedef enum {
	CF_MODE_NONE,
	CF_MODE_CLONE,
	CF_MODE_CLONERANGE,
	CF_MODE_COPYFILERANGE,
	CF_MODE_DEDUPERANGE,
} cf_mode_t;

static int
usage(void)
{
	printf(
	    "usage:\n"
	    "  FICLONE:\n"
	    "    clonefile -c <src> <dst>\n"
	    "  FICLONERANGE:\n"
	    "    clonefile -r <src> <dst> <soff> <doff> <len>\n"
	    "  copy_file_range:\n"
	    "    clonefile -f <src> <dst> [<soff> <doff> <len | \"all\">]\n"
	    "  FIDEDUPERANGE:\n"
	    "    clonefile -d <src> <dst> <soff> <doff> <len>\n");
	return (1);
}

int do_clone(int sfd, int dfd);
int do_clonerange(int sfd, int dfd, loff_t soff, loff_t doff, size_t len);
int do_copyfilerange(int sfd, int dfd, loff_t soff, loff_t doff, size_t len);
int do_deduperange(int sfd, int dfd, loff_t soff, loff_t doff, size_t len);

int quiet = 0;

int
main(int argc, char **argv)
{
	cf_mode_t mode = CF_MODE_NONE;

	int c;
	while ((c = getopt(argc, argv, "crfdq")) != -1) {
		switch (c) {
			case 'c':
				mode = CF_MODE_CLONE;
				break;
			case 'r':
				mode = CF_MODE_CLONERANGE;
				break;
			case 'f':
				mode = CF_MODE_COPYFILERANGE;
				break;
			case 'd':
				mode = CF_MODE_DEDUPERANGE;
				break;
			case 'q':
				quiet = 1;
				break;
		}
	}

	switch (mode) {
		case CF_MODE_NONE:
			return (usage());
		case CF_MODE_CLONE:
			if ((argc-optind) != 2)
				return (usage());
			break;
		case CF_MODE_CLONERANGE:
		case CF_MODE_DEDUPERANGE:
			if ((argc-optind) != 5)
				return (usage());
			break;
		case CF_MODE_COPYFILERANGE:
			if ((argc-optind) != 2 && (argc-optind) != 5)
				return (usage());
			break;
		default:
			abort();
	}

	loff_t soff = 0, doff = 0;
	size_t len = SSIZE_MAX;
	unsigned long long len2;
	if ((argc-optind) == 5) {
		soff = strtoull(argv[optind+2], NULL, 10);
		if (soff == ULLONG_MAX) {
			fprintf(stderr, "invalid source offset");
			return (1);
		}
		doff = strtoull(argv[optind+3], NULL, 10);
		if (doff == ULLONG_MAX) {
			fprintf(stderr, "invalid dest offset");
			return (1);
		}
		if (mode == CF_MODE_COPYFILERANGE &&
		    strcmp(argv[optind+4], "all") == 0) {
			len = SSIZE_MAX;
		} else {
			len2 = strtoull(argv[optind+4], NULL, 10);
			if (len2 == ULLONG_MAX) {
				fprintf(stderr, "invalid length");
				return (1);
			}
			if (len2 < SSIZE_MAX)
				len = (size_t)len2;
		}
	}

	int sfd = open(argv[optind], O_RDONLY);
	if (sfd < 0) {
		fprintf(stderr, "open: %s: %s\n",
		    argv[optind], strerror(errno));
		return (1);
	}

	int dfd = open(argv[optind+1], O_WRONLY|O_CREAT,
	    S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH);
	if (dfd < 0) {
		fprintf(stderr, "open: %s: %s\n",
		    argv[optind+1], strerror(errno));
		close(sfd);
		return (1);
	}

	int err;
	switch (mode) {
		case CF_MODE_CLONE:
			err = do_clone(sfd, dfd);
			break;
		case CF_MODE_CLONERANGE:
			err = do_clonerange(sfd, dfd, soff, doff, len);
			break;
		case CF_MODE_COPYFILERANGE:
			err = do_copyfilerange(sfd, dfd, soff, doff, len);
			break;
		case CF_MODE_DEDUPERANGE:
			err = do_deduperange(sfd, dfd, soff, doff, len);
			break;
		default:
			abort();
	}

	if (!quiet) {
		off_t spos = lseek(sfd, 0, SEEK_CUR);
		off_t slen = lseek(sfd, 0, SEEK_END);
		off_t dpos = lseek(dfd, 0, SEEK_CUR);
		off_t dlen = lseek(dfd, 0, SEEK_END);

		fprintf(stderr, "file offsets: src=%jd/%jd; dst=%jd/%jd\n",
		    spos, slen, dpos, dlen);
	}

	close(dfd);
	close(sfd);

	return (err == 0 ? 0 : 1);
}

int
do_clone(int sfd, int dfd)
{
	if (!quiet)
		fprintf(stderr, "using FICLONE\n");
	int err = ioctl(dfd, CF_FICLONE, sfd);
	if (err < 0) {
		fprintf(stderr, "ioctl(FICLONE): %s\n", strerror(errno));
		return (err);
	}
	return (0);
}

int
do_clonerange(int sfd, int dfd, loff_t soff, loff_t doff, size_t len)
{
	if (!quiet)
		fprintf(stderr, "using FICLONERANGE\n");
	cf_file_clone_range_t fcr = {
		.src_fd = sfd,
		.src_offset = soff,
		.src_length = len,
		.dest_offset = doff,
	};
	int err = ioctl(dfd, CF_FICLONERANGE, &fcr);
	if (err < 0) {
		fprintf(stderr, "ioctl(FICLONERANGE): %s\n", strerror(errno));
		return (err);
	}
	return (0);
}

int
do_copyfilerange(int sfd, int dfd, loff_t soff, loff_t doff, size_t len)
{
	if (!quiet)
		fprintf(stderr, "using copy_file_range\n");
	ssize_t copied = cf_copy_file_range(sfd, &soff, dfd, &doff, len, 0);
	if (copied < 0) {
		fprintf(stderr, "copy_file_range: %s\n", strerror(errno));
		return (1);
	}
	if (len == SSIZE_MAX) {
		struct stat sb;

		if (fstat(sfd, &sb) < 0) {
			fprintf(stderr, "fstat(sfd): %s\n", strerror(errno));
			return (1);
		}
		len = sb.st_size;
	}
	if (copied != len) {
		fprintf(stderr, "copy_file_range: copied less than requested: "
		    "requested=%zu; copied=%zd\n", len, copied);
		return (1);
	}
	return (0);
}

int
do_deduperange(int sfd, int dfd, loff_t soff, loff_t doff, size_t len)
{
	if (!quiet)
		fprintf(stderr, "using FIDEDUPERANGE\n");

	char buf[sizeof (cf_file_dedupe_range_t)+
	    sizeof (cf_file_dedupe_range_info_t)] = {0};
	cf_file_dedupe_range_t *fdr = (cf_file_dedupe_range_t *)&buf[0];
	cf_file_dedupe_range_info_t *fdri =
	    (cf_file_dedupe_range_info_t *)
	    &buf[sizeof (cf_file_dedupe_range_t)];

	fdr->src_offset = soff;
	fdr->src_length = len;
	fdr->dest_count = 1;

	fdri->dest_fd = dfd;
	fdri->dest_offset = doff;

	int err = ioctl(sfd, CF_FIDEDUPERANGE, fdr);
	if (err != 0)
		fprintf(stderr, "ioctl(FIDEDUPERANGE): %s\n", strerror(errno));

	if (fdri->status < 0) {
		fprintf(stderr, "dedup failed: %s\n", strerror(-fdri->status));
		err = -1;
	} else if (fdri->status == CF_FILE_DEDUPE_RANGE_DIFFERS) {
		fprintf(stderr, "dedup failed: range differs\n");
		err = -1;
	}

	return (err);
}
