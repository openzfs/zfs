include $(top_srcdir)/config/Rules.am

# Disable GCC stack protection for zvol_id.  This is a kludge and should be
# removed once https://github.com/zfsonlinux/zfs/issues/569 is resolved.
AM_CFLAGS += -fno-stack-protector

DEFAULT_INCLUDES += \
	-I$(top_srcdir)/include \
	-I$(top_srcdir)/lib/libspl/include

udev_PROGRAMS = zvol_id

zvol_id_SOURCES = \
	zvol_id_main.c
