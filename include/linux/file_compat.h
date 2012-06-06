/*****************************************************************************\
 *  Copyright (C) 2007-2010 Lawrence Livermore National Security, LLC.
 *  Copyright (C) 2007 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Brian Behlendorf <behlendorf1@llnl.gov>.
 *  UCRL-CODE-235197
 *
 *  This file is part of the SPL, Solaris Porting Layer.
 *  For details, see <http://github.com/behlendorf/spl/>.
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

#ifndef _SPL_FILE_COMPAT_H
#define _SPL_FILE_COMPAT_H

#include <linux/fs.h>
#ifdef HAVE_FDTABLE_HEADER
#include <linux/fdtable.h>
#endif

static inline struct file *
spl_filp_open(const char *name, int flags, int mode, int *err)
{
        struct file *filp = NULL;
        int rc;

        filp = filp_open(name, flags, mode);
        if (IS_ERR(filp)) {
                rc = PTR_ERR(filp);
                if (err)
                        *err = rc;
                filp = NULL;
        }
        return filp;
}

#define spl_filp_close(f)		filp_close(f, NULL)
#define spl_filp_poff(f)		(&(f)->f_pos)
#define spl_filp_write(fp, b, s, p)	(fp)->f_op->write((fp), (b), (s), p)

#ifdef HAVE_VFS_FSYNC
# ifdef HAVE_2ARGS_VFS_FSYNC
#  define spl_filp_fsync(fp, sync)	vfs_fsync(fp, sync)
# else
#  define spl_filp_fsync(fp, sync)	vfs_fsync(fp, (fp)->f_dentry, sync)
# endif /* HAVE_2ARGS_VFS_FSYNC */
#else
# include <linux/buffer_head.h>
# define spl_filp_fsync(fp, sync)	file_fsync(fp, (fp)->f_dentry, sync)
#endif /* HAVE_VFS_FSYNC */

#ifdef HAVE_INODE_I_MUTEX
#define spl_inode_lock(ip)		(mutex_lock(&(ip)->i_mutex))
#define spl_inode_lock_nested(ip, type)	(mutex_lock_nested((&(ip)->i_mutex),  \
					(type)))
#define spl_inode_unlock(ip)		(mutex_unlock(&(ip)->i_mutex))
#else
#define spl_inode_lock(ip)		(down(&(ip)->i_sem))
#define spl_inode_unlock(ip)		(up(&(ip)->i_sem))
#endif /* HAVE_INODE_I_MUTEX */

#ifdef HAVE_KERN_PATH_PARENT_HEADER
# ifndef HAVE_KERN_PATH_PARENT_SYMBOL
typedef int (*kern_path_parent_t)(const char *, struct nameidata *);
extern kern_path_parent_t kern_path_parent_fn;
#  define spl_kern_path_parent(path, nd)	kern_path_parent_fn(path, nd)
# else
#  define spl_kern_path_parent(path, nd)	kern_path_parent(path, nd)
# endif /* HAVE_KERN_PATH_PARENT_SYMBOL */
#else
# define spl_kern_path_parent(path, nd)	path_lookup(path, LOOKUP_PARENT, nd)
#endif /* HAVE_KERN_PATH_PARENT_HEADER */

#ifndef HAVE_CLEAR_CLOSE_ON_EXEC
#define __clear_close_on_exec(fd, fdt)	FD_CLR(fd, fdt->close_on_exec)
#endif

#endif /* SPL_FILE_COMPAT_H */

