// SPDX-License-Identifier: BSD-2-Clause
#ifndef	_OPENSOLARIS_SYS_SYSCTL_H_
#define	_OPENSOLARIS_SYS_SYSCTL_H_

#ifdef _KERNEL
#include <sys/cdefs.h>
#include_next <sys/sysctl.h>

/*
 * FreeBSD-shaped SYSCTL_* used by ZFS_MODULE_PARAM.  NetBSD's native
 * macros have a different signature, so replace them with a link-set
 * of descriptors that kmod_core.c walks at load (same idea as
 * FreeBSD SYSINIT).  Variables stay static in their compilation unit.
 *
 * Do NOT use __link_set_decl / __link_set_foreach.  Those emit
 * undefined __start_link_set_* / __stop_link_set_* symbols.  The
 * kernel linker script defines those only for the statically linked
 * kernel; kobj_checksyms looks them up in the kernel and fails the
 * load (ENOEXEC) for a kmod.  NetBSD walks kmod link-sets with
 * module_find_section() during MODULE_CMD_INIT -- same as
 * link_set_modules / link_set_sysctl_funcs.  Keep the section name
 * "link_set_zfs_nb_sysctls".
 */
#undef SYSCTL_DECL
#undef SYSCTL_NODE
#undef SYSCTL_ROOT_NODE
#undef SYSCTL_INT
#undef SYSCTL_UINT
#undef SYSCTL_LONG
#undef SYSCTL_ULONG
#undef SYSCTL_QUAD
#undef SYSCTL_UQUAD
#undef SYSCTL_STRING
#undef SYSCTL_PROC
#undef SYSCTL_UAUTO
#undef SYSCTL_ADD_PROC
#undef SYSCTL_ADD_NODE
#undef SYSCTL_ADD_INT
#undef SYSCTL_ADD_U64
#undef SYSCTL_ADD_STRING
#undef SYSCTL_CHILDREN
#undef SYSCTL_STATIC_CHILDREN

#define	ZFS_NB_SYSCTL_NODE	1
#define	ZFS_NB_SYSCTL_INT	2
#define	ZFS_NB_SYSCTL_UINT	3
#define	ZFS_NB_SYSCTL_LONG	4
#define	ZFS_NB_SYSCTL_U64	5
#define	ZFS_NB_SYSCTL_STRING	6
#define	ZFS_NB_SYSCTL_PROC	7

/*
 * FreeBSD sysctl handlers take a req; provide a dummy so
 * existing ZFS_MODULE_PARAM handlers compile.  Defined before
 * the descriptor so the handler typedef can use it.
 */
struct sysctl_req {
	void	*newptr;
	size_t	newlen;
	void	*oldptr;
	size_t	oldlen;
	size_t	oldidx;
};

#define	SYSCTL_HANDLER_ARGS	void *oidp, void *arg1, intptr_t arg2, \
				struct sysctl_req *req

typedef int (*zfs_nb_sysctl_handler_t)(SYSCTL_HANDLER_ARGS);

struct zfs_nb_sysctl {
	const char	*parent;	/* "_vfs_zfs" / "_vfs_zfs_prefetch" */
	const char	*name;
	int		kind;
	void		*ptr;
	size_t		size;
	const char	*desc;
	int		flags;		/* PROC: CTLTYPE_* | CTLFLAG_* */
	intptr_t	arg;
	zfs_nb_sysctl_handler_t handler;
};

#define	_ZFS_NB_SYSCTL_ID(n)	__CONCAT(_zfs_nb_sysctl_, n)
#define	_ZFS_NB_SYSCTL_PTR(n)	__CONCAT(_zfs_nb_sysctl_p_, n)

#define	_ZFS_NB_SYSCTL(par, nam, knd, p, sz, dsc, fl)			\
	static const struct zfs_nb_sysctl _ZFS_NB_SYSCTL_ID(__LINE__)	\
	    __used = {							\
		.parent = #par,						\
		.name = #nam,						\
		.kind = (knd),						\
		.ptr = (void *)(p),					\
		.size = (sz),						\
		.desc = (dsc),						\
		.flags = (fl)						\
	};								\
	static const struct zfs_nb_sysctl * const			\
	    _ZFS_NB_SYSCTL_PTR(__LINE__)				\
	    __used __section("link_set_zfs_nb_sysctls")			\
	    __aligned(sizeof (void *)) =				\
	    &_ZFS_NB_SYSCTL_ID(__LINE__)

