/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or https://opensource.org/licenses/CDDL-1.0.
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
 * Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef	_SYS_TASKQ_H
#define	_SYS_TASKQ_H

#ifdef _KERNEL

#include <sys/types.h>
#include <sys/proc.h>
#include <sys/taskqueue.h>
#include <sys/thread.h>
#include <sys/ck.h>

#ifdef	__cplusplus
extern "C" {
#endif

#define	TASKQ_NAMELEN	31

typedef struct taskq {
	struct taskqueue	*tq_queue;
} taskq_t;

typedef uintptr_t taskqid_t;
typedef void (task_func_t)(void *);

typedef struct taskq_ent {
	struct task	 tqent_task;
	struct timeout_task tqent_timeout_task;
	task_func_t	*tqent_func;
	void		*tqent_arg;
	taskqid_t tqent_id;
	CK_LIST_ENTRY(taskq_ent) tqent_hash;
	uint8_t tqent_type;
	uint8_t tqent_registered;
	uint8_t tqent_cancelled;
	volatile uint32_t tqent_rc;
} taskq_ent_t;

/*
 * Public flags for taskq_create(): bit range 0-15
 */
#define	TASKQ_PREPOPULATE	0x0001	/* Prepopulate with threads and data */
#define	TASKQ_CPR_SAFE		0x0002	/* Use CPR safe protocol */
#define	TASKQ_DYNAMIC		0x0004	/* Use dynamic thread scheduling */
#define	TASKQ_THREADS_CPU_PCT	0x0008	/* number of threads as % of ncpu */
#define	TASKQ_DC_BATCH		0x0010	/* Taskq uses SDC in batch mode */

/*
 * Flags for taskq_dispatch. TQ_SLEEP/TQ_NOSLEEP should be same as
 * KM_SLEEP/KM_NOSLEEP.
 */
#define	TQ_SLEEP	0x00	/* Can block for memory */
#define	TQ_NOSLEEP	0x01	/* cannot block for memory; may fail */
#define	TQ_NOQUEUE	0x02	/* Do not enqueue if can't dispatch */
#define	TQ_NOALLOC	0x04	/* cannot allocate memory; may fail */
#define	TQ_FRONT	0x08	/* Put task at the front of the queue */

#define	TASKQID_INVALID		((taskqid_t)0)

#define	taskq_init_ent(x)
extern taskq_t *system_taskq;
/* Global dynamic task queue for long delay */
extern taskq_t *system_delay_taskq;

extern taskqid_t taskq_dispatch(taskq_t *, task_func_t, void *, uint_t);
extern taskqid_t taskq_dispatch_delay(taskq_t *, task_func_t, void *,
    uint_t, clock_t);
extern void taskq_dispatch_ent(taskq_t *, task_func_t, void *, uint_t,
    taskq_ent_t *);
extern int taskq_empty_ent(taskq_ent_t *);
taskq_t	*taskq_create(const char *, int, pri_t, int, int, uint_t);
taskq_t	*taskq_create_instance(const char *, int, int, pri_t, int, int, uint_t);
taskq_t	*taskq_create_proc(const char *, int, pri_t, int, int,
    struct proc *, uint_t);
taskq_t	*taskq_create_sysdc(const char *, int, int, int,
    struct proc *, uint_t, uint_t);
void	nulltask(void *);
extern void taskq_destroy(taskq_t *);
extern void taskq_wait_id(taskq_t *, taskqid_t);
extern void taskq_wait_outstanding(taskq_t *, taskqid_t);
extern void taskq_wait(taskq_t *);
extern int taskq_cancel_id(taskq_t *, taskqid_t);
extern int taskq_member(taskq_t *, kthread_t *);
extern taskq_t *taskq_of_curthread(void);
void	taskq_suspend(taskq_t *);
int	taskq_suspended(taskq_t *);
void	taskq_resume(taskq_t *);

#ifdef	__cplusplus
}
#endif

#endif /* _KERNEL */

#ifdef _STANDALONE
typedef int taskq_ent_t;
#define	taskq_init_ent(x)
#endif /* _STANDALONE */

#endif	/* _SYS_TASKQ_H */
