AUTOMAKE_OPTIONS = subdir-objects

launchdscriptdir = ${libexecdir}/zfs/launchd.d
launchdscript_SCRIPTS = \
	zpool-import-all.sh

CLEANFILES = $(launchdscript_SCRIPTS)

EXTRA_DIST = \
	$(top_srcdir)/etc/launchd/launchd.d/zpool-import-all.sh.in

do_subst = -$(SED) -e 's,@bindir\@,$(bindir),g' \
		-e 's,@runstatedir\@,$(runstatedir),g' \
		-e 's,@sbindir\@,$(sbindir),g' \
		-e 's,@sysconfdir\@,$(sysconfdir),g' \
		-e 's,@launchdscriptdir\@,$(launchdscriptdir),g'

zpool-import-all.sh: zpool-import-all.sh.in
	$(do_subst) < $(top_srcdir)/etc/launchd/launchd.d/zpool-import-all.sh.in > zpool-import-all.sh
	chmod +x zpool-import-all.sh

clean-local::
	-$(RM) $(launchdscript_SCRIPTS)
