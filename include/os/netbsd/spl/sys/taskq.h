// SPDX-License-Identifier: CDDL-1.0
#ifndef	_SYS_TASKQ_H
#define	_SYS_TASKQ_H

#ifdef _KERNEL

#include <sys/types.h>
#include <sys/proc.h>
#include <sys/queue.h>
#include <sys/mutex.h>
#include <sys/condvar.h>
#include <sys/thread.h>
#include <sys/workqueue.h>

#ifdef	__cplusplus
extern "C" {
#endif

#define	TASKQ_NAMELEN	31

typedef uint64_t taskqid_t;
typedef void (task_func_t)(void *);

struct taskq;
typedef struct taskq taskq_t;

/*
 * tqent_state is "on a queue right now", not "callback in flight".
 * The worker unlinks and returns to IDLE before calling tqent_func,
 * so the callback may taskq_dispatch_ent() the same storage
 * (zio->io_tqent).  FreeBSD's ta_pending works the same way.
 */
#define	TQENT_IDLE	0
#define	TQENT_QUEUED	1

typedef struct taskq_ent {
	TAILQ_ENTRY(taskq_ent) tqent_list;
	task_func_t	*tqent_func;
	void		*tqent_arg;
	struct taskq	*tqent_tq;
	taskqid_t	 tqent_id;
	clock_t		 tqent_expire;
	uint_t		 tqent_flags;
	uint_t		 tqent_state;
	unsigned	 tqent_dynamic:1;
	struct work	 tqent_work;
} taskq_ent_t;

#define	TASKQ_PREPOPULATE	0x0001
#define	TASKQ_CPR_SAFE		0x0002
#define	TASKQ_DYNAMIC		0x0004
#define	TASKQ_THREADS_CPU_PCT	0x0008
#define	TASKQ_DC_BATCH		0x0010

#define	TQ_SLEEP	0x00
#define	TQ_NOSLEEP	0x01
#define	TQ_NOQUEUE	0x02
#define	TQ_NOALLOC	0x04
#define	TQ_FRONT	0x08

#define	TASKQID_INVALID		((taskqid_t)0)

extern taskq_t *system_taskq;
extern taskq_t *system_delay_taskq;

void	spl_taskq_init(void);
void	spl_taskq_fini(void);

extern taskqid_t taskq_dispatch(taskq_t *, task_func_t, void *, uint_t);
extern taskqid_t taskq_dispatch_delay(taskq_t *, task_func_t, void *,
    uint_t, clock_t);
extern void taskq_dispatch_ent(taskq_t *, task_func_t, void *, uint_t,
    taskq_ent_t *);
extern int taskq_empty_ent(taskq_ent_t *);
extern void taskq_init_ent(taskq_ent_t *);

taskq_t	*taskq_create(const char *, int, pri_t, int, int, uint_t);
taskq_t	*taskq_create_synced(const char *, int, pri_t, int, int, uint_t,
    kthread_t ***);
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
extern int taskq_cancel_id(taskq_t *, taskqid_t, boolean_t);
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
typedef void taskq_t;
typedef int taskq_ent_t;
#define	taskq_init_ent(x)
#endif

#endif	/* _SYS_TASKQ_H */
