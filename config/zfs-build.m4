AC_DEFUN([ZFS_AC_LICENSE], [
	AC_MSG_CHECKING([zfs author])
	AC_MSG_RESULT([$ZFS_META_AUTHOR])

	AC_MSG_CHECKING([zfs license])
	AC_MSG_RESULT([$ZFS_META_LICENSE])
])

AC_DEFUN([ZFS_AC_DEBUG_ENABLE], [
	DEBUG_CFLAGS="-Werror"
	DEBUG_CPPFLAGS="-DDEBUG -UNDEBUG"
	DEBUG_LDFLAGS=""
	DEBUG_ZFS="_with_debug"
	AC_DEFINE(ZFS_DEBUG, 1, [zfs debugging enabled])

	KERNEL_DEBUG_CFLAGS="-Werror"
	KERNEL_DEBUG_CPPFLAGS="-DDEBUG -UNDEBUG"
])

AC_DEFUN([ZFS_AC_DEBUG_DISABLE], [
	DEBUG_CFLAGS=""
	DEBUG_CPPFLAGS="-UDEBUG -DNDEBUG"
	DEBUG_LDFLAGS=""
	DEBUG_ZFS="_without_debug"

	KERNEL_DEBUG_CFLAGS=""
	KERNEL_DEBUG_CPPFLAGS="-UDEBUG -DNDEBUG"
])

dnl #
dnl # When debugging is enabled:
dnl # - Enable all ASSERTs (-DDEBUG)
dnl # - Promote all compiler warnings to errors (-Werror)
dnl #
AC_DEFUN([ZFS_AC_DEBUG], [
	AC_MSG_CHECKING([whether assertion support will be enabled])
	AC_ARG_ENABLE([debug],
		[AS_HELP_STRING([--enable-debug],
		[Enable compiler and code assertions @<:@default=no@:>@])],
		[],
		[enable_debug=no])

	AS_CASE(["x$enable_debug"],
		["xyes"],
		[ZFS_AC_DEBUG_ENABLE],
		["xno"],
		[ZFS_AC_DEBUG_DISABLE],
		[AC_MSG_ERROR([Unknown option $enable_debug])])

	AC_SUBST(DEBUG_CFLAGS)
	AC_SUBST(DEBUG_CPPFLAGS)
	AC_SUBST(DEBUG_LDFLAGS)
	AC_SUBST(DEBUG_ZFS)

	AC_SUBST(KERNEL_DEBUG_CFLAGS)
	AC_SUBST(KERNEL_DEBUG_CPPFLAGS)

	AC_MSG_RESULT([$enable_debug])
])

AC_DEFUN([ZFS_AC_DEBUGINFO_ENABLE], [
	DEBUG_CFLAGS="$DEBUG_CFLAGS -g -fno-inline $NO_IPA_SRA"

	KERNEL_DEBUG_CFLAGS="$KERNEL_DEBUG_CFLAGS -fno-inline $NO_IPA_SRA"
	KERNEL_MAKE="$KERNEL_MAKE CONFIG_DEBUG_INFO=y"

	DEBUGINFO_ZFS="_with_debuginfo"
])

AC_DEFUN([ZFS_AC_DEBUGINFO_DISABLE], [
	DEBUGINFO_ZFS="_without_debuginfo"
])

AC_DEFUN([ZFS_AC_DEBUGINFO], [
	AC_MSG_CHECKING([whether debuginfo support will be forced])
	AC_ARG_ENABLE([debuginfo],
		[AS_HELP_STRING([--enable-debuginfo],
		[Force generation of debuginfo @<:@default=no@:>@])],
		[],
		[enable_debuginfo=no])

	AS_CASE(["x$enable_debuginfo"],
		["xyes"],
		[ZFS_AC_DEBUGINFO_ENABLE],
		["xno"],
		[ZFS_AC_DEBUGINFO_DISABLE],
		[AC_MSG_ERROR([Unknown option $enable_debuginfo])])

	AC_SUBST(DEBUG_CFLAGS)
	AC_SUBST(DEBUGINFO_ZFS)

	AC_SUBST(KERNEL_DEBUG_CFLAGS)
	AC_SUBST(KERNEL_MAKE)

	AC_MSG_RESULT([$enable_debuginfo])
])

