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
 * Copyright (C) 2013,2020 Jorgen Lundman <lundman@lundman.net>
 * Copyright (C) 2023 Sean Doran <smd@use.net>
 *
 */

#include <IOKit/IOLib.h>
#include <sys/mutex.h>
#include <sys/atomic.h>
#include <mach/mach_types.h>
#include <mach/kern_return.h>
#include <kern/thread.h>
#include <string.h>
#include <sys/debug.h>
#include <kern/debug.h>
#include <sys/thread.h>

// Not defined in headers
extern boolean_t lck_mtx_try_lock(lck_mtx_t *lck);

/*
 * SPL mutexes: use the XNU interface, rather than the ones below,
 * initialized in spl-osx.c and used in spl-thread.c
 */
lck_grp_attr_t	*spl_mtx_grp_attr;
lck_attr_t	*spl_mtx_lck_attr;
lck_grp_t	*spl_mtx_grp;

static lck_attr_t	*zfs_lock_attr = NULL;
static lck_grp_attr_t	*zfs_group_attr = NULL;

static lck_grp_t *zfs_mutex_group = NULL;

uint64_t zfs_active_mutex = 0;

#ifdef SPL_DEBUG_MUTEX
#include <sys/list.h>
static list_t mutex_list;
static wrapper_mutex_t mutex_list_mtx;
static uint64_t mutex_list_wait_loc;


struct leak {
	list_node_t	mutex_leak_node;

#define	SPL_DEBUG_MUTEX_MAXCHAR_FUNC 24
#define	SPL_DEBUG_MUTEX_MAXCHAR_FILE 40 /* __FILE__ may have ../../... */

	char		last_locked_file[SPL_DEBUG_MUTEX_MAXCHAR_FILE];
	char		last_locked_function[SPL_DEBUG_MUTEX_MAXCHAR_FUNC];
	int		last_locked_line;
	void		*mp;

	uint64_t	locktime;	// time lock was taken
	hrtime_t	mutex_created_time;
	char		creation_file[SPL_DEBUG_MUTEX_MAXCHAR_FILE];
	char		creation_function[SPL_DEBUG_MUTEX_MAXCHAR_FUNC];
	int		creation_line;
	uint64_t	total_lock_count;
	uint64_t	total_trylock_success;
	uint64_t	total_trylock_miss;
	uint32_t	period_lock_count;
	uint32_t	period_trylock_miss;
};

static int wdlist_exit = 0;

void
spl_wdlist_settime(void *mpleak, uint64_t value)
{
	struct leak *leak = (struct leak *)mpleak;
	VERIFY3P(leak, !=, NULL);
	atomic_store_nonatomic(&leak->locktime, value);
}

