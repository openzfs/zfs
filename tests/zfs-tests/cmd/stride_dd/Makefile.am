include $(top_srcdir)/config/Rules.am

pkgexecdir = $(datadir)/@PACKAGE@/zfs-tests/bin

pkgexec_PROGRAMS = stride_dd
stride_dd_SOURCES = stride_dd.c
if !BUILD_MACOS
stride_dd_LDADD = -lrt
endif
