
noinst_launch_ddir = \
	$(launchdscriptdir)/

do_subst = -$(SED) -e 's,@bindir\@,$(bindir),g' \
		-e 's,@runstatedir\@,$(runstatedir),g' \
		-e 's,@sbindir\@,$(sbindir),g' \
		-e 's,@sysconfdir\@,$(sysconfdir),g' \
		-e 's,@launchdscriptdir\@,$(launchdscriptdir),g'

CLEANFILES += %D%/zpool-import-all.sh

INSTALL_DATA_HOOKS += zpool-import-all.sh

zpool-import-all.sh: %D%/zpool-import-all.sh.in
	-$(AM_V_at)$(MKDIR_P) $(DESTDIR)/$(launchdscriptdir)/
	-$(AM_V_GEN)$(SED) -e 's,@bindir\@,$(bindir),g' \
		-e 's,@runstatedir\@,$(runstatedir),g' \
		-e 's,@sbindir\@,$(sbindir),g' \
		-e 's,@sysconfdir\@,$(sysconfdir),g' \
		-e 's,@launchdscriptdir\@,$(launchdscriptdir),g' \
		'$<' > $(DESTDIR)/$(launchdscriptdir)/'$@'
	chmod +x $(DESTDIR)/$(launchdscriptdir)/'$@'
