pkglibexecdir = $(libexecdir)/@PACKAGE@
nobase_pkglibexec_SCRIPTS  = common.sh
nobase_pkglibexec_SCRIPTS += zconfig.sh
nobase_pkglibexec_SCRIPTS += zfs.sh
nobase_pkglibexec_SCRIPTS += zpool-create.sh
nobase_pkglibexec_SCRIPTS += zpool-config/*
nobase_pkglibexec_SCRIPTS += zpios.sh
nobase_pkglibexec_SCRIPTS += zpios-sanity.sh
nobase_pkglibexec_SCRIPTS += zpios-survey.sh
nobase_pkglibexec_SCRIPTS += zpios-test/*
nobase_pkglibexec_SCRIPTS += zpios-profile/*
EXTRA_DIST = zfs-update.sh $(nobase_pkglibexec_SCRIPTS)

ZFS=${top_srcdir}/scripts/zfs.sh
ZCONFIG=${top_srcdir}/scripts/zconfig.sh
ZTEST=${top_builddir}/cmd/ztest/ztest
ZPIOS_SANITY=${top_srcdir}/scripts/zpios-sanity.sh

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
	@echo -n "===================================="
	@echo -n " ZPIOS "
	@echo    "===================================="
	@echo
	@$(ZFS)
	@$(ZPIOS_SANITY)
	@$(ZFS) -u
	@echo
