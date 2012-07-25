systemdgeneratordir = $(systemddir)/system-generators

systemd_PROGRAMS = systemdescaper

systemdescaper_SOURCES = \
	$(top_srcdir)/systemd/systemdescaper.c

systemdgenerator_SCRIPTS = \
	$(top_srcdir)/systemd/systemd-zfs-generator

EXTRA_DIST = \
	$(top_srcdir)/systemd/systemd-zfs-generator.in $(top_srcdir)/systemd/README.systemd

$(systemdgenerator_SCRIPTS):
	-$(SED) -e 's,@bindir\@,$(bindir),g' \
		-e 's,@sbindir\@,$(sbindir),g' \
		-e 's,@sysconfdir\@,$(sysconfdir),g' \
		-e 's,@systemddir\@,$(systemddir),g' \
		-e 's,@systemdgeneratordir\@,$(systemdgeneratordir),g' \
		'$@.in' >'$@' && chmod +x '$@'

distclean-local::
	-$(RM) $(systemdgenerator_SCRIPTS)

clean-local::
	-$(RM) $(systemdgenerator_SCRIPTS)
