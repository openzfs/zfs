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

#endif
