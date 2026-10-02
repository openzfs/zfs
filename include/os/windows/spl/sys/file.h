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

#ifndef _SPL_FILE_H
#define	_SPL_FILE_H

#define	FIGNORECASE	0x00080000
#define	FLINKREPLACE	0x00100000 // Windows
/*
 * Windows-only: skip ZFS POSIX ACL check in zfs_rename because Windows
 * already validated DELETE access on the source FileObject at open time.
 */
#define	FBYPASS_ZFS_ACL	0x00200000
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
	uint64_t	f_win_offset; /* soft partition start */
	uint64_t	f_win_length; /* soft partition length */
};

#define	file_t struct spl_fileproc

void *getf(uint64_t fd);
void releasef(uint64_t fd);
void releasefp(struct spl_fileproc *fp);

/* O3X extended - get vnode from previos getf() */
struct vnode *getf_vnode(void *fp);

#endif /* SPL_FILE_H */
