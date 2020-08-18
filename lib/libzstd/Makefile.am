include $(top_srcdir)/config/Rules.am

VPATH = $(top_srcdir)/module/zstd

# Includes kernel code, generate warnings for large stack frames
AM_CFLAGS += $(FRAME_LARGER_THAN)

noinst_LTLIBRARIES = libzstd.la

KERNEL_C = \
	lib/zstd.c \
	zfs_zstd.c

nodist_libzstd_la_SOURCES = $(KERNEL_C)

# -fno-tree-vectorize is set for gcc in zstd/common/compiler.h
# Set it for other compilers, too.
lib/zstd.$(OBJEXT):  CFLAGS += -fno-tree-vectorize
lib/zstd.l$(OBJEXT): CFLAGS += -fno-tree-vectorize

# Quiet warnings about frame size due to unused code in unmodified zstd lib
lib/zstd.$(OBJEXT):  CFLAGS += -Wframe-larger-than=20480
lib/zstd.l$(OBJEXT): CFLAGS += -Wframe-larger-than=20480
