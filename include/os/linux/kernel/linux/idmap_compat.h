// SPDX-License-Identifier: CDDL-1.0
/*
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this
 * source.  A copy of the CDDL is also available via the Internet at
 * http://www.illumos.org/license/CDDL.
 */

/*
 * Copyright (c) 2026, TrueNAS.
 */

#ifndef _ZFS_IDMAP_COMPAT_H
#define	_ZFS_IDMAP_COMPAT_H

/*
 * Linux has a notion of a "user namespace", the set of users & groups.
 * Processes and mounts can exist in different namespaces, such that an
 * operation performed by a user in one namespace may need to be applied
 * as a user in a different namespace.
 *
 * Example: a regular user (id=1000) runs a program in a container. A host
 * filesystem is mounted into the container. Inside the container, the
 * filesystem is written to by the "root" user (id=0). So, the id needs to
 * be "mapped" to the other namespace.
 *
 * The VFS takes care of most of this for us, however, inode operations can
 * access the object representing the id mapping in case they need to do
 * further work (eg permission checks), or pass it back into the kernel when
 * using generic helper functions.
 *
 * The type and method for accessing the mapping object has changed over the
 * years:
 *
 * - 3.8: user namespaces were introduce in 3.8. the "active" user namespace
 *        was bound to the current task credential, available to the inode
 *        operation via kcred->user_ns.
 *
 * - 5.12: to distinguish between the user namespace the current task is
 *         operating in vs the user namespace of the target inode (mount),
 *         a struct user_namespace is passed to the inode operation in the
 *         first arg.
 *
 * - 6.3: to allow the id mapping process to be changed in the future to
 *        consider more than just the user namespaces, inode operations now
 *        now get passed a struct mnt_idmap, an abstract object with methods
 *        to request the "mapped" value for particular things.
 *
 * We currently support kernels that span this range, so we need to support all
 * three methods. Fortunately, the kernel core switched in its entirety in the
 * same release, so we don't need to handle conversions between eg
 * user_namespace and mnt_idmap or anything like that.
 *
 * This file contains the types and macros needed for this support.
 */

/*
 * zidmap_t will always match the type of the thing carrying the mapping for
 * the current context. It can't ever be used directly, but we can name it
 * and cast to and from it safely in prototypes and calls.
 */
#ifdef HAVE_IDMAP_MNTIDMAP
typedef struct mnt_idmap	zidmap_t;
#else
typedef struct user_namespace	zidmap_t;
#endif

/*
 * The "identity" idmap, typically that of "host" namespace, "root" mount or
 * similar. Prepared in advance by the SPL.
 */
extern zidmap_t *zfs_init_idmap;

/*
 * Below is a macro system for defining inode_operations function callbacks
 * that always receive a zidmap_t in their params, regardless of how they were
 * called.
 *
 * Typical use:
 *
 *   ZPL_IDMAP_IOP_DEFINE(int, zpl_create, 4,
 *     struct inode *, dir, struct dentry *, dentry, umode_t, mode, bool, flag)
 *   {
 *     ...
 *   }
 *
 * This emits a trampoline function for the inode_operations table, with
 * first arg appropriate to the idmap type, one of:
 *
 *   static int zpl_create(struct mnt_idmap *, [args...])
 *   static int zpl_create(struct user_namespace *, [args...])
 *   static int zpl_create([args...])
 *
 * and the header for the implementing function:
 *
 * static int __zpl_create(zidmap_t *idmap, struct inode * dir,
 *   struct dentry * dentry, umode_t mode, bool flag)
 *
 * The trampoline function calls the implementing function with the first arg
 * filled in appropriately for the type & method. Since it's a zidmap_t, it
 * can be passed safely back to the kernel through the matching wrappers (eg
 * zpl_setattr_prepare(idmap, ...).
 */

/* Helper: expand (type, name) pairs into "type name" for function prototypes */
#define	__ZPL_ARGS_1(t, n)	t n
#define	__ZPL_ARGS_2(t, n, ...)	t n, __ZPL_ARGS_1(__VA_ARGS__)
#define	__ZPL_ARGS_3(t, n, ...)	t n, __ZPL_ARGS_2(__VA_ARGS__)
#define	__ZPL_ARGS_4(t, n, ...)	t n, __ZPL_ARGS_3(__VA_ARGS__)
#define	__ZPL_ARGS_5(t, n, ...)	t n, __ZPL_ARGS_4(__VA_ARGS__)
#define	__ZPL_ARGS_6(t, n, ...)	t n, __ZPL_ARGS_5(__VA_ARGS__)

/* Helper: expend (type, name) pairs into "name" for function calls */
#define	__ZPL_ARGNAMES_1(t, n)		n
#define	__ZPL_ARGNAMES_2(t, n, ...)	n, __ZPL_ARGNAMES_1(__VA_ARGS__)
#define	__ZPL_ARGNAMES_3(t, n, ...)	n, __ZPL_ARGNAMES_2(__VA_ARGS__)
#define	__ZPL_ARGNAMES_4(t, n, ...)	n, __ZPL_ARGNAMES_3(__VA_ARGS__)
#define	__ZPL_ARGNAMES_5(t, n, ...)	n, __ZPL_ARGNAMES_4(__VA_ARGS__)
#define	__ZPL_ARGNAMES_6(t, n, ...)	n, __ZPL_ARGNAMES_5(__VA_ARGS__)

/* Define the appropriate wrapper by the configure checks. */
#if defined(HAVE_IDMAP_MNTIDMAP)
#define	_ZPL_IDMAP_IOP_WRAPPER(rty, fn, n, ...)				\
static rty fn(struct mnt_idmap *idmap, __ZPL_ARGS_##n(__VA_ARGS__))	\
{									\
	return (__##fn(idmap, __ZPL_ARGNAMES_##n(__VA_ARGS__)));	\
}
#elif defined(HAVE_IDMAP_USERNS)
#define	_ZPL_IDMAP_IOP_WRAPPER(rty, fn, n, ...)				\
static rty fn(struct user_namespace *user_ns, __ZPL_ARGS_##n(__VA_ARGS__)) \
{									\
	return (__##fn(user_ns, __ZPL_ARGNAMES_##n(__VA_ARGS__)));	\
}
#else
#define	_ZPL_IDMAP_IOP_WRAPPER(rty, fn, n, ...)				\
static rty fn(__ZPL_ARGS_##n(__VA_ARGS__))				\
{									\
	return (__##fn(kcred->user_ns, __ZPL_ARGNAMES_##n(__VA_ARGS__))); \
}
#endif

/*
 * Declare the implementing function, then fill in the wrapper, then emit the
 * header for the implementation to follow.
 *
 * Note that idmap __maybe_unused, to avoid needing every function not using it
 * (most of them) to have to silence the compiler warning.
 */
#define	_ZPL_IDMAP_IOP_DEFINE(rty, fn, n, ...)				\
static rty __##fn(zidmap_t *idmap, __ZPL_ARGS_##n(__VA_ARGS__));	\
_ZPL_IDMAP_IOP_WRAPPER(rty, fn, n, __VA_ARGS__)				\
static rty __##fn(zidmap_t *idmap __maybe_unused, __ZPL_ARGS_##n(__VA_ARGS__))

#define	ZPL_IDMAP_IOP_DEFINE(rty, fn, n, ...)	\
	_ZPL_IDMAP_IOP_DEFINE(rty, fn, n, ##__VA_ARGS__)

#endif
