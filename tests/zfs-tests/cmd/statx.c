/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright (c) 2025, Rob Norris <robn@despairlabs.com>
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

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/syscall.h>
#include <unistd.h>

/*
 * statx() may be available in the kernel, but not in the libc, so we build
 * our own wrapper if we can't link one.
 */

#ifndef __NR_statx
#if defined(__x86_64__)
#define	__NR_statx (332)
#elif defined(__i386__)
#define	__NR_statx (383)
#elif defined(__s390__)
#define	__NR_statx (379)
#elif defined(__arm__)
#define	__NR_statx (397)
#elif defined(__aarch64__)
#define	__NR_statx (291)
#elif defined(__powerpc__)
#define	__NR_statx (383)
#else
#error "no definition of __NR_statx for this platform"
#endif
#endif /* __NR_statx */


int
statx(int, const char *, int, unsigned int, void *)
    __attribute__((weak));

static inline int
_statx(int fd, const char *path, int flags, unsigned int mask, void *stx)
{
	if (statx)
		return (statx(fd, path, flags, mask, stx));
	else
		return (syscall(__NR_statx, fd, path, flags, mask, stx));
}

#ifndef STATX_TYPE
#define	STATX_TYPE	(1<<0)
#endif
#ifndef STATX_MODE
#define	STATX_MODE	(1<<1)
#endif
#ifndef STATX_NLINK
#define	STATX_NLINK	(1<<2)
#endif
#ifndef STATX_UID
#define	STATX_UID	(1<<3)
#endif
#ifndef STATX_GID
#define	STATX_GID	(1<<4)
#endif
#ifndef STATX_ATIME
#define	STATX_ATIME	(1<<5)
#endif
#ifndef STATX_MTIME
#define	STATX_MTIME	(1<<6)
#endif
#ifndef STATX_CTIME
#define	STATX_CTIME	(1<<7)
#endif
#ifndef STATX_INO
#define	STATX_INO	(1<<8)
#endif
#ifndef STATX_SIZE
#define	STATX_SIZE	(1<<9)
#endif
#ifndef STATX_BLOCKS
#define	STATX_BLOCKS	(1<<10)
#endif
#ifndef STATX_BTIME
#define	STATX_BTIME	(1<<11)
#endif
#ifndef STATX_MNT_ID
#define	STATX_MNT_ID	(1<<12)
#endif
#ifndef STATX_DIOALIGN
#define	STATX_DIOALIGN	(1<<13)
#endif
#ifndef S_IFMT
#define	S_IFMT 0170000
#endif

typedef struct {
	int64_t		tv_sec;
	uint32_t	tv_nsec;
	int32_t		_pad;
} stx_timestamp_t;
_Static_assert(sizeof (stx_timestamp_t) == 0x10,
	"stx_timestamp_t not 16 bytes");

typedef struct {
	uint32_t	stx_mask;
	uint32_t	stx_blksize;
	uint64_t	stx_attributes;
	uint32_t	stx_nlink;
	uint32_t	stx_uid;
	uint32_t	stx_gid;
	uint16_t	stx_mode;
	uint16_t	_pad1;
	uint64_t	stx_ino;
	uint64_t	stx_size;
	uint64_t	stx_blocks;
	uint64_t	stx_attributes_mask;
	stx_timestamp_t	stx_atime;
	stx_timestamp_t	stx_btime;
	stx_timestamp_t	stx_ctime;
	stx_timestamp_t	stx_mtime;
	uint32_t	stx_rdev_major;
	uint32_t	stx_rdev_minor;
	uint32_t	stx_dev_major;
	uint32_t	stx_dev_minor;
	uint64_t	stx_mnt_id;
	uint32_t	stx_dio_mem_align;
	uint32_t	stx_dio_offset_align;
	uint64_t	_pad2[12];
} stx_t;
_Static_assert(sizeof (stx_t) == 0x100, "stx_t not 256 bytes");

typedef struct {
	const char *name;
	unsigned int mask;
} stx_field_t;

