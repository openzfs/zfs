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
 * Copyright(c) 2022 Jorgen Lundman <lundman@lundman.net>
 */

/*
 * OpenZFS litter the source files with ZFS_MODULE_PARAMS
 * which are tunables for the kernel. They are generally "static".
 *
 * For Windows, we collect all these into a "linker set", which
 * we can iterate at start up, and add the tunables to the Registry.
 *
 * Having just a pointer to the variable isn't going to be enough
 * so the macro will define a struct with:
 *   - ptr to tunable
 *   - name of tunable
 *   - submodule name
 *   - type of tunable (int, long, string)
 * and this struct is put into the linker set.
 * For _CALL style, we also define
 *   - func to call
 * which allows a function to be called to sanitise the input.
 */


#ifndef _SPL_MOD_H
#define	_SPL_MOD_H

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#include <sys/linker_set.h>
#include <sys/string.h>

#define	MODULE_INIT(s)
#define	MODULE_AUTHOR(s)
#define	MODULE_LICENSE(s)
#define	MODULE_VERSION(s)
#define	ZFS_MODULE_DESCRIPTION(s)
#define	ZFS_MODULE_AUTHOR(s)
#define	ZFS_MODULE_LICENSE(s)
#define	ZFS_MODULE_VERSION(s)

#ifdef _MSC_VER
#define	__init
#define	__exit
#else
#define	__init __attribute__((unused))
#define	__exit __attribute__((unused))
#endif

// Glancing at Linux kernel, module parameters limit:
#define	LINUX_MAX_MODULE_PARAM_LEN 1024

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

#define	module_param_named(a, b, c, d)

#define	module_init_early(fn)	\
	void \
	wrap_ ## fn(void *dummy __unused) \
	{	\
	fn();   \
	}

typedef enum {
	ZT_ZMOD_RD,
	ZT_ZMOD_RW
} ztunable_perm;

/*
 * STRING is a bit awkward, Linux kernel use it as
 * "char *s = NULL", so it is allocated elsewhere.
 * But we also like to be able to use it with static
 * areas, like *version = "openzfs-2.1.8", so we
 * internally add a flag member, so we can know what to
 * free.
 */
typedef enum {
	ZT_FLAG_ALLOCATED = 0,
	ZT_FLAG_STATIC = (1<<0),
	ZT_FLAG_WRITEONLY = (1<<1),
} ztunable_flag;

/*
 * ZFS_MODULE_CALL() and VIRTUAL do not define a type (like ULONG) in the
 * MACRO so, they are set to ZT_TYPE_NOTSET. The call ZT_GET_VALUE( ..., &type)
 * is used to fetch the real type from each handler function.
 * The handler functions are given and expected:
 * function(struct ztunable_s *zt, void **ptr, ULONG *len, ULONG *type, \
 * boolean_t set)
 *  * GET: point 'ptr' to variable, set 'len' size of variable, set 'type' to
 * real type.
 *  * SET: 'ptr' points to input, 'len' has size (for ASSERT), set 'type' to
 * real type.
 */
typedef enum {
	ZT_TYPE_NOTSET,	// _CALL sets no type.
	ZT_TYPE_INT,	// Linux INT
	ZT_TYPE_UINT,	// Linux UINT
	ZT_TYPE_LONG,	// Linux LONG
	ZT_TYPE_ULONG,	// Linux ULONG
	ZT_TYPE_STRING,	// Linux STRING
	ZT_TYPE_U64,	// Future expansion
	ZT_TYPE_S64,	// Future expansion
} ztunable_type;

// Enhance this to dynamic one day?
#define	ZFS_MODULE_STRMAX	64

static inline uint64_t ZT_TYPE_REGISTRY(ztunable_type t)
{
	switch (t) {
	case ZT_TYPE_INT:
	case ZT_TYPE_UINT:
		return (REG_DWORD);
	// "long" on linux is 8 bytes (x64), and windows 4. we
	// has special type for it, so for "ZT_" it is 8 bytes.
	case ZT_TYPE_LONG:
	case ZT_TYPE_ULONG:
		return (REG_QWORD);
	case ZT_TYPE_STRING:
		return (REG_SZ);
	case ZT_TYPE_U64:
	case ZT_TYPE_S64:
		return (REG_QWORD);
	case ZT_TYPE_NOTSET:
		/* not reached */
		ASSERT3U(t, !=, ZT_TYPE_NOTSET);
		return (REG_NONE);
	}
	return (REG_NONE);
}

static inline uint64_t ZT_TYPE_SIZE(ztunable_type t)
{
	switch (t) {
	case ZT_TYPE_INT:
	case ZT_TYPE_UINT:
		return (sizeof (int));
		// For now, "long" on linux is 8 bytes, and windows 4.
	case ZT_TYPE_LONG:
	case ZT_TYPE_ULONG:
		return (sizeof (uint64_t));
	case ZT_TYPE_STRING:
		return (sizeof (uintptr_t));
	case ZT_TYPE_U64:
	case ZT_TYPE_S64:
		return (sizeof (uint64_t));
	case ZT_TYPE_NOTSET:
		/* not reached */
		ASSERT3U(t, !=, ZT_TYPE_NOTSET);
		return (0);
	}
	return (0);
}

#define	ZFS_MODULE_PARAM_ARGS \
    struct ztunable_s *zt, void **ptr, ULONG *len, ULONG *type, boolean_t set

typedef struct ztunable_s {
	void *zt_ptr;
	int (*zt_func)(ZFS_MODULE_PARAM_ARGS); /* If SET this is a callout */
	const char *zt_name;
	const char *zt_prefix;
	const char *zt_desc;
	ztunable_perm zt_perm;
	ztunable_type zt_type;
	ztunable_flag zt_flag;
} ztunable_t;

