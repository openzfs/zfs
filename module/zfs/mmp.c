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
#include <sys/dsl_pool.h>
#include <sys/mmp.h>
#include <sys/spa.h>
#include <sys/spa_impl.h>
#include <sys/vdev.h>
#include <sys/vdev_impl.h>
#include <sys/zfs_context.h>
#include <sys/callb.h>

/*
 * MMP (Multi-Modifier Protection) attempts to prevent a user from
 * importing or opening a pool on more than one host at a time, in
 * particular by issuing "zpool import -f" on one host, while the pool is
 * already imported on another host.  There are many other ways in which
 * a device could be used by two hosts for different purposes at the same
 * time, resulting in pool damage.  This implementation does not attempt
 * to detect most of those cases.
 *
 * MMP operates by ensuring there is frequent visible change on disk (a
 * "heartbeat") at all times, and altering the import process to check
 * for such change and failing the import if it is detected.
 *
 * On-disk:
 *
 * The last uberblock slot in each uberblock ring is used only for MMP;
 * uberblocks written by the sync thread always go into the first (N-1)
 * slots.  The Nth slot used for MMP is used to hold an uberblock which is
 * exactly the same as the last synced uberblock, except that its
 * ub_timestamp is frequently updated.  Like all other uberblocks, the
 * slot is written with an embedded checksum, and slots with invalid
 * checksums are ignored.  This provides the "heartbeat", with no risk of
 * overwriting good uberblocks that must be preserved, e.g. previous txg's and
 * associated blockpointers.
 *
 * Two optional fields are added to struct uberblock: ub_mmp_magic and
 * ub_mmp_delay.  The magic field allows zfs to tell whether ub_mmp_delay is
 * valid.  The delay field is a decaying average of the amount of time between
 * completion of successive MMP writes, in nanoseconds.  It is used to predict
 * how long the import must wait to detect activity in the pool, before
 * concluding it is not in use.
 *
 * ZFS module parameters:
 *
 * ulong zfs_mmp_interval is used to control mmp writes and the mmp activity
 * check on import.
 *
 * For zfs_mmp_interval == 0, no mmp writes are performed, and uberblocks
 * written contain mmp_delay == 0.  On import, no activity check is performed
 * if best uberblock mmp_delay == 0 as well as zfs_mmp_interval == 0.  This
 * provides a way for the user to effectively disable MMP (although the thread
 * always runs).
 *
 * For zfs_mmp_interval > 0, the mmp write period is zfs_mmp_interval/(# leaf
 * vdevs) milliseconds, so that on average, each leaf vdev will receive an mmp
 * write in this interval.
 *
 * uint zfs_mmp_import_intervals is used to control the activity test on
 * import.
 * The duration of the test is
 *   zfs_mmp_import_intervals * zfs_mmp_interval + random(25%)
 * If zfs_mmp_import_intervals is set to 0, the test is zfs_mmp_interval long.
 *
 * uint zfs_mmp_fail_intervals is used to respond to mmp write failures.
 *
 * For zfs_mmp_fail_intervals > 0, the MMP thread calls zio_suspend() if
 * (zfs_mmp_fail_intervals * zfs_mmp_interval) ms have passed since the last
 * successful mmp write.  This provides some assurance (not perfect) that the
 * pool will not stay active if the "heartbeat" is not visible.
 *
 * For zfs_mmp_fail_intervals == 0, the MMP thread never calls zio_suspend(),
 * regardless of how long it has been since a successful MMP write.
 *
 * During Import:
 *
 * The import process performs an activity check if the pool may be in
 * use.  An activity check is typically required if ZPOOL_CONFIG_HOSTID
 * does not match the system hostid, the pool state is POOL_STATE_ACTIVE,
 * and the pool is not a root pool.
 *
 * The activity check vdevs finds the "best" uberblock (highest txg &
 * timestamp), waits some time, and then finds the "best" uberblock again.
 * If the txg and timestamp in both "best" uberblocks do not match, the pool
 * is in use by another host and the import fails.
 *
 * If the "best" uberblock has a valid ub_mmp_delay field, the duration
 * of the test is (ub_mmp_delay * 10) nanoseconds.  Otherwise, the
 * duration of the test is (zfs_mmp_interval * 10) milliseconds.  The
 * duration is then extended by a random fraction of the original delay
 * to attempt to detect simultaneous imports, for example if both partners
 * are rebooted at the same time.
 *
 * spa_load_impl()
 * 1. Call vdev_uberblock_load and save the "best" uberblock it finds.
 * 2. Check the config nvlist for the hostlist mismatch, pool state, and
 *    root/non-root pool as described above, and skip the activity test
 *    if appropriate.
 * 3. Check the supplied config nvlist for key ZPOOL_CONFIG_IMPORT_TXG.
 *    If it exists, and its value is 1, skip the activity test.
 *    This is used by zdb to allow inspection of live pools.
 * 4. Check the supplied config nvlist for keys ZPOOL_CONFIG_IMPORT_TXG
 *    and ZPOOL_CONFIG_TIMESTAMP.  If they exist, and their values match
 *    the txg and timestamp from the "best" uberblock, skip the activity
 *    test. This avoids doing the activity check twice, when
 *    spa_tryimport() is called to validate a config before passing it to
 *    spa_import().
 * 5. If zfs_mmp_interval == 0 and ub_mmp_delay == 0, skip the activity
 *    test.
 * 6. Call vdev_uberblock_load again and compare the new txg and
 *    timestamp with those in the original "best" uberblock.  If they
 *    changed, return EBUSY.
 * 7. Repeat step 6 at intervals until the test duration is exceeded.
 * 8. Complete the rest of spa_load_impl(); if successful and sync_thread
 *    is started, start mmp_thread.
 *
 * spa_tryimport()
 * If spa_load() returns success, add keys ZPOOL_CONFIG_IMPORT_TXG and
 * ZPOOL_CONFIG_TIMESTAMP to the generated config nvlist, with values from the
 * "best" uberblock's ub_txg and ub_timestamp fields.  This allows spa_import()
 * to skip the activity as it can tell one was already performed, if necessary.
 *
 * While the pool is open:
 * mmp_thread()
 * 1. If zfs_mmp_interval == 0, go to step 6
 * 2. Randomly choose a label within a randomly chosen leaf vdev which is
 *    writable and does not have an oustanding MMP write.
 * 3. Copy the last synced uberblock from the spa and update ub_timestamp.
 * 4. Write the buffer to the MMP uberblock slot.
 * 5. If zfs_mmp_interval > 0, sleep zfs_mmp_interval/(# leaf vdevs)
 * 6. If zfs_mmp_interval == 0, sleep default zfs_mmp_interval
 * 7. Go to step 1
 *
 * Limitations:
 *
 * If the system breaks down, and the second import occurs when it should not
 * (e.g. cables pulled and then replaced on the original host), there is no
 * backup mechanism to detect this.  Using zfs_mmp_fail_intervals can help with
 * this but may not be sufficient.
 *
 * It may not detect and prevent two hosts importing the pool at the
 * same time, if their random delays are too similar.
 *
 * It does not detect changes to one or more individual devices by some outside
 * process, e.g. "zpool labelclear -f" or "zpool add -f".
 */

