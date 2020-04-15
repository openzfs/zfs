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
 * Copyright (c) 2010, Oracle and/or its affiliates. All rights reserved.
 * Copyright (c) 2012, 2014 by Delphix. All rights reserved.
 */

#include <sys/zfs_context.h>

typedef struct zfs_dbgmsg {
	list_node_t zdm_node;
	time_t zdm_timestamp;
	int zdm_size;
	char zdm_msg[1]; /* variable length allocation */
} zfs_dbgmsg_t;

list_t zfs_dbgmsgs;
int zfs_dbgmsg_size;
kmutex_t zfs_dbgmsgs_lock;
int zfs_dbgmsg_maxsize = 4<<20; /* 4MB */
kstat_t *zfs_dbgmsg_kstat;

int zfs_dbgmsg_enable = 1;

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
zfs_dbgmsg_purge(int max_size)
{
	zfs_dbgmsg_t *zdm;
	int size;

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

/*
 * Debug logging is enabled by default for production kernel builds.
 * The overhead for this is negligible and the logs can be valuable when
 * debugging.  For non-production user space builds all debugging except
 * logging is enabled since performance is no longer a concern.
 */
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
	zfs_dbgmsg_t *zdm;

	if (zfs_dbgmsg_kstat)
		kstat_delete(zfs_dbgmsg_kstat);

	while ((zdm = list_remove_head(&zfs_dbgmsgs)) != NULL) {
		int size = sizeof (zfs_dbgmsg_t) + strlen(zdm->zdm_msg);
		kmem_free(zdm, size);
		zfs_dbgmsg_size -= size;
	}
	mutex_destroy(&zfs_dbgmsgs_lock);
	ASSERT0(zfs_dbgmsg_size);
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
		__dprintf(B_FALSE, file, func, line, "error %d", err);
}

/*
 * Print these messages by running:
 * echo ::zfs_dbgmsg | mdb -k
 *
 * Monitor these messages by running:
 * dtrace -qn 'zfs-dbgmsg{printf("%s\n", stringof(arg0))}'
 *
 * When used with libzpool, monitor with:
 * dtrace -qn 'zfs$pid::zfs_dbgmsg:probe1{printf("%s\n", copyinstr(arg1))}'
 */

/*
 * MacOS X's dtrace doesn't handle the PROBEs, so
 * we have a utility function that we can watch with
 * sudo dtrace -qn '__zfs_dbgmsg:entry{printf("%s\n", stringof(arg0));}'
 */
noinline void
__zfs_dbgmsg(char *buf)
{
	int size = sizeof (zfs_dbgmsg_t) + strlen(buf);
	zfs_dbgmsg_t *zdm = kmem_zalloc(size, KM_SLEEP);
	zdm->zdm_size = size;
	zdm->zdm_timestamp = gethrestime_sec();
	strlcpy(zdm->zdm_msg, buf, size);

	mutex_enter(&zfs_dbgmsgs_lock);
	list_insert_tail(&zfs_dbgmsgs, zdm);
	zfs_dbgmsg_size += size;
	zfs_dbgmsg_purge(MAX(zfs_dbgmsg_maxsize, 0));
	mutex_exit(&zfs_dbgmsgs_lock);
}

void
__dprintf(boolean_t dprint, const char *file, const char *func,
    int line, const char *fmt, ...)
{
	int size, i;
	va_list adx;
	char *buf, *nl;
	char *prefix = (dprint) ? "dprintf: " : "";
	const char *newfile;

	/*
	 * Get rid of annoying prefix to filename.
	 */
	newfile = strrchr(file, '/');
	if (newfile != NULL) {
		newfile = newfile + 1; /* Get rid of leading / */
	} else {
		newfile = file;
	}

	va_start(adx, fmt);
	size = vsnprintf(NULL, 0, fmt, adx);
	va_end(adx);

	size += snprintf(NULL, 0, "%s%s:%d:%s(): ", prefix, newfile, line,
	    func);

	size++; /* null byte in the "buf" string */

	/*
	 * There is one byte of string in sizeof (zfs_dbgmsg_t), used
	 * for the terminating null.
	 */
	buf = kmem_alloc(size, KM_SLEEP);
	int roger = 0;

	va_start(adx, fmt);
	i = snprintf(buf, size + 1, "%s%s:%d:%s(): ",
	    prefix, newfile, line, func);
	roger = vsnprintf(buf + i, size -i + 1, fmt, adx);
	va_end(adx);

	/*
	 * Get rid of trailing newline for dprintf logs.
	 */
	if (dprint && buf[0] != '\0') {
		nl = &buf[strlen(buf) - 1];
		if (*nl == '\n')
			*nl = '\0';
	}

	DTRACE_PROBE1(zfs__dbgmsg, char *, zdm->zdm_msg);

	__zfs_dbgmsg(buf);

	/* Also emit string to log/console */
	printf("%s\n", buf);

	kmem_free(buf, size);
}

void
zfs_dbgmsg_print(const char *tag)
{
	zfs_dbgmsg_t *zdm;

	(void) printf("ZFS_DBGMSG(%s):\n", tag);
	mutex_enter(&zfs_dbgmsgs_lock);
	for (zdm = list_head(&zfs_dbgmsgs); zdm;
	    zdm = list_next(&zfs_dbgmsgs, zdm))
		(void) printf("%s\n", zdm->zdm_msg);
	mutex_exit(&zfs_dbgmsgs_lock);
}
