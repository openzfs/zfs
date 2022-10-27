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
 * Copyright (c) 2010, Oracle and/or its affiliates. All rights reserved.
 * Copyright (c) 2013, 2020 by Delphix. All rights reserved.
 */

#include <sys/types.h>
#include <sys/param.h>
#include <sys/errno.h>
#include <sys/kmem.h>
#include <sys/sunddi.h>
#include <sys/zfs_ioctl.h>
#include <sys/zfs_onexit.h>
#include <sys/zvol.h>

/*
 * ZFS kernel routines may add/delete callback routines to be invoked
 * upon process exit (triggered via the close operation from the /dev/zfs
 * driver).
 *
 * These cleanup callbacks are intended to allow for the accumulation
 * of kernel state across multiple ioctls.  User processes participate
 * simply by opening ZFS_DEV. This causes the ZFS driver to do create
 * some private data for the file descriptor and generating a unique
 * minor number. The process then passes along that file descriptor to
 * each ioctl that might have a cleanup operation.
 *
 * Consumers of the onexit routines should call zfs_onexit_fd_hold() early
 * on to validate the given fd and add a reference to its file table entry.
 * This allows the consumer to do its work and then add a callback, knowing
 * that zfs_onexit_add_cb() won't fail with EBADF.  When finished, consumers
 * should call zfs_onexit_fd_rele().
 *
 * A simple example is zfs_ioc_recv(), where we might create an AVL tree
 * with dataset/GUID mappings and then reuse that tree on subsequent
 * zfs_ioc_recv() calls.
 *
 * On the first zfs_ioc_recv() call, dmu_recv_stream() will kmem_alloc()
 * the AVL tree and pass it along with a callback function to
 * zfs_onexit_add_cb(). The zfs_onexit_add_cb() routine will register the
 * callback and return an action handle.
 *
 * The action handle is then passed from user space to subsequent
 * zfs_ioc_recv() calls, so that dmu_recv_stream() can fetch its AVL tree
 * by calling zfs_onexit_cb_data() with the device minor number and
 * action handle.
 *
 * If the user process exits abnormally, the callback is invoked implicitly
 * as part of the driver close operation.  Once the user space process is
 * finished with the accumulated kernel state, it can also just call close(2)
 * on the cleanup fd to trigger the cleanup callback.
 */

void
zfs_onexit_init(zfs_onexit_t **zop)
{
	zfs_onexit_t *zo;

	zo = *zop = kmem_zalloc(sizeof (zfs_onexit_t), KM_SLEEP);
	mutex_init(&zo->zo_lock, NULL, MUTEX_DEFAULT, NULL);
	list_create(&zo->zo_actions, sizeof (zfs_onexit_action_node_t),
	    offsetof(zfs_onexit_action_node_t, za_link));
}

void
zfs_onexit_destroy(zfs_onexit_t *zo)
{
	zfs_onexit_action_node_t *ap;

	mutex_enter(&zo->zo_lock);
	while ((ap = list_head(&zo->zo_actions)) != NULL) {
		list_remove(&zo->zo_actions, ap);
		mutex_exit(&zo->zo_lock);
		ap->za_func(ap->za_data);
		kmem_free(ap, sizeof (zfs_onexit_action_node_t));
		mutex_enter(&zo->zo_lock);
	}
	mutex_exit(&zo->zo_lock);

	list_destroy(&zo->zo_actions);
	mutex_destroy(&zo->zo_lock);
	kmem_free(zo, sizeof (zfs_onexit_t));
}

/*
 * Consumers might need to operate by minor number instead of fd, since
 * they might be running in another thread (e.g. txg_sync_thread). Callers
 * of this function must call zfs_onexit_fd_rele() when they're finished
 * using the minor number.
 */
zfs_file_t *
zfs_onexit_fd_hold(int fd, minor_t *minorp)
{
	zfs_onexit_t *zo = NULL;

	zfs_file_t *fp = zfs_file_get(fd);
	if (fp == NULL)
		return (NULL);

	int error = zfsdev_getminor(fp, minorp);
	if (error) {
		zfs_onexit_fd_rele(fp);
		return (NULL);
	}

	zo = zfsdev_get_state(*minorp, ZST_ONEXIT);
	if (zo == NULL) {
		zfs_onexit_fd_rele(fp);
		return (NULL);
	}
	return (fp);
}

void
zfs_onexit_fd_rele(zfs_file_t *fp)
{
	zfs_file_put(fp);
}

static int
zfs_onexit_minor_to_state(minor_t minor, zfs_onexit_t **zo)
{
	*zo = zfsdev_get_state(minor, ZST_ONEXIT);
	if (*zo == NULL)
		return (SET_ERROR(EBADF));

	return (0);
}

/*
 * Add a callback to be invoked when the calling process exits.
 */
int
zfs_onexit_add_cb(minor_t minor, void (*func)(void *), void *data,
    uintptr_t *action_handle)
{
	zfs_onexit_t *zo;
	zfs_onexit_action_node_t *ap;
	int error;

	error = zfs_onexit_minor_to_state(minor, &zo);
	if (error)
		return (error);

	ap = kmem_alloc(sizeof (zfs_onexit_action_node_t), KM_SLEEP);
	list_link_init(&ap->za_link);
	ap->za_func = func;
	ap->za_data = data;

	mutex_enter(&zo->zo_lock);
	list_insert_tail(&zo->zo_actions, ap);
	mutex_exit(&zo->zo_lock);
	if (action_handle)
		*action_handle = (uintptr_t)ap;

	return (0);
}
