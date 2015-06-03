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

#ifndef _SPL_RWLOCK_H
#define _SPL_RWLOCK_H

#include <sys/types.h>
#include <linux/rwsem.h>
#include <linux/rwsem_compat.h>

typedef enum {
        RW_DRIVER  = 2,
        RW_DEFAULT = 4
} krw_type_t;

typedef enum {
        RW_NONE   = 0,
        RW_WRITER = 1,
        RW_READER = 2
} krw_t;

typedef struct {
        struct rw_semaphore rw_rwlock;
        kthread_t *rw_owner;
} krwlock_t;

#define SEM(rwp)                        ((struct rw_semaphore *)(rwp))

static inline void
spl_rw_set_owner(krwlock_t *rwp)
{
        unsigned long flags;

        spl_rwsem_lock_irqsave(&SEM(rwp)->wait_lock, flags);
        rwp->rw_owner = current;
        spl_rwsem_unlock_irqrestore(&SEM(rwp)->wait_lock, flags);
}

static inline void
spl_rw_clear_owner(krwlock_t *rwp)
{
        unsigned long flags;

        spl_rwsem_lock_irqsave(&SEM(rwp)->wait_lock, flags);
        rwp->rw_owner = NULL;
        spl_rwsem_unlock_irqrestore(&SEM(rwp)->wait_lock, flags);
}

static inline kthread_t *
rw_owner(krwlock_t *rwp)
{
        unsigned long flags;
        kthread_t *owner;

        spl_rwsem_lock_irqsave(&SEM(rwp)->wait_lock, flags);
        owner = rwp->rw_owner;
        spl_rwsem_unlock_irqrestore(&SEM(rwp)->wait_lock, flags);

        return owner;
}

static inline int
RW_READ_HELD(krwlock_t *rwp)
{
	return (spl_rwsem_is_locked(SEM(rwp)) && rw_owner(rwp) == NULL);
}

static inline int
RW_WRITE_HELD(krwlock_t *rwp)
{
	return (spl_rwsem_is_locked(SEM(rwp)) && rw_owner(rwp) == current);
}

static inline int
RW_LOCK_HELD(krwlock_t *rwp)
{
	return spl_rwsem_is_locked(SEM(rwp));
}

/*
 * The following functions must be a #define and not static inline.
 * This ensures that the native linux semaphore functions (down/up)
 * will be correctly located in the users code which is important
 * for the built in kernel lock analysis tools
 */
#define rw_init(rwp, name, type, arg)                                   \
({                                                                      \
        static struct lock_class_key __key;                             \
                                                                        \
        __init_rwsem(SEM(rwp), #rwp, &__key);                           \
        spl_rw_clear_owner(rwp);                                        \
})

#define rw_destroy(rwp)                                                 \
({                                                                      \
        VERIFY(!RW_LOCK_HELD(rwp));                                     \
})

#define rw_tryenter(rwp, rw)                                            \
({                                                                      \
        int _rc_ = 0;                                                   \
                                                                        \
        switch (rw) {                                                   \
        case RW_READER:                                                 \
                _rc_ = down_read_trylock(SEM(rwp));                     \
                break;                                                  \
        case RW_WRITER:                                                 \
                if ((_rc_ = down_write_trylock(SEM(rwp))))              \
                        spl_rw_set_owner(rwp);                          \
                break;                                                  \
        default:                                                        \
                VERIFY(0);                                              \
        }                                                               \
        _rc_;                                                           \
})

#define rw_enter(rwp, rw)                                               \
({                                                                      \
        switch (rw) {                                                   \
        case RW_READER:                                                 \
                down_read(SEM(rwp));                                    \
                break;                                                  \
        case RW_WRITER:                                                 \
                down_write(SEM(rwp));                                   \
                spl_rw_set_owner(rwp);                                  \
                break;                                                  \
        default:                                                        \
                VERIFY(0);                                              \
        }                                                               \
})

#define rw_exit(rwp)                                                    \
({                                                                      \
        if (RW_WRITE_HELD(rwp)) {                                       \
                spl_rw_clear_owner(rwp);                                \
                up_write(SEM(rwp));                                     \
        } else {                                                        \
                ASSERT(RW_READ_HELD(rwp));                              \
                up_read(SEM(rwp));                                      \
        }                                                               \
})

#define rw_downgrade(rwp)                                               \
({                                                                      \
        spl_rw_clear_owner(rwp);                                        \
        downgrade_write(SEM(rwp));                                      \
})

#if defined(CONFIG_RWSEM_GENERIC_SPINLOCK)
/*
 * For the generic implementations of rw-semaphores the following is
 * true.  If your semaphore implementation internally represents the
 * semaphore state differently then special case handling is required.
 * - if activity/count is 0 then there are no active readers or writers
 * - if activity/count is +ve then that is the number of active readers
 * - if activity/count is -1 then there is one active writer
 */

extern void __up_read_locked(struct rw_semaphore *);
extern int __down_write_trylock_locked(struct rw_semaphore *);

#define rw_tryupgrade(rwp)                                              \
({                                                                      \
        unsigned long _flags_;                                          \
        int _rc_ = 0;                                                   \
                                                                        \
        spl_rwsem_lock_irqsave(&SEM(rwp)->wait_lock, _flags_);           \
        if ((list_empty(&SEM(rwp)->wait_list)) &&                       \
            (SEM(rwp)->activity == 1)) {                                \
                __up_read_locked(SEM(rwp));                             \
                VERIFY(_rc_ = __down_write_trylock_locked(SEM(rwp)));   \
                (rwp)->rw_owner = current;                              \
        }                                                               \
        spl_rwsem_unlock_irqrestore(&SEM(rwp)->wait_lock, _flags_);      \
        _rc_;                                                           \
})
#else
/*
 * rw_tryupgrade() can be implemented correctly but for each supported
 * arch we will need a custom implementation.  For the x86 implementation
 * it looks like a custom cmpxchg() to atomically check and promote the
 * rwsem would be safe.  For now that's not worth the trouble so in this
 * case rw_tryupgrade() has just been disabled.
 */
#define rw_tryupgrade(rwp)      ({ 0; })
#endif

int spl_rw_init(void);
void spl_rw_fini(void);

#endif /* _SPL_RWLOCK_H */
