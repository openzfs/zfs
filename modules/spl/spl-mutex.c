#include <sys/mutex.h>

#ifdef DEBUG_SUBSYSTEM
#undef DEBUG_SUBSYSTEM
#endif

#define DEBUG_SUBSYSTEM S_MUTEX

/* Mutex implementation based on those found in Solaris.  This means
 * they the MUTEX_DEFAULT type is an adaptive mutex.  When calling
 * mutex_enter() your process will spin waiting for the lock if it's
 * likely the lock will be free'd shortly.  If it looks like the
 * lock will be held for a longer time we schedule and sleep waiting
 * for it.  This determination is made by checking if the holder of
 * the lock is currently running on cpu or sleeping waiting to be
 * scheduled.  If the holder is currently running it's likely the
 * lock will be shortly dropped.
 *
 * XXX: This is basically a rough implementation to see if this
 * helps our performance.  If it does a more careful implementation
 * should be done, perhaps in assembly.
 */

/*  0:         Never spin when trying to aquire lock
 * -1:         Spin until aquired or holder yeilds without dropping lock
 *  1-MAX_INT: Spin for N attempts before sleeping for lock
 */
int mutex_spin_max = 100;

#ifdef DEBUG_MUTEX
int mutex_stats[MUTEX_STATS_SIZE] = { 0 };
DEFINE_MUTEX(mutex_stats_lock);
LIST_HEAD(mutex_stats_list);
#endif

void
__spl_mutex_init(kmutex_t *mp, char *name, int type, void *ibc)
{
	ASSERT(mp);
	ASSERT(name);
	ASSERT(ibc == NULL);
	ASSERT(mp->km_magic != KM_MAGIC); /* Never double init */

	mp->km_magic = KM_MAGIC;
	mp->km_owner = NULL;
	mp->km_name = NULL;
	mp->km_name_size = strlen(name) + 1;

	switch (type) {
		case MUTEX_DEFAULT:
			mp->km_type = MUTEX_ADAPTIVE;
			break;
		case MUTEX_SPIN:
		case MUTEX_ADAPTIVE:
			mp->km_type = type;
			break;
		default:
			SBUG();
	}

	/* Semaphore kmem_alloc'ed to keep struct size down (<64b) */
	mp->km_sem = kmem_alloc(sizeof(struct semaphore), KM_SLEEP);
	if (mp->km_sem == NULL)
		return;

	mp->km_name = kmem_alloc(mp->km_name_size, KM_SLEEP);
	if (mp->km_name == NULL) {
		kmem_free(mp->km_sem, sizeof(struct semaphore));
		return;
	}

	sema_init(mp->km_sem, 1);
	strcpy(mp->km_name, name);

#ifdef DEBUG_MUTEX
	mp->km_stats = kmem_zalloc(sizeof(int) * MUTEX_STATS_SIZE, KM_SLEEP);
        if (mp->km_stats == NULL) {
		kmem_free(mp->km_name, mp->km_name_size);
		kmem_free(mp->km_sem, sizeof(struct semaphore));
		return;
	}

	mutex_lock(&mutex_stats_lock);
	list_add_tail(&mp->km_list, &mutex_stats_list);
	mutex_unlock(&mutex_stats_lock);
#endif
}
EXPORT_SYMBOL(__spl_mutex_init);

void
__spl_mutex_destroy(kmutex_t *mp)
{
	ASSERT(mp);
	ASSERT(mp->km_magic == KM_MAGIC);

#ifdef DEBUG_MUTEX
	mutex_lock(&mutex_stats_lock);
	list_del_init(&mp->km_list);
	mutex_unlock(&mutex_stats_lock);

	kmem_free(mp->km_stats, sizeof(int) * MUTEX_STATS_SIZE);
#endif
	kmem_free(mp->km_name, mp->km_name_size);
	kmem_free(mp->km_sem, sizeof(struct semaphore));

	memset(mp, KM_POISON, sizeof(*mp));
}
EXPORT_SYMBOL(__spl_mutex_destroy);

/* Return 1 if we acquired the mutex, else zero.  */
int
__mutex_tryenter(kmutex_t *mp)
{
	int rc;
	ENTRY;

	ASSERT(mp);
	ASSERT(mp->km_magic == KM_MAGIC);
	MUTEX_STAT_INC(mutex_stats, MUTEX_TRYENTER_TOTAL);
	MUTEX_STAT_INC(mp->km_stats, MUTEX_TRYENTER_TOTAL);

	rc = down_trylock(mp->km_sem);
	if (rc == 0) {
		ASSERT(mp->km_owner == NULL);
		mp->km_owner = current;
		MUTEX_STAT_INC(mutex_stats, MUTEX_TRYENTER_NOT_HELD);
		MUTEX_STAT_INC(mp->km_stats, MUTEX_TRYENTER_NOT_HELD);
	}

	RETURN(!rc);
}
EXPORT_SYMBOL(__mutex_tryenter);

