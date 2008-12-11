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
 * Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

/*
 * ZFS fault injection
 *
 * To handle fault injection, we keep track of a series of zinject_record_t
 * structures which describe which logical block(s) should be injected with a
 * fault.  These are kept in a global list.  Each record corresponds to a given
 * spa_t and maintains a special hold on the spa_t so that it cannot be deleted
 * or exported while the injection record exists.
 *
 * Device level injection is done using the 'zi_guid' field.  If this is set, it
 * means that the error is destined for a particular device, not a piece of
 * data.
 *
 * This is a rather poor data structure and algorithm, but we don't expect more
 * than a few faults at any one time, so it should be sufficient for our needs.
 */

#include <sys/arc.h>
#include <sys/zio_impl.h>
#include <sys/zfs_ioctl.h>
#include <sys/spa_impl.h>
#include <sys/vdev_impl.h>
#include <sys/fs/zfs.h>

uint32_t zio_injection_enabled;

typedef struct inject_handler {
	int			zi_id;
	spa_t			*zi_spa;
	zinject_record_t	zi_record;
	list_node_t		zi_link;
} inject_handler_t;

static list_t inject_handlers;
static krwlock_t inject_lock;
static int inject_next_id = 1;

/*
 * Returns true if the given record matches the I/O in progress.
 */
static boolean_t
zio_match_handler(zbookmark_t *zb, uint64_t type,
    zinject_record_t *record, int error)
{
	/*
	 * Check for a match against the MOS, which is based on type
	 */
	if (zb->zb_objset == 0 && record->zi_objset == 0 &&
	    record->zi_object == 0) {
		if (record->zi_type == DMU_OT_NONE ||
		    type == record->zi_type)
			return (record->zi_freq == 0 ||
			    spa_get_random(100) < record->zi_freq);
		else
			return (B_FALSE);
	}

	/*
	 * Check for an exact match.
	 */
	if (zb->zb_objset == record->zi_objset &&
	    zb->zb_object == record->zi_object &&
	    zb->zb_level == record->zi_level &&
	    zb->zb_blkid >= record->zi_start &&
	    zb->zb_blkid <= record->zi_end &&
	    error == record->zi_error)
		return (record->zi_freq == 0 ||
		    spa_get_random(100) < record->zi_freq);

	return (B_FALSE);
}

/*
 * Determine if the I/O in question should return failure.  Returns the errno
 * to be returned to the caller.
 */
int
zio_handle_fault_injection(zio_t *zio, int error)
{
	int ret = 0;
	inject_handler_t *handler;

	/*
	 * Ignore I/O not associated with any logical data.
	 */
	if (zio->io_logical == NULL)
		return (0);

	/*
	 * Currently, we only support fault injection on reads.
	 */
	if (zio->io_type != ZIO_TYPE_READ)
		return (0);

	rw_enter(&inject_lock, RW_READER);

	for (handler = list_head(&inject_handlers); handler != NULL;
	    handler = list_next(&inject_handlers, handler)) {

		/* Ignore errors not destined for this pool */
		if (zio->io_spa != handler->zi_spa)
			continue;

		/* Ignore device errors */
		if (handler->zi_record.zi_guid != 0)
			continue;

		/* If this handler matches, return EIO */
		if (zio_match_handler(&zio->io_logical->io_bookmark,
		    zio->io_bp ? BP_GET_TYPE(zio->io_bp) : DMU_OT_NONE,
		    &handler->zi_record, error)) {
			ret = error;
			break;
		}
	}

	rw_exit(&inject_lock);

	return (ret);
}

/*
 * Determine if the zio is part of a label update and has an injection
 * handler associated with that portion of the label. Currently, we
 * allow error injection in either the nvlist or the uberblock region of
 * of the vdev label.
 */
int
zio_handle_label_injection(zio_t *zio, int error)
{
	inject_handler_t *handler;
	vdev_t *vd = zio->io_vd;
	uint64_t offset = zio->io_offset;
	int label;
	int ret = 0;

	if (offset + zio->io_size > VDEV_LABEL_START_SIZE &&
	    offset < vd->vdev_psize - VDEV_LABEL_END_SIZE)
		return (0);

	rw_enter(&inject_lock, RW_READER);

	for (handler = list_head(&inject_handlers); handler != NULL;
	    handler = list_next(&inject_handlers, handler)) {
		uint64_t start = handler->zi_record.zi_start;
		uint64_t end = handler->zi_record.zi_end;

		/* Ignore device only faults */
		if (handler->zi_record.zi_start == 0)
			continue;

		/*
		 * The injection region is the relative offsets within a
		 * vdev label. We must determine the label which is being
		 * updated and adjust our region accordingly.
		 */
		label = vdev_label_number(vd->vdev_psize, offset);
		start = vdev_label_offset(vd->vdev_psize, label, start);
		end = vdev_label_offset(vd->vdev_psize, label, end);

		if (zio->io_vd->vdev_guid == handler->zi_record.zi_guid &&
		    (offset >= start && offset <= end)) {
			ret = error;
			break;
		}
	}
	rw_exit(&inject_lock);
	return (ret);
}


