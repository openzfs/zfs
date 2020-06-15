include $(top_srcdir)/config/Rules.am

AUTOMAKE_OPTIONS = subdir-objects

#DEFAULT_INCLUDES += \
#	-I$(top_srcdir)/include \
#	-I$(top_srcdir)/lib/libspl/include

sbin_PROGRAMS = zconfigd

zconfigd_SOURCES = \
	zconfigd.c \
	zconfigd.h

zconfigd_LDFLAGS = -framework IOKit -framework Foundation
