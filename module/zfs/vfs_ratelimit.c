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
 * Copyright (c) 2024 The FreeBSD Foundation
 *
 * This software was developed by Pawel Dawidek <pawel@dawidek.net>
 * under sponsorship from the FreeBSD Foundation.
 */

#include <sys/zfs_context.h>
#include <sys/dmu_objset.h>
#include <sys/dsl_dataset.h>
#include <sys/dsl_dir.h>
#include <sys/spa_impl.h>
#include <sys/vfs_ratelimit.h>
#include <sys/zfs_vfsops.h>

/*
 * The following comment describes rate limiting bandwidth and operations for
 * datasets that have the ratelimit property configured.
 *
 * The goal was to provide practically useful rate limiting for ZFS
 * without introducing any performance degradation when the limits are
 * configured, but not exceeded.
 *
 * The rate limiting is applied at the VFS level for file systems, before going
 * to DMU. The limits are not applied at the disk level. This means that even if
 * no disk access is required to perform the given operation, the dataset is
 * still charged for it.
 * The reasons for this design choice are the following:
 * - It would be impossible or at least very complicated to enforce such limits
 *   at the VDEV level, especially for writes. At that point the writes are
 *   already assigned to the specific txg and wating here would mean the whole
 *   pool has to wait.
 * - It would be hard to predict what limits should be configured as there are a
 *   lot of factors that dictate how much disk bandwidth is really required
 *   (due to RAIDZ inflation, compression, gang blocks, deduplication,
 *    block cloning, NOP writes, I/O aggregation, metadata traffic, etc.).
 * By enforcing the limits at the VFS level for file system operations it should
 * be easy to find out what limits applications require and verify that the
 * limits are correctly enforced by monitoring system calls issued by the
 * applications.
 *
 * Bandwidth and operation limits are divided into three types: read, write and
 * total, where total is a combined limit for reads and writes.
 *
 * Each dataset can have its own limits configured. The configured limits are
 * enforced on the dataset and all its children - limits are hierarchical,
 * like quota. Even if a child dataset has a higher limit configured than its
 * parent, it cannot go beyond its parent limit.
 *
 * Dataset can have only selected limits configured (eg. read bandwidth and
 * write operations, but not the rest).
 *
 * The limits are stored in the vfs_ratelimit structure and attached to the
 * dsl_dir of the dataset we have configured the ratelimit properties on.
 * We walk down the dataset tree and set dd_ratelimit_root field to point to
 * this dsl_dir until we find dsl_dir that also has the vfs_ratelimit structure
 * already attached to it (which means it has its own limits configured).
 * During the accounting it allows us to quickly access the ratelimit
 * structure we need by just going to ds_dir->dd_ratelimit_root;
 * If ratelimits are not configured on this dataset and all of its ancestors,
 * the ds_dir->dd_ratelimit_root will be set to NULL, so we know we don't
 * have to do any accounting.
 *
 * The limits are configured per second, but we divde the second and the limits
 * into RATELIMIT_RESOLUTION slots (16 by default). This is to avoid a choking
 * effect, when process is doing progress in 1s steps. For example if we have
 * read bandwidth limits configured to 100MB/s and the process is trying to
 * read 130MB, it will take 1.3 seconds, not 2 seconds.
 * Note that very low limits may be rounded up - 7 ops/s limit will be rounded
 * up to 16 ops/s, so each time slot is assigned 1 op/s limit. This rounding up
 * is done in the kernel and isn't shown in the properties.
 *
 * How does the accounting work?
 *
 * When a request comes, we may need to consider multiple limits.
 * For example a data read request of eg. 192kB (with 128kB recordsize) is
 * accounted as 192kB bandwidth read, 192kB bandwidth total, two operations read
 * and two operations total. Not all of those limits have to be configured or
 * some might be configured on a dataset and others on a parent dataset(s).
 *
 * For each type we use two fields to track the wait times: rl_timeslot and
 * rl_reminder. rl_timeslot holds the point in time up to which the last
 * processes is waiting for. If the rl_timeslot is lower than the current time,
 * it means that no processes are waiting. rl_reminder is the amount of data
 * modulo the limit. For example if we have a read bandwidth limit of 64MB/s,
 * so it is 4MB per 1/16s. The process is trying to read 11MB. This would
 * give us rl_timeslot = now + 2 (we account for 2 full time slots of 1/16s)
 * and rl_reminder = 3MB. This process has to sleep for 2/16s. When immediately
 * another process is trying to read 1MB, this 1MB will be added to the current
 * rl_reminder giving 4MB, so full limit unit for 1/16s. Now rl_timeslot will
 * be set to now + 3 and rl_reminder to 0. The last process is going to sleep
 * for 3/16s.
 */

