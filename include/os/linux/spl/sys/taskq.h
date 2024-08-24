/*
 *  Copyright (C) 2007-2010 Lawrence Livermore National Security, LLC.
 *  Copyright (C) 2007 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Brian Behlendorf <behlendorf1@llnl.gov>.
 *  UCRL-CODE-235197
 *
 *  This file is part of the SPL, Solaris Porting Layer.
 *
 *  The SPL is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the
 *  Free Software Foundation; either version 2 of the License, or (at your
 *  option) any later version.
 *
 *  The SPL is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with the SPL.  If not, see <http://www.gnu.org/licenses/>.
 */
/*
 * Copyright (c) 2024, Klara Inc.
 * Copyright (c) 2024, Syneto
 */

#ifndef _SPL_TASKQ_H
#define	_SPL_TASKQ_H

#include <linux/module.h>
#include <linux/gfp.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/kthread.h>
#include <sys/types.h>
#include <sys/thread.h>
#include <sys/rwlock.h>
#include <sys/wait.h>
#include <sys/wmsum.h>

typedef struct kstat_s kstat_t;

#define	TASKQ_NAMELEN		31

#define	TASKQ_PREPOPULATE	0x00000001
#define	TASKQ_CPR_SAFE		0x00000002
#define	TASKQ_DYNAMIC		0x00000004
#define	TASKQ_THREADS_CPU_PCT	0x00000008
#define	TASKQ_DC_BATCH		0x00000010
#define	TASKQ_ACTIVE		0x80000000

/*
 * Flags for taskq_dispatch. TQ_SLEEP/TQ_NOSLEEP should be same as
 * KM_SLEEP/KM_NOSLEEP.  TQ_NOQUEUE/TQ_NOALLOC are set particularly
 * large so as not to conflict with already used GFP_* defines.
 */
#define	TQ_SLEEP		0x00000000
#define	TQ_NOSLEEP		0x00000001
#define	TQ_PUSHPAGE		0x00000002
#define	TQ_NOQUEUE		0x01000000
#define	TQ_NOALLOC		0x02000000
#define	TQ_NEW			0x04000000
#define	TQ_FRONT		0x08000000

/*
 * Reserved taskqid values.
 */
#define	TASKQID_INVALID		((taskqid_t)0)
#define	TASKQID_INITIAL		((taskqid_t)1)

/*
 * spin_lock(lock) and spin_lock_nested(lock,0) are equivalent,
 * so TQ_LOCK_DYNAMIC must not evaluate to 0
 */
typedef enum tq_lock_role {
	TQ_LOCK_GENERAL =	0,
	TQ_LOCK_DYNAMIC =	1,
} tq_lock_role_t;

typedef unsigned long taskqid_t;
typedef void (task_func_t)(void *);

typedef struct taskq_sums {
	/* gauges (inc/dec counters, current value) */
	wmsum_t tqs_threads_active;		/* threads running a task */
	wmsum_t tqs_threads_idle;		/* threads waiting for work */
	wmsum_t tqs_threads_total;		/* total threads */
	wmsum_t tqs_tasks_pending;		/* tasks waiting to execute */
	wmsum_t tqs_tasks_priority;		/* hi-pri tasks waiting */
	wmsum_t tqs_tasks_total;		/* total waiting tasks */
	wmsum_t tqs_tasks_delayed;		/* tasks deferred to future */
	wmsum_t tqs_entries_free;		/* task entries on free list */

	/* counters (inc only, since taskq creation) */
	wmsum_t tqs_threads_created;		/* threads created */
	wmsum_t tqs_threads_destroyed;		/* threads destroyed */
	wmsum_t tqs_tasks_dispatched;		/* tasks dispatched */
	wmsum_t tqs_tasks_dispatched_delayed;	/* tasks delayed to future */
	wmsum_t tqs_tasks_executed_normal;	/* normal pri tasks executed */
	wmsum_t tqs_tasks_executed_priority;	/* high pri tasks executed */
	wmsum_t tqs_tasks_executed;		/* total tasks executed */
	wmsum_t tqs_tasks_delayed_requeued;	/* delayed tasks requeued */
	wmsum_t tqs_tasks_cancelled;		/* tasks cancelled before run */
	wmsum_t tqs_thread_wakeups;		/* total thread wakeups */
	wmsum_t tqs_thread_wakeups_nowork;	/* thread woken but no tasks */
	wmsum_t tqs_thread_sleeps;		/* total thread sleeps */
} taskq_sums_t;

