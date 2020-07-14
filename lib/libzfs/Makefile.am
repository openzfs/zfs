include $(top_srcdir)/config/Rules.am

VPATH = \
	$(top_srcdir)/module/icp \
	$(top_srcdir)/module/zcommon \
	$(top_srcdir)/lib/libzfs

# Suppress unused but set variable warnings often due to ASSERTs
AM_CFLAGS += $(NO_UNUSED_BUT_SET_VARIABLE)
AM_CFLAGS += $(LIBCRYPTO_CFLAGS) $(ZLIB_CFLAGS)

pkgconfig_DATA = libzfs.pc

lib_LTLIBRARIES = libzfs.la

USER_C = \
	libzfs_changelist.c \
	libzfs_config.c \
	libzfs_crypto.c \
	libzfs_dataset.c \
	libzfs_diff.c \
	libzfs_import.c \
	libzfs_iter.c \
	libzfs_mount.c \
	libzfs_pool.c \
	libzfs_sendrecv.c \
	libzfs_status.c \
	libzfs_util.c


if BUILD_FREEBSD
USER_C += \
	os/freebsd/libzfs_compat.c \
	os/freebsd/libzfs_ioctl_compat.c \
	os/freebsd/libzfs_zmount.c
endif

if BUILD_LINUX
USER_C += \
	os/linux/libzfs_mount_os.c \
	os/linux/libzfs_pool_os.c \
	os/linux/libzfs_sendrecv_os.c \
	os/linux/libzfs_util_os.c
endif

KERNEL_C = \
	algs/sha2/sha2.c \
	cityhash.c \
	zfeature_common.c \
	zfs_comutil.c \
	zfs_deleg.c \
	zfs_fletcher.c \
	zfs_fletcher_aarch64_neon.c \
	zfs_fletcher_avx512.c \
	zfs_fletcher_intel.c \
	zfs_fletcher_sse.c \
	zfs_fletcher_superscalar.c \
	zfs_fletcher_superscalar4.c \
	zfs_namecheck.c \
	zfs_prop.c \
	zfs_uio.c \
	zpool_prop.c \
	zprop_common.c

dist_libzfs_la_SOURCES = \
	$(USER_C)

nodist_libzfs_la_SOURCES = \
	$(KERNEL_C)

libzfs_la_LIBADD = \
	$(abs_top_builddir)/lib/libshare/libshare.la \
	$(abs_top_builddir)/lib/libzfs_core/libzfs_core.la \
	$(abs_top_builddir)/lib/libnvpair/libnvpair.la \
	$(abs_top_builddir)/lib/libuutil/libuutil.la

libzfs_la_LIBADD += -lm $(LIBCRYPTO_LIBS) $(ZLIB_LIBS) $(LTLIBINTL)

libzfs_la_LDFLAGS = -pthread

if !ASAN_ENABLED
libzfs_la_LDFLAGS += -Wl,-z,defs
endif

if BUILD_FREEBSD
libzfs_la_LIBADD += -lutil -lgeom
libzfs_la_LDFLAGS += -version-info 4:0:0
else
libzfs_la_LDFLAGS += -version-info 2:0:0
endif

# Licensing data
EXTRA_DIST = THIRDPARTYLICENSE.openssl THIRDPARTYLICENSE.openssl.descrip
