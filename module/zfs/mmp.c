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
 * Copyright (c) 2017 by Lawrence Livermore National Security, LLC.
 */

#include <sys/abd.h>
#include <sys/mmp.h>
#include <sys/spa.h>
#include <sys/spa_impl.h>
#include <sys/time.h>
#include <sys/vdev.h>
#include <sys/vdev_impl.h>
#include <sys/zfs_context.h>
#include <sys/callb.h>

/*
 * Multi-Modifier Protection (MMP) attempts to prevent a user from importing
 * or opening a pool on more than one host at a time.  In particular, it
 * prevents "zpool import -f" on a host from succeeding while the pool is
 * already imported on another host.  There are many other ways in which a
 * device could be used by two hosts for different purposes at the same time
 * resulting in pool damage.  This implementation does not attempt to detect
 * those cases.
 *
 * MMP operates by ensuring there are frequent visible changes on disk (a
 * "heartbeat") at all times.  And by altering the import process to check
 * for these changes and failing the import when they are detected.  This
 * functionality is enabled by setting the 'multihost' pool property to on.
 *
 * Uberblocks written by the txg_sync thread always go into the first
 * (N-MMP_BLOCKS_PER_LABEL) slots, the remaining slots are reserved for MMP.
 * They are used to hold uberblocks which are exactly the same as the last
 * synced uberblock except that the ub_timestamp and mmp_config are frequently
 * updated.  Like all other uberblocks, the slot is written with an embedded
 * checksum, and slots with invalid checksums are ignored.  This provides the
 * "heartbeat", with no risk of overwriting good uberblocks that must be
 * preserved, e.g. previous txgs and associated block pointers.
 *
 * Three optional fields are added to uberblock structure; ub_mmp_magic,
 * ub_mmp_config, and ub_mmp_delay.  The ub_mmp_magic value allows zfs to tell
 * whether the other ub_mmp_* fields are valid.  The ub_mmp_config field tells
 * the importing host the settings of zfs_multihost_interval and
 * zfs_multihost_fail_intervals on the host which last had (or currently has)
 * the pool imported.  These determine how long a host must wait to detect
 * activity in the pool, before concluding the pool is not in use.  The
 * mmp_delay field is a decaying average of the amount of time between
 * completion of successive MMP writes, in nanoseconds.  It indicates whether
 * MMP is enabled.
 *
 * During import an activity test may now be performed to determine if
 * the pool is in use.  The activity test is typically required if the
 * ZPOOL_CONFIG_HOSTID does not match the system hostid, the pool state is
 * POOL_STATE_ACTIVE, and the pool is not a root pool.
 *
 * The activity test finds the "best" uberblock (highest txg, timestamp, and, if
 * ub_mmp_magic is valid, sequence number from ub_mmp_config).  It then waits
 * some time, and finds the "best" uberblock again.  If any of the mentioned
 * fields have different values in the newly read uberblock, the pool is in use
 * by another host and the import fails.  In order to assure the accuracy of the
 * activity test, the default values result in an activity test duration of 20x
 * the mmp write interval.
 *
 * The duration of the "zpool import" activity test depends on the information
 * available in the "best" uberblock:
 *
 * 1) If uberblock was written by zfs-0.8 or newer and fail_intervals > 0:
 *    ub_mmp_config.fail_intervals * ub_mmp_config.multihost_interval * 2
 *
 *    In this case, a weak guarantee is provided.  Since the host which last had
 *    the pool imported will suspend the pool if no mmp writes land within
 *    fail_intervals * multihost_interval ms, the absence of writes during that
 *    time means either the pool is not imported, or it is imported but the pool
 *    is suspended and no further writes will occur.
 *
 *    Note that resuming the suspended pool on the remote host would invalidate
 *    this guarantee, and so it is not allowed.
 *
 *    The factor of 2 provides a conservative safety factor and derives from
 *    MMP_IMPORT_SAFETY_FACTOR;
 *
 * 2) If uberblock was written by zfs-0.8 or newer and fail_intervals == 0:
 *    (ub_mmp_config.multihost_interval + ub_mmp_delay) *
 *        zfs_multihost_import_intervals
 *
 *    In this case no guarantee can provided.  However, as long as some devices
 *    are healthy and connected, it is likely that at least one write will land
 *    within (multihost_interval + mmp_delay) because multihost_interval is
 *    enough time for a write to be attempted to each leaf vdev, and mmp_delay
 *    is enough for one to land, based on past delays.  Multiplying by
 *    zfs_multihost_import_intervals provides a conservative safety factor.
 *
 * 3) If uberblock was written by zfs-0.7:
 *    (zfs_multihost_interval + ub_mmp_delay) * zfs_multihost_import_intervals
 *
 *    The same logic as case #2 applies, but we do not know remote tunables.
 *
 *    We use the local value for zfs_multihost_interval because the original MMP
 *    did not record this value in the uberblock.
 *
 *    ub_mmp_delay >= (zfs_multihost_interval / leaves), so if the other host
 *    has a much larger zfs_multihost_interval set, ub_mmp_delay will reflect
 *    that.  We will have waited enough time for zfs_multihost_import_intervals
 *    writes to be issued and all but one to land.
 *
 *    single device pool example delays
 *
 *    import_delay = (1 + 1) * 20   =  40s #defaults, no I/O delay
 *    import_delay = (1 + 10) * 20  = 220s #defaults, 10s I/O delay
 *    import_delay = (10 + 10) * 20 = 400s #10s multihost_interval,
 *                                          no I/O delay
 *    100 device pool example delays
 *
 *    import_delay = (1 + .01) * 20 =  20s #defaults, no I/O delay
 *    import_delay = (1 + 10) * 20  = 220s #defaults, 10s I/O delay
 *    import_delay = (10 + .1) * 20 = 202s #10s multihost_interval,
 *                                          no I/O delay
 *
 * 4) Otherwise, this uberblock was written by a pre-MMP zfs:
 *    zfs_multihost_import_intervals * zfs_multihost_interval
 *
 *    In this case local tunables are used.  By default this product = 10s, long
 *    enough for a pool with any activity at all to write at least one
 *    uberblock.  No guarantee can be provided.
 *
 * Additionally, the duration is then extended by a random 25% to attempt to to
 * detect simultaneous imports.  For example, if both partner hosts are rebooted
 * at the same time and automatically attempt to import the pool.
 */

