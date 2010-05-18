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
 * Copyright (C) 2008-2010 Lawrence Livermore National Security, LLC.
 * Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 * Written by Brian Behlendorf <behlendorf1@llnl.gov>.
 * LLNL-CODE-403049.
 */

#ifndef _SYS_VDEV_DISK_H
#define _SYS_VDEV_DISK_H

#ifdef	__cplusplus
extern "C" {
#endif

#ifdef _KERNEL
#include <sys/vdev.h>
#include <sys/ddi.h>
#include <sys/sunldi.h>
#include <sys/sunddi.h>

typedef struct vdev_disk {
	ddi_devid_t		vd_devid;
	char			*vd_minor;
	struct block_device	*vd_bdev;
} vdev_disk_t;

extern int vdev_disk_physio(struct block_device *, caddr_t,
			    size_t, uint64_t, int);
extern int vdev_disk_read_rootlabel(char *, char *, nvlist_t **);

/* 2.6.24 API change */
#ifdef HAVE_2ARGS_BIO_END_IO_T
# define BIO_END_IO_PROTO(fn, x, y, z)	static void fn(struct bio *x, int z)
# define BIO_END_IO_RETURN(rc)		return
#else
# define BIO_END_IO_PROTO(fn, x, y, z)	static int fn(struct bio *x, \
					              unsigned int y, int z)
# define BIO_END_IO_RETURN(rc)		return rc
#endif /* HAVE_2ARGS_BIO_END_IO_T */

/* 2.6.29 API change */
#ifdef HAVE_BIO_RW_SYNCIO
# define DIO_RW_SYNCIO			BIO_RW_SYNCIO
#else
# define DIO_RW_SYNCIO			BIO_RW_SYNC
#endif /* HAVE_BIO_RW_SYNCIO */

/* 2.6.28 API change */
#ifdef HAVE_OPEN_BDEV_EXCLUSIVE
# define vdev_bdev_open(path, md, hld)	open_bdev_exclusive(path, md, hld)
# define vdev_bdev_close(bdev, md)	close_bdev_exclusive(bdev, md)
#else
# define vdev_bdev_open(path, md, hld)	open_bdev_excl(path, md, hld)
# define vdev_bdev_close(bdev, md)	close_bdev_excl(bdev)
#endif /* HAVE_OPEN_BDEV_EXCLUSIVE */

/* 2.6.22 API change */
#ifdef HAVE_1ARG_INVALIDATE_BDEV
# define vdev_bdev_invalidate(bdev)	invalidate_bdev(bdev)
#else
# define vdev_bdev_invalidate(bdev)	invalidate_bdev(bdev, 1)
#endif /* HAVE_1ARG_INVALIDATE_BDEV */

/* 2.6.30 API change */
#ifdef HAVE_BDEV_LOGICAL_BLOCK_SIZE
# define vdev_bdev_block_size(bdev)	bdev_logical_block_size(bdev)
#else
# define vdev_bdev_block_size(bdev)	bdev_hardsect_size(bdev)
#endif

#endif /* _KERNEL */

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_VDEV_DISK_H */
