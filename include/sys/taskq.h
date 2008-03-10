#ifndef _SPL_TASKQ_H
#define _SPL_TASKQ_H

#ifdef  __cplusplus
extern "C" {
#endif

/*
 * Task Queues - As of linux 2.6.x task queues have been replaced by a
 * similar construct called work queues.  The big difference on the linux
 * side is that functions called from work queues run in process context
 * and not interrupt context.
 *
 * One nice feature of Solaris which does not exist in linux work
 * queues in the notion of a dynamic work queue.  Rather than implementing
 * this in the shim layer I'm hardcoding one-thread per work queue.
 *
 * XXX - This may end up being a significant performance penalty which
 * forces us to implement dynamic workqueues.  Which is all very doable
 * with a little effort.
 */
#include <linux/module.h>
#include <linux/workqueue.h>
#include <linux/gfp.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <sys/types.h>

#undef DEBUG_TASKQ_UNIMPLEMENTED

#define TASKQ_NAMELEN   31
#define taskq_t                         workq_t

typedef struct workqueue_struct workq_t;
typedef unsigned long taskqid_t;
typedef void (*task_func_t)(void *);

/*
 * Public flags for taskq_create(): bit range 0-15
 */
#define TASKQ_PREPOPULATE       0x0000  /* XXX - Workqueues fully populate */
#define TASKQ_CPR_SAFE          0x0000  /* XXX - No analog */
#define TASKQ_DYNAMIC           0x0000  /* XXX - Worksqueues not dynamic */

/*
 * Flags for taskq_dispatch. TQ_SLEEP/TQ_NOSLEEP should be same as
 * KM_SLEEP/KM_NOSLEEP.
 */
#define TQ_SLEEP                0x00    /* XXX - Workqueues don't support    */
#define TQ_NOSLEEP              0x00    /*       these sorts of flags.  They */
#define TQ_NOQUEUE              0x00    /*       always run in application   */
#define TQ_NOALLOC              0x00    /*       context and can sleep.      */


#ifdef DEBUG_TASKQ_UNIMPLEMENTED
static __inline__ void taskq_init(void) {
#error "taskq_init() not implemented"
}

static __inline__ taskq_t *
taskq_create_instance(const char *, int, int, pri_t, int, int, uint_t) {
#error "taskq_create_instance() not implemented"
}

extern void     nulltask(void *);
extern void     taskq_suspend(taskq_t *);
extern int      taskq_suspended(taskq_t *);
extern void     taskq_resume(taskq_t *);

#endif /* DEBUG_TASKQ_UNIMPLEMENTED */

extern taskqid_t __taskq_dispatch(taskq_t *, task_func_t, void *, uint_t);
extern taskq_t *__taskq_create(const char *, int, pri_t, int, int, uint_t);

#define taskq_create(name, thr, pri, min, max, flags) \
	__taskq_create(name, thr, pri, min, max, flags)
#define taskq_dispatch(tq, func, priv, flags)         \
	__taskq_dispatch(tq, func, priv, flags)
#define taskq_destroy(tq)                             destroy_workqueue(tq)
#define taskq_wait(tq)                                flush_workqueue(tq)
#define taskq_member(tq, kthr)                        1 /* XXX -Just be true */

#ifdef  __cplusplus
}
#endif

#endif  /* _SPL_TASKQ_H */
