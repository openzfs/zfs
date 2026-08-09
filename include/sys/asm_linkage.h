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
 * Copyright 2005 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef _SYS_ASM_LINKAGE_H
#define	_SYS_ASM_LINKAGE_H

#define	ASMABI

#if defined(__i386) || defined(__amd64)

#include <sys/ia32/asm_linkage.h>	/* XX64	x86/sys/asm_linkage.h */

#endif

#if defined(_KERNEL) && defined(HAVE_KERNEL_OBJTOOL)

#include <asm/frame.h>

#else /* userspace */
#define	FRAME_BEGIN
#define	FRAME_END
#endif


#endif	/* _SYS_ASM_LINKAGE_H */