/*
 * Used to control mmp writes and the mmp activity check on import.
 * For zfs_mmp_interval == 0:
 *	No mmp writes are performed.
 *	Uberblocks written contain mmp_delay == 0.
 *	On import, no activity check if best uberblock mmp_delay == 0.
 *
 * For zfs_mmp_interval > 0:
 *	The mmp write period is zfs_mmp_interval/(# leaf vdevs) milliseconds,
 *	so that on average, an mmp write will be issued for each leaf vdev,
 *	every zfs_mmp_interval ms.
 *	If there are I/O delays, i.e. due to a busy disk, the mmp updates may
 *	may land on-disk at irregular, and possibly long, intervals.  mmp_delay
 *	in the uberblock written reflects the most recent interval.
 *
 * If zfs_mmp_interval > 0 or mmp_delay in best uberblock > 0:
 *	The activity check waits up to
 *	zfs_mmp_import_intervals * MAX(zfs_mmp_interval, NSEC2MSEC(mmp_delay) *
 *	(# leaf vdevs)) + (random delay factor)
 *	for activity before concluding the pool is safe to import.
 */
ulong_t zfs_mmp_interval = MMP_DEFAULT_INTERVAL;

/*
 * Used to control the duration of the activity test on import.
 * The duration of the test is
 *   zfs_mmp_import_intervals * zfs_mmp_interval + random(25%)
 * A value of 0 is ignored, and treated as if it was set to 1.
 */
