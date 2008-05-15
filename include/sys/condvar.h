#ifndef _SPL_CONDVAR_H
#define _SPL_CONDVAR_H

#ifdef  __cplusplus
extern "C" {
#endif

#include <linux/module.h>
#include <linux/wait.h>
#include <sys/mutex.h>

/* The kcondvar_t struct is protected by mutex taken externally before
 * calling any of the wait/signal funs, and passed into the wait funs.
 */
#define CV_MAGIC			0x346545f4
#define CV_POISON			0x95

typedef struct {
	int cv_magic;
	char *cv_name;
	int cv_name_size;
	wait_queue_head_t cv_event;
	atomic_t cv_waiters;
	kmutex_t *cv_mutex;
	spinlock_t cv_lock;
} kcondvar_t;

typedef enum { CV_DEFAULT=0, CV_DRIVER } kcv_type_t;

extern void __cv_init(kcondvar_t *cvp, char *name, kcv_type_t type, void *arg);
extern void __cv_destroy(kcondvar_t *cvp);
extern void __cv_wait(kcondvar_t *cvp, kmutex_t *mp);
extern clock_t __cv_timedwait(kcondvar_t *cvp, kmutex_t *mp,
			      clock_t expire_time);
extern void __cv_signal(kcondvar_t *cvp);
extern void __cv_broadcast(kcondvar_t *cvp);

#define cv_init(cvp, name, type, arg)                                 \
({                                                                    \
	if ((name) == NULL)                                           \
		__cv_init(cvp, #cvp, type, arg);                      \
	else                                                          \
		__cv_init(cvp, name, type, arg);                      \
})
#define cv_destroy(cvp)			__cv_destroy(cvp)
#define cv_wait(cvp, mp)		__cv_wait(cvp, mp)
#define cv_timedwait(cvp, mp, t)	__cv_timedwait(cvp, mp, t)
#define cv_signal(cvp)			__cv_signal(cvp)
#define cv_broadcast(cvp)		__cv_broadcast(cvp)

#endif /* _SPL_CONDVAR_H */
