/*
 * CDDL HEADER START
 *
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this
 * source.  A copy of the CDDL is also available via the Internet at
 * http://www.illumos.org/license/CDDL.
 *
 * CDDL HEADER END
 */
/*
 * Copyright (C) 2017 by Lawrence Livermore National Security, LLC.
 */

#ifndef _SYS_MMP_H
#define	_SYS_MMP_H

#include <sys/spa.h>
#include <sys/zfs_context.h>
#include <sys/uberblock_impl.h>

#ifdef	__cplusplus
extern "C" {
#endif

#define	MMP_DEFAULT_INTERVAL		1000
#define	MMP_DEFAULT_IMPORT_INTERVALS	10
#define	MMP_DEFAULT_FAIL_INTERVALS	5

typedef struct mmp_thread_state {
	kmutex_t	mmp_thread_lock; /* protect thread mgmt fields */
	kcondvar_t	mmp_thread_cv;
	kthread_t	*mmp_thread;
	uint8_t		mmp_thread_exiting;
	kmutex_t	mmp_io_lock;	/* protect below */
	hrtime_t	mmp_last_write;	/* last successful MMP write */
	uint64_t	mmp_delay;	/* decaying avg ns between MMP writes */
	uberblock_t	mmp_ub;		/* last ub written by sync */
	zio_t		*mmp_zio_root;	/* root of mmp write zios */
} mmp_thread_state_t;


extern void mmp_init(struct spa *spa);
extern void mmp_fini(struct spa *spa);
extern void mmp_thread_start(struct spa *spa);
extern void mmp_thread_stop(struct spa *spa);
extern void mmp_update_uberblock(struct spa *spa, struct uberblock *ub);

/* Global tuning */
extern ulong_t zfs_mmp_interval;
extern uint_t zfs_mmp_fail_intervals;
extern uint_t zfs_mmp_import_intervals;

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_MMP_H */
