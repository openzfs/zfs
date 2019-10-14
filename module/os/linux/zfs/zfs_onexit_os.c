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

/*
 * Consumers might need to operate by minor number instead of fd, since
 * they might be running in another thread (e.g. txg_sync_thread). Callers
 * of this function must call zfs_onexit_fd_rele() when they're finished
 * using the minor number.
 */
int
zfs_onexit_fd_hold(int fd, minor_t *minorp)
{
	zfs_onexit_t *zo = NULL;
	int error;

	error = zfsdev_getminor(fd, minorp);
	if (error) {
		zfs_onexit_fd_rele(fd);
		return (error);
	}

	zo = zfsdev_get_state(*minorp, ZST_ONEXIT);
	if (zo == NULL) {
		zfs_onexit_fd_rele(fd);
		return (SET_ERROR(EBADF));
	}
	return (0);
}

void
zfs_onexit_fd_rele(int fd)
{
	releasef(fd);
}
