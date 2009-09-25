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

#include <sys/types.h>
#include <linux/rwsem.h>

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

/*
 * For the generic and x86 implementations of rw-semaphores the following
 * is true.  If your semaphore implementation internally represents the
 * semaphore state differently special case handling will be required.
 * - if activity/count is 0 then there are no active readers or writers
 * - if activity/count is +ve then that is the number of active readers
 * - if activity/count is -1 then there is one active writer
 */
#define SEM(rwp)                        ((struct rw_semaphore *)(rwp))

#if defined(CONFIG_RWSEM_GENERIC_SPINLOCK)
# define RW_COUNT(rwp)                  (SEM(rwp)->activity)
# define rw_exit_locked(rwp)            __up_read_locked(rwp)
# define rw_tryenter_locked(rwp)        __down_write_trylock_locked(rwp)
extern void __up_read_locked(struct rw_semaphore *);
extern int __down_write_trylock_locked(struct rw_semaphore *);
#else
# define RW_COUNT(rwp)                  (SEM(rwp)->count & RWSEM_ACTIVE_MASK)
# define rw_exit_locked(rwp)            up_read(rwp)
# define rw_tryenter_locked(rwp)        down_write_trylock(rwp)
#endif

static inline kthread_t *
spl_rw_get_owner(krwlock_t *rwp)
{
        return rwp->rw_owner;
}

static inline void
spl_rw_set_owner(krwlock_t *rwp)
{
        unsigned long flags;

        spin_lock_irqsave(&SEM(rwp)->wait_lock, flags);
        rwp->rw_owner = current;
        spin_unlock_irqrestore(&SEM(rwp)->wait_lock, flags);
}

static inline void
spl_rw_clear_owner(krwlock_t *rwp)
{
        unsigned long flags;

        spin_lock_irqsave(&SEM(rwp)->wait_lock, flags);
        rwp->rw_owner = NULL;
        spin_unlock_irqrestore(&SEM(rwp)->wait_lock, flags);
}

static inline kthread_t *
rw_owner(krwlock_t *rwp)
{
        unsigned long flags;
        kthread_t *owner;

        spin_lock_irqsave(&SEM(rwp)->wait_lock, flags);
        owner = spl_rw_get_owner(rwp);
        spin_unlock_irqrestore(&SEM(rwp)->wait_lock, flags);

        return owner;
}

static inline int
RW_READ_HELD(krwlock_t *rwp)
{
        unsigned long flags;
        int rc;

        spin_lock_irqsave(&SEM(rwp)->wait_lock, flags);
        rc = ((RW_COUNT(rwp) > 0) && (spl_rw_get_owner(rwp) == NULL));
        spin_unlock_irqrestore(&SEM(rwp)->wait_lock, flags);

        return rc;
}

static inline int
RW_WRITE_HELD(krwlock_t *rwp)
{
        unsigned long flags;
        int rc;

        spin_lock_irqsave(&SEM(rwp)->wait_lock, flags);
        rc = ((RW_COUNT(rwp) < 0) && (spl_rw_get_owner(rwp) == current));
        spin_unlock_irqrestore(&SEM(rwp)->wait_lock, flags);

        return rc;
}

static inline int
RW_LOCK_HELD(krwlock_t *rwp)
{
        unsigned long flags;
        int rc;

        spin_lock_irqsave(&SEM(rwp)->wait_lock, flags);
        rc = (RW_COUNT(rwp) != 0);
        spin_unlock_irqrestore(&SEM(rwp)->wait_lock, flags);

        return rc;
}

/*
 * The following functions must be a #define and not static inline.
 * This ensures that the native linux semaphore functions (down/up)
 * will be correctly located in the users code which is important
 * for the built in kernel lock analysis tools
 */
#define rw_init(rwp, name, type, arg)                                   \
({                                                                      \
        init_rwsem(SEM(rwp));                                           \
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
                SBUG();                                                 \
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
                SBUG();                                                 \
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

#define rw_tryupgrade(rwp)                                              \
({                                                                      \
        unsigned long _flags_;                                          \
        int _rc_ = 0;                                                   \
                                                                        \
        spin_lock_irqsave(&SEM(rwp)->wait_lock, _flags_);               \
        if (list_empty(&SEM(rwp)->wait_list) && (RW_COUNT(rwp) == 1)) { \
                rw_exit_locked(SEM(rwp));                               \
                VERIFY(_rc_ = rw_tryenter_locked(SEM(rwp)));            \
                (rwp)->rw_owner = current;                              \
        }                                                               \
        spin_unlock_irqrestore(&SEM(rwp)->wait_lock, _flags_);          \
        _rc_;                                                           \
})

int spl_rw_init(void);
void spl_rw_fini(void);

#endif /* _SPL_RWLOCK_H */
