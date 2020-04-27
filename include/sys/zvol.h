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

extern void zvol_create_minor(const char *);
extern void zvol_create_minors_recursive(const char *);
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
extern int zvol_set_volblocksize(const char *, uint64_t);
extern int zvol_set_snapdev(const char *, zprop_source_t, uint64_t);
extern int zvol_set_volmode(const char *, zprop_source_t, uint64_t);
extern zvol_state_handle_t *zvol_suspend(const char *);
extern int zvol_resume(zvol_state_handle_t *);
extern void *zvol_tag(zvol_state_handle_t *);

extern int zvol_init(void);
extern void zvol_fini(void);
extern int zvol_busy(void);

#endif /* _KERNEL */
#endif /* _SYS_ZVOL_H */
