include $(top_srcdir)/config/Rules.am

DEFAULT_INCLUDES  = -I${moddir}/zcommon/include \
                    -I${libdir}/libspl/include

lib_LTLIBRARIES = libzcommon.la

libzfs_la_SOURCES = ${moddir}/zcommon/zfs_comutil.c \
                    ${moddir}/zcommon/zfs_deleg.c \
                    ${moddir}/zcommon/zfs_namecheck.c \
                    ${moddir}/zcommon/zfs_prop.c \
                    ${moddir}/zcommon/zpool_prop.c \
                    ${moddir}/zcommon/zprop_common.c \
                    ${moddir}/zcommon/include/sys/fm/fs/zfs.h \
                    ${moddir}/zcommon/include/sys/fs \
                    ${moddir}/zcommon/include/sys/fs/zfs.h \
                    ${moddir}/zcommon/include/zfs_comutil.h \
                    ${moddir}/zcommon/include/zfs_deleg.h \
                    ${moddir}/zcommon/include/zfs_namecheck.h \
                    ${moddir}/zcommon/include/zfs_prop.h \
