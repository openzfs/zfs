// SPDX-License-Identifier: CDDL-1.0
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
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this
 * source.  A copy of the CDDL is also available via the Internet at
 * https://opensource.org/license/CDDL-1.0.
 */

AC_INIT(m4_esyscmd(awk '/^Name:/ {printf $2}' META),
	m4_esyscmd(awk '/^Version:/ {printf $2}' META))
CFGOPTS="$*"
AC_LANG(C)
ZFS_AC_META
AC_CONFIG_AUX_DIR([config])
AC_CONFIG_MACRO_DIR([config])
AC_CANONICAL_TARGET
AM_MAINTAINER_MODE
m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES([yes])])
AM_INIT_AUTOMAKE([subdir-objects foreign tar-pax])
# Remove default macros from config.h:
#   PACKAGE, PACKAGE_{BUGREPORT,NAME,STRING,TARNAME,VERSION}, STDC_HEADERS, VERSION
AC_CONFIG_HEADERS([zfs_config.h], [
	$SED -nri~ -e '/^$/be' -e 'N;N;/#define (PACKAGE|VERSION|STDC_HEADERS)/d' -e ':e' -e 'p' zfs_config.h && rm zfs_config.h~ || exit])

LT_INIT
AC_PROG_INSTALL
AC_PROG_CC
AC_PROG_LN_S
PKG_PROG_PKG_CONFIG
AM_PROG_AS
AM_PROG_CC_C_O
AX_PTHREAD
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
ZFS_AC_METASLAB_TRACING
ZFS_AC_OBJTOOL_WERROR

AC_CONFIG_FILES([
	contrib/debian/rules
	contrib/debian/changelog
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

AC_CONFIG_FILES([scripts/objtool-wrapper], [chmod +x scripts/objtool-wrapper])

AC_OUTPUT

ZFS_AC_KERNEL_VERSION_WARNING
