#
# This file is part of the ZFS Linux port.
#
# Copyright (c) 2008 Lawrence Livermore National Security, LLC.
# Produced at Lawrence Livermore National Laboratory
# Written by:
#         Brian Behlendorf <behlendorf1@llnl.gov>,
#         Herb Wartens <wartens2@llnl.gov>,
#         Jim Garlick <garlick@llnl.gov>
# LLNL-CODE-403049
#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License, Version 1.0 only
# (the "License").  You may not use this file except in compliance
# with the License.
#
# You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
# or http://www.opensolaris.org/os/licensing.
# See the License for the specific language governing permissions
# and limitations under the License.
#
# When distributing Covered Code, include this CDDL HEADER in each
# file and include the License file at usr/src/OPENSOLARIS.LICENSE.
# If applicable, add the following below this CDDL HEADER, with the
# fields enclosed by brackets "[]" replaced with your own identifying
# information: Portions Copyright [yyyy] [name of copyright owner]
#
# CDDL HEADER END
#

AC_INIT
AC_LANG(C)
ZFS_AC_META
AC_CONFIG_AUX_DIR([config])
AC_CANONICAL_SYSTEM
AM_INIT_AUTOMAKE([$ZFS_META_NAME], [$ZFS_META_VERSION])
AC_CONFIG_HEADERS([zfs_config.h])
AM_MAINTAINER_MODE

AC_PROG_INSTALL
AC_PROG_CC
AC_PROG_LIBTOOL
AM_PROG_AS

zfsconfig=all
kernelsrc=
kernelbuild=
splsrc=
splbuild=

ZFS_AC_KERNEL
ZFS_AC_SPL
ZFS_AC_CONFIG
ZFS_AC_LICENSE
ZFS_AC_DEBUG

AC_CONFIG_FILES([ 
	Makefile
	config/Makefile
	doc/Makefile
	scripts/Makefile
	lib/Makefile
	lib/libavl/Makefile
	lib/libnvpair/Makefile
	lib/libunicode/Makefile
	lib/libuutil/Makefile
	lib/libzpool/Makefile
	lib/libzfs/Makefile
	cmd/Makefile
	cmd/zdb/Makefile
	cmd/zfs/Makefile
	cmd/zinject/Makefile
	cmd/zpool/Makefile
	cmd/ztest/Makefile
	cmd/zpios/Makefile
	module/Makefile
	module/avl/Makefile
	module/avl/include/Makefile
	module/avl/include/sys/Makefile
	module/nvpair/Makefile
	module/nvpair/include/Makefile
	module/nvpair/include/sys/Makefile
	module/unicode/Makefile
	module/unicode/include/Makefile
	module/unicode/include/sys/Makefile
	module/zcommon/Makefile
	module/zcommon/include/Makefile
	module/zcommon/include/sys/Makefile
	module/zcommon/include/sys/fs/Makefile
	module/zcommon/include/sys/fm/Makefile
	module/zcommon/include/sys/fm/fs/Makefile
	module/zfs/Makefile
	module/zpios/Makefile
])
AC_OUTPUT
