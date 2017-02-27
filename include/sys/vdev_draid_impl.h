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

#ifndef _VDEV_DRAID_IMPL_H
#define	_VDEV_DRAID_IMPL_H

#include <sys/types.h>
#include <sys/abd.h>
#include <sys/nvpair.h>

#ifdef  __cplusplus
extern "C" {
#endif

typedef struct zio zio_t;
typedef struct vdev vdev_t;
typedef struct raidz_map raidz_map_t;

struct vdev_draid_configuration {
	uint64_t dcf_data;
	uint64_t dcf_parity;
	uint64_t dcf_spare;
	uint64_t dcf_children;
	uint64_t dcf_bases;
	abd_t *dcf_zero_abd;
	const uint64_t *dcf_base_perms;
};

extern boolean_t vdev_draid_ms_mirrored(const vdev_t *, uint64_t);
extern boolean_t vdev_draid_group_degraded(vdev_t *, vdev_t *,
    uint64_t, uint64_t, boolean_t);
extern uint64_t vdev_draid_check_block(const vdev_t *vd, uint64_t, uint64_t);
extern uint64_t vdev_draid_get_astart(const vdev_t *, const uint64_t);
extern uint64_t vdev_draid_offset2group(const vdev_t *, uint64_t, boolean_t);
extern uint64_t vdev_draid_group2offset(const vdev_t *, uint64_t, boolean_t);
extern boolean_t vdev_draid_is_remainder_group(const vdev_t *,
    uint64_t, boolean_t);
extern uint64_t vdev_draid_get_groupsz(const vdev_t *, boolean_t);
extern boolean_t vdev_draid_config_validate(const vdev_t *, nvlist_t *);
extern boolean_t vdev_draid_config_add(nvlist_t *, nvlist_t *);
extern void vdev_draid_fix_skip_sectors(zio_t *);
extern int vdev_draid_hide_skip_sectors(raidz_map_t *);
extern void vdev_draid_restore_skip_sectors(raidz_map_t *, int);
extern boolean_t vdev_draid_readable(vdev_t *, uint64_t);
extern boolean_t vdev_draid_is_dead(vdev_t *, uint64_t);
extern boolean_t vdev_draid_missing(vdev_t *, uint64_t, uint64_t, uint64_t);
extern vdev_t *vdev_draid_spare_get_parent(vdev_t *);
extern nvlist_t *vdev_draid_spare_read_config(vdev_t *);

#define	VDEV_DRAID_MAX_CHILDREN	255
#define	VDEV_DRAID_U8_MAX	((uint8_t)-1)

#define	VDEV_DRAID_SPARE_PATH_FMT "$"VDEV_TYPE_DRAID"%lu-%lu-s%lu"

/* trace_printk is GPL only */
#undef	DRAID_USE_TRACE_PRINTK

#ifdef _KERNEL
#define	U64FMT "%llu"
#ifdef DRAID_USE_TRACE_PRINTK
#define	draid_print(fmt, ...) trace_printk(fmt, ##__VA_ARGS__)
#else
#define	draid_print(fmt, ...) printk(fmt, ##__VA_ARGS__)
#endif
#else
#define	U64FMT "%lu"
#define	draid_print(fmt, ...) printf(fmt, ##__VA_ARGS__)
#endif

extern int draid_debug_lvl;
extern void vdev_draid_debug_zio(zio_t *, boolean_t);

#define	draid_dbg(lvl, fmt, ...) \
	do { \
		if (draid_debug_lvl >= (lvl)) \
			draid_print(fmt, ##__VA_ARGS__); \
	} while (0);


#ifdef  __cplusplus
}
#endif

#endif /* _VDEV_DRAID_IMPL_H */
