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
 * Copyright (c) 2009, 2010, Oracle and/or its affiliates. All rights reserved.
 * Copyright (c) 2016 by Delphix. All rights reserved.
 * Copyright (c) 2023, Klara Inc.
 */

#ifndef _SYS_DDT_IMPL_H
#define	_SYS_DDT_IMPL_H

#include <sys/ddt.h>

#ifdef	__cplusplus
extern "C" {
#endif

/* DDT version numbers */
#define	DDT_VERSION_LEGACY	(0)
#define	DDT_VERSION_FDT		(1)

/* Names of interesting objects in the DDT root dir */
#define	DDT_DIR_VERSION		"version"
#define	DDT_DIR_FLAGS		"flags"

/* Fill a lightweight entry from a live entry. */
#define	DDT_ENTRY_TO_LIGHTWEIGHT(ddt, dde, ddlwe) do {		\
	memset((ddlwe), 0, sizeof (*ddlwe));			\
	(ddlwe)->ddlwe_key = (dde)->dde_key;			\
	(ddlwe)->ddlwe_type = (dde)->dde_type;			\
	(ddlwe)->ddlwe_class = (dde)->dde_class;		\
	(ddlwe)->ddlwe_nphys = DDT_NPHYS(ddt);			\
	for (int p = 0; p < (ddlwe)->ddlwe_nphys; p++)		\
		(ddlwe)->ddlwe_phys[p] = (dde)->dde_phys[p];	\
} while (0)

/*
 * Ops vector to access a specific DDT object type.
 */
typedef struct {
	char ddt_op_name[32];
	int (*ddt_op_create)(objset_t *os, uint64_t *object, dmu_tx_t *tx,
	    boolean_t prehash);
	int (*ddt_op_destroy)(objset_t *os, uint64_t object, dmu_tx_t *tx);
	int (*ddt_op_lookup)(objset_t *os, uint64_t object,
	    const ddt_key_t *ddk, ddt_phys_t *phys, size_t psize);
	int (*ddt_op_contains)(objset_t *os, uint64_t object,
	    const ddt_key_t *ddk);
	void (*ddt_op_prefetch)(objset_t *os, uint64_t object,
	    const ddt_key_t *ddk);
	void (*ddt_op_prefetch_all)(objset_t *os, uint64_t object);
	int (*ddt_op_update)(objset_t *os, uint64_t object,
	    const ddt_key_t *ddk, const ddt_phys_t *phys, size_t psize,
	    dmu_tx_t *tx);
	int (*ddt_op_remove)(objset_t *os, uint64_t object,
	    const ddt_key_t *ddk, dmu_tx_t *tx);
	int (*ddt_op_walk)(objset_t *os, uint64_t object, uint64_t *walk,
	    ddt_key_t *ddk, ddt_phys_t *phys, size_t psize);
	int (*ddt_op_count)(objset_t *os, uint64_t object, uint64_t *count);
} ddt_ops_t;

extern const ddt_ops_t ddt_zap_ops;

extern void ddt_stat_update(ddt_t *ddt, ddt_entry_t *dde, uint64_t neg);

/*
 * These are only exposed so that zdb can access them. Try not to use them
 * outside of the DDT implementation proper, and if you do, consider moving
 * them up.
 */

/*
 * Enough room to expand DMU_POOL_DDT format for all possible DDT
 * checksum/class/type combinations.
 */
#define	DDT_NAMELEN	32

extern uint64_t ddt_phys_total_refcnt(const ddt_t *ddt, const ddt_entry_t *dde);

extern void ddt_key_fill(ddt_key_t *ddk, const blkptr_t *bp);

extern void ddt_stat_add(ddt_stat_t *dst, const ddt_stat_t *src, uint64_t neg);

extern void ddt_object_name(ddt_t *ddt, ddt_type_t type, ddt_class_t clazz,
    char *name);
extern int ddt_object_walk(ddt_t *ddt, ddt_type_t type, ddt_class_t clazz,
    uint64_t *walk, ddt_lightweight_entry_t *ddlwe);
extern int ddt_object_count(ddt_t *ddt, ddt_type_t type, ddt_class_t clazz,
    uint64_t *count);
extern int ddt_object_info(ddt_t *ddt, ddt_type_t type, ddt_class_t clazz,
    dmu_object_info_t *);

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_DDT_H */
