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
 * Copyright (C) 2011 Lawrence Livermore National Security, LLC.
 */

#ifndef _ZFS_XATTR_H
#define	_ZFS_XATTR_H

#include <linux/posix_acl_xattr.h>

/*
 * 2.6.35 API change,
 * The const keyword was added to the 'struct xattr_handler' in the
 * generic Linux super_block structure.  To handle this we define an
 * appropriate xattr_handler_t typedef which can be used.  This was
 * the preferred solution because it keeps the code clean and readable.
 */
#ifdef HAVE_CONST_XATTR_HANDLER
typedef const struct xattr_handler	xattr_handler_t;
#else
typedef struct xattr_handler		xattr_handler_t;
#endif

/*
 * 2.6.33 API change,
 * The xattr_hander->get() callback was changed to take a dentry
 * instead of an inode, and a handler_flags argument was added.
 */
#ifdef HAVE_DENTRY_XATTR_GET
#define	ZPL_XATTR_GET_WRAPPER(fn)					\
static int								\
fn(struct dentry *dentry, const char *name, void *buffer, size_t size,	\
    int unused_handler_flags)						\
{									\
	return (__ ## fn(dentry->d_inode, name, buffer, size));		\
}
#else
#define	ZPL_XATTR_GET_WRAPPER(fn)					\
static int								\
fn(struct inode *ip, const char *name, void *buffer, size_t size)	\
{									\
	return (__ ## fn(ip, name, buffer, size));			\
}
#endif /* HAVE_DENTRY_XATTR_GET */

/*
 * 2.6.33 API change,
 * The xattr_hander->set() callback was changed to take a dentry
 * instead of an inode, and a handler_flags argument was added.
 */
#ifdef HAVE_DENTRY_XATTR_SET
#define	ZPL_XATTR_SET_WRAPPER(fn)					\
static int								\
fn(struct dentry *dentry, const char *name, const void *buffer,		\
    size_t size, int flags, int unused_handler_flags)			\
{									\
	return (__ ## fn(dentry->d_inode, name, buffer, size, flags));	\
}
#else
#define	ZPL_XATTR_SET_WRAPPER(fn)					\
static int								\
fn(struct inode *ip, const char *name, const void *buffer,		\
    size_t size, int flags)						\
{									\
	return (__ ## fn(ip, name, buffer, size, flags));		\
}
#endif /* HAVE_DENTRY_XATTR_SET */

#ifdef HAVE_6ARGS_SECURITY_INODE_INIT_SECURITY
#define	zpl_security_inode_init_security(ip, dip, qstr, nm, val, len)	\
	security_inode_init_security(ip, dip, qstr, nm, val, len)
#else
#define	zpl_security_inode_init_security(ip, dip, qstr, nm, val, len)	\
	security_inode_init_security(ip, dip, nm, val, len)
#endif /* HAVE_6ARGS_SECURITY_INODE_INIT_SECURITY */

/*
 * Linux 3.7 API change. posix_acl_{from,to}_xattr gained the user_ns
 * parameter.  For the HAVE_POSIX_ACL_FROM_XATTR_USERNS version the
 * userns _may_ not be correct because it's used outside the RCU.
 */
#ifdef HAVE_POSIX_ACL_FROM_XATTR_USERNS
static inline struct posix_acl *
zpl_acl_from_xattr(const void *value, int size)
{
	return (posix_acl_from_xattr(CRED()->user_ns, value, size));
}

static inline int
zpl_acl_to_xattr(struct posix_acl *acl, void *value, int size)
{
	return (posix_acl_to_xattr(CRED()->user_ns, acl, value, size));
}

#else

static inline struct posix_acl *
zpl_acl_from_xattr(const void *value, int size)
{
	return (posix_acl_from_xattr(value, size));
}

static inline int
zpl_acl_to_xattr(struct posix_acl *acl, void *value, int size)
{
	return (posix_acl_to_xattr(acl, value, size));
}
#endif /* HAVE_POSIX_ACL_FROM_XATTR_USERNS */

#endif /* _ZFS_XATTR_H */
