// SPDX-License-Identifier: CDDL-1.0
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
#include <sys/zfs_iolimit.h>
#include <sys/zfs_vfsops.h>

/*
 * The following comment describes rate limiting bandwidth and operations for
 * datasets that have the iolimit property configured.
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
 * The limits are stored in the zfs_iolimit structure and attached to the
 * dsl_dir of the dataset we have configured the iolimit properties on.
 * We walk down the dataset tree and set dd_iolimit_root field to point to
 * this dsl_dir until we find dsl_dir that also has the zfs_iolimit structure
 * already attached to it (which means it has its own limits configured).
 * During the accounting it allows us to quickly access the iolimit
 * structure we need by just going to ds_dir->dd_iolimit_root;
 * If iolimits are not configured on this dataset and all of its ancestors,
 * the ds_dir->dd_iolimit_root will be set to NULL, so we know we don't
 * have to do any accounting.
 *
 * The limits are configured per second, but we divde the second and the limits
 * into IOLIMIT_RESOLUTION slots (16 by default). This is to avoid a choking
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
 * accounted as 192kB bandwidth read, 192kB bandwidth total, two read operations
 * and two total operations. Not all of those limits have to be configured or
 * some might be configured on a dataset and others on a parent dataset(s).
 *
 * For each type we use two fields to track the wait times: iol_timeslot and
 * iol_reminder. iol_timeslot holds the point in time up to which the last
 * processes is waiting for. If the iol_timeslot is lower than the current time,
 * it means that no processes are waiting. iol_reminder is the amount of data
 * modulo the limit. For example if we have a read bandwidth limit of 64MB/s,
 * so it is 4MB per 1/16s. The process is trying to read 11MB. This would
 * give us iol_timeslot = now + 2 (we account for 2 full time slots of 1/16s)
 * and iol_reminder = 3MB. This process has to sleep for 2/16s. When immediately
 * another process is trying to read 1MB, this 1MB will be added to the current
 * iol_reminder giving 4MB, so full limit unit for 1/16s. Now iol_timeslot will
 * be set to now + 3 and iol_reminder to 0. The last process is going to sleep
 * for 3/16s.
 */

/*
 * Number of slots we divide one second into. More granularity is better for
 * interactivity, but for small limits we may lose some precision.
 */
#define	IOLIMIT_RESOLUTION	16

struct zfs_iolimit {
	kmutex_t	iol_lock;
	uint64_t	iol_limits[ZFS_IOLIMIT_NTYPES];
	uint64_t	iol_timeslot[ZFS_IOLIMIT_NTYPES];
	uint64_t	iol_reminder[ZFS_IOLIMIT_NTYPES];
};

int
zfs_iolimit_prop_to_type(zfs_prop_t prop)
{

	switch (prop) {
	case ZFS_PROP_IOLIMIT_BW_READ:
		return (ZFS_IOLIMIT_BW_READ);
	case ZFS_PROP_IOLIMIT_BW_WRITE:
		return (ZFS_IOLIMIT_BW_WRITE);
	case ZFS_PROP_IOLIMIT_BW_TOTAL:
		return (ZFS_IOLIMIT_BW_TOTAL);
	case ZFS_PROP_IOLIMIT_OP_READ:
		return (ZFS_IOLIMIT_OP_READ);
	case ZFS_PROP_IOLIMIT_OP_WRITE:
		return (ZFS_IOLIMIT_OP_WRITE);
	case ZFS_PROP_IOLIMIT_OP_TOTAL:
		return (ZFS_IOLIMIT_OP_TOTAL);
	default:
		panic("Invalid property %d", prop);
	}
}

zfs_prop_t
zfs_iolimit_type_to_prop(int type)
{

	switch (type) {
	case ZFS_IOLIMIT_BW_READ:
		return (ZFS_PROP_IOLIMIT_BW_READ);
	case ZFS_IOLIMIT_BW_WRITE:
		return (ZFS_PROP_IOLIMIT_BW_WRITE);
	case ZFS_IOLIMIT_BW_TOTAL:
		return (ZFS_PROP_IOLIMIT_BW_TOTAL);
	case ZFS_IOLIMIT_OP_READ:
		return (ZFS_PROP_IOLIMIT_OP_READ);
	case ZFS_IOLIMIT_OP_WRITE:
		return (ZFS_PROP_IOLIMIT_OP_WRITE);
	case ZFS_IOLIMIT_OP_TOTAL:
		return (ZFS_PROP_IOLIMIT_OP_TOTAL);
	default:
		panic("Invalid type %d", type);
	}
}

