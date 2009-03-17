#ifndef _SPL_ATOMIC_COMPAT_H
#define _SPL_ATOMIC_COMPAT_H

#include <asm/atomic.h>
#include <spl_config.h>

#ifndef HAVE_ATOMIC64_T
#include <linux/spinlock.h>

typedef struct {
	spinlock_t lock;
	__s64 val;
} atomic64_t;

#define ATOMIC64_INIT(i) { .lock = SPIN_LOCK_UNLOCKED, .val = (i) }

static inline void atomic64_add(__s64 i, atomic64_t *v)
{
	unsigned long flags;

	spin_lock_irqsave(&v->lock, flags);
	v->val += i;
	spin_unlock_irqrestore(&v->lock, flags);
}

static inline void atomic64_sub(__s64 i, atomic64_t *v)
{
	unsigned long flags;

	spin_lock_irqsave(&v->lock, flags);
	v->val -= i;
	spin_unlock_irqrestore(&v->lock, flags);
}

static inline __s64 atomic64_read(atomic64_t *v)
{
	unsigned long flags;
	__s64 r;

	spin_lock_irqsave(&v->lock, flags);
	r = v->val;
	spin_unlock_irqrestore(&v->lock, flags);

	return r;
}

static inline void atomic64_set(atomic64_t *v, __s64 i)
{
	unsigned long flags;

	spin_lock_irqsave(&v->lock, flags);
	v->val = i;
	spin_unlock_irqrestore(&v->lock, flags);
}

#endif /* HAVE_ATOMIC64_T */

#endif /* _SPL_ATOMIC_COMPAT_H */