/*
 * Used to control the frequency of mmp writes which are performed when the
 * 'multihost' pool property is on.  This is one factor used to determine the
 * length of the activity check during import.
 *
 * On average an mmp write will be issued for each leaf vdev every
 * zfs_multihost_interval milliseconds.  In practice, the observed period can
 * vary with the I/O load and this observed value is the ub_mmp_delay which is
 * stored in the uberblock.  The minimum allowed value is 100 ms.
 */
ulong_t zfs_multihost_interval = MMP_DEFAULT_INTERVAL;

/*
 * Used to control the duration of the activity test on import.  Smaller values
 * of zfs_multihost_import_intervals will reduce the import time but increase
 * the risk of failing to detect an active pool.  The total activity check time
 * is never allowed to drop below one second.  A value of 0 is ignored and
 * treated as if it was set to 1.
 */
uint_t zfs_multihost_import_intervals = MMP_DEFAULT_IMPORT_INTERVALS;

/*
 * Controls the behavior of the pool when mmp write failures or delays are
 * detected.
 *
 * When zfs_multihost_fail_intervals = 0, mmp write failures or delays are
 * ignored.  The failures will still be reported to the ZED which depending on
 * its configuration may take action such as suspending the pool or taking a
 * device offline.
 *
 * When zfs_multihost_fail_intervals > 0, the pool will be suspended if
 * zfs_multihost_fail_intervals * zfs_multihost_interval milliseconds pass
 * without a successful mmp write.  This guarantees the activity test will see
 * mmp writes if the pool is imported.  A value of 1 is ignored and treated as
 * if it was set to 2, because a single leaf vdev pool will issue a write once
 * per multihost_interval and thus any variation in latency would cause the
 * pool to be suspended.
 */
