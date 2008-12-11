include $(top_srcdir)/config/Rules.am

DEFAULT_INCLUDES  = -I${moddir}/avl/include \
                    -I${libdir}/libspl/include

lib_LTLIBRARIES = libavl.la

libavl_la_SOURCES = ${moddir}/avl/avl.c \
                    ${moddir}/avl/include/sys/avl.h \
                    ${moddir}/avl/include/sys/avl_impl.h
