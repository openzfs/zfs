include $(top_srcdir)/config/Rules.am

VPATH = \
	$(top_srcdir)/module/nvpair \
	$(top_srcdir)/lib/libnvpair

# Includes kernel code, generate warnings for large stack frames
# and required CFLAGS for libtirpc
AM_CFLAGS += $(FRAME_LARGER_THAN) $(LIBTIRPC_CFLAGS)

lib_LTLIBRARIES = libnvpair.la

include $(top_srcdir)/config/Abigail.am

USER_C = \
	libnvpair.c \
	libnvpair_json.c \
	nvpair_alloc_system.c

KERNEL_C = \
	nvpair_alloc_fixed.c \
	nvpair.c \
	fnvpair.c

dist_libnvpair_la_SOURCES = \
	$(USER_C)

nodist_libnvpair_la_SOURCES = \
	$(KERNEL_C)

libnvpair_la_LIBADD = \
	$(abs_top_builddir)/lib/libspl/libspl_assert.la

libnvpair_la_LIBADD += $(LIBTIRPC_LIBS) $(LTLIBINTL)

libnvpair_la_LDFLAGS =

if !ASAN_ENABLED
libnvpair_la_LDFLAGS += -Wl,-z,defs
endif

libnvpair_la_LDFLAGS += -version-info 3:0:0

include $(top_srcdir)/config/CppCheck.am

# Library ABI
EXTRA_DIST = libnvpair.abi libnvpair.suppr
