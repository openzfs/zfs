AC_DEFUN([ZFS_AC_CONFIG_USER_INITRAMFS], [
	AC_ARG_ENABLE([initramfs],
		AS_HELP_STRING([--enable-initramfs],
		[install initramfs-tools files [[default: check]]]),
		[enable_initramfs=$enableval],
		[enable_initramfs=check])

	AC_ARG_WITH(initramfsdir,
		AS_HELP_STRING([--with-initramfsdir=DIR],
		[install initramfs-tools files in dir [[/usr/share/initramfs-tools]]]), [
	      enable_initramfs=yes
		  initramfsdir="$withval"
		],[initramfsdir=/usr/share/initramfs-tools])

	AS_IF([test "x$initramfs" = xcheck],
	  [AS_IF([test -d "$path"], [enable_initramfs=yes], [enable_initramfs=no])])

	AS_IF([test "x$initramfs" = "xyes"],
	  [RPM_DEFINE_INITRAMFS='--define "_initramfs 1" --define "_initramfsdir $(initramfsdir)"'],
	  [RPM_DEFINE_INITRAMFS=])

	AC_SUBST(initramfsdir)
	AC_SUBST(RPM_DEFINE_INITRAMFS)
	AM_CONDITIONAL([INITRAMFS_ENABLED], [$enable_initramfs])
])