uint_t zfs_multihost_fail_intervals = MMP_DEFAULT_FAIL_INTERVALS;

char *mmp_tag = "mmp_write_uberblock";
static void mmp_thread(void *arg);

void
mmp_init(spa_t *spa)
{
	mmp_thread_t *mmp = &spa->spa_mmp;

	mutex_init(&mmp->mmp_thread_lock, NULL, MUTEX_DEFAULT, NULL);
	cv_init(&mmp->mmp_thread_cv, NULL, CV_DEFAULT, NULL);
	mutex_init(&mmp->mmp_io_lock, NULL, MUTEX_DEFAULT, NULL);
	mmp->mmp_kstat_id = 1;
}

void
mmp_fini(spa_t *spa)
{
	mmp_thread_t *mmp = &spa->spa_mmp;

	mutex_destroy(&mmp->mmp_thread_lock);
	cv_destroy(&mmp->mmp_thread_cv);
	mutex_destroy(&mmp->mmp_io_lock);
}

static void
mmp_thread_enter(mmp_thread_t *mmp, callb_cpr_t *cpr)
{
	CALLB_CPR_INIT(cpr, &mmp->mmp_thread_lock, callb_generic_cpr, FTAG);
	mutex_enter(&mmp->mmp_thread_lock);
}

static void
mmp_thread_exit(mmp_thread_t *mmp, kthread_t **mpp, callb_cpr_t *cpr)
{
	ASSERT(*mpp != NULL);
	*mpp = NULL;
	cv_broadcast(&mmp->mmp_thread_cv);
	CALLB_CPR_EXIT(cpr);		/* drops &mmp->mmp_thread_lock */
	thread_exit();
}

void
mmp_thread_start(spa_t *spa)
{
	mmp_thread_t *mmp = &spa->spa_mmp;

	if (spa_writeable(spa)) {
		mutex_enter(&mmp->mmp_thread_lock);
		if (!mmp->mmp_thread) {
			mmp->mmp_thread = thread_create(NULL, 0, mmp_thread,
			    spa, 0, &p0, TS_RUN, defclsyspri);
			zfs_dbgmsg("MMP thread started pool '%s' "
			    "gethrtime %llu", spa_name(spa), gethrtime());
		}
		mutex_exit(&mmp->mmp_thread_lock);
	}
}

void
mmp_thread_stop(spa_t *spa)
{
	mmp_thread_t *mmp = &spa->spa_mmp;

	mutex_enter(&mmp->mmp_thread_lock);
	mmp->mmp_thread_exiting = 1;
	cv_broadcast(&mmp->mmp_thread_cv);

	while (mmp->mmp_thread) {
		cv_wait(&mmp->mmp_thread_cv, &mmp->mmp_thread_lock);
	}
	mutex_exit(&mmp->mmp_thread_lock);
	zfs_dbgmsg("MMP thread stopped pool '%s' gethrtime %llu",
	    spa_name(spa), gethrtime());

	ASSERT(mmp->mmp_thread == NULL);
	mmp->mmp_thread_exiting = 0;
}

typedef enum mmp_vdev_state_flag {
	MMP_FAIL_NOT_WRITABLE	= (1 << 0),
	MMP_FAIL_WRITE_PENDING	= (1 << 1),
} mmp_vdev_state_flag_t;

/*
 * Find a leaf vdev to write an MMP block to.  It must not have an outstanding
 * mmp write (if so a new write will also likely block).  If there is no usable
 * leaf, a nonzero error value is returned. The error value returned is a bit
 * field.
 *
 * MMP_FAIL_WRITE_PENDING   One or more leaf vdevs are writeable, but have an
 *                          outstanding MMP write.
 * MMP_FAIL_NOT_WRITABLE    One or more leaf vdevs are not writeable.
 */