#define	SYSCTL_DECL(...)
#define	SYSCTL_ROOT_NODE(...)
#define	SYSCTL_NODE(parent, oid, name, perm, handler, desc)		\
	_ZFS_NB_SYSCTL(parent, name, ZFS_NB_SYSCTL_NODE, NULL, 0, desc, \
	    perm)
#define	SYSCTL_INT(parent, oid, name, perm, ptr, val, desc)		\
	_ZFS_NB_SYSCTL(parent, name, ZFS_NB_SYSCTL_INT, ptr,		\
	    sizeof (int), desc, perm)
#define	SYSCTL_UINT(parent, oid, name, perm, ptr, val, desc)		\
	_ZFS_NB_SYSCTL(parent, name, ZFS_NB_SYSCTL_UINT, ptr,		\
	    sizeof (unsigned int), desc, perm)
#define	SYSCTL_LONG(parent, oid, name, perm, ptr, val, desc)		\
	_ZFS_NB_SYSCTL(parent, name, ZFS_NB_SYSCTL_LONG, ptr,		\
	    sizeof (long), desc, perm)
#define	SYSCTL_ULONG(parent, oid, name, perm, ptr, val, desc)		\
	_ZFS_NB_SYSCTL(parent, name, ZFS_NB_SYSCTL_U64, ptr,		\
	    sizeof (unsigned long), desc, perm)
#define	SYSCTL_QUAD(parent, oid, name, perm, ptr, val, desc)		\
	_ZFS_NB_SYSCTL(parent, name, ZFS_NB_SYSCTL_U64, ptr,		\
	    sizeof (uint64_t), desc, perm)
#define	SYSCTL_UQUAD(parent, oid, name, perm, ptr, val, desc)		\
	_ZFS_NB_SYSCTL(parent, name, ZFS_NB_SYSCTL_U64, ptr,		\
	    sizeof (uint64_t), desc, perm)
#define	SYSCTL_U64(parent, oid, name, perm, ptr, val, desc)		\
	_ZFS_NB_SYSCTL(parent, name, ZFS_NB_SYSCTL_U64, ptr,		\
	    sizeof (uint64_t), desc, perm)
#define	SYSCTL_S64(parent, oid, name, perm, ptr, val, desc)		\
	_ZFS_NB_SYSCTL(parent, name, ZFS_NB_SYSCTL_U64, ptr,		\
	    sizeof (int64_t), desc, perm)
#define	SYSCTL_STRING(parent, oid, name, perm, ptr, len, desc)		\
	_ZFS_NB_SYSCTL(parent, name, ZFS_NB_SYSCTL_STRING, ptr,		\
	    (len), desc, perm)
#define	SYSCTL_CONST_STRING(parent, oid, name, perm, ptr, desc)		\
	_ZFS_NB_SYSCTL(parent, name, ZFS_NB_SYSCTL_STRING,		\
	    __UNCONST(ptr), 0, desc, perm)

#define	SYSCTL_NULL_INT_PTR	NULL
#define	SYSCTL_NULL_LONG_PTR	NULL
#define	SYSCTL_NULL_QUAD_PTR	NULL
#define	SYSCTL_SIZEOF(...)

/*
 * PROC: emit a link-set descriptor with the FreeBSD handler.  kmod_core
 * wraps it in a NetBSD SYSCTLFN and sysctl_createv's it.  The blake3 /
 * sha2 / fletcher / spa_taskq handlers are compiled for NetBSD too
 * (same FreeBSD-shaped functions, our sbuf.h).
 */
#define	SYSCTL_PROC(par, oid, nam, flg, p, a, fn, fmt, dsc)		\
	static const struct zfs_nb_sysctl _ZFS_NB_SYSCTL_ID(__LINE__)	\
	    __used = {							\
		.parent = #par,						\
		.name = #nam,						\
		.kind = ZFS_NB_SYSCTL_PROC,				\
		.ptr = (void *)(uintptr_t)(p),				\
		.size = 0,						\
		.desc = (dsc),						\
		.flags = (flg),						\
		.arg = (intptr_t)(a),					\
		.handler = (zfs_nb_sysctl_handler_t)(fn)		\
	};								\
	static const struct zfs_nb_sysctl * const			\
	    _ZFS_NB_SYSCTL_PTR(__LINE__)				\
	    __used __section("link_set_zfs_nb_sysctls")			\
	    __aligned(sizeof (void *)) =				\
	    &_ZFS_NB_SYSCTL_ID(__LINE__)
