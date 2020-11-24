/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or https://opensource.org/licenses/CDDL-1.0.
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
 * Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

/*
 * Copyright (c) 2012,2021 by Delphix. All rights reserved.
 */

#include <sys/spa.h>
#include <sys/spa_impl.h>
#include <sys/vdev.h>
#include <sys/vdev_impl.h>
#include <sys/zio.h>
#include <sys/zio_checksum.h>

#include <sys/fm/fs/zfs.h>
#include <sys/fm/protocol.h>
#include <sys/fm/util.h>
#include <sys/sysevent.h>

/*
 * This general routine is responsible for generating all the different ZFS
 * ereports.  The payload is dependent on the class, and which arguments are
 * supplied to the function:
 *
 * 	EREPORT			POOL	VDEV	IO
 * 	block			X	X	X
 * 	data			X		X
 * 	device			X	X
 * 	pool			X
 *
 * If we are in a loading state, all errors are chained together by the same
 * SPA-wide ENA (Error Numeric Association).
 *
 * For isolated I/O requests, we get the ENA from the zio_t. The propagation
 * gets very complicated due to RAID-Z, gang blocks, and vdev caching.  We want
 * to chain together all ereports associated with a logical piece of data.  For
 * read I/Os, there  are basically three 'types' of I/O, which form a roughly
 * layered diagram:
 *
 * 	+---------------+
 * 	| Aggregate I/O |	No associated logical data or device
 * 	+---------------+
 *              |
 *              V
 * 	+---------------+	Reads associated with a piece of logical data.
 * 	|   Read I/O    |	This includes reads on behalf of RAID-Z,
 * 	+---------------+       mirrors, gang blocks, retries, etc.
 *              |
 *              V
 * 	+---------------+	Reads associated with a particular device, but
 * 	| Physical I/O  |	no logical data.  Issued as part of vdev caching
 * 	+---------------+	and I/O aggregation.
 *
 * Note that 'physical I/O' here is not the same terminology as used in the rest
 * of ZIO.  Typically, 'physical I/O' simply means that there is no attached
 * blockpointer.  But I/O with no associated block pointer can still be related
 * to a logical piece of data (i.e. RAID-Z requests).
 *
 * Purely physical I/O always have unique ENAs.  They are not related to a
 * particular piece of logical data, and therefore cannot be chained together.
 * We still generate an ereport, but the DE doesn't correlate it with any
 * logical piece of data.  When such an I/O fails, the delegated I/O requests
 * will issue a retry, which will trigger the 'real' ereport with the correct
 * ENA.
 *
 * We keep track of the ENA for a ZIO chain through the 'io_logical' member.
 * When a new logical I/O is issued, we set this to point to itself.  Child I/Os
 * then inherit this pointer, so that when it is first set subsequent failures
 * will use the same ENA.  For vdev cache fill and queue aggregation I/O,
 * this pointer is set to NULL, and no ereport will be generated (since it
 * doesn't actually correspond to any particular device or piece of data,
 * and the caller will always retry without caching or queueing anyway).
 *
 * For checksum errors, we want to include more information about the actual
 * error which occurs.  Accordingly, we build an ereport when the error is
 * noticed, but instead of sending it in immediately, we hang it off of the
 * io_cksum_report field of the logical IO.  When the logical IO completes
 * (successfully or not), zfs_ereport_finish_checksum() is called with the
 * good and bad versions of the buffer (if available), and we annotate the
 * ereport with information about the differences.
 */

#ifdef _KERNEL
/*
 * Duplicate ereport Detection
 *
 * Some ereports are retained momentarily for detecting duplicates.  These
 * are kept in a recent_events_node_t in both a time-ordered list and an AVL
 * tree of recent unique ereports.
 *
 * The lifespan of these recent ereports is bounded (15 mins) and a cleaner
 * task is used to purge stale entries.
 */
static list_t recent_events_list;
static avl_tree_t recent_events_tree;
static kmutex_t recent_events_lock;
static taskqid_t recent_events_cleaner_tqid;

/*
 * Each node is about 128 bytes so 2,000 would consume 1/4 MiB.
 *
 * This setting can be changed dynamically and setting it to zero
 * disables duplicate detection.
 */
static unsigned int zfs_zevent_retain_max = 2000;

/*
 * The lifespan for a recent ereport entry. The default of 15 minutes is
 * intended to outlive the zfs diagnosis engine's threshold of 10 errors
 * over a period of 10 minutes.
 */
static unsigned int zfs_zevent_retain_expire_secs = 900;

typedef enum zfs_subclass {
	ZSC_IO,
	ZSC_DATA,
	ZSC_CHECKSUM
} zfs_subclass_t;

typedef struct {
	/* common criteria */
	uint64_t	re_pool_guid;
	uint64_t	re_vdev_guid;
	int		re_io_error;
	uint64_t	re_io_size;
	uint64_t	re_io_offset;
	zfs_subclass_t	re_subclass;
	zio_priority_t	re_io_priority;

	/* logical zio criteria (optional) */
	zbookmark_phys_t re_io_bookmark;

	/* internal state */
	avl_node_t	re_tree_link;
	list_node_t	re_list_link;
	uint64_t	re_timestamp;
} recent_events_node_t;

static int
recent_events_compare(const void *a, const void *b)
{
	const recent_events_node_t *node1 = a;
	const recent_events_node_t *node2 = b;
	int cmp;

	/*
	 * The comparison order here is somewhat arbitrary.
	 * What's important is that if every criteria matches, then it
	 * is a duplicate (i.e. compare returns 0)
	 */
	if ((cmp = TREE_CMP(node1->re_subclass, node2->re_subclass)) != 0)
		return (cmp);
	if ((cmp = TREE_CMP(node1->re_pool_guid, node2->re_pool_guid)) != 0)
		return (cmp);
	if ((cmp = TREE_CMP(node1->re_vdev_guid, node2->re_vdev_guid)) != 0)
		return (cmp);
	if ((cmp = TREE_CMP(node1->re_io_error, node2->re_io_error)) != 0)
		return (cmp);
	if ((cmp = TREE_CMP(node1->re_io_priority, node2->re_io_priority)) != 0)
		return (cmp);
	if ((cmp = TREE_CMP(node1->re_io_size, node2->re_io_size)) != 0)
		return (cmp);
	if ((cmp = TREE_CMP(node1->re_io_offset, node2->re_io_offset)) != 0)
		return (cmp);

	const zbookmark_phys_t *zb1 = &node1->re_io_bookmark;
	const zbookmark_phys_t *zb2 = &node2->re_io_bookmark;

	if ((cmp = TREE_CMP(zb1->zb_objset, zb2->zb_objset)) != 0)
		return (cmp);
	if ((cmp = TREE_CMP(zb1->zb_object, zb2->zb_object)) != 0)
		return (cmp);
	if ((cmp = TREE_CMP(zb1->zb_level, zb2->zb_level)) != 0)
		return (cmp);
	if ((cmp = TREE_CMP(zb1->zb_blkid, zb2->zb_blkid)) != 0)
		return (cmp);

	return (0);
}

/*
 * workaround: vdev properties don't have inheritance
 */
static uint64_t
vdev_prop_get_inherited(vdev_t *vd, vdev_prop_t prop)
{
	uint64_t propdef, propval;

	propdef = vdev_prop_default_numeric(prop);
	switch (prop) {
		case VDEV_PROP_CHECKSUM_N:
			propval = vd->vdev_checksum_n;
			break;
		case VDEV_PROP_CHECKSUM_T:
			propval = vd->vdev_checksum_t;
			break;
		case VDEV_PROP_IO_N:
			propval = vd->vdev_io_n;
			break;
		case VDEV_PROP_IO_T:
			propval = vd->vdev_io_t;
			break;
		case VDEV_PROP_SLOW_IO_N:
			propval = vd->vdev_slow_io_n;
			break;
		case VDEV_PROP_SLOW_IO_T:
			propval = vd->vdev_slow_io_t;
			break;
		default:
			propval = propdef;
			break;
	}

	if (propval != propdef)
		return (propval);

	if (vd->vdev_parent == NULL)
		return (propdef);

	return (vdev_prop_get_inherited(vd->vdev_parent, prop));
}

