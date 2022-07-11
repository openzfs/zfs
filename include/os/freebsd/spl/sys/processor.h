/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or https://opensource.org/licenses/CDDL-1.0.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
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
