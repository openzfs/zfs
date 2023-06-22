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

/* Fill a lightweight entry from a log entry. */
#define	DDT_LOG_ENTRY_TO_LIGHTWEIGHT(ddt, ddle, ddlwe) do {	\
	memset((ddlwe), 0, sizeof (*ddlwe));			\
	(ddlwe)->ddlwe_key = (ddle)->ddle_key;			\
	(ddlwe)->ddlwe_type = (ddle)->ddle_type;		\
	(ddlwe)->ddlwe_class = (ddle)->ddle_class;		\
	(ddlwe)->ddlwe_nphys = DDT_NPHYS(ddt);			\
	for (int p = 0; p < (ddlwe)->ddlwe_nphys; p++)		\
		(ddlwe)->ddlwe_phys[p] = (ddle)->ddle_phys[p];	\
} while (0)

/*
 * An entry on the log tree. These are "frozen", and a record of what's in
 * the on-disk log. They can't be used in place, but can be "loaded" back into
 * the live tree.
 */
typedef struct {
	ddt_key_t	ddle_key;	/* ddt_log_tree key */
	avl_node_t	ddle_node;	/* ddt_log_tree node */

	ddt_type_t	ddle_type;	/* storage type */
	ddt_class_t	ddle_class;	/* storage class */

	/* extra allocation for flat/trad phys */
	ddt_phys_t	ddle_phys[];
} ddt_log_entry_t;

/* On-disk log record types. */
typedef enum {
	DLR_INVALID	= 0,	/* end of block marker */
	DLR_ENTRY	= 1,	/* an entry to add or replace in the log tree */
} ddt_log_record_type_t;

/* On-disk log record header. */
typedef struct {
	uint16_t	dlr_reclen;	/* length of record header+payload */
	uint8_t		dlr_type;	/* ddt_log_record_type_t */

	uint8_t		dlr_pad[3];	/* pad header to 64 bits */

	/* DLR_ENTRY: storage type (ddt_type) and class (ddt_class) */
	uint8_t		dlr_entry_type;
	uint8_t		dlr_entry_class;

	uint8_t		dlr_payload[];  /* dlr_length bytes of payload */
} ddt_log_record_t;

/* Payload for DLR_ENTRY. */
typedef struct {
	ddt_key_t	dlre_key;
	ddt_phys_t	dlre_phys[];
} ddt_log_record_entry_t;

/* Log flags (ddl_flags, dlh_flags) */
#define	DDL_FLAG_FLUSHING	(1 << 0)	/* this log is being flushed */
#define	DDL_FLAG_CHECKPOINT	(1 << 1)	/* header has a checkpoint */

/* On-disk log header, stored in the bonus buffer. */
typedef struct {
	uint32_t	dlh_version;	/* log version */
	uint32_t	dlh_flags;	/* log flags */
	uint64_t	dlh_nblocks;	/* number of blocks in this log */
	uint64_t	dlh_first_txg;	/* txg this log went active */
	ddt_key_t	dlh_checkpoint;	/* last checkpoint */
} ddt_log_header_t;

/* DDT log update state */
typedef struct {
	dmu_tx_t	*dlu_tx;	/* tx the update is being applied to */
	dnode_t		*dlu_dn;	/* log object dnode */
	dmu_buf_t	**dlu_dbp;	/* array of block buffer pointers */
	int		dlu_ndbp;	/* number of block buffer pointers */
	uint16_t	dlu_reclen;	/* cached length of record */
	uint64_t	dlu_block;	/* block for next entry */
	uint64_t	dlu_offset;	/* offset for next entry */
} ddt_log_update_t;

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

/* Dedup log API */
extern void ddt_log_begin(ddt_t *ddt, size_t nentries, dmu_tx_t *tx,
    ddt_log_update_t *dlu);
extern void ddt_log_entry(ddt_t *ddt, ddt_lightweight_entry_t *dde,
    ddt_log_update_t *dlu);
extern void ddt_log_commit(ddt_t *ddt, ddt_log_update_t *dlu);

extern boolean_t ddt_log_take_first(ddt_t *ddt, ddt_log_t *ddl,
    ddt_lightweight_entry_t *ddlwe);
extern boolean_t ddt_log_take_key(ddt_t *ddt, ddt_log_t *ddl,
    const ddt_key_t *ddk, ddt_lightweight_entry_t *ddlwe);

extern void ddt_log_checkpoint(ddt_t *ddt, ddt_lightweight_entry_t *ddlwe,
    dmu_tx_t *tx);
extern void ddt_log_truncate(ddt_t *ddt, dmu_tx_t *tx);

extern boolean_t ddt_log_swap(ddt_t *ddt, dmu_tx_t *tx);

extern void ddt_log_destroy(ddt_t *ddt, dmu_tx_t *tx);

extern int ddt_log_load(ddt_t *ddt);
extern void ddt_log_alloc(ddt_t *ddt);
extern void ddt_log_free(ddt_t *ddt);

extern void ddt_log_init(void);
extern void ddt_log_fini(void);

/*
 * These are only exposed so that zdb can access them. Try not to use them
 * outside of the DDT implementation proper, and if you do, consider moving
 * them up.
 */
#define	DDT_NAMELEN	118

extern uint64_t ddt_phys_total_refcnt(const ddt_t *ddt, const ddt_entry_t *dde);

extern void ddt_key_fill(ddt_key_t *ddk, const blkptr_t *bp);

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
