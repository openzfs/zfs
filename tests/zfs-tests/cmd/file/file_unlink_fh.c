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
 * Copyright 2026, tiehexue <tiehexue@hotmail.com>. All rights reserved.
 */

/*
 * Verify that a filehandle-based reopen of an open-unlinked file
 * succeeds. On Linux this exercises zfs_vget() via fh_to_dentry;
 * on FreeBSD it exercises zfs_fhtovp() via VFS_FHTOVP.
 *
 * Exit 0 on success, non-zero on failure.
 *
 * Most code copied from https://reviews.freebsd.org/D57982
 * and https://github.com/openzfs/zfs/issues/18699
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>

#ifdef __FreeBSD__
#include <sys/param.h>
#include <sys/mount.h>
#endif

int
main(int argc, char *argv[])
{
	const char *filename;
	int file_fd = -1, handle_fd = -1;
	int ret = 2;

	if (argc != 2) {
		(void) fprintf(stderr, "usage: %s <filename>\n", argv[0]);
		return (2);
	}
	filename = argv[1];

	file_fd = open(filename, O_CREAT | O_RDWR | O_TRUNC, 0644);
	if (file_fd < 0) {
		perror("open");
		goto out;
	}

#ifdef __linux__
	struct file_handle *fhp;
	int mount_fd, mnt_id;

	mount_fd = open(".", O_RDONLY | O_DIRECTORY);
	if (mount_fd < 0) {
		perror("open mount_fd");
		goto out;
	}

	fhp = malloc(sizeof (struct file_handle) + 128);
	if (fhp == NULL) {
		perror("malloc");
		(void) close(mount_fd);
		goto out;
	}
	fhp->handle_bytes = 128;

	/* Obtain filehandle while the file still has a name */
	if (name_to_handle_at(AT_FDCWD, filename, fhp,
	    &mnt_id, 0) < 0) {
		perror("name_to_handle_at");
		free(fhp);
		(void) close(mount_fd);
		goto out;
	}

	/* Unlink — file_fd keeps the inode alive */
	if (unlink(filename) < 0) {
		perror("unlink");
		free(fhp);
		(void) close(mount_fd);
		goto out;
	}

	/* Reopen via filehandle — exercises fh_to_dentry/zfs_vget */
	handle_fd = open_by_handle_at(mount_fd, fhp, O_RDONLY);
	free(fhp);
	(void) close(mount_fd);
#elif defined(__FreeBSD__)
	fhandle_t fh;

	/* Obtain ZFS filehandle while the file still has a name */
	if (getfh(filename, &fh) < 0) {
		perror("getfh");
		goto out;
	}

	/* Unlink — file_fd keeps the vnode alive */
	if (unlink(filename) < 0) {
		perror("unlink");
		goto out;
	}

	/* Reopen via filehandle — exercises VFS_FHTOVP/zfs_fhtovp */
	handle_fd = fhopen(&fh, O_RDONLY);
#endif

	if (handle_fd < 0) {
		(void) fprintf(stderr, "FAIL: filehandle reopen: %s\n",
		    strerror(errno));
		ret = 1;
	} else {
		(void) printf("filehandle reopen succeeded, fd=%d\n",
		    handle_fd);
		ret = 0;
	}

out:
	if (handle_fd >= 0)
		(void) close(handle_fd);
	if (file_fd >= 0)
		(void) close(file_fd);
	return (ret);
}