static void
mutex_enter_adaptive(kmutex_t *mp)
{
	struct task_struct *owner;
	int count = 0;

	/* Lock is not held so we expect to aquire the lock */
	if ((owner = mp->km_owner) == NULL) {
		down(mp->km_sem);
		MUTEX_STAT_INC(mutex_stats, MUTEX_ENTER_NOT_HELD);
		MUTEX_STAT_INC(mp->km_stats, MUTEX_ENTER_NOT_HELD);
	} else {
		/* The lock is held by a currently running task which
		 * we expect will drop the lock before leaving the
		 * head of the runqueue.  So the ideal thing to do
		 * is spin until we aquire the lock and avoid a
		 * context switch.  However it is also possible the
		 * task holding the lock yields the processor with
		 * out dropping lock.  In which case, we know it's
		 * going to be a while so we stop spinning and go
		 * to sleep waiting for the lock to be available.
		 * This should strike the optimum balance between
		 * spinning and sleeping waiting for a lock.
		 */
		while (task_curr(owner) && (count <= mutex_spin_max)) {
			if (down_trylock(mp->km_sem) == 0) {
				MUTEX_STAT_INC(mutex_stats, MUTEX_ENTER_SPIN);
				MUTEX_STAT_INC(mp->km_stats, MUTEX_ENTER_SPIN);
				GOTO(out, count);
			}
			count++;
		}

		/* The lock is held by a sleeping task so it's going to
		 * cost us minimally one context switch.  We might as
		 * well sleep and yield the processor to other tasks.
		 */
		down(mp->km_sem);
		MUTEX_STAT_INC(mutex_stats, MUTEX_ENTER_SLEEP);
		MUTEX_STAT_INC(mp->km_stats, MUTEX_ENTER_SLEEP);
	}
out:
	MUTEX_STAT_INC(mutex_stats, MUTEX_ENTER_TOTAL);
	MUTEX_STAT_INC(mp->km_stats, MUTEX_ENTER_TOTAL);
}

void
__mutex_enter(kmutex_t *mp)
{
	ENTRY;
	ASSERT(mp);
	ASSERT(mp->km_magic == KM_MAGIC);

	switch (mp->km_type) {
		case MUTEX_SPIN:
			while (down_trylock(mp->km_sem));
			MUTEX_STAT_INC(mutex_stats, MUTEX_ENTER_SPIN);
			MUTEX_STAT_INC(mp->km_stats, MUTEX_ENTER_SPIN);
			break;
		case MUTEX_ADAPTIVE:
			mutex_enter_adaptive(mp);
			break;
	}

	ASSERT(mp->km_owner == NULL);
	mp->km_owner = current;

	EXIT;
}
EXPORT_SYMBOL(__mutex_enter);

void
__mutex_exit(kmutex_t *mp)
{
	ENTRY;
	ASSERT(mp);
	ASSERT(mp->km_magic == KM_MAGIC);
	ASSERT(mp->km_owner == current);
	mp->km_owner = NULL;
	up(mp->km_sem);
	EXIT;
}
EXPORT_SYMBOL(__mutex_exit);

/* Return 1 if mutex is held by current process, else zero.  */
int
__mutex_owned(kmutex_t *mp)
{
	ENTRY;
	ASSERT(mp);
	ASSERT(mp->km_magic == KM_MAGIC);
	RETURN(mp->km_owner == current);
}
EXPORT_SYMBOL(__mutex_owned);

/* Return owner if mutex is owned, else NULL.  */
kthread_t *
__spl_mutex_owner(kmutex_t *mp)
{
	ENTRY;
	ASSERT(mp);
	ASSERT(mp->km_magic == KM_MAGIC);
	RETURN(mp->km_owner);
}
EXPORT_SYMBOL(__spl_mutex_owner);

int
spl_mutex_init(void)
{
	ENTRY;
	RETURN(0);
}

void
spl_mutex_fini(void)
{
        ENTRY;
#ifdef DEBUG_MUTEX
	ASSERT(list_empty(&mutex_stats_list));
#endif
        EXIT;
}

