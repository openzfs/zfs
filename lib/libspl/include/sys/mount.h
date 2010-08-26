/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License, Version 1.0 only
 * (the "License").  You may not use this file except in compliance
 * with the License.
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
 * Copyright 2006 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#include_next <sys/mount.h>

#ifndef _LIBSPL_SYS_MOUNT_H
#define _LIBSPL_SYS_MOUNT_H

#include <sys/mntent.h>
#include <assert.h>
#include <string.h>
#include <stdlib.h>

/*
 * Some old glibc headers don't define BLKGETSIZE64
 * and we don't want to require the kernel headers
 */
#if !defined(BLKGETSIZE64)
#define BLKGETSIZE64		_IOR(0x12, 114, size_t)
#endif

#define MS_FORCE		MNT_FORCE
#define MS_OVERLAY		32768
#define MS_NOMNTTAB		0         /* Not supported in Linux */
#define MS_OPTIONSTR		0         /* Not necessary in Linux */

#endif /* _LIBSPL_SYS_MOUNT_H */
