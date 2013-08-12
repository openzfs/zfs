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
 * Copyright (c) 2012 by Delphix. All rights reserved.
 */

#include <sys/zfs_context.h>

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
	return;
}


#if defined(_KERNEL)
module_param(zfs_flags, int, 0644);
MODULE_PARM_DESC(zfs_flags, "Set additional debugging flags");

module_param(zfs_recover, int, 0644);
MODULE_PARM_DESC(zfs_recover, "Set to attempt to recover from fatal errors");
#endif /* _KERNEL */
