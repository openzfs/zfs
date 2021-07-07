include $(top_srcdir)/config/Shellcheck.am

pkgdatadir = $(datadir)/@PACKAGE@

dist_pkgdata_SCRIPTS = \
	zimport.sh \
	zfs.sh \
	zfs-tests.sh \
	zloop.sh \
	zfs-helpers.sh

EXTRA_SCRIPTS = \
	commitcheck.sh \
	common.sh.in \
	dkms.mkconf \
	dkms.postbuild \
	kmodtool \
	make_gitrev.sh \
	man-dates.sh \
	paxcheck.sh \
	mancheck.sh

EXTRA_DIST = \
	cstyle.pl \
	enum-extract.pl \
	zfs2zol-patch.sed \
	zol2zfs-patch.sed \
	$(EXTRA_SCRIPTS)

SHELLCHECK_IGNORE = ,SC1117
SHELLCHECKSCRIPTS = $(EXTRA_SCRIPTS)

define EXTRA_ENVIRONMENT

# Only required for in-tree use
export INTREE="yes"
export GDB="libtool --mode=execute gdb"
export LDMOD=/sbin/insmod

export CMD_DIR=@abs_top_builddir@/cmd
export UDEV_RULE_DIR=@abs_top_builddir@/udev/rules.d
export ZEDLET_ETC_DIR=$$CMD_DIR/zed/zed.d
export ZEDLET_LIBEXEC_DIR=$$CMD_DIR/zed/zed.d
export ZPOOL_SCRIPT_DIR=$$CMD_DIR/zpool/zpool.d
export ZPOOL_SCRIPTS_PATH=$$CMD_DIR/zpool/zpool.d
export ZPOOL_COMPAT_DIR=$$CMD_DIR/zpool/compatibility.d
export CONTRIB_DIR=@abs_top_builddir@/contrib
export LIB_DIR=@abs_top_builddir@/lib
export SYSCONF_DIR=@abs_top_builddir@/etc

export INSTALL_UDEV_DIR=@udevdir@
export INSTALL_UDEV_RULE_DIR=@udevruledir@
export INSTALL_MOUNT_HELPER_DIR=@mounthelperdir@
export INSTALL_SYSCONF_DIR=@sysconfdir@
export INSTALL_PYTHON_DIR=@pythonsitedir@

export KMOD_SPL=@abs_top_builddir@/module/spl/spl.ko
export KMOD_ZAVL=@abs_top_builddir@/module/avl/zavl.ko
export KMOD_ZNVPAIR=@abs_top_builddir@/module/nvpair/znvpair.ko
export KMOD_ZUNICODE=@abs_top_builddir@/module/unicode/zunicode.ko
export KMOD_ZCOMMON=@abs_top_builddir@/module/zcommon/zcommon.ko
export KMOD_ZLUA=@abs_top_builddir@/module/lua/zlua.ko
export KMOD_ICP=@abs_top_builddir@/module/icp/icp.ko
export KMOD_ZFS=@abs_top_builddir@/module/zfs/zfs.ko
export KMOD_FREEBSD=@abs_top_builddir@/module/openzfs.ko
export KMOD_ZZSTD=@abs_top_builddir@/module/zstd/zzstd.ko
endef

export EXTRA_ENVIRONMENT

all-local:
	-$(SED) -e '\|^export BIN_DIR=|s|$$|@abs_top_builddir@/bin|' \
		-e '\|^export SBIN_DIR=|s|$$|@abs_top_builddir@/bin|' \
		-e '\|^export LIBEXEC_DIR=|s|$$|@abs_top_builddir@/bin|' \
		-e '\|^export ZTS_DIR=|s|$$|@abs_top_srcdir@/tests|' \
		-e '\|^export SCRIPT_DIR=|s|$$|@abs_top_srcdir@/scripts|' \
		$(abs_top_srcdir)/scripts/common.sh.in >common.sh
	-echo "$$EXTRA_ENVIRONMENT" >>common.sh

clean-local:
	-$(RM) common.sh

install-data-hook:
	-$(SED) -e '\|^export BIN_DIR=|s|$$|@bindir@|' \
		-e '\|^export SBIN_DIR=|s|$$|@sbindir@|' \
		-e '\|^export LIBEXEC_DIR=|s|$$|@zfsexecdir@|' \
		-e '\|^export ZTS_DIR=|s|$$|@datadir@/@PACKAGE@|' \
		-e '\|^export SCRIPT_DIR=|s|$$|@datadir@/@PACKAGE@|' \
		$(abs_top_srcdir)/scripts/common.sh.in \
		>$(DESTDIR)$(datadir)/@PACKAGE@/common.sh