static int
mmp_next_leaf(spa_t *spa)
{
	vdev_t *leaf;
	vdev_t *starting_leaf;
	int fail_mask = 0;

	ASSERT(MUTEX_HELD(&spa->spa_mmp.mmp_io_lock));
	ASSERT(spa_config_held(spa, SCL_STATE, RW_READER));
	ASSERT(list_link_active(&spa->spa_leaf_list.list_head) == B_TRUE);
	ASSERT(!list_is_empty(&spa->spa_leaf_list));

	if (spa->spa_mmp.mmp_leaf_last_gen != spa->spa_leaf_list_gen) {
		spa->spa_mmp.mmp_last_leaf = list_head(&spa->spa_leaf_list);
		spa->spa_mmp.mmp_leaf_last_gen = spa->spa_leaf_list_gen;
	}

	leaf = spa->spa_mmp.mmp_last_leaf;
	if (leaf == NULL)
		leaf = list_head(&spa->spa_leaf_list);
	starting_leaf = leaf;

	do {
		leaf = list_next(&spa->spa_leaf_list, leaf);
		if (leaf == NULL)
			leaf = list_head(&spa->spa_leaf_list);

		/*
		 * We skip unwritable, offline, detached, and dRAID spare
		 * devices as they are either not legal targets or the write
		 * may fail or not be seen by other hosts.  Skipped dRAID
		 * spares can never be written so the fail mask is not set.
		 */
		if (!vdev_writeable(leaf) || leaf->vdev_offline ||
		    leaf->vdev_detached) {
			fail_mask |= MMP_FAIL_NOT_WRITABLE;
		} else if (leaf->vdev_ops == &vdev_draid_spare_ops) {
			continue;
		} else if (leaf->vdev_mmp_pending != 0) {
			fail_mask |= MMP_FAIL_WRITE_PENDING;
		} else {
			spa->spa_mmp.mmp_last_leaf = leaf;
			return (0);
		}
	} while (leaf != starting_leaf);

	ASSERT(fail_mask);

	return (fail_mask);
}

/*
 * MMP writes are issued on a fixed schedule, but may complete at variable,
 * much longer, intervals.  The mmp_delay captures long periods between
 * successful writes for any reason, including disk latency, scheduling delays,
 * etc.
 *
 * The mmp_delay is usually calculated as a decaying average, but if the latest
 * delay is higher we do not average it, so that we do not hide sudden spikes
 * which the importing host must wait for.
 *
 * If writes are occurring frequently, such as due to a high rate of txg syncs,
 * the mmp_delay could become very small.  Since those short delays depend on
 * activity we cannot count on, we never allow mmp_delay to get lower than rate
 * expected if only mmp_thread writes occur.
 *
 * If an mmp write was skipped or fails, and we have already waited longer than
 * mmp_delay, we need to update it so the next write reflects the longer delay.
 *
 * Do not set mmp_delay if the multihost property is not on, so as not to
 * trigger an activity check on import.
 */
static void
mmp_delay_update(spa_t *spa, boolean_t write_completed)
{
	mmp_thread_t *mts = &spa->spa_mmp;
	hrtime_t delay = gethrtime() - mts->mmp_last_write;

	ASSERT(MUTEX_HELD(&mts->mmp_io_lock));

	if (spa_multihost(spa) == B_FALSE) {
		mts->mmp_delay = 0;
		return;
	}

	if (delay > mts->mmp_delay)
		mts->mmp_delay = delay;

	if (write_completed == B_FALSE)
		return;

	mts->mmp_last_write = gethrtime();

	/*
	 * strictly less than, in case delay was changed above.
	 */
	if (delay < mts->mmp_delay) {
		hrtime_t min_delay =
		    MSEC2NSEC(MMP_INTERVAL_OK(zfs_multihost_interval)) /
		    MAX(1, vdev_count_leaves(spa));
		mts->mmp_delay = MAX(((delay + mts->mmp_delay * 127) / 128),
		    min_delay);
	}
}

