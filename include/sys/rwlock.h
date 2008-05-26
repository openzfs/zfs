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

#ifndef _SPL_RWLOCK_H
#define _SPL_RWLOCK_H

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/rwsem.h>
#include <asm/current.h>
#include <sys/types.h>
#include <sys/kmem.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
	RW_DRIVER  = 2,		/* driver (DDI) rwlock */
	RW_DEFAULT = 4		/* kernel default rwlock */
} krw_type_t;

typedef enum {
	RW_WRITER,
	RW_READER
} krw_t;


#define RW_MAGIC  0x3423645a
#define RW_POISON 0xa6

typedef struct {
	int32_t rw_magic;
	int32_t rw_name_size;
	char *rw_name;
	struct rw_semaphore rw_sem;
	struct task_struct *rw_owner;	/* holder of the write lock */
} krwlock_t;

extern void __rw_init(krwlock_t *rwlp, char *name, krw_type_t type, void *arg);
extern void __rw_destroy(krwlock_t *rwlp);
extern int __rw_tryenter(krwlock_t *rwlp, krw_t rw);
extern void __rw_enter(krwlock_t *rwlp, krw_t rw);
extern void __rw_exit(krwlock_t *rwlp);
extern void __rw_downgrade(krwlock_t *rwlp);
extern int __rw_tryupgrade(krwlock_t *rwlp);
extern kthread_t *__rw_owner(krwlock_t *rwlp);
extern int __rw_read_held(krwlock_t *rwlp);
extern int __rw_write_held(krwlock_t *rwlp);
extern int __rw_lock_held(krwlock_t *rwlp);

#define rw_init(rwlp, name, type, arg)					\
({									\
        if ((name) == NULL)						\
                __rw_init(rwlp, #rwlp, type, arg);			\
        else								\
                __rw_init(rwlp, name, type, arg);			\
})
#define rw_destroy(rwlp)	__rw_destroy(rwlp)
#define rw_tryenter(rwlp, rw)	__rw_tryenter(rwlp, rw)
#define rw_enter(rwlp, rw)	__rw_enter(rwlp, rw)
#define rw_exit(rwlp)		__rw_exit(rwlp)
#define rw_downgrade(rwlp)	__rw_downgrade(rwlp)
#define rw_tryupgrade(rwlp)	__rw_tryupgrade(rwlp)
#define rw_owner(rwlp)		__rw_owner(rwlp)
#define RW_READ_HELD(rwlp)	__rw_read_held(rwlp)
#define RW_WRITE_HELD(rwlp)	__rw_write_held(rwlp)
#define RW_LOCK_HELD(rwlp)	__rw_lock_held(rwlp)

#ifdef __cplusplus
}
#endif

#endif /* _SPL_RWLOCK_H */
