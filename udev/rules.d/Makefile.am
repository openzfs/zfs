udevrule_DATA = \
	60-zpool.rules \
	60-zvol.rules

EXTRA_DIST = \
	60-zpool.rules.in \
	60-zvol.rules.in

$(udevrule_DATA):
	-$(SED) -e 's,@udevdir\@,$(udevdir),g' '$@.in' >'$@'

distclean-local::
	-$(RM) $(udevrule_DATA)