static void
mmp_write_done(zio_t *zio)
{
	spa_t *spa = zio->io_spa;
	vdev_t *vd = zio->io_vd;
	mmp_thread_t *mts = zio->io_private;

	mutex_enter(&mts->mmp_io_lock);
	uint64_t mmp_kstat_id = vd->vdev_mmp_kstat_id;
	hrtime_t mmp_write_duration = gethrtime() - vd->vdev_mmp_pending;

	mmp_delay_update(spa, (zio->io_error == 0));

	vd->vdev_mmp_pending = 0;
	vd->vdev_mmp_kstat_id = 0;

	mutex_exit(&mts->mmp_io_lock);
	spa_config_exit(spa, SCL_STATE, mmp_tag);

	spa_mmp_history_set(spa, mmp_kstat_id, zio->io_error,
	    mmp_write_duration);

	abd_free(zio->io_abd);
}

/*
 * When the uberblock on-disk is updated by a spa_sync,
 * creating a new "best" uberblock, update the one stored
 * in the mmp thread state, used for mmp writes.
 */
void
mmp_update_uberblock(spa_t *spa, uberblock_t *ub)
{
	mmp_thread_t *mmp = &spa->spa_mmp;

	mutex_enter(&mmp->mmp_io_lock);
	mmp->mmp_ub = *ub;
	mmp->mmp_seq = 1;
	mmp->mmp_ub.ub_timestamp = gethrestime_sec();
	mmp_delay_update(spa, B_TRUE);
	mutex_exit(&mmp->mmp_io_lock);
}

/*
 * Choose a random vdev, label, and MMP block, and write over it
 * with a copy of the last-synced uberblock, whose timestamp
 * has been updated to reflect that the pool is in use.
 */
