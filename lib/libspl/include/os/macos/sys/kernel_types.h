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

#ifndef LIBSPL_SYS_KERNEL_TYPES_H
#define	LIBSPL_SYS_KERNEL_TYPES_H

// This might need #ifdef for xcode version
// #define	_STRUCT_TIMEVAL32

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
