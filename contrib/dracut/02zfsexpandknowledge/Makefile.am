include $(top_srcdir)/config/Substfiles.am
include $(top_srcdir)/config/Shellcheck.am

pkgdracutdir = $(dracutdir)/modules.d/02zfsexpandknowledge
pkgdracut_SCRIPTS = \
	module-setup.sh

SHELLCHECK_OPTS = --enable=all
SUBSTFILES += $(pkgdracut_SCRIPTS)
