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
 * Copyright 2015 Nexenta Systems, Inc.  All rights reserved.
 * Copyright (c) 2005, 2010, Oracle and/or its affiliates. All rights reserved.
 * Copyright (c) 2014, 2019 by Delphix. All rights reserved.
 * Copyright 2016 Igor Kozhukhov <ikozhukhov@gmail.com>
 * Copyright 2017 RackTop Systems.
 * Copyright (c) 2018 Datto Inc.
 * Copyright 2018 OmniOS Community Edition (OmniOSce) Association.
 */

#include <dirent.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <libgen.h>
#include <libintl.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <unistd.h>
#include <zone.h>
#include <sys/mntent.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/vfs.h>
#include <sys/dsl_crypt.h>

#include <libzfs.h>

#include "libzfs_impl.h"
#include <thread_pool.h>

/*
 * The filesystem is mounted by invoking the system mount utility rather
 * than by the system call mount(2).  This ensures that the /etc/mtab
 * file is correctly locked for the update.  Performing our own locking
 * and /etc/mtab update requires making an unsafe assumption about how
 * the mount utility performs its locking.  Unfortunately, this also means
 * in the case of a mount failure we do not have the exact errno.  We must
 * make due with return value from the mount process.
 *
 * In the long term a shared library called libmount is under development
 * which provides a common API to address the locking and errno issues.
 * Once the standard mount utility has been updated to use this library
 * we can add an autoconf check to conditionally use it.
 *
 * http://www.kernel.org/pub/linux/utils/util-linux/libmount-docs/index.html
 */
int
do_mount(const char *src, const char *mntpt, char *opts)
{
	char *argv[9] = {
	    "/bin/mount",
	    "--no-canonicalize",
	    "-t", MNTTYPE_ZFS,
	    "-o", opts,
	    (char *)src,
	    (char *)mntpt,
	    (char *)NULL };
	int rc;

	/* Return only the most critical mount error */
	rc = libzfs_run_process(argv[0], argv, STDOUT_VERBOSE|STDERR_VERBOSE);
	if (rc) {
		if (rc & MOUNT_FILEIO)
			return (EIO);
		if (rc & MOUNT_USER)
			return (EINTR);
		if (rc & MOUNT_SOFTWARE)
			return (EPIPE);
		if (rc & MOUNT_BUSY)
			return (EBUSY);
		if (rc & MOUNT_SYSERR)
			return (EAGAIN);
		if (rc & MOUNT_USAGE)
			return (EINVAL);

		return (ENXIO); /* Generic error */
	}

	return (0);
}

int
do_unmount(const char *mntpt, int flags)
{
	char force_opt[] = "-f";
	char lazy_opt[] = "-l";
	char *argv[7] = {
	    "/bin/umount",
	    "-t", MNTTYPE_ZFS,
	    NULL, NULL, NULL, NULL };
	int rc, count = 3;

	if (flags & MS_FORCE) {
		argv[count] = force_opt;
		count++;
	}

	if (flags & MS_DETACH) {
		argv[count] = lazy_opt;
		count++;
	}

	argv[count] = (char *)mntpt;
	rc = libzfs_run_process(argv[0], argv, STDOUT_VERBOSE|STDERR_VERBOSE);

	return (rc ? EINVAL : 0);
}

int
zutil_can_user_mount(void)
{

	return (geteuid() == 0);
}
