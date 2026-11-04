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
 * Open an object on the filesystem containing <path> by a hand-built short
 * ZFS file handle, as an NFS server would.  Exits 0 if the open succeeds,
 * or with the errno of the failed open_by_handle_at(2).
 *
 * usage: open_by_fid <path> <object> <gen>
 */

#ifndef _GNU_SOURCE
#define	_GNU_SOURCE
#endif
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Matches zfid_short_t */
#define	SHORT_FID_LEN	10

int
main(int argc, char **argv)
{
	struct file_handle *fh;
	uint64_t object, gen;
	int mfd, fd;

	if (argc != 4) {
		(void) fprintf(stderr, "usage: %s <path> <object> <gen>\n",
		    argv[0]);
		return (EINVAL);
	}

	object = strtoull(argv[2], NULL, 0);
	gen = strtoull(argv[3], NULL, 0);

	mfd = open(argv[1], O_RDONLY);
	if (mfd < 0) {
		perror(argv[1]);
		return (errno);
	}

	/* fid_t: 2 byte length followed by the zfid_short_t payload */
	fh = calloc(1, sizeof (*fh) + 12);
	if (fh == NULL)
		return (ENOMEM);
	fh->handle_bytes = 12;
	fh->handle_type = 1;	/* FILEID_INO32_GEN */
	fh->f_handle[0] = SHORT_FID_LEN;
	for (int i = 0; i < 6; i++)
		fh->f_handle[2 + i] = (object >> (8 * i)) & 0xff;
	for (int i = 0; i < 4; i++)
		fh->f_handle[8 + i] = (gen >> (8 * i)) & 0xff;

	fd = open_by_handle_at(mfd, fh, O_RDONLY);
	if (fd < 0) {
		int err = errno;
		(void) printf("open_by_handle_at: %s\n", strerror(err));
		return (err);
	}

	(void) close(fd);
	(void) close(mfd);
	free(fh);
	return (0);
}
