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

zfsconfig=kernel
kernelsrc=
kernelbuild=
splsrc=
splbuild=

ZFS_AC_KERNEL
ZFS_AC_SPL
ZFS_AC_CONFIG
ZFS_AC_LICENSE
ZFS_AC_DEBUG
ZFS_AC_2ARGS_BIO_END_IO_T

# Check for needed userspace bits
AC_CHECK_HEADERS(sys/types.h sys/byteorder.h sys/isa_defs.h \
                 sys/systeminfo.h sys/u8_textprep.h libdiskmgt.h)

AC_CHECK_FUNCS(strlcat strlcpy strnlen issetugid setmntent getexecname)

AC_CHECK_LIB([diskmgt], [libdiskmgt_error],
	[AC_DEFINE([HAVE_LIBDISKMGT], 1,
        [Define to 1 if 'libdiskmgt' library available])])

AC_CHECK_LIB([efi], [efi_alloc_and_init],
	[AC_DEFINE([HAVE_LIBEFI], 1,
	[Define to 1 if 'libefi' library available])])

AC_CHECK_LIB([share], [sa_init],
	[AC_DEFINE([HAVE_LIBSHARE], 1,
	[Define to 1 if 'libshare' library available])])

AC_EGREP_HEADER(ioctl, unistd.h,
	[AC_DEFINE([HAVE_IOCTL_IN_UNISTD_H], 1,
        [Define to 1 if ioctl() is defined in <unistd.h> header file])])

AC_EGREP_HEADER(ioctl, sys/ioctl.h,
	[AC_DEFINE([HAVE_IOCTL_IN_SYS_IOCTL_H], 1,
        [Define to 1 if ioctl() is defined in <sys/ioctl.h> header file])])

AC_EGREP_HEADER(ioctl, stropts.h,
	[AC_DEFINE([HAVE_IOCTL_IN_STROPTS_H], 1,
        [Define to 1 if ioctl() is defined in <stropts.h> header file])])

AC_EGREP_HEADER(strcmp, strings.h, 
	[AC_DEFINE([HAVE_STRCMP_IN_STRINGS_H], 1,
        [Define to 1 if strcmpl() is defined in <strings.h> header file])])

AC_EGREP_HEADER(sysinfo, sys/systeminfo.h, 
	[AC_DEFINE([HAVE_SYSINFO_IN_SYS_SYSTEMINFO_H], 1,
        [Define to 1 if sysinfo() is defined in <sys/systeminfo.h> header file])])

#AC_DEFINE([HAVE_ZVOL], 1, ["Define to 1 to include ZVOL support"])
#AC_DEFINE([HAVE_ZPL], 1, ["Define to 1 to include ZPL support"])
#AC_DEFINE([WANT_FAKE_IOCTL], 1, ["Define to 1 to use fake ioctl() support"])
#AC_DEFINE([HAVE_DM_INUSE_SWAP], 1, ["None"])
#AC_DEFINE([HAVE_UNICODE], 1, ["None"])
#AC_DEFINE([HAVE_INTTYPES], 1, [Define to 1 if unint16 defined in <sys/types.h> header file])

AC_CONFIG_FILES([ Makefile
                  config/Makefile
                  doc/Makefile
                  scripts/Makefile
                  zfs/Makefile
                  zfs/lib/Makefile
                  zfs/lib/libnvpair/include/sys/Makefile
                  zfs/lib/libnvpair/include/Makefile
                  zfs/lib/libnvpair/Makefile
                  zfs/lib/libavl/include/sys/Makefile
                  zfs/lib/libavl/include/Makefile
                  zfs/lib/libavl/Makefile
                  zfs/lib/libuutil/include/Makefile
                  zfs/lib/libuutil/Makefile
                  zfs/lib/libzfs/include/Makefile
                  zfs/lib/libzfs/Makefile
                  zfs/lib/libumem/include/Makefile
                  zfs/lib/libumem/Makefile
                  zfs/lib/libumem/sys/Makefile
                  zfs/lib/libzcommon/include/Makefile
                  zfs/lib/libzcommon/include/sys/fm/fs/Makefile
                  zfs/lib/libzcommon/include/sys/fm/Makefile
                  zfs/lib/libzcommon/include/sys/Makefile
                  zfs/lib/libzcommon/include/sys/fs/Makefile
                  zfs/lib/libzcommon/Makefile
                  zfs/lib/libzpool/Makefile
                  zfs/lib/libspl/include/sys/Makefile
                  zfs/lib/libspl/include/Makefile
                  zfs/lib/libspl/Makefile
                  zfs/lib/libzpios/Makefile
                  zfs/zcmd/ztest/Makefile
                  zfs/zcmd/Makefile
                  zfs/zcmd/zfs/Makefile
                  zfs/zcmd/zdb/Makefile
                  zfs/zcmd/zinject/Makefile
                  zfs/zcmd/zdump/Makefile
                  zfs/zcmd/zpool/Makefile
                  zfs/zcmd/zpios/Makefile
               ])
AC_OUTPUT
