/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */
/*
 * Copyright (c) 2010, Oracle and/or its affiliates. All rights reserved.
 * Copyright (c) 2013 by Delphix. All rights reserved.
 */

#include <sys/types.h>
#include <sys/param.h>
#include <sys/errno.h>
#include <sys/kmem.h>
#include <sys/sunddi.h>
#include <sys/zfs_ioctl.h>
#include <sys/zfs_onexit.h>

static int
zfs_onexit_minor_to_state(minor_t minor, zfs_onexit_t **zo)
{
	*zo = zfsdev_get_state(minor, ZST_ONEXIT);
	if (*zo == NULL)
		return (SET_ERROR(EBADF));

	return (0);
}

int
zfs_onexit_fd_hold(int fd, minor_t *minorp)
{
	file_t *fp, *tmpfp;
	zfs_onexit_t *zo;
	void *data;
	int error;

	if ((error = zfs_file_get(fd, &fp)))
		return (error);

	tmpfp = curthread->td_fpop;
	curthread->td_fpop = fp;
	error = devfs_get_cdevpriv(&data);
	if (error == 0)
		*minorp = (minor_t)(uintptr_t)data;
	curthread->td_fpop = tmpfp;
	if (error != 0)
		return (SET_ERROR(EBADF));
	return (zfs_onexit_minor_to_state(*minorp, &zo));
}

void
zfs_onexit_fd_rele(int fd)
{
	zfs_file_put(fd);
}
