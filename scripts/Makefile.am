SUBDIRS = zpool-config zpool-layout zpios-test zpios-profile

pkglibexecdir = $(libexecdir)/@PACKAGE@
dist_pkglibexec_SCRIPTS = \
	$(top_builddir)/scripts/common.sh \
	$(top_srcdir)/scripts/zconfig.sh \
	$(top_srcdir)/scripts/zfault.sh \
	$(top_srcdir)/scripts/zfs.sh \
	$(top_srcdir)/scripts/zpool-create.sh \
	$(top_srcdir)/scripts/zpios.sh \
	$(top_srcdir)/scripts/zpios-sanity.sh \
	$(top_srcdir)/scripts/zpios-survey.sh \
	$(top_srcdir)/scripts/smb.sh

ZFS=$(top_builddir)/scripts/zfs.sh
ZCONFIG=$(top_builddir)/scripts/zconfig.sh
ZFAULT=$(top_builddir)/scripts/zfault.sh
ZTEST=$(top_builddir)/cmd/ztest/ztest
ZPIOS_SANITY=$(top_builddir)/scripts/zpios-sanity.sh

all:
	@list='$(dist_pkglibexec_SCRIPTS)'; \
	for file in $$list; do \
		link=$$(basename $$file); \
		if [ ! -e $$link ]; then \
			$(LN_S) $$file $$link; \
		fi \
	done

clean:
	@list='$(dist_pkglibexec_SCRIPTS)'; \
	for file in $$list; do \
		link=$$(basename $$file); \
		if [ -L $$link ]; then \
			$(RM) $$link; \
		fi \
	done

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
