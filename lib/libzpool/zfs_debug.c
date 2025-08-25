// SPDX-License-Identifier: CDDL-1.0
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
 * Copyright (c) 2012, 2014 by Delphix. All rights reserved.
 * Copyright (c) 2024, Rob Norris <robn@despairlabs.com>
 */

#include <sys/zfs_context.h>

typedef struct zfs_dbgmsg {
	list_node_t		zdm_node;
	uint64_t		zdm_timestamp;
	uint_t			zdm_size;
	char			zdm_msg[]; /* variable length allocation */
} zfs_dbgmsg_t;

static list_t zfs_dbgmsgs;
static kmutex_t zfs_dbgmsgs_lock;
static uint_t zfs_dbgmsg_size = 0;
static uint_t zfs_dbgmsg_maxsize = 4<<20; /* 4MB */

int zfs_dbgmsg_enable = B_TRUE;

static void
zfs_dbgmsg_purge(uint_t max_size)
{
	while (zfs_dbgmsg_size > max_size) {
		zfs_dbgmsg_t *zdm = list_remove_head(&zfs_dbgmsgs);
		if (zdm == NULL)
			return;

		uint_t size = zdm->zdm_size;
		kmem_free(zdm, size);
		zfs_dbgmsg_size -= size;
	}
}

void
zfs_dbgmsg_init(void)
{
	list_create(&zfs_dbgmsgs, sizeof (zfs_dbgmsg_t),
	    offsetof(zfs_dbgmsg_t, zdm_node));
	mutex_init(&zfs_dbgmsgs_lock, NULL, MUTEX_DEFAULT, NULL);
}

void
zfs_dbgmsg_fini(void)
{
	zfs_dbgmsg_t *zdm;
	while ((zdm = list_remove_head(&zfs_dbgmsgs)))
		umem_free(zdm, zdm->zdm_size);
	mutex_destroy(&zfs_dbgmsgs_lock);
}

void
__set_error(const char *file, const char *func, int line, int err)
{
	if (zfs_flags & ZFS_DEBUG_SET_ERROR)
		__dprintf(B_FALSE, file, func, line, "error %lu",
		    (ulong_t)err);
}

void
__zfs_dbgmsg(char *buf)
{
	uint_t size = sizeof (zfs_dbgmsg_t) + strlen(buf) + 1;
	zfs_dbgmsg_t *zdm = umem_zalloc(size, KM_SLEEP);
	zdm->zdm_size = size;
	zdm->zdm_timestamp = gethrestime_sec();
	strcpy(zdm->zdm_msg, buf);

	mutex_enter(&zfs_dbgmsgs_lock);
	list_insert_tail(&zfs_dbgmsgs, zdm);
	zfs_dbgmsg_size += size;
	zfs_dbgmsg_purge(zfs_dbgmsg_maxsize);
	mutex_exit(&zfs_dbgmsgs_lock);
}

void
zfs_dbgmsg_print(int fd, const char *tag)
{
	ssize_t ret __attribute__((unused));

	mutex_enter(&zfs_dbgmsgs_lock);

	/*
	 * We use write() in this function instead of printf()
	 * so it is safe to call from a signal handler.
	 */
	ret = write(fd, "ZFS_DBGMSG(", 11);
	ret = write(fd, tag, strlen(tag));
	ret = write(fd, ") START:\n", 9);

	for (zfs_dbgmsg_t *zdm = list_head(&zfs_dbgmsgs); zdm != NULL;
	    zdm = list_next(&zfs_dbgmsgs, zdm)) {
		ret = write(fd, zdm->zdm_msg, strlen(zdm->zdm_msg));
		ret = write(fd, "\n", 1);
	}

	ret = write(fd, "ZFS_DBGMSG(", 11);
	ret = write(fd, tag, strlen(tag));
	ret = write(fd, ") END\n", 6);

	mutex_exit(&zfs_dbgmsgs_lock);
}
