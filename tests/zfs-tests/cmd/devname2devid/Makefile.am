include $(top_srcdir)/config/Rules.am

pkgexecdir = $(datadir)/@PACKAGE@/zfs-tests/bin

if WANT_DEVNAME2DEVID
pkgexec_PROGRAMS = devname2devid
devname2devid_SOURCES = devname2devid.c
devname2devid_LDADD = -ludev
endif
