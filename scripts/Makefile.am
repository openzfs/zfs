SUBDIRS = zpool-config zpios-test zpios-profile
EXTRA_DIST = common.sh zfs-update.sh zfs.sh zpool-create.sh
EXTRA_DIST += zpios.sh zpios-survey.sh

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
	@$(shell ulimit -c unlimited)
	@$(ZTEST) -V
	@echo
	@$(ZFS) -vu
