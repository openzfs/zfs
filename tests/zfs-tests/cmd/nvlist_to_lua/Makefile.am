include $(top_srcdir)/config/Rules.am

pkgexecdir = $(datadir)/@PACKAGE@/zfs-tests/bin

DEFAULT_INCLUDES += \
	-I$(top_srcdir)/include \
	-I$(top_srcdir)/lib/libspl/include

pkgexec_PROGRAMS = nvlist_to_lua

nvlist_to_lua_SOURCES = nvlist_to_lua.c
nvlist_to_lua_LDADD = \
	$(top_builddir)/lib/libnvpair/libnvpair.la \
	$(top_builddir)/lib/libzfs_core/libzfs_core.la
