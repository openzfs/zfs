modulesload_DATA = \
	zfs.conf

EXTRA_DIST = \
	zfs.conf.in

$(modulesload_DATA):%:%.in
	-$(SED) \
		-e '' \
		$< >'$@'

CLEANFILES = $(modulesload_DATA)
