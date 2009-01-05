/*
 *  This file is part of the SPL: Solaris Porting Layer.
 *
 *  Copyright (c) 2008 Lawrence Livermore National Security, LLC.
 *  Produced at Lawrence Livermore National Laboratory
 *  Written by:
 *          Brian Behlendorf <behlendorf1@llnl.gov>,
 *          Herb Wartens <wartens2@llnl.gov>,
 *          Jim Garlick <garlick@llnl.gov>
 *  UCRL-CODE-235197
 *
 *  This is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA.
 */

#ifndef _SPL_TASKQ_H
#define _SPL_TASKQ_H

#ifdef  __cplusplus
extern "C" {
#endif

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

typedef unsigned long taskqid_t;
typedef void (task_func_t)(void *);

/*
 * Flags for taskq_dispatch. TQ_SLEEP/TQ_NOSLEEP should be same as
 * KM_SLEEP/KM_NOSLEEP.  TQ_NOQUEUE/TQ_NOALLOC are set particularly
 * large so as not to conflict with already used GFP_* defines.
 */
#define TQ_SLEEP                KM_SLEEP
#define TQ_NOSLEEP              KM_NOSLEEP
#define TQ_NOQUEUE              0x01000000
#define TQ_NOALLOC              0x02000000
#define TQ_NEW                  0x04000000
#define TQ_ACTIVE               0x80000000

typedef struct taskq {
        spinlock_t              tq_lock;       /* protects taskq_t */
        unsigned long           tq_lock_flags; /* interrupt state */
        struct task_struct      **tq_threads;  /* thread pointers */
	const char              *tq_name;      /* taskq name */
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
	struct list_head        tq_work_list;  /* work task_t's */
	struct list_head        tq_pend_list;  /* pending task_t's */
	wait_queue_head_t       tq_work_waitq; /* new work waitq */
	wait_queue_head_t       tq_wait_waitq; /* wait waitq */
} taskq_t;

/* Global system-wide dynamic task queue available for all consumers */
extern taskq_t *system_taskq;

extern taskqid_t __taskq_dispatch(taskq_t *, task_func_t, void *, uint_t);
extern taskq_t *__taskq_create(const char *, int, pri_t, int, int, uint_t);
extern void __taskq_destroy(taskq_t *);
extern void __taskq_wait(taskq_t *);
extern int __taskq_member(taskq_t *, void *);

int spl_taskq_init(void);
void spl_taskq_fini(void);

#define taskq_member(tq, t)                __taskq_member(tq, t)
#define taskq_wait_id(tq, id)              __taskq_wait_id(tq, id)
#define taskq_wait(tq)                     __taskq_wait(tq)
#define taskq_dispatch(tq, f, p, fl)       __taskq_dispatch(tq, f, p, fl)
#define taskq_create(n, th, p, mi, ma, fl) __taskq_create(n, th, p, mi, ma, fl)
#define taskq_destroy(tq)                  __taskq_destroy(tq)

#ifdef  __cplusplus
}
#endif

#endif  /* _SPL_TASKQ_H */