dnl #
dnl # Disabled by default, provides basic memory tracking.  Track the total
dnl # number of bytes allocated with kmem_alloc() and freed with kmem_free().
dnl # Then at module unload time if any bytes were leaked it will be reported
dnl # on the console.
dnl #
AC_DEFUN([ZFS_AC_DEBUG_KMEM], [
	AC_MSG_CHECKING([whether basic kmem accounting is enabled])
	AC_ARG_ENABLE([debug-kmem],
		[AS_HELP_STRING([--enable-debug-kmem],
		[Enable basic kmem accounting @<:@default=no@:>@])],
		[],
		[enable_debug_kmem=no])

	AS_IF([test "x$enable_debug_kmem" = xyes], [
		KERNEL_DEBUG_CPPFLAGS="${KERNEL_DEBUG_CPPFLAGS} -DDEBUG_KMEM"
		DEBUG_KMEM_ZFS="_with_debug_kmem"
	], [
		DEBUG_KMEM_ZFS="_without_debug_kmem"
	])

	AC_SUBST(KERNEL_DEBUG_CPPFLAGS)
	AC_SUBST(DEBUG_KMEM_ZFS)

	AC_MSG_RESULT([$enable_debug_kmem])
])

dnl #
dnl # Disabled by default, provides detailed memory tracking.  This feature
dnl # also requires --enable-debug-kmem to be set.  When enabled not only will
dnl # total bytes be tracked but also the location of every kmem_alloc() and
dnl # kmem_free().  When the module is unloaded a list of all leaked addresses
dnl # and where they were allocated will be dumped to the console.  Enabling
dnl # this feature has a significant impact on performance but it makes finding
dnl # memory leaks straight forward.
dnl #
AC_DEFUN([ZFS_AC_DEBUG_KMEM_TRACKING], [
	AC_MSG_CHECKING([whether detailed kmem tracking is enabled])
	AC_ARG_ENABLE([debug-kmem-tracking],
		[AS_HELP_STRING([--enable-debug-kmem-tracking],
		[Enable detailed kmem tracking  @<:@default=no@:>@])],
		[],
		[enable_debug_kmem_tracking=no])

	AS_IF([test "x$enable_debug_kmem_tracking" = xyes], [
		KERNEL_DEBUG_CPPFLAGS="${KERNEL_DEBUG_CPPFLAGS} -DDEBUG_KMEM_TRACKING"
		DEBUG_KMEM_TRACKING_ZFS="_with_debug_kmem_tracking"
	], [
		DEBUG_KMEM_TRACKING_ZFS="_without_debug_kmem_tracking"
	])

	AC_SUBST(KERNEL_DEBUG_CPPFLAGS)
	AC_SUBST(DEBUG_KMEM_TRACKING_ZFS)

	AC_MSG_RESULT([$enable_debug_kmem_tracking])
])

AC_DEFUN([ZFS_AC_CONFIG_ALWAYS], [
	ZFS_AC_CONFIG_ALWAYS_CC_NO_UNUSED_BUT_SET_VARIABLE
	ZFS_AC_CONFIG_ALWAYS_CC_NO_BOOL_COMPARE
	ZFS_AC_CONFIG_ALWAYS_CC_FRAME_LARGER_THAN
	ZFS_AC_CONFIG_ALWAYS_CC_NO_FORMAT_TRUNCATION
	ZFS_AC_CONFIG_ALWAYS_CC_NO_FORMAT_ZERO_LENGTH
	ZFS_AC_CONFIG_ALWAYS_CC_NO_OMIT_FRAME_POINTER
	ZFS_AC_CONFIG_ALWAYS_CC_NO_IPA_SRA
	ZFS_AC_CONFIG_ALWAYS_CC_ASAN
	ZFS_AC_CONFIG_ALWAYS_TOOLCHAIN_SIMD
	ZFS_AC_CONFIG_ALWAYS_SYSTEM
	ZFS_AC_CONFIG_ALWAYS_ARCH
	ZFS_AC_CONFIG_ALWAYS_PYTHON
	ZFS_AC_CONFIG_ALWAYS_PYZFS
	ZFS_AC_CONFIG_ALWAYS_SED
])

