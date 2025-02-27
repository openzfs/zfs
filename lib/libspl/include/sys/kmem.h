// SPDX-License-Identifier: CDDL-1.0
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
 * Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef _SYS_KMEM_H
#define	_SYS_KMEM_H

#include <stdlib.h>

#ifdef	__cplusplus
extern "C" {
#endif

#define	KM_SLEEP	0x00000000	/* same as KM_SLEEP */
#define	KM_NOSLEEP	0x00000001	/* same as KM_NOSLEEP */

#define	kmem_alloc(size, flags)		((void) sizeof (flags), malloc(size))
#define	kmem_free(ptr, size)		((void) sizeof (size), free(ptr))

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_KMEM_H */
