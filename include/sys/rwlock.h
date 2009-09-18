/*
 *  This file is part of the SPL: Solaris Porting Layer.
 *
 *  Copyright (c) 2009 Lawrence Livermore National Security, LLC.
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
#include <sys/types.h>

typedef enum {
        RW_DRIVER  = 2,
        RW_DEFAULT = 4
} krw_type_t;

typedef enum {
	RW_NONE   = 0,
        RW_WRITER = 1,
        RW_READER = 2
} krw_t;

typedef struct rw_semaphore krwlock_t;

#define rw_init(rwlp, name, type, arg)  init_rwsem(rwlp)
#define rw_destroy(rwlp)                ((void)0)
#define rw_downgrade(rwlp)              downgrade_write(rwlp)
#define RW_LOCK_HELD(rwlp)              rwsem_is_locked(rwlp)
/*
 * the rw-semaphore definition
 * - if activity/count is 0 then there are no active readers or writers
 * - if activity/count is +ve then that is the number of active readers
 * - if activity/count is -1 then there is one active writer
 */
#if defined(CONFIG_RWSEM_GENERIC_SPINLOCK)
# define RW_COUNT(rwlp)            ((rwlp)->activity)
# define RW_READ_HELD(rwlp)        ((RW_COUNT(rwlp) > 0) ? RW_COUNT(rwlp) : 0)
# define RW_WRITE_HELD(rwlp)       ((RW_COUNT(rwlp) < 0))
# define rw_exit_locked(rwlp)      __up_read_locked(rwlp)
# define rw_tryenter_locked(rwlp)  __down_write_trylock_locked(rwlp)
void __up_read_locked(struct rw_semaphore *);
int __down_write_trylock_locked(struct rw_semaphore *);
#else
# define RW_COUNT(rwlp)            ((rwlp)->count & RWSEM_ACTIVE_MASK)
# define RW_READ_HELD(rwlp)        ((RW_COUNT(rwlp) > 0) ? RW_COUNT(rwlp) : 0)
# define RW_WRITE_HELD(rwlp)       ((RW_COUNT(rwlp) < 0))
# define rw_exit_locked(rwlp)      up_read(rwlp)
# define rw_tryenter_locked(rwlp)  down_write_trylock(rwlp)
#endif

#define rw_tryenter(rwlp, rw)                                                 \
({                                                                            \
        int _rc_ = 0;                                                         \
        switch (rw) {                                                         \
                case RW_READER: _rc_ = down_read_trylock(rwlp);  break;       \
                case RW_WRITER: _rc_ = down_write_trylock(rwlp); break;       \
                default:        SBUG();                                       \
        }                                                                     \
        _rc_;                                                                 \
})

#define rw_enter(rwlp, rw)                                                    \
({                                                                            \
        switch (rw) {                                                         \
                case RW_READER: down_read(rwlp);  break;                      \
                case RW_WRITER: down_write(rwlp); break;                      \
                default:        SBUG();                                       \
        }                                                                     \
})

#define rw_exit(rwlp)                                                         \
({                                                                            \
        if (RW_READ_HELD(rwlp))                                               \
              up_read(rwlp);                                                  \
        else if (RW_WRITE_HELD(rwlp))                                         \
              up_write(rwlp);                                                 \
        else                                                                  \
              SBUG();                                                         \
})

#define rw_tryupgrade(rwlp)                                                   \
({                                                                            \
        unsigned long flags;                                                  \
        int _rc_ = 0;                                                         \
        spin_lock_irqsave(&(rwlp)->wait_lock, flags);                         \
        if (list_empty(&(rwlp)->wait_list) && (RW_READ_HELD(rwlp) == 1)) {    \
                rw_exit_locked(rwlp);                                         \
                _rc_ = rw_tryenter_locked(rwlp);                              \
                ASSERT(_rc_);                                                 \
        }                                                                     \
        spin_unlock_irqrestore(&(rwlp)->wait_lock, flags);                    \
        _rc_;                                                                 \
})

#endif /* _SPL_RWLOCK_H */
