libbtree_la_CFLAGS  = $(AM_CFLAGS) $(KERNEL_CFLAGS) $(LIBRARY_CFLAGS)
libbtree_la_CFLAGS += -fvisibility=hidden

noinst_LTLIBRARIES += libbtree.la

nodist_libbtree_la_SOURCES = \
	module/zfs/btree.c
