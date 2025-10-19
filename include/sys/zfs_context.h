// SPDX-License-Identifier: CDDL-1.0
/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or https://opensource.org/licenses/CDDL-1.0.
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
 * Copyright 2011 Nexenta Systems, Inc.  All rights reserved.
 * Copyright (c) 2012, 2018 by Delphix. All rights reserved.
 * Copyright (c) 2012, Joyent, Inc. All rights reserved.
 */

#ifndef _SYS_ZFS_CONTEXT_H
#define	_SYS_ZFS_CONTEXT_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * This code compiles in three different contexts. When __KERNEL__ is defined,
 * the code uses "unix-like" kernel interfaces. When _STANDALONE is defined, the
 * code is running in a reduced capacity environment of the boot loader which is
 * generally a subset of both POSIX and kernel interfaces (with a few unique
 * interfaces too). When neither are defined, it's in a userland POSIX or
 * similar environment.
 */
#if defined(__KERNEL__) || defined(_STANDALONE)
#include <sys/types.h>
#include <sys/atomic.h>
#include <sys/sysmacros.h>
#include <sys/vmsystm.h>
#include <sys/condvar.h>
#include <sys/cmn_err.h>
#include <sys/kmem.h>
#include <sys/kmem_cache.h>
#include <sys/vmem.h>
#include <sys/misc.h>
#include <sys/taskq.h>
#include <sys/param.h>
#include <sys/disp.h>
#include <sys/debug.h>
#include <sys/random.h>
#include <sys/string.h>
#include <sys/byteorder.h>
#include <sys/list.h>
#include <sys/time.h>
#include <sys/zone.h>
#include <sys/kstat.h>
#include <sys/zfs_debug.h>
#include <sys/sysevent.h>
#include <sys/sysevent/eventdefs.h>
#include <sys/zfs_delay.h>
#include <sys/sunddi.h>
#include <sys/ctype.h>
#include <sys/disp.h>
#include <sys/trace.h>
#include <sys/procfs_list.h>
#include <sys/mod.h>
#include <sys/uio_impl.h>
#include <sys/zfs_context_os.h>
#else /* _KERNEL || _STANDALONE */

#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <stdarg.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <setjmp.h>
#include <assert.h>
#include <limits.h>
#include <atomic.h>
#include <dirent.h>
#include <time.h>
#include <ctype.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/cred.h>
#include <sys/sysmacros.h>
#include <sys/resource.h>
#include <sys/byteorder.h>
#include <sys/list.h>
#include <sys/mod.h>
#include <sys/uio.h>
#include <sys/zfs_debug.h>
#include <sys/kstat.h>
#include <sys/u8_textprep.h>
#include <sys/sysevent.h>
#include <sys/sysevent/eventdefs.h>
#include <sys/sunddi.h>
#include <sys/debug.h>
#include <sys/trace_zfs.h>
#include <sys/zone.h>

#include <sys/mutex.h>
#include <sys/rwlock.h>
#include <sys/condvar.h>
#include <sys/cmn_err.h>
#include <sys/thread.h>
#include <sys/taskq.h>
#include <sys/tsd.h>
#include <sys/procfs_list.h>
#include <sys/kmem.h>
#include <sys/zfs_delay.h>
#include <sys/vnode.h>
#include <sys/callb.h>
#include <sys/trace.h>
#include <sys/systm.h>
#include <sys/misc.h>
#include <sys/random.h>
#include <sys/callb.h>
#include <sys/trace.h>

#include <sys/zfs_context_os.h>

/*
 * Stack
 */

#define	noinline	__attribute__((noinline))
#define	likely(x)	__builtin_expect((x), 1)
#define	unlikely(x)	__builtin_expect((x), 0)

#ifdef __FreeBSD__
typedef off_t loff_t;
#endif

/*
 * Kernel modules
 */
#define	__init
#define	__exit

#endif  /* _KERNEL || _STANDALONE */

#ifdef __cplusplus
};
#endif

#endif	/* _SYS_ZFS_CONTEXT_H */