AC_DEFUN([ZFS_AC_CONFIG], [

        dnl # Remove the previous build test directory.
        rm -Rf build

	ZFS_CONFIG=all
	AC_ARG_WITH([config],
		AS_HELP_STRING([--with-config=CONFIG],
		[Config file 'kernel|user|all|srpm']),
		[ZFS_CONFIG="$withval"])
	AC_ARG_ENABLE([linux-builtin],
		[AS_HELP_STRING([--enable-linux-builtin],
		[Configure for builtin in-tree kernel modules @<:@default=no@:>@])],
		[],
		[enable_linux_builtin=no])

	AC_MSG_CHECKING([zfs config])
	AC_MSG_RESULT([$ZFS_CONFIG]);
	AC_SUBST(ZFS_CONFIG)

	ZFS_AC_CONFIG_ALWAYS


	AM_COND_IF([BUILD_LINUX], [
		AC_ARG_VAR([TEST_JOBS],
		    [simultaneous jobs during configure (defaults to $(nproc))])
		if test "x$ac_cv_env_TEST_JOBS_set" != "xset"; then
			TEST_JOBS=$(nproc)
		fi
		AC_SUBST(TEST_JOBS)
	])

	case "$ZFS_CONFIG" in
		kernel) ZFS_AC_CONFIG_KERNEL ;;
		user)	ZFS_AC_CONFIG_USER   ;;
		all)    ZFS_AC_CONFIG_USER
			ZFS_AC_CONFIG_KERNEL ;;
		srpm)                        ;;
		*)
		AC_MSG_RESULT([Error!])
		AC_MSG_ERROR([Bad value "$ZFS_CONFIG" for --with-config,
		              user kernel|user|all|srpm]) ;;
	esac

	AM_CONDITIONAL([CONFIG_USER],
	    [test "$ZFS_CONFIG" = user -o "$ZFS_CONFIG" = all])
	AM_CONDITIONAL([CONFIG_KERNEL],
	    [test "$ZFS_CONFIG" = kernel -o "$ZFS_CONFIG" = all] &&
	    [test "x$enable_linux_builtin" != xyes ])
	AM_CONDITIONAL([CONFIG_QAT],
	    [test "$ZFS_CONFIG" = kernel -o "$ZFS_CONFIG" = all] &&
	    [test "x$qatsrc" != x ])
	AM_CONDITIONAL([WANT_DEVNAME2DEVID], [test "x$user_libudev" = xyes ])
	AM_CONDITIONAL([WANT_MMAP_LIBAIO], [test "x$user_libaio" = xyes ])
	AM_CONDITIONAL([PAM_ZFS_ENABLED], [test "x$enable_pam" = xyes])
])

