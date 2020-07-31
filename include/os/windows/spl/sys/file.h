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

#ifndef _SPL_FILE_H
#define	_SPL_FILE_H

#define	FIGNORECASE	0x00080000
#define	FKIOCTL		0x80000000
#define	FCOPYSTR	0x40000000

#include <sys/list.h>

struct spl_fileproc {
	void		*f_vnode;
	list_node_t	f_next;
	uint64_t	f_fd;
	uint64_t	f_offset;
	void		*f_proc;
	void		*f_fp;
	int		f_writes;
	uint64_t	f_file;
	HANDLE		f_handle;
	void		*f_fileobject;
	void		*f_deviceobject;
};

#define	file_t struct spl_fileproc

void *getf(uint64_t fd);
void releasef(uint64_t fd);
void releasefp(struct spl_fileproc *fp);

/* O3X extended - get vnode from previos getf() */
struct vnode *getf_vnode(void *fp);

#endif /* SPL_FILE_H */
