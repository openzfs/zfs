/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License Version 1.0 (CDDL-1.0).
 * You can obtain a copy of the license from the top-level file
 * "OPENSOLARIS.LICENSE" or at <http://opensource.org/licenses/CDDL-1.0>.
 * You may not use this file except in compliance with the license.
 *
 * CDDL HEADER END
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
