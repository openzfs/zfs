pkglibexecdir = $(libexecdir)/@PACKAGE@
nobase_pkglibexec_SCRIPTS  = common.sh
nobase_pkglibexec_SCRIPTS += zfs.sh
nobase_pkglibexec_SCRIPTS += zpool-create.sh
nobase_pkglibexec_SCRIPTS += zpool-config/*
nobase_pkglibexec_SCRIPTS += zpios.sh
nobase_pkglibexec_SCRIPTS += zpios-survey.sh
nobase_pkglibexec_SCRIPTS += zpios-test/*
nobase_pkglibexec_SCRIPTS += zpios-profile/*
EXTRA_DIST = zfs-update.sh $(nobase_pkglibexec_SCRIPTS)

ZFS=${top_srcdir}/scripts/zfs.sh
ZCONFIG=${top_srcdir}/scripts/zconfig.sh
ZTEST=${top_builddir}/cmd/ztest/ztest
ZPIOS=${top_srcdir}/scripts/zpios.sh

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
	@$(ZPIOS) -c file-raid0  -t tiny
	@$(ZPIOS) -c file-raid10 -t tiny | tail -1
	@$(ZPIOS) -c file-raidz  -t tiny | tail -1
	@$(ZPIOS) -c file-raidz2 -t tiny | tail -1
	@$(ZPIOS) -c lo-raid0    -t tiny | tail -1
	@$(ZPIOS) -c lo-raid10   -t tiny | tail -1
	@$(ZPIOS) -c lo-raidz    -t tiny | tail -1
	@$(ZPIOS) -c lo-raidz2   -t tiny | tail -1
	@$(ZFS) -u
	@echo
