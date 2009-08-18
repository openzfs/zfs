pkglibexecdir = $(libexecdir)/@PACKAGE@
nobase_pkglibexec_SCRIPTS  = common.sh
nobase_pkglibexec_SCRIPTS += zfs.sh
nobase_pkglibexec_SCRIPTS += zpool-create.sh
nobase_pkglibexec_SCRIPTS += zpool-config/*
EXTRA_DIST = zfs-update.sh $(nobase_pkglibexec_SCRIPTS)

ZFS=${top_srcdir}/scripts/zfs.sh
ZTEST=${top_builddir}/cmd/ztest/ztest
ZPIOS=${top_srcdir}/scripts/zpios.sh

check:
	@$(ZFS) -v
	@echo
	@echo -n "===================================="
	@echo -n " ZTEST "
	@echo    "===================================="
	@echo
	@$(ZTEST) -V
	@echo
	@$(ZFS) -vu
