#	$NetBSD: Makefile.inc,v 1.9 2016/01/14 21:45:01 christos Exp $
#	@(#)Makefile.inc	8.1 (Berkeley) 6/4/93

# regex sources
.PATH: ${.CURDIR}/regex

CPPFLAGS+=-DPOSIX_MISTAKE

SRCS+=	regcomp.c regerror.c regexec.c regfree.c regsub.c

MAN+=	regex.3 re_format.7

MLINKS+=regex.3 regcomp.3 regex.3 regexec.3 regex.3 regerror.3 \
	regex.3 regfree.3 regex.3 regnsub.3 regex.3 regasub.3