uint_t zfs_mmp_import_intervals = MMP_DEFAULT_IMPORT_INTERVALS;

/*
 * Used to respond to mmp write failures.
 * For zfs_mmp_fail_intervals == 0:
 *	MMP write failures are ignored by the mmp thread, although zfs may
 *	detect failures and report them to the ZED, which in turn may take
 *	action such as suspending the pool or taking a device offline.
 *
 * For zfs_mmp_fail_intervals > 0:
 *	Suspend the pool if (zfs_mmp_fail_intervals * zfs_mmp_interval) ms
 *	have passed since the last successful mmp write.  This guarantees
 *	the activity test will see mmp writes if the pool is imported.
 */
uint_t zfs_mmp_fail_intervals = MMP_DEFAULT_FAIL_INTERVALS;

static void mmp_thread(spa_t *spa);

void
mmp_init(spa_t *spa)
{
	mmp_thread_state_t *mmp = &spa->spa_mmp;

	mutex_init(&mmp->mmp_thread_lock, NULL, MUTEX_DEFAULT, NULL);
	cv_init(&mmp->mmp_thread_cv, NULL, CV_DEFAULT, NULL);
	mutex_init(&mmp->mmp_io_lock, NULL, MUTEX_DEFAULT, NULL);
}

void
mmp_fini(spa_t *spa)
{
	mmp_thread_state_t *mmp = &spa->spa_mmp;

	mutex_destroy(&mmp->mmp_thread_lock);
	cv_destroy(&mmp->mmp_thread_cv);
	mutex_destroy(&mmp->mmp_io_lock);
}

static void
mmp_thread_enter(mmp_thread_state_t *mmp, callb_cpr_t *cpr)
{
	CALLB_CPR_INIT(cpr, &mmp->mmp_thread_lock, callb_generic_cpr, FTAG);
	mutex_enter(&mmp->mmp_thread_lock);
}

static void
mmp_thread_exit(mmp_thread_state_t *mmp, kthread_t **mpp, callb_cpr_t *cpr)
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
	mmp_thread_state_t *mmp = &spa->spa_mmp;

	if (spa_writeable(spa)) {
		mutex_enter(&mmp->mmp_thread_lock);
		if (!mmp->mmp_thread) {
			dprintf("mmp_thread_start pool %s\n",
			    spa->spa_name);
			mmp->mmp_thread = thread_create(NULL, 0, mmp_thread,
			    spa, 0, &p0, TS_RUN, defclsyspri);
		}
		mutex_exit(&mmp->mmp_thread_lock);
	}
}

void
mmp_thread_stop(spa_t *spa)
{
	mmp_thread_state_t *mmp = &spa->spa_mmp;

	mutex_enter(&mmp->mmp_thread_lock);
	mmp->mmp_thread_exiting = 1;
	cv_broadcast(&mmp->mmp_thread_cv);

	while (mmp->mmp_thread) {
		cv_wait(&mmp->mmp_thread_cv, &mmp->mmp_thread_lock);
	}
	mutex_exit(&mmp->mmp_thread_lock);

	ASSERT(mmp->mmp_thread == NULL);
	mmp->mmp_thread_exiting = 0;
}

/*
 * Randomly choose a leaf vdev, to write an MMP block to.  It must be
 * writable.  It must not have an outstanding mmp write (if so then
 * there is a problem, and a new write will also block).
 *
 * We try 10 times to pick a random leaf without an outstanding write.
 * If 90% of the leaves have pending writes, this gives us a >65%
 * chance of finding one we can write to.  There will be at least
 * (zfs_mmp_fail_intervals) tries before the inability to write an MMP
 * block causes serious problems.
 */

static vdev_t *
vdev_random_leaf(spa_t *spa)
{
	vdev_t *vd, *child;
	int pending_writes = 10;

	ASSERT(spa);
	ASSERT(spa_config_held(spa, SCL_STATE, RW_READER) == SCL_STATE);

	/*
	 * Since we hold SCL_STATE, neither pool nor vdev state can
	 * change.  Therefore, if the root is not dead, there is a
	 * child that is not dead, and so on down to a leaf.
	 */
	if (!vdev_writeable(spa->spa_root_vdev))
		return (NULL);

	vd = spa->spa_root_vdev;
	while (!vd->vdev_ops->vdev_op_leaf) {
		child = vd->vdev_child[spa_get_random(vd->vdev_children)];

		if (!vdev_writeable(child))
			continue;

		if (child->vdev_ops->vdev_op_leaf && child->vdev_mmp_pending) {
			if (pending_writes-- > 0)
				continue;
			else
				return (NULL);
		}

		vd = child;
	}
	return (vd);
}

