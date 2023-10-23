libnvpair_la_CFLAGS  = $(AM_CFLAGS) $(KERNEL_CFLAGS) $(LIBRARY_CFLAGS)
libnvpair_la_CFLAGS += $(LIBTIRPC_CFLAGS)
libnvpair_la_CFLAGS += -fvisibility=hidden

# wchar_t is undefined-signedness, but we compare to >=0; this warns with unsigned wchar_t
%D%/libnvpair_la-libnvpair_json.$(OBJEXT) : CFLAGS += -Wno-type-limits
%D%/libnvpair_la-libnvpair_json.l$(OBJEXT): CFLAGS += -Wno-type-limits


if BUILD_MACOS
# See comment in macOS rpc/xdr.h, starting at:
# "A xdrproc_t exists for each data type which is to be encoded or decoded."
module/nvpair/nvpair.$(OBJEXT): CFLAGS += -Wno-incompatible-pointer-types
../../module/nvpair/nvpair.$(OBJEXT): CFLAGS += -Wwgat -Wno-incompatible-pointer-types
%D%/libnvpair_la-nvpair.$(OBJEXT) : CFLAGS += -Wno-type-limits
nvpair.$(OBJEXT) : CFLAGS += -Wno-type-limits2
libnvpair_la-nvpair.$(OBJEXT) : CFLAGS += -Wno-type-limits3
%D%/../../module/nvpair/libnvpair_la-nvpair.$(OBJEXT) : CFLAGS += -Wno-type-limits4
endif
nvpair.lo : CFLAGS += -Wno-type-limits2
libnvpair_la-nvpair.lo : CFLAGS += -Wno-type-limits2
%D%/libnvpair_la-nvpair.lo : CFLAGS += -Wno-type-limits2
../../module/nvpair/nvpair.lo : CFLAGS += -Wno-type-limits2
module/nvpair/nvpair.lo : CFLAGS += -Wno-type-limits2
module/nvpair/libnvpair_la-nvpair.lo : CFLAGS += -Wno-type-limits2




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

if !BUILD_MACOS
if !ASAN_ENABLED
libnvpair_la_LDFLAGS += -Wl,-z,defs
endif
endif

libnvpair_la_LDFLAGS += -version-info 3:0:0

dist_noinst_DATA += %D%/libnvpair.abi %D%/libnvpair.suppr
