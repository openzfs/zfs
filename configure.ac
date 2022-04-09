/*
 * This file is part of OpenZFS.
 *
 * Copyright (c) 2009 Lawrence Livermore National Security, LLC.
 * Produced at Lawrence Livermore National Laboratory
 * Written by:
 *         Brian Behlendorf <behlendorf1@llnl.gov>,
 *         Herb Wartens <wartens2@llnl.gov>,
 *         Jim Garlick <garlick@llnl.gov>
 * LLNL-CODE-403049
 *
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License, Version 1.0 only
 * (the "License").  You may not use this file except in compliance
 * with the License.
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

AC_INIT(m4_esyscmd(awk '/^Name:/ {printf $2}' META),
	m4_esyscmd(awk '/^Version:/ {printf $2}' META))
AC_LANG(C)
ZFS_AC_META
AC_CONFIG_AUX_DIR([config])
AC_CONFIG_MACRO_DIR([config])
AC_CANONICAL_TARGET
AM_MAINTAINER_MODE
m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES([yes])])
AM_INIT_AUTOMAKE([subdir-objects foreign])
AC_CONFIG_HEADERS([zfs_config.h], [
	(mv zfs_config.h zfs_config.h.tmp &&
	awk -f ${ac_srcdir}/config/config.awk zfs_config.h.tmp >zfs_config.h &&
	rm zfs_config.h.tmp) || exit 1])

LT_INIT
AC_PROG_INSTALL
AC_PROG_CC
AC_PROG_LN_S
PKG_PROG_PKG_CONFIG
AM_PROG_AS
AM_PROG_CC_C_O
AX_CODE_COVERAGE
_AM_PROG_TAR(pax)

ZFS_AC_LICENSE
ZFS_AC_CONFIG
ZFS_AC_PACKAGE
ZFS_AC_DEBUG
ZFS_AC_DEBUGINFO
ZFS_AC_DEBUG_KMEM
ZFS_AC_DEBUG_KMEM_TRACKING
ZFS_AC_DEBUG_INVARIANTS

AC_CONFIG_FILES([
	Makefile
	include/Makefile
	lib/libzfs/libzfs.pc
	lib/libzfsbootenv/libzfsbootenv.pc
	lib/libzfs_core/libzfs_core.pc
	module/Kbuild
	module/Makefile
	rpm/generic/zfs-dkms.spec
	rpm/generic/zfs-kmod.spec
	rpm/generic/zfs.spec
	rpm/redhat/zfs-dkms.spec
	rpm/redhat/zfs-kmod.spec
	rpm/redhat/zfs.spec
	tests/Makefile
	tests/runfiles/Makefile
	tests/test-runner/Makefile
	tests/test-runner/bin/Makefile
	tests/test-runner/include/Makefile
	tests/zfs-tests/Makefile
	tests/zfs-tests/callbacks/Makefile
	tests/zfs-tests/cmd/Makefile
	tests/zfs-tests/include/Makefile
	tests/zfs-tests/tests/Makefile
	tests/zfs-tests/tests/functional/Makefile
	tests/zfs-tests/tests/functional/acl/Makefile
	tests/zfs-tests/tests/functional/acl/off/Makefile
	tests/zfs-tests/tests/functional/acl/posix/Makefile
	tests/zfs-tests/tests/functional/acl/posix-sa/Makefile
	tests/zfs-tests/tests/functional/alloc_class/Makefile
	tests/zfs-tests/tests/functional/arc/Makefile
	tests/zfs-tests/tests/functional/atime/Makefile
	tests/zfs-tests/tests/functional/bootfs/Makefile
	tests/zfs-tests/tests/functional/btree/Makefile
	tests/zfs-tests/tests/functional/cache/Makefile
	tests/zfs-tests/tests/functional/cachefile/Makefile
	tests/zfs-tests/tests/functional/casenorm/Makefile
	tests/zfs-tests/tests/functional/channel_program/Makefile
	tests/zfs-tests/tests/functional/channel_program/lua_core/Makefile
	tests/zfs-tests/tests/functional/channel_program/synctask_core/Makefile
	tests/zfs-tests/tests/functional/chattr/Makefile
	tests/zfs-tests/tests/functional/checksum/Makefile
	tests/zfs-tests/tests/functional/clean_mirror/Makefile
	tests/zfs-tests/tests/functional/cli_root/Makefile
	tests/zfs-tests/tests/functional/cli_root/zdb/Makefile
	tests/zfs-tests/tests/functional/cli_root/zfs/Makefile
	tests/zfs-tests/tests/functional/cli_root/zfs_bookmark/Makefile
	tests/zfs-tests/tests/functional/cli_root/zfs_change-key/Makefile
	tests/zfs-tests/tests/functional/cli_root/zfs_clone/Makefile
	tests/zfs-tests/tests/functional/cli_root/zfs_copies/Makefile
	tests/zfs-tests/tests/functional/cli_root/zfs_create/Makefile
	tests/zfs-tests/tests/functional/cli_root/zfs_destroy/Makefile
	tests/zfs-tests/tests/functional/cli_root/zfs_diff/Makefile
	tests/zfs-tests/tests/functional/cli_root/zfs_get/Makefile
	tests/zfs-tests/tests/functional/cli_root/zfs_ids_to_path/Makefile
	tests/zfs-tests/tests/functional/cli_root/zfs_inherit/Makefile
	tests/zfs-tests/tests/functional/cli_root/zfs_jail/Makefile
	tests/zfs-tests/tests/functional/cli_root/zfs_load-key/Makefile
	tests/zfs-tests/tests/functional/cli_root/zfs_mount/Makefile
	tests/zfs-tests/tests/functional/cli_root/zfs_program/Makefile
	tests/zfs-tests/tests/functional/cli_root/zfs_promote/Makefile
	tests/zfs-tests/tests/functional/cli_root/zfs_property/Makefile
	tests/zfs-tests/tests/functional/cli_root/zfs_receive/Makefile
	tests/zfs-tests/tests/functional/cli_root/zfs_rename/Makefile
	tests/zfs-tests/tests/functional/cli_root/zfs_reservation/Makefile
	tests/zfs-tests/tests/functional/cli_root/zfs_rollback/Makefile
	tests/zfs-tests/tests/functional/cli_root/zfs_send/Makefile
	tests/zfs-tests/tests/functional/cli_root/zfs_set/Makefile
	tests/zfs-tests/tests/functional/cli_root/zfs_share/Makefile
	tests/zfs-tests/tests/functional/cli_root/zfs_snapshot/Makefile
	tests/zfs-tests/tests/functional/cli_root/zfs_sysfs/Makefile
	tests/zfs-tests/tests/functional/cli_root/zfs_unload-key/Makefile
	tests/zfs-tests/tests/functional/cli_root/zfs_unmount/Makefile
	tests/zfs-tests/tests/functional/cli_root/zfs_unshare/Makefile
	tests/zfs-tests/tests/functional/cli_root/zfs_upgrade/Makefile
	tests/zfs-tests/tests/functional/cli_root/zfs_wait/Makefile
	tests/zfs-tests/tests/functional/cli_root/zhack/Makefile
	tests/zfs-tests/tests/functional/cli_root/zpool/Makefile
	tests/zfs-tests/tests/functional/cli_root/zpool_add/Makefile
	tests/zfs-tests/tests/functional/cli_root/zpool_attach/Makefile
	tests/zfs-tests/tests/functional/cli_root/zpool_clear/Makefile
	tests/zfs-tests/tests/functional/cli_root/zpool_create/Makefile
	tests/zfs-tests/tests/functional/cli_root/zpool_destroy/Makefile
	tests/zfs-tests/tests/functional/cli_root/zpool_detach/Makefile
	tests/zfs-tests/tests/functional/cli_root/zpool_events/Makefile
	tests/zfs-tests/tests/functional/cli_root/zpool_expand/Makefile
	tests/zfs-tests/tests/functional/cli_root/zpool_export/Makefile
	tests/zfs-tests/tests/functional/cli_root/zpool_get/Makefile
	tests/zfs-tests/tests/functional/cli_root/zpool_history/Makefile
	tests/zfs-tests/tests/functional/cli_root/zpool_import/Makefile
	tests/zfs-tests/tests/functional/cli_root/zpool_import/blockfiles/Makefile
	tests/zfs-tests/tests/functional/cli_root/zpool_initialize/Makefile
	tests/zfs-tests/tests/functional/cli_root/zpool_labelclear/Makefile
	tests/zfs-tests/tests/functional/cli_root/zpool_offline/Makefile
	tests/zfs-tests/tests/functional/cli_root/zpool_online/Makefile
	tests/zfs-tests/tests/functional/cli_root/zpool_remove/Makefile
	tests/zfs-tests/tests/functional/cli_root/zpool_reopen/Makefile
	tests/zfs-tests/tests/functional/cli_root/zpool_replace/Makefile
	tests/zfs-tests/tests/functional/cli_root/zpool_resilver/Makefile
	tests/zfs-tests/tests/functional/cli_root/zpool_scrub/Makefile
	tests/zfs-tests/tests/functional/cli_root/zpool_set/Makefile
	tests/zfs-tests/tests/functional/cli_root/zpool_split/Makefile
	tests/zfs-tests/tests/functional/cli_root/zpool_status/Makefile
	tests/zfs-tests/tests/functional/cli_root/zpool_sync/Makefile
	tests/zfs-tests/tests/functional/cli_root/zpool_trim/Makefile
	tests/zfs-tests/tests/functional/cli_root/zpool_upgrade/Makefile
	tests/zfs-tests/tests/functional/cli_root/zpool_upgrade/blockfiles/Makefile
	tests/zfs-tests/tests/functional/cli_root/zpool_wait/Makefile
	tests/zfs-tests/tests/functional/cli_root/zpool_wait/scan/Makefile
	tests/zfs-tests/tests/functional/cli_user/Makefile
	tests/zfs-tests/tests/functional/cli_user/misc/Makefile
	tests/zfs-tests/tests/functional/cli_user/zfs_list/Makefile
	tests/zfs-tests/tests/functional/cli_user/zpool_iostat/Makefile
	tests/zfs-tests/tests/functional/cli_user/zpool_list/Makefile
	tests/zfs-tests/tests/functional/cli_user/zpool_status/Makefile
	tests/zfs-tests/tests/functional/compression/Makefile
	tests/zfs-tests/tests/functional/cp_files/Makefile
	tests/zfs-tests/tests/functional/crtime/Makefile
	tests/zfs-tests/tests/functional/ctime/Makefile
	tests/zfs-tests/tests/functional/deadman/Makefile
	tests/zfs-tests/tests/functional/delegate/Makefile
	tests/zfs-tests/tests/functional/devices/Makefile
	tests/zfs-tests/tests/functional/dos_attributes/Makefile
	tests/zfs-tests/tests/functional/events/Makefile
	tests/zfs-tests/tests/functional/exec/Makefile
	tests/zfs-tests/tests/functional/fallocate/Makefile
	tests/zfs-tests/tests/functional/fault/Makefile
	tests/zfs-tests/tests/functional/features/Makefile
	tests/zfs-tests/tests/functional/features/async_destroy/Makefile
	tests/zfs-tests/tests/functional/features/large_dnode/Makefile
	tests/zfs-tests/tests/functional/grow/Makefile
	tests/zfs-tests/tests/functional/history/Makefile
	tests/zfs-tests/tests/functional/hkdf/Makefile
	tests/zfs-tests/tests/functional/inheritance/Makefile
	tests/zfs-tests/tests/functional/inuse/Makefile
	tests/zfs-tests/tests/functional/io/Makefile
	tests/zfs-tests/tests/functional/l2arc/Makefile
	tests/zfs-tests/tests/functional/large_files/Makefile
	tests/zfs-tests/tests/functional/largest_pool/Makefile
	tests/zfs-tests/tests/functional/libzfs/Makefile
	tests/zfs-tests/tests/functional/limits/Makefile
	tests/zfs-tests/tests/functional/link_count/Makefile
	tests/zfs-tests/tests/functional/log_spacemap/Makefile
	tests/zfs-tests/tests/functional/migration/Makefile
	tests/zfs-tests/tests/functional/mmap/Makefile
	tests/zfs-tests/tests/functional/mmp/Makefile
	tests/zfs-tests/tests/functional/mount/Makefile
	tests/zfs-tests/tests/functional/mv_files/Makefile
	tests/zfs-tests/tests/functional/nestedfs/Makefile
	tests/zfs-tests/tests/functional/no_space/Makefile
	tests/zfs-tests/tests/functional/nopwrite/Makefile
	tests/zfs-tests/tests/functional/online_offline/Makefile
	tests/zfs-tests/tests/functional/pam/Makefile
	tests/zfs-tests/tests/functional/pool_checkpoint/Makefile
	tests/zfs-tests/tests/functional/pool_names/Makefile
	tests/zfs-tests/tests/functional/poolversion/Makefile
	tests/zfs-tests/tests/functional/privilege/Makefile
	tests/zfs-tests/tests/functional/procfs/Makefile
	tests/zfs-tests/tests/functional/projectquota/Makefile
	tests/zfs-tests/tests/functional/pyzfs/Makefile
	tests/zfs-tests/tests/functional/quota/Makefile
	tests/zfs-tests/tests/functional/raidz/Makefile
	tests/zfs-tests/tests/functional/redacted_send/Makefile
	tests/zfs-tests/tests/functional/redundancy/Makefile
	tests/zfs-tests/tests/functional/refquota/Makefile
	tests/zfs-tests/tests/functional/refreserv/Makefile
	tests/zfs-tests/tests/functional/removal/Makefile
	tests/zfs-tests/tests/functional/rename_dirs/Makefile
	tests/zfs-tests/tests/functional/replacement/Makefile
	tests/zfs-tests/tests/functional/reservation/Makefile
	tests/zfs-tests/tests/functional/rootpool/Makefile
	tests/zfs-tests/tests/functional/rsend/Makefile
	tests/zfs-tests/tests/functional/scrub_mirror/Makefile
	tests/zfs-tests/tests/functional/simd/Makefile
	tests/zfs-tests/tests/functional/slog/Makefile
	tests/zfs-tests/tests/functional/snapshot/Makefile
	tests/zfs-tests/tests/functional/snapused/Makefile
	tests/zfs-tests/tests/functional/sparse/Makefile
	tests/zfs-tests/tests/functional/stat/Makefile
	tests/zfs-tests/tests/functional/suid/Makefile
	tests/zfs-tests/tests/functional/threadsappend/Makefile
	tests/zfs-tests/tests/functional/tmpfile/Makefile
	tests/zfs-tests/tests/functional/trim/Makefile
	tests/zfs-tests/tests/functional/truncate/Makefile
	tests/zfs-tests/tests/functional/upgrade/Makefile
	tests/zfs-tests/tests/functional/user_namespace/Makefile
	tests/zfs-tests/tests/functional/userquota/Makefile
	tests/zfs-tests/tests/functional/vdev_zaps/Makefile
	tests/zfs-tests/tests/functional/write_dirs/Makefile
	tests/zfs-tests/tests/functional/xattr/Makefile
	tests/zfs-tests/tests/functional/zpool_influxdb/Makefile
	tests/zfs-tests/tests/functional/zvol/Makefile
	tests/zfs-tests/tests/functional/zvol/zvol_ENOSPC/Makefile
	tests/zfs-tests/tests/functional/zvol/zvol_cli/Makefile
	tests/zfs-tests/tests/functional/zvol/zvol_misc/Makefile
	tests/zfs-tests/tests/functional/zvol/zvol_swap/Makefile
	tests/zfs-tests/tests/perf/Makefile
	tests/zfs-tests/tests/perf/fio/Makefile
	tests/zfs-tests/tests/perf/regression/Makefile
	tests/zfs-tests/tests/perf/scripts/Makefile
	tests/zfs-tests/tests/stress/Makefile
	zfs.release
])


AC_OUTPUT
