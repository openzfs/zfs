modulesload_DATA = \
	$(top_srcdir)/etc/modules-load.d/zfs.conf

EXTRA_DIST = \
	$(top_srcdir)/etc/modules-load.d/zfs.conf.in

$(modulesload_DATA):
	-$(SED) \
		-e '' \
		'$@.in' >'$@'

distclean-local::
	-$(RM) $(modulesload_DATA)
