/*****************************************************************************\
 *  Copyright (C) 2007-2010 Lawrence Livermore National Security, LLC.
 *  Copyright (C) 2007 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Brian Behlendorf <behlendorf1@llnl.gov>.
 *  UCRL-CODE-235197
 *
 *  This file is part of the SPL, Solaris Porting Layer.
 *  For details, see <http://zfsonlinux.org/>.
 *
 *  The SPL is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the
 *  Free Software Foundation; either version 2 of the License, or (at your
 *  option) any later version.
 *
 *  The SPL is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with the SPL.  If not, see <http://www.gnu.org/licenses/>.
\*****************************************************************************/

#ifndef _SPL_KOBJ_H
#define _SPL_KOBJ_H

#include <sys/vnode.h>

typedef struct _buf {
	vnode_t *vp;
} _buf_t;

typedef struct _buf buf_t;

extern struct _buf *kobj_open_file(const char *name);
extern void kobj_close_file(struct _buf *file);
extern int kobj_read_file(struct _buf *file, char *buf,
			  ssize_t size, offset_t off);
extern int kobj_get_filesize(struct _buf *file, uint64_t *size);

#endif /* SPL_KOBJ_H */
