include $(top_srcdir)/config/Rules.am

VPATH = $(top_srcdir)/module/zstd

DEFAULT_INCLUDES += -I$(top_srcdir)/module/zstd

noinst_LTLIBRARIES = libzstd.la

KERNEL_C = \
	zstdlib.c \
	zstd.c

nodist_libzstd_la_SOURCES = $(KERNEL_C)

# -fno-tree-vectorize is set for gcc in zstd/common/compiler.h
# Set it for other compilers, too.
zstdlib.$(OBJEXT): CFLAGS += -fno-tree-vectorize
zstdlib.l$(OBJEXT): CFLAGS += -fno-tree-vectorize