static void zfs_ereport_schedule_cleaner(void);

/*
 * background task to clean stale recent event nodes.
 */
static void
zfs_ereport_cleaner(void *arg)
{
	recent_events_node_t *entry;
	uint64_t now = gethrtime();

	/*
	 * purge expired entries
	 */
	mutex_enter(&recent_events_lock);
	while ((entry = list_tail(&recent_events_list)) != NULL) {
		uint64_t age = NSEC2SEC(now - entry->re_timestamp);
		if (age <= zfs_zevent_retain_expire_secs)
			break;

		/* remove expired node */
		avl_remove(&recent_events_tree, entry);
		list_remove(&recent_events_list, entry);
		kmem_free(entry, sizeof (*entry));
	}

	/* Restart the cleaner if more entries remain */
	recent_events_cleaner_tqid = 0;
	if (!list_is_empty(&recent_events_list))
		zfs_ereport_schedule_cleaner();

	mutex_exit(&recent_events_lock);
}

static void
zfs_ereport_schedule_cleaner(void)
{
	ASSERT(MUTEX_HELD(&recent_events_lock));

	uint64_t timeout = SEC2NSEC(zfs_zevent_retain_expire_secs + 1);

	recent_events_cleaner_tqid = taskq_dispatch_delay(
	    system_delay_taskq, zfs_ereport_cleaner, NULL, TQ_SLEEP,
	    ddi_get_lbolt() + NSEC_TO_TICK(timeout));
}

/*
 * Clear entries for a given vdev or all vdevs in a pool when vdev == NULL
 */
void
zfs_ereport_clear(spa_t *spa, vdev_t *vd)
{
	uint64_t vdev_guid, pool_guid;

	ASSERT(vd != NULL || spa != NULL);
	if (vd == NULL) {
		vdev_guid = 0;
		pool_guid = spa_guid(spa);
	} else {
		vdev_guid = vd->vdev_guid;
		pool_guid = 0;
	}

	mutex_enter(&recent_events_lock);

	recent_events_node_t *next = list_head(&recent_events_list);
	while (next != NULL) {
		recent_events_node_t *entry = next;

		next = list_next(&recent_events_list, next);

		if (entry->re_vdev_guid == vdev_guid ||
		    entry->re_pool_guid == pool_guid) {
			avl_remove(&recent_events_tree, entry);
			list_remove(&recent_events_list, entry);
			kmem_free(entry, sizeof (*entry));
		}
	}

	mutex_exit(&recent_events_lock);
}

/*
 * Check if an ereport would be a duplicate of one recently posted.
 *
 * An ereport is considered a duplicate if the set of criteria in
 * recent_events_node_t all match.
 *
 * Only FM_EREPORT_ZFS_IO, FM_EREPORT_ZFS_DATA, and FM_EREPORT_ZFS_CHECKSUM
 * are candidates for duplicate checking.
 */
static boolean_t
zfs_ereport_is_duplicate(const char *subclass, spa_t *spa, vdev_t *vd,
    const zbookmark_phys_t *zb, zio_t *zio, uint64_t offset, uint64_t size)
{
	recent_events_node_t search = {0}, *entry;

	if (vd == NULL || zio == NULL)
		return (B_FALSE);

	if (zfs_zevent_retain_max == 0)
		return (B_FALSE);

	if (strcmp(subclass, FM_EREPORT_ZFS_IO) == 0)
		search.re_subclass = ZSC_IO;
	else if (strcmp(subclass, FM_EREPORT_ZFS_DATA) == 0)
		search.re_subclass = ZSC_DATA;
	else if (strcmp(subclass, FM_EREPORT_ZFS_CHECKSUM) == 0)
		search.re_subclass = ZSC_CHECKSUM;
	else
		return (B_FALSE);

	search.re_pool_guid = spa_guid(spa);
	search.re_vdev_guid = vd->vdev_guid;
	search.re_io_error = zio->io_error;
	search.re_io_priority = zio->io_priority;
	/* if size is supplied use it over what's in zio */
	if (size) {
		search.re_io_size = size;
		search.re_io_offset = offset;
	} else {
		search.re_io_size = zio->io_size;
		search.re_io_offset = zio->io_offset;
	}

	/* grab optional logical zio criteria */
	if (zb != NULL) {
		search.re_io_bookmark.zb_objset = zb->zb_objset;
		search.re_io_bookmark.zb_object = zb->zb_object;
		search.re_io_bookmark.zb_level = zb->zb_level;
		search.re_io_bookmark.zb_blkid = zb->zb_blkid;
	}

	uint64_t now = gethrtime();

	mutex_enter(&recent_events_lock);

	/* check if we have seen this one recently */
	entry = avl_find(&recent_events_tree, &search, NULL);
	if (entry != NULL) {
		uint64_t age = NSEC2SEC(now - entry->re_timestamp);

		/*
		 * There is still an active cleaner (since we're here).
		 * Reset the last seen time for this duplicate entry
		 * so that its lifespand gets extended.
		 */
		list_remove(&recent_events_list, entry);
		list_insert_head(&recent_events_list, entry);
		entry->re_timestamp = now;

		zfs_zevent_track_duplicate();
		mutex_exit(&recent_events_lock);

		return (age <= zfs_zevent_retain_expire_secs);
	}

	if (avl_numnodes(&recent_events_tree) >= zfs_zevent_retain_max) {
		/* recycle oldest node */
		entry = list_tail(&recent_events_list);
		ASSERT(entry != NULL);
		list_remove(&recent_events_list, entry);
		avl_remove(&recent_events_tree, entry);
	} else {
		entry = kmem_alloc(sizeof (recent_events_node_t), KM_SLEEP);
	}

	/* record this as a recent ereport */
	*entry = search;
	avl_add(&recent_events_tree, entry);
	list_insert_head(&recent_events_list, entry);
	entry->re_timestamp = now;

	/* Start a cleaner if not already scheduled */
	if (recent_events_cleaner_tqid == 0)
		zfs_ereport_schedule_cleaner();

	mutex_exit(&recent_events_lock);
	return (B_FALSE);
}

void
zfs_zevent_post_cb(nvlist_t *nvl, nvlist_t *detector)
{
	if (nvl)
		fm_nvlist_destroy(nvl, FM_NVA_FREE);

	if (detector)
		fm_nvlist_destroy(detector, FM_NVA_FREE);
}

/*
 * We want to rate limit ZIO delay, deadman, and checksum events so as to not
 * flood zevent consumers when a disk is acting up.
 *
 * Returns 1 if we're ratelimiting, 0 if not.
 */
static int
zfs_is_ratelimiting_event(const char *subclass, vdev_t *vd)
{
	int rc = 0;
	/*
	 * zfs_ratelimit() returns 1 if we're *not* ratelimiting and 0 if we
	 * are.  Invert it to get our return value.
	 */
	if (strcmp(subclass, FM_EREPORT_ZFS_DELAY) == 0) {
		rc = !zfs_ratelimit(&vd->vdev_delay_rl);
	} else if (strcmp(subclass, FM_EREPORT_ZFS_DEADMAN) == 0) {
		rc = !zfs_ratelimit(&vd->vdev_deadman_rl);
	} else if (strcmp(subclass, FM_EREPORT_ZFS_CHECKSUM) == 0) {
		rc = !zfs_ratelimit(&vd->vdev_checksum_rl);
	}

	if (rc)	{
		/* We're rate limiting */
		fm_erpt_dropped_increment();
	}

	return (rc);
}

