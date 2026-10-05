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
 * Copyright 2005 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef _SYS_VDEV_FILE_H
#define	_SYS_VDEV_FILE_H

#include <sys/vdev.h>

#ifdef	__cplusplus
extern "C" {
#endif

typedef struct vdev_file {
	zfs_file_t	*vf_file;
} vdev_file_t;

extern void vdev_file_init(void);
extern void vdev_file_fini(void);

#ifdef __linux__
extern mode_t vdev_file_open_mode(spa_mode_t spa_mode);
#endif

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_VDEV_FILE_H */
