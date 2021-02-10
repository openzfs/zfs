include $(top_srcdir)/config/Rules.am

# Unconditionally enable debugging for zhack
AM_CPPFLAGS += -DDEBUG -UNDEBUG -DZFS_DEBUG

sbin_PROGRAMS = zhack

zhack_SOURCES = \
	zhack.c

zhack_LDADD = \
	$(abs_top_builddir)/lib/libzpool/libzpool.la \
	$(abs_top_builddir)/lib/libzfs_core/libzfs_core.la \
	$(abs_top_builddir)/lib/libnvpair/libnvpair.la

include $(top_srcdir)/config/CppCheck.am
