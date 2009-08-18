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
 * Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef	_SYS_DMU_OBJSET_H
#define	_SYS_DMU_OBJSET_H

#include <sys/spa.h>
#include <sys/arc.h>
#include <sys/txg.h>
#include <sys/zfs_context.h>
#include <sys/dnode.h>
#include <sys/zio.h>
#include <sys/zil.h>

#ifdef	__cplusplus
extern "C" {
#endif

struct dsl_dataset;
struct dmu_tx;
struct objset_impl;

#define	OBJSET_PHYS_SIZE 2048
#define	OBJSET_OLD_PHYS_SIZE 1024

#define	OBJSET_FLAG_USERACCOUNTING_COMPLETE	(1ULL<<0)

typedef struct objset_phys {
	dnode_phys_t os_meta_dnode;
	zil_header_t os_zil_header;
	uint64_t os_type;
	uint64_t os_flags;
	char os_pad[OBJSET_PHYS_SIZE - sizeof (dnode_phys_t)*3 -
	    sizeof (zil_header_t) - sizeof (uint64_t)*2];
	dnode_phys_t os_userused_dnode;
	dnode_phys_t os_groupused_dnode;
} objset_phys_t;

struct objset {
	struct objset_impl *os;
	int os_mode;
};

typedef struct objset_impl {
	/* Immutable: */
	struct dsl_dataset *os_dsl_dataset;
	spa_t *os_spa;
	arc_buf_t *os_phys_buf;
	objset_phys_t *os_phys;
	dnode_t *os_meta_dnode;
	dnode_t *os_userused_dnode;
	dnode_t *os_groupused_dnode;
	zilog_t *os_zil;
	objset_t os;
	uint8_t os_checksum;	/* can change, under dsl_dir's locks */
	uint8_t os_compress;	/* can change, under dsl_dir's locks */
	uint8_t os_copies;	/* can change, under dsl_dir's locks */
	uint8_t os_primary_cache;	/* can change, under dsl_dir's locks */
	uint8_t os_secondary_cache;	/* can change, under dsl_dir's locks */

	/* no lock needed: */
	struct dmu_tx *os_synctx; /* XXX sketchy */
	blkptr_t *os_rootbp;
	zil_header_t os_zil_header;
	list_t os_synced_dnodes;
	uint64_t os_flags;

	/* Protected by os_obj_lock */
	kmutex_t os_obj_lock;
	uint64_t os_obj_next;

	/* Protected by os_lock */
	kmutex_t os_lock;
	list_t os_dirty_dnodes[TXG_SIZE];
	list_t os_free_dnodes[TXG_SIZE];
	list_t os_dnodes;
	list_t os_downgraded_dbufs;

	/* stuff we store for the user */
	kmutex_t os_user_ptr_lock;
	void *os_user_ptr;
} objset_impl_t;

#define	DMU_META_DNODE_OBJECT	0
#define	DMU_OBJECT_IS_SPECIAL(obj) ((int64_t)(obj) <= 0)

#define	DMU_OS_IS_L2CACHEABLE(os)				\
	((os)->os_secondary_cache == ZFS_CACHE_ALL ||		\
	(os)->os_secondary_cache == ZFS_CACHE_METADATA)

/* called from zpl */
int dmu_objset_open(const char *name, dmu_objset_type_t type, int mode,
    objset_t **osp);
void dmu_objset_close(objset_t *os);
int dmu_objset_create(const char *name, dmu_objset_type_t type,
    objset_t *clone_parent, uint64_t flags,
    void (*func)(objset_t *os, void *arg, cred_t *cr, dmu_tx_t *tx), void *arg);
int dmu_objset_destroy(const char *name, boolean_t defer);
int dmu_objset_rollback(objset_t *os);
int dmu_objset_snapshot(char *fsname, char *snapname, nvlist_t *props,
    boolean_t recursive);
void dmu_objset_stats(objset_t *os, nvlist_t *nv);
void dmu_objset_fast_stat(objset_t *os, dmu_objset_stats_t *stat);
void dmu_objset_space(objset_t *os, uint64_t *refdbytesp, uint64_t *availbytesp,
    uint64_t *usedobjsp, uint64_t *availobjsp);
uint64_t dmu_objset_fsid_guid(objset_t *os);
int dmu_objset_find(char *name, int func(char *, void *), void *arg,
    int flags);
int dmu_objset_find_spa(spa_t *spa, const char *name,
    int func(spa_t *, uint64_t, const char *, void *), void *arg, int flags);
int dmu_objset_prefetch(char *name, void *arg);
void dmu_objset_byteswap(void *buf, size_t size);
int dmu_objset_evict_dbufs(objset_t *os);

/* called from dsl */
void dmu_objset_sync(objset_impl_t *os, zio_t *zio, dmu_tx_t *tx);
objset_impl_t *dmu_objset_create_impl(spa_t *spa, struct dsl_dataset *ds,
    blkptr_t *bp, dmu_objset_type_t type, dmu_tx_t *tx);
int dmu_objset_open_impl(spa_t *spa, struct dsl_dataset *ds, blkptr_t *bp,
    objset_impl_t **osip);
void dmu_objset_evict(struct dsl_dataset *ds, void *arg);
void dmu_objset_do_userquota_callbacks(objset_impl_t *os, dmu_tx_t *tx);
boolean_t dmu_objset_userused_enabled(objset_impl_t *os);
int dmu_objset_userspace_upgrade(objset_t *os);
boolean_t dmu_objset_userspace_present(objset_t *os);

#ifdef	__cplusplus
}
#endif

#endif /* _SYS_DMU_OBJSET_H */
