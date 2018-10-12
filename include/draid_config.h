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
 * Copyright (c) 2016, Intel Corporation.
 */

#ifndef _DRAID_CONFIG_H
#define	_DRAID_CONFIG_H

#include <sys/types.h>
#include <sys/nvpair.h>

#ifdef  __cplusplus
extern "C" {
#endif

#define	VDEV_DRAID_MAX_CHILDREN	255
#define	VDEV_DRAID_U8_MAX	((uint8_t)-1)

#define	VDEV_RAIDZ_MAXPARITY	3

/*
 * Double '%' characters in the front because it's used as format string in
 * scanf()/printf() family of functions
 */
#define	VDEV_DRAID_SPARE_PATH_FMT "%%"VDEV_TYPE_DRAID"%lu-%lu-s%lu"

struct abd;

struct vdev_draid_configuration {
	uint64_t dcf_data;
	uint64_t dcf_parity;
	uint64_t dcf_spare;
	uint64_t dcf_children;
	uint64_t dcf_bases;
	struct abd *dcf_zero_abd;	/* zfs module and libzpool only */
	const uint64_t *dcf_base_perms;
};

struct vdev;
typedef struct vdev vdev_t;

extern boolean_t vdev_draid_config_validate(const vdev_t *, nvlist_t *);

#ifndef _KERNEL
extern boolean_t vdev_draid_config_add(nvlist_t *, nvlist_t *);
extern nvlist_t *draidcfg_read_file(const char *);
#endif

#ifdef  __cplusplus
}
#endif

#endif /* _DRAID_CONFIG_H */
