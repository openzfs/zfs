#
# Shown below is a simplified dependency graph of the OpenZFS provided
# libraries.  Administrative commands (`zfs`, `zpool`, etc) interface with
# the kernel modules using the `libzfs.so` and `libzfs_core.so` libraries.
# These libraries provide a stable ABI across OpenZFS point releases.
#
# The `libzpool.so` library is a user space build of the DMU and SPA layers
# used to implement debugging tools (zdb) and code validation tools (ztest).
# These library interfaces are subject to change at any time.
#
#
# CMDS:   zhack/ztest/zdb/            zfs/zpool/zed/
#        raidz_{test,bench}          zinject/zstream
#                |                          |
# LIBS:          |                          |              libzfsbootenv*
#                |                          |                    |
#                |                          |                    |
#             libzpool                   libzfs* ----------------+
#             | | |  \                  / | | |
#    libicp --/ | |   \                /  | | \------- libshare
#               | |    \              /   | |
#    libzstd ---/ |     \            /    | \--------- libuutil
#                 |      \          /     \              | |
#    libunicode --/       \        /       \             | |
#                          \      /         \            | |
#                          libzutil        libzfs_core*  | |
#                          | | | | \          |     |    | |
#                          | | | | |          |     |    | |
#                          | | | | |          |     |    | |
#    libtpool -------------/ | | | \---- libnvpair* |    | |
#                            | | |                  |    | |
#    libefi -----------------/ | \------ libavl* --------/ |
#                              |                    |      |
#                              \-------- libspl ----+------/
#
# * - A stable ABI is provided for these libraries
#
#
# NB: GNU Automake Manual, Chapter 8.3.5: Libtool Convenience Libraries
# These nine libraries are intermediary build components.
#
SUBDIRS = libavl libicp libshare libspl libtpool libzstd
CPPCHECKDIRS  = libavl libicp libnvpair libshare libspl libtpool libunicode
CPPCHECKDIRS += libuutil libzfs libzfs_core libzfsbootenv libzpool libzutil

if BUILD_LINUX
SUBDIRS += libefi
CPPCHECKDIRS += libefi
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

PHONY = checkabi storeabi cppcheck
checkabi: $(ABILIBS)
	set -e ; for dir in $(ABILIBS) ; do \
		$(MAKE) -C $$dir checkabi ; \
	done

storeabi: $(ABILIBS)
	set -e ; for dir in $(ABILIBS) ; do \
		$(MAKE) -C $$dir storeabi ; \
	done

cppcheck: $(CPPCHECKDIRS)
	set -e ; for dir in $(CPPCHECKDIRS) ; do \
		$(MAKE) -C $$dir cppcheck ; \
	done