inline static void
spl_wdlist_check(void *ignored)
{
	struct leak *mp;
	uint64_t prev_noe = gethrestime_sec(); /* we time in seconds */

	printf("SPL: Mutex watchdog is alive\n");

	lck_mtx_lock((lck_mtx_t *)&mutex_list_mtx);

	while (!wdlist_exit) {

		struct timespec ts = { .tv_sec = SPL_MUTEX_WATCHDOG_SLEEP };
		int msleep_result;

		/* only this thread can modify these */
		static uint32_t period_lock_record_holder = 0;
		static uint32_t period_miss_record_holder = 0;

		msleep_result = msleep((caddr_t)&mutex_list_wait_loc,
		    (lck_mtx_t *)&mutex_list_mtx, PRIBIO,
		    "mutex watchdog napping",
		    &ts);

		/*
		 * this assertion will fail deliberately (0 == 35)
		 * when this thread is woken up by
		 * spl_mutex_subsystem_fini(), but it's also good to
		 * know if anything other than EAGAIN is seen before
		 * then, for now.  (this *is* SPL_DEBUG_MUTEX after all :-) )
		 */
		ASSERT3S(msleep_result, ==, EAGAIN);

		spl_data_barrier();

		uint64_t noe = gethrestime_sec();
		for (mp = list_head(&mutex_list);
		    mp;
		    mp = list_next(&mutex_list, mp)) {
			uint64_t locktime = mp->locktime;
			if ((locktime > 0) && (noe > locktime) &&
			    noe - locktime >= SPL_MUTEX_WATCHDOG_TIMEOUT) {
				printf("SPL: mutex (%p) held for %llus by "
				    "'%s':%s:%d\n", mp,
				    noe - mp->locktime,
				    mp->last_locked_file,
				    mp->last_locked_function,
				    mp->last_locked_line);
			} // if old

#define	HIGH_LOCKS_PER_RUN		10000
#define	HIGH_TRYLOCK_MISS_PER_RUN	100

			const uint32_t period_locks = atomic_swap_32(
			    &mp->period_lock_count, 0);
			const uint32_t period_trymiss = atomic_swap_32(
			    &mp->period_trylock_miss, 0);

			if (period_locks > HIGH_LOCKS_PER_RUN &&
			    period_locks >
			    (period_lock_record_holder * 100) / 90) {
				printf("SPL: hot lock mutex (%p)"
				    " [created %s:%s:%d]"
				    " locked %u times in %llu seconds,"
				    " hottest was %u"
				    " [last locked by %s:%s:%d]\n",
				    mp,
				    mp->creation_file, mp->creation_function,
				    mp->creation_line, period_locks,
				    noe - prev_noe,
				    period_lock_record_holder,
				    mp->last_locked_file,
				    mp->last_locked_function,
				    mp->last_locked_line);
				if (period_locks > period_lock_record_holder)
					period_lock_record_holder =
					    period_locks;
			}

			if (period_trymiss > HIGH_TRYLOCK_MISS_PER_RUN &&
			    period_trymiss > (period_miss_record_holder *
			    90) / 100) {
				printf("SPL: hot miss mutex (%p)"
				    " [created %s:%s:%d]"
				    " had %u mutex_trylock misses in"
				    " %llu seconds, hottest was %u"
				    " [last locked by %s:%s:%d]\n",
				    mp,
				    mp->creation_file, mp->creation_function,
				    mp->creation_line, period_trymiss,
				    noe - prev_noe,
				    period_miss_record_holder,
				    mp->last_locked_file,
				    mp->last_locked_function,
				    mp->last_locked_line);
				if (period_trymiss > period_miss_record_holder)
					period_miss_record_holder =
					    period_trymiss;
			}

		} // for all

		/* decay "high score" record by 1% every pass */
		period_lock_record_holder =
		    (period_lock_record_holder * 100) / 99;
		period_miss_record_holder =
		    (period_miss_record_holder * 100) / 99;
		prev_noe = noe;

	} // while not exit

	wdlist_exit = 0;
	spl_data_barrier();
	wakeup_one((caddr_t)&mutex_list_wait_loc);
	lck_mtx_unlock((lck_mtx_t *)&mutex_list_mtx);

	printf("SPL: watchdog thread exit\n");
	thread_exit();
}
#endif


int
spl_mutex_subsystem_init(void)
{
	zfs_lock_attr = lck_attr_alloc_init();
	zfs_group_attr = lck_grp_attr_alloc_init();
	zfs_mutex_group  = lck_grp_alloc_init("zfs-mutex", zfs_group_attr);

#ifdef SPL_DEBUG_MUTEX
	{
		unsigned char mutex[128];
		int i;

		memset(mutex, 0xAF, sizeof (mutex));
		lck_mtx_init((lck_mtx_t *)&mutex[0], zfs_mutex_group,
		    zfs_lock_attr);
		for (i = sizeof (mutex) -1; i >= 0; i--)
			if (mutex[i] != 0xAF)
				break;

		printf("SPL: %s:%d: mutex size is %u\n",
		    __func__, __LINE__, i+1);

	}

	list_create(&mutex_list, sizeof (struct leak),
	    offsetof(struct leak, mutex_leak_node));
	/* We can not call mutex_init() as it would use "leak" */
	lck_mtx_init((lck_mtx_t *)&mutex_list_mtx, zfs_mutex_group,
	    zfs_lock_attr);

	/* create without timesharing or qos */
	(void) thread_create_named_with_extpol_and_qos(
	    "spl_wdlist_check (mutex)",
	    NULL, NULL, NULL,
	    NULL, 0, spl_wdlist_check, NULL, 0, 0, maxclsyspri);
#endif /* SPL_DEBUG_MUTEX */
	return (0);
}