static void
mmp_write_uberblock(spa_t *spa)
{
	int flags = ZIO_FLAG_CONFIG_WRITER | ZIO_FLAG_CANFAIL;
	mmp_thread_t *mmp = &spa->spa_mmp;
	uberblock_t *ub;
	vdev_t *vd = NULL;
	int label, error;
	uint64_t offset;

	hrtime_t lock_acquire_time = gethrtime();
	spa_config_enter(spa, SCL_STATE, mmp_tag, RW_READER);
	lock_acquire_time = gethrtime() - lock_acquire_time;
	if (lock_acquire_time > (MSEC2NSEC(MMP_MIN_INTERVAL) / 10))
		zfs_dbgmsg("MMP SCL_STATE acquisition pool '%s' took %llu ns "
		    "gethrtime %llu", spa_name(spa), lock_acquire_time,
		    gethrtime());

	mutex_enter(&mmp->mmp_io_lock);

	error = mmp_next_leaf(spa);

	/*
	 * spa_mmp_history has two types of entries:
	 * Issued MMP write: records time issued, error status, etc.
	 * Skipped MMP write: an MMP write could not be issued because no
	 * suitable leaf vdev was available.  See comment above struct
	 * spa_mmp_history for details.
	 */

	if (error) {
		mmp_delay_update(spa, B_FALSE);
		if (mmp->mmp_skip_error == error) {
			spa_mmp_history_set_skip(spa, mmp->mmp_kstat_id - 1);
		} else {
			mmp->mmp_skip_error = error;
			spa_mmp_history_add(spa, mmp->mmp_ub.ub_txg,
			    gethrestime_sec(), mmp->mmp_delay, NULL, 0,
			    mmp->mmp_kstat_id++, error);
			zfs_dbgmsg("MMP error choosing leaf pool '%s' "
			    "gethrtime %llu fail_mask %#x", spa_name(spa),
			    gethrtime(), error);
		}
		mutex_exit(&mmp->mmp_io_lock);
		spa_config_exit(spa, SCL_STATE, mmp_tag);
		return;
	}

	vd = spa->spa_mmp.mmp_last_leaf;
	if (mmp->mmp_skip_error != 0) {
		mmp->mmp_skip_error = 0;
		zfs_dbgmsg("MMP write after skipping due to unavailable "
		    "leaves, pool '%s' gethrtime %llu leaf %llu",
		    spa_name(spa), (u_longlong_t)gethrtime(),
		    (u_longlong_t)vd->vdev_guid);
	}

	if (mmp->mmp_zio_root == NULL)
		mmp->mmp_zio_root = zio_root(spa, NULL, NULL,
		    flags | ZIO_FLAG_GODFATHER);

	if (mmp->mmp_ub.ub_timestamp != gethrestime_sec()) {
		/*
		 * Want to reset mmp_seq when timestamp advances because after
		 * an mmp_seq wrap new values will not be chosen by
		 * uberblock_compare() as the "best".
		 */
		mmp->mmp_ub.ub_timestamp = gethrestime_sec();
		mmp->mmp_seq = 1;
	}

	ub = &mmp->mmp_ub;
	ub->ub_mmp_magic = MMP_MAGIC;
	ub->ub_mmp_delay = mmp->mmp_delay;
	ub->ub_mmp_config = MMP_SEQ_SET(mmp->mmp_seq) |
	    MMP_INTERVAL_SET(MMP_INTERVAL_OK(zfs_multihost_interval)) |
	    MMP_FAIL_INT_SET(MMP_FAIL_INTVS_OK(
	    zfs_multihost_fail_intervals));
	vd->vdev_mmp_pending = gethrtime();
	vd->vdev_mmp_kstat_id = mmp->mmp_kstat_id;

	zio_t *zio  = zio_null(mmp->mmp_zio_root, spa, NULL, NULL, NULL, flags);
	abd_t *ub_abd = abd_alloc_for_io(VDEV_UBERBLOCK_SIZE(vd), B_TRUE);
	abd_zero(ub_abd, VDEV_UBERBLOCK_SIZE(vd));
	abd_copy_from_buf(ub_abd, ub, sizeof (uberblock_t));

	mmp->mmp_seq++;
	mmp->mmp_kstat_id++;
	mutex_exit(&mmp->mmp_io_lock);

	offset = VDEV_UBERBLOCK_OFFSET(vd, VDEV_UBERBLOCK_COUNT(vd) -
	    MMP_BLOCKS_PER_LABEL + random_in_range(MMP_BLOCKS_PER_LABEL));

	label = random_in_range(VDEV_LABELS);
	vdev_label_write(zio, vd, label, ub_abd, offset,
	    VDEV_UBERBLOCK_SIZE(vd), mmp_write_done, mmp,
	    flags | ZIO_FLAG_DONT_PROPAGATE);

	(void) spa_mmp_history_add(spa, ub->ub_txg, ub->ub_timestamp,
	    ub->ub_mmp_delay, vd, label, vd->vdev_mmp_kstat_id, 0);

	zio_nowait(zio);
}

