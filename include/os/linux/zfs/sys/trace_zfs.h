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

#ifndef _OS_LINUX_ZFS_TRACE_H
#define	_OS_LINUX_ZFS_TRACE_H

#include <sys/multilist.h>
#include <sys/arc_impl.h>
#include <sys/vdev_impl.h>
#include <sys/zio.h>
#include <sys/dbuf.h>
#include <sys/dmu_objset.h>
#include <sys/dsl_dataset.h>
#include <sys/dmu_tx.h>
#include <sys/dnode.h>
#include <sys/zfs_znode.h>
#include <sys/zil_impl.h>
#include <sys/zrlock.h>

#include <sys/trace.h>
#include <sys/trace_acl.h>
#include <sys/trace_arc.h>
#include <sys/trace_dbgmsg.h>
#include <sys/trace_dbuf.h>
#include <sys/trace_dmu.h>
#include <sys/trace_dnode.h>
#include <sys/trace_multilist.h>
#include <sys/trace_rrwlock.h>
#include <sys/trace_txg.h>
#include <sys/trace_vdev.h>
#include <sys/trace_zil.h>
#include <sys/trace_zio.h>
#include <sys/trace_zrlock.h>

#endif
