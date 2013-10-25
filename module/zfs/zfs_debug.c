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
int zfs_dbgmsg_maxsize = 1<<20; /* 1MB */
#endif

/*
 * Enable various debugging features.
 */
int zfs_flags = 0;

/*
 * zfs_recover can be set to nonzero to attempt to recover from
 * otherwise-fatal errors, typically caused by on-disk corruption.  When
 * set, calls to zfs_panic_recover() will turn into warning messages.
 */
int zfs_recover = 0;


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
	return;
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
#endif /* _KERNEL */