void
spl_mutex_subsystem_fini(void)
{
#ifdef SPL_DEBUG_MUTEX
	uint64_t total = 0;
	printf("SPL: %s:%d: Dumping leaked mutex allocations..."
	    " zfs_active_mutex == %llu\n",
	    __func__, __LINE__, atomic_load_64(&zfs_active_mutex));

	/* Ask the thread to quit */
	lck_mtx_lock((lck_mtx_t *)&mutex_list_mtx);
	wdlist_exit = 1;
	spl_data_barrier();
	while (wdlist_exit) {
		wakeup_one((caddr_t)&mutex_list_wait_loc);
		msleep((caddr_t)&mutex_list_wait_loc,
		    (lck_mtx_t *)&mutex_list_mtx,
		    PRIBIO, "waiting for mutex watchdog thread to end", 0);
		spl_data_barrier();
	}

	/* mutex watchdog thread has quit, we hold the mutex */

	/* walk the leak list */

	while (1) {
		struct leak *leak, *runner;
		uint32_t found;

		leak = list_head(&mutex_list);

		if (leak) {
			list_remove(&mutex_list, leak);
		}
		if (!leak)
			break;

		// Run through list and count up how many times this leak is
		// found, removing entries as we go.
		for (found = 1, runner = list_head(&mutex_list);
		    runner;
		    runner = runner ? list_next(&mutex_list, runner) :
		    list_head(&mutex_list)) {

			if (strcmp(leak->last_locked_file,
			    runner->last_locked_file) == 0 &&
			    strcmp(leak->last_locked_function,
			    runner->last_locked_function) == 0 &&
			    leak->last_locked_line ==
			    runner->last_locked_line) {
				// Same place
				found++;
				list_remove(&mutex_list, runner);
				IOFreeType(runner, struct leak);
				runner = NULL;
			} // if same

		} // for all nodes

		printf("SPL: %s:%d  mutex %p : last lock %s %s %d :"
		    " # leaks: %u"
		    " created %llu seconds ago at %s:%s:%d"
		    " locked %llu,"
		    "try_s %llu try_w %llu\n",
		    __func__, __LINE__,
		    leak->mp,
		    leak->last_locked_file,
		    leak->last_locked_function,
		    leak->last_locked_line,
		    found,
		    NSEC2SEC(gethrtime() - leak->mutex_created_time),
		    leak->creation_file,
		    leak->creation_function,
		    leak->creation_line,
		    leak->total_lock_count,
		    leak->total_trylock_success,
		    leak->total_trylock_miss);

		IOFreeType(leak, struct leak);
		total += found;

	}
	lck_mtx_unlock((lck_mtx_t *)&mutex_list_mtx);

	printf("SPL: %s:%d Dumped %llu leaked allocations.\n",
	    __func__, __LINE__, total);

	/* We can not call mutex_destroy() as it uses leak */
	lck_mtx_destroy((lck_mtx_t *)&mutex_list_mtx, zfs_mutex_group);
	list_destroy(&mutex_list);
#endif

	if (atomic_load_64(&zfs_active_mutex) != 0) {
		printf("SPL: %s:%d: zfs_active_mutex is %llu\n",
		    __func__, __LINE__, atomic_load_64(&zfs_active_mutex));
	} else {
		printf("SPL: %s: good, zero zfs_active_mutex\n",
		    __func__);
	}

	lck_attr_free(zfs_lock_attr);
	zfs_lock_attr = NULL;

	lck_grp_attr_free(zfs_group_attr);
	zfs_group_attr = NULL;

	lck_grp_free(zfs_mutex_group);
	zfs_mutex_group = NULL;
}



#ifdef SPL_DEBUG_MUTEX
void
spl_mutex_init(kmutex_t *mp, char *name, kmutex_type_t type, void *ibc,
    const char *file, const char *fn, const int line)
