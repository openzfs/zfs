# SPDX-License-Identifier: CDDL-1.0
CLEANFILES += %D%/zfs-path

INSTALL_DATA_HOOKS += pathsd-data-hook
pathsd-data-hook:
	-$(AM_V_at)$(MKDIR_P) $(DESTDIR)$(sysconfdir)/paths.d
	-$(SED) -e 's,@bindir\@,$(bindir),g' \
		-e 's,@runstatedir\@,$(runstatedir),g' \
		-e 's,@sbindir\@,$(sbindir),g' \
		-e 's,@sysconfdir\@,$(sysconfdir),g' \
		-e 's,@launchddaemondir\@,$(launchddaemondir),g' \
		-e 's,@launchdscriptdir\@,$(launchdscriptdir),g' \
		%D%/zfs-path.in > $(DESTDIR)/$(sysconfdir)/paths.d/zfs-path
