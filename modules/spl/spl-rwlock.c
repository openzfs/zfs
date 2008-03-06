#include <sys/rwlock.h>

int
__rw_read_held(krwlock_t *rwlp)
{
	BUG_ON(rwlp->rw_magic != RW_MAGIC);

	if (__rw_lock_held(rwlp) && rwlp->rw_owner == NULL) {
		return 1;
	}

	return 0;
}
EXPORT_SYMBOL(__rw_read_held);

int
__rw_write_held(krwlock_t *rwlp)
{
	BUG_ON(rwlp->rw_magic != RW_MAGIC);

	if (rwlp->rw_owner == current) {
		return 1;
	}

	return 0;
}
EXPORT_SYMBOL(__rw_write_held);

int
__rw_lock_held(krwlock_t *rwlp)
{
	BUG_ON(rwlp->rw_magic != RW_MAGIC);

#ifdef CONFIG_RWSEM_GENERIC_SPINLOCK
	if (rwlp->rw_sem.activity != 0) {
#else
	if (rwlp->rw_sem.count != 0) {
#endif
		return 1;
	}

	return 0;
}
EXPORT_SYMBOL(__rw_lock_held);
