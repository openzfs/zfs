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
 * Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef _SYS_VNODE_H
#define	_SYS_VNODE_H

typedef struct vattr {
	uint_t		va_mask;	/* bit-mask of attributes */
	u_offset_t	va_size;	/* file size in bytes */
} vattr_t;

/*
 * The kernel module uses the Linux ATTR_* values for the AT_* flags
 * (include/os/linux/spl/sys/vnode.h), and logs va_mask with them in
 * TX_SETATTR records, so the same values are used here.
 */
#define	AT_MODE		0x00001		/* ATTR_MODE */
#define	AT_UID		0x00002		/* ATTR_UID */
#define	AT_GID		0x00004		/* ATTR_GID */
#define	AT_SIZE		0x00008		/* ATTR_SIZE */
#define	AT_ATIME	0x00010		/* ATTR_ATIME */
#define	AT_MTIME	0x00020		/* ATTR_MTIME */
#define	AT_CTIME	0x00040		/* ATTR_CTIME */

#define	ATTR_XVATTR	(1U << 31)
#define	AT_XVATTR	ATTR_XVATTR

#define	CRCREAT		0

#define	F_FREESP	11
#define	FIGNORECASE	0x80000 /* request case-insensitive lookups */


#endif /* _SYS_VNODE_H */