/*
 * Number of slots we divide one second into. More granularity is better for
 * interactivity, but for small limits we may lose some precision.
 */
#define	RATELIMIT_RESOLUTION	16

struct vfs_ratelimit {
	kmutex_t	rl_lock;
	uint64_t	rl_limits[ZFS_RATELIMIT_NTYPES];
	uint64_t	rl_timeslot[ZFS_RATELIMIT_NTYPES];
	uint64_t	rl_reminder[ZFS_RATELIMIT_NTYPES];
};

int
vfs_ratelimit_prop_to_type(zfs_prop_t prop)
{

	switch (prop) {
	case ZFS_PROP_RATELIMIT_BW_READ:
		return (ZFS_RATELIMIT_BW_READ);
	case ZFS_PROP_RATELIMIT_BW_WRITE:
		return (ZFS_RATELIMIT_BW_WRITE);
	case ZFS_PROP_RATELIMIT_BW_TOTAL:
		return (ZFS_RATELIMIT_BW_TOTAL);
	case ZFS_PROP_RATELIMIT_OP_READ:
		return (ZFS_RATELIMIT_OP_READ);
	case ZFS_PROP_RATELIMIT_OP_WRITE:
		return (ZFS_RATELIMIT_OP_WRITE);
	case ZFS_PROP_RATELIMIT_OP_TOTAL:
		return (ZFS_RATELIMIT_OP_TOTAL);
	default:
		panic("Invalid property %d", prop);
	}
}

zfs_prop_t
vfs_ratelimit_type_to_prop(int type)
{

	switch (type) {
	case ZFS_RATELIMIT_BW_READ:
		return (ZFS_PROP_RATELIMIT_BW_READ);
	case ZFS_RATELIMIT_BW_WRITE:
		return (ZFS_PROP_RATELIMIT_BW_WRITE);
	case ZFS_RATELIMIT_BW_TOTAL:
		return (ZFS_PROP_RATELIMIT_BW_TOTAL);
	case ZFS_RATELIMIT_OP_READ:
		return (ZFS_PROP_RATELIMIT_OP_READ);
	case ZFS_RATELIMIT_OP_WRITE:
		return (ZFS_PROP_RATELIMIT_OP_WRITE);
	case ZFS_RATELIMIT_OP_TOTAL:
		return (ZFS_PROP_RATELIMIT_OP_TOTAL);
	default:
		panic("Invalid type %d", type);
	}
}

static boolean_t
ratelimit_is_none(const uint64_t *limits)
{

	for (int i = ZFS_RATELIMIT_FIRST; i <= ZFS_RATELIMIT_LAST; i++) {
		if (limits[i] != 0) {
			return (B_FALSE);
		}
	}

	return (B_TRUE);
}

struct vfs_ratelimit *
vfs_ratelimit_alloc(const uint64_t *limits)
{
	struct vfs_ratelimit *rl;
	int i;

	ASSERT(limits == NULL || !ratelimit_is_none(limits));

	rl = kmem_zalloc(sizeof (*rl), KM_SLEEP);

	mutex_init(&rl->rl_lock, NULL, MUTEX_DEFAULT, NULL);

	if (limits != NULL) {
		for (i = ZFS_RATELIMIT_FIRST; i <= ZFS_RATELIMIT_LAST; i++) {
			uint64_t limit;

			/*
			 * We cannot have limits lower than RATELIMIT_RESOLUTION
			 * as they will effectively be zero, so unlimited.
			 */
			limit = limits[i];
			if (limit > 0 && limit < RATELIMIT_RESOLUTION) {
				limit = RATELIMIT_RESOLUTION;
			}
			rl->rl_limits[i] = limit / RATELIMIT_RESOLUTION;
		}
	}

	return (rl);
}

void
vfs_ratelimit_free(struct vfs_ratelimit *rl)
{

	if (rl == NULL) {
		return;
	}

	mutex_destroy(&rl->rl_lock);

	kmem_free(rl, sizeof (*rl));
}

/*
 * If this change will make all the limits to be 0, we free the vfs_ratelimit
 * structure and return NULL.
 */
