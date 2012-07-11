###############################################################################
# SPL AutoConf Configuration
###############################################################################
# Copyright (C) 2007-2010 Lawrence Livermore National Security, LLC.
# Copyright (C) 2007 The Regents of the University of California.
# Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
# Written by Brian Behlendorf <behlendorf1@llnl.gov>.
# UCRL-CODE-235197
#
# This file is part of the SPL, Solaris Porting Layer.
# For details, see <http://github.com/behlendorf/spl/>.
#
# The SPL is free software; you can redistribute it and/or modify it
# under the terms of the GNU General Public License as published by the
# Free Software Foundation; either version 2 of the License, or (at your
# option) any later version.
#
# The SPL is distributed in the hope that it will be useful, but WITHOUT
# ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
# FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
# for more details.
#
# You should have received a copy of the GNU General Public License along
# with the SPL.  If not, see <http://www.gnu.org/licenses/>.
###############################################################################

AC_INIT
AC_LANG(C)
SPL_AC_META
AC_CONFIG_AUX_DIR([config])
AC_CANONICAL_SYSTEM
AM_MAINTAINER_MODE
AM_SILENT_RULES
AM_INIT_AUTOMAKE([$SPL_META_NAME], [$SPL_META_VERSION])
AC_CONFIG_HEADERS([spl_config.h], [
	(mv spl_config.h spl_config.h.tmp &&
	awk -f ${ac_srcdir}/config/config.awk spl_config.h.tmp >spl_config.h &&
	rm spl_config.h.tmp) || exit 1])

AC_PROG_INSTALL
AC_PROG_CC
AC_PROG_LIBTOOL

SPL_AC_LICENSE
SPL_AC_PACKAGE
SPL_AC_CONFIG

AC_CONFIG_FILES([
	Makefile
	lib/Makefile
	cmd/Makefile
	module/Makefile
	module/spl/Makefile
	module/splat/Makefile
	include/Makefile
	scripts/Makefile
	spl.spec
	spl-modules.spec
	PKGBUILD-spl
	PKGBUILD-spl-modules
	spl.release
])

AC_OUTPUT
