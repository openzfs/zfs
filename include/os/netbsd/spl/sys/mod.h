// SPDX-License-Identifier: BSD-2-Clause
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

#define	param_set_arc_u64_args(var) \
    CTLTYPE_U64, &var, 0, param_set_arc_u64, "QU"

#define	param_set_arc_int_args(var) \
    CTLTYPE_INT, &var, 0, param_set_arc_int, "I"

#define	param_set_arc_min_args(var) \
    CTLTYPE_U64, NULL, 0, param_set_arc_min, "QU"

#define	param_set_arc_max_args(var) \
    CTLTYPE_U64, NULL, 0, param_set_arc_max, "QU"

#define	param_set_l2arc_dwpd_limit_args(var) \
    CTLTYPE_U64, &var, 0, param_set_l2arc_dwpd_limit, "QU"

#define	param_set_arc_free_target_args(var) \
    CTLTYPE_UINT, NULL, 0, param_set_arc_free_target, "IU"

#define	param_set_arc_no_grow_shift_args(var) \
    CTLTYPE_INT, NULL, 0, param_set_arc_no_grow_shift, "I"

#define	param_set_deadman_failmode_args(var) \
    CTLTYPE_STRING, NULL, 0, param_set_deadman_failmode, "A"

#define	param_set_active_allocator_args(var) \
    CTLTYPE_STRING, NULL, 0, param_set_active_allocator, "A"

#define	param_set_deadman_synctime_args(var) \
    CTLTYPE_U64, NULL, 0, param_set_deadman_synctime, "QU"

#define	param_set_deadman_ziotime_args(var) \
    CTLTYPE_U64, NULL, 0, param_set_deadman_ziotime, "QU"

#define	param_set_multihost_interval_args(var) \
    CTLTYPE_U64, NULL, 0, param_set_multihost_interval, "QU"

#define	param_set_slop_shift_args(var) \
    CTLTYPE_INT, NULL, 0, param_set_slop_shift, "I"

#define	param_set_min_auto_ashift_args(var) \
    CTLTYPE_UINT, NULL, 0, param_set_min_auto_ashift, "IU"

#define	param_set_max_auto_ashift_args(var) \
    CTLTYPE_UINT, NULL, 0, param_set_max_auto_ashift, "IU"

#define	param_set_raidz_impl_args(var) \
    CTLTYPE_STRING, NULL, 0, param_set_raidz_impl, "A"

#define	spa_taskq_read_param_set_args(var) \
    CTLTYPE_STRING, NULL, 0, spa_taskq_read_param, "A"

#define	spa_taskq_write_param_set_args(var) \
    CTLTYPE_STRING, NULL, 0, spa_taskq_write_param, "A"

#define	spa_taskq_free_param_set_args(var) \
    CTLTYPE_STRING, NULL, 0, spa_taskq_free_param, "A"

#define	fletcher_4_param_set_args(var) \
    CTLTYPE_STRING, NULL, 0, fletcher_4_param, "A"

#define	blake3_param_set_args(var) \
    CTLTYPE_STRING, NULL, 0, blake3_param, "A"

#define	sha256_param_set_args(var) \
    CTLTYPE_STRING, NULL, 0, sha256_param, "A"

#define	sha512_param_set_args(var) \
    CTLTYPE_STRING, NULL, 0, sha512_param, "A"

#include <sys/kernel.h>
/*
 * Linux: real module_init / module_exit.
 * FreeBSD: these expand to SYSINIT / SYSUNINIT.
 * NetBSD MODULE(9) has a single openzfs_modcmd hook, so the macros
 * stay empty and kmod_core.c zfs__init() calls the same functions
 * Linux openzfs_init() / FreeBSD SYSINIT run:
 *
 *   zcommon_init   (module_init_early -- kfpu, fletcher_4, simd_stat)
 *   zstd_init      (module_init)
 *   zfs_kmod_init  -> spa_init -> raidz_math_init, chksum_init
 *
 * Linux also calls icp_init (KCF + aes_mod_init + sha2_mod_init).
 * FreeBSD Makefile.bsd does not compile illumos-crypto/kcf, and
 * neither do we.  SHA2/BLAKE3 checksums are not KCF -- they go
 * through generic_impl_init() from chksum_benchmark() in spa_init.
 */
#define	module_init(fn)
#define	module_init_early(fn)
#define	module_exit(fn)

#endif /* SPL_MOD_H */