/*
 * Return B_TRUE if the event actually posted, B_FALSE if not.
 */
static boolean_t
zfs_ereport_start(nvlist_t **ereport_out, nvlist_t **detector_out,
    const char *subclass, spa_t *spa, vdev_t *vd, const zbookmark_phys_t *zb,
    zio_t *zio, uint64_t stateoroffset, uint64_t size)
{
	nvlist_t *ereport, *detector;

	uint64_t ena;
	char class[64];

	if ((ereport = fm_nvlist_create(NULL)) == NULL)
		return (B_FALSE);

	if ((detector = fm_nvlist_create(NULL)) == NULL) {
		fm_nvlist_destroy(ereport, FM_NVA_FREE);
		return (B_FALSE);
	}

	/*
	 * Serialize ereport generation
	 */
	mutex_enter(&spa->spa_errlist_lock);

	/*
	 * Determine the ENA to use for this event.  If we are in a loading
	 * state, use a SPA-wide ENA.  Otherwise, if we are in an I/O state, use
	 * a root zio-wide ENA.  Otherwise, simply use a unique ENA.
	 */
	if (spa_load_state(spa) != SPA_LOAD_NONE) {
		if (spa->spa_ena == 0)
			spa->spa_ena = fm_ena_generate(0, FM_ENA_FMT1);
		ena = spa->spa_ena;
	} else if (zio != NULL && zio->io_logical != NULL) {
		if (zio->io_logical->io_ena == 0)
			zio->io_logical->io_ena =
			    fm_ena_generate(0, FM_ENA_FMT1);
		ena = zio->io_logical->io_ena;
	} else {
		ena = fm_ena_generate(0, FM_ENA_FMT1);
	}

	/*
	 * Construct the full class, detector, and other standard FMA fields.
	 */
	(void) snprintf(class, sizeof (class), "%s.%s",
	    ZFS_ERROR_CLASS, subclass);

	fm_fmri_zfs_set(detector, FM_ZFS_SCHEME_VERSION, spa_guid(spa),
	    vd != NULL ? vd->vdev_guid : 0);

	fm_ereport_set(ereport, FM_EREPORT_VERSION, class, ena, detector, NULL);

	/*
	 * Construct the per-ereport payload, depending on which parameters are
	 * passed in.
	 */

	/*
	 * Generic payload members common to all ereports.
	 */
	fm_payload_set(ereport,
	    FM_EREPORT_PAYLOAD_ZFS_POOL, DATA_TYPE_STRING, spa_name(spa),
	    FM_EREPORT_PAYLOAD_ZFS_POOL_GUID, DATA_TYPE_UINT64, spa_guid(spa),
	    FM_EREPORT_PAYLOAD_ZFS_POOL_STATE, DATA_TYPE_UINT64,
	    (uint64_t)spa_state(spa),
	    FM_EREPORT_PAYLOAD_ZFS_POOL_CONTEXT, DATA_TYPE_INT32,
	    (int32_t)spa_load_state(spa), NULL);

	fm_payload_set(ereport, FM_EREPORT_PAYLOAD_ZFS_POOL_FAILMODE,
	    DATA_TYPE_STRING,
	    spa_get_failmode(spa) == ZIO_FAILURE_MODE_WAIT ?
	    FM_EREPORT_FAILMODE_WAIT :
	    spa_get_failmode(spa) == ZIO_FAILURE_MODE_CONTINUE ?
	    FM_EREPORT_FAILMODE_CONTINUE : FM_EREPORT_FAILMODE_PANIC,
	    NULL);

	if (vd != NULL) {
		vdev_t *pvd = vd->vdev_parent;
		vdev_queue_t *vq = &vd->vdev_queue;
		vdev_stat_t *vs = &vd->vdev_stat;
		vdev_t *spare_vd;
		uint64_t *spare_guids;
		char **spare_paths;
		int i, spare_count;

		fm_payload_set(ereport, FM_EREPORT_PAYLOAD_ZFS_VDEV_GUID,
		    DATA_TYPE_UINT64, vd->vdev_guid,
		    FM_EREPORT_PAYLOAD_ZFS_VDEV_TYPE,
		    DATA_TYPE_STRING, vd->vdev_ops->vdev_op_type, NULL);
		if (vd->vdev_path != NULL)
			fm_payload_set(ereport,
			    FM_EREPORT_PAYLOAD_ZFS_VDEV_PATH,
			    DATA_TYPE_STRING, vd->vdev_path, NULL);
		if (vd->vdev_devid != NULL)
			fm_payload_set(ereport,
			    FM_EREPORT_PAYLOAD_ZFS_VDEV_DEVID,
			    DATA_TYPE_STRING, vd->vdev_devid, NULL);
		if (vd->vdev_fru != NULL)
			fm_payload_set(ereport,
			    FM_EREPORT_PAYLOAD_ZFS_VDEV_FRU,
			    DATA_TYPE_STRING, vd->vdev_fru, NULL);
		if (vd->vdev_enc_sysfs_path != NULL)
			fm_payload_set(ereport,
			    FM_EREPORT_PAYLOAD_ZFS_VDEV_ENC_SYSFS_PATH,
			    DATA_TYPE_STRING, vd->vdev_enc_sysfs_path, NULL);
		if (vd->vdev_ashift)
			fm_payload_set(ereport,
			    FM_EREPORT_PAYLOAD_ZFS_VDEV_ASHIFT,
			    DATA_TYPE_UINT64, vd->vdev_ashift, NULL);

		if (vq != NULL) {
			fm_payload_set(ereport,
			    FM_EREPORT_PAYLOAD_ZFS_VDEV_COMP_TS,
			    DATA_TYPE_UINT64, vq->vq_io_complete_ts, NULL);
			fm_payload_set(ereport,
			    FM_EREPORT_PAYLOAD_ZFS_VDEV_DELTA_TS,
			    DATA_TYPE_UINT64, vq->vq_io_delta_ts, NULL);
		}

		if (vs != NULL) {
			fm_payload_set(ereport,
			    FM_EREPORT_PAYLOAD_ZFS_VDEV_READ_ERRORS,
			    DATA_TYPE_UINT64, vs->vs_read_errors,
			    FM_EREPORT_PAYLOAD_ZFS_VDEV_WRITE_ERRORS,
			    DATA_TYPE_UINT64, vs->vs_write_errors,
			    FM_EREPORT_PAYLOAD_ZFS_VDEV_CKSUM_ERRORS,
			    DATA_TYPE_UINT64, vs->vs_checksum_errors,
			    FM_EREPORT_PAYLOAD_ZFS_VDEV_DELAYS,
			    DATA_TYPE_UINT64, vs->vs_slow_ios,
			    FM_EREPORT_PAYLOAD_ZFS_VDEV_DIO_VERIFY_ERRORS,
			    DATA_TYPE_UINT64, vs->vs_dio_verify_errors,
			    NULL);
		}

		if (pvd != NULL) {
			fm_payload_set(ereport,
			    FM_EREPORT_PAYLOAD_ZFS_PARENT_GUID,
			    DATA_TYPE_UINT64, pvd->vdev_guid,
			    FM_EREPORT_PAYLOAD_ZFS_PARENT_TYPE,
			    DATA_TYPE_STRING, pvd->vdev_ops->vdev_op_type,
			    NULL);
			if (pvd->vdev_path)
				fm_payload_set(ereport,
				    FM_EREPORT_PAYLOAD_ZFS_PARENT_PATH,
				    DATA_TYPE_STRING, pvd->vdev_path, NULL);
			if (pvd->vdev_devid)
				fm_payload_set(ereport,
				    FM_EREPORT_PAYLOAD_ZFS_PARENT_DEVID,
				    DATA_TYPE_STRING, pvd->vdev_devid, NULL);
		}

		spare_count = spa->spa_spares.sav_count;
		spare_paths = kmem_zalloc(sizeof (char *) * spare_count,
		    KM_SLEEP);
		spare_guids = kmem_zalloc(sizeof (uint64_t) * spare_count,
		    KM_SLEEP);

		for (i = 0; i < spare_count; i++) {
			spare_vd = spa->spa_spares.sav_vdevs[i];
			if (spare_vd) {
				spare_paths[i] = spare_vd->vdev_path;
				spare_guids[i] = spare_vd->vdev_guid;
			}
		}

		fm_payload_set(ereport, FM_EREPORT_PAYLOAD_ZFS_VDEV_SPARE_PATHS,
		    DATA_TYPE_STRING_ARRAY, spare_count, spare_paths,
		    FM_EREPORT_PAYLOAD_ZFS_VDEV_SPARE_GUIDS,
		    DATA_TYPE_UINT64_ARRAY, spare_count, spare_guids, NULL);

		kmem_free(spare_guids, sizeof (uint64_t) * spare_count);
		kmem_free(spare_paths, sizeof (char *) * spare_count);
	}

	if (zio != NULL) {
		/*
		 * Payload common to all I/Os.
		 */
		fm_payload_set(ereport, FM_EREPORT_PAYLOAD_ZFS_ZIO_ERR,
		    DATA_TYPE_INT32, zio->io_error, NULL);
		fm_payload_set(ereport, FM_EREPORT_PAYLOAD_ZFS_ZIO_FLAGS,
		    DATA_TYPE_UINT64, zio->io_flags, NULL);
		fm_payload_set(ereport, FM_EREPORT_PAYLOAD_ZFS_ZIO_STAGE,
		    DATA_TYPE_UINT32, zio->io_stage, NULL);
		fm_payload_set(ereport, FM_EREPORT_PAYLOAD_ZFS_ZIO_PIPELINE,
		    DATA_TYPE_UINT32, zio->io_pipeline, NULL);
		fm_payload_set(ereport, FM_EREPORT_PAYLOAD_ZFS_ZIO_DELAY,
		    DATA_TYPE_UINT64, zio->io_delay, NULL);
		fm_payload_set(ereport, FM_EREPORT_PAYLOAD_ZFS_ZIO_TIMESTAMP,
		    DATA_TYPE_UINT64, zio->io_timestamp, NULL);
		fm_payload_set(ereport, FM_EREPORT_PAYLOAD_ZFS_ZIO_DELTA,
		    DATA_TYPE_UINT64, zio->io_delta, NULL);
		fm_payload_set(ereport, FM_EREPORT_PAYLOAD_ZFS_ZIO_PRIORITY,
		    DATA_TYPE_UINT32, zio->io_priority, NULL);

		/*
		 * If the 'size' parameter is non-zero, it indicates this is a
		 * RAID-Z or other I/O where the physical offset and length are
		 * provided for us, instead of within the zio_t.
		 */
		if (vd != NULL) {
			if (size)
				fm_payload_set(ereport,
				    FM_EREPORT_PAYLOAD_ZFS_ZIO_OFFSET,
				    DATA_TYPE_UINT64, stateoroffset,
				    FM_EREPORT_PAYLOAD_ZFS_ZIO_SIZE,
				    DATA_TYPE_UINT64, size, NULL);
			else
				fm_payload_set(ereport,
				    FM_EREPORT_PAYLOAD_ZFS_ZIO_OFFSET,
				    DATA_TYPE_UINT64, zio->io_offset,
				    FM_EREPORT_PAYLOAD_ZFS_ZIO_SIZE,
				    DATA_TYPE_UINT64, zio->io_size, NULL);
		}
	} else if (vd != NULL) {
		/*
		 * If we have a vdev but no zio, this is a device fault, and the
		 * 'stateoroffset' parameter indicates the previous state of the
		 * vdev.
		 */
		fm_payload_set(ereport,
		    FM_EREPORT_PAYLOAD_ZFS_PREV_STATE,
		    DATA_TYPE_UINT64, stateoroffset, NULL);
	}

	/*
	 * Payload for I/Os with corresponding logical information.
	 */
	if (zb != NULL && (zio == NULL || zio->io_logical != NULL)) {
		fm_payload_set(ereport,
		    FM_EREPORT_PAYLOAD_ZFS_ZIO_OBJSET,
		    DATA_TYPE_UINT64, zb->zb_objset,
		    FM_EREPORT_PAYLOAD_ZFS_ZIO_OBJECT,
		    DATA_TYPE_UINT64, zb->zb_object,
		    FM_EREPORT_PAYLOAD_ZFS_ZIO_LEVEL,
		    DATA_TYPE_INT64, zb->zb_level,
		    FM_EREPORT_PAYLOAD_ZFS_ZIO_BLKID,
		    DATA_TYPE_UINT64, zb->zb_blkid, NULL);
	}

	/*
	 * Payload for tuning the zed
	 */
	if (vd != NULL && strcmp(subclass, FM_EREPORT_ZFS_CHECKSUM) == 0) {
		uint64_t cksum_n, cksum_t;

		cksum_n = vdev_prop_get_inherited(vd, VDEV_PROP_CHECKSUM_N);
		if (cksum_n != vdev_prop_default_numeric(VDEV_PROP_CHECKSUM_N))
			fm_payload_set(ereport,
			    FM_EREPORT_PAYLOAD_ZFS_VDEV_CKSUM_N,
			    DATA_TYPE_UINT64,
			    cksum_n,
			    NULL);

		cksum_t = vdev_prop_get_inherited(vd, VDEV_PROP_CHECKSUM_T);
		if (cksum_t != vdev_prop_default_numeric(VDEV_PROP_CHECKSUM_T))
			fm_payload_set(ereport,
			    FM_EREPORT_PAYLOAD_ZFS_VDEV_CKSUM_T,
			    DATA_TYPE_UINT64,
			    cksum_t,
			    NULL);
	}

	if (vd != NULL && strcmp(subclass, FM_EREPORT_ZFS_IO) == 0) {
		uint64_t io_n, io_t;

		io_n = vdev_prop_get_inherited(vd, VDEV_PROP_IO_N);
		if (io_n != vdev_prop_default_numeric(VDEV_PROP_IO_N))
			fm_payload_set(ereport,
			    FM_EREPORT_PAYLOAD_ZFS_VDEV_IO_N,
			    DATA_TYPE_UINT64,
			    io_n,
			    NULL);

		io_t = vdev_prop_get_inherited(vd, VDEV_PROP_IO_T);
		if (io_t != vdev_prop_default_numeric(VDEV_PROP_IO_T))
			fm_payload_set(ereport,
			    FM_EREPORT_PAYLOAD_ZFS_VDEV_IO_T,
			    DATA_TYPE_UINT64,
			    io_t,
			    NULL);
	}

	if (vd != NULL && strcmp(subclass, FM_EREPORT_ZFS_DELAY) == 0) {
		uint64_t slow_io_n, slow_io_t;

		slow_io_n = vdev_prop_get_inherited(vd, VDEV_PROP_SLOW_IO_N);
		if (slow_io_n != vdev_prop_default_numeric(VDEV_PROP_SLOW_IO_N))
			fm_payload_set(ereport,
			    FM_EREPORT_PAYLOAD_ZFS_VDEV_SLOW_IO_N,
			    DATA_TYPE_UINT64,
			    slow_io_n,
			    NULL);

		slow_io_t = vdev_prop_get_inherited(vd, VDEV_PROP_SLOW_IO_T);
		if (slow_io_t != vdev_prop_default_numeric(VDEV_PROP_SLOW_IO_T))
			fm_payload_set(ereport,
			    FM_EREPORT_PAYLOAD_ZFS_VDEV_SLOW_IO_T,
			    DATA_TYPE_UINT64,
			    slow_io_t,
			    NULL);
	}

	mutex_exit(&spa->spa_errlist_lock);

	*ereport_out = ereport;
	*detector_out = detector;
	return (B_TRUE);
}

