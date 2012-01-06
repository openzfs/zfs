/*****************************************************************************\
 *  Copyright (C) 2007-2010 Lawrence Livermore National Security, LLC.
 *  Copyright (C) 2007 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Brian Behlendorf <behlendorf1@llnl.gov>.
 *  UCRL-CODE-235197
 *
 *  This file is part of the SPL, Solaris Porting Layer.
 *  For details, see <http://zfsonlinux.org/>.
 *
 *  The SPL is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the
 *  Free Software Foundation; either version 2 of the License, or (at your
 *  option) any later version.
 *
 *  The SPL is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with the SPL.  If not, see <http://www.gnu.org/licenses/>.
\*****************************************************************************/

/*
 * Available debug functions.  These function should be used by any
 * package which needs to integrate with the SPL log infrastructure.
 *
 * SDEBUG()		- Log debug message with specified mask.
 * SDEBUG_LIMIT()	- Log just 1 debug message with specified mask.
 * SWARN()		- Log a warning message.
 * SERROR()		- Log an error message.
 * SEMERG()		- Log an emergency error message.
 * SCONSOLE()		- Log a generic message to the console.
 *
 * SENTRY		- Log entry point to a function.
 * SEXIT		- Log exit point from a function.
 * SRETURN(x)		- Log return from a function.
 * SGOTO(x, y)		- Log goto within a function.
 */

#ifndef _SPL_DEBUG_INTERNAL_H
#define _SPL_DEBUG_INTERNAL_H

#include <linux/limits.h>
#include <linux/sched.h>

#define SS_UNDEFINED	0x00000001
#define SS_ATOMIC	0x00000002
#define SS_KOBJ		0x00000004
#define SS_VNODE	0x00000008
#define SS_TIME		0x00000010
#define SS_RWLOCK	0x00000020
#define SS_THREAD	0x00000040
#define SS_CONDVAR	0x00000080
#define SS_MUTEX	0x00000100
#define SS_RNG		0x00000200
#define SS_TASKQ	0x00000400
#define SS_KMEM		0x00000800
#define SS_DEBUG	0x00001000
#define SS_GENERIC	0x00002000
#define SS_PROC		0x00004000
#define SS_MODULE	0x00008000
#define SS_CRED		0x00010000
#define SS_KSTAT	0x00020000
#define SS_XDR		0x00040000
#define SS_TSD		0x00080000
#define SS_ZLIB		0x00100000
#define SS_USER1	0x01000000
#define SS_USER2	0x02000000
#define SS_USER3	0x04000000
#define SS_USER4	0x08000000
#define SS_USER5	0x10000000
#define SS_USER6	0x20000000
#define SS_USER7	0x40000000
#define SS_USER8	0x80000000
#define SS_DEBUG_SUBSYS	SS_UNDEFINED

#define SD_TRACE	0x00000001
#define SD_INFO		0x00000002
#define SD_WARNING	0x00000004
#define SD_ERROR	0x00000008
#define SD_EMERG	0x00000010
#define SD_CONSOLE	0x00000020
#define SD_IOCTL	0x00000040
#define SD_DPRINTF	0x00000080
#define SD_OTHER	0x00000100
#define SD_CANTMASK	(SD_ERROR | SD_EMERG | SD_WARNING | SD_CONSOLE)

/* Debug log support enabled */
#ifdef DEBUG_LOG

