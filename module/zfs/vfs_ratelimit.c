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
 *    NOP writes, I/O aggregation, metadata traffic, etc.).
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
 * During the accounting it allows us for quick access to the ratelimit
 * structure we need by just going to ds_dir->dd_ratelimit_root;
 * If ratelimits are not configured on this dataset or any of its parents,
 * the ds_dir->dd_ratelimit_root will be set to NULL, so we know we don't
 * have to do any accounting.
 *
 * The limits are configured per second, but we divde the second and the limits
 * into RATELIMIT_RESOLUTION slots (10 by default). This is to avoid a choking
 * effect, when process is doing progress in 1s steps. For example if we have
 * read bandwidth limits configured to 100MB/s and the process is trying to
 * read 130MB, it will take 1.3 seconds, not 2 seconds.
 * Not that very low limits may be rounded up - 7 ops/s limit will be rounded
 * up to 10 ops/s, so each slot is assigned 1 op/s limit. This rounding up
 * is done in the kernel and isn't shown in the properties value.
 *
 * How does the accounting work?
 *
 * When a request comes, we may need to consider multiple limits.
 * For example a data read request of eg. 192kB (with 128kB recordsize) is
 * accounted as 192kB bandwidth read, 192kB bandwidth total, two operations read
 * and two operations total. Not all of those limits have to be configured or
 * some might be configured on a dataset and others on a parent dataset(s).
 *
 * We remember those values in the rtslot structures at every level we have
 * limits configured on. The rtslot strucuture also remembers the time of
 * the request. For each ratelimit type (read bandwidth, total, operation read,
 * operation total) and for each dataset with the limits configured when we walk
 * the dataset tree up we find the point in time until which we have to wait to
 * satisfy configured limit. We select the furthest point in time and we do to
 * sleep. If the request doesn't exceed any limits, we just do the accounting
 * and allow for the request to be executed immediately.
 */

/*
 * Number of slots we divide one second into. More granularity is better for
 * interactivity, but it takes more memory and more calculations.
 */
#define	RATELIMIT_RESOLUTION	16

struct vfs_ratelimit {
	kmutex_t	rl_lock;
	uint64_t	rl_limits[ZFS_RATELIMIT_NTYPES];
	/* List of current waiters and past activity. */
	list_t		rl_list;
};

struct rtslot {
	list_node_t	rts_node;
	hrtime_t	rts_timeslot;
	int		rts_types;
	uint64_t	rts_counts[ZFS_RATELIMIT_NTYPES];
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

