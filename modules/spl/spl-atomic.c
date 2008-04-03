#include <sys/atomic.h>

/* Global atomic lock declarations */
spinlock_t atomic64_lock = SPIN_LOCK_UNLOCKED;
spinlock_t atomic32_lock = SPIN_LOCK_UNLOCKED;

EXPORT_SYMBOL(atomic64_lock);
EXPORT_SYMBOL(atomic32_lock);
