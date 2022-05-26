libnvpair_la_CFLAGS  = $(AM_CFLAGS) $(KERNEL_CFLAGS) $(LIBRARY_CFLAGS)
libnvpair_la_CFLAGS += $(LIBTIRPC_CFLAGS)
libnvpair_la_CFLAGS += -fvisibility=hidden

# wchar_t is undefined-signedness, but we compare to >=0; this warns with unsigned wchar_t
%D%/libnvpair_la-libnvpair_json.$(OBJEXT) : CFLAGS += -Wno-type-limits
%D%/libnvpair_la-libnvpair_json.l$(OBJEXT): CFLAGS += -Wno-type-limits

lib_LTLIBRARIES += libnvpair.la
CPPCHECKTARGETS += libnvpair.la

dist_libnvpair_la_SOURCES = \
	%D%/libnvpair.c \
	%D%/libnvpair_json.c \
	%D%/nvpair_alloc_system.c

nodist_libnvpair_la_SOURCES = \
	module/nvpair/nvpair_alloc_fixed.c \
	module/nvpair/nvpair.c \
	module/nvpair/fnvpair.c

libnvpair_la_LIBADD = \
	libspl_assert.la

libnvpair_la_LIBADD += $(LIBTIRPC_LIBS) $(LTLIBINTL)

libnvpair_la_LDFLAGS =

if !ASAN_ENABLED
libnvpair_la_LDFLAGS += -Wl,-z,defs
endif

libnvpair_la_LDFLAGS += -version-info 3:0:0

dist_noinst_DATA += %D%/libnvpair.abi %D%/libnvpair.suppr
