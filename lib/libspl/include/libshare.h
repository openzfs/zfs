/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
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
 * Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */
#ifndef _LIBSPL_LIBSHARE_H
#define _LIBSPL_LIBSHARE_H

typedef void *sa_handle_t;	/* opaque handle to access core functions */
typedef void *sa_group_t;
typedef void *sa_share_t;

/* API Initialization */
#define	SA_INIT_SHARE_API	0x0001	/* init share specific interface */
#define	SA_INIT_CONTROL_API	0x0002	/* init control specific interface */

/* Error values */
#define	SA_OK			0
#define	SA_NO_MEMORY		2	/* no memory for data structures */
#define	SA_CONFIG_ERR		6	/* system configuration error */

#endif /* _LIBSPL_LIBSHARE_H */