#else
void
spl_mutex_init(kmutex_t *mp, char *name, kmutex_type_t type, void *ibc)
#endif
{
	ASSERT(type != MUTEX_SPIN);
	ASSERT3P(ibc, ==, NULL);

#ifdef SPL_DEBUG_MUTEX
	VERIFY3U(atomic_load_nonatomic(&mp->m_initialised), !=, MUTEX_INIT);
#endif

	lck_mtx_init((lck_mtx_t *)&mp->m_lock, zfs_mutex_group, zfs_lock_attr);
	mp->m_owner = NULL;
	mp->m_waiters = 0;
	mp->m_sleepers = 0;

	atomic_inc_64(&zfs_active_mutex);

#ifdef SPL_DEBUG_MUTEX
	mp->m_initialised = MUTEX_INIT;

	struct leak *leak;

	leak = IOMallocType(struct leak);

	VERIFY3P(leak, !=, NULL);

	memset(leak, 0, sizeof (struct leak));

	leak->mutex_created_time = gethrtime();
	strlcpy(leak->last_locked_file, file, SPL_DEBUG_MUTEX_MAXCHAR_FILE);
	strlcpy(leak->last_locked_function, fn, SPL_DEBUG_MUTEX_MAXCHAR_FUNC);
	leak->last_locked_line = line;
	strlcpy(leak->creation_file, file, SPL_DEBUG_MUTEX_MAXCHAR_FILE);
	strlcpy(leak->creation_function, fn, SPL_DEBUG_MUTEX_MAXCHAR_FUNC);
	leak->creation_line = line;
	leak->mp = mp;

	spl_data_barrier();

	lck_mtx_lock((lck_mtx_t *)&mutex_list_mtx);
	list_link_init(&leak->mutex_leak_node);
	list_insert_tail(&mutex_list, leak);
	mp->leak = leak;
	lck_mtx_unlock((lck_mtx_t *)&mutex_list_mtx);
#endif
	spl_data_barrier();
}

