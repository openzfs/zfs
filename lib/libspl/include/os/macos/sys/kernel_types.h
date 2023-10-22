/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License, Version 1.0 only
 * (the "License").  You may not use this file except in compliance
 * with the License.
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

#ifndef LIBSPL_SYS_KERNEL_TYPES_H
#define	LIBSPL_SYS_KERNEL_TYPES_H

/*
 * Unfortunately, XNU defines uio_t, proc_t and vnode_t differently to
 * ZFS, so we need to hack around it.
 */

#undef vnode_t
#undef uio_t
#define	proc_t kernel_proc_t
#include_next <sys/kernel_types.h>
#define	vnode_t struct vnode
#define	uio_t struct uio
#undef proc_t


/* Other missing Linux types */
typedef	off_t	loff_t;

#endif
