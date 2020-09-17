# NB: GNU Automake Manual, Chapter 8.3.5: Libtool Convenience Libraries
# These nine libraries are intermediary build components.
SUBDIRS = libavl libicp libshare libspl libtpool libzstd

if BUILD_LINUX
SUBDIRS += libefi
endif

# libnvpair is installed as part of the final build product
# libzutil depends on it, so it must be compiled before libzutil
SUBDIRS += libnvpair

# libzutil depends on libefi if present
SUBDIRS += libzutil libunicode

# These five libraries, which are installed as the final build product,
# incorporate the eight convenience libraries given above.
SUBDIRS += libuutil libzfs_core libzfs libzpool libzfsbootenv
