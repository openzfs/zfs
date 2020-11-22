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

#ifndef _SPL_MOD_H
#define	_SPL_MOD_H

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#define	MODULE_INIT(s)
#define	MODULE_AUTHOR(s)
#define	MODULE_LICENSE(s)
#define	MODULE_VERSION(s)
#define	ZFS_MODULE_DESCRIPTION(s)
#define	ZFS_MODULE_AUTHOR(s)
#define	ZFS_MODULE_LICENSE(s)
#define	ZFS_MODULE_VERSION(s)

#define	ZFS_MODULE_PARAM_CALL(scope_prefix, name_prefix, name, setfunc, \
    getfunc, perm, desc)

#ifdef _MSC_VER
#define	__init
#define	__exit 
#else
#define	__init __attribute__((unused))
#define	__exit __attribute__((unused))
#endif

/*
 * The init/fini functions need to be called, but they are all static
 */
#define	module_init(fn)		\
	int wrap_ ## fn(void)	\
	{			\
		return (fn());	\
	}

#define	module_exit(fn)		\
	void wrap_ ## fn(void)	\
	{			\
		fn();		\
	}

#define	ZFS_MODULE_PARAM_ARGS	void

#define	ZFS_MODULE_PARAM(A, B, C, D, E, F)
#define	module_param_call(a, b, c, d, e)
#define	module_param_named(a, b, c, d)

struct zfs_kernel_param_s;
typedef struct zfs_kernel_param_s zfs_kernel_param_t;


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SPL_MOD_H */
