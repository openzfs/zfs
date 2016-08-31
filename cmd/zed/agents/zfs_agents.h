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
 * Agents from ZFS FMA and syseventd - linked directly into ZED daemon binary
 */

/*
 * ZFS Sysevent Linkable Module (SLM)
 */
extern int zfs_slm_init(libzfs_handle_t *zfs_hdl);
extern void zfs_slm_fini(void);
extern void zfs_slm_event(const char *, const char *, nvlist_t *);

/*
 * ZFS FMA Retire Agent
 */
extern int zfs_retire_init(libzfs_handle_t *zfs_hdl);
extern void zfs_retire_fini(void);
extern void zfs_retire_recv(nvlist_t *nvl, const char *class);

/*
 * ZFS FMA Diagnosis Engine
 */
extern int zfs_diagnosis_init(libzfs_handle_t *zfs_hdl);
extern void zfs_diagnosis_fini(void);
extern void zfs_diagnosis_recv(nvlist_t *nvl, const char *class);

#ifdef	__cplusplus
}
#endif

#endif	/* !ZFS_AGENTS_H */
