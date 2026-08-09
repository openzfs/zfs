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
 * Copyright (c) 2006, 2010, Oracle and/or its affiliates. All rights reserved.
 * Copyright (c) 2016 Actifio, Inc. All rights reserved.
 */

#ifndef	_SYS_ZVOL_H
#define	_SYS_ZVOL_H

#include <sys/zfs_context.h>

#define	ZVOL_OBJ		1ULL
#define	ZVOL_ZAP_OBJ		2ULL

#define	SPEC_MAXOFFSET_T	((1LL << ((NBBY * sizeof (daddr32_t)) + \
				DEV_BSHIFT - 1)) - 1)

extern void zvol_create_minors(const char *);
extern void zvol_remove_minors(spa_t *, const char *, boolean_t);
extern void zvol_rename_minors(spa_t *, const char *, const char *, boolean_t);

#ifdef _KERNEL
struct zvol_state;
typedef struct zvol_state zvol_state_handle_t;

extern int zvol_check_volsize(uint64_t, uint64_t);
extern int zvol_check_volblocksize(const char *, uint64_t);
extern int zvol_get_stats(objset_t *, nvlist_t *);
extern boolean_t zvol_is_zvol(const char *);
extern void zvol_create_cb(objset_t *, void *, cred_t *, dmu_tx_t *);
extern int zvol_set_volsize(const char *, uint64_t);
extern int zvol_set_volthreading(const char *, boolean_t);
extern int zvol_set_common(const char *, zfs_prop_t, zprop_source_t, uint64_t);
extern int zvol_set_ro(const char *, boolean_t);
extern int zvol_suspend(const char *, zvol_state_handle_t **);
extern int zvol_resume(zvol_state_handle_t *);
extern void *zvol_tag(zvol_state_handle_t *);

extern int zvol_init(void);
extern void zvol_fini(void);
extern int zvol_busy(void);

#endif /* _KERNEL */
#endif /* _SYS_ZVOL_H */
