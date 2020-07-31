/*****************************************************************************\
 *  Copyright (C) 2007-2010 Lawrence Livermore National Security, LLC.
 *  Copyright (C) 2007 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Brian Behlendorf <behlendorf1@llnl.gov>.
 *  UCRL-CODE-235197
 *
 *  This file is part of the SPL, Solaris Porting Layer.
 *  For details, see <http://zfsonlinux.org/>.
 *
 *  The SPL is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the
 *  Free Software Foundation; either version 2 of the License, or (at your
 *  option) any later version.
 *
 *  The SPL is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with the SPL.  If not, see <http://www.gnu.org/licenses/>.
\*****************************************************************************/



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
 *
 * Copyright (C) 2017 Jorgen Lundman <lundman@lundman.net>
 *
 */

#ifndef _SPL_POLICY_H
#define _SPL_POLICY_H

#ifdef _KERNEL

#include <sys/vnode.h>
#include <sys/cred.h>

struct vattr;

int secpolicy_fs_unmount(cred_t *, struct mount *);
int secpolicy_nfs(const cred_t *);
int secpolicy_sys_config(const cred_t *, boolean_t);
int secpolicy_zfs(const cred_t *);
int secpolicy_zinject(const cred_t *);
//int secpolicy_vnode_setids_setgids(const cred_t *, gid_t);
//int secpolicy_vnode_setid_retain(const cred_t *, boolean_t);
//void secpolicy_setid_clear(struct vattr *, cred_t *);
int secpolicy_vnode_any_access(const cred_t *, struct vnode *, uid_t);
int secpolicy_vnode_access2(const cred_t *, struct vnode *, uid_t, mode_t, mode_t);
//int secpolicy_vnode_chown(const cred_t *, uid_t);
//int secpolicy_vnode_setdac(const cred_t *, uid_t);
//int secpolicy_vnode_remove(const cred_t *);
/*
 * This function to be called from xxfs_setattr().
 * Must be called with the node's attributes read-write locked.
 *
 *		cred_t *		- acting credentials
 *		struct vnode *		- vnode we're operating on
 *		struct vattr *va	- new attributes, va_mask may be
 *					  changed on return from a call
 *		struct vattr *oldva	- old attributes, need include owner
 *					  and mode only
 *		int flags		- setattr flags
 *		int iaccess(void *node, int mode, cred_t *cr)
 *					- non-locking internal access function
 *						mode be checked
 *						w/ VREAD|VWRITE|VEXEC, not fs
 *						internal mode encoding.
 *
 *		void *node		- internal node (inode, tmpnode) to
 *					  pass as arg to iaccess
 */
int secpolicy_vnode_setattr(cred_t *, struct vnode *, vattr_t *,
    const vattr_t *, int, int (void *, int, cred_t *), void *);

//int secpolicy_xvattr(xvattr_t *, uid_t, cred_t *, vtype_t);
int secpolicy_vnode_stky_modify(const cred_t *);
int	secpolicy_setid_setsticky_clear(struct vnode *vp, vattr_t *vap,
	    const vattr_t *ovap, cred_t *cr);
//int secpolicy_basic_link(const cred_t *);

int secpolicy_vnode_remove(struct vnode *, const cred_t *);
int secpolicy_vnode_create_gid(const cred_t *);
int secpolicy_vnode_setids_setgids(struct vnode *, const cred_t *, gid_t);
int secpolicy_vnode_setdac(struct vnode *, const cred_t *, uid_t);
int secpolicy_vnode_chown(struct vnode *, const cred_t *, uid_t);
int secpolicy_vnode_setid_retain(struct vnode *, const cred_t *, boolean_t);
int secpolicy_xvattr(struct vnode *, vattr_t *, uid_t, const cred_t *, enum vtype);
int secpolicy_setid_clear(vattr_t *, struct vnode *, const cred_t *);
int secpolicy_basic_link(struct vnode *, const cred_t *);
int secpolicy_fs_mount_clearopts(const cred_t *, struct mount *);
int secpolicy_fs_mount(const cred_t *, struct vnode *, struct mount *);

#endif	/* _KERNEL */

#endif /* SPL_POLICY_H */
