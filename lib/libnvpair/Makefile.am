include $(top_srcdir)/config/Rules.am

DEFAULT_INCLUDES  = -I${moddir}/nvpair/include \
                    -I${libdir}/libspl/include

lib_LTLIBRARIES = libnvpair.la

libnvpair_la_SOURCES = ${moddir}/nvpair/nvpair.c \
                       ${libdir}/libnvpair/libnvpair.c \
                       ${libdir}/libnvpair/nvpair_alloc_system.c \
                       ${moddir}/nvpair/include/sys/nvpair.h \
                       ${moddir}/nvpair/include/sys/nvpair_impl.h
                       ${libdir}/libnvpair/include/libnvpair.h
