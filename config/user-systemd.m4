AC_DEFUN([ZFS_AC_CONFIG_USER_SYSTEMD], [
	AC_ARG_ENABLE(systemd,
		AS_HELP_STRING([--enable-systemd],
		[install systemd unit/preset files [[default: yes]]]),
		[enable_systemd=$enableval],
		[enable_systemd=check])

	AC_ARG_WITH(systemdunitdir,
		AS_HELP_STRING([--with-systemdunitdir=DIR],
		[install systemd unit files in dir [[$libdir/systemd/system]]]),
		systemdunitdir=$withval,systemdunitdir=$libdir/systemd/system)

	AC_ARG_WITH(systemdpresetdir,
		AS_HELP_STRING([--with-systemdpresetdir=DIR],
		[install systemd preset files in dir [[$libdir/systemd/system-preset]]]),
		systemdpresetdir=$withval,systemdpresetdir=$libdir/systemd/system-preset)

	AC_ARG_WITH(systemdmodulesloaddir,
		AS_HELP_STRING([--with-systemdmodulesloaddir=DIR],
		[install systemd module load files into dir [[$libdir/modules-load.d]]]),
		systemdmodulesloaddir=$withval,systemdmodulesloaddir=$libdir/modules-load.d)

	AC_ARG_WITH(systemdgeneratordir,
		AS_HELP_STRING([--with-systemdgeneratordir=DIR],
		[install systemd generators in dir [[$libdir/systemd/system-generators]]]),
		systemdgeneratordir=$withval,systemdgeneratordir=$libdir/systemd/system-generators)

	AS_IF([test "x$enable_systemd" = xcheck], [
		AS_IF([systemctl --version >/dev/null 2>&1],
			[enable_systemd=yes],
			[enable_systemd=no])
	])

	AC_MSG_CHECKING(for systemd support)
	AC_MSG_RESULT([$enable_systemd])

	AS_IF([test "x$enable_systemd" = xyes], [
		DEFINE_SYSTEMD='--with systemd --define "_unitdir $(systemdunitdir)" --define "_presetdir $(systemdpresetdir)" --define "_generatordir $(systemdgeneratordir)"'
		modulesloaddir=$systemdmodulesloaddir
	],[
		DEFINE_SYSTEMD='--without systemd'
	])

	ZFS_INIT_SYSTEMD=$enable_systemd
	ZFS_WANT_MODULES_LOAD_D=$enable_systemd

	AC_SUBST(DEFINE_SYSTEMD)
	AC_SUBST(systemdunitdir)
	AC_SUBST(systemdpresetdir)
	AC_SUBST(systemdgeneratordir)
	AC_SUBST(modulesloaddir)
])
