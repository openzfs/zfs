include $(top_srcdir)/config/Rules.am

EXTRA_DIST = \
	README \
	history_event-zfs-list-cacher.sh.in

zedconfdir = $(sysconfdir)/zfs/zed.d

dist_zedconf_DATA = \
	zed-functions.sh \
	zed.rc

zedexecdir = $(zfsexecdir)/zed.d

dist_zedexec_SCRIPTS = \
	all-debug.sh \
	all-syslog.sh \
	data-notify.sh \
	generic-notify.sh \
	resilver_finish-notify.sh \
	scrub_finish-notify.sh \
	statechange-led.sh \
	statechange-notify.sh \
	vdev_clear-led.sh \
	vdev_attach-led.sh \
	pool_import-led.sh \
	resilver_finish-start-scrub.sh

nodist_zedexec_SCRIPTS = history_event-zfs-list-cacher.sh

$(nodist_zedexec_SCRIPTS): %: %.in
	-$(SED) -e 's,@bindir\@,$(bindir),g' \
		-e 's,@runstatedir\@,$(runstatedir),g' \
		-e 's,@sbindir\@,$(sbindir),g' \
		-e 's,@sysconfdir\@,$(sysconfdir),g' \
		$< >'$@'

zedconfdefaults = \
	all-syslog.sh \
	data-notify.sh \
	resilver_finish-notify.sh \
	scrub_finish-notify.sh \
	statechange-led.sh \
	statechange-notify.sh \
	vdev_clear-led.sh \
	vdev_attach-led.sh \
	pool_import-led.sh \
	resilver_finish-start-scrub.sh

install-data-hook:
	$(MKDIR_P) "$(DESTDIR)$(zedconfdir)"
	for f in $(zedconfdefaults); do \
	  test -f "$(DESTDIR)$(zedconfdir)/$${f}" -o \
	       -L "$(DESTDIR)$(zedconfdir)/$${f}" || \
	    ln -s "$(zedexecdir)/$${f}" "$(DESTDIR)$(zedconfdir)"; \
	done
	chmod 0600 "$(DESTDIR)$(zedconfdir)/zed.rc"
