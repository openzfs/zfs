modulesload_DATA = \
	zfs.conf

EXTRA_DIST = \
	$(top_srcdir)/etc/modules-load.d/zfs.conf.in

$(modulesload_DATA):
	-$(SED) \
		-e '' \
		"$(top_srcdir)/etc/modules-load.d/$@.in" >'$@'

distclean-local::
	-$(RM) $(modulesload_DATA)
