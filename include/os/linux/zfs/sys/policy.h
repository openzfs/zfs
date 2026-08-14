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
 * Copyright (c) 2003, 2010, Oracle and/or its affiliates. All rights reserved.
 * Copyright 2015, Joyent, Inc. All rights reserved.
 * Copyright (c) 2016, Lawrence Livermore National Security, LLC.
 */

#ifndef _SYS_POLICY_H
#define	_SYS_POLICY_H

#ifdef _KERNEL

#include <sys/cred.h>
#include <sys/types.h>
#include <sys/xvattr.h>
#include <sys/zpl.h>

struct znode;

int secpolicy_sys_config(const cred_t *, boolean_t);
int secpolicy_vnode_access2(const cred_t *, struct inode *,
    uid_t, mode_t, mode_t);
int secpolicy_vnode_any_access(const cred_t *, struct inode *, uid_t);
int secpolicy_vnode_chown(const cred_t *, uid_t);
int secpolicy_vnode_create_gid(const cred_t *);
int secpolicy_vnode_remove(const cred_t *);
int secpolicy_vnode_setdac(const cred_t *, uid_t);
int secpolicy_vnode_setid_retain(struct znode *, const cred_t *, boolean_t);
int secpolicy_vnode_setids_setgids(const cred_t *, gid_t, zidmap_t *,
    struct user_namespace *);
int secpolicy_zinject(const cred_t *);
int secpolicy_zfs(const cred_t *);
void secpolicy_setid_clear(vattr_t *, cred_t *);
int secpolicy_setid_setsticky_clear(struct inode *, vattr_t *,
    const vattr_t *, cred_t *, zidmap_t *, struct user_namespace *);
int secpolicy_xvattr(xvattr_t *, uid_t, cred_t *, mode_t);
int secpolicy_vnode_setattr(cred_t *, struct inode *, struct vattr *,
    const struct vattr *, int, int (void *, int, cred_t *), void *);
int secpolicy_basic_link(const cred_t *);

#endif /* _KERNEL */
#endif /* _SYS_POLICY_H */
