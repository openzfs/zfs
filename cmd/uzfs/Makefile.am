include $(top_srcdir)/config/Rules.am

# Unconditionally enable ASSERTs
AM_CPPFLAGS += -DDEBUG -UNDEBUG -DZFS_DEBUG

bin_PROGRAMS = uzfs

uzfs_SOURCES = uzfs.c

uzfs_LDADD = \
	$(abs_top_builddir)/lib/libuzfs/libuzfs.la \
	$(abs_top_builddir)/lib/libzpool/libzpool.la

include $(top_srcdir)/config/CppCheck.am
