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
DISTLIBS = libuutil libzfs_core libzfs libzpool libzfsbootenv
SUBDIRS += $(DISTLIBS)
DISTLIBS += libnvpair

# An ABI is stored for each of these libraries.  Note that libzpool.so
# is only linked against by ztest and zdb and no stable ABI is provided.
ABILIBS = libnvpair libuutil libzfs_core libzfs libzfsbootenv

PHONY = checkabi storeabi
checkabi: $(ABILIBS)
	set -e ; for dir in $(ABILIBS) ; do \
		$(MAKE) -C $$dir checkabi ; \
	done

storeabi: $(ABILIBS)
	set -e ; for dir in $(ABILIBS) ; do \
		$(MAKE) -C $$dir storeabi ; \
	done