void
spl_mutex_destroy(kmutex_t *mp)
{

	VERIFY3P(mp, !=, NULL);

#ifdef SPL_DEBUG_MUTEX
	VERIFY3U(atomic_load_nonatomic(&mp->m_initialised), ==, MUTEX_INIT);
#endif

	if (atomic_load_nonatomic(&mp->m_owner) != 0)
		panic("SPL: releasing held mutex");

	lck_mtx_destroy((lck_mtx_t *)&mp->m_lock, zfs_mutex_group);

	atomic_dec_64(&zfs_active_mutex);

#ifdef SPL_DEBUG_MUTEX
	atomic_store_nonatomic(&mp->m_initialised, MUTEX_DESTROYED);

	struct leak *leak = (struct leak *)mp->leak;

	VERIFY3P(leak, !=, NULL);

	/* WAGs, but they rise dynamically on very fast&busy systems */

#define	BUSY_LOCK_THRESHOLD			1000 * 1000
#define	BUSY_LOCK_PER_SECOND_THRESHOLD		1000

	/*
	 * Multiple mutex_destroy() can be in flight from different threads,
	 * so these have to be protected.  We can do that _Atomic declaration
	 * and the short CAS loop below, since we're just doing
	 * straightforward compares and new-value stores.
	 */
	static _Atomic uint64_t busy_lock_per_second_record_holder = 0;

	if (leak->total_lock_count > BUSY_LOCK_THRESHOLD) {
		const hrtime_t nsage =
		    gethrtime() - leak->mutex_created_time;
		const uint64_t secage = NSEC2SEC(nsage) + 1;
		const uint64_t meanlps = leak->total_lock_count / secage;
		const uint64_t hot_thresh =
		    (busy_lock_per_second_record_holder * 100) / 90;

		if (meanlps > BUSY_LOCK_PER_SECOND_THRESHOLD &&
		    meanlps > hot_thresh) {
			printf("SPL: %s:%d: destroyed hot lock (mean lps %llu)"
			    " %llu mutex_enters since creation at %s:%s:%d"
			    " %llu seconds ago (hot was %llu lps)"
			    " [most recent lock %s:%s:%d]\n",
			    __func__, __LINE__,
			    meanlps,
			    leak->total_lock_count,
			    leak->creation_file, leak->creation_function,
			    leak->creation_line,
			    secage,
			    busy_lock_per_second_record_holder,
			    leak->last_locked_file,
			    leak->last_locked_function, leak->last_locked_line);

			/* update the global record holder */
			uint8_t b_lck = false;
			while (meanlps > busy_lock_per_second_record_holder) {
				if (!atomic_cas_8(&b_lck, false, true)) {
					busy_lock_per_second_record_holder =
					    meanlps;
				}
			}
		}
	}

#define	TRYLOCK_CALL_THRESHOLD	1000 * 1000
#define	TRYLOCK_WAIT_MIN_PCT	2		// mutex_trylock misses as %

	static _Atomic uint64_t miss_per_second_record_holder = 0;

	const uint64_t try_calls =
	    leak->total_trylock_success +
	    leak->total_trylock_miss;
	const uint64_t try_misses =
	    leak->total_trylock_miss;

	if (try_misses > 0 && try_calls > TRYLOCK_CALL_THRESHOLD) {
		const uint64_t notheldpct =
		    (try_misses * 100) / try_calls;
		const uint64_t miss_thresh =
		    (miss_per_second_record_holder * 100) / 90;

		if (notheldpct > TRYLOCK_WAIT_MIN_PCT &&
		    notheldpct > miss_thresh) {
			printf("SPL: %s:%d: destroyed lock which"
			    " waited often in mutex_trylock:"
			    " %llu all locks,"
			    " %llu trysuccess, %llu miss, notheldpct %llu,"
			    " created %llu seconds ago at %s:%s:%d"
			    " (thresh was %llu miss/s)"
			    " [most recent lock location %s:%s:%d]\n",
			    __func__, __LINE__,
			    leak->total_lock_count,
			    leak->total_trylock_success,
			    leak->total_trylock_miss,
			    notheldpct,
			    NSEC2SEC(gethrtime() -
			    leak->mutex_created_time),
			    leak->creation_file, leak->creation_function,
			    leak->creation_line,
			    miss_per_second_record_holder,
			    leak->last_locked_file,
			    leak->last_locked_function, leak->last_locked_line);

			/* update the global record holder */
			uint8_t b_lck = false;
			while (notheldpct > miss_per_second_record_holder) {
				if (!atomic_cas_8(&b_lck, false, true)) {
					miss_per_second_record_holder =
					    notheldpct;
				}
			}
		}
	}

	lck_mtx_lock((lck_mtx_t *)&mutex_list_mtx);
	list_remove(&mutex_list, leak);
	mp->leak = NULL;
	lck_mtx_unlock((lck_mtx_t *)&mutex_list_mtx);
	IOFreeType(leak, struct leak);
#endif
}

#ifdef SPL_DEBUG_MUTEX
void
spl_mutex_enter(kmutex_t *mp, const char *file, const char *func,
    const int line)
