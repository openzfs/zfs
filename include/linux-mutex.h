#ifndef _SYS_LINUX_MUTEX_H
#define	_SYS_LINUX_MUTEX_H

#ifdef	__cplusplus
extern "C" {
#endif

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
} kmutex_t;

#undef mutex_init
static __inline__ void
mutex_init(kmutex_t *mp, char *name, int type, void *ibc)
{
	BUG_ON(ibc != NULL);		/* XXX - Spin mutexes not needed? */
	BUG_ON(type != MUTEX_DEFAULT);	/* XXX - Only default type supported? */

	mp->km_magic = KM_MAGIC;
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
	BUG_ON(mp->km_magic != KM_MAGIC);

	if (mp->km_name)
		kfree(mp->km_name);

	memset(mp, KM_POISON, sizeof(*mp));
}

static __inline__ void
mutex_enter(kmutex_t *mp)
{
	BUG_ON(mp->km_magic != KM_MAGIC);
	down(&mp->km_sem);  /* Will check in_atomic() for us */
	BUG_ON(mp->km_owner != NULL);
	mp->km_owner = current;
}

/* Return 1 if we acquired the mutex, else zero.
 */
static __inline__ int
mutex_tryenter(kmutex_t *mp)
{
	int result;

	BUG_ON(mp->km_magic != KM_MAGIC);
	result = down_trylock(&mp->km_sem); /* returns 0 if acquired */
	if (result == 0) {
		BUG_ON(mp->km_owner != NULL);
		mp->km_owner = current;
		return 1;
	}
	return 0;
}

static __inline__ void
mutex_exit(kmutex_t *mp)
{
	BUG_ON(mp->km_magic != KM_MAGIC);
	BUG_ON(mp->km_owner != current);
	mp->km_owner = NULL;
	up(&mp->km_sem);
}

/* Return 1 if mutex is held by current process, else zero.
 */
static __inline__ int
mutex_owned(kmutex_t *mp)
{
	BUG_ON(mp->km_magic != KM_MAGIC);
	return (mp->km_owner == current);
}

/* Return owner if mutex is owned, else NULL.
 */
static __inline__ kthread_t *
mutex_owner(kmutex_t *mp)
{
	BUG_ON(mp->km_magic != KM_MAGIC);
	return mp->km_owner;
}

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_LINUX_MUTEX_H */