struct vfs_ratelimit *
vfs_ratelimit_set(struct vfs_ratelimit *rl, zfs_prop_t prop, uint64_t limit)
{
	int type;

	if (rl == NULL) {
		if (limit == 0) {
			return (NULL);
		} else {
			rl = vfs_ratelimit_alloc(NULL);
		}
	}

	type = vfs_ratelimit_prop_to_type(prop);
	if (limit > 0 && limit < RATELIMIT_RESOLUTION) {
		limit = RATELIMIT_RESOLUTION;
	}
	rl->rl_limits[type] = limit / RATELIMIT_RESOLUTION;

	if (ratelimit_is_none(rl->rl_limits)) {
		vfs_ratelimit_free(rl);
		return (NULL);
	}

	return (rl);
}

static __inline hrtime_t
gettimeslot(void)
{
	inode_timespec_t ts;

	gethrestime(&ts);

	return (((hrtime_t)ts.tv_sec * RATELIMIT_RESOLUTION) +
	    ts.tv_nsec / (NANOSEC / RATELIMIT_RESOLUTION));
}

/*
 * Returns bit mask of the types configured for the given ratelimit structure.
 */
static int
ratelimit_types(const uint64_t *counts)
{
	int types, type;

	types = 0;
	for (type = ZFS_RATELIMIT_FIRST; type <= ZFS_RATELIMIT_LAST; type++) {
		if (counts[type] > 0) {
			types |= (1 << type);
		}
	}

	return (types);
}

/*
 * Returns the ratelimit structure that includes one of the requested types
 * configured on the given dataset (os). If the given dataset doesn't have
 * ratelimit structure for one of the types, we walk up dataset tree trying
 * to find a dataset that has limits configured for one of the types we are
 * interested in.
 */
static dsl_dir_t *
ratelimit_first(objset_t *os, int types)
{
	dsl_dir_t *dd;

	ASSERT(RRM_READ_HELD(&os->os_spa->spa_ratelimit_lock));

	dd = os->os_dsl_dataset->ds_dir->dd_ratelimit_root;
	for (;;) {
		if (dd == NULL) {
			return (NULL);
		}
		if (dd->dd_ratelimit != NULL) {
			int mytypes;

			mytypes = ratelimit_types(dd->dd_ratelimit->rl_limits);
			if ((mytypes & types) != 0) {
				/*
				 * This dataset has at last one limit we are
				 * interested in.
				 */
				return (dd);
			}
		}
		if (dd->dd_parent == NULL) {
			return (NULL);
		}
		dd = dd->dd_parent->dd_ratelimit_root;
	}
}

/*
 * Returns the ratelimit structure of the parent dataset. If the parent dataset
 * has no ratelimit structure configured or the ratelimit structure doesn't
 * include any of the types we are interested in, we walk up and continue our
 * search.
 */
static dsl_dir_t *
ratelimit_parent(dsl_dir_t *dd, int types)
{
	ASSERT(RRM_READ_HELD(&dd->dd_pool->dp_spa->spa_ratelimit_lock));

	for (;;) {
		if (dd->dd_parent == NULL) {
			return (NULL);
		}
		dd = dd->dd_parent->dd_ratelimit_root;
		if (dd == NULL) {
			return (NULL);
		}
		if (dd->dd_ratelimit != NULL) {
			int mytypes;

			mytypes = ratelimit_types(dd->dd_ratelimit->rl_limits);
			if ((mytypes & types) != 0) {
				/*
				 * This dataset has at last one limit we are
				 * interested in.
				 */
				return (dd);
			}
		}
	}
}

/*
 * Account for our request across all the types configured in this ratelimit
 * structure.
 * Return a timeslot we should wait for or now if we can execute the request
 * without waiting (we are within limits).
 */
static hrtime_t
ratelimit_account(struct vfs_ratelimit *rl, hrtime_t now,
    const uint64_t *counts)
{
	hrtime_t timeslot;
	int type;

	timeslot = now;

	mutex_enter(&rl->rl_lock);

	for (type = ZFS_RATELIMIT_FIRST; type <= ZFS_RATELIMIT_LAST; type++) {
		uint64_t count;

		if (rl->rl_limits[type] == 0) {
			/* This type has no limit configured on this dataset. */
			continue;
		}
		count = counts[type];
		if (count == 0) {
			/* Not interested in this type. */
			continue;
		}

		if (rl->rl_timeslot[type] < now) {
			rl->rl_reminder[type] = 0;
			rl->rl_timeslot[type] = now;
		} else {
			count += rl->rl_reminder[type];
		}

		rl->rl_timeslot[type] += count / rl->rl_limits[type];
		rl->rl_reminder[type] = count % rl->rl_limits[type];

		if (timeslot < rl->rl_timeslot[type]) {
			timeslot = rl->rl_timeslot[type];
		}
	}

	mutex_exit(&rl->rl_lock);

	return (timeslot);
}

