include $(top_srcdir)/config/Rules.am

AM_CFLAGS += $(DEBUG_STACKFLAGS) $(FRAME_LARGER_THAN)

DEFAULT_INCLUDES += \
	-I$(top_srcdir)/include \
	-I$(top_srcdir)/lib/libspl/include

noinst_LTLIBRARIES = libunicode.la

libunicode_la_SOURCES = \
	$(top_srcdir)/module/unicode/u8_textprep.c \
	$(top_srcdir)/module/unicode/uconv.c
