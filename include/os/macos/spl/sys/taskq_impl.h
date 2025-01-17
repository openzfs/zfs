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
 * Copyright 2010 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */
/*
 * Copyright 2011 Nexenta Systems, Inc.  All rights reserved.
 */
/*
 * Copyright (C) 2015 Jorgen Lundman <lundman@lundman.net>
 */


#ifndef	_SYS_TASKQ_IMPL_H
#define	_SYS_TASKQ_IMPL_H

#include <sys/taskq.h>
#include <sys/inttypes.h>
#include <sys/vmem.h>
#include <sys/list.h>
#include <sys/kstat.h>

#ifdef	__cplusplus
extern "C" {
#endif

typedef struct taskq_bucket taskq_bucket_t;

typedef struct taskq_ent {
	struct taskq_ent	*tqent_next;
	struct taskq_ent	*tqent_prev;
	task_func_t		*tqent_func;
	void			*tqent_arg;
	union {
		taskq_bucket_t	*tqent_bucket;
		uintptr_t	tqent_flags;
	}			tqent_un;
	kthread_t		*tqent_thread;
	kcondvar_t		tqent_cv;
#ifdef __APPLE__
	/* Used to simulate TS_STOPPED */
	kmutex_t		tqent_thread_lock;
	kcondvar_t		tqent_thread_cv;
#endif
} taskq_ent_t;

#define	TQENT_FLAG_PREALLOC	0x1

/*
 * Taskq Statistics fields are not protected by any locks.
 */
typedef struct tqstat {
	uint_t		tqs_hits;
	uint_t		tqs_misses;
	uint_t		tqs_overflow;	/* no threads to allocate   */
	uint_t		tqs_tcreates;	/* threads created 	*/
	uint_t		tqs_tdeaths;	/* threads died		*/
	uint_t		tqs_maxthreads;	/* max # of alive threads */
	uint_t		tqs_nomem;	/* # of times there were no memory */
	uint_t		tqs_disptcreates;
} tqstat_t;

/*
 * Per-CPU hash bucket manages taskq_bent_t structures using freelist.
 */
struct taskq_bucket {
	kmutex_t	tqbucket_lock;
	taskq_t		*tqbucket_taskq;	/* Enclosing taskq */
	taskq_ent_t	tqbucket_freelist;
	uint_t		tqbucket_nalloc;	/* # of allocated entries */
	uint_t		tqbucket_nfree;		/* # of free entries */
	kcondvar_t	tqbucket_cv;
	ushort_t	tqbucket_flags;
	hrtime_t	tqbucket_totaltime;
	tqstat_t	tqbucket_stat;
};

/*
 * Bucket flags.
 */
#define	TQBUCKET_CLOSE		0x01
#define	TQBUCKET_SUSPEND	0x02

#define	TASKQ_INTERFACE_FLAGS	0x0000ffff	/* defined in <sys/taskq.h> */

/*
 * taskq implementation flags: bit range 16-31
 */
#define	TASKQ_CHANGING		0x00010000	/* nthreads != target */
#define	TASKQ_SUSPENDED		0x00020000	/* taskq is suspended */
#define	TASKQ_NOINSTANCE	0x00040000	/* no instance number */
#define	TASKQ_THREAD_CREATED	0x00080000	/* a thread has been created */
#define	TASKQ_DUTY_CYCLE	0x00100000	/* using the SDC class */

struct taskq {
	char		tq_name[TASKQ_NAMELEN + 1];
	kmutex_t	tq_lock;
	krwlock_t	tq_threadlock;
	kcondvar_t	tq_dispatch_cv;
	kcondvar_t	tq_wait_cv;
	kcondvar_t	tq_exit_cv;
	pri_t		tq_pri;		/* Scheduling priority */
	uint_t		tq_flags;
	int		tq_active;
	int		tq_nthreads;
	int		tq_nthreads_target;
	int		tq_nthreads_max;
	int		tq_threads_ncpus_pct;
	int		tq_nalloc;
	int		tq_minalloc;
	int		tq_maxalloc;
	kcondvar_t	tq_maxalloc_cv;
	int		tq_maxalloc_wait;
	taskq_ent_t	*tq_freelist;
	taskq_ent_t	tq_task;
	int		tq_maxsize;
	taskq_bucket_t	*tq_buckets;	/* Per-cpu array of buckets */
	int		tq_instance;
	uint_t		tq_nbuckets;	/* # of buckets	(2^n)	    */
	union {
		kthread_t *_tq_thread;
		kthread_t **_tq_threadlist;
	}		tq_thr;

	list_node_t	tq_cpupct_link;	/* linkage for taskq_cpupct_list */
	proc_t		*tq_proc;	/* process for taskq threads */
	int		tq_cpupart;	/* cpupart id bound to */
	uint_t		tq_DC;		/* duty cycle for SDC */

	/*
	 * Statistics.
	 */
	kstat_t		*tq_kstat;	/* Exported statistics */
	hrtime_t	tq_totaltime;	/* Time spent processing tasks */
	uint64_t	tq_tasks;	/* Total # of tasks posted */
	uint64_t	tq_executed;	/* Total # of tasks executed */
	int		tq_maxtasks;	/* Max number of tasks in the queue */
	int		tq_tcreates;
	int		tq_tdeaths;
};

/* Special form of taskq dispatch that uses preallocated entries. */
void taskq_dispatch_ent(taskq_t *, task_func_t, void *, uint_t, taskq_ent_t *);


#define	tq_thread tq_thr._tq_thread
#define	tq_threadlist tq_thr._tq_threadlist

/* The MAX guarantees we have at least one thread */
#define	TASKQ_THREADS_PCT(ncpus, pct)	MAX(((ncpus) * (pct)) / 100, 1)

/* Extra ZOL / Apple */
extern void taskq_init_ent(taskq_ent_t *t);
extern taskqid_t taskq_dispatch_delay(taskq_t *tq, task_func_t func, void *arg,
    uint_t flags, clock_t expire_time);


#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_TASKQ_IMPL_H */
