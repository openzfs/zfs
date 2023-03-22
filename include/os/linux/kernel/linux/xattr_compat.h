/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or https://opensource.org/licenses/CDDL-1.0.
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
typedef const struct xattr_handler	xattr_handler_t;

/*
 * 4.5 API change,
 */
#if defined(HAVE_XATTR_LIST_SIMPLE)
#define	ZPL_XATTR_LIST_WRAPPER(fn)					\
static bool								\
fn(struct dentry *dentry)						\
{									\
	return (!!__ ## fn(dentry->d_inode, NULL, 0, NULL, 0));		\
}
/*
 * 4.4 API change,
 */
#elif defined(HAVE_XATTR_LIST_DENTRY)
#define	ZPL_XATTR_LIST_WRAPPER(fn)					\
static size_t								\
fn(struct dentry *dentry, char *list, size_t list_size,			\
    const char *name, size_t name_len, int type)			\
{									\
	return (__ ## fn(dentry->d_inode,				\
	    list, list_size, name, name_len));				\
}
/*
 * 2.6.33 API change,
 */
#elif defined(HAVE_XATTR_LIST_HANDLER)
#define	ZPL_XATTR_LIST_WRAPPER(fn)					\
static size_t								\
fn(const struct xattr_handler *handler, struct dentry *dentry,		\
    char *list, size_t list_size, const char *name, size_t name_len)	\
{									\
	return (__ ## fn(dentry->d_inode,				\
	    list, list_size, name, name_len));				\
}
#else
#error "Unsupported kernel"
#endif

/*
 * 4.7 API change,
 * The xattr_handler->get() callback was changed to take a both dentry and
 * inode, because the dentry might not be attached to an inode yet.
 */
#if defined(HAVE_XATTR_GET_DENTRY_INODE)
#define	ZPL_XATTR_GET_WRAPPER(fn)					\
static int								\
fn(const struct xattr_handler *handler, struct dentry *dentry,		\
    struct inode *inode, const char *name, void *buffer, size_t size)	\
{									\
	return (__ ## fn(inode, name, buffer, size));			\
}
/*
 * 4.4 API change,
 * The xattr_handler->get() callback was changed to take a xattr_handler,
 * and handler_flags argument was removed and should be accessed by
 * handler->flags.
 */
#elif defined(HAVE_XATTR_GET_HANDLER)
#define	ZPL_XATTR_GET_WRAPPER(fn)					\
static int								\
fn(const struct xattr_handler *handler, struct dentry *dentry,		\
    const char *name, void *buffer, size_t size)			\
{									\
	return (__ ## fn(dentry->d_inode, name, buffer, size));		\
}
/*
 * 2.6.33 API change,
 * The xattr_handler->get() callback was changed to take a dentry
 * instead of an inode, and a handler_flags argument was added.
 */
#elif defined(HAVE_XATTR_GET_DENTRY)
#define	ZPL_XATTR_GET_WRAPPER(fn)					\
static int								\
fn(struct dentry *dentry, const char *name, void *buffer, size_t size,	\
    int unused_handler_flags)						\
{									\
	return (__ ## fn(dentry->d_inode, name, buffer, size));		\
}
/*
 * Android API change,
 * The xattr_handler->get() callback was changed to take a dentry and inode
 * and flags, because the dentry might not be attached to an inode yet.
 */
#elif defined(HAVE_XATTR_GET_DENTRY_INODE_FLAGS)
#define	ZPL_XATTR_GET_WRAPPER(fn)					\
static int								\
fn(const struct xattr_handler *handler, struct dentry *dentry,		\
    struct inode *inode, const char *name, void *buffer,		\
    size_t size, int flags)						\
{									\
	return (__ ## fn(inode, name, buffer, size));			\
}
#else
#error "Unsupported kernel"
#endif

/*
 * 6.3 API change,
 * The xattr_handler->set() callback was changed to take the
 * struct mnt_idmap* as the first arg, to support idmapped
 * mounts.
 */
#if defined(HAVE_XATTR_SET_IDMAP)
#define	ZPL_XATTR_SET_WRAPPER(fn)					\
static int								\
fn(const struct xattr_handler *handler, struct mnt_idmap *user_ns,	\
    struct dentry *dentry, struct inode *inode, const char *name,	\
    const void *buffer, size_t size, int flags)	\
{									\
	return (__ ## fn(user_ns, inode, name, buffer, size, flags));	\
}
/*
 * 5.12 API change,
 * The xattr_handler->set() callback was changed to take the
 * struct user_namespace* as the first arg, to support idmapped
 * mounts.
 */
#elif defined(HAVE_XATTR_SET_USERNS)
#define	ZPL_XATTR_SET_WRAPPER(fn)					\
static int								\
fn(const struct xattr_handler *handler, struct user_namespace *user_ns, \
    struct dentry *dentry, struct inode *inode, const char *name,	\
    const void *buffer, size_t size, int flags)	\
{									\
	return (__ ## fn(user_ns, inode, name, buffer, size, flags));	\
}
/*
 * 4.7 API change,
 * The xattr_handler->set() callback was changed to take a both dentry and
 * inode, because the dentry might not be attached to an inode yet.
 */
#elif defined(HAVE_XATTR_SET_DENTRY_INODE)
#define	ZPL_XATTR_SET_WRAPPER(fn)					\
static int								\
fn(const struct xattr_handler *handler, struct dentry *dentry,		\
    struct inode *inode, const char *name, const void *buffer,		\
    size_t size, int flags)						\
{									\
	return (__ ## fn(kcred->user_ns, inode, name, buffer, size, flags));\
}
/*
 * 4.4 API change,
 * The xattr_handler->set() callback was changed to take a xattr_handler,
 * and handler_flags argument was removed and should be accessed by
 * handler->flags.
 */
#elif defined(HAVE_XATTR_SET_HANDLER)
#define	ZPL_XATTR_SET_WRAPPER(fn)					\
static int								\
fn(const struct xattr_handler *handler, struct dentry *dentry,		\
    const char *name, const void *buffer, size_t size, int flags)	\
{									\
	return (__ ## fn(kcred->user_ns, dentry->d_inode, name,	\
	    buffer, size, flags));					\
}
/*
 * 2.6.33 API change,
 * The xattr_handler->set() callback was changed to take a dentry
 * instead of an inode, and a handler_flags argument was added.
 */
#elif defined(HAVE_XATTR_SET_DENTRY)
#define	ZPL_XATTR_SET_WRAPPER(fn)					\
static int								\
fn(struct dentry *dentry, const char *name, const void *buffer,		\
    size_t size, int flags, int unused_handler_flags)			\
{									\
	return (__ ## fn(kcred->user_ns, dentry->d_inode, name,	\
	    buffer, size, flags));					\
}
#else
#error "Unsupported kernel"
#endif

/*
 * Linux 3.7 API change. posix_acl_{from,to}_xattr gained the user_ns
 * parameter.  All callers are expected to pass the &init_user_ns which
 * is available through the init credential (kcred).
 */
static inline struct posix_acl *
zpl_acl_from_xattr(const void *value, int size)
{
	return (posix_acl_from_xattr(kcred->user_ns, value, size));
}

static inline int
zpl_acl_to_xattr(struct posix_acl *acl, void *value, int size)
{
	return (posix_acl_to_xattr(kcred->user_ns, acl, value, size));
}

#endif /* _ZFS_XATTR_H */
