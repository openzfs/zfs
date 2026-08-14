// SPDX-License-Identifier: BSD-2-Clause
/*
 * FreeBSD rmslock (reader-mostly) mapped onto NetBSD krwlock_t.
 *
 * Upstream ZFS treats the teardown lock as reader-recursive: zfs_write
 * holds it while uiomove faults into getpages, which calls zfs_enter
 * again.  NetBSD krwlock_t is not recursive and a queued writer
 * blocks new readers, so a nested rms_rlock deadlocks.  Track
 * per-LWP nest depth and only take the rwlock on the first acquire.
 */

#ifndef	_OPENSOLARIS_SYS_RMLOCK_H_
#define	_OPENSOLARIS_SYS_RMLOCK_H_

#ifdef _KERNEL

#include <sys/rwlock.h>
#include <sys/lwp.h>
#include <sys/specificdata.h>
#include <sys/kmem.h>
#include <sys/debug.h>

#define	RMS_NEST_MAX	8

struct rmslock {
	krwlock_t	rms_lock;
};
typedef struct rmslock rmslock_t;

struct rms_tsd {
	struct {
		struct rmslock	*lock;
		int		depth;
	} slot[RMS_NEST_MAX];
};

extern specificdata_key_t spl_rms_lwp_key;

void	spl_rms_init(void);
void	spl_rms_fini(void);

static inline struct rms_tsd *
rms_tsd(int kmflag)
{
	struct rms_tsd *t;

	t = lwp_getspecific(spl_rms_lwp_key);
	if (t != NULL)
		return (t);
	t = kmem_zalloc(sizeof (*t), kmflag);
	if (t == NULL)
		return (NULL);
	lwp_setspecific(spl_rms_lwp_key, t);
	return (t);
}

static inline int
rms_tsd_find(struct rms_tsd *t, struct rmslock *l)
{
	int i;

	if (t == NULL)
		return (-1);
	for (i = 0; i < RMS_NEST_MAX; i++) {
		if (t->slot[i].lock == l)
			return (i);
	}
	return (-1);
}

static inline int
rms_tsd_add(struct rms_tsd *t, struct rmslock *l)
{
	int i;

	if (t == NULL)
		return (-1);
	for (i = 0; i < RMS_NEST_MAX; i++) {
		if (t->slot[i].lock == NULL) {
			t->slot[i].lock = l;
			t->slot[i].depth = 1;
			return (i);
		}
	}
	return (-1);
}

static inline void
rms_init(struct rmslock *l, const char *n)
{
	(void) n;
	rw_init(&l->rms_lock);
}

static inline void
rms_destroy(struct rmslock *l)
{
	rw_destroy(&l->rms_lock);
}

static inline void
rms_rlock(struct rmslock *l)
{
	struct rms_tsd *t;
	int i;

	/*
	 * Sleeping acquire is never used by the pagedaemon (that
	 * path is rms_try_rlock / zfs_tryenter).  KM_SLEEP matches
	 * Linux rrw_enter_read's TSD node allocation.  Without TSD,
	 * a nested reader takes the non-recursive krwlock_t again
	 * and deadlocks against a queued writer.
	 */
	t = rms_tsd(KM_SLEEP);
	VERIFY(t != NULL);
	i = rms_tsd_find(t, l);
	if (i >= 0) {
		t->slot[i].depth++;
		return;
	}
	rw_enter(&l->rms_lock, RW_READER);
	VERIFY(rms_tsd_add(t, l) >= 0);
}

static inline int
rms_try_rlock(struct rmslock *l)
{
	struct rms_tsd *t;
	int i;

	/*
	 * Pagedaemon / try-enter must not block.  Fail the try if
	 * TSD cannot be allocated: taking the rwlock without
	 * tracking would deadlock a later nested acquire.
	 */
	t = rms_tsd(KM_NOSLEEP);
	if (t == NULL)
		return (0);
	i = rms_tsd_find(t, l);
	if (i >= 0) {
		t->slot[i].depth++;
		return (1);
	}
	if (!rw_tryenter(&l->rms_lock, RW_READER))
		return (0);
	if (rms_tsd_add(t, l) < 0) {
		rw_exit(&l->rms_lock);
		return (0);
	}
	return (1);
}

static inline void
rms_runlock(struct rmslock *l)
{
	struct rms_tsd *t = lwp_getspecific(spl_rms_lwp_key);
	int i = rms_tsd_find(t, l);

	if (i >= 0) {
		if (--t->slot[i].depth > 0)
			return;
		t->slot[i].lock = NULL;
	}
	rw_exit(&l->rms_lock);
}

static inline void
rms_wlock(struct rmslock *l)
{
	rw_enter(&l->rms_lock, RW_WRITER);
}

static inline void
rms_wunlock(struct rmslock *l)
{
	rw_exit(&l->rms_lock);
}

static inline void
rms_unlock(struct rmslock *l)
{
	if (rw_write_held(&l->rms_lock))
		rw_exit(&l->rms_lock);
	else
		rms_runlock(l);
}

#define	rms_rowned(l)		rw_read_held(&(l)->rms_lock)
#define	rms_wowned(l)		rw_write_held(&(l)->rms_lock)
#define	rms_owned_any(l)	rw_lock_held(&(l)->rms_lock)

#endif /* _KERNEL */

#endif /* _OPENSOLARIS_SYS_RMLOCK_H_ */