dnl #
dnl # Check for rpm+rpmbuild to build RPM packages.  If these tools
dnl # are missing it is non-fatal but you will not be able to build
dnl # RPM packages and will be warned if you try too.
dnl #
dnl # By default the generic spec file will be used because it requires
dnl # minimal dependencies.  Distribution specific spec files can be
dnl # placed under the 'rpm/<distribution>' directory and enabled using
dnl # the --with-spec=<distribution> configure option.
dnl #
AC_DEFUN([ZFS_AC_RPM], [
	RPM=rpm
	RPMBUILD=rpmbuild

	AC_MSG_CHECKING([whether $RPM is available])
	AS_IF([tmp=$($RPM --version 2>/dev/null)], [
		RPM_VERSION=$(echo $tmp | $AWK '/RPM/ { print $[3] }')
		HAVE_RPM=yes
		AC_MSG_RESULT([$HAVE_RPM ($RPM_VERSION)])
	],[
		HAVE_RPM=no
		AC_MSG_RESULT([$HAVE_RPM])
	])

	AC_MSG_CHECKING([whether $RPMBUILD is available])
	AS_IF([tmp=$($RPMBUILD --version 2>/dev/null)], [
		RPMBUILD_VERSION=$(echo $tmp | $AWK '/RPM/ { print $[3] }')
		HAVE_RPMBUILD=yes
		AC_MSG_RESULT([$HAVE_RPMBUILD ($RPMBUILD_VERSION)])
	],[
		HAVE_RPMBUILD=no
		AC_MSG_RESULT([$HAVE_RPMBUILD])
	])

	RPM_DEFINE_COMMON='--define "$(DEBUG_ZFS) 1"'
	RPM_DEFINE_COMMON=${RPM_DEFINE_COMMON}' --define "$(DEBUGINFO_ZFS) 1"'
	RPM_DEFINE_COMMON=${RPM_DEFINE_COMMON}' --define "$(DEBUG_KMEM_ZFS) 1"'
	RPM_DEFINE_COMMON=${RPM_DEFINE_COMMON}' --define "$(DEBUG_KMEM_TRACKING_ZFS) 1"'
	RPM_DEFINE_COMMON=${RPM_DEFINE_COMMON}' --define "$(ASAN_ZFS) 1"'

	RPM_DEFINE_UTIL=' --define "_initconfdir $(initconfdir)"'

	dnl # Make the next three RPM_DEFINE_UTIL additions conditional, since
	dnl # their values may not be set when running:
	dnl #
	dnl #	./configure --with-config=srpm
	dnl #
	AS_IF([test -n "$dracutdir" ], [
		RPM_DEFINE_UTIL=${RPM_DEFINE_UTIL}' --define "_dracutdir $(dracutdir)"'
	])
	AS_IF([test -n "$udevdir" ], [
		RPM_DEFINE_UTIL=${RPM_DEFINE_UTIL}' --define "_udevdir $(udevdir)"'
	])
	AS_IF([test -n "$udevruledir" ], [
		RPM_DEFINE_UTIL=${RPM_DEFINE_UTIL}' --define "_udevruledir $(udevruledir)"'
	])
	RPM_DEFINE_UTIL=${RPM_DEFINE_UTIL}' $(DEFINE_SYSTEMD)'
	RPM_DEFINE_UTIL=${RPM_DEFINE_UTIL}' $(DEFINE_PYZFS)'
	RPM_DEFINE_UTIL=${RPM_DEFINE_UTIL}' $(DEFINE_PAM)'
	RPM_DEFINE_UTIL=${RPM_DEFINE_UTIL}' $(DEFINE_PYTHON_VERSION)'
	RPM_DEFINE_UTIL=${RPM_DEFINE_UTIL}' $(DEFINE_PYTHON_PKG_VERSION)'

	dnl # Override default lib directory on Debian/Ubuntu systems.  The
	dnl # provided /usr/lib/rpm/platform/<arch>/macros files do not
	dnl # specify the correct path for multiarch systems as described
	dnl # by the packaging guidelines.
	dnl #
	dnl # https://wiki.ubuntu.com/MultiarchSpec
	dnl # https://wiki.debian.org/Multiarch/Implementation
	dnl #
	AS_IF([test "$DEFAULT_PACKAGE" = "deb"], [
		MULTIARCH_LIBDIR="lib/$(dpkg-architecture -qDEB_HOST_MULTIARCH)"
		RPM_DEFINE_UTIL=${RPM_DEFINE_UTIL}' --define "_lib $(MULTIARCH_LIBDIR)"'
		AC_SUBST(MULTIARCH_LIBDIR)
	])

	dnl # Make RPM_DEFINE_KMOD additions conditional on CONFIG_KERNEL,
	dnl # since the values will not be set otherwise. The spec files
	dnl # provide defaults for them.
	dnl #
	RPM_DEFINE_KMOD='--define "_wrong_version_format_terminate_build 0"'
	AM_COND_IF([CONFIG_KERNEL], [
		RPM_DEFINE_KMOD=${RPM_DEFINE_KMOD}' --define "kernels $(LINUX_VERSION)"'
		RPM_DEFINE_KMOD=${RPM_DEFINE_KMOD}' --define "ksrc $(LINUX)"'
		RPM_DEFINE_KMOD=${RPM_DEFINE_KMOD}' --define "kobj $(LINUX_OBJ)"'
	])

	RPM_DEFINE_DKMS=''

	SRPM_DEFINE_COMMON='--define "build_src_rpm 1"'
	SRPM_DEFINE_UTIL=
	SRPM_DEFINE_KMOD=
	SRPM_DEFINE_DKMS=

	RPM_SPEC_DIR="rpm/generic"
	AC_ARG_WITH([spec],
		AS_HELP_STRING([--with-spec=SPEC],
		[Spec files 'generic|redhat']),
		[RPM_SPEC_DIR="rpm/$withval"])

	AC_MSG_CHECKING([whether spec files are available])
	AC_MSG_RESULT([yes ($RPM_SPEC_DIR/*.spec.in)])

	AC_SUBST(HAVE_RPM)
	AC_SUBST(RPM)
	AC_SUBST(RPM_VERSION)

	AC_SUBST(HAVE_RPMBUILD)
	AC_SUBST(RPMBUILD)
	AC_SUBST(RPMBUILD_VERSION)

	AC_SUBST(RPM_SPEC_DIR)
	AC_SUBST(RPM_DEFINE_UTIL)
	AC_SUBST(RPM_DEFINE_KMOD)
	AC_SUBST(RPM_DEFINE_DKMS)
	AC_SUBST(RPM_DEFINE_COMMON)
	AC_SUBST(SRPM_DEFINE_UTIL)
	AC_SUBST(SRPM_DEFINE_KMOD)
	AC_SUBST(SRPM_DEFINE_DKMS)
	AC_SUBST(SRPM_DEFINE_COMMON)
])

dnl #
dnl # Check for dpkg+dpkg-buildpackage to build DEB packages.  If these
dnl # tools are missing it is non-fatal but you will not be able to build
dnl # DEB packages and will be warned if you try too.
dnl #
AC_DEFUN([ZFS_AC_DPKG], [
	DPKG=dpkg
	DPKGBUILD=dpkg-buildpackage

	AC_MSG_CHECKING([whether $DPKG is available])
	AS_IF([tmp=$($DPKG --version 2>/dev/null)], [
		DPKG_VERSION=$(echo $tmp | $AWK '/Debian/ { print $[7] }')
		HAVE_DPKG=yes
		AC_MSG_RESULT([$HAVE_DPKG ($DPKG_VERSION)])
	],[
		HAVE_DPKG=no
		AC_MSG_RESULT([$HAVE_DPKG])
	])

	AC_MSG_CHECKING([whether $DPKGBUILD is available])
	AS_IF([tmp=$($DPKGBUILD --version 2>/dev/null)], [
		DPKGBUILD_VERSION=$(echo $tmp | \
		    $AWK '/Debian/ { print $[4] }' | cut -f-4 -d'.')
		HAVE_DPKGBUILD=yes
		AC_MSG_RESULT([$HAVE_DPKGBUILD ($DPKGBUILD_VERSION)])
	],[
		HAVE_DPKGBUILD=no
		AC_MSG_RESULT([$HAVE_DPKGBUILD])
	])

	AC_SUBST(HAVE_DPKG)
	AC_SUBST(DPKG)
	AC_SUBST(DPKG_VERSION)

	AC_SUBST(HAVE_DPKGBUILD)
	AC_SUBST(DPKGBUILD)
	AC_SUBST(DPKGBUILD_VERSION)
])

dnl #
dnl # Until native packaging for various different packing systems
dnl # can be added the least we can do is attempt to use alien to
dnl # convert the RPM packages to the needed package type.  This is
dnl # a hack but so far it has worked reasonable well.
dnl #
AC_DEFUN([ZFS_AC_ALIEN], [
	ALIEN=alien

	AC_MSG_CHECKING([whether $ALIEN is available])
	AS_IF([tmp=$($ALIEN --version 2>/dev/null)], [
		ALIEN_VERSION=$(echo $tmp | $AWK '{ print $[3] }')
		HAVE_ALIEN=yes
		AC_MSG_RESULT([$HAVE_ALIEN ($ALIEN_VERSION)])
	],[
		HAVE_ALIEN=no
		AC_MSG_RESULT([$HAVE_ALIEN])
	])

	AC_SUBST(HAVE_ALIEN)
	AC_SUBST(ALIEN)
	AC_SUBST(ALIEN_VERSION)
])

dnl #
dnl # Using the VENDOR tag from config.guess set the default
dnl # package type for 'make pkg': (rpm | deb | tgz)
dnl #
AC_DEFUN([ZFS_AC_DEFAULT_PACKAGE], [
	AC_MSG_CHECKING([os distribution])
	AC_ARG_WITH([vendor],
		[AS_HELP_STRING([--with-vendor],
			[Distribution vendor @<:@default=check@:>@])],
		[with_vendor=$withval],
		[with_vendor=check])
	AS_IF([test "x$with_vendor" = "xcheck"],[
		if test -f /etc/toss-release ; then
			VENDOR=toss ;
		elif test -f /etc/fedora-release ; then
			VENDOR=fedora ;
		elif test -f /etc/redhat-release ; then
			VENDOR=redhat ;
		elif test -f /etc/gentoo-release ; then
			VENDOR=gentoo ;
		elif test -f /etc/arch-release ; then
			VENDOR=arch ;
		elif test -f /etc/SuSE-release ; then
			VENDOR=sles ;
		elif test -f /etc/slackware-version ; then
			VENDOR=slackware ;
		elif test -f /etc/lunar.release ; then
			VENDOR=lunar ;
		elif test -f /etc/lsb-release ; then
			VENDOR=ubuntu ;
		elif test -f /etc/debian_version ; then
			VENDOR=debian ;
		elif test -f /etc/alpine-release ; then
			VENDOR=alpine ;
		elif test -f /bin/freebsd-version ; then
			VENDOR=freebsd ;
		else
			VENDOR= ;
		fi],
		[ test "x${with_vendor}" != x],[
			VENDOR="$with_vendor" ],
		[ VENDOR= ; ]
	)
	AC_MSG_RESULT([$VENDOR])
	AC_SUBST(VENDOR)

	AC_MSG_CHECKING([default package type])
	case "$VENDOR" in
		toss)       DEFAULT_PACKAGE=rpm  ;;
		redhat)     DEFAULT_PACKAGE=rpm  ;;
		fedora)     DEFAULT_PACKAGE=rpm  ;;
		gentoo)     DEFAULT_PACKAGE=tgz  ;;
		alpine)     DEFAULT_PACKAGE=tgz  ;;
		arch)       DEFAULT_PACKAGE=tgz  ;;
		sles)       DEFAULT_PACKAGE=rpm  ;;
		slackware)  DEFAULT_PACKAGE=tgz  ;;
		lunar)      DEFAULT_PACKAGE=tgz  ;;
		ubuntu)     DEFAULT_PACKAGE=deb  ;;
		debian)     DEFAULT_PACKAGE=deb  ;;
		freebsd)    DEFAULT_PACKAGE=pkg  ;;
		*)          DEFAULT_PACKAGE=rpm  ;;
	esac
	AC_MSG_RESULT([$DEFAULT_PACKAGE])
	AC_SUBST(DEFAULT_PACKAGE)

	AC_MSG_CHECKING([default init directory])
	case "$VENDOR" in
		freebsd)    initdir=$sysconfdir/rc.d  ;;
		*)          initdir=$sysconfdir/init.d;;
	esac
	AC_MSG_RESULT([$initdir])
	AC_SUBST(initdir)

	AC_MSG_CHECKING([default init script type and shell])
	case "$VENDOR" in
		toss)       DEFAULT_INIT_SCRIPT=redhat ;;
		redhat)     DEFAULT_INIT_SCRIPT=redhat ;;
		fedora)     DEFAULT_INIT_SCRIPT=fedora ;;
		gentoo)     DEFAULT_INIT_SCRIPT=openrc ;;
		alpine)     DEFAULT_INIT_SCRIPT=openrc ;;
		arch)       DEFAULT_INIT_SCRIPT=lsb    ;;
		sles)       DEFAULT_INIT_SCRIPT=lsb    ;;
		slackware)  DEFAULT_INIT_SCRIPT=lsb    ;;
		lunar)      DEFAULT_INIT_SCRIPT=lunar  ;;
		ubuntu)     DEFAULT_INIT_SCRIPT=lsb    ;;
		debian)     DEFAULT_INIT_SCRIPT=lsb    ;;
		freebsd)    DEFAULT_INIT_SCRIPT=freebsd;;
		*)          DEFAULT_INIT_SCRIPT=lsb    ;;
	esac

	# On gentoo, it's possible that OpenRC isn't installed.  Check if
	# /sbin/openrc-run exists, and if not, fall back to generic defaults.

	DEFAULT_INIT_SHELL="/bin/sh"
	AS_IF([test "$DEFAULT_INIT_SCRIPT" = "openrc"], [
		AS_IF([test -x "/sbin/openrc-run"],
			[DEFAULT_INIT_SHELL="/sbin/openrc-run"],
			[DEFAULT_INIT_SCRIPT=lsb])
	])

	AC_MSG_RESULT([$DEFAULT_INIT_SCRIPT:$DEFAULT_INIT_SHELL])
	AC_SUBST(DEFAULT_INIT_SCRIPT)
	AC_SUBST(DEFAULT_INIT_SHELL)

	AC_MSG_CHECKING([default nfs server init script])
	AS_IF([test "$VENDOR" = "debian"],
		[DEFAULT_INIT_NFS_SERVER="nfs-kernel-server"],
		[DEFAULT_INIT_NFS_SERVER="nfs"]
	)
	AC_MSG_RESULT([$DEFAULT_INIT_NFS_SERVER])
	AC_SUBST(DEFAULT_INIT_NFS_SERVER)

	AC_MSG_CHECKING([default init config directory])
	case "$VENDOR" in
		alpine)     initconfdir=/etc/conf.d    ;;
		gentoo)     initconfdir=/etc/conf.d    ;;
		toss)       initconfdir=/etc/sysconfig ;;
		redhat)     initconfdir=/etc/sysconfig ;;
		fedora)     initconfdir=/etc/sysconfig ;;
		sles)       initconfdir=/etc/sysconfig ;;
		ubuntu)     initconfdir=/etc/default   ;;
		debian)     initconfdir=/etc/default   ;;
		freebsd)    initconfdir=$sysconfdir/rc.conf.d;;
		*)          initconfdir=/etc/default   ;;
	esac
	AC_MSG_RESULT([$initconfdir])
	AC_SUBST(initconfdir)

	AC_MSG_CHECKING([whether initramfs-tools is available])
	if test -d /usr/share/initramfs-tools ; then
		RPM_DEFINE_INITRAMFS='--define "_initramfs 1"'
		AC_MSG_RESULT([yes])
	else
		RPM_DEFINE_INITRAMFS=''
		AC_MSG_RESULT([no])
	fi
	AC_SUBST(RPM_DEFINE_INITRAMFS)
])

dnl #
dnl # Default ZFS package configuration
dnl #
AC_DEFUN([ZFS_AC_PACKAGE], [
	ZFS_AC_DEFAULT_PACKAGE
	AS_IF([test x$VENDOR != xfreebsd], [
		ZFS_AC_RPM
		ZFS_AC_DPKG
		ZFS_AC_ALIEN
	])
])
