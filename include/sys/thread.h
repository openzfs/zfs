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

#ifndef _SPL_THREAD_H
#define _SPL_THREAD_H

#ifdef  __cplusplus
extern "C" {
#endif

#include <linux/module.h>
#include <linux/mm.h>
#include <linux/spinlock.h>
#include <linux/kthread.h>
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
	                #func, arg, len, pp, state, pri)
#define thread_exit()			__thread_exit()
#define curthread			get_current()

extern kthread_t *__thread_create(caddr_t stk, size_t  stksize,
                                  thread_func_t func, const char *name,
                                  void *args, size_t len, int *pp,
                                  int state, pri_t pri);
extern void __thread_exit(void);

#ifdef  __cplusplus
}
#endif

#endif  /* _SPL_THREAD_H */

