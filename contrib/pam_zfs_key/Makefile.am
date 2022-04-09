%C%_pam_zfs_key_la_CFLAGS  = $(AM_CFLAGS)
%C%_pam_zfs_key_la_CFLAGS += $(LIBCRYPTO_CFLAGS)

pammodule_LTLIBRARIES = %D%/pam_zfs_key.la

%C%_pam_zfs_key_la_SOURCES = %D%/pam_zfs_key.c

%C%_pam_zfs_key_la_LIBADD = \
	libnvpair.la \
	libuutil.la \
	libzfs.la \
	libzfs_core.la

%C%_pam_zfs_key_la_LIBADD += -lpam $(LIBCRYPTO_LIBS)

%C%_pam_zfs_key_la_LDFLAGS = -version-info 1:0:0 -avoid-version -module -shared

dist_pamconfigs_DATA = %D%/zfs_key