#else
void
spl_mutex_enter(kmutex_t *mp)
#endif
{
#ifdef SPL_DEBUG_MUTEX
	VERIFY3U(atomic_load_nonatomic(&mp->m_initialised), ==, MUTEX_INIT);
#endif

#ifdef DEBUG
	if (unlikely(*((uint64_t *)mp) == 0xdeadbeefdeadbeef)) {
		panic("SPL: mutex_enter deadbeef");
		__builtin_unreachable();
	}
#endif

	if (atomic_load_nonatomic(&mp->m_owner) == current_thread()) {
		panic("mutex_enter: locking against myself!");
		__builtin_unreachable();
	}

	atomic_inc_64(&mp->m_waiters);
	spl_data_barrier();
	lck_mtx_lock((lck_mtx_t *)&mp->m_lock);
	spl_data_barrier();
	atomic_dec_64(&mp->m_waiters);
	atomic_store_nonatomic(&mp->m_owner, current_thread());

#ifdef SPL_DEBUG_MUTEX
	if (likely(mp->leak)) {
		/*
		 * We have the lock here, so our leak structure will not be
		 * interfered with by other mutex_* functions operating on
		 * this lock, except for the periodic spl_wdlist_check()
		 * thread (see below) or a mutex_tryenter() (which will fail)
		 */
		struct leak *leak = (struct leak *)mp->leak;
		leak->locktime = gethrestime_sec();
		strlcpy(leak->last_locked_file,
		    file, sizeof (leak->last_locked_file));
		strlcpy(leak->last_locked_function,
		    func, sizeof (leak->last_locked_function));
		leak->last_locked_line = line;
		leak->total_lock_count++;
		/*
		 * We allow a possible inaccuracy here by not
		 * doing an atomic_inc_32() for the period lock.
		 * The race can only be between this current thread
		 * right here, and the spl_wdlist_check() periodic
		 * read-modify-write.
		 *
		 * That RMW is done by an atomic_swap_32()
		 * which uses SEQ_CST on Mac platforms,
		 * which should order that read&zero against this
		 * increment. In particular, the increment here shouldn't
		 * be here_read_large_old_value_from_memory__to_register,
		 * here_increment_register,
		 * periodic_thread_sets_old_value_to_zero,
		 * here_write_large_value_from_register_to_memory,
		 * but it is technically possible (the race window is
		 * very narrow!).
		 *
		 * The result would only be a (potential!) spurious printf
		 * about a hot lock from the periodic thread at its next run,
		 * and so the cost of a SEQ_CST atomic increment here is
		 * not justified.
		 */
		leak->period_lock_count++;
	} else {
		panic("SPL: %s:%d: where is my leak data?"
		    " possible compilation mismatch", __func__, __LINE__);
		__builtin_unreachable();
	}
#endif

}


/*
 * So far, the interruptible part does not work, this just
 * calls regular mutex_enter.
 */
#ifdef SPL_DEBUG_MUTEX
int
spl_mutex_enter_interruptible(kmutex_t *mp, const char *file,
    const char *func, const int line)
#else
int
spl_mutex_enter_interruptible(kmutex_t *mp)
#endif
{
	int error = 0;
#ifdef SPL_DEBUG_MUTEX
	VERIFY3U(atomic_load_nonatomic(&mp->m_initialised), ==, MUTEX_INIT);
#endif

#ifdef DEBUG
	if (unlikely(*((uint64_t *)mp) == 0xdeadbeefdeadbeef)) {
		panic("SPL: mutex_enter deadbeef");
		__builtin_unreachable();
	}
#endif

	if (atomic_load_nonatomic(&mp->m_owner) == current_thread()) {
		panic("mutex_enter: locking against myself!");
		__builtin_unreachable();
	}

	atomic_inc_64(&mp->m_waiters);
	spl_data_barrier();
	lck_mtx_lock((lck_mtx_t *)&mp->m_lock);
	spl_data_barrier();

	if (error != 0)
		goto interrupted;

	atomic_dec_64(&mp->m_waiters);
	atomic_store_nonatomic(&mp->m_owner, current_thread());

#ifdef SPL_DEBUG_MUTEX
	if (likely(mp->leak)) {
		/*
		 * We have the lock here, so our leak structure will not be
		 * interfered with by other mutex_* functions operating on
		 * this lock, except for the periodic spl_wdlist_check()
		 * thread (see below) or a mutex_tryenter() (which will fail)
		 */
		struct leak *leak = (struct leak *)mp->leak;
		leak->locktime = gethrestime_sec();
		strlcpy(leak->last_locked_file,
		    file, sizeof (leak->last_locked_file));
		strlcpy(leak->last_locked_function,
		    func, sizeof (leak->last_locked_function));
		leak->last_locked_line = line;
		leak->total_lock_count++;
		/*
		 * We allow a possible inaccuracy here by not
		 * doing an atomic_inc_32() for the period lock.
		 * The race can only be between this current thread
		 * right here, and the spl_wdlist_check() periodic
		 * read-modify-write.
		 *
		 * That RMW is done by an atomic_swap_32()
		 * which uses SEQ_CST on Mac platforms,
		 * which should order that read&zero against this
		 * increment. In particular, the increment here shouldn't
		 * be here_read_large_old_value_from_memory__to_register,
		 * here_increment_register,
		 * periodic_thread_sets_old_value_to_zero,
		 * here_write_large_value_from_register_to_memory,
		 * but it is technically possible (the race window is
		 * very narrow!).
		 *
		 * The result would only be a (potential!) spurious printf
		 * about a hot lock from the periodic thread at its next run,
		 * and so the cost of a SEQ_CST atomic increment here is
		 * not justified.
		 */
		leak->period_lock_count++;
	} else {
		panic("SPL: %s:%d: where is my leak data?"
		    " possible compilation mismatch", __func__, __LINE__);
		__builtin_unreachable();
	}
#endif

interrupted:

	return (error);
}

