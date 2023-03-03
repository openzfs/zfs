include $(top_srcdir)/config/Shellcheck.am

scriptsdir = /usr/share/initramfs-tools/scripts

dist_scripts_SCRIPTS = \
	zfs

SUBDIRS = local-top

SHELLCHECK_IGNORE = ,SC2295
SHELLCHECKDIRS = $(SUBDIRS)
SHELLCHECK_SHELL = sh
