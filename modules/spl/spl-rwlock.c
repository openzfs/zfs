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
	int rc = 0;

	BUG_ON(rwlp->rw_magic != RW_MAGIC);

	spin_lock_irq(&(rwlp->rw_sem.wait_lock));
#ifdef CONFIG_RWSEM_GENERIC_SPINLOCK
	if (rwlp->rw_sem.activity != 0) {
#else
	if (rwlp->rw_sem.count != 0) {
#endif
		rc = 1;
	}

	spin_unlock_irq(&(rwlp->rw_sem.wait_lock));

	return rc;
}
EXPORT_SYMBOL(__rw_lock_held);
