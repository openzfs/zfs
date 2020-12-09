/*
 * CDDL HEADER START
 *
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this
 * source.  A copy of the CDDL is also available via the Internet at
 * http://www.illumos.org/license/CDDL.
 *
 * CDDL HEADER END
 */
/*
 * Copyright (C) 2017 by Lawrence Livermore National Security, LLC.
 */

#ifndef _SYS_FRAME_H
#define	_SYS_FRAME_H

#ifdef	__cplusplus
extern "C" {
#endif

#if defined(__KERNEL__) && defined(HAVE_KERNEL_OBJTOOL) && \
    defined(HAVE_STACK_FRAME_NON_STANDARD)
#if defined(HAVE_KERNEL_OBJTOOL_HEADER)
#include <linux/objtool.h>
#else
#include <linux/frame.h>
#endif
#else
#define	STACK_FRAME_NON_STANDARD(func)
#endif

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_FRAME_H */
