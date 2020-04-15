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
 *
 * Copyright (C) 2013 Jorgen Lundman <lundman@lundman.net>
 *
 */

#ifndef _SPL_FILE_H
#define	_SPL_FILE_H

#define	FIGNORECASE			0x00080000
#define	FKIOCTL				0x80000000
#define	ED_CASE_CONFLICT	0x10

#include <sys/list.h>

/*
 * XNU has all the proc structs as opaque and with no functions we
 * are allowed to call, so we implement file IO from within the kernel
 * as vnode operations.
 * The second mode is when we are given a "fd" from userland, which we
 * map in here, using getf()/releasef().
 * When it comes to IO, if "fd" is set, we use it (fo_rdwr()) as it
 * can handle both files, and pipes.
 * In kernel space file ops, we use vn_rdwr on the vnode.
 */
struct spl_fileproc {
	void		*f_vnode;	/* underlying vnode */
	list_node_t	f_next;		/* * next getf() link for releasef() */
	int		f_fd;		/* * userland file descriptor */
	off_t		f_offset;	/* offset for stateful IO */
	void		*f_proc;	/* opaque */
	void		*f_fp;		/* opaque */
	int		f_writes;	/* did write? for close sync */
	int		f_ioflags;	/* IO_APPEND */
	minor_t		f_file;		/* minor of the file */
	void		*f_private;	/* zfsdev_state_t */
};
/* Members with '*' are not used when 'fd' is not given */

void releasefp(struct spl_fileproc *fp);
void *getf(int fd);
void releasef(int fd);

struct vnode *getf_vnode(void *fp);

#endif /* SPL_FILE_H */
