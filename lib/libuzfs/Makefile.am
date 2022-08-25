include $(top_srcdir)/config/Rules.am

# Suppress unused but set variable warnings often due to ASSERTs
AM_CFLAGS += $(NO_UNUSED_BUT_SET_VARIABLE)

# Unconditionally enable ASSERTs
AM_CPPFLAGS += -DDEBUG -UNDEBUG -DZFS_DEBUG

lib_LTLIBRARIES = libuzfs.la

USER_C = libuzfs.c

libuzfs_la_SOURCES = $(USER_C)

libuzfs_la_LIBADD = $(abs_top_builddir)/lib/libzpool/libzpool.la

include $(top_srcdir)/config/CppCheck.am
