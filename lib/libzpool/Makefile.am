include $(top_srcdir)/config/Rules.am

DEFAULT_INCLUDES += \
        -I${top_srcdir}/lib/libzpool/include \
        -I${top_srcdir}/lib/libspl/include \
        -I${top_srcdir}/lib/libnvpair/include \
        -I${top_srcdir}/module/zcommon/include \
        -I${top_srcdir}/module/nvpair/include \
        -I${top_srcdir}/module/avl/include \
        -I${top_srcdir}/module/zfs/include

lib_LTLIBRARIES = libzpool.la

libzpool_la_SOURCES = \
	${top_srcdir}/lib/libzpool/kernel.c \
	${top_srcdir}/lib/libzpool/taskq.c \
	${top_srcdir}/lib/libzpool/util.c \
        ${top_srcdir}/module/zcommon/zfs_comutil.c \
        ${top_srcdir}/module/zcommon/zfs_deleg.c \
        ${top_srcdir}/module/zcommon/zfs_namecheck.c \
        ${top_srcdir}/module/zcommon/zfs_prop.c \
        ${top_srcdir}/module/zcommon/zpool_prop.c \
        ${top_srcdir}/module/zcommon/zprop_common.c \
        ${top_srcdir}/module/zcommon/include/sys/fm/fs/zfs.h \
        ${top_srcdir}/module/zcommon/include/sys/fs \
        ${top_srcdir}/module/zcommon/include/sys/fs/zfs.h \
        ${top_srcdir}/module/zcommon/include/zfs_comutil.h \
        ${top_srcdir}/module/zcommon/include/zfs_deleg.h \
        ${top_srcdir}/module/zcommon/include/zfs_namecheck.h \
        ${top_srcdir}/module/zcommon/include/zfs_prop.h

# FIXME: ADD MISSING SOURCES FROM ZFS MODULE