static hrtime_t
ratelimit_account_all(objset_t *os, const uint64_t *counts)
{
	dsl_dir_t *dd;
	hrtime_t now, timeslot;
	int types;

	ASSERT(RRM_READ_HELD(&os->os_spa->spa_ratelimit_lock));

	types = ratelimit_types(counts);
	now = timeslot = gettimeslot();

	for (dd = ratelimit_first(os, types); dd != NULL;
	    dd = ratelimit_parent(dd, types)) {
		hrtime_t ts;

		ts = ratelimit_account(dd->dd_ratelimit, now, counts);
		if (ts > timeslot) {
			timeslot = ts;
		}
	}

	return (timeslot);
}

static int
ratelimit_sleep(hrtime_t timeslot)
{
	hrtime_t now;
	int error = 0;

	now = gettimeslot();

	if (timeslot > now) {
		/*
		 * Too much traffic, slow it down.
		 */
#ifdef _KERNEL
		if (delay_sig((hz / RATELIMIT_RESOLUTION) * (timeslot - now))) {
			error = SET_ERROR(EINTR);
		}
#else
		delay((hz / RATELIMIT_RESOLUTION) * (timeslot - now));
#endif
	}

	return (error);
}

static int
vfs_ratelimit_sleep(objset_t *os, const uint64_t *counts)
{
	hrtime_t timeslot;

	/*
	 * Prevents configuration changes when we have requests in-flight.
	 */
	rrm_enter_read(&os->os_spa->spa_ratelimit_lock, FTAG);

	timeslot = ratelimit_account_all(os, counts);

	rrm_exit(&os->os_spa->spa_ratelimit_lock, FTAG);

	return (ratelimit_sleep(timeslot));
}

/*
 * For every data read we charge:
 * - bytes of read bandwidth
 * - bytes of total bandwidth
 * - (bytes + blocksize - 1) / blocksize of read operations
 * - (bytes + blocksize - 1) / blocksize of total operations
 */
int
vfs_ratelimit_data_read(objset_t *os, size_t blocksize, size_t bytes)
{
	uint64_t counts[ZFS_RATELIMIT_NTYPES];
	size_t operations;

	if (bytes == 0) {
		return (0);
	}
	if (blocksize == 0) {
		blocksize = bytes;
	}
	operations = (bytes + blocksize - 1) / blocksize;

	memset(counts, 0, sizeof (counts));
	counts[ZFS_RATELIMIT_BW_READ] = bytes;
	counts[ZFS_RATELIMIT_BW_TOTAL] = bytes;
	counts[ZFS_RATELIMIT_OP_READ] = operations;
	counts[ZFS_RATELIMIT_OP_TOTAL] = operations;

	return (vfs_ratelimit_sleep(os, counts));
}

/*
 * For every data write we charge:
 * - bytes of write bandwidth
 * - bytes of total bandwidth
 * - (bytes + blocksize - 1) / blocksize of read operations
 * - (bytes + blocksize - 1) / blocksize of total operations
 */
int
vfs_ratelimit_data_write(objset_t *os, size_t blocksize, size_t bytes)
{
	uint64_t counts[ZFS_RATELIMIT_NTYPES];
	size_t operations;

	if (bytes == 0) {
		return (0);
	}
	if (blocksize == 0) {
		blocksize = bytes;
	}
	operations = (bytes + blocksize - 1) / blocksize;

	memset(counts, 0, sizeof (counts));
	counts[ZFS_RATELIMIT_BW_WRITE] = bytes;
	counts[ZFS_RATELIMIT_BW_TOTAL] = bytes;
	counts[ZFS_RATELIMIT_OP_WRITE] = operations;
	counts[ZFS_RATELIMIT_OP_TOTAL] = operations;

	return (vfs_ratelimit_sleep(os, counts));
}