static void
mmp_thread(void *arg)
{
	spa_t *spa = (spa_t *)arg;
	mmp_thread_t *mmp = &spa->spa_mmp;
	boolean_t suspended = spa_suspended(spa);
	boolean_t multihost = spa_multihost(spa);
	uint64_t mmp_interval = MSEC2NSEC(MMP_INTERVAL_OK(
	    zfs_multihost_interval));
	uint32_t mmp_fail_intervals = MMP_FAIL_INTVS_OK(
	    zfs_multihost_fail_intervals);
	hrtime_t mmp_fail_ns = mmp_fail_intervals * mmp_interval;
	boolean_t last_spa_suspended = suspended;
	boolean_t last_spa_multihost = multihost;
	uint64_t last_mmp_interval = mmp_interval;
	uint32_t last_mmp_fail_intervals = mmp_fail_intervals;
	hrtime_t last_mmp_fail_ns = mmp_fail_ns;
	callb_cpr_t cpr;
	int skip_wait = 0;

	mmp_thread_enter(mmp, &cpr);

	/*
	 * There have been no MMP writes yet.  Setting mmp_last_write here gives
	 * us one mmp_fail_ns period, which is consistent with the activity
	 * check duration, to try to land an MMP write before MMP suspends the
	 * pool (if so configured).
	 */

	mutex_enter(&mmp->mmp_io_lock);
	mmp->mmp_last_write = gethrtime();
	mmp->mmp_delay = MSEC2NSEC(MMP_INTERVAL_OK(zfs_multihost_interval));
	mutex_exit(&mmp->mmp_io_lock);

	while (!mmp->mmp_thread_exiting) {
		hrtime_t next_time = gethrtime() +
		    MSEC2NSEC(MMP_DEFAULT_INTERVAL);
		int leaves = MAX(vdev_count_leaves(spa), 1);

		/* Detect changes in tunables or state */

		last_spa_suspended = suspended;
		last_spa_multihost = multihost;
		suspended = spa_suspended(spa);
		multihost = spa_multihost(spa);

		last_mmp_interval = mmp_interval;
		last_mmp_fail_intervals = mmp_fail_intervals;
		last_mmp_fail_ns = mmp_fail_ns;
		mmp_interval = MSEC2NSEC(MMP_INTERVAL_OK(
		    zfs_multihost_interval));
		mmp_fail_intervals = MMP_FAIL_INTVS_OK(
		    zfs_multihost_fail_intervals);

		/* Smooth so pool is not suspended when reducing tunables */
		if (mmp_fail_intervals * mmp_interval < mmp_fail_ns) {
			mmp_fail_ns = (mmp_fail_ns * 31 +
			    mmp_fail_intervals * mmp_interval) / 32;
		} else {
			mmp_fail_ns = mmp_fail_intervals *
			    mmp_interval;
		}

		if (mmp_interval != last_mmp_interval ||
		    mmp_fail_intervals != last_mmp_fail_intervals) {
			/*
			 * We want other hosts to see new tunables as quickly as
			 * possible.  Write out at higher frequency than usual.
			 */
			skip_wait += leaves;
		}

		if (multihost)
			next_time = gethrtime() + mmp_interval / leaves;

		if (mmp_fail_ns != last_mmp_fail_ns) {
			zfs_dbgmsg("MMP interval change pool '%s' "
			    "gethrtime %llu last_mmp_interval %llu "
			    "mmp_interval %llu last_mmp_fail_intervals %u "
			    "mmp_fail_intervals %u mmp_fail_ns %llu "
			    "skip_wait %d leaves %d next_time %llu",
			    spa_name(spa), (u_longlong_t)gethrtime(),
			    (u_longlong_t)last_mmp_interval,
			    (u_longlong_t)mmp_interval, last_mmp_fail_intervals,
			    mmp_fail_intervals, (u_longlong_t)mmp_fail_ns,
			    skip_wait, leaves, (u_longlong_t)next_time);
		}

		/*
		 * MMP off => on, or suspended => !suspended:
		 * No writes occurred recently.  Update mmp_last_write to give
		 * us some time to try.
		 */
		if ((!last_spa_multihost && multihost) ||
		    (last_spa_suspended && !suspended)) {
			zfs_dbgmsg("MMP state change pool '%s': gethrtime %llu "
			    "last_spa_multihost %u multihost %u "
			    "last_spa_suspended %u suspended %u",
			    spa_name(spa), (u_longlong_t)gethrtime(),
			    last_spa_multihost, multihost, last_spa_suspended,
			    suspended);
			mutex_enter(&mmp->mmp_io_lock);
			mmp->mmp_last_write = gethrtime();
			mmp->mmp_delay = mmp_interval;
			mutex_exit(&mmp->mmp_io_lock);
		}

		/*
		 * MMP on => off:
		 * mmp_delay == 0 tells importing node to skip activity check.
		 */
		if (last_spa_multihost && !multihost) {
			mutex_enter(&mmp->mmp_io_lock);
			mmp->mmp_delay = 0;
			mutex_exit(&mmp->mmp_io_lock);
		}

		/*
		 * Suspend the pool if no MMP write has succeeded in over
		 * mmp_interval * mmp_fail_intervals nanoseconds.
		 */
		if (multihost && !suspended && mmp_fail_intervals &&
		    (gethrtime() - mmp->mmp_last_write) > mmp_fail_ns) {
			zfs_dbgmsg("MMP suspending pool '%s': gethrtime %llu "
			    "mmp_last_write %llu mmp_interval %llu "
			    "mmp_fail_intervals %llu mmp_fail_ns %llu",
			    spa_name(spa), (u_longlong_t)gethrtime(),
			    (u_longlong_t)mmp->mmp_last_write,
			    (u_longlong_t)mmp_interval,
			    (u_longlong_t)mmp_fail_intervals,
			    (u_longlong_t)mmp_fail_ns);
			cmn_err(CE_WARN, "MMP writes to pool '%s' have not "
			    "succeeded in over %llu ms; suspending pool. "
			    "Hrtime %llu",
			    spa_name(spa),
			    NSEC2MSEC(gethrtime() - mmp->mmp_last_write),
			    gethrtime());
			zio_suspend(spa, NULL, ZIO_SUSPEND_MMP);
		}

		if (multihost && !suspended)
			mmp_write_uberblock(spa);

		if (skip_wait > 0) {
			next_time = gethrtime() + MSEC2NSEC(MMP_MIN_INTERVAL) /
			    leaves;
			skip_wait--;
		}

		CALLB_CPR_SAFE_BEGIN(&cpr);
		(void) cv_timedwait_idle_hires(&mmp->mmp_thread_cv,
		    &mmp->mmp_thread_lock, next_time, USEC2NSEC(100),
		    CALLOUT_FLAG_ABSOLUTE);
		CALLB_CPR_SAFE_END(&cpr, &mmp->mmp_thread_lock);
	}

	/* Outstanding writes are allowed to complete. */
	zio_wait(mmp->mmp_zio_root);

	mmp->mmp_zio_root = NULL;
	mmp_thread_exit(mmp, &mmp->mmp_thread, &cpr);
}