/* if it's <= 128 bytes, save the corruption directly */
#define	ZFM_MAX_INLINE		(128 / sizeof (uint64_t))

#define	MAX_RANGES		16

typedef struct zfs_ecksum_info {
	/* inline arrays of bits set and cleared. */
	uint64_t zei_bits_set[ZFM_MAX_INLINE];
	uint64_t zei_bits_cleared[ZFM_MAX_INLINE];

	/*
	 * for each range, the number of bits set and cleared.  The Hamming
	 * distance between the good and bad buffers is the sum of them all.
	 */
	uint32_t zei_range_sets[MAX_RANGES];
	uint32_t zei_range_clears[MAX_RANGES];

	struct zei_ranges {
		uint32_t	zr_start;
		uint32_t	zr_end;
	} zei_ranges[MAX_RANGES];

	size_t	zei_range_count;
	uint32_t zei_mingap;
	uint32_t zei_allowed_mingap;

} zfs_ecksum_info_t;

static void
update_bad_bits(uint64_t value_arg, uint32_t *count)
{
	size_t i;
	size_t bits = 0;
	uint64_t value = BE_64(value_arg);

	/* We store the bits in big-endian (largest-first) order */
	for (i = 0; i < 64; i++) {
		if (value & (1ull << i))
			++bits;
	}
	/* update the count of bits changed */
	*count += bits;
}

