SUBDIRS = zpool-config zpios-test zpios-profile

EXTRA_DIST = dkms.mkconf dkms.postbuild kmodtool zfs2zol-patch.sed cstyle.pl

pkgdatadir = $(datadir)/@PACKAGE@
dist_pkgdata_SCRIPTS = \
	$(top_builddir)/scripts/common.sh \
	$(top_srcdir)/scripts/zconfig.sh \
	$(top_srcdir)/scripts/zfault.sh \
	$(top_srcdir)/scripts/ziltest.sh \
	$(top_srcdir)/scripts/zimport.sh \
	$(top_srcdir)/scripts/zfs.sh \
	$(top_srcdir)/scripts/zpool-create.sh \
	$(top_srcdir)/scripts/zpios.sh \
	$(top_srcdir)/scripts/zpios-sanity.sh \
	$(top_srcdir)/scripts/zpios-survey.sh \
	$(top_srcdir)/scripts/smb.sh

ZFS=$(top_builddir)/scripts/zfs.sh
ZCONFIG=$(top_builddir)/scripts/zconfig.sh
ZFAULT=$(top_builddir)/scripts/zfault.sh
ZIMPORT=$(top_builddir)/scripts/zimport.sh
ZTEST=$(top_builddir)/cmd/ztest/ztest
ZPIOS_SANITY=$(top_builddir)/scripts/zpios-sanity.sh

check:
	@$(ZFS) -u
	@echo
	@echo -n "===================================="
	@echo -n " ZTEST "
	@echo    "===================================="
	@echo
	@$(ZFS)
	@$(ZTEST) -V
	@$(ZFS) -u
	@echo
	@echo
	@echo -n "==================================="
	@echo -n " ZCONFIG "
	@echo    "==================================="
	@echo
	@$(ZCONFIG) -c
	@echo
	@echo -n "==================================="
	@echo -n " ZFAULT "
	@echo    "==================================="
	@echo
	@$(ZFAULT) -c
	@echo
	@echo -n "===================================="
	@echo -n " ZPIOS "
	@echo    "===================================="
	@echo
	@$(ZFS)
	@$(ZPIOS_SANITY)
	@$(ZFS) -u
	@echo
