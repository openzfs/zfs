SUBDIRS = zpool-config zpios-test zpios-profile

EXTRA_DIST = dkms.mkconf dkms.postbuild kmodtool zfs2zol-patch.sed cstyle.pl

pkgdatadir = $(datadir)/@PACKAGE@
dist_pkgdata_SCRIPTS = \
	$(top_builddir)/scripts/common.sh \
	$(top_srcdir)/scripts/zconfig.sh \
	$(top_srcdir)/scripts/ziltest.sh \
	$(top_srcdir)/scripts/zimport.sh \
	$(top_srcdir)/scripts/zfs.sh \
	$(top_srcdir)/scripts/zfs-tests.sh \
	$(top_srcdir)/scripts/zloop.sh \
	$(top_srcdir)/scripts/zpool-create.sh \
	$(top_srcdir)/scripts/zpios.sh \
	$(top_srcdir)/scripts/zpios-sanity.sh \
	$(top_srcdir)/scripts/zpios-survey.sh \
	$(top_srcdir)/scripts/smb.sh \
	$(top_srcdir)/scripts/zfs-helpers.sh
