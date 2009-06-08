include $(top_srcdir)/config/Rules.am

DEFAULT_INCLUDES += \
        -I${top_srcdir}/lib/libspl/include
COMPILE = $(CC) $(DEFS) $(DEFAULT_INCLUDES) $(INCLUDES) \
	  $(AM_CPPFLAGS) $(CPPFLAGS) $(AM_CFLAGS) $(CFLAGS)
atomic_SOURCE = atomic.c
atomic_ASM = atomic.S

# Generates assembly to simplify inclusion in ../Makefile.am
all-am:
	$(COMPILE) -c -S ${atomic_SOURCE} -o ${atomic_ASM}

clean-generic:
	rm ${atomic_ASM}
