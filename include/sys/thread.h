#ifndef _SPL_THREAD_H
#define _SPL_THREAD_H

#ifdef  __cplusplus
extern "C" {
#endif

#include <linux/module.h>
#include <linux/mm.h>
#include <linux/spinlock.h>
#include <sys/types.h>
#include <sys/sysmacros.h>

/*
 * Thread interfaces
 */
#define TP_MAGIC			0x53535353

#define TS_SLEEP			TASK_INTERRUPTIBLE
#define TS_RUN				TASK_RUNNING
#define TS_ZOMB				EXIT_ZOMBIE
#define TS_STOPPED			TASK_STOPPED
#if 0
#define TS_FREE				0x00	/* No clean linux mapping */
#define TS_ONPROC			0x04	/* No clean linux mapping */
#define TS_WAIT				0x20	/* No clean linux mapping */
#endif

typedef void (*thread_func_t)(void *);

#define thread_create(stk, stksize, func, arg, len, pp, state, pri)      \
	__thread_create(stk, stksize, (thread_func_t)func,               \
		        arg, len, pp, state, pri)
#define thread_exit()			__thread_exit()
#define curthread			get_current()

extern kthread_t *__thread_create(caddr_t stk, size_t  stksize,
                            thread_func_t func, void *args,
                            size_t len, int *pp, int state,
                            pri_t pri);
extern void __thread_exit(void);

#ifdef  __cplusplus
}
#endif

#endif  /* _SPL_THREAD_H */

