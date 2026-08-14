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
 * Copyright (c) 2005, 2010, Oracle and/or its affiliates. All rights reserved.
 * Copyright 2011 Nexenta Systems, Inc.  All rights reserved.
 */

#ifndef	_ACL_COMMON_H
#define	_ACL_COMMON_H

#include <sys/types.h>
#include <sys/acl.h>
#include <sys/stat.h>

#ifdef	__cplusplus
extern "C" {
#endif

typedef struct trivial_acl {
	uint32_t	allow0;		/* allow mask for bits only in owner */
	uint32_t	deny1;		/* deny mask for bits not in owner */
	uint32_t	deny2;		/* deny mask for bits not in group */
	uint32_t	owner;		/* allow mask matching mode */
	uint32_t	group;		/* allow mask matching mode */
	uint32_t	everyone;	/* allow mask matching mode */
} trivial_acl_t;

extern int acltrivial(const char *);
extern void adjust_ace_pair(ace_t *pair, mode_t mode);
extern void adjust_ace_pair_common(void *, size_t, size_t, mode_t);
extern int ace_trivial_common(void *, int,
    uintptr_t (*walk)(void *, uintptr_t, int aclcnt, uint16_t *, uint16_t *,
    uint32_t *mask));
#if !defined(_KERNEL)
extern acl_t *acl_alloc(acl_type_t);
extern void acl_free(acl_t *aclp);
extern int acl_translate(acl_t *aclp, int target_flavor, boolean_t isdir,
    uid_t owner, gid_t group);
#endif	/* !_KERNEL */
int cmp2acls(void *a, void *b);
int acl_trivial_create(mode_t mode, boolean_t isdir, ace_t **acl, int *count);
void acl_trivial_access_masks(mode_t mode, boolean_t isdir,
    trivial_acl_t *masks);

#ifdef	__cplusplus
}
#endif

#endif /* _ACL_COMMON_H */
