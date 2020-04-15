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

#include <sys/sysctl.h>

#define	MODULE_INIT(s)
#define	MODULE_AUTHOR(s)
#define	MODULE_LICENSE(s)
#define	MODULE_VERSION(s)
#define	ZFS_MODULE_DESCRIPTION(s)
#define	ZFS_MODULE_AUTHOR(s)
#define	ZFS_MODULE_LICENSE(s)
#define	ZFS_MODULE_VERSION(s)

#define	__init __attribute__((unused))
#define	__exit __attribute__((unused))

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

// XNU defines SYSCTL_HANDLER_ARGS with "()" so it no worky.
// #define	ZFS_MODULE_PARAM_ARGS	SYSCTL_HANDLER_ARGS
#define	ZFS_MODULE_PARAM_ARGS	\
	struct sysctl_oid *oidp, void *arg1, int arg2, struct sysctl_req *req

#define	ZMOD_RW CTLFLAG_RW
#define	ZMOD_RD CTLFLAG_RD

/* BEGIN CSTYLED */

/* Handle some FreeBSD sysctl differences */
#define	SYSCTL_CONST_STRING(parent, nbr, name, access, ptr, descr)		\
	SYSCTL_STRING(parent, nbr, name, access, ptr, sizeof(ptr), descr)
#define	SYSCTL_UQUAD(parent, nbr, name, access, ptr, val, descr) \
	SYSCTL_QUAD(parent, nbr, name, access, ptr, descr)

#define	CTLFLAG_RWTUN CTLFLAG_RW
#define	CTLFLAG_RDTUN CTLFLAG_RD
#define	CTLTYPE_UINT CTLTYPE_INT
#define	CTLTYPE_ULONG CTLTYPE_INT
#define	CTLTYPE_U64 CTLTYPE_QUAD
#define	CTLTYPE_S64 CTLTYPE_QUAD
#define	CTLFLAG_MPSAFE 0

/*
 * Why do all SYSCTL take "val" except for LONG/ULONG ?
 * Jump through hoops here to handle that.
 */
#define	ZSYSCTL_INT SYSCTL_INT
#define	ZSYSCTL_UINT SYSCTL_UINT
#define	ZSYSCTL_STRING SYSCTL_STRING
#define	ZSYSCTL_LONG(parent, nbr, name, access, ptr, val, descr) \
    SYSCTL_LONG(parent, nbr, name, access, ptr, descr)
#if defined SYSCTL_ULONG
#define	ZSYSCTL_ULONG(parent, nbr, name, access, ptr, val, descr) \
    SYSCTL_ULONG(parent, nbr, name, access, ptr, descr)
#else
#define	ZSYSCTL_ULONG(parent, nbr, name, access, ptr, val, descr) \
    SYSCTL_LONG(parent, nbr, name, access, ptr, descr)
#endif
/*
 * Appears to be no default for 64bit values in Linux, if
 * ZOL adds it using STANDARD_PARAM_DEF let us guess
 * they will go with LLONG/ULLONG
 */
#define	ZSYSCTL_LLONG(parent, nbr, name, access, ptr, val, descr) \
    SYSCTL_QUAD(parent, nbr, name, access, ptr, descr)
#define	ZSYSCTL_ULLONG(parent, nbr, name, access, ptr, val, descr) \
    SYSCTL_QUAD(parent, nbr, name, access, ptr, descr)
#define	ZSYSCTL_U64(parent, nbr, name, access, ptr, val, descr) \
    SYSCTL_QUAD(parent, nbr, name, access, ptr, descr)
#define	ZSYSCTL_S64(parent, nbr, name, access, ptr, val, descr) \
    SYSCTL_QUAD(parent, nbr, name, access, ptr, descr)

