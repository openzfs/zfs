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
#include <sys/kmem.h>

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

typedef struct task {
	spinlock_t              t_lock;
	struct list_head        t_list;
	taskqid_t               t_id;
        task_func_t             *t_func;
        void                    *t_arg;
} task_t;

typedef struct taskq {
        spinlock_t              tq_lock;       /* protects taskq_t */
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

extern taskqid_t __taskq_dispatch(taskq_t *, task_func_t, void *, uint_t);
extern taskq_t *__taskq_create(const char *, int, pri_t, int, int, uint_t);
extern void __taskq_destroy(taskq_t *);
extern void __taskq_wait(taskq_t *);
extern int __taskq_member(taskq_t *, void *);

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
