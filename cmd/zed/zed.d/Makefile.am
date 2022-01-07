include $(top_srcdir)/config/Rules.am
include $(top_srcdir)/config/Substfiles.am
include $(top_srcdir)/config/Shellcheck.am

EXTRA_DIST += README

zedconfdir = $(sysconfdir)/zfs/zed.d

dist_zedconf_DATA = \
	zed-functions.sh \
	zed.rc

SHELLCHECKSCRIPTS = zed-functions.sh zed.rc
SHELLCHECK_OPTS = --enable=all
SHELLCHECK_SHELL = dash

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
	resilver_finish-start-scrub.sh \
	trim_finish-notify.sh

nodist_zedexec_SCRIPTS = history_event-zfs-list-cacher.sh

SUBSTFILES += $(nodist_zedexec_SCRIPTS)

zedconfdefaults = \
	all-syslog.sh \
	data-notify.sh \
	history_event-zfs-list-cacher.sh \
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

# False positive: 1>&"${ZED_FLOCK_FD}" looks suspiciously similar to a >&filename bash extension
CHECKBASHISMS_IGNORE = -e 'should be >word 2>&1' -e '&"$${ZED_FLOCK_FD}"'
