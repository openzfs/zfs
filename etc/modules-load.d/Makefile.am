modulesload_DATA = \
	zfs.conf

EXTRA_DIST = \
	$(top_srcdir)/etc/modules-load.d/zfs.conf.in

$(modulesload_DATA):%:%.in
	-$(SED) \
		-e '' \
		$< >'$@'

# Double-colon rules are allowed; there are multiple independent definitions.
distclean-local::
	-$(RM) $(modulesload_DATA)
