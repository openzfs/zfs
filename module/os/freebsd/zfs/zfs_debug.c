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
 */

#include <sys/zfs_context.h>
#include <sys/kstat.h>

typedef struct zfs_dbgmsg {
	list_node_t zdm_node;
	time_t zdm_timestamp;
	uint_t zdm_size;
	char zdm_msg[];
} zfs_dbgmsg_t;

static list_t zfs_dbgmsgs;
static uint_t zfs_dbgmsg_size = 0;
static kmutex_t zfs_dbgmsgs_lock;
uint_t zfs_dbgmsg_maxsize = 4<<20; /* 4MB */
static kstat_t *zfs_dbgmsg_kstat;

/*
 * Internal ZFS debug messages are enabled by default.
 *
 * # Print debug messages as they're logged
 * dtrace -n 'zfs-dbgmsg { print(stringof(arg0)); }'
 *
 * # Print all logged dbgmsg entries
 * sysctl kstat.zfs.misc.dbgmsg
 *
 * # Disable the kernel debug message log.
 * sysctl vfs.zfs.dbgmsg_enable=0
 */
int zfs_dbgmsg_enable = B_TRUE;

static int
zfs_dbgmsg_headers(char *buf, size_t size)
{
	(void) snprintf(buf, size, "%-12s %-8s\n", "timestamp", "message");

	return (0);
}

static int
zfs_dbgmsg_data(char *buf, size_t size, void *data)
{
	zfs_dbgmsg_t *zdm = (zfs_dbgmsg_t *)data;

	(void) snprintf(buf, size, "%-12llu %-s\n",
	    (u_longlong_t)zdm->zdm_timestamp, zdm->zdm_msg);

	return (0);
}

static void *
zfs_dbgmsg_addr(kstat_t *ksp, loff_t n)
{
	zfs_dbgmsg_t *zdm = (zfs_dbgmsg_t *)ksp->ks_private;

	ASSERT(MUTEX_HELD(&zfs_dbgmsgs_lock));

	if (n == 0)
		ksp->ks_private = list_head(&zfs_dbgmsgs);
	else if (zdm)
		ksp->ks_private = list_next(&zfs_dbgmsgs, zdm);

	return (ksp->ks_private);
}

static void
zfs_dbgmsg_purge(uint_t max_size)
{
	zfs_dbgmsg_t *zdm;
	uint_t size;

	ASSERT(MUTEX_HELD(&zfs_dbgmsgs_lock));

	while (zfs_dbgmsg_size > max_size) {
		zdm = list_remove_head(&zfs_dbgmsgs);
		if (zdm == NULL)
			return;

		size = zdm->zdm_size;
		kmem_free(zdm, size);
		zfs_dbgmsg_size -= size;
	}
}

static int
zfs_dbgmsg_update(kstat_t *ksp, int rw)
{
	if (rw == KSTAT_WRITE)
		zfs_dbgmsg_purge(0);

	return (0);
}

void
zfs_dbgmsg_init(void)
{
	list_create(&zfs_dbgmsgs, sizeof (zfs_dbgmsg_t),
	    offsetof(zfs_dbgmsg_t, zdm_node));
	mutex_init(&zfs_dbgmsgs_lock, NULL, MUTEX_DEFAULT, NULL);

	zfs_dbgmsg_kstat = kstat_create("zfs", 0, "dbgmsg", "misc",
	    KSTAT_TYPE_RAW, 0, KSTAT_FLAG_VIRTUAL);
	if (zfs_dbgmsg_kstat) {
		zfs_dbgmsg_kstat->ks_lock = &zfs_dbgmsgs_lock;
		zfs_dbgmsg_kstat->ks_ndata = UINT32_MAX;
		zfs_dbgmsg_kstat->ks_private = NULL;
		zfs_dbgmsg_kstat->ks_update = zfs_dbgmsg_update;
		kstat_set_raw_ops(zfs_dbgmsg_kstat, zfs_dbgmsg_headers,
		    zfs_dbgmsg_data, zfs_dbgmsg_addr);
		kstat_install(zfs_dbgmsg_kstat);
	}
}

void
zfs_dbgmsg_fini(void)
{
	if (zfs_dbgmsg_kstat)
		kstat_delete(zfs_dbgmsg_kstat);

	mutex_enter(&zfs_dbgmsgs_lock);
	zfs_dbgmsg_purge(0);
	mutex_exit(&zfs_dbgmsgs_lock);
	mutex_destroy(&zfs_dbgmsgs_lock);
}

void
__zfs_dbgmsg(char *buf)
{
	zfs_dbgmsg_t *zdm;
	uint_t size;

	DTRACE_PROBE1(zfs__dbgmsg, char *, buf);

	size = sizeof (zfs_dbgmsg_t) + strlen(buf) + 1;
	zdm = kmem_zalloc(size, KM_SLEEP);
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
__set_error(const char *file, const char *func, int line, int err)
{
	/*
	 * To enable this:
	 *
	 * $ echo 512 >/sys/module/zfs/parameters/zfs_flags
	 */
	if (zfs_flags & ZFS_DEBUG_SET_ERROR)
		__dprintf(B_FALSE, file, func, line, "error %lu", (ulong_t)err);
}

void
__dprintf(boolean_t dprint, const char *file, const char *func,
    int line, const char *fmt, ...)
{
	const char *newfile;
	va_list adx;
	size_t size;
	char *buf;
	char *nl;
	int i;

	size = 1024;
	buf = kmem_alloc(size, KM_SLEEP);

	/*
	 * Get rid of annoying prefix to filename.
	 */
	newfile = strrchr(file, '/');
	if (newfile != NULL) {
		newfile = newfile + 1; /* Get rid of leading / */
	} else {
		newfile = file;
	}

	i = snprintf(buf, size, "%s:%d:%s(): ", newfile, line, func);

	if (i < size) {
		va_start(adx, fmt);
		(void) vsnprintf(buf + i, size - i, fmt, adx);
		va_end(adx);
	}

	/*
	 * Get rid of trailing newline for dprintf logs.
	 */
	if (dprint && buf[0] != '\0') {
		nl = &buf[strlen(buf) - 1];
		if (*nl == '\n')
			*nl = '\0';
	}

	/*
	 * To get this data:
	 *
	 * $ sysctl -n kstat.zfs.misc.dbgmsg
	 */
	__zfs_dbgmsg(buf);

	kmem_free(buf, size);
}

ZFS_MODULE_PARAM(zfs, zfs_, dbgmsg_enable, INT, ZMOD_RW,
	"Enable ZFS debug message log");

ZFS_MODULE_PARAM(zfs, zfs_, dbgmsg_maxsize, UINT, ZMOD_RW,
	"Maximum ZFS debug log size");