/* See sysctl_os.c for the constructor work */
#define	ZFS_MODULE_PARAM(scope_prefix, name_prefix, name, type, perm, desc) \
    SYSCTL_DECL( _tunable_ ## scope_prefix); \
    ZSYSCTL_##type( _tunable_ ## scope_prefix, OID_AUTO, name, perm, \
	    &name_prefix ## name, 0, desc) ; \
	__attribute__((constructor)) void \
	    _zcnst_sysctl__tunable_ ## scope_prefix ## _ ## name (void) \
	{ \
		sysctl_register_oid(&sysctl__tunable_ ## scope_prefix ## _ ## name ); \
	} \
	__attribute__((destructor)) void \
	    _zdest_sysctl__tunable_ ## scope_prefix ## _ ## name (void) \
	{ \
		sysctl_unregister_oid(&sysctl__tunable_ ## scope_prefix ## _ ## name ); \
	}

/*
 * Same as above, but direct names; so they can be empty.
 * Used internally in macOS.
 */
#define	ZFS_MODULE_IMPL(scope, variable, name, type, perm, desc) \
	SYSCTL_DECL( _tunable ## scope);		\
	ZSYSCTL_##type( _tunable ## scope, OID_AUTO, name, perm,	\
		&variable, 0, desc) ;		\
	__attribute__((constructor)) void	\
	_zcnst_sysctl__tunable ## scope ## _ ## name (void)	\
	{		\
		sysctl_register_oid(&sysctl__tunable ## scope ## _ ## name );	\
	}		\
	__attribute__((destructor)) void		\
	_zdest_sysctl__tunable ## scope ## _ ## name (void)	\
	{		\
		sysctl_unregister_oid(&sysctl__tunable ## scope ## _ ## name ); \
	}

/* Function callback sysctls */
#define	ZFS_MODULE_PARAM_CALL_IMPL(parent, name, perm, args, desc)	\
    SYSCTL_DECL(parent); \
    SYSCTL_PROC(parent, OID_AUTO, name, perm | args, desc) ;	\
	__attribute__((constructor)) void \
	    _zcnst_sysctl_ ## parent ## _ ## name (void) \
	{ \
		sysctl_register_oid(&sysctl_## parent ## _ ## name ); \
	} \
	__attribute__((destructor)) void \
	    _zdest_sysctl_ ## parent ## _ ## name (void) \
	{ \
		sysctl_unregister_oid(&sysctl_ ## parent ## _ ## name ); \
	}

/*
 * Too few arguments? You probably added a new MODULE_PARAM_CALL
 * but have yet to create a #define for it below, see for example
 * blake3_param_set_args - ie, "func" + "_args"
 */
#define	ZFS_MODULE_PARAM_CALL(scope_prefix, name_prefix, name, func, _, perm, desc) \
    ZFS_MODULE_PARAM_CALL_IMPL(_tunable_ ## scope_prefix, name, perm, func ## _args(name_prefix ## name), desc)

#define	ZFS_MODULE_VIRTUAL_PARAM_CALL ZFS_MODULE_PARAM_CALL

/*
 * FreeBSD anchor the function name (+ _args) to work out the
 * CTLTYPE_* to use (and print "LU" etc). To call a wrapper
 * function in sysctl_os.c, which calls the real function.
 * We could also map "param_set_charp" to "CTLTYPE_STRING" more
 * automatically, however, we still need manual update for the
 * wrapping functions so it would not gain anything.
 */

#define	param_set_arc_u64_args(var) \
    CTLTYPE_ULONG, &var, 0, param_set_arc_u64, "QU"

#define	param_set_arc_min_args(var) \
    CTLTYPE_ULONG, &var, 0, param_set_arc_min, "LU"

#define	param_set_arc_max_args(var) \
    CTLTYPE_QUAD, &var, 0, param_set_arc_max, "QU"

#define	param_set_arc_int_args(var) \
    CTLTYPE_INT, &var, 0, param_set_arc_int, "I"

#define	param_set_deadman_failmode_args(var) \
    CTLTYPE_STRING, NULL, 0, param_set_deadman_failmode, "A"

#define	param_set_deadman_synctime_args(var) \
    CTLTYPE_ULONG, NULL, 0, param_set_deadman_synctime, "LU"

#define	param_set_deadman_ziotime_args(var) \
    CTLTYPE_ULONG, NULL, 0, param_set_deadman_ziotime, "LU"

#define	param_set_multihost_interval_args(var) \
    CTLTYPE_ULONG, &var, 0, param_set_multihost_interval, "LU"

#define	param_set_slop_shift_args(var) \
    CTLTYPE_INT, &var, 0, param_set_slop_shift, "I"

#define	param_set_min_auto_ashift_args(var) \
    CTLTYPE_U64, &var, 0, param_set_min_auto_ashift, "QU"

#define	param_set_max_auto_ashift_args(var) \
    CTLTYPE_U64, &var, 0, param_set_max_auto_ashift, "QU"

#define	fletcher_4_param_set_args(var) \
    CTLTYPE_STRING, NULL, 0, fletcher_4_param, "A"

#define	blake3_param_set_args(var) \
    CTLTYPE_STRING, NULL, 0, blake3_param, "A"

#define	icp_gcm_avx_set_chunk_size_args(var) \
    CTLTYPE_STRING, var, 0, param_icp_gcm_avx_set_chunk_size, "A"

#define	icp_gcm_impl_set_args(var)	\
    CTLTYPE_STRING, var, 0, param_icp_gcm_impl_set, "A"

#define	icp_aes_impl_set_args(var) \
    CTLTYPE_STRING, var, 0, param_icp_aes_impl_set, "A"

#define	zfs_vdev_raidz_impl_set_args(var) \
    CTLTYPE_STRING, var, 0, param_zfs_vdev_raidz_impl_set, "A"

/*
 * Too few arguments? You probably added a new MODULE_PARAM_CALL
 * but have yet to create a #define for it above, see for example
 * blake3_param_set_args - ie, "func" + "_args". As well as
 * possible handler in os/macos/zfs/syscall_os.c
 */
#define	module_param_call(name, _set, _get, var, mode) \
	extern int param_ ## func(ZFS_MODULE_PARAM_ARGS);	   \
    ZFS_MODULE_PARAM_CALL_IMPL(_tunable, name, ZMOD_RW,   \
		_set ## _args(var), "xxx")

#define	module_param_named(a, b, c, d)

#define	module_init_early(fn) \
void \
wrap_ ## fn(void *dummy __unused) \
{	\
	fn();	\
}

kern_return_t spl_start(kmod_info_t *ki, void *d);
kern_return_t spl_stop(kmod_info_t *ki, void *d);

struct zfs_kernel_param_s;
typedef struct zfs_kernel_param_s zfs_kernel_param_t;

extern int param_set_uint(char *v, zfs_kernel_param_t *kp);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SPL_MOD_H */
