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
 * Copyright (c) 2013 by Delphix. All rights reserved.
 */

#include <sys/zfs_context.h>

#if !defined(_KERNEL) || !defined(__linux__)
list_t zfs_dbgmsgs;
int zfs_dbgmsg_size;
kmutex_t zfs_dbgmsgs_lock;
int zfs_dbgmsg_maxsize = 4<<20; /* 4MB */
#endif

/*
 * Enable various debugging features.
 */
int zfs_flags = 0;

/*
 * zfs_recover can be set to nonzero to attempt to recover from
 * otherwise-fatal errors, typically caused by on-disk corruption.  When
 * set, calls to zfs_panic_recover() will turn into warning messages.
 * This should only be used as a last resort, as it typically results
 * in leaked space, or worse.
 */
int zfs_recover = B_FALSE;

/*
 * If destroy encounters an EIO while reading metadata (e.g. indirect
 * blocks), space referenced by the missing metadata can not be freed.
 * Normally this causes the background destroy to become "stalled", as
 * it is unable to make forward progress.  While in this stalled state,
 * all remaining space to free from the error-encountering filesystem is
 * "temporarily leaked".  Set this flag to cause it to ignore the EIO,
 * permanently leak the space from indirect blocks that can not be read,
 * and continue to free everything else that it can.
 *
 * The default, "stalling" behavior is useful if the storage partially
 * fails (i.e. some but not all i/os fail), and then later recovers.  In
 * this case, we will be able to continue pool operations while it is
 * partially failed, and when it recovers, we can continue to free the
 * space, with no leaks.  However, note that this case is actually
 * fairly rare.
 *
 * Typically pools either (a) fail completely (but perhaps temporarily,
 * e.g. a top-level vdev going offline), or (b) have localized,
 * permanent errors (e.g. disk returns the wrong data due to bit flip or
 * firmware bug).  In case (a), this setting does not matter because the
 * pool will be suspended and the sync thread will not be able to make
 * forward progress regardless.  In case (b), because the error is
 * permanent, the best we can do is leak the minimum amount of space,
 * which is what setting this flag will do.  Therefore, it is reasonable
 * for this flag to normally be set, but we chose the more conservative
 * approach of not setting it, so that there is no possibility of
 * leaking space in the "partial temporary" failure case.
 */
int zfs_free_leak_on_eio = B_FALSE;


void
zfs_panic_recover(const char *fmt, ...)
{
	va_list adx;

	va_start(adx, fmt);
	vcmn_err(zfs_recover ? CE_WARN : CE_PANIC, fmt, adx);
	va_end(adx);
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
#if !defined(_KERNEL) || !defined(__linux__)
	list_create(&zfs_dbgmsgs, sizeof (zfs_dbgmsg_t),
	    offsetof(zfs_dbgmsg_t, zdm_node));
	mutex_init(&zfs_dbgmsgs_lock, NULL, MUTEX_DEFAULT, NULL);
#endif

	if (zfs_flags == 0) {
#if defined(_KERNEL)
		zfs_flags = ZFS_DEBUG_DPRINTF;
		spl_debug_set_mask(spl_debug_get_mask() | SD_DPRINTF);
		spl_debug_set_subsys(spl_debug_get_subsys() | SS_USER1);
#else
		zfs_flags = ~ZFS_DEBUG_DPRINTF;
#endif /* _KERNEL */
	}
}

void
zfs_dbgmsg_fini(void)
{
#if !defined(_KERNEL) || !defined(__linux__)
	zfs_dbgmsg_t *zdm;

	while ((zdm = list_remove_head(&zfs_dbgmsgs)) != NULL) {
		int size = sizeof (zfs_dbgmsg_t) + strlen(zdm->zdm_msg);
		kmem_free(zdm, size);
		zfs_dbgmsg_size -= size;
	}
	mutex_destroy(&zfs_dbgmsgs_lock);
	ASSERT0(zfs_dbgmsg_size);
#endif
}

#if !defined(_KERNEL) || !defined(__linux__)
/*
 * Print these messages by running:
 * echo ::zfs_dbgmsg | mdb -k
 *
 * Monitor these messages by running:
 * 	dtrace -q -n 'zfs-dbgmsg{printf("%s\n", stringof(arg0))}'
 */
void
zfs_dbgmsg(const char *fmt, ...)
{
	int size;
	va_list adx;
	zfs_dbgmsg_t *zdm;

	va_start(adx, fmt);
	size = vsnprintf(NULL, 0, fmt, adx);
	va_end(adx);

	/*
	 * There is one byte of string in sizeof (zfs_dbgmsg_t), used
	 * for the terminating null.
	 */
	zdm = kmem_alloc(sizeof (zfs_dbgmsg_t) + size, KM_SLEEP);
	zdm->zdm_timestamp = gethrestime_sec();

	va_start(adx, fmt);
	(void) vsnprintf(zdm->zdm_msg, size + 1, fmt, adx);
	va_end(adx);

	DTRACE_PROBE1(zfs__dbgmsg, char *, zdm->zdm_msg);

	mutex_enter(&zfs_dbgmsgs_lock);
	list_insert_tail(&zfs_dbgmsgs, zdm);
	zfs_dbgmsg_size += sizeof (zfs_dbgmsg_t) + size;
	while (zfs_dbgmsg_size > zfs_dbgmsg_maxsize) {
		zdm = list_remove_head(&zfs_dbgmsgs);
		size = sizeof (zfs_dbgmsg_t) + strlen(zdm->zdm_msg);
		kmem_free(zdm, size);
		zfs_dbgmsg_size -= size;
	}
	mutex_exit(&zfs_dbgmsgs_lock);
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
#endif

#if defined(_KERNEL)
module_param(zfs_flags, int, 0644);
MODULE_PARM_DESC(zfs_flags, "Set additional debugging flags");

module_param(zfs_recover, int, 0644);
MODULE_PARM_DESC(zfs_recover, "Set to attempt to recover from fatal errors");

module_param(zfs_free_leak_on_eio, int, 0644);
MODULE_PARM_DESC(zfs_free_leak_on_eio,
	"Set to ignore IO errors during free and permanently leak the space");
#endif /* _KERNEL */
