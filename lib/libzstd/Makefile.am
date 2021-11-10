include $(top_srcdir)/config/Rules.am

VPATH = $(top_srcdir)/module/zstd

# -fno-tree-vectorize is set for gcc in zstd/common/compiler.h
# Set it for other compilers, too.
AM_CFLAGS += -fno-tree-vectorize

noinst_LTLIBRARIES = libzstd.la

KERNEL_C = \
	lib/zstd.c \
	zfs_zstd.c

nodist_libzstd_la_SOURCES = $(KERNEL_C)

lib/zstd.$(OBJEXT):  CFLAGS += -fno-tree-vectorize -include $(top_srcdir)/module/zstd/include/zstd_compat_wrapper.h -Wp,-w
lib/zstd.l$(OBJEXT): CFLAGS += -fno-tree-vectorize -include $(top_srcdir)/module/zstd/include/zstd_compat_wrapper.h -Wp,-w

zfs_zstd.$(OBJEXT):  CFLAGS += -include $(top_srcdir)/module/zstd/include/zstd_compat_wrapper.h
zfs_zstd.l$(OBJEXT): CFLAGS += -include $(top_srcdir)/module/zstd/include/zstd_compat_wrapper.h

include $(top_srcdir)/config/CppCheck.am
