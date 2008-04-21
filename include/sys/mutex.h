#ifndef _SPL_MUTEX_H
#define	_SPL_MUTEX_H

#ifdef	__cplusplus
extern "C" {
#endif

#include <linux/module.h>
#include <linux/hardirq.h>
#include <sys/types.h>

/* See the "Big Theory Statement" in solaris mutex.c.
 *
 * Spin mutexes apparently aren't needed by zfs so we assert
 * if ibc is non-zero.
 *
 * Our impementation of adaptive mutexes aren't really adaptive.
 * They go to sleep every time.
 */

#define MUTEX_DEFAULT		0
#define MUTEX_HELD(x)           (mutex_owned(x))

#define KM_MAGIC		0x42424242
#define KM_POISON		0x84

typedef struct {
	int km_magic;
	char *km_name;
	struct task_struct *km_owner;
	struct semaphore km_sem;
	spinlock_t km_lock;
} kmutex_t;

#undef mutex_init
static __inline__ void
mutex_init(kmutex_t *mp, char *name, int type, void *ibc)
{
	ASSERT(mp);
	ASSERT(ibc == NULL);		/* XXX - Spin mutexes not needed */
	ASSERT(type == MUTEX_DEFAULT);	/* XXX - Only default type supported */

	mp->km_magic = KM_MAGIC;
	spin_lock_init(&mp->km_lock);
	sema_init(&mp->km_sem, 1);
	mp->km_owner = NULL;
	mp->km_name = NULL;

	if (name) {
		mp->km_name = kmalloc(strlen(name) + 1, GFP_KERNEL);
		if (mp->km_name)
			strcpy(mp->km_name, name);
	}
}

#undef mutex_destroy
static __inline__ void
mutex_destroy(kmutex_t *mp)
{
	ASSERT(mp);
	ASSERT(mp->km_magic == KM_MAGIC);
	spin_lock(&mp->km_lock);

	if (mp->km_name)
		kfree(mp->km_name);

	memset(mp, KM_POISON, sizeof(*mp));
	spin_unlock(&mp->km_lock);
}

static __inline__ void
mutex_enter(kmutex_t *mp)
{
	ASSERT(mp);
	ASSERT(mp->km_magic == KM_MAGIC);
	spin_lock(&mp->km_lock);

	if (unlikely(in_atomic() && !current->exit_state)) {
		printk("May schedule while atomic: %s/0x%08x/%d\n",
		       current->comm, preempt_count(), current->pid);
		spin_unlock(&mp->km_lock);
		BUG();
	}

	spin_unlock(&mp->km_lock);

	down(&mp->km_sem);

	spin_lock(&mp->km_lock);
	ASSERT(mp->km_owner == NULL);
	mp->km_owner = current;
	spin_unlock(&mp->km_lock);
}

/* Return 1 if we acquired the mutex, else zero.  */
static __inline__ int
mutex_tryenter(kmutex_t *mp)
{
	int rc;

	ASSERT(mp);
	ASSERT(mp->km_magic == KM_MAGIC);
	spin_lock(&mp->km_lock);

	if (unlikely(in_atomic() && !current->exit_state)) {
		printk("May schedule while atomic: %s/0x%08x/%d\n",
		       current->comm, preempt_count(), current->pid);
		spin_unlock(&mp->km_lock);
		BUG();
	}

	spin_unlock(&mp->km_lock);
	rc = down_trylock(&mp->km_sem); /* returns 0 if acquired */
	if (rc == 0) {
		spin_lock(&mp->km_lock);
		ASSERT(mp->km_owner == NULL);
		mp->km_owner = current;
		spin_unlock(&mp->km_lock);
		return 1;
	}
	return 0;
}

static __inline__ void
mutex_exit(kmutex_t *mp)
{
	ASSERT(mp);
	ASSERT(mp->km_magic == KM_MAGIC);
	spin_lock(&mp->km_lock);

	ASSERT(mp->km_owner == current);
	mp->km_owner = NULL;
	spin_unlock(&mp->km_lock);
	up(&mp->km_sem);
}

/* Return 1 if mutex is held by current process, else zero.  */
static __inline__ int
mutex_owned(kmutex_t *mp)
{
	int rc;

	ASSERT(mp);
	ASSERT(mp->km_magic == KM_MAGIC);
	spin_lock(&mp->km_lock);
	rc = (mp->km_owner == current);
	spin_unlock(&mp->km_lock);

	return rc;
}

/* Return owner if mutex is owned, else NULL.  */
static __inline__ kthread_t *
mutex_owner(kmutex_t *mp)
{
	kthread_t *thr;

	ASSERT(mp);
	ASSERT(mp->km_magic == KM_MAGIC);
	spin_lock(&mp->km_lock);
	thr = mp->km_owner;
	spin_unlock(&mp->km_lock);

	return thr;
}

#ifdef	__cplusplus
}
#endif

#endif	/* _SPL_MUTEX_H */
