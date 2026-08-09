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
 *	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T
 *	  All Rights Reserved
 *
 */

/*
 * Copyright 2014 Garrett D'Amore <garrett@damore.org>
 *
 * Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef _SYS_PROCESSOR_H
#define	_SYS_PROCESSOR_H

#include <sys/types.h>
#ifdef	__cplusplus
extern "C" {
#endif

/*
 * Definitions for p_online, processor_info & lgrp system calls.
 */

/*
 * Type for an lgrpid
 */
typedef uint16_t lgrpid_t;

/*
 * Type for processor name (CPU number).
 */
typedef	int	processorid_t;
typedef int	chipid_t;

#define	getcpuid() curcpu

#ifdef __cplusplus
}
#endif

#endif	/* _SYS_PROCESSOR_H */
