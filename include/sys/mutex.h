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

#ifndef _SPL_MUTEX_H
#define	_SPL_MUTEX_H

#ifdef	__cplusplus
extern "C" {
#endif

#include <linux/module.h>
#include <linux/hardirq.h>
#include <sys/types.h>
#include <sys/kmem.h>

#define MUTEX_DEFAULT		0
#define MUTEX_SPIN		1
#define MUTEX_ADAPTIVE		2

#define MUTEX_ENTER_TOTAL	0
#define MUTEX_ENTER_NOT_HELD	1
#define MUTEX_ENTER_SPIN	2
#define MUTEX_ENTER_SLEEP	3
#define MUTEX_TRYENTER_TOTAL	4
#define MUTEX_TRYENTER_NOT_HELD	5
#define MUTEX_STATS_SIZE	6

#define KM_MAGIC		0x42424242
#define KM_POISON		0x84

typedef struct {
	int32_t km_magic;
	int16_t km_type;
	int16_t km_name_size;
	char *km_name;
	struct task_struct *km_owner;
	struct semaphore *km_sem;
#ifdef DEBUG_MUTEX
	int *km_stats;
	struct list_head km_list;
#endif
} kmutex_t;

extern int mutex_spin_max;

#ifdef DEBUG_MUTEX
extern int mutex_stats[MUTEX_STATS_SIZE];
extern spinlock_t mutex_stats_lock;
extern struct list_head mutex_stats_list;
#define MUTEX_STAT_INC(stats, stat)	((stats)[stat]++)
#else
#define MUTEX_STAT_INC(stats, stat)
#endif

int spl_mutex_init(void);
void spl_mutex_fini(void);

extern void __spl_mutex_init(kmutex_t *mp, char *name, int type, void *ibc);
extern void __spl_mutex_destroy(kmutex_t *mp);
extern int __mutex_tryenter(kmutex_t *mp);
extern void __mutex_enter(kmutex_t *mp);
extern void __mutex_exit(kmutex_t *mp);
extern int __mutex_owned(kmutex_t *mp);
extern kthread_t *__spl_mutex_owner(kmutex_t *mp);

#undef mutex_init
#undef mutex_destroy

#define mutex_init(mp, name, type, ibc)					\
({									\
	if ((name) == NULL)						\
		__spl_mutex_init(mp, #mp, type, ibc);			\
	else								\
		__spl_mutex_init(mp, name, type, ibc);			\
})
#define mutex_destroy(mp)	__spl_mutex_destroy(mp)
#define mutex_tryenter(mp)	__mutex_tryenter(mp)
#define mutex_enter(mp)		__mutex_enter(mp)
#define mutex_exit(mp)		__mutex_exit(mp)
#define mutex_owned(mp)		__mutex_owned(mp)
#define mutex_owner(mp)		__spl_mutex_owner(mp)
#define MUTEX_HELD(mp)		mutex_owned(mp)

#ifdef	__cplusplus
}
#endif

#endif	/* _SPL_MUTEX_H */
