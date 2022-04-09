CLEANFILES =
EXTRA_DIST =
include $(top_srcdir)/config/Substfiles.am
include $(top_srcdir)/config/Shellcheck.am

hooksdir = /usr/share/initramfs-tools/hooks

hooks_SCRIPTS = \
	zfs \
	zfsunlock

SUBSTFILES += $(hooks_SCRIPTS)
SHELLCHECKSCRIPTS += $(hooks_SCRIPTS)
