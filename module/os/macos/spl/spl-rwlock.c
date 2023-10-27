/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */

/*
 *
 * Copyright (C) 2008 MacZFS
 * Copyright (C) 2013, 2020 Jorgen Lundman <lundman@lundman.net>
 * Copyright (C) 2023 Sean Doran <smd@use.net>
 *
 */

#include <IOKit/IOLib.h>
#include <kern/thread.h>
#include <sys/thread.h>
#include <sys/rwlock.h>
#include <kern/debug.h>
#include <sys/atomic.h>
#include <sys/mutex.h>
#include <mach/mach_types.h>
#include <mach/kern_return.h>
#include <string.h>
#include <sys/debug.h>


static lck_attr_t	*zfs_rwlock_attr = NULL;
static lck_grp_attr_t	*zfs_rwlock_group_attr = NULL;
static lck_grp_t	*zfs_rwlock_group = NULL;

uint64_t zfs_active_rwlock = 0;

#ifdef SPL_DEBUG_RWLOCK
#include <sys/list.h>
static list_t rwlock_list;
static wrapper_mutex_t rwlock_list_mutex;
struct leak {
	list_node_t	rwlock_leak_node;

#define	SPL_DEBUG_RWLOCK_MAXCHAR 32
	char location_file[SPL_DEBUG_RWLOCK_MAXCHAR];
	char location_function[SPL_DEBUG_RWLOCK_MAXCHAR];
	uint64_t location_line;
	void *mp;

	uint64_t wdlist_locktime; // time lock was taken
	char wdlist_file[32]; // storing holder
	uint64_t wdlist_line;
};

#endif

/*
 * We run rwlock with DEBUG on for now, as it protects against
 * uninitialised access etc, and almost no cost.
 */
#ifndef DEBUG
#define	DEBUG
#endif

#ifdef DEBUG
int
rw_isinit(krwlock_t *rwlp)
{
	if (rwlp->rw_pad != 0x012345678)
		return (0);
	return (1);
}
#endif


#ifdef SPL_DEBUG_RWLOCK

static void
rwlist_mutex_enter(wrapper_mutex_t *mtxp)
{
	spl_data_barrier();
	lck_mtx_lock((lck_mtx_t *)mtxp);
	spl_data_barrier();
}

static void
rwlist_mutex_exit(wrapper_mutex_t *mtxp)
{
	spl_data_barrier();
	lck_mtx_unlock((lck_mtx_t *)mtxp);
}

void
rw_initx(krwlock_t *rwlp, char *name, krw_type_t type, __unused void *arg,
    const char *file, const char *fn, int line)
#else
void
rw_init(krwlock_t *rwlp, char *name, krw_type_t type, __unused void *arg)
#endif
{
	ASSERT(type != RW_DRIVER);

#ifdef DEBUG
	VERIFY3U(rwlp->rw_pad, !=, 0x012345678);
#endif

	lck_rw_init((lck_rw_t *)&rwlp->rw_lock[0],
	    zfs_rwlock_group, zfs_rwlock_attr);
	rwlp->rw_owner = NULL;
	rwlp->rw_readers = 0;
#ifdef DEBUG
	rwlp->rw_pad = 0x012345678;
#endif
	atomic_inc_64(&zfs_active_rwlock);

#ifdef SPL_DEBUG_RWLOCK
	struct leak *leak;

	leak = IOMallocType(struct leak);

	if (leak) {
		memset(leak, 0, sizeof (struct leak));
		strlcpy(leak->location_file, file, SPL_DEBUG_RWLOCK_MAXCHAR);
		strlcpy(leak->location_function, fn, SPL_DEBUG_RWLOCK_MAXCHAR);
		leak->location_line = line;
		leak->mp = rwlp;

		rwlist_mutex_enter(&rwlock_list_mutex);
		list_link_init(&leak->rwlock_leak_node);
		list_insert_tail(&rwlock_list, leak);
		rwlp->leak = leak;
		rwlist_mutex_exit(&rwlock_list_mutex);
	}
	leak->wdlist_locktime = 0;
	leak->wdlist_file[0] = 0;
	leak->wdlist_line = 0;
#endif
}

void
rw_destroy(krwlock_t *rwlp)
{
#ifdef DEBUG
	VERIFY3U(rwlp->rw_pad, ==, 0x012345678);
#endif

	lck_rw_destroy((lck_rw_t *)&rwlp->rw_lock[0], zfs_rwlock_group);
#ifdef DEBUG
	rwlp->rw_pad = 0x99;
#endif
	atomic_dec_64(&zfs_active_rwlock);
	ASSERT3P(atomic_load_nonatomic(&rwlp->rw_owner), ==, NULL);
	ASSERT3U(atomic_load_nonatomic(&rwlp->rw_readers), ==, 0);

#ifdef SPL_DEBUG_RWLOCK
	if (rwlp->leak) {
		struct leak *leak = (struct leak *)rwlp->leak;
		rwlist_mutex_enter(&rwlock_list_mutex);
		list_remove(&rwlock_list, leak);
		rwlp->leak = NULL;
		rwlist_mutex_exit(&rwlock_list_mutex);
		IOFreeType(leak, struct leak);
	}
#endif
}

