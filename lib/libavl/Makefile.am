include $(top_srcdir)/config/Rules.am

VPATH = $(top_srcdir)/module/avl/

# Includes kernel code, generate warnings for large stack frames
AM_CFLAGS += $(FRAME_LARGER_THAN)

noinst_LTLIBRARIES = libavl.la

USER_C =

KERNEL_C = \
	avl.c

nodist_libavl_la_SOURCES = \
	$(USER_C) \
	$(KERNEL_C)

EXTRA_DIST = $(USER_C)
