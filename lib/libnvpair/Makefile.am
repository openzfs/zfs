include $(top_srcdir)/config/Rules.am

DEFAULT_INCLUDES  = -I${moddir}/nvpair/include \
                    -I${libdir}/libnvpair/include \
                    -I${libdir}/libspl/include

lib_LTLIBRARIES = libnvpair.la

libnvpair_la_SOURCES = libnvpair.c \
                       nvpair_alloc_system.c \
                       include/libnvpair.h
                       ${moddir}/nvpair/nvpair.c \
                       ${moddir}/nvpair/include/sys/nvpair.h \
                       ${moddir}/nvpair/include/sys/nvpair_impl.h \
