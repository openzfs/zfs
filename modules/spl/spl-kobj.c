/*
 *  This file is part of the SPL: Solaris Porting Layer.
 *
 *  Copyright (c) 2008 Lawrence Livermore National Security, LLC.
 *  Produced at Lawrence Livermore National Laboratory
 *  Written by:
 *          Brian Behlendorf <behlendorf1@llnl.gov>,
 *          Herb Wartens <wartens2@llnl.gov>,
 *          Jim Garlick <garlick@llnl.gov>
 *  UCRL-CODE-235197
 *
 *  This is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA.
 */

#include <sys/kobj.h>

#ifdef DEBUG_SUBSYSTEM
#undef DEBUG_SUBSYSTEM
#endif

#define DEBUG_SUBSYSTEM S_KOBJ

struct _buf *
kobj_open_file(const char *name)
{
	struct _buf *file;
	vnode_t *vp;
	int rc;
	ENTRY;

	file = kmalloc(sizeof(_buf_t), GFP_KERNEL);
	if (file == NULL)
		RETURN((_buf_t *)-1UL);

	if ((rc = vn_open(name, UIO_SYSSPACE, FREAD, 0644, &vp, 0, 0))) {
		kfree(file);
		RETURN((_buf_t *)-1UL);
	}

	file->vp = vp;

	RETURN(file);
} /* kobj_open_file() */
EXPORT_SYMBOL(kobj_open_file);

void
kobj_close_file(struct _buf *file)
{
	ENTRY;
	VOP_CLOSE(file->vp, 0, 0, 0, 0, 0);
	VN_RELE(file->vp);
        kfree(file);
        EXIT;
} /* kobj_close_file() */
EXPORT_SYMBOL(kobj_close_file);

int
kobj_read_file(struct _buf *file, char *buf, ssize_t size, offset_t off)
{
	ENTRY;
	RETURN(vn_rdwr(UIO_READ, file->vp, buf, size, off,
	       UIO_SYSSPACE, 0, RLIM64_INFINITY, 0, NULL));
} /* kobj_read_file() */
EXPORT_SYMBOL(kobj_read_file);

int
kobj_get_filesize(struct _buf *file, uint64_t *size)
{
        vattr_t vap;
	int rc;
	ENTRY;

	rc = VOP_GETATTR(file->vp, &vap, 0, 0, NULL);
	if (rc)
		RETURN(rc);

        *size = vap.va_size;

        RETURN(rc);
} /* kobj_get_filesize() */
EXPORT_SYMBOL(kobj_get_filesize);
