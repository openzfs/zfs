include $(top_srcdir)/config/Rules.am

pkgexecdir = $(datadir)/@PACKAGE@/zfs-tests/bin

pkgexec_PROGRAMS = nvlist_to_lua

nvlist_to_lua_SOURCES = nvlist_to_lua.c
nvlist_to_lua_LDADD = \
	$(abs_top_builddir)/lib/libzfs_core/libzfs_core.la \
	$(abs_top_builddir)/lib/libnvpair/libnvpair.la