/*
 * Signal the MMP thread to wake it, when it is sleeping on
 * its cv.  Used when some module parameter has changed and
 * we want the thread to know about it.
 * Only signal if the pool is active and mmp thread is
 * running, otherwise there is no thread to wake.
 */
static void
mmp_signal_thread(spa_t *spa)
{
	mmp_thread_t *mmp = &spa->spa_mmp;

	mutex_enter(&mmp->mmp_thread_lock);
	if (mmp->mmp_thread)
		cv_broadcast(&mmp->mmp_thread_cv);
	mutex_exit(&mmp->mmp_thread_lock);
}

void
mmp_signal_all_threads(void)
{
	spa_t *spa = NULL;

	mutex_enter(&spa_namespace_lock);
	while ((spa = spa_next(spa))) {
		if (spa->spa_state == POOL_STATE_ACTIVE)
			mmp_signal_thread(spa);
	}
	mutex_exit(&spa_namespace_lock);
}

/* BEGIN CSTYLED */
ZFS_MODULE_PARAM_CALL(zfs_multihost, zfs_multihost_, interval,
	param_set_multihost_interval, param_get_ulong, ZMOD_RW,
	"Milliseconds between mmp writes to each leaf");
/* END CSTYLED */

ZFS_MODULE_PARAM(zfs_multihost, zfs_multihost_, fail_intervals, UINT, ZMOD_RW,
	"Max allowed period without a successful mmp write");

ZFS_MODULE_PARAM(zfs_multihost, zfs_multihost_, import_intervals, UINT, ZMOD_RW,
	"Number of zfs_multihost_interval periods to wait for activity");
