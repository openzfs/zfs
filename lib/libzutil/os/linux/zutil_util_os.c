
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
 * Copyright (c) 2005, 2010, Oracle and/or its affiliates. All rights reserved.
 * Copyright (c) 2018, Joyent, Inc. All rights reserved.
 * Copyright (c) 2011, 2018 by Delphix. All rights reserved.
 * Copyright 2016 Igor Kozhukhov <ikozhukhov@gmail.com>
 * Copyright (c) 2017 Datto Inc.
 */

/*
 * Internal utility routines for the ZFS library.
 */

#include <errno.h>
#include <fcntl.h>
#include <libintl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <unistd.h>
#include <math.h>
#include <sys/stat.h>
#include <sys/mnttab.h>
#include <sys/mntent.h>
#include <sys/types.h>
#include <sys/wait.h>

#include <libzfs.h>
#include <libzfs_core.h>

#include "libzfs_impl.h"
#include "zfs_prop.h"
#include "zfeature_common.h"
#include <zfs_fletcher.h>
#include <libzutil.h>

int
zutil_module_loaded(const char *module)
{
	const char path_prefix[] = "/sys/module/";
	char path[256];

	memcpy(path, path_prefix, sizeof (path_prefix) - 1);
	strcpy(path + sizeof (path_prefix) - 1, module);

	return (access(path, F_OK) == 0);
}

int
zutil_load_module(const char *module)
{
	char *argv[4] = {"/sbin/modprobe", "-q", (char *)module, (char *)0};

	if (libzfs_run_process("/sbin/modprobe", argv, 0))
				return (ENOEXEC);
	return (0);
}
