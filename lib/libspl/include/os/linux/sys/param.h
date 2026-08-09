// SPDX-License-Identifier: CDDL-1.0
/*
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this
 * source.  A copy of the CDDL is also available via the Internet at
 * https://opensource.org/license/CDDL-1.0.
 */
/*
 * Copyright 2006 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef _LIBSPL_SYS_PARAM_H
#define	_LIBSPL_SYS_PARAM_H

#include_next <sys/param.h>
#include <unistd.h>

/*
 * File system parameters and macros.
 *
 * The file system is made out of blocks of at most MAXBSIZE units,
 * with smaller units (fragments) only in the last direct block.
 * MAXBSIZE primarily determines the size of buffers in the buffer
 * pool. It may be made larger without any effect on existing
 * file systems; however making it smaller may make some file
 * systems unmountable.
 *
 * Note that the blocked devices are assumed to have DEV_BSIZE
 * "sectors" and that fragments must be some multiple of this size.
 */
#define	MAXBSIZE	8192
#define	DEV_BSIZE	512
#define	DEV_BSHIFT	9		/* log2(DEV_BSIZE) */

#define	MAXNAMELEN	256
#define	MAXOFFSET_T	LLONG_MAX

#define	UID_NOBODY	60001		/* user ID no body */
#define	GID_NOBODY	UID_NOBODY
#define	UID_NOACCESS	60002		/* user ID no access */

#define	MAXUID		UINT32_MAX	/* max user id */
#define	MAXPROJID	MAXUID		/* max project id */

#ifdef	PAGESIZE
#undef	PAGESIZE
#endif /* PAGESIZE */

extern size_t spl_pagesize(void);
#define	PAGESIZE	(spl_pagesize())

#define	ptob(x)		((x) * PAGESIZE)

#endif