#define __SDEBUG(cdls, subsys, mask, format, a...)			\
do {									\
	if (((mask) & SD_CANTMASK) != 0 ||				\
	    ((spl_debug_mask & (mask)) != 0 &&				\
	     (spl_debug_subsys & (subsys)) != 0))			\
		spl_debug_msg(cdls, subsys, mask, __FILE__,		\
		__FUNCTION__, __LINE__, format, ## a);			\
} while (0)

#define SDEBUG(mask, format, a...)					\
	__SDEBUG(NULL, SS_DEBUG_SUBSYS, mask, format, ## a)

#define __SDEBUG_LIMIT(subsys, mask, format, a...)			\
do {									\
	static spl_debug_limit_state_t cdls;				\
									\
	__SDEBUG(&cdls, subsys, mask, format, ## a);			\
} while (0)

#define SDEBUG_LIMIT(mask, format, a...)				\
	__SDEBUG_LIMIT(SS_DEBUG_SUBSYS, mask, format, ## a)

#define SWARN(fmt, a...)		SDEBUG_LIMIT(SD_WARNING, fmt, ## a)
#define SERROR(fmt, a...)		SDEBUG_LIMIT(SD_ERROR, fmt, ## a)
#define SEMERG(fmt, a...)		SDEBUG_LIMIT(SD_EMERG, fmt, ## a)
#define SCONSOLE(mask, fmt, a...)	SDEBUG(SD_CONSOLE | (mask), fmt, ## a)

#define SENTRY				SDEBUG(SD_TRACE, "Process entered\n")
#define SEXIT				SDEBUG(SD_TRACE, "Process leaving\n")

#define SRETURN(rc)							\
do {									\
	typeof(rc) RETURN__ret = (rc);					\
	SDEBUG(SD_TRACE, "Process leaving (rc=%lu : %ld : %lx)\n",	\
	    (long)RETURN__ret, (long)RETURN__ret, (long)RETURN__ret);	\
	return RETURN__ret;						\
} while (0)

#define SGOTO(label, rc)						\
do {									\
	long GOTO__ret = (long)(rc);					\
	SDEBUG(SD_TRACE,"Process leaving via %s (rc=%lu : %ld : %lx)\n",\
	    #label, (unsigned long)GOTO__ret, (signed long)GOTO__ret,	\
	    (signed long)GOTO__ret);					\
	goto label;							\
} while (0)

typedef struct {
	unsigned long	cdls_next;
	int		cdls_count;
	long		cdls_delay;
} spl_debug_limit_state_t;

/* Global debug variables */
extern unsigned long spl_debug_subsys;
extern unsigned long spl_debug_mask;
extern unsigned long spl_debug_printk;
extern int spl_debug_mb;
extern unsigned int spl_debug_binary;
extern unsigned int spl_debug_catastrophe;
extern unsigned int spl_debug_panic_on_bug;
extern char spl_debug_file_path[PATH_MAX];
extern unsigned int spl_console_ratelimit;
extern long spl_console_max_delay;
extern long spl_console_min_delay;
extern unsigned int spl_console_backoff;
extern unsigned int spl_debug_stack;

/* Exported debug functions */
extern int spl_debug_mask2str(char *str, int size, unsigned long mask, int ss);
extern int spl_debug_str2mask(unsigned long *mask, const char *str, int ss);
extern unsigned long spl_debug_set_mask(unsigned long mask);
extern unsigned long spl_debug_get_mask(void);
extern unsigned long spl_debug_set_subsys(unsigned long mask);
extern unsigned long spl_debug_get_subsys(void);
extern int spl_debug_set_mb(int mb);
extern int spl_debug_get_mb(void);
extern int spl_debug_dumplog(int flags);
extern void spl_debug_dumpstack(struct task_struct *tsk);
extern void spl_debug_bug(char *file, const char *fn, const int line, int fl);
extern int spl_debug_msg(void *arg, int subsys, int mask, const char *file,
    const char *fn, const int line, const char *format, ...);
extern int spl_debug_clear_buffer(void);
extern int spl_debug_mark_buffer(char *text);

int spl_debug_init(void);
void spl_debug_fini(void);

/* Debug log support disabled */
#else /* DEBUG_LOG */

#define __SDEBUG(x, y, mask, fmt, a...)	((void)0)
#define SDEBUG(mask, fmt, a...)		((void)0)
#define SDEBUG_LIMIT(x, y, fmt, a...)	((void)0)
#define SWARN(fmt, a...)		((void)0)
#define SERROR(fmt, a...)		((void)0)
#define SEMERG(fmt, a...)		((void)0)
#define SCONSOLE(mask, fmt, a...)	((void)0)

#define SENTRY				((void)0)
#define SEXIT				((void)0)
#define SRETURN(x)			return (x)
#define SGOTO(x, y)			{ ((void)(y)); goto x; }

static inline unsigned long
spl_debug_set_mask(unsigned long mask) {
	return (0);
}

static inline unsigned long
spl_debug_get_mask(void) {
	return (0);
}

static inline unsigned long
spl_debug_set_subsys(unsigned long mask) {
	return (0);
}

static inline unsigned long
spl_debug_get_subsys(void) {
	return (0);
}

static inline int
spl_debug_set_mb(int mb) {
	return (0);
}

static inline int
spl_debug_get_mb(void) {
	return (0);
}

static inline int
spl_debug_dumplog(int flags)
{
	return (0);
}

static inline void
spl_debug_dumpstack(struct task_struct *tsk)
{
	return;
}

static inline void
spl_debug_bug(char *file, const char *fn, const int line, int fl)
{
	return;
}

static inline int
spl_debug_msg(void *arg, int subsys, int mask, const char *file,
    const char *fn, const int line, const char *format, ...)
{
	return (0);
}

static inline int
spl_debug_clear_buffer(void)
{
	return (0);
}

static inline int
spl_debug_mark_buffer(char *text)
{
	return (0);
}

static inline int
spl_debug_init(void) {
	return (0);
}

static inline void
spl_debug_fini(void) {
	return;
}

#endif /* DEBUG_LOG */

#endif /* SPL_DEBUG_INTERNAL_H */
