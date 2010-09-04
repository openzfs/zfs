include $(top_srcdir)/config/Rules.am

DEFAULT_INCLUDES += \
        -I$(top_srcdir)/lib/libspl/include

atomic_SOURCE = atomic.c
atomic_ASM = atomic.S

COMPILE = $(CC) $(DEFS) $(DEFAULT_INCLUDES) $(INCLUDES) \
	  $(AM_CPPFLAGS) $(CPPFLAGS) $(AM_CFLAGS) $(CFLAGS) -fPIC
EXTRA_DIST = $(atomic_SOURCE)

# Generates assembly to simplify inclusion in ../Makefile.am
all-am:
	$(COMPILE) -c -S $(atomic_SOURCE) -o $(atomic_ASM)

clean-generic:
	$(RM) $(atomic_ASM)
