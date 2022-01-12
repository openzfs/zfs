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

#ifndef	_SYS_ZVOL_IMPL_H
#define	_SYS_ZVOL_IMPL_H

#include <sys/zfs_context.h>

#define	ZVOL_RDONLY	0x1
/*
 * Whether the zvol has been written to (as opposed to ZVOL_RDONLY, which
 * specifies whether or not the zvol _can_ be written to)
 */
#define	ZVOL_WRITTEN_TO	0x2

#define	ZVOL_DUMPIFIED	0x4

#define	ZVOL_EXCL	0x8

/*
 * The in-core state of each volume.
 */
typedef struct zvol_state {
	char			zv_name[MAXNAMELEN];	/* name */
	uint64_t		zv_volsize;		/* advertised space */
	uint64_t		zv_volblocksize;	/* volume block size */
	objset_t		*zv_objset;	/* objset handle */
	uint32_t		zv_flags;	/* ZVOL_* flags */
	uint32_t		zv_open_count;	/* open counts */
	uint32_t		zv_changed;	/* disk changed */
	uint32_t		zv_volmode;	/* volmode */
	zilog_t			*zv_zilog;	/* ZIL handle */
	zfs_rangelock_t		zv_rangelock;	/* for range locking */
	dnode_t			*zv_dn;		/* dnode hold */
	dataset_kstats_t	zv_kstat;	/* zvol kstats */
	list_node_t		zv_next;	/* next zvol_state_t linkage */
	uint64_t		zv_hash;	/* name hash */
	struct hlist_node	zv_hlink;	/* hash link */
	kmutex_t		zv_state_lock;	/* protects zvol_state_t */
	atomic_t		zv_suspend_ref;	/* refcount for suspend */
	krwlock_t		zv_suspend_lock;	/* suspend lock */
	struct zvol_state_os	*zv_zso;	/* private platform state */
} zvol_state_t;


extern krwlock_t zvol_state_lock;
#define	ZVOL_HT_SIZE	1024
extern struct hlist_head *zvol_htable;
#define	ZVOL_HT_HEAD(hash)	(&zvol_htable[(hash) & (ZVOL_HT_SIZE-1)])
extern zil_replay_func_t *const zvol_replay_vector[TX_MAX_TYPE];

extern unsigned int zvol_volmode;
extern unsigned int zvol_inhibit_dev;

/*
 * platform independent functions exported to platform code
 */
zvol_state_t *zvol_find_by_name_hash(const char *name,
    uint64_t hash, int mode);
int zvol_first_open(zvol_state_t *zv, boolean_t readonly);
uint64_t zvol_name_hash(const char *name);
void zvol_remove_minors_impl(const char *name);
void zvol_last_close(zvol_state_t *zv);
void zvol_insert(zvol_state_t *zv);
void zvol_log_truncate(zvol_state_t *zv, dmu_tx_t *tx, uint64_t off,
    uint64_t len, boolean_t sync);
void zvol_log_write(zvol_state_t *zv, dmu_tx_t *tx, uint64_t offset,
    uint64_t size, int sync);
int zvol_get_data(void *arg, uint64_t arg2, lr_write_t *lr, char *buf,
    struct lwb *lwb, zio_t *zio);
int zvol_init_impl(void);
void zvol_fini_impl(void);
void zvol_wait_close(zvol_state_t *zv);

/*
 * platform dependent functions exported to platform independent code
 */
void zvol_os_free(zvol_state_t *zv);
void zvol_os_rename_minor(zvol_state_t *zv, const char *newname);
int zvol_os_create_minor(const char *name);
int zvol_os_update_volsize(zvol_state_t *zv, uint64_t volsize);
boolean_t zvol_os_is_zvol(const char *path);
void zvol_os_clear_private(zvol_state_t *zv);
void zvol_os_set_disk_ro(zvol_state_t *zv, int flags);
void zvol_os_set_capacity(zvol_state_t *zv, uint64_t capacity);

#endif
