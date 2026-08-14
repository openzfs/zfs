// SPDX-License-Identifier: BSD-2-Clause
/*
 * FreeBSD taskqueue(9) stand-in on top of the SPL taskq.
 *
 * zfs_rmnode enqueues zfs_unlinked_drain so vnode create/remove is
 * not re-entered on the caller's stack.  Repeated enqueues coalesce
 * to one execution.  Drain waits until the task is neither queued
 * nor running.
 */

#ifndef	_OPENSOLARIS_SYS_TASKQUEUE_H_
#define	_OPENSOLARIS_SYS_TASKQUEUE_H_

#ifdef _KERNEL

#include <sys/types.h>
#include <sys/taskq.h>

struct taskqueue;

struct task {
	void		(*ta_func)(void *, int);
	void		*ta_context;
	volatile uint32_t ta_pending;
	volatile uint32_t ta_running;
	volatile uint32_t ta_dispatched;
	kthread_t	*ta_lwp;	/* worker running this task, or NULL */
	taskq_ent_t	ta_ent;
	taskq_t		*ta_tq;
};

void	taskqueue_init_task(struct task *, void (*)(void *, int), void *);
int	taskqueue_enqueue(struct taskqueue *, struct task *);
void	taskqueue_drain(struct taskqueue *, struct task *);

#define	TASK_INIT(t, pri, func, ctx)					\
	taskqueue_init_task((t), (void (*)(void *, int))(func), (ctx))

#endif /* _KERNEL */

#endif /* _OPENSOLARIS_SYS_TASKQUEUE_H_ */