int
vfs_ratelimit_data_copy(objset_t *srcos, objset_t *dstos, size_t blocksize,
    size_t bytes)
{
	uint64_t counts[ZFS_RATELIMIT_NTYPES];
	size_t operations;
	hrtime_t dstts, srcts;
	spa_t *spa = srcos->os_spa;

	if (bytes == 0) {
		return (0);
	}
	if (blocksize == 0) {
		blocksize = bytes;
	}
	operations = (bytes + blocksize - 1) / blocksize;

	/*
	 * Prevents configuration changes when we have requests in-flight.
	 */
	rrm_enter_read(&spa->spa_ratelimit_lock, FTAG);

	memset(counts, 0, sizeof (counts));
	counts[ZFS_RATELIMIT_BW_READ] = bytes;
	counts[ZFS_RATELIMIT_BW_TOTAL] = bytes;
	counts[ZFS_RATELIMIT_OP_READ] = operations;
	counts[ZFS_RATELIMIT_OP_TOTAL] = operations;

	srcts = ratelimit_account_all(srcos, counts);

	memset(counts, 0, sizeof (counts));
	counts[ZFS_RATELIMIT_BW_WRITE] = bytes;
	counts[ZFS_RATELIMIT_BW_TOTAL] = bytes;
	counts[ZFS_RATELIMIT_OP_WRITE] = operations;
	counts[ZFS_RATELIMIT_OP_TOTAL] = operations;

	dstts = ratelimit_account_all(dstos, counts);

	rrm_exit(&spa->spa_ratelimit_lock, FTAG);

	return (ratelimit_sleep(dstts > srcts ? dstts : srcts));
}

/*
 * For every metadata read we charge:
 * - one read operation
 * - one total operation
 */
int
vfs_ratelimit_metadata_read(objset_t *os)
{
	uint64_t counts[ZFS_RATELIMIT_NTYPES];

	memset(counts, 0, sizeof (counts));
	counts[ZFS_RATELIMIT_OP_READ] = 1;
	counts[ZFS_RATELIMIT_OP_TOTAL] = 1;

	return (vfs_ratelimit_sleep(os, counts));
}

/*
 * For every metadata write we charge:
 * - one read operation
 * - one total operation
 */
int
vfs_ratelimit_metadata_write(objset_t *os)
{
	uint64_t counts[ZFS_RATELIMIT_NTYPES];

	memset(counts, 0, sizeof (counts));
	counts[ZFS_RATELIMIT_OP_WRITE] = 1;
	counts[ZFS_RATELIMIT_OP_TOTAL] = 1;

	return (vfs_ratelimit_sleep(os, counts));
}

/*
 * Function spins until timeout is reached or the process received a signal.
 * This function is different than ratelimit_sleep(), because pause_sig()
 * might not be woken up by a signal if the process has multiple threads.
 * We use *_spin() functions for zfs send/recv where kernel starts additional
 * kernel threads and interrupting userland process with CTRL+C (SIGINT)
 * doesn't interrupt pause_sig() waiting in another kernel thread.
 */
static void
ratelimit_spin(objset_t *os, const uint64_t *counts)
{
	hrtime_t timeslot;

	/*
	 * Prevents configuration changes when we have requests in-flight.
	 */
	rrm_enter_read(&os->os_spa->spa_ratelimit_lock, FTAG);

	timeslot = ratelimit_account_all(os, counts);

	rrm_exit(&os->os_spa->spa_ratelimit_lock, FTAG);

	while (timeslot > gettimeslot() && !issig()) {
		delay(hz / RATELIMIT_RESOLUTION);
	}
}

void
vfs_ratelimit_data_read_spin(objset_t *os, size_t blocksize, size_t bytes)
{
	uint64_t counts[ZFS_RATELIMIT_NTYPES];
	size_t operations;

	if (bytes == 0) {
		return;
	}

	if (blocksize == 0) {
		blocksize = bytes;
	}
	operations = (bytes + blocksize - 1) / blocksize;

	memset(counts, 0, sizeof (counts));
	counts[ZFS_RATELIMIT_BW_READ] = bytes;
	counts[ZFS_RATELIMIT_BW_TOTAL] = bytes;
	counts[ZFS_RATELIMIT_OP_READ] = operations;
	counts[ZFS_RATELIMIT_OP_TOTAL] = operations;

	ratelimit_spin(os, counts);
}

void
vfs_ratelimit_data_write_spin(objset_t *os, size_t blocksize, size_t bytes)
{
	uint64_t counts[ZFS_RATELIMIT_NTYPES];
	size_t operations;

	if (bytes == 0) {
		return;
	}

	if (blocksize == 0) {
		blocksize = bytes;
	}
	operations = (bytes + blocksize - 1) / blocksize;

	memset(counts, 0, sizeof (counts));
	counts[ZFS_RATELIMIT_BW_WRITE] = bytes;
	counts[ZFS_RATELIMIT_BW_TOTAL] = bytes;
	counts[ZFS_RATELIMIT_OP_WRITE] = operations;
	counts[ZFS_RATELIMIT_OP_TOTAL] = operations;

	ratelimit_spin(os, counts);
}
