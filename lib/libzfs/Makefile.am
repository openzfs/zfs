libzfs_la_CFLAGS  = $(AM_CFLAGS) $(LIBRARY_CFLAGS)
libzfs_la_CFLAGS += $(LIBCRYPTO_CFLAGS) $(ZLIB_CFLAGS)
libzfs_la_CFLAGS += -fvisibility=hidden

lib_LTLIBRARIES += libzfs.la
CPPCHECKTARGETS += libzfs.la

dist_libzfs_la_SOURCES = \
	%D%/libzfs_impl.h \
	%D%/libzfs_changelist.c \
	%D%/libzfs_config.c \
	%D%/libzfs_crypto.c \
	%D%/libzfs_dataset.c \
	%D%/libzfs_diff.c \
	%D%/libzfs_import.c \
	%D%/libzfs_iter.c \
	%D%/libzfs_mount.c \
	%D%/libzfs_pool.c \
	%D%/libzfs_sendrecv.c \
	%D%/libzfs_status.c \
	%D%/libzfs_util.c

if BUILD_FREEBSD
dist_libzfs_la_SOURCES += \
	%D%/os/freebsd/libzfs_compat.c \
	%D%/os/freebsd/libzfs_zmount.c
endif

if BUILD_LINUX
dist_libzfs_la_SOURCES += \
	%D%/os/linux/libzfs_mount_os.c \
	%D%/os/linux/libzfs_pool_os.c \
	%D%/os/linux/libzfs_util_os.c
endif

nodist_libzfs_la_SOURCES = \
	module/zcommon/cityhash.c \
	module/zcommon/zfeature_common.c \
	module/zcommon/zfs_comutil.c \
	module/zcommon/zfs_deleg.c \
	module/zcommon/zfs_fletcher.c \
	module/zcommon/zfs_fletcher_aarch64_neon.c \
	module/zcommon/zfs_fletcher_avx512.c \
	module/zcommon/zfs_fletcher_intel.c \
	module/zcommon/zfs_fletcher_sse.c \
	module/zcommon/zfs_fletcher_superscalar.c \
	module/zcommon/zfs_fletcher_superscalar4.c \
	module/zcommon/zfs_namecheck.c \
	module/zcommon/zfs_prop.c \
	module/zcommon/zfs_valstr.c \
	module/zcommon/zpool_prop.c \
	module/zcommon/zprop_common.c

libzfs_la_LIBADD = \
	libshare.la \
	libzfs_core.la \
	libnvpair.la \
	libzutil.la \
	libuutil.la

libzfs_la_LIBADD += -lrt -lm $(LIBCRYPTO_LIBS) $(ZLIB_LIBS) $(LIBFETCH_LIBS) $(LTLIBINTL)

libzfs_la_LDFLAGS = -pthread

if !ASAN_ENABLED
libzfs_la_LDFLAGS += -Wl,-z,defs
endif

if BUILD_FREEBSD
libzfs_la_LIBADD += -lutil -lgeom
endif

libzfs_la_LDFLAGS += -version-info 6:0:0

pkgconfig_DATA += %D%/libzfs.pc

dist_noinst_DATA += %D%/libzfs.abi %D%/libzfs.suppr
dist_noinst_DATA += %D%/THIRDPARTYLICENSE.openssl %D%/THIRDPARTYLICENSE.openssl.descrip