void
rw_enter(krwlock_t *rwlp, krw_t rw)
{
#ifdef DEBUG
	if (rwlp->rw_pad != 0x012345678)
		panic("rwlock %p not initialised\n", rwlp);
#endif

	if (rw == RW_READER) {
		spl_data_barrier();
		lck_rw_lock_shared((lck_rw_t *)&rwlp->rw_lock[0]);
		spl_data_barrier();
		atomic_inc_32((volatile uint32_t *)&rwlp->rw_readers);
		ASSERT3P(atomic_load_nonatomic(&rwlp->rw_owner), ==, NULL);
	} else {
		if (atomic_load_nonatomic(&rwlp->rw_owner) == current_thread())
			panic("rw_enter: locking against myself!");
		spl_data_barrier();
		lck_rw_lock_exclusive((lck_rw_t *)&rwlp->rw_lock[0]);
		spl_data_barrier();
		ASSERT3P(rwlp->rw_owner, ==, NULL);
		ASSERT3U(rwlp->rw_readers, ==, 0);
		atomic_store_nonatomic(&rwlp->rw_owner, current_thread());
	}
}

/*
 * kernel private from osfmk/kern/locks.h
 */
extern boolean_t lck_rw_try_lock(lck_rw_t *lck, lck_rw_type_t lck_rw_type);

int
rw_tryenter(krwlock_t *rwlp, krw_t rw)
{
	int held = 0;

#ifdef DEBUG
	if (rwlp->rw_pad != 0x012345678)
		panic("rwlock %p not initialised\n", rwlp);
#endif

	if (rw == RW_READER) {
		spl_data_barrier();
		held = lck_rw_try_lock((lck_rw_t *)&rwlp->rw_lock[0],
		    LCK_RW_TYPE_SHARED);
		if (held) {
			spl_data_barrier();
			atomic_inc_32((volatile uint32_t *)&rwlp->rw_readers);
		}
	} else {
		if (atomic_load_nonatomic(&rwlp->rw_owner) == current_thread())
			panic("rw_tryenter: locking against myself!");
		spl_data_barrier();
		held = lck_rw_try_lock((lck_rw_t *)&rwlp->rw_lock[0],
		    LCK_RW_TYPE_EXCLUSIVE);
		if (held) {
			spl_data_barrier();
			atomic_store_nonatomic(&rwlp->rw_owner,
			    current_thread());
		}
	}

	return (held);
}

/*
 * It appears a difference between Darwin's
 * lck_rw_lock_shared_to_exclusive() and Solaris's rw_tryupgrade() and
 * FreeBSD's sx_try_upgrade() is that on failure to upgrade, the prior
 * held shared/reader lock is lost on Darwin, but retained on
 * Solaris/FreeBSD. We could re-acquire the lock in this situation,
 * but it enters a possibility of blocking, when tryupgrade is meant
 * to be non-blocking.
 * Also note that XNU's lck_rw_lock_shared_to_exclusive() is always
 * blocking (when waiting on readers), which means we can not use it.
 */
int
rw_tryupgrade(krwlock_t *rwlp)
{
	int held = 0;

	if (atomic_load_nonatomic(&rwlp->rw_owner) == current_thread())
		panic("rw_enter: locking against myself!");

	/* More readers than us? give up */
	if (atomic_load_nonatomic(&rwlp->rw_readers) != 1)
		return (0);

	/*
	 * It is ON. We need to drop our READER lock, and try to
	 * grab the WRITER as quickly as possible.
	 */
	atomic_dec_32((volatile uint32_t *)&rwlp->rw_readers);
	lck_rw_unlock_shared((lck_rw_t *)&rwlp->rw_lock[0]);

	/* Grab the WRITER lock */
	held = lck_rw_try_lock((lck_rw_t *)&rwlp->rw_lock[0],
	    LCK_RW_TYPE_EXCLUSIVE);

	if (held) {
		/*
		 * Looks like we won the exclusive lock.
		 * If we are on a relaxed memory ordering system,
		 * we need a barrier here anyway, which will publish
		 * the rw_owner write
		 */
		rwlp->rw_owner = current_thread();
		spl_data_barrier();
		ASSERT3U(rwlp->rw_readers, ==, 0);
		return (1);
	}

	/*
	 * The worst has happened, we failed to grab WRITE lock, either
	 * due to another WRITER lock, or, some READER came along.
	 * IllumOS implementation returns with the READER lock again
	 * so we need to grab it.
	 */
	rw_enter(rwlp, RW_READER);
	return (0);

}