void
spl_mutex_exit(kmutex_t *mp)
{
#ifdef DEBUG
	if (unlikely(*((uint64_t *)mp) == 0xdeadbeefdeadbeef)) {
		panic("SPL: mutex_exit deadbeef");
		__builtin_unreachable();
	}
#endif

#ifdef SPL_DEBUG_MUTEX
	VERIFY3U(atomic_load_nonatomic(&mp->m_initialised), ==, MUTEX_INIT);
#endif

#ifdef SPL_DEBUG_MUTEX
	if (likely(mp->leak)) {
		struct leak *leak = (struct leak *)mp->leak;
		uint64_t locktime = leak->locktime;
		uint64_t noe = gethrestime_sec();
		if ((locktime > 0) && (noe > locktime) &&
		    noe - locktime >= SPL_MUTEX_WATCHDOG_TIMEOUT) {
			printf("SPL: mutex (%p) finally released after %llus "
			    "was held by %s:'%s':%d\n",
			    leak, noe - leak->locktime,
			    leak->last_locked_file, leak->last_locked_function,
			    leak->last_locked_line);
		}
		leak->locktime = 0;
	} else {
		panic("SPL: %s:%d: where is my leak data?",
		    __func__, __LINE__);
		__builtin_unreachable();
	}
#endif
	mp->m_owner = NULL;
	spl_data_barrier();
	lck_mtx_unlock((lck_mtx_t *)&mp->m_lock);
}

int
#ifdef SPL_DEBUG_MUTEX
spl_mutex_tryenter(kmutex_t *mp, const char *file, const char *func,
    const int line)