/*
 * We've now filled up the range array, and need to increase "mingap" and
 * shrink the range list accordingly.  zei_mingap is always the smallest
 * distance between array entries, so we set the new_allowed_gap to be
 * one greater than that.  We then go through the list, joining together
 * any ranges which are closer than the new_allowed_gap.
 *
 * By construction, there will be at least one.  We also update zei_mingap
 * to the new smallest gap, to prepare for our next invocation.
 */
static void
zei_shrink_ranges(zfs_ecksum_info_t *eip)
{
	uint32_t mingap = UINT32_MAX;
	uint32_t new_allowed_gap = eip->zei_mingap + 1;

	size_t idx, output;
	size_t max = eip->zei_range_count;

	struct zei_ranges *r = eip->zei_ranges;

	ASSERT3U(eip->zei_range_count, >, 0);
	ASSERT3U(eip->zei_range_count, <=, MAX_RANGES);

	output = idx = 0;
	while (idx < max - 1) {
		uint32_t start = r[idx].zr_start;
		uint32_t end = r[idx].zr_end;

		while (idx < max - 1) {
			idx++;

			uint32_t nstart = r[idx].zr_start;
			uint32_t nend = r[idx].zr_end;

			uint32_t gap = nstart - end;
			if (gap < new_allowed_gap) {
				end = nend;
				continue;
			}
			if (gap < mingap)
				mingap = gap;
			break;
		}
		r[output].zr_start = start;
		r[output].zr_end = end;
		output++;
	}
	ASSERT3U(output, <, eip->zei_range_count);
	eip->zei_range_count = output;
	eip->zei_mingap = mingap;
	eip->zei_allowed_mingap = new_allowed_gap;
}

static void
zei_add_range(zfs_ecksum_info_t *eip, int start, int end)
{
	struct zei_ranges *r = eip->zei_ranges;
	size_t count = eip->zei_range_count;

	if (count >= MAX_RANGES) {
		zei_shrink_ranges(eip);
		count = eip->zei_range_count;
	}
	if (count == 0) {
		eip->zei_mingap = UINT32_MAX;
		eip->zei_allowed_mingap = 1;
	} else {
		int gap = start - r[count - 1].zr_end;

		if (gap < eip->zei_allowed_mingap) {
			r[count - 1].zr_end = end;
			return;
		}
		if (gap < eip->zei_mingap)
			eip->zei_mingap = gap;
	}
	r[count].zr_start = start;
	r[count].zr_end = end;
	eip->zei_range_count++;
}

static size_t
zei_range_total_size(zfs_ecksum_info_t *eip)
{
	struct zei_ranges *r = eip->zei_ranges;
	size_t count = eip->zei_range_count;
	size_t result = 0;
	size_t idx;

	for (idx = 0; idx < count; idx++)
		result += (r[idx].zr_end - r[idx].zr_start);

	return (result);
}

static zfs_ecksum_info_t *
annotate_ecksum(nvlist_t *ereport, zio_bad_cksum_t *info,
    const abd_t *goodabd, const abd_t *badabd, size_t size,
    boolean_t drop_if_identical)
{
	const uint64_t *good;
	const uint64_t *bad;

	size_t nui64s = size / sizeof (uint64_t);

	size_t inline_size;
	int no_inline = 0;
	size_t idx;
	size_t range;

	size_t offset = 0;
	ssize_t start = -1;

	zfs_ecksum_info_t *eip = kmem_zalloc(sizeof (*eip), KM_SLEEP);

	/* don't do any annotation for injected checksum errors */
	if (info != NULL && info->zbc_injected)
		return (eip);

	if (info != NULL && info->zbc_has_cksum) {
		fm_payload_set(ereport,
		    FM_EREPORT_PAYLOAD_ZFS_CKSUM_ALGO,
		    DATA_TYPE_STRING,
		    info->zbc_checksum_name,
		    NULL);

		if (info->zbc_byteswapped) {
			fm_payload_set(ereport,
			    FM_EREPORT_PAYLOAD_ZFS_CKSUM_BYTESWAP,
			    DATA_TYPE_BOOLEAN, 1,
			    NULL);
		}
	}

	if (badabd == NULL || goodabd == NULL)
		return (eip);

	ASSERT3U(nui64s, <=, UINT32_MAX);
	ASSERT3U(size, ==, nui64s * sizeof (uint64_t));
	ASSERT3U(size, <=, SPA_MAXBLOCKSIZE);
	ASSERT3U(size, <=, UINT32_MAX);

	good = (const uint64_t *) abd_borrow_buf_copy((abd_t *)goodabd, size);
	bad = (const uint64_t *) abd_borrow_buf_copy((abd_t *)badabd, size);

	/* build up the range list by comparing the two buffers. */
	for (idx = 0; idx < nui64s; idx++) {
		if (good[idx] == bad[idx]) {
			if (start == -1)
				continue;

			zei_add_range(eip, start, idx);
			start = -1;
		} else {
			if (start != -1)
				continue;

			start = idx;
		}
	}
	if (start != -1)
		zei_add_range(eip, start, idx);

	/* See if it will fit in our inline buffers */
	inline_size = zei_range_total_size(eip);
	if (inline_size > ZFM_MAX_INLINE)
		no_inline = 1;

	/*
	 * If there is no change and we want to drop if the buffers are
	 * identical, do so.
	 */
	if (inline_size == 0 && drop_if_identical) {
		kmem_free(eip, sizeof (*eip));
		abd_return_buf((abd_t *)goodabd, (void *)good, size);
		abd_return_buf((abd_t *)badabd, (void *)bad, size);
		return (NULL);
	}

	/*
	 * Now walk through the ranges, filling in the details of the
	 * differences.  Also convert our uint64_t-array offsets to byte
	 * offsets.
	 */
	for (range = 0; range < eip->zei_range_count; range++) {
		size_t start = eip->zei_ranges[range].zr_start;
		size_t end = eip->zei_ranges[range].zr_end;

		for (idx = start; idx < end; idx++) {
			uint64_t set, cleared;

			// bits set in bad, but not in good
			set = ((~good[idx]) & bad[idx]);
			// bits set in good, but not in bad
			cleared = (good[idx] & (~bad[idx]));

			if (!no_inline) {
				ASSERT3U(offset, <, inline_size);
				eip->zei_bits_set[offset] = set;
				eip->zei_bits_cleared[offset] = cleared;
				offset++;
			}

			update_bad_bits(set, &eip->zei_range_sets[range]);
			update_bad_bits(cleared, &eip->zei_range_clears[range]);
		}

		/* convert to byte offsets */
		eip->zei_ranges[range].zr_start	*= sizeof (uint64_t);
		eip->zei_ranges[range].zr_end	*= sizeof (uint64_t);
	}

	abd_return_buf((abd_t *)goodabd, (void *)good, size);
	abd_return_buf((abd_t *)badabd, (void *)bad, size);

	eip->zei_allowed_mingap	*= sizeof (uint64_t);
	inline_size		*= sizeof (uint64_t);

	/* fill in ereport */
	fm_payload_set(ereport,
	    FM_EREPORT_PAYLOAD_ZFS_BAD_OFFSET_RANGES,
	    DATA_TYPE_UINT32_ARRAY, 2 * eip->zei_range_count,
	    (uint32_t *)eip->zei_ranges,
	    FM_EREPORT_PAYLOAD_ZFS_BAD_RANGE_MIN_GAP,
	    DATA_TYPE_UINT32, eip->zei_allowed_mingap,
	    FM_EREPORT_PAYLOAD_ZFS_BAD_RANGE_SETS,
	    DATA_TYPE_UINT32_ARRAY, eip->zei_range_count, eip->zei_range_sets,
	    FM_EREPORT_PAYLOAD_ZFS_BAD_RANGE_CLEARS,
	    DATA_TYPE_UINT32_ARRAY, eip->zei_range_count, eip->zei_range_clears,
	    NULL);

	if (!no_inline) {
		fm_payload_set(ereport,
		    FM_EREPORT_PAYLOAD_ZFS_BAD_SET_BITS,
		    DATA_TYPE_UINT8_ARRAY,
		    inline_size, (uint8_t *)eip->zei_bits_set,
		    FM_EREPORT_PAYLOAD_ZFS_BAD_CLEARED_BITS,
		    DATA_TYPE_UINT8_ARRAY,
		    inline_size, (uint8_t *)eip->zei_bits_cleared,
		    NULL);
	}
	return (eip);
}
#else
void
zfs_ereport_clear(spa_t *spa, vdev_t *vd)
{
	(void) spa, (void) vd;
}
#endif

