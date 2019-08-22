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
 * Copyright (c) 2014 by Delphix. All rights reserved.
 */

#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <libzfs.h>
#include <stdlib.h>
#include <stddef.h>
#include <sys/types.h>
#include <sys/list.h>
#include <sys/stat.h>
#include <sys/errno.h>
#include <umem.h>

#define	PRINT_HOLE 0x1
#define	PRINT_DATA 0x2
#define	PRINT_VERBOSE 0x4

static void
usage(char *msg, int exit_value)
{
	(void) fprintf(stderr, "getholes [-dhv] filename\n");
	(void) fprintf(stderr, "%s\n", msg);
	exit(exit_value);
}

typedef struct segment {
	list_node_t	seg_node;
	int		seg_type;
	off_t		seg_offset;
	off_t		seg_len;
} seg_t;

/*
 * Return an appropriate whence value, depending on whether the file begins
 * with a holes or data.
 */
static int
starts_with_hole(int fd)
{
	off_t	off;

	if ((off = lseek(fd, 0, SEEK_HOLE)) == -1) {
		/* ENXIO means no holes were found */
		if (errno == ENXIO)
			return (SEEK_DATA);
		perror("lseek failed");
		exit(1);
	}

	return (off == 0 ? SEEK_HOLE : SEEK_DATA);
}

static void
print_list(list_t *seg_list, char *fname, int options)
{
	uint64_t	lz_holes, bs = 0;
	uint64_t	hole_blks_seen = 0, data_blks_seen = 0;
	seg_t		*seg;

	if (0 == bs)
		if (zfs_get_hole_count(fname, &lz_holes, &bs) != 0) {
			perror("zfs_get_hole_count");
			exit(1);
		}

	while ((seg = list_remove_head(seg_list)) != NULL) {
		if (options & PRINT_VERBOSE)
			(void) fprintf(stdout, "%c %llu:%llu\n",
			    seg->seg_type == SEEK_HOLE ? 'h' : 'd',
			    (unsigned long long)seg->seg_offset,
			    (unsigned long long)seg->seg_len);

		if (seg->seg_type == SEEK_HOLE) {
			hole_blks_seen += seg->seg_len / bs;
		} else {
			data_blks_seen += seg->seg_len / bs;
		}
		umem_free(seg, sizeof (seg_t));
	}

	/* Verify libzfs sees the same number of hole blocks found manually. */
	if (lz_holes != hole_blks_seen) {
		(void) fprintf(stderr, "Counted %llu holes, but libzfs found "
		    "%llu\n", (unsigned long long)hole_blks_seen,
		    (unsigned long long)lz_holes);
		exit(1);
	}

	if (options & PRINT_HOLE && options & PRINT_DATA) {
		(void) fprintf(stdout, "datablks: %llu\n",
		    (unsigned long long)data_blks_seen);
		(void) fprintf(stdout, "holeblks: %llu\n",
		    (unsigned long long)hole_blks_seen);
		return;
	}

	if (options & PRINT_DATA) {
		(void) fprintf(stdout, "%llu\n",
		    (unsigned long long)data_blks_seen);
	}
	if (options & PRINT_HOLE) {
		(void) fprintf(stdout, "%llu\n",
		    (unsigned long long)hole_blks_seen);
	}
}

int
main(int argc, char *argv[])
{
	off_t		len, off = 0;
	int		c, fd, options = 0, whence = SEEK_DATA;
	struct stat	statbuf;
	char		*fname;
	list_t		seg_list;
	seg_t		*seg = NULL;

	list_create(&seg_list, sizeof (seg_t), offsetof(seg_t, seg_node));

	while ((c = getopt(argc, argv, "dhv")) != -1) {
		switch (c) {
		case 'd':
			options |= PRINT_DATA;
			break;
		case 'h':
			options |= PRINT_HOLE;
			break;
		case 'v':
			options |= PRINT_VERBOSE;
			break;
		}
	}
	argc -= optind;
	argv += optind;

	if (argc != 1)
		usage("Incorrect number of arguments.", 1);

	if ((fname = argv[0]) == NULL)
		usage("No filename provided.", 1);

	if ((fd = open(fname, O_LARGEFILE | O_RDONLY)) < 0) {
		perror("open failed");
		exit(1);
	}

	if (fstat(fd, &statbuf) != 0) {
		perror("fstat failed");
		exit(1);
	}
	len = statbuf.st_size;

	whence = starts_with_hole(fd);
	while ((off = lseek(fd, off, whence)) != -1) {
		seg_t	*s;

		seg = umem_alloc(sizeof (seg_t), UMEM_DEFAULT);
		seg->seg_type = whence;
		seg->seg_offset = off;

		list_insert_tail(&seg_list, seg);
		if ((s = list_prev(&seg_list, seg)) != NULL)
			s->seg_len = seg->seg_offset - s->seg_offset;

		whence = whence == SEEK_HOLE ? SEEK_DATA : SEEK_HOLE;
	}
	if (errno != ENXIO) {
		perror("lseek failed");
		exit(1);
	}
	(void) close(fd);

	/*
	 * If this file ends with a hole block, then populate the length of
	 * the last segment, otherwise this is the end of the file, so
	 * discard the remaining zero length segment.
	 */
	if (seg && seg->seg_offset != len) {
		seg->seg_len = len - seg->seg_offset;
	} else {
		(void) list_remove_tail(&seg_list);
	}

	print_list(&seg_list, fname, options);
	list_destroy(&seg_list);
	return (0);
}