static inline void
ZT_SET_VALUE(ztunable_t *zt, void **ptr, ULONG *len, ULONG *type)
{

	if (zt->zt_func != NULL) {
		zt->zt_func(zt, ptr, len, type, B_TRUE);
		return;
	}

	switch (zt->zt_type) {
	case ZT_TYPE_INT:
	case ZT_TYPE_UINT:
		ASSERT3U(*len, >=, sizeof (int));
		*(int *)zt->zt_ptr = *(int *)*ptr;
		return;
	case ZT_TYPE_LONG:
	case ZT_TYPE_ULONG:
		ASSERT3U(*len, >=, sizeof (uint64_t));
		*(uint64_t *)zt->zt_ptr = *(uint64_t *)*ptr;
		return;
	case ZT_TYPE_STRING:
		if (zt->zt_flag & ZT_FLAG_STATIC) {
			strlcpy(zt->zt_ptr, *ptr, ZFS_MODULE_STRMAX);
			return;
		}
		zt->zt_ptr = (void *)*ptr;
		return;
	case ZT_TYPE_U64:
	case ZT_TYPE_S64:
		ASSERT3U(*len, >=, sizeof (uint64_t));
		*(uint64_t *)zt->zt_ptr = *(uint64_t *)*ptr;
		return;
	case ZT_TYPE_NOTSET:
		/* not reached */
		ASSERT3U(zt->zt_type, !=, ZT_TYPE_NOTSET);
		return;
	}
}

// This SETs ptr to point to the value location.
static inline void
ZT_GET_VALUE(ztunable_t *zt, void **ptr, ULONG *len, ULONG *type)
{

	if (zt->zt_func != NULL) {
		zt->zt_func(zt, ptr, len, type, B_FALSE);
		return;
	}

	*len = ZT_TYPE_SIZE(zt->zt_type);
	*type = zt->zt_type;

	switch (zt->zt_type) {
	case ZT_TYPE_INT:
	case ZT_TYPE_UINT:
	case ZT_TYPE_LONG:
	case ZT_TYPE_ULONG:
	case ZT_TYPE_U64:
	case ZT_TYPE_S64:
		*ptr = zt->zt_ptr;
		return;
	case ZT_TYPE_STRING:
		*ptr = zt->zt_ptr;
		if (zt->zt_ptr != NULL)
			*len = strlen(zt->zt_ptr);
		return;
	case ZT_TYPE_NOTSET:
		/* not reached */
		ASSERT3U(zt->zt_type, !=, ZT_TYPE_NOTSET);
		return;
	}
}

#define	ZFS_MODULE_PARAM(scope_prefix, name_prefix, name, type, perm, desc) \
	static ztunable_t zt_ ## name_prefix ## name = { \
		.zt_ptr = &name_prefix ## name, \
		.zt_func = NULL, \
		.zt_name = #name_prefix #name, \
		.zt_prefix = #scope_prefix, \
		.zt_desc = #desc, \
		.zt_perm = __CONCAT(ZT_, perm), \
		.zt_type = ZT_TYPE_ ## type, \
		.zt_flag = ZT_FLAG_STATIC \
	}; \
	SET_ENTRY(zt, zt_ ## name_prefix ## name)

/* Used only internally in Windows port */
#define	ZFS_MODULE_RAW(scope_prefix, name, variable, type, perm, flag, desc) \
	static ztunable_t zt_ ## variable = { \
		.zt_ptr = &variable, \
		.zt_func = NULL, \
		.zt_name = #name, \
		.zt_prefix = #scope_prefix, \
		.zt_desc = #desc, \
		.zt_perm = __CONCAT(ZT_, perm), \
		.zt_type = ZT_TYPE_ ## type, \
		.zt_flag = flag \
	}; \
	SET_ENTRY(zt, zt_ ## variable)


#define	ZFS_MODULE_PARAM_CALL_IMPL( \
    scope_prefix, name_prefix, name, perm, func, args, desc) \
	static ztunable_t zt_ ## name_prefix ## name = { \
		.zt_ptr = args, \
		.zt_func = func, \
		.zt_name = #name_prefix #name, \
		.zt_prefix = #scope_prefix, \
		.zt_desc = #desc, \
		.zt_perm = __CONCAT(ZT_, perm), \
		.zt_type = ZT_TYPE_NOTSET, \
		.zt_flag = ZT_FLAG_STATIC \
	}; \
	SET_ENTRY(zt, zt_ ## name_prefix ## name)

#define	ZFS_MODULE_PARAM_CALL( \
    scope_prefix, name_prefix, name, func, _, perm, desc) \
	ZFS_MODULE_PARAM_CALL_IMPL(scope_prefix, name_prefix, name, perm, \
	    func, &name_prefix ## name, desc)

#define	ZFS_MODULE_VIRTUAL_PARAM_CALL( \
    scope_prefix, name_prefix, name, func, _, perm, desc) \
	ZFS_MODULE_PARAM_CALL_IMPL(scope_prefix, name_prefix, name, perm, \
	    win32_ ## func, NULL, desc)

#define	module_param_call(name, _set, _get, var, mode) \
	extern int win32_ ## _set(ZFS_MODULE_PARAM_ARGS);	\
	ZFS_MODULE_PARAM_CALL_IMPL(zfs, /* */, name, ZMOD_RW,	\
		win32_ ## _set, var, "xxx")

struct zfs_kernel_param_s;
typedef struct zfs_kernel_param_s zfs_kernel_param_t;

extern int param_set_uint(char *v, zfs_kernel_param_t *kp);
#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SPL_MOD_H */
