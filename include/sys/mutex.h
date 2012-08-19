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

#ifndef _SPL_MUTEX_H
#define _SPL_MUTEX_H

#include <sys/types.h>
#include <linux/mutex.h>
#include <linux/compiler_compat.h>

typedef enum {
        MUTEX_DEFAULT  = 0,
        MUTEX_SPIN     = 1,
        MUTEX_ADAPTIVE = 2
} kmutex_type_t;

#if defined(HAVE_MUTEX_OWNER) && defined(CONFIG_SMP) && !defined(CONFIG_DEBUG_MUTEXES)

/*
 * We define a 1-field struct rather than a straight typedef to enforce type
 * safety.
 */
typedef struct {
        struct mutex m;
} kmutex_t;

static inline kthread_t *
mutex_owner(kmutex_t *mp)
{
#if defined(HAVE_MUTEX_OWNER_TASK_STRUCT)
	return ACCESS_ONCE(mp->m.owner);
#else
	struct thread_info *owner = ACCESS_ONCE(mp->m.owner);
	if (owner)
		return owner->task;

	return NULL;
#endif
}

#define mutex_owned(mp)         (mutex_owner(mp) == current)
#define MUTEX_HELD(mp)          mutex_owned(mp)
#define MUTEX_NOT_HELD(mp)      (!MUTEX_HELD(mp))
#undef mutex_init
#define mutex_init(mp, name, type, ibc)                                 \
({                                                                      \
        static struct lock_class_key __key;                             \
        ASSERT(type == MUTEX_DEFAULT);                                  \
                                                                        \
        __mutex_init(&(mp)->m, #mp, &__key);                            \
})

#undef mutex_destroy
#define mutex_destroy(mp)                                               \
({                                                                      \
        VERIFY3P(mutex_owner(mp), ==, NULL);                            \
})

#define mutex_tryenter(mp)              mutex_trylock(&(mp)->m)
#define mutex_enter(mp)                                                 \
({                                                                      \
        ASSERT3P(mutex_owner(mp), !=, current);				\
        mutex_lock(&(mp)->m);						\
 })
#define mutex_exit(mp)                  mutex_unlock(&(mp)->m)

#ifdef HAVE_GPL_ONLY_SYMBOLS
# define mutex_enter_nested(mp, sc)     mutex_lock_nested(&(mp)->m, sc)
#else
# define mutex_enter_nested(mp, sc)     mutex_enter(mp)
#endif /* HAVE_GPL_ONLY_SYMBOLS */

#else /* HAVE_MUTEX_OWNER */

typedef struct {
        struct mutex m_mutex;
        kthread_t *m_owner;
} kmutex_t;

#ifdef HAVE_TASK_CURR
extern int spl_mutex_spin_max(void);
#else /* HAVE_TASK_CURR */
# define task_curr(owner)       0
# define spl_mutex_spin_max()   0
#endif /* HAVE_TASK_CURR */

#define MUTEX(mp)               (&((mp)->m_mutex))

static inline void
spl_mutex_set_owner(kmutex_t *mp)
{
        mp->m_owner = current;
}

static inline void
spl_mutex_clear_owner(kmutex_t *mp)
{
        mp->m_owner = NULL;
}

#define mutex_owner(mp)         (ACCESS_ONCE((mp)->m_owner))
#define mutex_owned(mp)         (mutex_owner(mp) == current)
#define MUTEX_HELD(mp)          mutex_owned(mp)
#define MUTEX_NOT_HELD(mp)      (!MUTEX_HELD(mp))

/*
 * The following functions must be a #define and not static inline.
 * This ensures that the native linux mutex functions (lock/unlock)
 * will be correctly located in the users code which is important
 * for the built in kernel lock analysis tools
 */
#undef mutex_init
#define mutex_init(mp, name, type, ibc)                                 \
({                                                                      \
        static struct lock_class_key __key;                             \
        ASSERT(type == MUTEX_DEFAULT);                                  \
                                                                        \
        __mutex_init(MUTEX(mp), #mp, &__key);                           \
        spl_mutex_clear_owner(mp);                                      \
})

#undef mutex_destroy
#define mutex_destroy(mp)                                               \
({                                                                      \
        VERIFY3P(mutex_owner(mp), ==, NULL);                            \
})

#define mutex_tryenter(mp)                                              \
({                                                                      \
        int _rc_;                                                       \
                                                                        \
        if ((_rc_ = mutex_trylock(MUTEX(mp))) == 1)                     \
                spl_mutex_set_owner(mp);                                \
                                                                        \
        _rc_;                                                           \
})

/*
 * Adaptive mutexs assume that the lock may be held by a task running
 * on a different cpu.  The expectation is that the task will drop the
 * lock before leaving the head of the run queue.  So the ideal thing
 * to do is spin until we acquire the lock and avoid a context switch.
 * However it is also possible the task holding the lock yields the
 * processor with out dropping lock.  In this case, we know it's going
 * to be a while so we stop spinning and go to sleep waiting for the
 * lock to be available.  This should strike the optimum balance
 * between spinning and sleeping waiting for a lock.
 */
#define mutex_enter(mp)                                                 \
({                                                                      \
        kthread_t *_owner_;                                             \
        int _rc_, _count_;                                              \
                                                                        \
        _rc_ = 0;                                                       \
        _count_ = 0;                                                    \
        _owner_ = mutex_owner(mp);                                      \
        ASSERT3P(_owner_, !=, current);					\
                                                                        \
        while (_owner_ && task_curr(_owner_) &&                         \
               _count_ <= spl_mutex_spin_max()) {                       \
                if ((_rc_ = mutex_trylock(MUTEX(mp))))                  \
                        break;                                          \
                                                                        \
                _count_++;                                              \
        }                                                               \
                                                                        \
        if (!_rc_)                                                      \
                mutex_lock(MUTEX(mp));                                  \
                                                                        \
        spl_mutex_set_owner(mp);                                        \
})

#define mutex_exit(mp)                                                  \
({                                                                      \
        spl_mutex_clear_owner(mp);                                      \
        mutex_unlock(MUTEX(mp));                                        \
})

#ifdef HAVE_GPL_ONLY_SYMBOLS
# define mutex_enter_nested(mp, sc)                                     \
({                                                                      \
        mutex_lock_nested(MUTEX(mp), sc);                               \
        spl_mutex_set_owner(mp);                                        \
})
#else
# define mutex_enter_nested(mp, sc)                                     \
({                                                                      \
        mutex_enter(mp);                                                \
})
#endif

#endif /* HAVE_MUTEX_OWNER */

int spl_mutex_init(void);
void spl_mutex_fini(void);

#endif /* _SPL_MUTEX_H */
