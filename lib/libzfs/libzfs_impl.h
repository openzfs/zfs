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
 * Copyright (c) 2005, 2010, Oracle and/or its affiliates. All rights reserved.
 * Copyright (c) 2011, 2020 by Delphix. All rights reserved.
 * Copyright (c) 2018 Datto Inc.
 * Copyright 2020 Joyent, Inc.
 */

#ifndef	_LIBZFS_IMPL_H
#define	_LIBZFS_IMPL_H

#include <sys/fs/zfs.h>
#include <sys/nvpair.h>
#include <sys/dmu.h>
#include <sys/zfs_ioctl.h>
#include <regex.h>

#include <libuutil.h>
#include <libzfs.h>
#include <libshare.h>
#include <libzfs_core.h>

#ifdef	__cplusplus
extern "C" {
#endif

#define	ERRBUFLEN 1024

struct libzfs_handle {
	int libzfs_error;
	int libzfs_fd;
	zpool_handle_t *libzfs_pool_handles;
	uu_avl_pool_t *libzfs_ns_avlpool;
	uu_avl_t *libzfs_ns_avl;
	uint64_t libzfs_ns_gen;
	int libzfs_desc_active;
	char libzfs_action[1024];
	char libzfs_desc[1024];
	int libzfs_printerr;
	boolean_t libzfs_mnttab_enable;
	/*
	 * We need a lock to handle the case where parallel mount
	 * threads are populating the mnttab cache simultaneously. The
	 * lock only protects the integrity of the avl tree, and does
	 * not protect the contents of the mnttab entries themselves.
	 */
	pthread_mutex_t libzfs_mnttab_cache_lock;
	avl_tree_t libzfs_mnttab_cache;
	int libzfs_pool_iter;
	boolean_t libzfs_prop_debug;
	regex_t libzfs_urire;
	uint64_t libzfs_max_nvlist;
	void *libfetch;
	char *libfetch_load_error;
};

struct zfs_handle {
	libzfs_handle_t *zfs_hdl;
	zpool_handle_t *zpool_hdl;
	char zfs_name[ZFS_MAX_DATASET_NAME_LEN];
	zfs_type_t zfs_type; /* type including snapshot */
	zfs_type_t zfs_head_type; /* type excluding snapshot */
	dmu_objset_stats_t zfs_dmustats;
	nvlist_t *zfs_props;
	nvlist_t *zfs_user_props;
	nvlist_t *zfs_recvd_props;
	boolean_t zfs_mntcheck;
	char *zfs_mntopts;
	uint8_t *zfs_props_table;
};

/*
 * This is different from checking zfs_type, because it will also catch
 * snapshots of volumes.
 */
#define	ZFS_IS_VOLUME(zhp) ((zhp)->zfs_head_type == ZFS_TYPE_VOLUME)
#define	ZHP_MAX_PROPNAMES 4

struct zpool_handle {
	libzfs_handle_t *zpool_hdl;
	zpool_handle_t *zpool_next;
	char zpool_name[ZFS_MAX_DATASET_NAME_LEN];
	int zpool_state;
	unsigned int zpool_n_propnames;
	const char *zpool_propnames[ZHP_MAX_PROPNAMES];
	size_t zpool_config_size;
	nvlist_t *zpool_config;
	nvlist_t *zpool_old_config;
	nvlist_t *zpool_props;
	diskaddr_t zpool_start_block;
};

typedef int (*zfs_uri_handler_fn_t)(struct libzfs_handle *, const char *,
    const char *, zfs_keyformat_t, boolean_t, uint8_t **, size_t *);

typedef struct zfs_uri_handler {
	const char *zuh_scheme;
	zfs_uri_handler_fn_t zuh_handler;
} zfs_uri_handler_t;

#define	CONFIG_BUF_MINSIZE	262144

extern int zfs_error(libzfs_handle_t *, int, const char *);
extern int zfs_error_fmt(libzfs_handle_t *, int, const char *, ...)
    __attribute__((format(printf, 3, 4)));
extern void zfs_error_aux(libzfs_handle_t *, const char *, ...)
    __attribute__((format(printf, 2, 3)));
extern void *zfs_alloc(libzfs_handle_t *, size_t);
extern void *zfs_realloc(libzfs_handle_t *, void *, size_t, size_t);
extern char *zfs_asprintf(libzfs_handle_t *, const char *, ...)
    __attribute__((format(printf, 2, 3)));
extern char *zfs_strdup(libzfs_handle_t *, const char *);
extern int no_memory(libzfs_handle_t *);

extern int zfs_standard_error_fmt(libzfs_handle_t *, int, const char *, ...)
    __attribute__((format(printf, 3, 4)));
extern void zfs_setprop_error(libzfs_handle_t *, zfs_prop_t, int, char *);
extern int zpool_standard_error(libzfs_handle_t *, int, const char *);
extern int zpool_standard_error_fmt(libzfs_handle_t *, int, const char *, ...)
    __attribute__((format(printf, 3, 4)));

extern zfs_handle_t *make_dataset_handle_zc(libzfs_handle_t *, zfs_cmd_t *);
extern zfs_handle_t *make_dataset_simple_handle_zc(zfs_handle_t *, zfs_cmd_t *);

extern int zprop_parse_value(libzfs_handle_t *, nvpair_t *, int, zfs_type_t,
    nvlist_t *, const char **, uint64_t *, const char *);
extern int zprop_expand_list(libzfs_handle_t *hdl, zprop_list_t **plp,
    zfs_type_t type);

/*
 * Use this changelist_gather() flag to force attempting mounts
 * on each change node regardless of whether or not it is currently
 * mounted.
 */
#define	CL_GATHER_MOUNT_ALWAYS	1
/*
 * changelist_gather() flag to force it to iterate on mounted datasets only
 */
#define	CL_GATHER_ITER_MOUNTED	2
/*
 * Use this changelist_gather() flag to prevent unmounting of file systems.
 */
#define	CL_GATHER_DONT_UNMOUNT	4

typedef struct prop_changelist prop_changelist_t;

extern void zcmd_alloc_dst_nvlist(libzfs_handle_t *, zfs_cmd_t *, size_t);
extern void zcmd_write_src_nvlist(libzfs_handle_t *, zfs_cmd_t *, nvlist_t *);
extern void zcmd_write_conf_nvlist(libzfs_handle_t *, zfs_cmd_t *, nvlist_t *);
extern void zcmd_expand_dst_nvlist(libzfs_handle_t *, zfs_cmd_t *);
extern int zcmd_read_dst_nvlist(libzfs_handle_t *, zfs_cmd_t *, nvlist_t **);
extern void zcmd_free_nvlists(zfs_cmd_t *);

extern int changelist_prefix(prop_changelist_t *);
extern int changelist_postfix(prop_changelist_t *);
extern void changelist_rename(prop_changelist_t *, const char *, const char *);
extern void changelist_remove(prop_changelist_t *, const char *);
extern void changelist_free(prop_changelist_t *);
extern prop_changelist_t *changelist_gather(zfs_handle_t *, zfs_prop_t, int,
    int);
extern int changelist_unshare(prop_changelist_t *, const enum sa_protocol *);
extern int changelist_haszonedchild(prop_changelist_t *);

extern void remove_mountpoint(zfs_handle_t *);
extern int create_parents(libzfs_handle_t *, char *, int);

extern zfs_handle_t *make_dataset_handle(libzfs_handle_t *, const char *);
extern zfs_handle_t *make_bookmark_handle(zfs_handle_t *, const char *,
    nvlist_t *props);

extern int zpool_open_silent(libzfs_handle_t *, const char *,
    zpool_handle_t **);

extern boolean_t zpool_name_valid(libzfs_handle_t *, boolean_t, const char *);

extern int zfs_validate_name(libzfs_handle_t *hdl, const char *path, int type,
    boolean_t modifying);

extern void namespace_clear(libzfs_handle_t *);

typedef struct {
	zfs_prop_t p_prop;
	int p_share_err;
	int p_unshare_err;
} proto_table_t;

typedef struct differ_info {
	zfs_handle_t *zhp;
	char *fromsnap;
	char *frommnt;
	char *tosnap;
	char *tomnt;
	char *ds;
	char *dsmnt;
	char *tmpsnap;
	char errbuf[ERRBUFLEN];
	boolean_t isclone;
	boolean_t scripted;
	boolean_t classify;
	boolean_t timestamped;
	boolean_t no_mangle;
	uint64_t shares;
	int zerr;
	int cleanupfd;
	int outputfd;
	int datafd;
} differ_info_t;

extern int do_mount(zfs_handle_t *zhp, const char *mntpt, const char *opts,
    int flags);
extern int do_unmount(zfs_handle_t *zhp, const char *mntpt, int flags);
extern int libzfs_load_module(void);
extern int zpool_relabel_disk(libzfs_handle_t *hdl, const char *path,
    const char *msg);
extern int find_shares_object(differ_info_t *di);

#ifdef	__cplusplus
}
#endif

#endif	/* _LIBZFS_IMPL_H */
