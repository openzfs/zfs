pathsddir = $(sysconfdir)/paths.d
pathsd_DATA = %D%/zfs-path

paths_ddir = $(sysconfdir)/paths.d/
dist_paths_d_DATA = \
	%D%/zfs-path

SUBSTFILES += $(dist_paths_d_DATA)

CLEANFILES += $(pathsd_DATA)
CLEANFILES += %D%/zfs-path

INSTALL_DATA_HOOKS += pathsd-data-hook
pathsd-data-hook:
	-$(SED) -e 's,@bindir\@,$(bindir),g' \
		-e 's,@runstatedir\@,$(runstatedir),g' \
		-e 's,@sbindir\@,$(sbindir),g' \
		-e 's,@sysconfdir\@,$(sysconfdir),g' \
		-e 's,@launchddaemondir\@,$(launchddaemondir),g' \
		-e 's,@launchdscriptdir\@,$(launchdscriptdir),g' \
		%D%/zfs-path.in > $(DESTDIR)/$(sysconfdir)/paths.d/zfs-path
