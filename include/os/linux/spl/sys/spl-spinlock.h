#ifndef	_SPL_SPIN_H_
#define	_SPL_SPIN_H_

#include <linux/spinlock.h>

typedef spinlock_t	spl_spinlock_t;

static void __maybe_unused spl_spin_init(spl_spinlock_t *l)
{
	spin_lock_init(l);
}

static void __maybe_unused spl_spin_destroy(spl_spinlock_t *l) { }

static void __always_inline __maybe_unused
spl_spin_lock(spl_spinlock_t *l)
{
	spin_lock(l);
}

static void __always_inline __maybe_unused
spl_spin_unlock(spl_spinlock_t *l)
{
	spin_unlock(l);
}

#endif /* _SPL_SPIN_H_ */
