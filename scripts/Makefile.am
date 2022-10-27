scriptsdir = $(datadir)/$(PACKAGE)
dist_noinst_SCRIPTS = \
	%D%/commitcheck.sh \
	%D%/common.sh.in \
	%D%/dkms.mkconf \
	%D%/dkms.postbuild \
	%D%/kmodtool \
	%D%/make_gitrev.sh \
	%D%/man-dates.sh \
	%D%/mancheck.sh \
	%D%/paxcheck.sh \
	%D%/zfs-tests-color.sh

scripts_scripts = \
	%D%/zfs-helpers.sh \
	%D%/zfs-tests.sh \
	%D%/zfs.sh \
	%D%/zimport.sh \
	%D%/zloop.sh

if CONFIG_USER
dist_scripts_SCRIPTS = $(scripts_scripts)
else
dist_noinst_SCRIPTS += $(scripts_scripts)
endif

dist_noinst_DATA += \
	%D%/cstyle.pl \
	%D%/enum-extract.pl \
	%D%/zfs2zol-patch.sed \
	%D%/zol2zfs-patch.sed

SHELLCHECKSCRIPTS += $(dist_scripts_SCRIPTS) $(dist_noinst_SCRIPTS)

define SCRIPTS_EXTRA_ENVIRONMENT

# Only required for in-tree use
export INTREE="yes"
export GDB="libtool --mode=execute gdb"
export LDMOD=/sbin/insmod

export CMD_DIR=$(abs_top_builddir)
export UDEV_SCRIPT_DIR=$(abs_top_srcdir)/udev
export UDEV_CMD_DIR=$(abs_top_builddir)/udev
export UDEV_RULE_DIR=$(abs_top_builddir)/udev/rules.d
export ZEDLET_ETC_DIR=$$CMD_DIR/cmd/zed/zed.d
export ZEDLET_LIBEXEC_DIR=$$CMD_DIR/cmd/zed/zed.d
export ZPOOL_SCRIPT_DIR=$$CMD_DIR/cmd/zpool/zpool.d
export ZPOOL_SCRIPTS_PATH=$$CMD_DIR/cmd/zpool/zpool.d
export ZPOOL_COMPAT_DIR=$$CMD_DIR/cmd/zpool/compatibility.d
export CONTRIB_DIR=$(abs_top_builddir)/contrib
export LIB_DIR=$(abs_top_builddir)/.libs
export SYSCONF_DIR=$(abs_top_builddir)/etc

export INSTALL_UDEV_DIR=$(udevdir)
export INSTALL_UDEV_RULE_DIR=$(udevruledir)
export INSTALL_MOUNT_HELPER_DIR=$(mounthelperdir)
export INSTALL_SYSCONF_DIR=$(sysconfdir)
export INSTALL_PYTHON_DIR=$(pythonsitedir)

export KMOD_SPL=$(abs_top_builddir)/module/spl.ko
export KMOD_ZFS=$(abs_top_builddir)/module/zfs.ko
export KMOD_FREEBSD=$(abs_top_builddir)/module/openzfs.ko
endef

export SCRIPTS_EXTRA_ENVIRONMENT

CLEANFILES += %D%/common.sh
%D%/common.sh: %D%/common.sh.in Makefile
	-$(AM_V_at)$(MKDIR_P) $(@D)
	-$(AM_V_GEN)$(SED) -e '/^export BIN_DIR=/s|$$|$(abs_top_builddir)/tests/zfs-tests/bin|' \
		-e '/^export SBIN_DIR=/s|$$|$(abs_top_builddir)|' \
		-e '/^export LIBEXEC_DIR=/s|$$|$(abs_top_builddir)|' \
		-e '/^export ZTS_DIR=/s|$$|$(abs_top_srcdir)/tests|' \
		-e '/^export SCRIPT_DIR=/s|$$|$(abs_top_srcdir)/scripts|' \
		$< >$@
	-$(AM_V_at)echo "$$SCRIPTS_EXTRA_ENVIRONMENT" >>$@

ALL_LOCAL += scripts-all-local
scripts-all-local: %D%/common.sh
	-SCRIPT_COMMON=$< $(srcdir)/%D%/zfs-tests.sh -c

CLEAN_LOCAL += scripts-clean-local
scripts-clean-local:
	-$(RM) -r tests/zfs-tests/bin/

INSTALL_DATA_HOOKS += scripts-install-data-hook
scripts-install-data-hook: %D%/common.sh.in Makefile
	-$(SED) -e '/^export BIN_DIR=/s|$$|$(bindir)|' \
		-e '/^export SBIN_DIR=/s|$$|$(sbindir)|' \
		-e '/^export LIBEXEC_DIR=/s|$$|$(zfsexecdir)|' \
		-e '/^export ZTS_DIR=/s|$$|$(datadir)/$(PACKAGE)|' \
		-e '/^export SCRIPT_DIR=/s|$$|$(datadir)/$(PACKAGE)|' \
		$< >$(DESTDIR)$(datadir)/$(PACKAGE)/common.sh
