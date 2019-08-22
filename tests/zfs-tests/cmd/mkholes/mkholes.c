/*
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this
 * source.  A copy of the CDDL is also available via the Internet at
 * http://www.illumos.org/license/CDDL.
 */

/*
 * Copyright (c) 2014, 2016 by Delphix. All rights reserved.
 */

#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/errno.h>
#include <umem.h>
#include <stddef.h>
#include <string.h>
#include <sys/types.h>
#include <sys/list.h>
#include <sys/sysmacros.h>
#include <sys/zfs_context.h>

typedef enum {
	SEG_HOLE,
	SEG_DATA,
	SEG_TYPES
} seg_type_t;

typedef struct segment {
	list_node_t	seg_node;
	seg_type_t	seg_type;
	off_t		seg_offset;
	off_t		seg_len;
} seg_t;

static int
no_memory(void) {
	(void) fprintf(stderr, "malloc failed\n");
	exit(255);
	return (0);
}

static void
usage(char *msg, int exit_value)
{
	(void) fprintf(stderr, "mkholes [-d|h offset:length] ... filename\n");
	(void) fprintf(stderr, "%s\n", msg);
	exit(exit_value);
}

static char *
get_random_buffer(size_t len)
{
	int	rand_fd;
	char	*buf;

	buf = umem_alloc(len, UMEM_NOFAIL);

	/*
	 * Fill the buffer from /dev/urandom to counteract the
	 * effects of compression.
	 */
	if ((rand_fd = open("/dev/urandom", O_RDONLY)) < 0) {
		perror("open /dev/urandom failed");
		exit(1);
	}

	if (read(rand_fd, buf, len) < 0) {
		perror("read /dev/urandom failed");
		exit(1);
	}

	(void) close(rand_fd);

	return (buf);
}

static void
push_segment(list_t *seg_list, seg_type_t seg_type, char *optarg)
{
	char		*off_str, *len_str;
	static off_t	file_size = 0;
	off_t		off, len;
	seg_t		*seg;

	off_str = strtok(optarg, ":");
	len_str = strtok(NULL, ":");

	if (off_str == NULL || len_str == NULL)
		usage("Bad offset or length", 1);

	off = strtoull(off_str, NULL, 0);
	len = strtoull(len_str, NULL, 0);

	if (file_size >= off + len)
		usage("Ranges must ascend and may not overlap.", 1);
	file_size = off + len;

	seg = umem_alloc(sizeof (seg_t), UMEM_NOFAIL);
	seg->seg_type = seg_type;
	seg->seg_offset = off;
	seg->seg_len = len;

	list_insert_tail(seg_list, seg);
}

int
main(int argc, char *argv[])
{
	int	c, fd;
	char	*fname;
	list_t	seg_list;
	seg_t	*seg;

	umem_nofail_callback(no_memory);
	list_create(&seg_list, sizeof (seg_t), offsetof(seg_t, seg_node));

	while ((c = getopt(argc, argv, "d:h:")) != -1) {
		switch (c) {
		case 'd':
			push_segment(&seg_list, SEG_DATA, optarg);
			break;
		case 'h':
			push_segment(&seg_list, SEG_HOLE, optarg);
			break;
		}
	}
	argc -= optind;
	argv += optind;

	if ((fname = argv[0]) == NULL)
		usage("No filename specified", 1);
	fname = argv[0];

	if ((fd = open(fname, O_LARGEFILE | O_RDWR | O_CREAT | O_SYNC,
	    00666)) < 0) {
		perror("open failed");
		exit(1);
	}

	fprintf(stderr, "a\n");

	while ((seg = list_remove_head(&seg_list)) != NULL) {
		fprintf(stderr, "b\n");
		char	*buf, *vbuf;
		off_t	off = seg->seg_offset;
		off_t	len = seg->seg_len;

		if (seg->seg_type == SEG_HOLE) {
			fprintf(stderr, "c\n");
			off_t bytes_read = 0;
			ssize_t readlen = 1024 * 1024 * 16;

#if defined(FALLOC_FL_PUNCH_HOLE) && defined(FALLOC_FL_KEEP_SIZE)
			off_t cur_len = lseek(fd, 0, SEEK_END) + 1;
			if (cur_len <= 0) {
				perror("lseek failed");
				exit(1);
			}
			if (off + len > cur_len) {
				if (ftruncate(fd, off + len) < 0) {
					perror("extend failed");
					exit(1);
				}

			}
			if (fallocate(fd, FALLOC_FL_PUNCH_HOLE | FALLOC_FL_KEEP_SIZE,
			    off, len) < 0) {
				perror("punch hole failed");
				exit(1);
			}
#else /* !(defined(FALLOC_FL_PUNCH_HOLE) && defined(FALLOC_FL_KEEP_SIZE)) */
			{
				perror("FALLOC_FL_PUNCH_HOLE unsupported");
				exit(1);
			}
#endif /* defined(FALLOC_FL_PUNCH_HOLE) && defined(FALLOC_FL_KEEP_SIZE) */

			fprintf(stderr, "d\n");
			buf = (char *)umem_alloc(readlen, UMEM_NOFAIL);
			vbuf = (char *)umem_zalloc(readlen, UMEM_NOFAIL);
			while (bytes_read < len) {
				fprintf(stderr, "e\n");
				ssize_t bytes = pread(fd, buf, readlen, off);
				if (bytes < 0) {
					perror("pread hole failed");
					exit(1);
				}

				if (memcmp(buf, vbuf, MIN(bytes, len)) != 0) {
					(void) fprintf(stderr, "Read back hole "
					    "didn't match.\n");
					exit(1);
				}
				bytes_read += bytes;
				off += bytes;
			}
			fprintf(stderr, "f\n");

			umem_free(buf, readlen);
			umem_free(vbuf, readlen);
			umem_free(seg, sizeof (seg_t));
		} else if (seg->seg_type == SEG_DATA) {
			buf = get_random_buffer(len);
			vbuf = (char *)umem_alloc(len, UMEM_NOFAIL);
			if ((pwrite(fd, buf, len, off)) < 0) {
				perror("pwrite failed");
				exit(1);
			}

			if ((pread(fd, vbuf, len, off)) != len) {
				perror("pread failed");
				exit(1);
			}

			if (memcmp(buf, vbuf, len) != 0) {
				(void) fprintf(stderr, "Read back buf didn't "
				    "match.\n");
				exit(1);
			}

			umem_free(buf, len);
			umem_free(vbuf, len);
			umem_free(seg, sizeof (seg_t));
		}
	}

	(void) close(fd);
	return (0);
}
