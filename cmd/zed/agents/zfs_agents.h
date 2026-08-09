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
 * Copyright (c) 2016, Intel Corporation.
 */

#ifndef	ZFS_AGENTS_H
#define	ZFS_AGENTS_H

#include <libzfs.h>
#include <libnvpair.h>


#ifdef	__cplusplus
extern "C" {
#endif

/*
 * Agent abstraction presented to ZED
 */
extern void zfs_agent_init(libzfs_handle_t *);
extern void zfs_agent_fini(void);
extern void zfs_agent_post_event(const char *, const char *, nvlist_t *);

/*
 * ZFS Sysevent Linkable Module (SLM)
 */
extern int zfs_slm_init(void);
extern void zfs_slm_fini(void);
extern void zfs_slm_event(const char *, const char *, nvlist_t *);

#ifdef	__cplusplus
}
#endif

#endif	/* !ZFS_AGENTS_H */