static boolean_t
iolimit_is_none(const uint64_t *limits)
{

	for (int i = ZFS_IOLIMIT_FIRST; i <= ZFS_IOLIMIT_LAST; i++) {
		if (limits[i] != 0) {
			return (B_FALSE);
		}
	}

	return (B_TRUE);
}

struct zfs_iolimit *
zfs_iolimit_alloc(const uint64_t *limits)
{
	struct zfs_iolimit *iol;
	int i;

	ASSERT(limits == NULL || !iolimit_is_none(limits));

	iol = kmem_zalloc(sizeof (*iol), KM_SLEEP);

	mutex_init(&iol->iol_lock, NULL, MUTEX_DEFAULT, NULL);

	if (limits != NULL) {
		for (i = ZFS_IOLIMIT_FIRST; i <= ZFS_IOLIMIT_LAST; i++) {
			uint64_t limit;

			/*
			 * We cannot have limits lower than IOLIMIT_RESOLUTION
			 * as they will effectively be zero, so unlimited.
			 */
			limit = limits[i];
			if (limit > 0 && limit < IOLIMIT_RESOLUTION) {
				limit = IOLIMIT_RESOLUTION;
			}
			iol->iol_limits[i] = limit / IOLIMIT_RESOLUTION;
		}
	}

	return (iol);
}

void
zfs_iolimit_free(struct zfs_iolimit *iol)
{

	if (iol == NULL) {
		return;
	}

	mutex_destroy(&iol->iol_lock);

	kmem_free(iol, sizeof (*iol));
}

/*
 * If this change will make all the limits to be 0, we free the zfs_iolimit
 * structure and return NULL.
 */
struct zfs_iolimit *
zfs_iolimit_set(struct zfs_iolimit *iol, zfs_prop_t prop, uint64_t limit)
{
	int type;

	if (iol == NULL) {
		if (limit == 0) {
			return (NULL);
		} else {
			iol = zfs_iolimit_alloc(NULL);
		}
	}

	type = zfs_iolimit_prop_to_type(prop);
	if (limit > 0 && limit < IOLIMIT_RESOLUTION) {
		limit = IOLIMIT_RESOLUTION;
	}
	iol->iol_limits[type] = limit / IOLIMIT_RESOLUTION;

	if (iolimit_is_none(iol->iol_limits)) {
		zfs_iolimit_free(iol);
		return (NULL);
	}

	return (iol);
}

static __inline hrtime_t
gettimeslot(void)
{
	inode_timespec_t ts;

	gethrestime(&ts);

	return (((hrtime_t)ts.tv_sec * IOLIMIT_RESOLUTION) +
	    ts.tv_nsec / (NANOSEC / IOLIMIT_RESOLUTION));
}

/*
 * Returns bit mask of the types configured for the given iolimit structure.
 */
static int
iolimit_types(const uint64_t *counts)
{
	int types, type;

	types = 0;
	for (type = ZFS_IOLIMIT_FIRST; type <= ZFS_IOLIMIT_LAST; type++) {
		if (counts[type] > 0) {
			types |= (1 << type);
		}
	}

	return (types);
}

static boolean_t
iolimit_exists(objset_t *os)
{
	return (os->os_dsl_dataset->ds_dir->dd_iolimit_root != NULL);
}

/*
 * Returns the iolimit structure that includes one of the requested types
 * configured on the given dataset (os). If the given dataset doesn't have
 * iolimit structure for one of the types, we walk up dataset tree trying
 * to find a dataset that has limits configured for one of the types we are
 * interested in.
 */
