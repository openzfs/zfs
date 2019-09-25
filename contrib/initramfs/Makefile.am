include $(top_srcdir)/config/Shellcheck.am

initrddir = $(initramfsdir)
dist_initrd_SCRIPTS = \
       zfsunlock

SUBDIRS = conf.d conf-hooks.d hooks scripts
SHELLCHECKDIRS = hooks scripts

EXTRA_DIST = \
	README.initramfs.markdown
