/*****************************************************************************\
 *  Copyright (C) 2007-2010 Lawrence Livermore National Security, LLC.
 *  Copyright (C) 2007 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Brian Behlendorf <behlendorf1@llnl.gov>.
 *  UCRL-CODE-235197
 *
 *  This file is part of the SPL, Solaris Porting Layer.
 *  For details, see <http://github.com/behlendorf/spl/>.
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
 * CDEBUG()		- Log debug message with specified mask.
 * CDEBUG_LIMIT()	- Log just 1 debug message with specified mask.
 * CWARN()		- Log a warning message.
 * CERROR()		- Log an error message.
 * CEMERG()		- Log an emergency error message.
 * CONSOLE()		- Log a generic message to the console.
 *
 * ENTRY		- Log entry point to a function.
 * EXIT			- Log exit point from a function.
 * RETURN(x)		- Log return from a function.
 * GOTO(x, y)		- Log goto within a function.
 */

#ifndef _SPL_DEBUG_INTERNAL_H
#define _SPL_DEBUG_INTERNAL_H

#include <linux/limits.h>

#define S_UNDEFINED	0x00000001
#define S_ATOMIC	0x00000002
#define S_KOBJ		0x00000004
#define S_VNODE		0x00000008
#define S_TIME		0x00000010
#define S_RWLOCK	0x00000020
#define S_THREAD	0x00000040
#define S_CONDVAR	0x00000080
#define S_MUTEX		0x00000100
#define S_RNG		0x00000200
#define S_TASKQ		0x00000400
#define S_KMEM		0x00000800
#define S_DEBUG		0x00001000
#define S_GENERIC	0x00002000
#define S_PROC		0x00004000
#define S_MODULE	0x00008000
#define S_CRED		0x00010000

#define D_TRACE		0x00000001
#define D_INFO		0x00000002
#define D_WARNING	0x00000004
#define D_ERROR		0x00000008
#define D_EMERG		0x00000010
#define D_CONSOLE	0x00000020
#define D_IOCTL		0x00000040
#define D_DPRINTF	0x00000080
#define D_OTHER		0x00000100

#define D_CANTMASK	(D_ERROR | D_EMERG | D_WARNING | D_CONSOLE)
#define DEBUG_SUBSYSTEM	S_UNDEFINED

#ifdef NDEBUG /* Debugging Disabled */

#define CDEBUG(mask, fmt, a...)		((void)0)
#define CDEBUG_LIMIT(x, y, fmt, a...)	((void)0)
#define CWARN(fmt, a...)		((void)0)
#define CERROR(fmt, a...)		((void)0)
#define CEMERG(fmt, a...)		((void)0)
#define CONSOLE(mask, fmt, a...)	((void)0)

#define ENTRY				((void)0)
#define EXIT				((void)0)
#define RETURN(x)			return (x)
#define GOTO(x, y)			{ ((void)(y)); goto x; }

#else /* Debugging Enabled */

#define __CDEBUG(cdls, subsys, mask, format, a...)			\
do {									\
	if (((mask) & D_CANTMASK) != 0 ||				\
	    ((spl_debug_mask & (mask)) != 0 &&				\
	     (spl_debug_subsys & (subsys)) != 0))			\
		spl_debug_msg(cdls, subsys, mask, __FILE__,		\
		__FUNCTION__, __LINE__, format, ## a);			\
} while (0)

#define CDEBUG(mask, format, a...)					\
	__CDEBUG(NULL, DEBUG_SUBSYSTEM, mask, format, ## a)

#define __CDEBUG_LIMIT(subsys, mask, format, a...)			\
do {									\
	static spl_debug_limit_state_t cdls;				\
									\
	__CDEBUG(&cdls, subsys, mask, format, ## a);			\
} while (0)

#define CDEBUG_LIMIT(mask, format, a...)				\
	__CDEBUG_LIMIT(DEBUG_SUBSYSTEM, mask, format, ## a)

#define CWARN(fmt, a...)		CDEBUG_LIMIT(D_WARNING, fmt, ## a)
#define CERROR(fmt, a...)		CDEBUG_LIMIT(D_ERROR, fmt, ## a)
#define CEMERG(fmt, a...)		CDEBUG_LIMIT(D_EMERG, fmt, ## a)
#define CONSOLE(mask, fmt, a...)	CDEBUG(D_CONSOLE | (mask), fmt, ## a)

#define ENTRY				CDEBUG(D_TRACE, "Process entered\n")
#define EXIT				CDEBUG(D_TRACE, "Process leaving\n")

#define RETURN(rc)							\
do {									\
	typeof(rc) RETURN__ret = (rc);					\
	CDEBUG(D_TRACE, "Process leaving (rc=%lu : %ld : %lx)\n",	\
	    (long)RETURN__ret, (long)RETURN__ret, (long)RETURN__ret);	\
	return RETURN__ret;						\
} while (0)

#define GOTO(label, rc)							\
do {									\
	long GOTO__ret = (long)(rc);					\
	CDEBUG(D_TRACE,"Process leaving via %s (rc=%lu : %ld : %lx)\n",	\
	    #label, (unsigned long)GOTO__ret, (signed long)GOTO__ret,	\
	    (signed long)GOTO__ret);					\
	goto label;							\
} while (0)

#endif /* NDEBUG */

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
extern int spl_debug_clear_buffer(void);
extern int spl_debug_mark_buffer(char *text);

int debug_init(void);
void debug_fini(void);

#endif /* SPL_DEBUG_INTERNAL_H */