#define	SYSCTL_UAUTO(...)
#define	SYSCTL_OPAQUE(...)
#define	SYSCTL_STRUCT(...)
#define	SYSCTL_JAIL_PARAM(...)
#define	SYSCTL_JAIL_PARAM_SYS_NODE(...)
#define	SYSCTL_ADD_PROC(...)		NULL
#define	SYSCTL_ADD_NODE(...)		NULL
#define	SYSCTL_ADD_INT(...)		NULL
#define	SYSCTL_ADD_U64(...)		NULL
#define	SYSCTL_ADD_STRING(...)		NULL
#define	SYSCTL_CHILDREN(...)		NULL
#define	SYSCTL_STATIC_CHILDREN(...)	NULL

#undef OID_AUTO
#define	OID_AUTO		(-1)
/*
 * FreeBSD CTLFLAG_RW / RD / RWTUN / RDTUN are not NetBSD sysctl
 * flags.  Defining CTLFLAG_RW as 0 makes it indistinguishable from
 * "no bit", and treating "no RWTUN" as read-only then freezes nodes
 * that pass CTLFLAG_RW (zfs_arc_free_target).  Keep the bits out of
 * the low byte: zfs_nb_proc_ctype() recovers CTLTYPE_* with
 * flags & 0xff.  kmod_core maps them to CTLFLAG_READWRITE or
 * CTLFLAG_READONLY and does not pass them to sysctl_createv.
 */
#undef	CTLFLAG_RW
#undef	CTLFLAG_RD
#undef	CTLFLAG_RWTUN
#undef	CTLFLAG_RDTUN
#define	CTLFLAG_RW		0x20000000
#define	CTLFLAG_RD		0x10000000
#define	CTLFLAG_RWTUN		0x40000000
#define	CTLFLAG_RDTUN		0x80000000
#ifndef CTLFLAG_MPSAFE
#define	CTLFLAG_MPSAFE		0
#endif
#ifndef CTLFLAG_SKIP
#define	CTLFLAG_SKIP		0
#endif
#ifndef CTLTYPE_INT
#define	CTLTYPE_INT		0
#define	CTLTYPE_U64		0
#define	CTLTYPE_STRING		0
#define	CTLTYPE_ULONG		0
#define	CTLTYPE_UINT		0
#endif
#ifndef CTLTYPE_S32
#define	CTLTYPE_S32		CTLTYPE_INT
#define	CTLTYPE_U32		CTLTYPE_INT
#define	CTLTYPE_S64		CTLTYPE_QUAD
#define	CTLTYPE_OPAQUE		CTLTYPE_STRUCT
#endif
#ifndef CTLTYPE_U64
#define	CTLTYPE_U64		CTLTYPE_QUAD
#endif
#ifndef CTLTYPE_UINT
#define	CTLTYPE_UINT		CTLTYPE_INT
#endif
#ifndef CTLTYPE_QUAD
#define	CTLTYPE_QUAD		0
#define	CTLTYPE_STRUCT		0
#endif
/*
 * NetBSD has no CTLTYPE_LONG.  A 64-bit long is a quad; 32-bit
 * long is an int.
 */
#ifndef CTLTYPE_LONG
#if defined(_LP64) || defined(__LP64__)
#define	CTLTYPE_LONG		CTLTYPE_QUAD
#else
#define	CTLTYPE_LONG		CTLTYPE_INT
#endif
#endif
#ifndef CTLTYPE_ULONG
#define	CTLTYPE_ULONG		CTLTYPE_LONG
#endif

int sysctl_handle_int(SYSCTL_HANDLER_ARGS);
int sysctl_handle_long(SYSCTL_HANDLER_ARGS);
int sysctl_handle_64(SYSCTL_HANDLER_ARGS);
int sysctl_handle_string(SYSCTL_HANDLER_ARGS);
#define	sysctl_ctx_init(...)		(0)
#define	sysctl_ctx_free(...)		(0)

#ifndef CTLFLAG_VNET
#define	CTLFLAG_VNET		0
#endif

#ifndef __UNCONST
#define	__UNCONST(a)		((void *)(unsigned long)(const void *)(a))
#endif

void zfs_nb_sysctl_install(struct sysctllog **, const struct sysctlnode *);

#endif /* _KERNEL */

#endif /* _OPENSOLARIS_SYS_SYSCTL_H_ */
