
CLEANFILES += $(launchddaemon_DATA)

launchddaemon_DATA = \
	$(top_srcdir)/etc/launchd/daemons/org.openzfsonosx.zconfigd.plist \
	$(top_srcdir)/etc/launchd/daemons/org.openzfsonosx.zed.plist \
	$(top_srcdir)/etc/launchd/daemons/org.openzfsonosx.zpool-import.plist \
	$(top_srcdir)/etc/launchd/daemons/org.openzfsonosx.zpool-import-all.plist \
	$(top_srcdir)/etc/launchd/daemons/org.openzfsonosx.InvariantDisks.plist

dist_launchddaemon_DATA = \
	%D%/org.openzfsonosx.zconfigd.plist.in \
	%D%/org.openzfsonosx.zed.plist.in \
	%D%/org.openzfsonosx.zpool-import.plist.in \
	%D%/org.openzfsonosx.zpool-import-all.plist.in \
	%D%/org.openzfsonosx.InvariantDisks.plist.in

%D%/org.openzfsonosx.zconfigd.plist: %D%/org.openzfsonosx.zconfigd.plist.in
%D%/org.openzfsonosx.zed.plist: %D%/org.openzfsonosx.zed.plist.in
%D%/org.openzfsonosx.zpool-import.plist: %D%/org.openzfsonosx.zpool-import.plist.in
%D%/org.openzfsonosx.zpool-import-all.plist: %D%/org.openzfsonosx.zpool-import-all.plist.in
%D%/org.openzfsonosx.InvariantDisks.plist: %D%/org.openzfsonosx.InvariantDisks.plist.in

$(launchddaemon_DATA):
	-$(SED) -e 's,@bindir\@,$(bindir),g' \
		-e 's,@runstatedir\@,$(runstatedir),g' \
		-e 's,@sbindir\@,$(sbindir),g' \
		-e 's,@sysconfdir\@,$(sysconfdir),g' \
		-e 's,@launchddaemondir\@,$(launchddaemondir),g' \
		-e 's,@launchdscriptdir\@,$(launchdscriptdir),g' \
		'$@.in' >'$@'