static dsl_dir_t *
iolimit_first(objset_t *os, int types)
{
	dsl_dir_t *dd;

	ASSERT(RRM_READ_HELD(&os->os_spa->spa_iolimit_lock));

	dd = os->os_dsl_dataset->ds_dir->dd_iolimit_root;
	for (;;) {
		if (dd == NULL) {
			return (NULL);
		}
		if (dd->dd_iolimit != NULL) {
			int mytypes;

			mytypes = iolimit_types(dd->dd_iolimit->iol_limits);
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
		dd = dd->dd_parent->dd_iolimit_root;
	}
}

/*
 * Returns the iolimit structure of the parent dataset. If the parent dataset
 * has no iolimit structure configured or the iolimit structure doesn't
 * include any of the types we are interested in, we walk up and continue our
 * search.
 */
static dsl_dir_t *
iolimit_parent(dsl_dir_t *dd, int types)
{
	ASSERT(RRM_READ_HELD(&dd->dd_pool->dp_spa->spa_iolimit_lock));

	for (;;) {
		if (dd->dd_parent == NULL) {
			return (NULL);
		}
		dd = dd->dd_parent->dd_iolimit_root;
		if (dd == NULL) {
			return (NULL);
		}
		if (dd->dd_iolimit != NULL) {
			int mytypes;

			mytypes = iolimit_types(dd->dd_iolimit->iol_limits);
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
 * Charge for our request across all the types configured in this iolimit
 * structure.
 * Return a timeslot we should wait for or now if we can execute the request
 * without waiting (we are within limits).
 */
static hrtime_t
iolimit_charge(struct zfs_iolimit *iol, hrtime_t now, const uint64_t *counts)
{
	hrtime_t timeslot;
	int type;

	timeslot = now;

	mutex_enter(&iol->iol_lock);

	for (type = ZFS_IOLIMIT_FIRST; type <= ZFS_IOLIMIT_LAST; type++) {
		uint64_t count;

		if (iol->iol_limits[type] == 0) {
			/* This type has no limit configured on this dataset. */
			continue;
		}
		count = counts[type];
		if (count == 0) {
			/* Not interested in this type. */
			continue;
		}

		if (iol->iol_timeslot[type] < now) {
			iol->iol_timeslot[type] = now;
			iol->iol_reminder[type] = 0;
		} else {
			count += iol->iol_reminder[type];
		}

		iol->iol_timeslot[type] += count / iol->iol_limits[type];
		iol->iol_reminder[type] = count % iol->iol_limits[type];

		if (timeslot < iol->iol_timeslot[type]) {
			timeslot = iol->iol_timeslot[type];
		}
	}

	mutex_exit(&iol->iol_lock);

	return (timeslot);
}

static hrtime_t
iolimit_charge_all(objset_t *os, const uint64_t *counts)
{
	dsl_dir_t *dd;
	hrtime_t now, timeslot;
	int types;

	ASSERT(RRM_READ_HELD(&os->os_spa->spa_iolimit_lock));

	types = iolimit_types(counts);
	now = timeslot = gettimeslot();

	for (dd = iolimit_first(os, types); dd != NULL;
	    dd = iolimit_parent(dd, types)) {
		hrtime_t ts;

		ts = iolimit_charge(dd->dd_iolimit, now, counts);
		if (ts > timeslot) {
			timeslot = ts;
		}
	}

	return (timeslot);
}

/*
 * Reimburse the iolimit charge when an I/O operation is interrupted.
 */
static void
iolimit_reimburse(struct zfs_iolimit *iol, hrtime_t now, const uint64_t *counts)
{
	int type;

	mutex_enter(&iol->iol_lock);

	for (type = ZFS_IOLIMIT_FIRST; type <= ZFS_IOLIMIT_LAST; type++) {
		uint64_t count, reminder;

		if (iol->iol_limits[type] == 0) {
			/* This type has no limit configured on this dataset. */
			continue;
		}
		count = counts[type];
		if (count == 0) {
			/* Not interested in this type. */
			continue;
		}

		if (iol->iol_timeslot[type] < now) {
			/* Nothing to reimburse here. */
			continue;
		}

		iol->iol_timeslot[type] -= count / iol->iol_limits[type];
		reminder = count % iol->iol_limits[type];
		if (reminder > iol->iol_reminder[type]) {
			iol->iol_timeslot[type]--;
			iol->iol_reminder[type] += iol->iol_limits[type];
		}
		iol->iol_reminder[type] -= reminder;
	}

	mutex_exit(&iol->iol_lock);
}

static void
iolimit_reimburse_all(objset_t *os, const uint64_t *counts)
{
	dsl_dir_t *dd;
	hrtime_t now;
	int types;

	now = gettimeslot();

	ASSERT(RRM_READ_HELD(&os->os_spa->spa_iolimit_lock));

	types = iolimit_types(counts);

	for (dd = iolimit_first(os, types); dd != NULL;
	    dd = iolimit_parent(dd, types)) {
		iolimit_reimburse(dd->dd_iolimit, now, counts);
	}
}

static int
iolimit_sleep(hrtime_t timeslot)
{
	hrtime_t now;
	int error = 0;

	now = gettimeslot();

	if (timeslot > now) {
		/*
		 * Too much traffic, slow it down.
		 */
#ifdef _KERNEL
		if (delay_sig((hz / IOLIMIT_RESOLUTION) * (timeslot - now))) {
			error = SET_ERROR(EINTR);
		}
#else
		delay((hz / IOLIMIT_RESOLUTION) * (timeslot - now));
#endif
	}

	return (error);
}

static int
zfs_iolimit_sleep(objset_t *os, const uint64_t *counts)
{
	hrtime_t timeslot;
	int error;

	/*
	 * Prevents configuration changes when we have requests in-flight.
	 */
	rrm_enter_read(&os->os_spa->spa_iolimit_lock, FTAG);

	timeslot = iolimit_charge_all(os, counts);

	rrm_exit(&os->os_spa->spa_iolimit_lock, FTAG);

	error = iolimit_sleep(timeslot);

	if (error == EINTR) {
		/*
		 * Process was interrupted, so the request won't be executed.
		 * Reimburse the charge on all levels.
		 */
		rrm_enter_read(&os->os_spa->spa_iolimit_lock, FTAG);
		iolimit_reimburse_all(os, counts);
		rrm_exit(&os->os_spa->spa_iolimit_lock, FTAG);
	}

	return (error);
}

/*
 * For every data read we charge:
 * - bytes of read bandwidth
 * - bytes of total bandwidth
 * - (bytes + blocksize - 1) / blocksize of read operations
 * - (bytes + blocksize - 1) / blocksize of total operations
 */
int
zfs_iolimit_data_read(objset_t *os, size_t blocksize, size_t bytes)
{
	uint64_t counts[ZFS_IOLIMIT_NTYPES];
	size_t operations;

	if (bytes == 0) {
		return (0);
	}
	if (!iolimit_exists(os)) {
		return (0);
	}
	if (blocksize == 0) {
		blocksize = bytes;
	}
	operations = (bytes + blocksize - 1) / blocksize;

	memset(counts, 0, sizeof (counts));
	counts[ZFS_IOLIMIT_BW_READ] = bytes;
	counts[ZFS_IOLIMIT_BW_TOTAL] = bytes;
	counts[ZFS_IOLIMIT_OP_READ] = operations;
	counts[ZFS_IOLIMIT_OP_TOTAL] = operations;

	return (zfs_iolimit_sleep(os, counts));
}

/*
 * For every data write we charge:
 * - bytes of write bandwidth
 * - bytes of total bandwidth
 * - (bytes + blocksize - 1) / blocksize of read operations
 * - (bytes + blocksize - 1) / blocksize of total operations
 */
int
zfs_iolimit_data_write(objset_t *os, size_t blocksize, size_t bytes)
{
	uint64_t counts[ZFS_IOLIMIT_NTYPES];
	size_t operations;

	if (bytes == 0) {
		return (0);
	}
	if (!iolimit_exists(os)) {
		return (0);
	}
	if (blocksize == 0) {
		blocksize = bytes;
	}
	operations = (bytes + blocksize - 1) / blocksize;

	memset(counts, 0, sizeof (counts));
	counts[ZFS_IOLIMIT_BW_WRITE] = bytes;
	counts[ZFS_IOLIMIT_BW_TOTAL] = bytes;
	counts[ZFS_IOLIMIT_OP_WRITE] = operations;
	counts[ZFS_IOLIMIT_OP_TOTAL] = operations;

	return (zfs_iolimit_sleep(os, counts));
}

int
zfs_iolimit_data_copy(objset_t *srcos, objset_t *dstos, size_t blocksize,
    size_t bytes)
{
	uint64_t counts[ZFS_IOLIMIT_NTYPES];
	size_t operations;
	hrtime_t dstts, srcts;
	spa_t *spa = srcos->os_spa;

	if (bytes == 0) {
		return (0);
	}
	if (!iolimit_exists(srcos) && !iolimit_exists(dstos)) {
		return (0);
	}
	if (blocksize == 0) {
		blocksize = bytes;
	}
	operations = (bytes + blocksize - 1) / blocksize;

	/*
	 * Prevents configuration changes when we have requests in-flight.
	 */
	rrm_enter_read(&spa->spa_iolimit_lock, FTAG);

	memset(counts, 0, sizeof (counts));
	counts[ZFS_IOLIMIT_BW_READ] = bytes;
	counts[ZFS_IOLIMIT_BW_TOTAL] = bytes;
	counts[ZFS_IOLIMIT_OP_READ] = operations;
	counts[ZFS_IOLIMIT_OP_TOTAL] = operations;

	srcts = iolimit_charge_all(srcos, counts);

	memset(counts, 0, sizeof (counts));
	counts[ZFS_IOLIMIT_BW_WRITE] = bytes;
	counts[ZFS_IOLIMIT_BW_TOTAL] = bytes;
	counts[ZFS_IOLIMIT_OP_WRITE] = operations;
	counts[ZFS_IOLIMIT_OP_TOTAL] = operations;

	dstts = iolimit_charge_all(dstos, counts);

	rrm_exit(&spa->spa_iolimit_lock, FTAG);

	return (iolimit_sleep(dstts > srcts ? dstts : srcts));
}

/*
 * For every metadata read we charge:
 * - one read operation
 * - one total operation
 */
int
zfs_iolimit_metadata_read(objset_t *os)
{
	uint64_t counts[ZFS_IOLIMIT_NTYPES];

	if (!iolimit_exists(os)) {
		return (0);
	}

	memset(counts, 0, sizeof (counts));
	counts[ZFS_IOLIMIT_OP_READ] = 1;
	counts[ZFS_IOLIMIT_OP_TOTAL] = 1;

	return (zfs_iolimit_sleep(os, counts));
}

/*
 * For every metadata write we charge:
 * - one read operation
 * - one total operation
 */
int
zfs_iolimit_metadata_write(objset_t *os)
{
	uint64_t counts[ZFS_IOLIMIT_NTYPES];

	if (!iolimit_exists(os)) {
		return (0);
	}

	memset(counts, 0, sizeof (counts));
	counts[ZFS_IOLIMIT_OP_WRITE] = 1;
	counts[ZFS_IOLIMIT_OP_TOTAL] = 1;

	return (zfs_iolimit_sleep(os, counts));
}

/*
 * Function spins until timeout is reached or the process received a signal.
 * This function is different than iolimit_sleep(), because pause_sig()
 * might not be woken up by a signal if the process has multiple threads.
 * We use *_spin() functions for zfs send/recv where kernel starts additional
 * kernel threads and interrupting userland process with CTRL+C (SIGINT)
 * doesn't interrupt pause_sig() waiting in another kernel thread.
 */
static void
iolimit_spin(objset_t *os, const uint64_t *counts)
{
	hrtime_t timeslot;

	/*
	 * Prevents configuration changes when we have requests in-flight.
	 */
	rrm_enter_read(&os->os_spa->spa_iolimit_lock, FTAG);

	timeslot = iolimit_charge_all(os, counts);

	rrm_exit(&os->os_spa->spa_iolimit_lock, FTAG);

	while (timeslot > gettimeslot() && !issig()) {
		delay(hz / IOLIMIT_RESOLUTION);
	}
}

void
zfs_iolimit_data_read_spin(objset_t *os, size_t blocksize, size_t bytes)
{
	uint64_t counts[ZFS_IOLIMIT_NTYPES];
	size_t operations;

	if (bytes == 0) {
		return;
	}
	if (!iolimit_exists(os)) {
		return;
	}
	if (blocksize == 0) {
		blocksize = bytes;
	}
	operations = (bytes + blocksize - 1) / blocksize;

	memset(counts, 0, sizeof (counts));
	counts[ZFS_IOLIMIT_BW_READ] = bytes;
	counts[ZFS_IOLIMIT_BW_TOTAL] = bytes;
	counts[ZFS_IOLIMIT_OP_READ] = operations;
	counts[ZFS_IOLIMIT_OP_TOTAL] = operations;

	iolimit_spin(os, counts);
}

void
zfs_iolimit_data_write_spin(objset_t *os, size_t blocksize, size_t bytes)
{
	uint64_t counts[ZFS_IOLIMIT_NTYPES];
	size_t operations;

	if (bytes == 0) {
		return;
	}
	if (!iolimit_exists(os)) {
		return;
	}
	if (blocksize == 0) {
		blocksize = bytes;
	}
	operations = (bytes + blocksize - 1) / blocksize;

	memset(counts, 0, sizeof (counts));
	counts[ZFS_IOLIMIT_BW_WRITE] = bytes;
	counts[ZFS_IOLIMIT_BW_TOTAL] = bytes;
	counts[ZFS_IOLIMIT_OP_WRITE] = operations;
	counts[ZFS_IOLIMIT_OP_TOTAL] = operations;

	iolimit_spin(os, counts);
}
