zedconfdir = $(sysconfdir)/zfs/zed.d
dist_zedconf_DATA = \
	%D%/zed-functions.sh \
	%D%/zed.rc

zedexecdir = $(zfsexecdir)/zed.d

dist_zedexec_SCRIPTS = \
	%D%/all-debug.sh \
	%D%/all-syslog.sh \
	%D%/data-notify.sh \
	%D%/generic-notify.sh \
	%D%/pool_import-led.sh \
	%D%/resilver_finish-notify.sh \
	%D%/resilver_finish-start-scrub.sh \
	%D%/scrub_finish-notify.sh \
	%D%/statechange-led.sh \
	%D%/statechange-notify.sh \
	%D%/trim_finish-notify.sh \
	%D%/vdev_attach-led.sh \
	%D%/vdev_clear-led.sh

nodist_zedexec_SCRIPTS = \
	%D%/history_event-zfs-list-cacher.sh

SUBSTFILES += $(nodist_zedexec_SCRIPTS)

zedconfdefaults = \
	all-syslog.sh \
	data-notify.sh \
	history_event-zfs-list-cacher.sh \
	pool_import-led.sh \
	resilver_finish-notify.sh \
	resilver_finish-start-scrub.sh \
	scrub_finish-notify.sh \
	statechange-led.sh \
	statechange-notify.sh \
	vdev_attach-led.sh \
	vdev_clear-led.sh

dist_noinst_DATA += %D%/README

INSTALL_DATA_HOOKS += zed-install-data-hook
zed-install-data-hook:
	$(MKDIR_P) "$(DESTDIR)$(zedconfdir)"
	set -x; for f in $(zedconfdefaults); do \
	  [ -f "$(DESTDIR)$(zedconfdir)/$${f}" ] ||\
	    [ -L "$(DESTDIR)$(zedconfdir)/$${f}" ] || \
	    $(LN_S) "$(zedexecdir)/$${f}" "$(DESTDIR)$(zedconfdir)"; \
	done

SHELLCHECKSCRIPTS += $(dist_zedconf_DATA) $(dist_zedexec_SCRIPTS) $(nodist_zedexec_SCRIPTS)
$(call SHELLCHECK_OPTS,$(dist_zedconf_DATA) $(dist_zedexec_SCRIPTS) $(nodist_zedexec_SCRIPTS)): SHELLCHECK_SHELL = sh
# False positive: 1>&"${ZED_FLOCK_FD}" looks suspiciously similar to a >&filename bash extension
$(call SHELLCHECK_OPTS,$(dist_zedconf_DATA) $(dist_zedexec_SCRIPTS) $(nodist_zedexec_SCRIPTS)): CHECKBASHISMS_IGNORE = -e 'should be >word 2>&1' -e '&"$${ZED_FLOCK_FD}"'
