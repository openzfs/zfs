AC_DEFUN([ZFS_AC_CONFIG_USER_SELINUX], [
	AC_ARG_WITH([selinux],
		AS_HELP_STRING([--with-selinux=@<:@/usr/share/selinux/devel@:>@],
		[build pam_zfs_key module SELinux policy [[default: check]]]),
		[
			AS_IF([test "x$with_selinux" = xyes],
				with_selinux=/usr/share/selinux/devel)
		],
		[with_selinux=no])

	AS_IF([test "x$with_selinux" != "xno"], [
		AS_IF([test -f "$with_selinux/Makefile"],
			[selinux_makefile="$with_selinux/Makefile"],
			[
				AC_MSG_FAILURE([
	*** SELinux policy development tools missing.
				])
				with_selinux=no
			]
		)
	])
	AC_SUBST(selinux_makefile)
])