stx_field_t fields[] = {
	{ "type",	STATX_TYPE },
	{ "mode",	STATX_MODE },
	{ "nlink",	STATX_NLINK },
	{ "uid",	STATX_UID },
	{ "gid",	STATX_GID },
	{ "atime",	STATX_ATIME },
	{ "mtime",	STATX_MTIME },
	{ "ctime",	STATX_CTIME },
	{ "ino",	STATX_INO },
	{ "size",	STATX_SIZE },
	{ "blocks",	STATX_BLOCKS },
	{ "btime",	STATX_BTIME },
	{ "mnt_id",	STATX_MNT_ID },
	{ "dioalign",	STATX_DIOALIGN },
	{ NULL },
};

static int
usage(void)
{
	printf(
	    "usage: statx <field[,field,field]> <file>\n"
	    "available fields:\n");

	int w = 0;
	for (stx_field_t *f = fields; f->name != NULL; f++) {
		if (w > 0 && (w + strlen(f->name) + 1) > 60) {
			fputc('\n', stdout);
			w = 0;
		}
		if (w == 0)
			fputc(' ', stdout);
		w += printf(" %s", f->name);
	}
	if (w > 0)
		fputc('\n', stdout);
	return (1);
}

int
main(int argc, char **argv)
{
	if (argc < 3)
		return (usage());

	unsigned int mask = 0;

	char *name;
	while ((name = strsep(&argv[1], ",")) != NULL) {
		stx_field_t *f;
		for (f = fields; f->name != NULL; f++) {
			if (strcmp(name, f->name) == 0) {
				mask |= f->mask;
				break;
			}
		}
		if (f->name == NULL) {
			fprintf(stderr, "unknown field name: %s\n", name);
			return (usage());
		}
	}

	int fd = open(argv[2], O_PATH);
	if (fd < 0) {
		fprintf(stderr, "open: %s: %s\n", argv[2], strerror(errno));
		return (1);
	}

	stx_t stx = {};

	if (_statx(fd, "",
	    AT_EMPTY_PATH | AT_SYMLINK_NOFOLLOW, mask, &stx) < 0) {
		fprintf(stderr, "statx: %s: %s\n", argv[2], strerror(errno));
		close(fd);
		return (1);
	}

	int rc = 0;

	for (stx_field_t *f = fields; f->name != NULL; f++) {
		if (!(mask & f->mask))
			continue;
		if (!(stx.stx_mask & f->mask)) {
			printf("statx: kernel did not return field: %s\n",
			    f->name);
			rc = 2;
			continue;
		}
	}

	if (rc > 0)
		return (rc);

	for (stx_field_t *f = fields; f->name != NULL; f++) {
		if (!(mask & f->mask))
			continue;

		switch (f->mask) {
		case STATX_TYPE:
			printf("type: %u\n", stx.stx_mode & S_IFMT);
			break;
		case STATX_MODE:
			printf("mode: %u\n", stx.stx_mode & ~S_IFMT);
			break;
		case STATX_NLINK:
			printf("nlink: %u\n", stx.stx_nlink);
			break;
		case STATX_UID:
			printf("uid: %u\n", stx.stx_uid);
			break;
		case STATX_GID:
			printf("gid: %u\n", stx.stx_gid);
			break;
		case STATX_ATIME:
			printf("atime: %ld.%u\n",
			    stx.stx_atime.tv_sec, stx.stx_atime.tv_nsec);
			break;
		case STATX_MTIME:
			printf("mtime: %ld.%u\n",
			    stx.stx_mtime.tv_sec, stx.stx_mtime.tv_nsec);
			break;
		case STATX_CTIME:
			printf("ctime: %ld.%u\n",
			    stx.stx_ctime.tv_sec, stx.stx_ctime.tv_nsec);
			break;
		case STATX_INO:
			printf("ino: %lu\n", stx.stx_ino);
			break;
		case STATX_SIZE:
			printf("size: %lu\n", stx.stx_size);
			break;
		case STATX_BLOCKS:
			printf("blocks: %lu\n", stx.stx_blocks);
			break;
		case STATX_BTIME:
			printf("btime: %ld.%u\n",
			    stx.stx_btime.tv_sec, stx.stx_btime.tv_nsec);
			break;
		case STATX_MNT_ID:
			printf("mnt_id: %lu\n", stx.stx_mnt_id);
			break;
		case STATX_DIOALIGN:
			printf("dioalign: %u %u\n",
			    stx.stx_dio_mem_align, stx.stx_dio_offset_align);
			break;
		}
	}

	return (rc);
}
