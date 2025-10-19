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

#define	_SYS_VFS_H
#define	_SYS_SUNDDI_H

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

#include <sys/zfs_context_os.h>

/*
 * Stack
 */

#define	noinline	__attribute__((noinline))
#define	likely(x)	__builtin_expect((x), 1)
#define	unlikely(x)	__builtin_expect((x), 0)

/*
 * DTrace SDT probes have different signatures in userland than they do in
 * the kernel.  If they're being used in kernel code, re-define them out of
 * existence for their counterparts in libzpool.
 *
 * Here's an example of how to use the set-error probes in userland:
 * zfs$target:::set-error /arg0 == EBUSY/ {stack();}
 *
 * Here's an example of how to use DTRACE_PROBE probes in userland:
 * If there is a probe declared as follows:
 * DTRACE_PROBE2(zfs__probe_name, uint64_t, blkid, dnode_t *, dn);
 * Then you can use it as follows:
 * zfs$target:::probe2 /copyinstr(arg0) == "zfs__probe_name"/
 *     {printf("%u %p\n", arg1, arg2);}
 */

#ifdef DTRACE_PROBE
#undef	DTRACE_PROBE
#endif	/* DTRACE_PROBE */
#define	DTRACE_PROBE(a)

#ifdef DTRACE_PROBE1
#undef	DTRACE_PROBE1
#endif	/* DTRACE_PROBE1 */
#define	DTRACE_PROBE1(a, b, c)

#ifdef DTRACE_PROBE2
#undef	DTRACE_PROBE2
#endif	/* DTRACE_PROBE2 */
#define	DTRACE_PROBE2(a, b, c, d, e)

#ifdef DTRACE_PROBE3
#undef	DTRACE_PROBE3
#endif	/* DTRACE_PROBE3 */
#define	DTRACE_PROBE3(a, b, c, d, e, f, g)

#ifdef DTRACE_PROBE4
#undef	DTRACE_PROBE4
#endif	/* DTRACE_PROBE4 */
#define	DTRACE_PROBE4(a, b, c, d, e, f, g, h, i)

#ifdef __FreeBSD__
typedef off_t loff_t;
#endif

/*
 * Random stuff
 */
#define	max_ncpus	64
#define	boot_ncpus	(sysconf(_SC_NPROCESSORS_ONLN))

/*
 * Process priorities as defined by setpriority(2) and getpriority(2).
 */
#define	minclsyspri	19
#define	defclsyspri	0
/* Write issue taskq priority. */
#define	wtqclsyspri	-19
#define	maxclsyspri	-20

#define	CPU_SEQID	((uintptr_t)pthread_self() & (max_ncpus - 1))
#define	CPU_SEQID_UNSTABLE	CPU_SEQID

#define	NN_DIVISOR_1000	(1U << 0)
#define	NN_NUMBUF_SZ	(6)

extern int highbit64(uint64_t i);
extern int lowbit64(uint64_t i);

/*
 * Hostname information
 */
extern int ddi_strtoull(const char *str, char **nptr, int base,
    u_longlong_t *result);

/* ZFS Boot Related stuff. */

struct _buf {
	intptr_t	_fd;
};

struct bootstat {
	uint64_t st_size;
};

extern int zfs_secpolicy_snapshot_perms(const char *name, cred_t *cr);
extern int zfs_secpolicy_rename_perms(const char *from, const char *to,
    cred_t *cr);
extern int zfs_secpolicy_destroy_perms(const char *name, cred_t *cr);
extern int secpolicy_zfs(const cred_t *cr);

#define	DDI_SLEEP	KM_SLEEP
#define	ddi_log_sysevent(_a, _b, _c, _d, _e, _f, _g) \
	sysevent_post_event(_c, _d, _b, "libzpool", _e, _f)

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
