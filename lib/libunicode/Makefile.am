include $(top_srcdir)/config/Rules.am

VPATH = $(top_srcdir)/module/unicode

# Includes kernel code, generate warnings for large stack frames
AM_CFLAGS += $(FRAME_LARGER_THAN)

noinst_LTLIBRARIES = libunicode.la

KERNEL_C = \
	u8_textprep.c \
	uconv.c

nodist_libunicode_la_SOURCES = \
	$(KERNEL_C)

include $(top_srcdir)/config/CppCheck.am
