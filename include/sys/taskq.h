/*****************************************************************************\
 *  Copyright (C) 2007-2010 Lawrence Livermore National Security, LLC.
 *  Copyright (C) 2007 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Brian Behlendorf <behlendorf1@llnl.gov>.
 *  UCRL-CODE-235197
 *
 *  This file is part of the SPL, Solaris Porting Layer.
 *  For details, see <http://github.com/behlendorf/spl/>.
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
\*****************************************************************************/

#ifndef _SPL_TASKQ_H
#define _SPL_TASKQ_H

#include <linux/module.h>
#include <linux/gfp.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/kthread.h>
#include <sys/types.h>
#include <sys/thread.h>

#define TASKQ_NAMELEN           31

#define TASKQ_PREPOPULATE       0x00000001
#define TASKQ_CPR_SAFE          0x00000002
#define TASKQ_DYNAMIC           0x00000004
#define TASKQ_THREADS_CPU_PCT   0x00000008
#define TASKQ_DC_BATCH          0x00000010

typedef unsigned long taskqid_t;
typedef void (task_func_t)(void *);

typedef struct taskq_ent {
        spinlock_t              tqent_lock;
        struct list_head        tqent_list;
        taskqid_t               tqent_id;
        task_func_t             *tqent_func;
        void                    *tqent_arg;
        uintptr_t               tqent_flags;
} taskq_ent_t;

#define TQENT_FLAG_PREALLOC     0x1

/*
 * Flags for taskq_dispatch. TQ_SLEEP/TQ_NOSLEEP should be same as
 * KM_SLEEP/KM_NOSLEEP.  TQ_NOQUEUE/TQ_NOALLOC are set particularly
 * large so as not to conflict with already used GFP_* defines.
 */
#define TQ_SLEEP                KM_SLEEP
#define TQ_NOSLEEP              KM_NOSLEEP
#define TQ_PUSHPAGE             KM_PUSHPAGE
#define TQ_NOQUEUE              0x01000000
#define TQ_NOALLOC              0x02000000
#define TQ_NEW                  0x04000000
#define TQ_FRONT                0x08000000
#define TQ_ACTIVE               0x80000000

typedef struct taskq {
        spinlock_t              tq_lock;       /* protects taskq_t */
        unsigned long           tq_lock_flags; /* interrupt state */
	const char              *tq_name;      /* taskq name */
        struct list_head        tq_thread_list;/* list of all threads */
	struct list_head        tq_active_list;/* list of active threads */
        int                     tq_nactive;    /* # of active threads */
        int                     tq_nthreads;   /* # of total threads */
	int                     tq_pri;        /* priority */
        int                     tq_minalloc;   /* min task_t pool size */
        int                     tq_maxalloc;   /* max task_t pool size */
	int                     tq_nalloc;     /* cur task_t pool size */
        uint_t                  tq_flags;      /* flags */
	taskqid_t               tq_next_id;    /* next pend/work id */
	taskqid_t               tq_lowest_id;  /* lowest pend/work id */
	struct list_head        tq_free_list;  /* free task_t's */
	struct list_head        tq_pend_list;  /* pending task_t's */
	struct list_head        tq_prio_list;  /* priority pending task_t's */
	wait_queue_head_t       tq_work_waitq; /* new work waitq */
	wait_queue_head_t       tq_wait_waitq; /* wait waitq */
} taskq_t;

typedef struct taskq_thread {
	struct list_head       tqt_thread_list;
	struct list_head       tqt_active_list;
	struct task_struct     *tqt_thread;
	taskq_t                *tqt_tq;
	taskqid_t              tqt_id;
        uintptr_t              tqt_flags;
} taskq_thread_t;

/* Global system-wide dynamic task queue available for all consumers */
extern taskq_t *system_taskq;

extern taskqid_t __taskq_dispatch(taskq_t *, task_func_t, void *, uint_t);
extern void __taskq_dispatch_ent(taskq_t *, task_func_t, void *, uint_t, taskq_ent_t *);
extern int __taskq_empty_ent(taskq_ent_t *);
extern void __taskq_init_ent(taskq_ent_t *);
extern taskq_t *__taskq_create(const char *, int, pri_t, int, int, uint_t);
extern void __taskq_destroy(taskq_t *);
extern void __taskq_wait_id(taskq_t *, taskqid_t);
extern void __taskq_wait(taskq_t *);
extern int __taskq_member(taskq_t *, void *);

int spl_taskq_init(void);
void spl_taskq_fini(void);

#define taskq_member(tq, t)                __taskq_member(tq, t)
#define taskq_wait_id(tq, id)              __taskq_wait_id(tq, id)
#define taskq_wait(tq)                     __taskq_wait(tq)
#define taskq_dispatch(tq, f, p, fl)       __taskq_dispatch(tq, f, p, fl)
#define taskq_dispatch_ent(tq, f, p, fl, t) __taskq_dispatch_ent(tq, f, p, fl, t)
#define taskq_empty_ent(t)                 __taskq_empty_ent(t)
#define taskq_init_ent(t)                  __taskq_init_ent(t)
#define taskq_create(n, th, p, mi, ma, fl) __taskq_create(n, th, p, mi, ma, fl)
#define taskq_create_proc(n, th, p, mi, ma, pr, fl)	\
	__taskq_create(n, th, p, mi, ma, fl)
#define taskq_create_sysdc(n, th, mi, ma, pr, dc, fl)	\
	__taskq_create(n, th, maxclsyspri, mi, ma, fl)
#define taskq_destroy(tq)                  __taskq_destroy(tq)

#endif  /* _SPL_TASKQ_H */
