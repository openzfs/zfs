include $(top_srcdir)/config/Rules.am

VPATH = \
	$(top_srcdir)/module/icp \
	$(top_srcdir)/module/zcommon \
	$(top_srcdir)/lib/libzfs

# Suppress unused but set variable warnings often due to ASSERTs
AM_CFLAGS += $(NO_UNUSED_BUT_SET_VARIABLE)

libzfs_pcdir = $(datarootdir)/pkgconfig
libzfs_pc_DATA = libzfs.pc libzfs_core.pc

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

nodist_libzfs_la_SOURCES = \
	$(USER_C) \
	$(KERNEL_C)

libzfs_la_LIBADD = \
	$(top_builddir)/lib/libnvpair/libnvpair.la \
	$(top_builddir)/lib/libuutil/libuutil.la \
	$(top_builddir)/lib/libzfs_core/libzfs_core.la \
	$(top_builddir)/lib/libzutil/libzutil.la

if BUILD_LINUX
libzfs_la_LIBADD += \
	$(top_builddir)/lib/libshare/libshare.la
endif

libzfs_la_LDFLAGS = -version-info 2:0:0

libzfs_la_LIBADD += -lm $(LIBSSL)

EXTRA_DIST = $(libzfs_pc_DATA) $(USER_C)

# Licensing data
EXTRA_DIST += THIRDPARTYLICENSE.openssl THIRDPARTYLICENSE.openssl.descrip
