pkglibexecdir = $(libexecdir)/@PACKAGE@
nobase_pkglibexec_SCRIPTS  = common.sh
nobase_pkglibexec_SCRIPTS += zconfig.sh
nobase_pkglibexec_SCRIPTS += zfs.sh
nobase_pkglibexec_SCRIPTS += zpool-create.sh
nobase_pkglibexec_SCRIPTS += zpool-config/*
EXTRA_DIST = zfs-update.sh $(nobase_pkglibexec_SCRIPTS)

ZFS=${top_srcdir}/scripts/zfs.sh
ZCONFIG=${top_srcdir}/scripts/zconfig.sh
ZTEST=${top_builddir}/cmd/ztest/ztest

check:
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
	@$(ZCONFIG)
	@echo
