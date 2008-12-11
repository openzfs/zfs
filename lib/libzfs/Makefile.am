include $(top_srcdir)/config/Rules.am

DEFAULT_INCLUDES  = -I${libdir}/libzfs/include \
                    -I${libdir}/libspl/include

lib_LTLIBRARIES = libzfs.la

libzfs_la_SOURCES = libzfs_changelist.c \
                    libzfs_config.c \
                    libzfs_dataset.c \
                    libzfs_graph.c \
                    libzfs_import.c \
                    libzfs_mount.c \
                    libzfs_pool.c \
                    libzfs_sendrecv.c \
                    libzfs_status.c \
                    libzfs_util.c \
                    include/libzfs.h \
                    include/libzfs_impl.h
