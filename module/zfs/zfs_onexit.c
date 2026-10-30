// SPDX-License-Identifier: CDDL-1.0
/*
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this
 * source.  A copy of the CDDL is also available via the Internet at
 * https://opensource.org/license/CDDL-1.0.
 */
/*
 * Copyright (c) 2010, Oracle and/or its affiliates. All rights reserved.
 * Copyright (c) 2013, 2020 by Delphix. All rights reserved.
 */

#include <sys/zfs_onexit.h>

/*
 * The per-open /dev/zfs state owns cleanup callbacks.  Temporary snapshot holds
 * use these callbacks to release their on-disk holds on final close or device
 * teardown.
 *
 * Ioctl callers retain a native state reference while synchronous DSL tasks
 * register callbacks.  Multiple pool sync threads may register callbacks on
 * the same cleanup file.
 */

void
zfs_onexit_init(zfs_onexit_t *zo)
{
	mutex_init(&zo->zo_lock, NULL, MUTEX_DEFAULT, NULL);
	list_create(&zo->zo_actions, sizeof (zfs_onexit_action_node_t),
	    offsetof(zfs_onexit_action_node_t, za_link));
}

void
zfs_onexit_fini(zfs_onexit_t *zo)
{
	zfs_onexit_action_node_t *ap;

	/* The owner has drained all state users before finalization. */
	while ((ap = list_remove_head(&zo->zo_actions)) != NULL) {
		ap->za_func(ap->za_data);
		kmem_free(ap, sizeof (zfs_onexit_action_node_t));
	}

	list_destroy(&zo->zo_actions);
	mutex_destroy(&zo->zo_lock);
}

/*
 * Register a callback on a held cleanup state.
 */
void
zfs_onexit_add_cb(zfs_onexit_t *zo, void (*func)(void *), void *data)
{
	zfs_onexit_action_node_t *ap;

	ap = kmem_alloc(sizeof (zfs_onexit_action_node_t), KM_SLEEP);
	list_link_init(&ap->za_link);
	ap->za_func = func;
	ap->za_data = data;

	mutex_enter(&zo->zo_lock);
	list_insert_tail(&zo->zo_actions, ap);
	mutex_exit(&zo->zo_lock);
}
