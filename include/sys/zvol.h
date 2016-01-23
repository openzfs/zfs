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
 */

#ifndef	_SYS_ZVOL_H
#define	_SYS_ZVOL_H

#include <sys/zfs_context.h>

#define	ZVOL_OBJ		1ULL
#define	ZVOL_ZAP_OBJ		2ULL

typedef enum {
	ZVOL_ASYNC_CREATE_TASKQ,
	ZVOL_ASYNC_REMOVE_TASKQ,
	ZVOL_ASYNC_CREATE_MINORS,
	ZVOL_ASYNC_REMOVE_MINORS,
	ZVOL_ASYNC_REMOVE_MINOR,
	ZVOL_ASYNC_RENAME_MINORS,
	ZVOL_ASYNC_SET_SNAPDEV,
	ZVOL_ASYNC_MAX
} zvol_async_op_t;

typedef struct {
	zvol_async_op_t op;
	char pool[MAXNAMELEN];
	char name1[MAXNAMELEN];
	char name2[MAXNAMELEN];
	uint64_t value;
} zvol_async_arg_t;

#ifdef _KERNEL

extern int zvol_check_volsize(uint64_t volsize, uint64_t blocksize);
extern int zvol_check_volblocksize(const char *name, uint64_t volblocksize);
extern int zvol_get_stats(objset_t *os, nvlist_t *nv);
extern boolean_t zvol_is_zvol(const char *);
extern void zvol_create_cb(objset_t *os, void *arg, cred_t *cr, dmu_tx_t *tx);
extern void zvol_async_create_taskq(const char *pool);
extern void zvol_async_remove_taskq(const char *pool);
extern void zvol_async_create_minors(const char *name);
extern void zvol_async_remove_minor(const char *name);
extern void zvol_async_remove_minors(const char *name);
extern void zvol_async_rename_minors(const char *oldname, const char *newname);
extern int zvol_async_set_snapdev(const char *, uint64_t);
extern int zvol_set_volsize(const char *, uint64_t);
extern int zvol_set_volblocksize(const char *, uint64_t);

extern int zvol_init(void);
extern void zvol_fini(void);

#endif /* _KERNEL */
#endif /* _SYS_ZVOL_H */
