// SPDX-License-Identifier: BSD-2-Clause
#ifndef	_OPENSOLARIS_SYS_LIBKERN_H_
#define	_OPENSOLARIS_SYS_LIBKERN_H_

/*
 * FreeBSD <sys/libkern.h> stand-in.  NetBSD exports the same string/mem
 * helpers from <sys/systm.h> / <lib/libkern/libkern.h>.
 */
#ifdef _KERNEL
#include <sys/systm.h>
#include <lib/libkern/libkern.h>
#endif

#ifndef MIN
#define	MIN(a, b)	(((a) < (b)) ? (a) : (b))
#endif
#ifndef MAX
#define	MAX(a, b)	(((a) > (b)) ? (a) : (b))
#endif

#endif /* _OPENSOLARIS_SYS_LIBKERN_H_ */
