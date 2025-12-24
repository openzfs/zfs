dnl # SPDX-License-Identifier: CDDL-1.0
dnl #
dnl # Check if libintl and possibly libiconv are needed for gettext() functionality
dnl #
AC_DEFUN([ZFS_AC_CONFIG_USER_GETTEXT], [
    AM_GNU_GETTEXT([external])
])
