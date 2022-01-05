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
 * Copyright (c) 2020 by Lawrence Livermore National Security, LLC.
 */

#ifndef _SYS_VDEV_DRAID_H
#define	_SYS_VDEV_DRAID_H

#include <sys/types.h>
#include <sys/abd.h>
#include <sys/nvpair.h>
#include <sys/zio.h>
#include <sys/vdev_impl.h>
#include <sys/vdev_raidz_impl.h>
#include <sys/vdev.h>

#ifdef  __cplusplus
extern "C" {
#endif

/*
 * Constants required to generate and use dRAID permutations.
 */
#define	VDEV_DRAID_SEED			0xd7a1d5eed
#define	VDEV_DRAID_MAX_MAPS		254
#define	VDEV_DRAID_ROWSHIFT		SPA_MAXBLOCKSHIFT
#define	VDEV_DRAID_ROWHEIGHT		(1ULL << VDEV_DRAID_ROWSHIFT)
#define	VDEV_DRAID_REFLOW_RESERVE	(2 * VDEV_DRAID_ROWHEIGHT)

/*
 * dRAID permutation map.
 */
typedef struct draid_map {
	uint64_t dm_children;	/* # of permutation columns */
	uint64_t dm_nperms;	/* # of permutation rows */
	uint64_t dm_seed;	/* dRAID map seed */
	uint64_t dm_checksum;	/* Checksum of generated map */
	uint8_t *dm_perms;	/* base permutation array */
} draid_map_t;

/*
 * dRAID configuration.
 */
typedef struct vdev_draid_config {
	/*
	 * Values read from the dRAID nvlist configuration.
	 */
	uint64_t vdc_ndata;		/* # of data devices in group */
	uint64_t vdc_nparity;		/* # of parity devices in group */
	uint64_t vdc_nspares;		/* # of distributed spares */
	uint64_t vdc_children;		/* # of children */
	uint64_t vdc_ngroups;		/* # groups per slice */

	/*
	 * Immutable derived constants.
	 */
	uint8_t *vdc_perms;		/* permutation array */
	uint64_t vdc_nperms;		/* # of permutations */
	uint64_t vdc_groupwidth;	/* = data + parity */
	uint64_t vdc_ndisks;		/* = children - spares */
	uint64_t vdc_groupsz;		/* = groupwidth * DRAID_ROWSIZE */
	uint64_t vdc_devslicesz;	/* = (groupsz * groups) / ndisks */
} vdev_draid_config_t;

/*
 * Functions for handling dRAID permutation maps.
 */
extern uint64_t vdev_draid_rand(uint64_t *);
extern int vdev_draid_lookup_map(uint64_t, const draid_map_t **);
extern int vdev_draid_generate_perms(const draid_map_t *, uint8_t **);

/*
 * General dRAID support functions.
 */
extern boolean_t vdev_draid_readable(vdev_t *, uint64_t);
extern boolean_t vdev_draid_missing(vdev_t *, uint64_t, uint64_t, uint64_t);
extern uint64_t vdev_draid_asize_to_psize(vdev_t *, uint64_t);
extern void vdev_draid_map_alloc_empty(zio_t *, struct raidz_row *);
extern int vdev_draid_map_verify_empty(zio_t *, struct raidz_row *);
extern nvlist_t *vdev_draid_read_config_spare(vdev_t *);

/* Functions for dRAID distributed spares. */
extern vdev_t *vdev_draid_spare_get_child(vdev_t *, uint64_t);
extern vdev_t *vdev_draid_spare_get_parent(vdev_t *);
extern int vdev_draid_spare_create(nvlist_t *, vdev_t *, uint64_t *, uint64_t);

#ifdef  __cplusplus
}
#endif

#endif /* _SYS_VDEV_DRAID_H */
