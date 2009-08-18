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
	@echo -n "===================================="
	@echo -n " ZPIOS "
	@echo    "===================================="
	@echo
	@$(ZPIOS) -c file-raid0  -t tiny
	@$(ZPIOS) -c file-raid10 -t tiny | tail -1
	@$(ZPIOS) -c file-raidz  -t tiny | tail -1
	@$(ZPIOS) -c file-raidz2 -t tiny | tail -1
	@$(ZPIOS) -c lo-raid0    -t tiny | tail -1
	@$(ZPIOS) -c lo-raid10   -t tiny | tail -1
	@$(ZPIOS) -c lo-raidz    -t tiny | tail -1
	@$(ZPIOS) -c lo-raidz2   -t tiny | tail -1
	@echo
	@$(ZFS) -vu