typedef struct taskq {
	spinlock_t		tq_lock;	/* protects taskq_t */
	char			*tq_name;	/* taskq name */
	int			tq_instance;	/* instance of tq_name */
	struct list_head	tq_thread_list;	/* list of all threads */
	struct list_head	tq_active_list;	/* list of active threads */
	int			tq_nactive;	/* # of active threads */
	int			tq_nthreads;	/* # of existing threads */
	int			tq_nspawn;	/* # of threads being spawned */
	int			tq_maxthreads;	/* # of threads maximum */
	/* If PERCPU flag is set, percent of NCPUs to have as threads */
	int			tq_cpu_pct;
	int			tq_pri;		/* priority */
	int			tq_minalloc;	/* min taskq_ent_t pool size */
	int			tq_maxalloc;	/* max taskq_ent_t pool size */
	int			tq_nalloc;	/* cur taskq_ent_t pool size */
	uint_t			tq_flags;	/* flags */
	taskqid_t		tq_next_id;	/* next pend/work id */
	taskqid_t		tq_lowest_id;	/* lowest pend/work id */
	struct list_head	tq_free_list;	/* free taskq_ent_t's */
	struct list_head	tq_pend_list;	/* pending taskq_ent_t's */
	struct list_head	tq_prio_list;	/* priority taskq_ent_t's */
	struct list_head	tq_delay_list;	/* delayed taskq_ent_t's */
	struct list_head	tq_taskqs;	/* all taskq_t's */
	wait_queue_head_t	tq_work_waitq;	/* new work waitq */
	wait_queue_head_t	tq_wait_waitq;	/* wait waitq */
	tq_lock_role_t		tq_lock_class;	/* class when taking tq_lock */
	/* list node for the cpu hotplug callback */
	struct hlist_node	tq_hp_cb_node;
	boolean_t		tq_hp_support;
	unsigned long		lastspawnstop;	/* when to purge dynamic */
	taskq_sums_t		tq_sums;
	kstat_t			*tq_ksp;
} taskq_t;

typedef struct taskq_ent {
	spinlock_t		tqent_lock;
	wait_queue_head_t	tqent_waitq;
	struct timer_list	tqent_timer;
	struct list_head	tqent_list;
	taskqid_t		tqent_id;
	task_func_t		*tqent_func;
	void			*tqent_arg;
	taskq_t			*tqent_taskq;
	uintptr_t		tqent_flags;
	unsigned long		tqent_birth;
} taskq_ent_t;

#define	TQENT_FLAG_PREALLOC	0x1
#define	TQENT_FLAG_CANCEL	0x2

/* bits 2-3 are which list tqent is on */
#define	TQENT_LIST_NONE		0x0
#define	TQENT_LIST_PENDING	0x4
#define	TQENT_LIST_PRIORITY	0x8
#define	TQENT_LIST_DELAY	0xc
#define	TQENT_LIST_MASK		0xc

typedef struct taskq_thread {
	struct list_head	tqt_thread_list;
	struct list_head	tqt_active_list;
	struct task_struct	*tqt_thread;
	taskq_t			*tqt_tq;
	taskqid_t		tqt_id;
	taskq_ent_t		*tqt_task;
	uintptr_t		tqt_flags;
} taskq_thread_t;

/* Global system-wide dynamic task queue available for all consumers */
extern taskq_t *system_taskq;
/* Global dynamic task queue for long delay */
extern taskq_t *system_delay_taskq;

/* List of all taskqs */
extern struct list_head tq_list;
extern struct rw_semaphore tq_list_sem;

extern taskqid_t taskq_dispatch(taskq_t *, task_func_t, void *, uint_t);
extern taskqid_t taskq_dispatch_delay(taskq_t *, task_func_t, void *,
    uint_t, clock_t);
extern void taskq_dispatch_ent(taskq_t *, task_func_t, void *, uint_t,
    taskq_ent_t *);
extern int taskq_empty_ent(taskq_ent_t *);
extern void taskq_init_ent(taskq_ent_t *);
extern taskq_t *taskq_create(const char *, int, pri_t, int, int, uint_t);
extern taskq_t *taskq_create_synced(const char *, int, pri_t, int, int, uint_t,
    kthread_t ***);
extern void taskq_destroy(taskq_t *);
extern void taskq_wait_id(taskq_t *, taskqid_t);
extern void taskq_wait_outstanding(taskq_t *, taskqid_t);
extern void taskq_wait(taskq_t *);
extern int taskq_cancel_id(taskq_t *, taskqid_t);
extern int taskq_member(taskq_t *, kthread_t *);
extern taskq_t *taskq_of_curthread(void);

#define	taskq_create_proc(name, nthreads, pri, min, max, proc, flags) \
    taskq_create(name, nthreads, pri, min, max, flags)
#define	taskq_create_sysdc(name, nthreads, min, max, proc, dc, flags) \
	((void) sizeof (dc), \
	    taskq_create(name, nthreads, maxclsyspri, min, max, flags))

int spl_taskq_init(void);
void spl_taskq_fini(void);

#endif  /* _SPL_TASKQ_H */
