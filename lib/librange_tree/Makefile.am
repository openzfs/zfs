# SPDX-License-Identifier: CDDL-1.0
librange_tree_la_CFLAGS  = $(AM_CFLAGS) $(KERNEL_CFLAGS) $(LIBRARY_CFLAGS)
librange_tree_la_CFLAGS += -fvisibility=hidden

noinst_LTLIBRARIES += librange_tree.la

nodist_librange_tree_la_SOURCES = \
	module/zfs/btree.c \
	module/zfs/range_tree.c
