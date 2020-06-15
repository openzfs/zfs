
# Anyone remember why we made this a library?
libspl_la_CPPFLAGS= \
	-Wall \
	-nostdinc \
	-mkernel \
	-fno-builtin-printf \
	-D_KERNEL \
	-DKERNEL \
	-DKERNEL_PRIVATE \
	-DDRIVER_PRIVATE \
	-DAPPLE \
	-DNeXT \
	-include $(top_builddir)/zfs_config.h \
	-I$(top_srcdir)/include/os/macos/spl \
	-I$(top_srcdir)/include \
	-I@KERNEL_HEADERS@/Headers \
	-I@KERNEL_HEADERS@/PrivateHeaders

libspl_la_CPPFLAGS += @KERNEL_DEBUG_CPPFLAGS@

libspl_la_LDFLAGS= \
	-Xlinker \
	-kext \
	-nostdlib \
	-lkmodc++ \
	-lkmod \
	-lcc_kext

libspl_la_LIBS = -lnone

# If we don't set this to nothing, it adds "-lz -liconv"
LIBS =

noinst_LTLIBRARIES = libspl.la

libspl_la_SOURCES = 	\
	spl-atomic.c \
	spl-condvar.c \
	spl-cred.c \
	spl-ddi.c \
	spl-err.c \
	spl-kmem.c \
	spl-kstat.c \
	spl-list.c \
	spl-mutex.c \
	spl-osx.c \
	spl-policy.c \
	spl-proc.c \
	spl-processor.c \
	spl-proc_list.c \
	spl-rwlock.c \
	spl-seg_kmem.c \
	spl-taskq.c \
	spl-thread.c \
	spl-time.c \
	spl-tsd.c \
	spl-uio.c \
	spl-vmem.c \
	spl-vnode.c \
	spl-xdr.c
