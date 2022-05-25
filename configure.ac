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
# Remove default macros from config.h:
#   PACKAGE, PACKAGE_{BUGREPORT,NAME,STRING,TARNAME,VERSION}, STDC_HEADERS, VERSION
AC_CONFIG_HEADERS([zfs_config.h], [
	sed -nri~ -e '/^$/be' -e 'N;N;/#define (PACKAGE|VERSION|STDC_HEADERS)/d' -e ':e' -e 'p' zfs_config.h && rm zfs_config.h~ || exit])

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
	lib/libzfs_core/libzfs_core.pc
	lib/libzfsbootenv/libzfsbootenv.pc
	module/Kbuild
	module/Makefile
	rpm/generic/zfs-dkms.spec
	rpm/generic/zfs-kmod.spec
	rpm/generic/zfs.spec
	rpm/redhat/zfs-dkms.spec
	rpm/redhat/zfs-kmod.spec
	rpm/redhat/zfs.spec
	tests/zfs-tests/tests/Makefile
	zfs.release
])


AC_OUTPUT
