include $(top_srcdir)/config/Rules.am

VPATH = $(top_srcdir)/module/unicode

AM_CFLAGS += $(DEBUG_STACKFLAGS) $(FRAME_LARGER_THAN)

DEFAULT_INCLUDES += \
	-I$(top_srcdir)/include \
	-I$(top_srcdir)/lib/libspl/include

noinst_LTLIBRARIES = libunicode.la

USER_C =

KERNEL_C = \
	u8_textprep.c \
	uconv.c

nodist_libunicode_la_SOURCES = \
	$(USER_C) \
	$(KERNEL_C)

EXTRA_DIST = $(USER_C)
