/*
 *  This file is part of the SPL: Solaris Porting Layer.
 *
 *  Copyright (c) 2008 Lawrence Livermore National Security, LLC.
 *  Produced at Lawrence Livermore National Laboratory
 *  Written by:
 *          Brian Behlendorf <behlendorf1@llnl.gov>,
 *          Herb Wartens <wartens2@llnl.gov>,
 *          Jim Garlick <garlick@llnl.gov>
 *  UCRL-CODE-235197
 *
 *  This is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA.
 */

AC_INIT
SPL_AC_META
AC_CONFIG_AUX_DIR([config])
AC_CANONICAL_SYSTEM
AM_INIT_AUTOMAKE([$SPL_META_NAME], [$SPL_META_VERSION])
AC_CONFIG_HEADERS([spl_config.h])
AM_MAINTAINER_MODE

AC_PROG_INSTALL
AC_PROG_CC
AC_PROG_LIBTOOL

LINUX=
LINUX_OBJ=

SPL_AC_KERNEL
SPL_AC_LICENSE
SPL_AC_DEBUG
SPL_AC_DEBUG_KMEM
SPL_AC_DEBUG_MUTEX
SPL_AC_DEBUG_KSTAT
SPL_AC_DEBUG_CALLB
SPL_AC_TYPE_UINTPTR_T
SPL_AC_TYPE_ATOMIC64_T
SPL_AC_3ARGS_INIT_WORK
SPL_AC_2ARGS_REGISTER_SYSCTL
SPL_AC_SET_SHRINKER
SPL_AC_PATH_IN_NAMEIDATA
SPL_AC_TASK_CURR
SPL_AC_CTL_UNNUMBERED
SPL_AC_FLS64
SPL_AC_DEVICE_CREATE
SPL_AC_CLASS_DEVICE_CREATE
SPL_AC_SET_NORMALIZED_TIMESPEC_EXPORT
SPL_AC_SET_NORMALIZED_TIMESPEC_INLINE
SPL_AC_TIMESPEC_SUB
SPL_AC_INIT_UTSNAME
SPL_AC_FDTABLE_HEADER
SPL_AC_FILES_FDTABLE
SPL_AC_UACCESS_HEADER
SPL_AC_KMALLOC_NODE
SPL_AC_MONOTONIC_CLOCK
SPL_AC_INODE_I_MUTEX
SPL_AC_DIV64_64
SPL_AC_3ARGS_ON_EACH_CPU
SPL_AC_FIRST_ONLINE_PGDAT
SPL_AC_NEXT_ONLINE_PGDAT
SPL_AC_NEXT_ZONE
SPL_AC_GET_ZONE_COUNTS

TOPDIR=`/bin/pwd`

# Add "V=1" to KERNELMAKE_PARAMS to enable verbose module build
KERNELMAKE_PARAMS=
KERNELCPPFLAGS="${KERNELCPPFLAGS} -I$TOPDIR -I$TOPDIR/include"

if test "${LINUX_OBJ}" != "${LINUX}"; then
	KERNELMAKE_PARAMS="$KERNELMAKE_PARAMS O=$kernelbuild"
fi

AC_SUBST(KERNELMAKE_PARAMS)
AC_SUBST(KERNELCPPFLAGS)
AC_SUBST(KERNELCFLAGS)

AC_CONFIG_FILES([ Makefile
                  lib/Makefile
                  cmd/Makefile
                  module/Makefile
                  module/spl/Makefile
                  module/splat/Makefile
                  include/Makefile
                  include/sys/Makefile
                  scripts/Makefile
                  scripts/spl.spec
               ])

AC_OUTPUT
