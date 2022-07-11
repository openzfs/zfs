/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or https://opensource.org/licenses/CDDL-1.0.
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
#endif /* _KERNEL */
#endif /* _SYS_VDEV_DISK_H */