static void
mmp_write_done(zio_t *zio)
{
	vdev_t *vd = zio->io_vd;
	mmp_thread_state_t *mts = zio->io_private;

	mutex_enter(&mts->mmp_io_lock);
	vd->vdev_mmp_pending = 0;

	if (zio->io_error)
		goto unlock;

	/*
	 * Mmp writes are queued on a fixed schedule, but under many
	 * circumstances, such as a busy device or faulty hardware,
	 * the writes will complete at variable, much longer,
	 * intervals.  In these cases, another node checking for
	 * activity must wait longer to account for these delays.
	 *
	 * Mmp_delay captures the interval between completion of the
	 * prior mmp write, and the one that just finished.  This
	 * tells the importer how soon it can expect to see activity
	 * if the pool is still imported.
	 *
	 * Do not set mmp_delay if zfs_mmp_interval == 0, so we do
	 * not trigger an activity check on import.
	 */
	if (zfs_mmp_interval) {
		hrtime_t delay = gethrtime() - mts->mmp_last_write;

		if (delay > mts->mmp_delay)
			mts->mmp_delay = delay;
		else
			mts->mmp_delay = (delay + mts->mmp_delay * 127) /
			    128;
	} else {
		mts->mmp_delay = 0;
	}
	mts->mmp_last_write = gethrtime();

unlock:
	mutex_exit(&mts->mmp_io_lock);

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
	mmp_thread_state_t *mmp = &spa->spa_mmp;

	mutex_enter(&mmp->mmp_io_lock);
	mmp->mmp_ub = *ub;
	mmp->mmp_ub.ub_timestamp = gethrestime_sec();
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
	mmp_thread_state_t *mmp = &spa->spa_mmp;
	uberblock_t *ub;
	vdev_t *vd;
	uint64_t offset;

	vd = vdev_random_leaf(spa);
	if (vd == NULL || !vdev_writeable(vd))
		return;

	mutex_enter(&mmp->mmp_io_lock);

	if (mmp->mmp_zio_root == NULL)
		mmp->mmp_zio_root = zio_root(spa, NULL, NULL,
		    flags | ZIO_FLAG_GODFATHER);

	ub = &mmp->mmp_ub;
	ub->ub_timestamp = gethrestime_sec();
	ub->ub_mmp_magic = MMP_MAGIC;
	ub->ub_mmp_delay = mmp->mmp_delay;
	vd->vdev_mmp_pending = gethrtime();

	zio_t *zio  = zio_null(mmp->mmp_zio_root, spa, NULL, NULL, NULL, flags);
	abd_t *ub_abd = abd_alloc_for_io(VDEV_UBERBLOCK_SIZE(vd), B_TRUE);
	abd_zero(ub_abd, VDEV_UBERBLOCK_SIZE(vd));
	abd_copy_from_buf(ub_abd, ub, sizeof (uberblock_t));

	mutex_exit(&mmp->mmp_io_lock);

	offset = VDEV_UBERBLOCK_OFFSET(vd, VDEV_FIRST_MMP_BLOCK(vd) +
	    spa_get_random(MMP_BLOCKS_PER_LABEL));

	vdev_label_write(zio, vd, spa_get_random(VDEV_LABELS),
	    ub_abd, offset, VDEV_UBERBLOCK_SIZE(vd), mmp_write_done, mmp,
	    flags | ZIO_FLAG_DONT_PROPAGATE);

	zio_nowait(zio);
}

