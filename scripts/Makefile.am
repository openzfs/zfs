SUBDIRS = zpool-config
EXTRA_DIST = common.sh zfs-update.sh zfs.sh zpool-create.sh

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