#else
spl_mutex_tryenter(kmutex_t *mp)
#endif
{
	int held;

#ifdef SPL_DEBUG_MUTEX
	VERIFY3U(atomic_load_nonatomic(&mp->m_initialised), ==, MUTEX_INIT);
#endif

	atomic_inc_64(&mp->m_waiters);
	spl_data_barrier();
	held = lck_mtx_try_lock((lck_mtx_t *)&mp->m_lock);
	/*
	 * Now do a full barrier, because that's the right thing to do after
	 * we get a lock from lck_mtx...(), which on Apple Silicon uses softer
	 * acquire semantics than the multithread store ordering we'd like
	 * in our emulation of heritage Solaris code.
	 *
	 * Apple Silicon relevant only.  spl_data_barrier() is a noop on
	 * strong memory model machines like Intel.
	 *
	 * Initially this was an unconditional spl_data_barrier(), but the
	 * point of the barrier is to let other threads know we have the lock
	 * in happens-before sense (i.e., that the lock is held before the
	 * other threads issue reads/writes on the affected cache lines, and
	 * every thread enjoys happens-after on any reads/writes of those
	 * cache lines after the barrier is issued).  The "dmb ish" is cheap
	 * but not free, and there could be a mutex_tryenter() in a fairly
	 * tight loop.  So we skip it if we don't obtain the lock.  We've also
	 * recently done a full barrier so that we know that a previous lock
	 * holder's mutex_exit() is in a happened-before state when we do
	 * lck_mtx_try_lock().
	 *
	 * The atomic_dec_64() will use acquire/release semantics and who
	 * knows how they slide around relative to the full barrier (it also
	 * is not necessarily a super-fast instruction), so we don't want to
	 * slide the barrier into a single if (held) after the atomic decrement.
	 *
	 * The atomic decrement also needs to happen before DEBUGging code, so
	 * it should stay close to the lck_mtx...().
	 */
	if (held)
		spl_data_barrier();
	atomic_dec_64(&mp->m_waiters);
	if (held) {
		atomic_store_nonatomic(&mp->m_owner, current_thread());
#ifdef SPL_DEBUG_MUTEX
		if (likely(mp->leak)) {
			/*
			 * see block comment in mutex_enter()'s
			 * SPL_DEBUG_MUTEX section, and below.
			 */
			struct leak *leak = (struct leak *)mp->leak;
			leak->locktime = gethrestime_sec();
			leak->total_trylock_success++;
			leak->total_lock_count++;
			leak->period_lock_count++;
			strlcpy(leak->last_locked_file, file,
			    SPL_DEBUG_MUTEX_MAXCHAR_FILE);
			strlcpy(leak->last_locked_function, func,
			    SPL_DEBUG_MUTEX_MAXCHAR_FUNC);
			leak->last_locked_line = line;
		} else {
			panic("SPL: %s:%d: where is my leak data?",
			    __func__, __LINE__);
			__builtin_unreachable();
		}

	} else {
		/*
		 * We are not protected by the lock here, so our
		 * read-modify-writes must be done atomically, since in the
		 * periodic spl_wdlist_check() thread these memory locations
		 * may also have a racing ("simultaneous") RMW.  Here we
		 * avoid the periodic thread potentially not seeing the
		 * trylock miss that would just go over the threshold for
		 * a diagnostic printf.
		 *
		 * The xnu code below lck_mtx_try_lock() for a miss is
		 * substantially more expensive than the cost of these atomic
		 * increments, so we shouldn't be doing mutex_trylock() in
		 * a tight loop anyway.
		 */
		VERIFY3P(mp->leak, !=, NULL);
		struct leak *leak = (struct leak *)mp->leak;
		atomic_inc_64(&leak->total_trylock_miss);
		atomic_inc_32(&leak->period_trylock_miss);
#endif
	}
	return (held);
}

int
spl_mutex_owned(kmutex_t *mp)
{
	return (atomic_load_nonatomic(&mp->m_owner) == current_thread());
}

struct kthread *
spl_mutex_owner(kmutex_t *mp)
{
	return (atomic_load_nonatomic(&mp->m_owner));
}

#ifdef SPL_DEBUG_MUTEX
void
spl_dbg_mutex_destroy(kmutex_t *mp, const char *file,
    const char *func, const int line)
{

	extern struct thread *spl_mutex_owner(kmutex_t *);
	extern int spl_mutex_owned(kmutex_t *);
	extern void spl_mutex_destroy(kmutex_t *);

	membar_consumer();
	VERIFY3P(mp, !=, NULL);
	struct thread *o = spl_mutex_owner(mp);
	if (o != NULL) {
		VERIFY3P(mp->leak, !=, NULL);
		struct leak *leak = (struct leak *)mp->leak;
		uint64_t noe = gethrestime_sec();
		if (!spl_mutex_owned(mp)) {
			panic("%s: mutex has other owner %p"
			    " destroy call at %s() in %s line %d,"
			    " last mutex_enter in %s:%s:%d"
			    " %llus ago"
			    "\n",
			    __func__, o, func, file, line,
			    leak->last_locked_file,
			    leak->last_locked_function,
			    leak->last_locked_line,
			    noe - leak->locktime);
		} else {
			panic("%s: mutex %p is owned by"
			    " current thread"
			    " from %s() in %s line %d"
			    " last mutex_enter in %s:%s:%d"
			    " %llus ago"
			    "\n",
			    __func__, o, func, file, line,
			    leak->last_locked_file,
			    leak->last_locked_function,
			    leak->last_locked_line,
			    noe - leak->locktime);
		}
	}
	spl_mutex_destroy(mp);
}
#endif