static void
mmp_thread(spa_t *spa)
{
	mmp_thread_state_t *mmp = &spa->spa_mmp;
	uint64_t last_zfs_mmp_interval = zfs_mmp_interval;
	boolean_t last_spa_suspended = spa_suspended(spa);
	callb_cpr_t cpr;

	mmp_thread_enter(mmp, &cpr);

	/*
	 * The done function calculates mmp_delay based on the prior
	 * value of mmp_delay and the elapsed time since the last write.
	 * For the first mmp write, there is no "last write", so we
	 * start with fake, but reasonable, nonzero values.
	 */
	mmp->mmp_last_write = gethrtime() - MSEC2NSEC(zfs_mmp_interval);
	mmp->mmp_delay = MSEC2NSEC(zfs_mmp_interval);

	for (;;) {
		/* for stable values within an iteration */
		uint64_t mmp_fail_intervals = zfs_mmp_fail_intervals;
		uint64_t mmp_interval = MSEC2NSEC(zfs_mmp_interval);
		boolean_t suspended = spa_suspended(spa);
		hrtime_t start, next_time;
		hrtime_t max_fail_ns = (MSEC2NSEC(mmp_interval) *
		    mmp_fail_intervals);

		if (mmp->mmp_thread_exiting)
			goto cleanup_and_exit;

		start = gethrtime();
		if (mmp_interval) {
			next_time = start + MSEC2NSEC(mmp_interval) /
			    vdev_count_leaves(spa);
		} else {
			next_time = start + MSEC2NSEC(MMP_DEFAULT_INTERVAL);
		}

		/*
		 * When MMP goes off => on, or spa goes suspended =>
		 * !suspended, we know no writes occurred recently.  We
		 * update mmp_last_write to give us some time to try.
		 */
		if ((!last_zfs_mmp_interval && mmp_interval) ||
		    (last_spa_suspended && !suspended)) {
			dprintf("mmp_thread transtion: pool %s last interval "
			    "%lu interval %lu last suspended %d suspended %d\n",
			    spa->spa_name, last_zfs_mmp_interval, mmp_interval,
			    last_spa_suspended, suspended);
			mutex_enter(&mmp->mmp_io_lock);
			mmp->mmp_last_write = gethrtime();
			mutex_exit(&mmp->mmp_io_lock);
		} else if (last_zfs_mmp_interval && !mmp_interval) {
			mutex_enter(&mmp->mmp_io_lock);
			mmp->mmp_delay = 0;
			mutex_exit(&mmp->mmp_io_lock);
		}
		last_zfs_mmp_interval = mmp_interval;
		last_spa_suspended = suspended;

		/*
		 * Check after the transition check above.
		 */
		if (!suspended && mmp_fail_intervals && mmp_interval &&
		    (start - mmp->mmp_last_write) > max_fail_ns) {
			dprintf("mmp suspending pool: pool %s "
			    "zfs_mmp_interval %lu zfs_mmp_fail_intervals %lu "
			    "mmp_last_write %llu now %llu\n", spa->spa_name,
			    mmp_interval, mmp_fail_intervals,
			    mmp->mmp_last_write, start);
			zio_suspend(spa, NULL);
		}

		if (zfs_mmp_interval) {
			spa_config_enter(spa, SCL_STATE, FTAG, RW_READER);
			mmp_write_uberblock(spa);
			spa_config_exit(spa, SCL_STATE, FTAG);
		}

		if (gethrtime() >= next_time)
			continue;

		CALLB_CPR_SAFE_BEGIN(&cpr);
		(void) cv_timedwait_hires(&mmp->mmp_thread_cv,
		    &mmp->mmp_thread_lock, next_time, 1,
		    CALLOUT_FLAG_ABSOLUTE);
		CALLB_CPR_SAFE_END(&cpr, &mmp->mmp_thread_lock);
	}

cleanup_and_exit:
	if (mmp->mmp_zio_root)		/* let outstanding writes complete */
		zio_wait(mmp->mmp_zio_root);
	mmp->mmp_zio_root = NULL;
	mmp_thread_exit(mmp, &mmp->mmp_thread, &cpr);
}

#if defined(_KERNEL) && defined(HAVE_SPL)
EXPORT_SYMBOL(mmp_init);
EXPORT_SYMBOL(mmp_fini);
EXPORT_SYMBOL(mmp_thread_start);
EXPORT_SYMBOL(mmp_thread_stop);

/* BEGIN CSTYLED */
module_param(zfs_mmp_fail_intervals, uint, 0644);
MODULE_PARM_DESC(zfs_mmp_fail_intervals,
	"Max allowed period without a successful mmp write");

module_param(zfs_mmp_interval, ulong, 0644);
MODULE_PARM_DESC(zfs_mmp_interval,
	"Milliseconds between mmp writes to each leaf");

module_param(zfs_mmp_import_intervals, uint, 0644);
MODULE_PARM_DESC(zfs_mmp_import_intervals,
	"Number of zfs_mmp_interval periods to wait for activity");
/* END CSTYLED */
#endif
