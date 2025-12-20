# SPDX-License-Identifier: CDDL-1.0
sbin_PROGRAMS   += zinject
CPPCHECKTARGETS += zinject

zinject_SOURCES = \
	%D%/translate.c \
	%D%/zinject.c \
	%D%/zinject.h

zinject_LDADD = \
	libzfs.la \
	libzfs_core.la \
	libnvpair.la