/*
 * Make sure our event is still valid for the given zio/vdev/pool.  For example,
 * we don't want to keep logging events for a faulted or missing vdev.
 */
boolean_t
zfs_ereport_is_valid(const char *subclass, spa_t *spa, vdev_t *vd, zio_t *zio)
{
#ifdef _KERNEL
	/*
	 * If we are doing a spa_tryimport() or in recovery mode,
	 * ignore errors.
	 */
	if (spa_load_state(spa) == SPA_LOAD_TRYIMPORT ||
	    spa_load_state(spa) == SPA_LOAD_RECOVER)
		return (B_FALSE);

	/*
	 * If we are in the middle of opening a pool, and the previous attempt
	 * failed, don't bother logging any new ereports - we're just going to
	 * get the same diagnosis anyway.
	 */
	if (spa_load_state(spa) != SPA_LOAD_NONE &&
	    spa->spa_last_open_failed)
		return (B_FALSE);

	if (zio != NULL) {
		/* If this is not a read or write zio, ignore the error */
		if (zio->io_type != ZIO_TYPE_READ &&
		    zio->io_type != ZIO_TYPE_WRITE)
			return (B_FALSE);

		if (vd != NULL) {
			/*
			 * If the vdev has already been marked as failing due
			 * to a failed probe, then ignore any subsequent I/O
			 * errors, as the DE will automatically fault the vdev
			 * on the first such failure.  This also catches cases
			 * where vdev_remove_wanted is set and the device has
			 * not yet been asynchronously placed into the REMOVED
			 * state.
			 */
			if (zio->io_vd == vd && !vdev_accessible(vd, zio))
				return (B_FALSE);

			/*
			 * Ignore checksum errors for reads from DTL regions of
			 * leaf vdevs.
			 */
			if (zio->io_type == ZIO_TYPE_READ &&
			    zio->io_error == ECKSUM &&
			    vd->vdev_ops->vdev_op_leaf &&
			    vdev_dtl_contains(vd, DTL_MISSING, zio->io_txg, 1))
				return (B_FALSE);
		}
	}

	/*
	 * For probe failure, we want to avoid posting ereports if we've
	 * already removed the device in the meantime.
	 */
	if (vd != NULL &&
	    strcmp(subclass, FM_EREPORT_ZFS_PROBE_FAILURE) == 0 &&
	    (vd->vdev_remove_wanted || vd->vdev_state == VDEV_STATE_REMOVED))
		return (B_FALSE);

	/* Ignore bogus delay events (like from ioctls or unqueued IOs) */
	if ((strcmp(subclass, FM_EREPORT_ZFS_DELAY) == 0) &&
	    (zio != NULL) && (!zio->io_timestamp)) {
		return (B_FALSE);
	}
#else
	(void) subclass, (void) spa, (void) vd, (void) zio;
#endif
	return (B_TRUE);
}

/*
 * Post an ereport for the given subclass
 *
 * Returns
 * - 0 if an event was posted
 * - EINVAL if there was a problem posting event
 * - EBUSY if the event was rate limited
 * - EALREADY if the event was already posted (duplicate)
 */
int
zfs_ereport_post(const char *subclass, spa_t *spa, vdev_t *vd,
    const zbookmark_phys_t *zb, zio_t *zio, uint64_t state)
{
	int rc = 0;
#ifdef _KERNEL
	nvlist_t *ereport = NULL;
	nvlist_t *detector = NULL;

	if (!zfs_ereport_is_valid(subclass, spa, vd, zio))
		return (EINVAL);

	if (zfs_ereport_is_duplicate(subclass, spa, vd, zb, zio, 0, 0))
		return (SET_ERROR(EALREADY));

	if (zfs_is_ratelimiting_event(subclass, vd))
		return (SET_ERROR(EBUSY));

	if (!zfs_ereport_start(&ereport, &detector, subclass, spa, vd,
	    zb, zio, state, 0))
		return (SET_ERROR(EINVAL));	/* couldn't post event */

	if (ereport == NULL)
		return (SET_ERROR(EINVAL));

	/* Cleanup is handled by the callback function */
	rc = zfs_zevent_post(ereport, detector, zfs_zevent_post_cb);
#else
	(void) subclass, (void) spa, (void) vd, (void) zb, (void) zio,
	    (void) state;
#endif
	return (rc);
}

/*
 * Prepare a checksum ereport
 *
 * Returns
 * - 0 if an event was posted
 * - EINVAL if there was a problem posting event
 * - EBUSY if the event was rate limited
 * - EALREADY if the event was already posted (duplicate)
 */
int
zfs_ereport_start_checksum(spa_t *spa, vdev_t *vd, const zbookmark_phys_t *zb,
    struct zio *zio, uint64_t offset, uint64_t length, zio_bad_cksum_t *info)
{
	zio_cksum_report_t *report;

#ifdef _KERNEL
	if (!zfs_ereport_is_valid(FM_EREPORT_ZFS_CHECKSUM, spa, vd, zio))
		return (SET_ERROR(EINVAL));

