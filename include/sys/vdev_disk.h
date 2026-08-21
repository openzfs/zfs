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
 * Copyright (C) 2008-2010 Lawrence Livermore National Security, LLC.
 * Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 * Written by Brian Behlendorf <behlendorf1@llnl.gov>.
 * LLNL-CODE-403049.
 * Copyright (c) 2018 by Delphix. All rights reserved.
 */

#ifndef _SYS_VDEV_DISK_H
#define	_SYS_VDEV_DISK_H

/*
 * Don't start the slice at the default block of 34; many storage
 * devices will use a stripe width of 128k, other vendors prefer a 1m
 * alignment.  It is best to play it safe and ensure a 1m alignment
 * given 512B blocks.  When the block size is larger by a power of 2
 * we will still be 1m aligned.  Some devices are sensitive to the
 * partition ending alignment as well.
 */
#define	NEW_START_BLOCK		2048
#define	PARTITION_END_ALIGNMENT	2048

#ifdef _KERNEL
#include <sys/vdev.h>

#ifdef __linux__
struct block_device *get_bdev(void *tsd);
int __vdev_disk_io_rw(vdev_t *v,
    struct block_device *bdev, zio_t *zio,
    size_t io_size, uint64_t io_offset);
int vdev_disk_io_flush(struct block_device *bdev, zio_t *zio);
void vdev_disk_error(zio_t *zio);
#endif /* __linux__ */

#endif /* _KERNEL */
#endif /* _SYS_VDEV_DISK_H */