void
rw_exit(krwlock_t *rwlp)
{
	if (rwlp->rw_owner == current_thread()) {
		ASSERT3U(atomic_load_nonatomic(&rwlp->rw_readers), ==, 0);
		atomic_store_nonatomic(&rwlp->rw_owner, NULL);
		lck_rw_unlock_exclusive((lck_rw_t *)&rwlp->rw_lock[0]);
	} else {
		ASSERT3P(atomic_load_nonatomic(&rwlp->rw_owner), ==, NULL);
		atomic_dec_32((volatile uint32_t *)&rwlp->rw_readers);
		lck_rw_unlock_shared((lck_rw_t *)&rwlp->rw_lock[0]);
	}
}

int
rw_lock_held(krwlock_t *rwlp)
{
	/*
	 * ### not sure about this one ###
	 */
	return (atomic_load_nonatomic(&rwlp->rw_owner) == current_thread() ||
	    atomic_load_nonatomic(&rwlp->rw_readers) > 0);
}

int
rw_read_held(krwlock_t *rwlp)
{
	return (rw_lock_held(rwlp) &&
	    atomic_load_nonatomic(&rwlp->rw_owner) == NULL);
}

int
rw_write_held(krwlock_t *rwlp)
{
	return (atomic_load_nonatomic(&rwlp->rw_owner) == current_thread());
}

void
rw_downgrade(krwlock_t *rwlp)
{
	if (atomic_load_nonatomic(&rwlp->rw_owner) != current_thread())
		panic("SPL: rw_downgrade not WRITE lock held\n");
	atomic_store_nonatomic(&rwlp->rw_owner, NULL);
	lck_rw_lock_exclusive_to_shared((lck_rw_t *)&rwlp->rw_lock[0]);
	spl_data_barrier();
	atomic_inc_32((volatile uint32_t *)&rwlp->rw_readers);
}

int
spl_rwlock_init(void)
{
	zfs_rwlock_attr = lck_attr_alloc_init();
	zfs_rwlock_group_attr = lck_grp_attr_alloc_init();
	zfs_rwlock_group = lck_grp_alloc_init("zfs-rwlock",
	    zfs_rwlock_group_attr);

#ifdef SPL_DEBUG_RWLOCK
	list_create(&rwlock_list, sizeof (struct leak),
	    offsetof(struct leak, rwlock_leak_node));
	lck_mtx_init((lck_mtx_t *)&rwlock_list_mutex,
	    zfs_rwlock_group, zfs_rwlock_attr);
#endif

	return (0);
}

void
spl_rwlock_fini(void)
{

#ifdef SPL_DEBUG_RWLOCK
	uint64_t total = 0;
	printf("SPL: %s:%d: Dumping leaked rwlock allocations..."
	    " zfs_active_rwlock == %llu\n",
	    __func__, __LINE__, atomic_load_64(&zfs_active_rwlock));

	rwlist_mutex_enter(&rwlock_list_mutex);
	while (1) {
		struct leak *leak, *runner;
		uint32_t found;

		leak = list_head(&rwlock_list);

		if (leak) {
			list_remove(&rwlock_list, leak);
		}
		if (!leak) break;

		// Run through list and count up how many times this leak is
		// found, removing entries as we go.
		for (found = 1, runner = list_head(&rwlock_list);
		    runner;
		    runner = runner ? list_next(&rwlock_list, runner) :
		    list_head(&rwlock_list)) {

			if (strcmp(leak->location_file, runner->location_file)
			    == 0 &&
			    strcmp(leak->location_function,
			    runner->location_function) == 0 &&
			    leak->location_line == runner->location_line) {
				// Same place
				found++;
				list_remove(&rwlock_list, runner);
				IOFreeType(runner, struct leak);
				runner = NULL;
			} // if same

		} // for all nodes

		printf("SPL: %s:%d  rwlock %p : %s %s %llu : # leaks: %u\n",
		    __func__, __LINE__,
		    leak->mp,
		    leak->location_file,
		    leak->location_function,
		    leak->location_line,
		    found);

		IOFreeType(leak, struct leak);
		total += found;

	}
	rwlist_mutex_exit(&rwlock_list_mutex);
	printf("SPL: %s:%d: Dumped %llu leaked allocations.\n",
	    __func__, __LINE__, total);

	lck_mtx_destroy((lck_mtx_t *)&rwlock_list_mutex,
	    zfs_rwlock_group);
	list_destroy(&rwlock_list);
#endif

	lck_grp_free(zfs_rwlock_group);
	zfs_rwlock_group = NULL;

	lck_grp_attr_free(zfs_rwlock_group_attr);
	zfs_rwlock_group_attr = NULL;

	lck_attr_free(zfs_rwlock_attr);
	zfs_rwlock_attr = NULL;

	if (atomic_load_64(&zfs_active_rwlock) != 0)
		printf("SPL: %s:%d, zfs_active_wrlock is %llu\n",
		    __func__, __LINE__, atomic_load_64(&zfs_active_rwlock));
	else
		printf("SPL: %s: good, zero zfs_active_wrlock\n",  __func__);

}