	if (zfs_ereport_is_duplicate(FM_EREPORT_ZFS_CHECKSUM, spa, vd, zb, zio,
	    offset, length))
		return (SET_ERROR(EALREADY));

	if (zfs_is_ratelimiting_event(FM_EREPORT_ZFS_CHECKSUM, vd))
		return (SET_ERROR(EBUSY));
#else
	(void) zb, (void) offset;
#endif

	report = kmem_zalloc(sizeof (*report), KM_SLEEP);

	zio_vsd_default_cksum_report(zio, report);

	/* copy the checksum failure information if it was provided */
	if (info != NULL) {
		report->zcr_ckinfo = kmem_zalloc(sizeof (*info), KM_SLEEP);
		memcpy(report->zcr_ckinfo, info, sizeof (*info));
	}

	report->zcr_sector = 1ULL << vd->vdev_top->vdev_ashift;
	report->zcr_align =
	    vdev_psize_to_asize(vd->vdev_top, report->zcr_sector);
	report->zcr_length = length;

#ifdef _KERNEL
	(void) zfs_ereport_start(&report->zcr_ereport, &report->zcr_detector,
	    FM_EREPORT_ZFS_CHECKSUM, spa, vd, zb, zio, offset, length);

	if (report->zcr_ereport == NULL) {
		zfs_ereport_free_checksum(report);
		return (0);
	}
#endif

	mutex_enter(&spa->spa_errlist_lock);
	report->zcr_next = zio->io_logical->io_cksum_report;
	zio->io_logical->io_cksum_report = report;
	mutex_exit(&spa->spa_errlist_lock);
	return (0);
}

void
zfs_ereport_finish_checksum(zio_cksum_report_t *report, const abd_t *good_data,
    const abd_t *bad_data, boolean_t drop_if_identical)
{
#ifdef _KERNEL
	zfs_ecksum_info_t *info;

	info = annotate_ecksum(report->zcr_ereport, report->zcr_ckinfo,
	    good_data, bad_data, report->zcr_length, drop_if_identical);
	if (info != NULL)
		zfs_zevent_post(report->zcr_ereport,
		    report->zcr_detector, zfs_zevent_post_cb);
	else
		zfs_zevent_post_cb(report->zcr_ereport, report->zcr_detector);

	report->zcr_ereport = report->zcr_detector = NULL;
	if (info != NULL)
		kmem_free(info, sizeof (*info));
#else
	(void) report, (void) good_data, (void) bad_data,
	    (void) drop_if_identical;
#endif
}

void
zfs_ereport_free_checksum(zio_cksum_report_t *rpt)
{
#ifdef _KERNEL
	if (rpt->zcr_ereport != NULL) {
		fm_nvlist_destroy(rpt->zcr_ereport,
		    FM_NVA_FREE);
		fm_nvlist_destroy(rpt->zcr_detector,
		    FM_NVA_FREE);
	}
#endif
	rpt->zcr_free(rpt->zcr_cbdata, rpt->zcr_cbinfo);

	if (rpt->zcr_ckinfo != NULL)
		kmem_free(rpt->zcr_ckinfo, sizeof (*rpt->zcr_ckinfo));

	kmem_free(rpt, sizeof (*rpt));
}

/*
 * Post a checksum ereport
 *
 * Returns
 * - 0 if an event was posted
 * - EINVAL if there was a problem posting event
 * - EBUSY if the event was rate limited
 * - EALREADY if the event was already posted (duplicate)
 */
int
zfs_ereport_post_checksum(spa_t *spa, vdev_t *vd, const zbookmark_phys_t *zb,
    struct zio *zio, uint64_t offset, uint64_t length,
    const abd_t *good_data, const abd_t *bad_data, zio_bad_cksum_t *zbc)
{
	int rc = 0;
#ifdef _KERNEL
	nvlist_t *ereport = NULL;
	nvlist_t *detector = NULL;
	zfs_ecksum_info_t *info;

	if (!zfs_ereport_is_valid(FM_EREPORT_ZFS_CHECKSUM, spa, vd, zio))
		return (SET_ERROR(EINVAL));

	if (zfs_ereport_is_duplicate(FM_EREPORT_ZFS_CHECKSUM, spa, vd, zb, zio,
	    offset, length))
		return (SET_ERROR(EALREADY));

	if (zfs_is_ratelimiting_event(FM_EREPORT_ZFS_CHECKSUM, vd))
		return (SET_ERROR(EBUSY));

	if (!zfs_ereport_start(&ereport, &detector, FM_EREPORT_ZFS_CHECKSUM,
	    spa, vd, zb, zio, offset, length) || (ereport == NULL)) {
		return (SET_ERROR(EINVAL));
	}

	info = annotate_ecksum(ereport, zbc, good_data, bad_data, length,
	    B_FALSE);

	if (info != NULL) {
		rc = zfs_zevent_post(ereport, detector, zfs_zevent_post_cb);
		kmem_free(info, sizeof (*info));
	}
#else
	(void) spa, (void) vd, (void) zb, (void) zio, (void) offset,
	    (void) length, (void) good_data, (void) bad_data, (void) zbc;
#endif
	return (rc);
}

/*
 * The 'sysevent.fs.zfs.*' events are signals posted to notify user space of
 * change in the pool.  All sysevents are listed in sys/sysevent/eventdefs.h
 * and are designed to be consumed by the ZFS Event Daemon (ZED).  For
 * additional details refer to the zed(8) man page.
 */
nvlist_t *
zfs_event_create(spa_t *spa, vdev_t *vd, const char *type, const char *name,
    nvlist_t *aux)
{
	nvlist_t *resource = NULL;
#ifdef _KERNEL
	char class[64];

	if (spa_load_state(spa) == SPA_LOAD_TRYIMPORT)
		return (NULL);

	if ((resource = fm_nvlist_create(NULL)) == NULL)
		return (NULL);

	(void) snprintf(class, sizeof (class), "%s.%s.%s", type,
	    ZFS_ERROR_CLASS, name);
	VERIFY0(nvlist_add_uint8(resource, FM_VERSION, FM_RSRC_VERSION));
	VERIFY0(nvlist_add_string(resource, FM_CLASS, class));
	VERIFY0(nvlist_add_string(resource,
	    FM_EREPORT_PAYLOAD_ZFS_POOL, spa_name(spa)));
	VERIFY0(nvlist_add_uint64(resource,
	    FM_EREPORT_PAYLOAD_ZFS_POOL_GUID, spa_guid(spa)));
	VERIFY0(nvlist_add_uint64(resource,
	    FM_EREPORT_PAYLOAD_ZFS_POOL_STATE, spa_state(spa)));
	VERIFY0(nvlist_add_int32(resource,
	    FM_EREPORT_PAYLOAD_ZFS_POOL_CONTEXT, spa_load_state(spa)));

	if (vd) {
		VERIFY0(nvlist_add_uint64(resource,
		    FM_EREPORT_PAYLOAD_ZFS_VDEV_GUID, vd->vdev_guid));
		VERIFY0(nvlist_add_uint64(resource,
		    FM_EREPORT_PAYLOAD_ZFS_VDEV_STATE, vd->vdev_state));
		if (vd->vdev_path != NULL)
			VERIFY0(nvlist_add_string(resource,
			    FM_EREPORT_PAYLOAD_ZFS_VDEV_PATH, vd->vdev_path));
		if (vd->vdev_devid != NULL)
			VERIFY0(nvlist_add_string(resource,
			    FM_EREPORT_PAYLOAD_ZFS_VDEV_DEVID, vd->vdev_devid));
		if (vd->vdev_fru != NULL)
			VERIFY0(nvlist_add_string(resource,
			    FM_EREPORT_PAYLOAD_ZFS_VDEV_FRU, vd->vdev_fru));
		if (vd->vdev_enc_sysfs_path != NULL)
			VERIFY0(nvlist_add_string(resource,
			    FM_EREPORT_PAYLOAD_ZFS_VDEV_ENC_SYSFS_PATH,
			    vd->vdev_enc_sysfs_path));
	}

	/* also copy any optional payload data */
	if (aux) {
		nvpair_t *elem = NULL;

		while ((elem = nvlist_next_nvpair(aux, elem)) != NULL)
			(void) nvlist_add_nvpair(resource, elem);
	}
#else
	(void) spa, (void) vd, (void) type, (void) name, (void) aux;
#endif
	return (resource);
}

