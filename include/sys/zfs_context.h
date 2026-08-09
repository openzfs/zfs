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

#include <sys/zfs_context_os.h>

#endif  /* _KERNEL || _STANDALONE */

#ifdef __cplusplus
};
#endif

#endif	/* _SYS_ZFS_CONTEXT_H */
