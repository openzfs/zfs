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
 * Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef _SYS_ZFS_CONTEXT_SPL_H
#define _SYS_ZFS_CONTEXT_SPL_H

#ifdef  __cplusplus
extern "C" {
#endif

#define _SYS_MUTEX_H
#define _SYS_RWLOCK_H
#define _SYS_CONDVAR_H
#define _SYS_SYSTM_H
#define _SYS_DEBUG_H
#define _SYS_T_LOCK_H
#define _SYS_VNODE_H
#define _SYS_VFS_H
#define _SYS_SUNDDI_H
#define _SYS_CALLB_H

/* In SPL module */
#include <sys/callb.h>
#include <sys/condvar.h>
#include <sys/cred.h>
#include <sys/sysmacros.h>
#include <sys/kmem.h>
#include <sys/kstat.h>
#include <sys/mutex.h>
#include <sys/random.h>
#include <sys/rwlock.h>
#include <sys/taskq.h>
#include <sys/thread.h>
#include <sys/time.h>
#include <sys/timer.h>
#include <sys/types.h>
#include <sys/vmsystm.h>
#include <sys/atomic.h>
#include <sys/vnode.h>
#include <sys/cmn_err.h>
#include <sys/uio.h>
#include <sys/sysevent/eventdefs.h>
#include <sys/kobj.h>

/* Ensure we do not pick these values up from spl_config.h */
#undef NDEBUG
#undef PACKAGE
#undef PACKAGE_BUGREPORT
#undef PACKAGE_NAME
#undef PACKAGE_STRING
#undef PACKAGE_TARNAME
#undef PACKAGE_VERSION
#undef STDC_HEADERS
#undef VERSION

/* In libport */
#include <sys/u8_textprep.h>

/* In libzcommon */
#include <sys/list.h>
#include <sys/zfs_debug.h>

#endif /* _SYS_ZFS_CONTEXT_SPL_H */
