include $(top_srcdir)/config/Substfiles.am
include $(top_srcdir)/config/Shellcheck.am

hooksdir = /usr/share/initramfs-tools/hooks

hooks_SCRIPTS = \
	zfs \
	zfsunlock

SHELLCHECK_OPTS = --enable=all
SUBSTFILES += $(hooks_SCRIPTS)
