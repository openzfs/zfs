/*
 * Copyright (c) 2020 iXsystems, Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHORS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef _SPL_MOD_H
#define	_SPL_MOD_H

#include <sys/sysctl.h>

#define	ZMOD_RW CTLFLAG_RWTUN
#define	ZMOD_RD CTLFLAG_RDTUN

#define	ZFS_MODULE_PARAM(scope_prefix, name_prefix, name, type, perm, desc) \
    SYSCTL_DECL(_vfs_ ## scope_prefix); \
    SYSCTL_##type(_vfs_ ## scope_prefix, OID_AUTO, name, perm, \
	&name_prefix ## name, 0, desc)

#define	ZFS_MODULE_PARAM_ARGS	SYSCTL_HANDLER_ARGS

#define	ZFS_MODULE_PARAM_CALL_IMPL(parent, name, perm, args, desc) \
    SYSCTL_DECL(parent); \
    SYSCTL_PROC(parent, OID_AUTO, name, CTLFLAG_MPSAFE | perm | args, desc)

#define	ZFS_MODULE_PARAM_CALL( \
    scope_prefix, name_prefix, name, func, _, perm, desc) \
	ZFS_MODULE_PARAM_CALL_IMPL(_vfs_ ## scope_prefix, name, perm, \
	    func ## _args(name_prefix ## name), desc)

#define	ZFS_MODULE_VIRTUAL_PARAM_CALL ZFS_MODULE_PARAM_CALL

#define	param_set_arc_long_args(var) \
    CTLTYPE_ULONG, &var, 0, param_set_arc_long, "LU"

#define	param_set_arc_int_args(var) \
    CTLTYPE_INT, &var, 0, param_set_arc_int, "I"

#define	param_set_arc_min_args(var) \
    CTLTYPE_ULONG, NULL, 0, param_set_arc_min, "LU"

#define	param_set_arc_max_args(var) \
    CTLTYPE_ULONG, NULL, 0, param_set_arc_max, "LU"

#define	param_set_arc_free_target_args(var) \
    CTLTYPE_UINT, NULL, 0, param_set_arc_free_target, "IU"

#define	param_set_arc_no_grow_shift_args(var) \
    CTLTYPE_INT, NULL, 0, param_set_arc_no_grow_shift, "I"

#define	param_set_deadman_failmode_args(var) \
    CTLTYPE_STRING, NULL, 0, param_set_deadman_failmode, "A"

#define	param_set_deadman_synctime_args(var) \
    CTLTYPE_ULONG, NULL, 0, param_set_deadman_synctime, "LU"

#define	param_set_deadman_ziotime_args(var) \
    CTLTYPE_ULONG, NULL, 0, param_set_deadman_ziotime, "LU"

#define	param_set_multihost_interval_args(var) \
    CTLTYPE_ULONG, NULL, 0, param_set_multihost_interval, "LU"

#define	param_set_slop_shift_args(var) \
    CTLTYPE_INT, NULL, 0, param_set_slop_shift, "I"

#define	param_set_min_auto_ashift_args(var) \
    CTLTYPE_U64, NULL, 0, param_set_min_auto_ashift, "QU"

#define	param_set_max_auto_ashift_args(var) \
    CTLTYPE_U64, NULL, 0, param_set_max_auto_ashift, "QU"

#define	fletcher_4_param_set_args(var) \
    CTLTYPE_STRING, NULL, 0, fletcher_4_param, "A"

#define	blake3_param_set_args(var) \
    CTLTYPE_STRING, NULL, 0, blake3_param, "A"

#include <sys/kernel.h>
#define	module_init(fn) \
static void \
wrap_ ## fn(void *dummy __unused) \
{ \
	fn(); \
} \
SYSINIT(zfs_ ## fn, SI_SUB_LAST, SI_ORDER_FIRST, wrap_ ## fn, NULL)

#define	module_init_early(fn) \
static void \
wrap_ ## fn(void *dummy __unused) \
{ \
	fn(); \
} \
SYSINIT(zfs_ ## fn, SI_SUB_INT_CONFIG_HOOKS, SI_ORDER_FIRST, wrap_ ## fn, NULL)

#define	module_exit(fn) \
static void \
wrap_ ## fn(void *dummy __unused) \
{ \
	fn(); \
} \
SYSUNINIT(zfs_ ## fn, SI_SUB_LAST, SI_ORDER_FIRST, wrap_ ## fn, NULL)

#endif /* SPL_MOD_H */
