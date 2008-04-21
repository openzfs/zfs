#include <sys/rwlock.h>

#ifdef DEBUG_SUBSYSTEM
#undef DEBUG_SUBSYSTEM
#endif

#define DEBUG_SUBSYSTEM S_RWLOCK

int
__rw_read_held(krwlock_t *rwlp)
{
	ENTRY;
	ASSERT(rwlp->rw_magic == RW_MAGIC);
	RETURN(__rw_lock_held(rwlp) && rwlp->rw_owner == NULL);
}
EXPORT_SYMBOL(__rw_read_held);

int
__rw_write_held(krwlock_t *rwlp)
{
	ENTRY;
	ASSERT(rwlp->rw_magic == RW_MAGIC);
	RETURN(rwlp->rw_owner == current);
}
EXPORT_SYMBOL(__rw_write_held);

int
__rw_lock_held(krwlock_t *rwlp)
{
	int rc = 0;
	ENTRY;

	ASSERT(rwlp->rw_magic == RW_MAGIC);

	spin_lock_irq(&(rwlp->rw_sem.wait_lock));
#ifdef CONFIG_RWSEM_GENERIC_SPINLOCK
	if (rwlp->rw_sem.activity != 0) {
#else
	if (rwlp->rw_sem.count != 0) {
#endif
		rc = 1;
	}

	spin_unlock_irq(&(rwlp->rw_sem.wait_lock));

	RETURN(rc);
}
EXPORT_SYMBOL(__rw_lock_held);
