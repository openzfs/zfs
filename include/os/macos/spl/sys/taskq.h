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
 * Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */
/*
 * Copyright (C) 2015 Jorgen Lundman <lundman@lundman.net>
 */

#ifndef	_SYS_TASKQ_H
#define	_SYS_TASKQ_H

#include <sys/types.h>
#include <sys/thread.h>
#include <sys/rwlock.h>

#ifdef	__cplusplus
extern "C" {
#endif

#define	TASKQ_NAMELEN	31

typedef struct taskq taskq_t;
typedef uintptr_t taskqid_t;
typedef void (task_func_t)(void *);

struct proc;
struct taskq_ent;

/* New ZFS expects to find taskq_ent_t as well */
#include <sys/taskq_impl.h>

/*
 * Public flags for taskq_create(): bit range 0-15
 */
#define	TASKQ_PREPOPULATE	0x0001	/* Prepopulate with threads and data */
#define	TASKQ_CPR_SAFE		0x0002	/* Use CPR safe protocol */
#define	TASKQ_DYNAMIC		0x0004	/* Use dynamic thread scheduling */
#define	TASKQ_THREADS_CPU_PCT	0x0008	/* number of threads as % of ncpu */
#define	TASKQ_DC_BATCH		0x0010	/* Taskq uses SDC in batch mode */

#ifdef __APPLE__
#define	TASKQ_TIMESHARE		0x0020  /* macOS dynamic thread priority */
#define	TASKQ_REALLY_DYNAMIC	0x0040  /* don't filter out TASKQ_DYNAMIC */
#endif

/*
 * Flags for taskq_dispatch. TQ_SLEEP/TQ_NOSLEEP should be same as
 * KM_SLEEP/KM_NOSLEEP.
 */
#define	TQ_SLEEP	0x00	/* Can block for memory */
#define	TQ_NOSLEEP	0x01	/* cannot block for memory; may fail */
#define	TQ_NOQUEUE	0x02	/* Do not enqueue if can't dispatch */
#define	TQ_NOALLOC	0x04	/* cannot allocate memory; may fail */
#define	TQ_FRONT	0x08	/* Put task at the front of the queue */

#define	TASKQID_INVALID ((taskqid_t)0)

#ifdef _KERNEL

extern taskq_t *system_taskq;
/* Global dynamic task queue for long delay */
extern taskq_t *system_delay_taskq;

extern int 	spl_taskq_init(void);
extern void	spl_taskq_fini(void);
extern void	taskq_mp_init(void);

extern taskq_t	*taskq_create(const char *, int, pri_t, int, int, uint_t);
extern taskq_t	*taskq_create_instance(const char *, int, int, pri_t, int,
    int, uint_t);
extern taskq_t	*taskq_create_proc(const char *, int, pri_t, int, int,
    proc_t *, uint_t);
extern taskq_t	*taskq_create_sysdc(const char *, int, int, int,
    proc_t *, uint_t, uint_t);
extern taskqid_t taskq_dispatch(taskq_t *, task_func_t, void *, uint_t);
extern void	nulltask(void *);
extern void	taskq_destroy(taskq_t *);
extern void	taskq_wait(taskq_t *);
#define	HAVE_TASKQ_WAIT_ID
extern void taskq_wait_id(taskq_t *, taskqid_t);
extern void	taskq_suspend(taskq_t *);
extern int	taskq_suspended(taskq_t *);
extern void	taskq_resume(taskq_t *);
extern int	taskq_member(taskq_t *, kthread_t *);
extern int taskq_cancel_id(taskq_t *, taskqid_t);
extern taskq_t *taskq_of_curthread(void);
extern int taskq_empty_ent(struct taskq_ent *);

extern void taskq_wait_outstanding(taskq_t *, taskqid_t);

extern void system_taskq_init(void);
extern void system_taskq_fini(void);

#endif	/* _KERNEL */

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_TASKQ_H */
