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
 * Copyright (C) 2011 Lawrence Livermore National Security, LLC.
 */

#ifndef _ZFS_XATTR_H
#define _ZFS_XATTR_H

/*
 * 2.6.35 API change,
 * The const keyword was added to the 'struct xattr_handler' in the
 * generic Linux super_block structure.  To handle this we define an
 * appropriate xattr_handler_t typedef which can be used.  This was
 * the preferred solution because it keeps the code clean and readable.
 */
#ifdef HAVE_CONST_XATTR_HANDLER
typedef const struct xattr_handler	xattr_handler_t;
#else
typedef struct xattr_handler		xattr_handler_t;
#endif

#endif /* _ZFS_XATTR_H */
