include $(top_srcdir)/config/Rules.am

AM_CFLAGS += $(LIBCRYPTO_CFLAGS)

pammodule_LTLIBRARIES=pam_zfs_key.la

pam_zfs_key_la_SOURCES = pam_zfs_key.c

pam_zfs_key_la_LIBADD = \
	$(abs_top_builddir)/lib/libnvpair/libnvpair.la \
	$(abs_top_builddir)/lib/libuutil/libuutil.la \
	$(abs_top_builddir)/lib/libzfs/libzfs.la \
	$(abs_top_builddir)/lib/libzfs_core/libzfs_core.la

pam_zfs_key_la_LDFLAGS = -version-info 1:0:0 -avoid-version -module -shared

pam_zfs_key_la_LIBADD += -lpam $(LIBCRYPTO_LIBS)

dist_pamconfigs_DATA = zfs_key