static void
zfs_post_common(spa_t *spa, vdev_t *vd, const char *type, const char *name,
    nvlist_t *aux)
{
#ifdef _KERNEL
	nvlist_t *resource;

	resource = zfs_event_create(spa, vd, type, name, aux);
	if (resource)
		zfs_zevent_post(resource, NULL, zfs_zevent_post_cb);
#else
	(void) spa, (void) vd, (void) type, (void) name, (void) aux;
#endif
}

/*
 * The 'resource.fs.zfs.removed' event is an internal signal that the given vdev
 * has been removed from the system.  This will cause the DE to ignore any
 * recent I/O errors, inferring that they are due to the asynchronous device
 * removal.
 */
void
zfs_post_remove(spa_t *spa, vdev_t *vd)
{
	zfs_post_common(spa, vd, FM_RSRC_CLASS, FM_RESOURCE_REMOVED, NULL);
}

/*
 * The 'resource.fs.zfs.autoreplace' event is an internal signal that the pool
 * has the 'autoreplace' property set, and therefore any broken vdevs will be
 * handled by higher level logic, and no vdev fault should be generated.
 */
void
zfs_post_autoreplace(spa_t *spa, vdev_t *vd)
{
	zfs_post_common(spa, vd, FM_RSRC_CLASS, FM_RESOURCE_AUTOREPLACE, NULL);
}

/*
 * The 'resource.fs.zfs.statechange' event is an internal signal that the
 * given vdev has transitioned its state to DEGRADED or HEALTHY.  This will
 * cause the retire agent to repair any outstanding fault management cases
 * open because the device was not found (fault.fs.zfs.device).
 */
void
zfs_post_state_change(spa_t *spa, vdev_t *vd, uint64_t laststate)
{
#ifdef _KERNEL
	nvlist_t *aux;

	/*
	 * Add optional supplemental keys to payload
	 */
	aux = fm_nvlist_create(NULL);
	if (vd && aux) {
		if (vd->vdev_physpath) {
			fnvlist_add_string(aux,
			    FM_EREPORT_PAYLOAD_ZFS_VDEV_PHYSPATH,
			    vd->vdev_physpath);
		}
		if (vd->vdev_enc_sysfs_path) {
			fnvlist_add_string(aux,
			    FM_EREPORT_PAYLOAD_ZFS_VDEV_ENC_SYSFS_PATH,
			    vd->vdev_enc_sysfs_path);
		}

		fnvlist_add_uint64(aux,
		    FM_EREPORT_PAYLOAD_ZFS_VDEV_LASTSTATE, laststate);
	}

	zfs_post_common(spa, vd, FM_RSRC_CLASS, FM_RESOURCE_STATECHANGE,
	    aux);

	if (aux)
		fm_nvlist_destroy(aux, FM_NVA_FREE);
#else
	(void) spa, (void) vd, (void) laststate;
#endif
}

#ifdef _KERNEL
void
zfs_ereport_init(void)
{
	mutex_init(&recent_events_lock, NULL, MUTEX_DEFAULT, NULL);
	list_create(&recent_events_list, sizeof (recent_events_node_t),
	    offsetof(recent_events_node_t, re_list_link));
	avl_create(&recent_events_tree,  recent_events_compare,
	    sizeof (recent_events_node_t), offsetof(recent_events_node_t,
	    re_tree_link));
}

/*
 * This 'early' fini needs to run before zfs_fini() which on Linux waits
 * for the system_delay_taskq to drain.
 */
void
zfs_ereport_taskq_fini(void)
{
	mutex_enter(&recent_events_lock);
	if (recent_events_cleaner_tqid != 0) {
		taskq_cancel_id(system_delay_taskq, recent_events_cleaner_tqid);
		recent_events_cleaner_tqid = 0;
	}
	mutex_exit(&recent_events_lock);
}

void
zfs_ereport_fini(void)
{
	recent_events_node_t *entry;

	while ((entry = list_remove_head(&recent_events_list)) != NULL) {
		avl_remove(&recent_events_tree, entry);
		kmem_free(entry, sizeof (*entry));
	}
	avl_destroy(&recent_events_tree);
	list_destroy(&recent_events_list);
	mutex_destroy(&recent_events_lock);
}

void
zfs_ereport_snapshot_post(const char *subclass, spa_t *spa, const char *name)
{
	nvlist_t *aux;

	aux = fm_nvlist_create(NULL);
	fnvlist_add_string(aux, FM_EREPORT_PAYLOAD_ZFS_SNAPSHOT_NAME, name);

	zfs_post_common(spa, NULL, FM_RSRC_CLASS, subclass, aux);
	fm_nvlist_destroy(aux, FM_NVA_FREE);
}

/*
 * Post when a event when a zvol is created or removed
 *
 * This is currently only used by macOS, since it uses the event to create
 * symlinks between the volume name (mypool/myvol) and the actual /dev
 * device (/dev/disk3).  For example:
 *
 * /var/run/zfs/dsk/mypool/myvol -> /dev/disk3
 *
 * name: The full name of the zvol ("mypool/myvol")
 * dev_name: The full /dev name for the zvol ("/dev/disk3")
 * raw_name: The raw  /dev name for the zvol ("/dev/rdisk3")
 */
void
zfs_ereport_zvol_post(const char *subclass, const char *name,
    const char *dev_name, const char *raw_name)
{
	nvlist_t *aux;
	char *r;

	boolean_t locked = mutex_owned(&spa_namespace_lock);
	if (!locked) mutex_enter(&spa_namespace_lock);
	spa_t *spa = spa_lookup(name);
	if (!locked) mutex_exit(&spa_namespace_lock);

	if (spa == NULL)
		return;

	aux = fm_nvlist_create(NULL);
	fnvlist_add_string(aux, FM_EREPORT_PAYLOAD_ZFS_DEVICE_NAME, dev_name);
	fnvlist_add_string(aux, FM_EREPORT_PAYLOAD_ZFS_RAW_DEVICE_NAME,
	    raw_name);
	r = strchr(name, '/');
	if (r && r[1])
		fnvlist_add_string(aux, FM_EREPORT_PAYLOAD_ZFS_VOLUME, &r[1]);

	zfs_post_common(spa, NULL, FM_RSRC_CLASS, subclass, aux);
	fm_nvlist_destroy(aux, FM_NVA_FREE);
}

EXPORT_SYMBOL(zfs_ereport_post);
EXPORT_SYMBOL(zfs_ereport_is_valid);
EXPORT_SYMBOL(zfs_ereport_post_checksum);
EXPORT_SYMBOL(zfs_post_remove);
EXPORT_SYMBOL(zfs_post_autoreplace);
EXPORT_SYMBOL(zfs_post_state_change);

ZFS_MODULE_PARAM(zfs_zevent, zfs_zevent_, retain_max, UINT, ZMOD_RW,
	"Maximum recent zevents records to retain for duplicate checking");
ZFS_MODULE_PARAM(zfs_zevent, zfs_zevent_, retain_expire_secs, UINT, ZMOD_RW,
	"Expiration time for recent zevents records");
#endif /* _KERNEL */
