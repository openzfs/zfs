
noinst_launch_d_DATA = \
	%D%/zpool-import-all.sh.in
noinst_launch_ddir = \
	$(launchdscriptdir)/

do_subst = -$(SED) -e 's,@bindir\@,$(bindir),g' \
		-e 's,@runstatedir\@,$(runstatedir),g' \
		-e 's,@sbindir\@,$(sbindir),g' \
		-e 's,@sysconfdir\@,$(sysconfdir),g' \
		-e 's,@launchdscriptdir\@,$(launchdscriptdir),g'

CLEANFILES += %D%/zpool-import-all.sh

INSTALL_DATA_HOOKS += install-zpool-import

install-zpool-import: %D%/zpool-import-all.sh.in
	$(do_subst) < %D%/zpool-import-all.sh.in > $(DESTDIR)/$(launchdscriptdir)/zpool-import-all.sh
	chmod +x $(DESTDIR)/$(launchdscriptdir)/zpool-import-all.sh