	for (int i = ZFS_RATELIMIT_FIRST; i < ZFS_RATELIMIT_NTYPES; i++) {
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
	list_create(&rl->rl_list, sizeof (struct rtslot),
	    offsetof(struct rtslot, rts_node));
	/* Create two slots for a good start. */
	for (i = 0; i < 2; i++) {
		list_insert_tail(&rl->rl_list,
		    kmem_zalloc(sizeof (struct rtslot), KM_SLEEP));
	}

	if (limits != NULL) {
		for (i = ZFS_RATELIMIT_FIRST; i < ZFS_RATELIMIT_NTYPES; i++) {
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
	struct rtslot *slot;

	if (rl == NULL) {
		return;
	}

	while ((slot = list_remove_head(&rl->rl_list)) != NULL) {
		kmem_free(slot, sizeof (*slot));
	}
	list_destroy(&rl->rl_list);

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
	hrtime_t nsec;

	gethrestime(&ts);
	nsec = ((hrtime_t)ts.tv_sec * NANOSEC) + ts.tv_nsec;
	return (nsec / (NANOSEC / RATELIMIT_RESOLUTION));
}

/*
 * Returns bit mask of the types configured for the given ratelimit structure.
 */
static int
ratelimit_types(const struct vfs_ratelimit *rl)
{
	int types, type;

	if (rl == NULL) {
		return (0);
	}

	types = 0;
	for (type = ZFS_RATELIMIT_FIRST; type <= ZFS_RATELIMIT_LAST; type++) {
		if (rl->rl_limits[type] > 0) {
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
	int mytypes;

	ASSERT(RRM_READ_HELD(&os->os_spa->spa_ratelimit_lock));

	dd = os->os_dsl_dataset->ds_dir->dd_ratelimit_root;
	for (;;) {
		if (dd == NULL) {
			return (NULL);
		}
		mytypes = ratelimit_types(dd->dd_ratelimit);
		if ((mytypes & types) != 0) {
			/*
			 * This dataset has at last one limit we are
			 * interested in.
			 */
			return (dd);
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
	int mytypes;

	ASSERT(RRM_READ_HELD(&dd->dd_pool->dp_spa->spa_ratelimit_lock));

	for (;;) {
		if (dd->dd_parent == NULL) {
			return (NULL);
		}
		dd = dd->dd_parent->dd_ratelimit_root;
		if (dd == NULL) {
			return (NULL);
		}
		mytypes = ratelimit_types(dd->dd_ratelimit);
		if ((mytypes & types) != 0) {
			/*
			 * This dataset has at last one limit we are
			 * interested in.
			 */
			return (dd);
		}
	}
}

/*
 * If we have any entries with 'timeslot > now' we also must have an entry with
 * 'timeslot == now'. In other words if there is no entry with
 * 'timeslot == now', it means that all the entires expired.
 *
 * We return either the most recent entry related to the given type or we return
 * 'timeslot == now' entry not related to the given type and we will use it to
 * store accouting information about this type as well.
 */
static struct rtslot *
ratelimit_find(struct vfs_ratelimit *rl, int typebit, hrtime_t now)
{
	struct rtslot *slot;

	ASSERT(MUTEX_HELD(&rl->rl_lock));

	for (slot = list_head(&rl->rl_list); slot != NULL;
	    slot = list_next(&rl->rl_list, slot)) {
		if (slot->rts_timeslot < now) {
			break;
		}
		if ((slot->rts_types & typebit) != 0 ||
		    slot->rts_timeslot == now) {
			return (slot);
		}
	}
	/* All the entries expired. */
#ifndef NDEBUG
	for (slot = list_head(&rl->rl_list); slot != NULL;
	    slot = list_next(&rl->rl_list, slot)) {
		ASSERT(slot->rts_timeslot < now);
	}
#endif

	return (NULL);
}

/*
 * Account for our request across all the types configured in this ratelimit
 * structure.
 * Return a timeslot we should wait for or now if we can execute the request
 * without waiting (we are within limits).
 */
static uint64_t
ratelimit_account(struct vfs_ratelimit *rl, int types, hrtime_t now,
    const uint64_t *counts)
{
	uint64_t timeslot;
	int type, typebit;

	timeslot = 0;

	mutex_enter(&rl->rl_lock);

	for (type = ZFS_RATELIMIT_FIRST; type <= ZFS_RATELIMIT_LAST; type++) {
		struct rtslot *slot;
		uint64_t count, nexttimeslot;

		typebit = (1 << type);

		if ((types & typebit) == 0) {
			/* Not interested in this type. */
			continue;
		}
		if (rl->rl_limits[type] == 0) {
			/* This type has no limit configured on this dataset. */
			continue;
		}
		count = counts[type];
		ASSERT(count > 0);

		slot = ratelimit_find(rl, typebit, now);
		if (slot == NULL) {
			slot = list_remove_tail(&rl->rl_list);
			ASSERT(slot->rts_timeslot < now);
			slot->rts_types = typebit;
			slot->rts_timeslot = now;
			memset(slot->rts_counts, 0, sizeof (slot->rts_counts));
			list_insert_head(&rl->rl_list, slot);
		} else if (slot->rts_timeslot == now) {
			/* The 'now' slot may not have our type yet. */
			slot->rts_types |= typebit;
		}
		ASSERT((slot->rts_types & typebit) != 0);
		nexttimeslot = slot->rts_timeslot + 1;

		for (;;) {
			if (slot->rts_counts[type] + count <=
			    rl->rl_limits[type]) {
				slot->rts_counts[type] += count;
				break;
			}

			/*
			 * This request is too big to fit into a single slot,
			 * ie. a single request exceeds the limit or this and
			 * the previous requests exceed the limit.
			 */

			/*
			 * Fit as much as we can into the current slot.
			 */
			count -= rl->rl_limits[type] - slot->rts_counts[type];
			slot->rts_counts[type] = rl->rl_limits[type];

			/*
			 * Take the next slot (if already exists isn't aware of
			 * our type yet), take an expired slot from the tail of
			 * the list or allocate a new slot.
			 */
			slot = list_prev(&rl->rl_list, slot);
			if (slot != NULL) {
				ASSERT((slot->rts_types & typebit) == 0);
				ASSERT(slot->rts_timeslot == nexttimeslot);
				ASSERT0(slot->rts_counts[type]);

				slot->rts_types |= typebit;
			} else {
				slot = list_tail(&rl->rl_list);
				if (slot->rts_timeslot < now) {
					list_remove(&rl->rl_list, slot);
				} else {
					slot = kmem_alloc(sizeof (*slot),
					    KM_SLEEP);
				}
				slot->rts_types = typebit;
				slot->rts_timeslot = nexttimeslot;
				memset(slot->rts_counts, 0,
				    sizeof (slot->rts_counts));
				list_insert_head(&rl->rl_list, slot);
			}

			nexttimeslot++;
		}

		if (timeslot < slot->rts_timeslot) {
			timeslot = slot->rts_timeslot;
		}
	}

	mutex_exit(&rl->rl_lock);

	return (timeslot);
}

static void
vfs_ratelimit(objset_t *os, int types, const uint64_t *counts)
{
	dsl_dir_t *dd;
	hrtime_t now, timeslot;

	now = gettimeslot();
	timeslot = 0;

	/*
	 * Prevents configuration changes when we have requests in-flight.
	 */
	rrm_enter_read(&os->os_spa->spa_ratelimit_lock, FTAG);

	for (dd = ratelimit_first(os, types); dd != NULL;
	    dd = ratelimit_parent(dd, types)) {
		hrtime_t ts;

		ts = ratelimit_account(dd->dd_ratelimit, types, now, counts);
		if (ts > timeslot) {
			timeslot = ts;
		}
	}

	rrm_exit(&os->os_spa->spa_ratelimit_lock, FTAG);

	if (timeslot > now) {
		/*
		 * Too much traffic, slow it down.
		 */
		delay((hz / RATELIMIT_RESOLUTION) * (timeslot - now));
	}
}

/*
 * For every data read we charge:
 * - bytes of read bandwidth
 * - bytes of total bandwidth
 * - (bytes - 1) / blocksize + 1 of read operations
 * - (bytes - 1) / blocksize + 1 of total operations
 */
void
vfs_ratelimit_data_read(objset_t *os, size_t blocksize, size_t bytes)
{
	uint64_t counts[ZFS_RATELIMIT_NTYPES];
	unsigned int types;

	if (bytes == 0) {
		return;
	}
	if (blocksize == 0) {
		blocksize = bytes;
	}

	types =  (1 << ZFS_RATELIMIT_BW_READ);
	types |= (1 << ZFS_RATELIMIT_BW_TOTAL);
	types |= (1 << ZFS_RATELIMIT_OP_READ);
	types |= (1 << ZFS_RATELIMIT_OP_TOTAL);

	memset(counts, 0, sizeof (counts));
	counts[ZFS_RATELIMIT_BW_READ] = bytes;
	counts[ZFS_RATELIMIT_BW_TOTAL] = bytes;
	counts[ZFS_RATELIMIT_OP_READ] = (bytes - 1) / blocksize + 1;
	counts[ZFS_RATELIMIT_OP_TOTAL] = (bytes - 1) / blocksize + 1;

	vfs_ratelimit(os, types, counts);
}

/*
 * For every data write we charge:
 * - bytes of write bandwidth
 * - bytes of total bandwidth
 * - (bytes - 1) / blocksize + 1 of write operations
 * - (bytes - 1) / blocksize + 1 of total operations
 */
void
vfs_ratelimit_data_write(objset_t *os, size_t blocksize, size_t bytes)
{
	uint64_t counts[ZFS_RATELIMIT_NTYPES];
	unsigned int types;

	if (bytes == 0) {
		return;
	}
	if (blocksize == 0) {
		blocksize = bytes;
	}

	types =  (1 << ZFS_RATELIMIT_BW_WRITE);
	types |= (1 << ZFS_RATELIMIT_BW_TOTAL);
	types |= (1 << ZFS_RATELIMIT_OP_WRITE);
	types |= (1 << ZFS_RATELIMIT_OP_TOTAL);

	memset(counts, 0, sizeof (counts));
	counts[ZFS_RATELIMIT_BW_WRITE] = bytes;
	counts[ZFS_RATELIMIT_BW_TOTAL] = bytes;
	counts[ZFS_RATELIMIT_OP_WRITE] = (bytes - 1) / blocksize + 1;
	counts[ZFS_RATELIMIT_OP_TOTAL] = (bytes - 1) / blocksize + 1;

	vfs_ratelimit(os, types, counts);
}

/*
 * For every metadata read we charge:
 * - one read operation
 * - one total operation
 */
void
vfs_ratelimit_metadata_read(objset_t *os)
{
	uint64_t counts[ZFS_RATELIMIT_NTYPES];
	unsigned int types;

	types =  (1 << ZFS_RATELIMIT_OP_READ);
	types |= (1 << ZFS_RATELIMIT_OP_TOTAL);

	memset(counts, 0, sizeof (counts));
	counts[ZFS_RATELIMIT_OP_READ] = 1;
	counts[ZFS_RATELIMIT_OP_TOTAL] = 1;

	vfs_ratelimit(os, types, counts);
}

/*
 * For every metadata write we charge:
 * - one read operation
 * - one total operation
 */
void
vfs_ratelimit_metadata_write(objset_t *os)
{
	uint64_t counts[ZFS_RATELIMIT_NTYPES];
	unsigned int types;

	types =  (1 << ZFS_RATELIMIT_OP_WRITE);
	types |= (1 << ZFS_RATELIMIT_OP_TOTAL);

	memset(counts, 0, sizeof (counts));
	counts[ZFS_RATELIMIT_OP_WRITE] = 1;
	counts[ZFS_RATELIMIT_OP_TOTAL] = 1;

	vfs_ratelimit(os, types, counts);
}
