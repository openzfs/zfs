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
	uint64_t dcf_groups;
	const uint64_t *dcf_data;
	uint64_t dcf_parity;
	uint64_t dcf_spare;
	uint64_t dcf_children;
	uint64_t dcf_bases;
	struct abd *dcf_zero_abd;	/* zfs module and libzpool only */
	const uint64_t *dcf_base_perms;
};

struct vdev;
typedef struct vdev vdev_t;

/*
 * Errors which may be returned when validating a dRAID configuration.
 *
 * - MISSING indicates the key/value pair does not exist,
 * - INVALID indicates the value falls outside the allow range,
 * - MISMATCH indicates the value is in some way inconsistent with other
 *   configuration values, or if provided the top-level dRAID vdev.
 */
typedef enum {
	DRAIDCFG_OK = 0,		/* valid configuration */
	DRAIDCFG_ERR_CHILDREN_MISSING,	/* children key/value is missing */
	DRAIDCFG_ERR_CHILDREN_INVALID,	/* children value is invalid */
	DRAIDCFG_ERR_CHILDREN_MISMATCH,	/* children value is inconsistent */
	DRAIDCFG_ERR_PARITY_MISSING,	/* parity key/value is missing */
	DRAIDCFG_ERR_PARITY_INVALID,	/* parity value is invalid */
	DRAIDCFG_ERR_PARITY_MISMATCH,	/* parity value is inconsistent */
	DRAIDCFG_ERR_GROUPS_MISSING,	/* groups key/value is missing */
	DRAIDCFG_ERR_GROUPS_INVALID,	/* groups value is invalid */
	DRAIDCFG_ERR_SPARES_MISSING,	/* spares key/value is missing */
	DRAIDCFG_ERR_SPARES_INVALID,	/* spares value is invalid */
	DRAIDCFG_ERR_DATA_MISSING,	/* data key/value is missing */
	DRAIDCFG_ERR_DATA_INVALID,	/* data value is invalid */
	DRAIDCFG_ERR_DATA_MISMATCH,	/* data value is inconsistent */
	DRAIDCFG_ERR_BASE_MISSING,	/* base key/value is missing */
	DRAIDCFG_ERR_BASE_INVALID,	/* base value is invalid */
	DRAIDCFG_ERR_PERM_MISSING,	/* perm key/value is missing */
	DRAIDCFG_ERR_PERM_INVALID,	/* perm value is invalid */
	DRAIDCFG_ERR_PERM_MISMATCH,	/* perm value is inconsistent */
	DRAIDCFG_ERR_PERM_DUPLICATE,	/* perm value is a duplicate */
	DRAIDCFG_ERR_LAYOUT,		/* layout (n - s) != (d + p) */
} draidcfg_err_t;

extern draidcfg_err_t vdev_draid_config_validate(const vdev_t *, nvlist_t *);

#ifndef _KERNEL
extern boolean_t vdev_draid_config_add(nvlist_t *, nvlist_t *);
extern nvlist_t *draidcfg_read_file(const char *);
#endif

#ifdef  __cplusplus
}
#endif

#endif /* _DRAID_CONFIG_H */
