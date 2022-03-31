include $(top_srcdir)/config/Rules.am

VPATH = $(top_srcdir)/module/unicode

# Includes kernel code, generate warnings for large stack frames
AM_CFLAGS += $(FRAME_LARGER_THAN)

# See https://debbugs.gnu.org/cgi/bugreport.cgi?bug=54020
AM_CFLAGS += -no-suppress

noinst_LTLIBRARIES = libunicode.la

KERNEL_C = \
	u8_textprep.c \
	uconv.c

nodist_libunicode_la_SOURCES = \
	$(KERNEL_C)

include $(top_srcdir)/config/CppCheck.am
