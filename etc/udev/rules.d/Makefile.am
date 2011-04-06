pkgsysconfdir = $(sysconfdir)/udev/rules.d

pkgsysconf_DATA = \
	60-zpool.rules \
	60-zvol.rules

EXTRA_DIST = 60-zpool.rules.in 60-zvol.rules.in

distclean-local::
	-$(RM) $(pkgsysconf_DATA)
