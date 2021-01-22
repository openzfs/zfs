include $(top_srcdir)/config/Rules.am

VPATH = $(top_srcdir)/module/avl/

# Includes kernel code, generate warnings for large stack frames
AM_CFLAGS += $(FRAME_LARGER_THAN)

noinst_LTLIBRARIES = libavl.la

KERNEL_C = \
	avl.c

nodist_libavl_la_SOURCES = \
	$(KERNEL_C)

include $(top_srcdir)/config/CppCheck.am
