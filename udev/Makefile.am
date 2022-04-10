udevrule_DATA = \
	%D%/rules.d/69-vdev.rules \
	%D%/rules.d/60-zvol.rules \
	%D%/rules.d/90-zfs.rules

SUBSTFILES += $(udevrule_DATA)


dist_udev_SCRIPTS = %D%/vdev_id
# TODO: #12084: SHELLCHECKSCRIPTS += %D%/vdev_id


udev_PROGRAMS    = %D%/zvol_id
CPPCHECKTARGETS += %D%/zvol_id


PHONY += udev
udev: $(dist_udev_SCRIPTS) $(udev_PROGRAMS)