int
zio_handle_device_injection(vdev_t *vd, int error)
{
	inject_handler_t *handler;
	int ret = 0;

	rw_enter(&inject_lock, RW_READER);

	for (handler = list_head(&inject_handlers); handler != NULL;
	    handler = list_next(&inject_handlers, handler)) {

		/* Ignore label specific faults */
		if (handler->zi_record.zi_start != 0)
			continue;

		if (vd->vdev_guid == handler->zi_record.zi_guid) {
			if (handler->zi_record.zi_error == error) {
				/*
				 * For a failed open, pretend like the device
				 * has gone away.
				 */
				if (error == ENXIO)
					vd->vdev_stat.vs_aux =
					    VDEV_AUX_OPEN_FAILED;
				ret = error;
				break;
			}
			if (handler->zi_record.zi_error == ENXIO) {
				ret = EIO;
				break;
			}
		}
	}

	rw_exit(&inject_lock);

	return (ret);
}

/*
 * Create a new handler for the given record.  We add it to the list, adding
 * a reference to the spa_t in the process.  We increment zio_injection_enabled,
 * which is the switch to trigger all fault injection.
 */
int
zio_inject_fault(char *name, int flags, int *id, zinject_record_t *record)
{
	inject_handler_t *handler;
	int error;
	spa_t *spa;

	/*
	 * If this is pool-wide metadata, make sure we unload the corresponding
	 * spa_t, so that the next attempt to load it will trigger the fault.
	 * We call spa_reset() to unload the pool appropriately.
	 */
	if (flags & ZINJECT_UNLOAD_SPA)
		if ((error = spa_reset(name)) != 0)
			return (error);

	if (!(flags & ZINJECT_NULL)) {
		/*
		 * spa_inject_ref() will add an injection reference, which will
		 * prevent the pool from being removed from the namespace while
		 * still allowing it to be unloaded.
		 */
		if ((spa = spa_inject_addref(name)) == NULL)
			return (ENOENT);

		handler = kmem_alloc(sizeof (inject_handler_t), KM_SLEEP);

		rw_enter(&inject_lock, RW_WRITER);

		*id = handler->zi_id = inject_next_id++;
		handler->zi_spa = spa;
		handler->zi_record = *record;
		list_insert_tail(&inject_handlers, handler);
		atomic_add_32(&zio_injection_enabled, 1);

		rw_exit(&inject_lock);
	}

	/*
	 * Flush the ARC, so that any attempts to read this data will end up
	 * going to the ZIO layer.  Note that this is a little overkill, but
	 * we don't have the necessary ARC interfaces to do anything else, and
	 * fault injection isn't a performance critical path.
	 */
	if (flags & ZINJECT_FLUSH_ARC)
		arc_flush(NULL);

	return (0);
}

/*
 * Returns the next record with an ID greater than that supplied to the
 * function.  Used to iterate over all handlers in the system.
 */
int
zio_inject_list_next(int *id, char *name, size_t buflen,
    zinject_record_t *record)
{
	inject_handler_t *handler;
	int ret;

	mutex_enter(&spa_namespace_lock);
	rw_enter(&inject_lock, RW_READER);

	for (handler = list_head(&inject_handlers); handler != NULL;
	    handler = list_next(&inject_handlers, handler))
		if (handler->zi_id > *id)
			break;

	if (handler) {
		*record = handler->zi_record;
		*id = handler->zi_id;
		(void) strncpy(name, spa_name(handler->zi_spa), buflen);
		ret = 0;
	} else {
		ret = ENOENT;
	}

	rw_exit(&inject_lock);
	mutex_exit(&spa_namespace_lock);

	return (ret);
}

/*
 * Clear the fault handler with the given identifier, or return ENOENT if none
 * exists.
 */
int
zio_clear_fault(int id)
{
	inject_handler_t *handler;
	int ret;

	rw_enter(&inject_lock, RW_WRITER);

	for (handler = list_head(&inject_handlers); handler != NULL;
	    handler = list_next(&inject_handlers, handler))
		if (handler->zi_id == id)
			break;

	if (handler == NULL) {
		ret = ENOENT;
	} else {
		list_remove(&inject_handlers, handler);
		spa_inject_delref(handler->zi_spa);
		kmem_free(handler, sizeof (inject_handler_t));
		atomic_add_32(&zio_injection_enabled, -1);
		ret = 0;
	}

	rw_exit(&inject_lock);

	return (ret);
}

void
zio_inject_init(void)
{
	rw_init(&inject_lock, NULL, RW_DEFAULT, NULL);
	list_create(&inject_handlers, sizeof (inject_handler_t),
	    offsetof(inject_handler_t, zi_link));
}

void
zio_inject_fini(void)
{
	list_destroy(&inject_handlers);
	rw_destroy(&inject_lock);
}
