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
 * Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#include <sys/spa.h>
#include <sys/spa_impl.h>
#include <sys/vdev.h>
#include <sys/vdev_impl.h>
#include <sys/zio.h>

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
 *      +---------------+
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
 */
void
zfs_ereport_post(const char *subclass, spa_t *spa, vdev_t *vd, zio_t *zio,
    uint64_t stateoroffset, uint64_t size)
{
#ifdef _KERNEL
	nvlist_t *ereport, *detector;
	uint64_t ena;
	char class[64];

	/*
	 * If we are doing a spa_tryimport(), ignore errors.
	 */
	if (spa->spa_load_state == SPA_LOAD_TRYIMPORT)
		return;

	/*
	 * If we are in the middle of opening a pool, and the previous attempt
	 * failed, don't bother logging any new ereports - we're just going to
	 * get the same diagnosis anyway.
	 */
	if (spa->spa_load_state != SPA_LOAD_NONE &&
	    spa->spa_last_open_failed)
		return;

	if (zio != NULL) {
		/*
		 * If this is not a read or write zio, ignore the error.  This
		 * can occur if the DKIOCFLUSHWRITECACHE ioctl fails.
		 */
		if (zio->io_type != ZIO_TYPE_READ &&
		    zio->io_type != ZIO_TYPE_WRITE)
			return;

		/*
		 * Ignore any errors from speculative I/Os, as failure is an
		 * expected result.
		 */
		if (zio->io_flags & ZIO_FLAG_SPECULATIVE)
			return;

		/*
		 * If this I/O is not a retry I/O, don't post an ereport.
		 * Otherwise, we risk making bad diagnoses based on B_FAILFAST
		 * I/Os.
		 */
		if (zio->io_error == EIO &&
		    !(zio->io_flags & ZIO_FLAG_IO_RETRY))
			return;

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
			if (zio->io_vd == vd &&
			    !vdev_accessible(vd, zio) &&
			    strcmp(subclass, FM_EREPORT_ZFS_PROBE_FAILURE) != 0)
				return;

			/*
			 * Ignore checksum errors for reads from DTL regions of
			 * leaf vdevs.
			 */
			if (zio->io_type == ZIO_TYPE_READ &&
			    zio->io_error == ECKSUM &&
			    vd->vdev_ops->vdev_op_leaf &&
			    vdev_dtl_contains(vd, DTL_MISSING, zio->io_txg, 1))
				return;
		}
	}

	if ((ereport = fm_nvlist_create(NULL)) == NULL)
		return;

	if ((detector = fm_nvlist_create(NULL)) == NULL) {
		fm_nvlist_destroy(ereport, FM_NVA_FREE);
		return;
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
	if (spa->spa_load_state != SPA_LOAD_NONE) {
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
	fm_payload_set(ereport, FM_EREPORT_PAYLOAD_ZFS_POOL,
	    DATA_TYPE_STRING, spa_name(spa), FM_EREPORT_PAYLOAD_ZFS_POOL_GUID,
	    DATA_TYPE_UINT64, spa_guid(spa),
	    FM_EREPORT_PAYLOAD_ZFS_POOL_CONTEXT, DATA_TYPE_INT32,
	    spa->spa_load_state, NULL);

	if (spa != NULL) {
		fm_payload_set(ereport, FM_EREPORT_PAYLOAD_ZFS_POOL_FAILMODE,
		    DATA_TYPE_STRING,
		    spa_get_failmode(spa) == ZIO_FAILURE_MODE_WAIT ?
		    FM_EREPORT_FAILMODE_WAIT :
		    spa_get_failmode(spa) == ZIO_FAILURE_MODE_CONTINUE ?
		    FM_EREPORT_FAILMODE_CONTINUE : FM_EREPORT_FAILMODE_PANIC,
		    NULL);
	}

	if (vd != NULL) {
		vdev_t *pvd = vd->vdev_parent;

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
	}

	if (zio != NULL) {
		/*
		 * Payload common to all I/Os.
		 */
		fm_payload_set(ereport, FM_EREPORT_PAYLOAD_ZFS_ZIO_ERR,
		    DATA_TYPE_INT32, zio->io_error, NULL);

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

		/*
		 * Payload for I/Os with corresponding logical information.
		 */
		if (zio->io_logical != NULL)
			fm_payload_set(ereport,
			    FM_EREPORT_PAYLOAD_ZFS_ZIO_OBJSET,
			    DATA_TYPE_UINT64,
			    zio->io_logical->io_bookmark.zb_objset,
			    FM_EREPORT_PAYLOAD_ZFS_ZIO_OBJECT,
			    DATA_TYPE_UINT64,
			    zio->io_logical->io_bookmark.zb_object,
			    FM_EREPORT_PAYLOAD_ZFS_ZIO_LEVEL,
			    DATA_TYPE_INT64,
			    zio->io_logical->io_bookmark.zb_level,
			    FM_EREPORT_PAYLOAD_ZFS_ZIO_BLKID,
			    DATA_TYPE_UINT64,
			    zio->io_logical->io_bookmark.zb_blkid, NULL);
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
	mutex_exit(&spa->spa_errlist_lock);

	fm_ereport_post(ereport, EVCH_SLEEP);

	fm_nvlist_destroy(ereport, FM_NVA_FREE);
	fm_nvlist_destroy(detector, FM_NVA_FREE);
#endif
}

static void
zfs_post_common(spa_t *spa, vdev_t *vd, const char *name)
{
#ifdef _KERNEL
	nvlist_t *resource;
	char class[64];

	if ((resource = fm_nvlist_create(NULL)) == NULL)
		return;

	(void) snprintf(class, sizeof (class), "%s.%s.%s", FM_RSRC_RESOURCE,
	    ZFS_ERROR_CLASS, name);
	VERIFY(nvlist_add_uint8(resource, FM_VERSION, FM_RSRC_VERSION) == 0);
	VERIFY(nvlist_add_string(resource, FM_CLASS, class) == 0);
	VERIFY(nvlist_add_uint64(resource,
	    FM_EREPORT_PAYLOAD_ZFS_POOL_GUID, spa_guid(spa)) == 0);
	if (vd)
		VERIFY(nvlist_add_uint64(resource,
		    FM_EREPORT_PAYLOAD_ZFS_VDEV_GUID, vd->vdev_guid) == 0);

	fm_ereport_post(resource, EVCH_SLEEP);

	fm_nvlist_destroy(resource, FM_NVA_FREE);
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
	zfs_post_common(spa, vd, FM_RESOURCE_REMOVED);
}

/*
 * The 'resource.fs.zfs.autoreplace' event is an internal signal that the pool
 * has the 'autoreplace' property set, and therefore any broken vdevs will be
 * handled by higher level logic, and no vdev fault should be generated.
 */
void
zfs_post_autoreplace(spa_t *spa, vdev_t *vd)
{
	zfs_post_common(spa, vd, FM_RESOURCE_AUTOREPLACE);
}
