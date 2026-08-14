// SPDX-License-Identifier: CDDL-1.0
#ifndef _LIBSPL_SYS_PARAM_H
#define	_LIBSPL_SYS_PARAM_H

#include_next <sys/param.h>
#include <unistd.h>

#ifdef MAXNAMELEN
#undef MAXNAMELEN
#endif
#define	MAXNAMELEN	256
#define	UID_NOACCESS	60002
#define	MAXUID		UINT32_MAX
#define	MAXPROJID	MAXUID

#ifdef	PAGESIZE
#undef	PAGESIZE
#endif

extern size_t spl_pagesize(void);
#define	PAGESIZE	(spl_pagesize())
#ifdef ptob
#undef ptob
#endif
#define	ptob(x)		((x) * PAGESIZE)

#ifndef nitems
#ifdef __arraycount
#define	nitems(x)	__arraycount(x)
#else
#define	nitems(x)	(sizeof (x) / sizeof ((x)[0]))
#endif
#endif
#endif
