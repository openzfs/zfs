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
 * Copyright (c) 2005, 2010, Oracle and/or its affiliates. All rights reserved.
 * Copyright (c) 2011, 2020 by Delphix. All rights reserved.
 * Copyright Joyent, Inc.
 * Copyright (c) 2013 Steven Hartland. All rights reserved.
 * Copyright (c) 2016, Intel Corporation.
 * Copyright 2016 Nexenta Systems, Inc.
 * Copyright (c) 2017 Open-E, Inc. All Rights Reserved.
 * Copyright (c) 2019 Datto Inc.
 * Copyright (c) 2021, Colm Buckley <colm@tuatha.org>
 */

#ifndef	_LIBZFS_H
#define	_LIBZFS_H extern __attribute__((visibility("default")))

#include <assert.h>
#include <libnvpair.h>
#include <sys/mnttab.h>
#include <sys/param.h>
#include <sys/types.h>
#include <sys/varargs.h>
#include <sys/fs/zfs.h>
#include <sys/avl.h>
#include <ucred.h>
#include <libzfs_core.h>

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * Miscellaneous ZFS constants
 */
#define	ZFS_MAXPROPLEN		MAXPATHLEN
#define	ZPOOL_MAXPROPLEN	MAXPATHLEN

/*
 * libzfs errors
 */
typedef enum zfs_error {
	EZFS_SUCCESS = 0,	/* no error -- success */
	EZFS_NOMEM = 2000,	/* out of memory */
	EZFS_BADPROP,		/* invalid property value */
	EZFS_PROPREADONLY,	/* cannot set readonly property */
	EZFS_PROPTYPE,		/* property does not apply to dataset type */
	EZFS_PROPNONINHERIT,	/* property is not inheritable */
	EZFS_PROPSPACE,		/* bad quota or reservation */
	EZFS_BADTYPE,		/* dataset is not of appropriate type */
	EZFS_BUSY,		/* pool or dataset is busy */
	EZFS_EXISTS,		/* pool or dataset already exists */
	EZFS_NOENT,		/* no such pool or dataset */
	EZFS_BADSTREAM,		/* bad backup stream */
	EZFS_DSREADONLY,	/* dataset is readonly */
	EZFS_VOLTOOBIG,		/* volume is too large for 32-bit system */
	EZFS_INVALIDNAME,	/* invalid dataset name */
	EZFS_BADRESTORE,	/* unable to restore to destination */
	EZFS_BADBACKUP,		/* backup failed */
	EZFS_BADTARGET,		/* bad attach/detach/replace target */
	EZFS_NODEVICE,		/* no such device in pool */
	EZFS_BADDEV,		/* invalid device to add */
	EZFS_NOREPLICAS,	/* no valid replicas */
	EZFS_RESILVERING,	/* resilvering (healing reconstruction) */
	EZFS_BADVERSION,	/* unsupported version */
	EZFS_POOLUNAVAIL,	/* pool is currently unavailable */
	EZFS_DEVOVERFLOW,	/* too many devices in one vdev */
	EZFS_BADPATH,		/* must be an absolute path */
	EZFS_CROSSTARGET,	/* rename or clone across pool or dataset */
	EZFS_ZONED,		/* used improperly in local zone */
	EZFS_MOUNTFAILED,	/* failed to mount dataset */
	EZFS_UMOUNTFAILED,	/* failed to unmount dataset */
	EZFS_UNSHARENFSFAILED,	/* failed to unshare over nfs */
	EZFS_SHARENFSFAILED,	/* failed to share over nfs */
	EZFS_PERM,		/* permission denied */
	EZFS_NOSPC,		/* out of space */
	EZFS_FAULT,		/* bad address */
	EZFS_IO,		/* I/O error */
	EZFS_INTR,		/* signal received */
	EZFS_ISSPARE,		/* device is a hot spare */
	EZFS_INVALCONFIG,	/* invalid vdev configuration */
	EZFS_RECURSIVE,		/* recursive dependency */
	EZFS_NOHISTORY,		/* no history object */
	EZFS_POOLPROPS,		/* couldn't retrieve pool props */
	EZFS_POOL_NOTSUP,	/* ops not supported for this type of pool */
	EZFS_POOL_INVALARG,	/* invalid argument for this pool operation */
	EZFS_NAMETOOLONG,	/* dataset name is too long */
	EZFS_OPENFAILED,	/* open of device failed */
	EZFS_NOCAP,		/* couldn't get capacity */
	EZFS_LABELFAILED,	/* write of label failed */
	EZFS_BADWHO,		/* invalid permission who */
	EZFS_BADPERM,		/* invalid permission */
	EZFS_BADPERMSET,	/* invalid permission set name */
	EZFS_NODELEGATION,	/* delegated administration is disabled */
	EZFS_UNSHARESMBFAILED,	/* failed to unshare over smb */
	EZFS_SHARESMBFAILED,	/* failed to share over smb */
	EZFS_BADCACHE,		/* bad cache file */
	EZFS_ISL2CACHE,		/* device is for the level 2 ARC */
	EZFS_VDEVNOTSUP,	/* unsupported vdev type */
	EZFS_NOTSUP,		/* ops not supported on this dataset */
	EZFS_ACTIVE_SPARE,	/* pool has active shared spare devices */
	EZFS_UNPLAYED_LOGS,	/* log device has unplayed logs */
	EZFS_REFTAG_RELE,	/* snapshot release: tag not found */
	EZFS_REFTAG_HOLD,	/* snapshot hold: tag already exists */
	EZFS_TAGTOOLONG,	/* snapshot hold/rele: tag too long */
	EZFS_PIPEFAILED,	/* pipe create failed */
	EZFS_THREADCREATEFAILED, /* thread create failed */
	EZFS_POSTSPLIT_ONLINE,	/* onlining a disk after splitting it */
	EZFS_SCRUBBING,		/* currently scrubbing */
	EZFS_NO_SCRUB,		/* no active scrub */
	EZFS_DIFF,		/* general failure of zfs diff */
	EZFS_DIFFDATA,		/* bad zfs diff data */
	EZFS_POOLREADONLY,	/* pool is in read-only mode */
	EZFS_SCRUB_PAUSED,	/* scrub currently paused */
	EZFS_ACTIVE_POOL,	/* pool is imported on a different system */
	EZFS_CRYPTOFAILED,	/* failed to setup encryption */
	EZFS_NO_PENDING,	/* cannot cancel, no operation is pending */
	EZFS_CHECKPOINT_EXISTS,	/* checkpoint exists */
	EZFS_DISCARDING_CHECKPOINT,	/* currently discarding a checkpoint */
	EZFS_NO_CHECKPOINT,	/* pool has no checkpoint */
	EZFS_DEVRM_IN_PROGRESS,	/* a device is currently being removed */
	EZFS_VDEV_TOO_BIG,	/* a device is too big to be used */
	EZFS_IOC_NOTSUPPORTED,	/* operation not supported by zfs module */
	EZFS_TOOMANY,		/* argument list too long */
	EZFS_INITIALIZING,	/* currently initializing */
	EZFS_NO_INITIALIZE,	/* no active initialize */
	EZFS_WRONG_PARENT,	/* invalid parent dataset (e.g ZVOL) */
	EZFS_TRIMMING,		/* currently trimming */
	EZFS_NO_TRIM,		/* no active trim */
	EZFS_TRIM_NOTSUP,	/* device does not support trim */
	EZFS_NO_RESILVER_DEFER,	/* pool doesn't support resilver_defer */
	EZFS_EXPORT_IN_PROGRESS,	/* currently exporting the pool */
	EZFS_REBUILDING,	/* resilvering (sequential reconstrution) */
	EZFS_VDEV_NOTSUP,	/* ops not supported for this type of vdev */
	EZFS_UNKNOWN
} zfs_error_t;

/*
 * The following data structures are all part
 * of the zfs_allow_t data structure which is
 * used for printing 'allow' permissions.
 * It is a linked list of zfs_allow_t's which
 * then contain avl tree's for user/group/sets/...
 * and each one of the entries in those trees have
 * avl tree's for the permissions they belong to and
 * whether they are local,descendent or local+descendent
 * permissions.  The AVL trees are used primarily for
 * sorting purposes, but also so that we can quickly find
 * a given user and or permission.
 */
typedef struct zfs_perm_node {
	avl_node_t z_node;
	char z_pname[MAXPATHLEN];
} zfs_perm_node_t;

typedef struct zfs_allow_node {
	avl_node_t z_node;
	char z_key[MAXPATHLEN];		/* name, such as joe */
	avl_tree_t z_localdescend;	/* local+descendent perms */
	avl_tree_t z_local;		/* local permissions */
	avl_tree_t z_descend;		/* descendent permissions */
} zfs_allow_node_t;

typedef struct zfs_allow {
	struct zfs_allow *z_next;
	char z_setpoint[MAXPATHLEN];
	avl_tree_t z_sets;
	avl_tree_t z_crperms;
	avl_tree_t z_user;
	avl_tree_t z_group;
	avl_tree_t z_everyone;
} zfs_allow_t;

/*
 * Basic handle types
 */
typedef struct zfs_handle zfs_handle_t;
typedef struct zpool_handle zpool_handle_t;
typedef struct libzfs_handle libzfs_handle_t;

_LIBZFS_H int zpool_wait(zpool_handle_t *, zpool_wait_activity_t);
_LIBZFS_H int zpool_wait_status(zpool_handle_t *, zpool_wait_activity_t,
    boolean_t *, boolean_t *);

/*
 * Library initialization
 */
_LIBZFS_H libzfs_handle_t *libzfs_init(void);
_LIBZFS_H void libzfs_fini(libzfs_handle_t *);

_LIBZFS_H libzfs_handle_t *zpool_get_handle(zpool_handle_t *);
_LIBZFS_H libzfs_handle_t *zfs_get_handle(zfs_handle_t *);

_LIBZFS_H void libzfs_print_on_error(libzfs_handle_t *, boolean_t);

_LIBZFS_H void zfs_save_arguments(int argc, char **, char *, int);
_LIBZFS_H int zpool_log_history(libzfs_handle_t *, const char *);

_LIBZFS_H int libzfs_errno(libzfs_handle_t *);
_LIBZFS_H const char *libzfs_error_init(int);
_LIBZFS_H const char *libzfs_error_action(libzfs_handle_t *);
_LIBZFS_H const char *libzfs_error_description(libzfs_handle_t *);
_LIBZFS_H int zfs_standard_error(libzfs_handle_t *, int, const char *);
_LIBZFS_H void libzfs_mnttab_init(libzfs_handle_t *);
_LIBZFS_H void libzfs_mnttab_fini(libzfs_handle_t *);
_LIBZFS_H void libzfs_mnttab_cache(libzfs_handle_t *, boolean_t);
_LIBZFS_H int libzfs_mnttab_find(libzfs_handle_t *, const char *,
    struct mnttab *);
_LIBZFS_H void libzfs_mnttab_add(libzfs_handle_t *, const char *,
    const char *, const char *);
_LIBZFS_H void libzfs_mnttab_remove(libzfs_handle_t *, const char *);

/*
 * Basic handle functions
 */
_LIBZFS_H zpool_handle_t *zpool_open(libzfs_handle_t *, const char *);
_LIBZFS_H zpool_handle_t *zpool_open_canfail(libzfs_handle_t *, const char *);
_LIBZFS_H void zpool_close(zpool_handle_t *);
_LIBZFS_H const char *zpool_get_name(zpool_handle_t *);
_LIBZFS_H int zpool_get_state(zpool_handle_t *);
_LIBZFS_H const char *zpool_state_to_name(vdev_state_t, vdev_aux_t);
_LIBZFS_H const char *zpool_pool_state_to_name(pool_state_t);
_LIBZFS_H void zpool_free_handles(libzfs_handle_t *);

/*
 * Iterate over all active pools in the system.
 */
typedef int (*zpool_iter_f)(zpool_handle_t *, void *);
_LIBZFS_H int zpool_iter(libzfs_handle_t *, zpool_iter_f, void *);
_LIBZFS_H boolean_t zpool_skip_pool(const char *);

/*
 * Functions to create and destroy pools
 */
_LIBZFS_H int zpool_create(libzfs_handle_t *, const char *, nvlist_t *,
    nvlist_t *, nvlist_t *);
_LIBZFS_H int zpool_destroy(zpool_handle_t *, const char *);
_LIBZFS_H int zpool_add(zpool_handle_t *, nvlist_t *);

typedef struct splitflags {
	/* do not split, but return the config that would be split off */
	int dryrun : 1;

	/* after splitting, import the pool */
	int import : 1;
	int name_flags;
} splitflags_t;

typedef struct trimflags {
	/* requested vdevs are for the entire pool */
	boolean_t fullpool;

	/* request a secure trim, requires support from device */
	boolean_t secure;

	/* after starting trim, block until trim completes */
	boolean_t wait;

	/* trim at the requested rate in bytes/second */
	uint64_t rate;
} trimflags_t;

/*
 * Functions to manipulate pool and vdev state
 */
_LIBZFS_H int zpool_scan(zpool_handle_t *, pool_scan_func_t, pool_scrub_cmd_t);
_LIBZFS_H int zpool_initialize(zpool_handle_t *, pool_initialize_func_t,
    nvlist_t *);
_LIBZFS_H int zpool_initialize_wait(zpool_handle_t *, pool_initialize_func_t,
    nvlist_t *);
_LIBZFS_H int zpool_trim(zpool_handle_t *, pool_trim_func_t, nvlist_t *,
    trimflags_t *);

_LIBZFS_H int zpool_clear(zpool_handle_t *, const char *, nvlist_t *);
_LIBZFS_H int zpool_reguid(zpool_handle_t *);
_LIBZFS_H int zpool_reopen_one(zpool_handle_t *, void *);

_LIBZFS_H int zpool_sync_one(zpool_handle_t *, void *);

_LIBZFS_H int zpool_vdev_online(zpool_handle_t *, const char *, int,
    vdev_state_t *);
_LIBZFS_H int zpool_vdev_offline(zpool_handle_t *, const char *, boolean_t);
_LIBZFS_H int zpool_vdev_attach(zpool_handle_t *, const char *,
    const char *, nvlist_t *, int, boolean_t);
_LIBZFS_H int zpool_vdev_detach(zpool_handle_t *, const char *);
_LIBZFS_H int zpool_vdev_remove(zpool_handle_t *, const char *);
_LIBZFS_H int zpool_vdev_remove_cancel(zpool_handle_t *);
_LIBZFS_H int zpool_vdev_indirect_size(zpool_handle_t *, const char *,
    uint64_t *);
_LIBZFS_H int zpool_vdev_split(zpool_handle_t *, char *, nvlist_t **,
    nvlist_t *, splitflags_t);

_LIBZFS_H int zpool_vdev_fault(zpool_handle_t *, uint64_t, vdev_aux_t);
_LIBZFS_H int zpool_vdev_degrade(zpool_handle_t *, uint64_t, vdev_aux_t);
_LIBZFS_H int zpool_vdev_clear(zpool_handle_t *, uint64_t);

_LIBZFS_H nvlist_t *zpool_find_vdev(zpool_handle_t *, const char *, boolean_t *,
    boolean_t *, boolean_t *);
_LIBZFS_H nvlist_t *zpool_find_vdev_by_physpath(zpool_handle_t *, const char *,
    boolean_t *, boolean_t *, boolean_t *);
_LIBZFS_H int zpool_label_disk(libzfs_handle_t *, zpool_handle_t *,
    const char *);
_LIBZFS_H uint64_t zpool_vdev_path_to_guid(zpool_handle_t *zhp,
    const char *path);

_LIBZFS_H const char *zpool_get_state_str(zpool_handle_t *);

/*
 * Functions to manage pool properties
 */
_LIBZFS_H int zpool_set_prop(zpool_handle_t *, const char *, const char *);
_LIBZFS_H int zpool_get_prop(zpool_handle_t *, zpool_prop_t, char *,
    size_t proplen, zprop_source_t *, boolean_t literal);
_LIBZFS_H uint64_t zpool_get_prop_int(zpool_handle_t *, zpool_prop_t,
    zprop_source_t *);
_LIBZFS_H int zpool_props_refresh(zpool_handle_t *);

_LIBZFS_H const char *zpool_prop_to_name(zpool_prop_t);
_LIBZFS_H const char *zpool_prop_values(zpool_prop_t);

/*
 * Functions to manage vdev properties
 */
_LIBZFS_H int zpool_get_vdev_prop_value(nvlist_t *, vdev_prop_t, char *, char *,
    size_t, zprop_source_t *, boolean_t);
_LIBZFS_H int zpool_get_vdev_prop(zpool_handle_t *, const char *, vdev_prop_t,
    char *, char *, size_t, zprop_source_t *, boolean_t);
_LIBZFS_H int zpool_get_all_vdev_props(zpool_handle_t *, const char *,
    nvlist_t **);
_LIBZFS_H int zpool_set_vdev_prop(zpool_handle_t *, const char *, const char *,
    const char *);

_LIBZFS_H const char *vdev_prop_to_name(vdev_prop_t);
_LIBZFS_H const char *vdev_prop_values(vdev_prop_t);
_LIBZFS_H boolean_t vdev_prop_user(const char *name);
_LIBZFS_H const char *vdev_prop_column_name(vdev_prop_t);
_LIBZFS_H boolean_t vdev_prop_align_right(vdev_prop_t);

/*
 * Pool health statistics.
 */
typedef enum {
	/*
	 * The following correspond to faults as defined in the (fault.fs.zfs.*)
	 * event namespace.  Each is associated with a corresponding message ID.
	 * This must be kept in sync with the zfs_msgid_table in
	 * lib/libzfs/libzfs_status.c.
	 */
	ZPOOL_STATUS_CORRUPT_CACHE,	/* corrupt /kernel/drv/zpool.cache */
	ZPOOL_STATUS_MISSING_DEV_R,	/* missing device with replicas */
	ZPOOL_STATUS_MISSING_DEV_NR,	/* missing device with no replicas */
	ZPOOL_STATUS_CORRUPT_LABEL_R,	/* bad device label with replicas */
	ZPOOL_STATUS_CORRUPT_LABEL_NR,	/* bad device label with no replicas */
	ZPOOL_STATUS_BAD_GUID_SUM,	/* sum of device guids didn't match */
	ZPOOL_STATUS_CORRUPT_POOL,	/* pool metadata is corrupted */
	ZPOOL_STATUS_CORRUPT_DATA,	/* data errors in user (meta)data */
	ZPOOL_STATUS_FAILING_DEV,	/* device experiencing errors */
	ZPOOL_STATUS_VERSION_NEWER,	/* newer on-disk version */
	ZPOOL_STATUS_HOSTID_MISMATCH,	/* last accessed by another system */
	ZPOOL_STATUS_HOSTID_ACTIVE,	/* currently active on another system */
	ZPOOL_STATUS_HOSTID_REQUIRED,	/* multihost=on and hostid=0 */
	ZPOOL_STATUS_IO_FAILURE_WAIT,	/* failed I/O, failmode 'wait' */
	ZPOOL_STATUS_IO_FAILURE_CONTINUE, /* failed I/O, failmode 'continue' */
	ZPOOL_STATUS_IO_FAILURE_MMP,	/* failed MMP, failmode not 'panic' */
	ZPOOL_STATUS_BAD_LOG,		/* cannot read log chain(s) */
	ZPOOL_STATUS_ERRATA,		/* informational errata available */

	/*
	 * If the pool has unsupported features but can still be opened in
	 * read-only mode, its status is ZPOOL_STATUS_UNSUP_FEAT_WRITE. If the
	 * pool has unsupported features but cannot be opened at all, its
	 * status is ZPOOL_STATUS_UNSUP_FEAT_READ.
	 */
	ZPOOL_STATUS_UNSUP_FEAT_READ,	/* unsupported features for read */
	ZPOOL_STATUS_UNSUP_FEAT_WRITE,	/* unsupported features for write */

	/*
	 * These faults have no corresponding message ID.  At the time we are
	 * checking the status, the original reason for the FMA fault (I/O or
	 * checksum errors) has been lost.
	 */
	ZPOOL_STATUS_FAULTED_DEV_R,	/* faulted device with replicas */
	ZPOOL_STATUS_FAULTED_DEV_NR,	/* faulted device with no replicas */

	/*
	 * The following are not faults per se, but still an error possibly
	 * requiring administrative attention.  There is no corresponding
	 * message ID.
	 */
	ZPOOL_STATUS_VERSION_OLDER,	/* older legacy on-disk version */
	ZPOOL_STATUS_FEAT_DISABLED,	/* supported features are disabled */
	ZPOOL_STATUS_RESILVERING,	/* device being resilvered */
	ZPOOL_STATUS_OFFLINE_DEV,	/* device offline */
	ZPOOL_STATUS_REMOVED_DEV,	/* removed device */
	ZPOOL_STATUS_REBUILDING,	/* device being rebuilt */
	ZPOOL_STATUS_REBUILD_SCRUB,	/* recommend scrubbing the pool */
	ZPOOL_STATUS_NON_NATIVE_ASHIFT,	/* (e.g. 512e dev with ashift of 9) */
	ZPOOL_STATUS_COMPATIBILITY_ERR,	/* bad 'compatibility' property */
	ZPOOL_STATUS_INCOMPATIBLE_FEAT,	/* feature set outside compatibility */

	/*
	 * Finally, the following indicates a healthy pool.
	 */
	ZPOOL_STATUS_OK
} zpool_status_t;

_LIBZFS_H zpool_status_t zpool_get_status(zpool_handle_t *, char **,
    zpool_errata_t *);
_LIBZFS_H zpool_status_t zpool_import_status(nvlist_t *, char **,
    zpool_errata_t *);

/*
 * Statistics and configuration functions.
 */
_LIBZFS_H nvlist_t *zpool_get_config(zpool_handle_t *, nvlist_t **);
_LIBZFS_H nvlist_t *zpool_get_features(zpool_handle_t *);
_LIBZFS_H int zpool_refresh_stats(zpool_handle_t *, boolean_t *);
_LIBZFS_H int zpool_get_errlog(zpool_handle_t *, nvlist_t **);

/*
 * Import and export functions
 */
_LIBZFS_H int zpool_export(zpool_handle_t *, boolean_t, const char *);
_LIBZFS_H int zpool_export_force(zpool_handle_t *, const char *);
_LIBZFS_H int zpool_import(libzfs_handle_t *, nvlist_t *, const char *,
    char *altroot);
_LIBZFS_H int zpool_import_props(libzfs_handle_t *, nvlist_t *, const char *,
    nvlist_t *, int);
_LIBZFS_H void zpool_print_unsup_feat(nvlist_t *config);

/*
 * Miscellaneous pool functions
 */
struct zfs_cmd;

_LIBZFS_H const char *const zfs_history_event_names[];

typedef enum {
	VDEV_NAME_PATH		= 1 << 0,
	VDEV_NAME_GUID		= 1 << 1,
	VDEV_NAME_FOLLOW_LINKS	= 1 << 2,
	VDEV_NAME_TYPE_ID	= 1 << 3,
} vdev_name_t;

_LIBZFS_H char *zpool_vdev_name(libzfs_handle_t *, zpool_handle_t *, nvlist_t *,
    int name_flags);
_LIBZFS_H int zpool_upgrade(zpool_handle_t *, uint64_t);
_LIBZFS_H int zpool_get_history(zpool_handle_t *, nvlist_t **, uint64_t *,
    boolean_t *);
_LIBZFS_H int zpool_events_next(libzfs_handle_t *, nvlist_t **, int *, unsigned,
    int);
_LIBZFS_H int zpool_events_clear(libzfs_handle_t *, int *);
_LIBZFS_H int zpool_events_seek(libzfs_handle_t *, uint64_t, int);
_LIBZFS_H void zpool_obj_to_path_ds(zpool_handle_t *, uint64_t, uint64_t,
    char *, size_t);
_LIBZFS_H void zpool_obj_to_path(zpool_handle_t *, uint64_t, uint64_t, char *,
    size_t);
_LIBZFS_H int zfs_ioctl(libzfs_handle_t *, int, struct zfs_cmd *);
_LIBZFS_H int zpool_get_physpath(zpool_handle_t *, char *, size_t);
_LIBZFS_H void zpool_explain_recover(libzfs_handle_t *, const char *, int,
    nvlist_t *);
_LIBZFS_H int zpool_checkpoint(zpool_handle_t *);
_LIBZFS_H int zpool_discard_checkpoint(zpool_handle_t *);
_LIBZFS_H boolean_t zpool_is_draid_spare(const char *);

/*
 * Basic handle manipulations.  These functions do not create or destroy the
 * underlying datasets, only the references to them.
 */
_LIBZFS_H zfs_handle_t *zfs_open(libzfs_handle_t *, const char *, int);
_LIBZFS_H zfs_handle_t *zfs_handle_dup(zfs_handle_t *);
_LIBZFS_H void zfs_close(zfs_handle_t *);
_LIBZFS_H zfs_type_t zfs_get_type(const zfs_handle_t *);
_LIBZFS_H zfs_type_t zfs_get_underlying_type(const zfs_handle_t *);
_LIBZFS_H const char *zfs_get_name(const zfs_handle_t *);
_LIBZFS_H zpool_handle_t *zfs_get_pool_handle(const zfs_handle_t *);
_LIBZFS_H const char *zfs_get_pool_name(const zfs_handle_t *);

/*
 * Property management functions.  Some functions are shared with the kernel,
 * and are found in sys/fs/zfs.h.
 */

/*
 * zfs dataset property management
 */
_LIBZFS_H const char *zfs_prop_default_string(zfs_prop_t);
_LIBZFS_H uint64_t zfs_prop_default_numeric(zfs_prop_t);
_LIBZFS_H const char *zfs_prop_column_name(zfs_prop_t);
_LIBZFS_H boolean_t zfs_prop_align_right(zfs_prop_t);

_LIBZFS_H nvlist_t *zfs_valid_proplist(libzfs_handle_t *, zfs_type_t,
    nvlist_t *, uint64_t, zfs_handle_t *, zpool_handle_t *, boolean_t,
    const char *);

_LIBZFS_H const char *zfs_prop_to_name(zfs_prop_t);
_LIBZFS_H int zfs_prop_set(zfs_handle_t *, const char *, const char *);
_LIBZFS_H int zfs_prop_set_list(zfs_handle_t *, nvlist_t *);
_LIBZFS_H int zfs_prop_get(zfs_handle_t *, zfs_prop_t, char *, size_t,
    zprop_source_t *, char *, size_t, boolean_t);
_LIBZFS_H int zfs_prop_get_recvd(zfs_handle_t *, const char *, char *, size_t,
    boolean_t);
_LIBZFS_H int zfs_prop_get_numeric(zfs_handle_t *, zfs_prop_t, uint64_t *,
    zprop_source_t *, char *, size_t);
_LIBZFS_H int zfs_prop_get_userquota_int(zfs_handle_t *zhp,
    const char *propname, uint64_t *propvalue);
_LIBZFS_H int zfs_prop_get_userquota(zfs_handle_t *zhp, const char *propname,
    char *propbuf, int proplen, boolean_t literal);
_LIBZFS_H int zfs_prop_get_written_int(zfs_handle_t *zhp, const char *propname,
    uint64_t *propvalue);
_LIBZFS_H int zfs_prop_get_written(zfs_handle_t *zhp, const char *propname,
    char *propbuf, int proplen, boolean_t literal);
_LIBZFS_H int zfs_prop_get_feature(zfs_handle_t *zhp, const char *propname,
    char *buf, size_t len);
_LIBZFS_H uint64_t getprop_uint64(zfs_handle_t *, zfs_prop_t, char **);
_LIBZFS_H uint64_t zfs_prop_get_int(zfs_handle_t *, zfs_prop_t);
_LIBZFS_H int zfs_prop_inherit(zfs_handle_t *, const char *, boolean_t);
_LIBZFS_H const char *zfs_prop_values(zfs_prop_t);
_LIBZFS_H int zfs_prop_is_string(zfs_prop_t prop);
_LIBZFS_H nvlist_t *zfs_get_all_props(zfs_handle_t *);
_LIBZFS_H nvlist_t *zfs_get_user_props(zfs_handle_t *);
_LIBZFS_H nvlist_t *zfs_get_recvd_props(zfs_handle_t *);
_LIBZFS_H nvlist_t *zfs_get_clones_nvl(zfs_handle_t *);

_LIBZFS_H int zfs_wait_status(zfs_handle_t *, zfs_wait_activity_t,
    boolean_t *, boolean_t *);

/*
 * zfs encryption management
 */
_LIBZFS_H int zfs_crypto_get_encryption_root(zfs_handle_t *, boolean_t *,
    char *);
_LIBZFS_H int zfs_crypto_create(libzfs_handle_t *, char *, nvlist_t *,
    nvlist_t *, boolean_t stdin_available, uint8_t **, uint_t *);
_LIBZFS_H int zfs_crypto_clone_check(libzfs_handle_t *, zfs_handle_t *, char *,
    nvlist_t *);
_LIBZFS_H int zfs_crypto_attempt_load_keys(libzfs_handle_t *, const char *);
_LIBZFS_H int zfs_crypto_load_key(zfs_handle_t *, boolean_t, const char *);
_LIBZFS_H int zfs_crypto_unload_key(zfs_handle_t *);
_LIBZFS_H int zfs_crypto_rewrap(zfs_handle_t *, nvlist_t *, boolean_t);

typedef struct zprop_list {
	int		pl_prop;
	char		*pl_user_prop;
	struct zprop_list *pl_next;
	boolean_t	pl_all;
	size_t		pl_width;
	size_t		pl_recvd_width;
	boolean_t	pl_fixed;
} zprop_list_t;

_LIBZFS_H int zfs_expand_proplist(zfs_handle_t *, zprop_list_t **, boolean_t,
    boolean_t);
_LIBZFS_H void zfs_prune_proplist(zfs_handle_t *, uint8_t *);
_LIBZFS_H int vdev_expand_proplist(zpool_handle_t *, const char *,
    zprop_list_t **);

#define	ZFS_MOUNTPOINT_NONE	"none"
#define	ZFS_MOUNTPOINT_LEGACY	"legacy"

#define	ZFS_FEATURE_DISABLED	"disabled"
#define	ZFS_FEATURE_ENABLED	"enabled"
#define	ZFS_FEATURE_ACTIVE	"active"

#define	ZFS_UNSUPPORTED_INACTIVE	"inactive"
#define	ZFS_UNSUPPORTED_READONLY	"readonly"

/*
 * zpool property management
 */
_LIBZFS_H int zpool_expand_proplist(zpool_handle_t *, zprop_list_t **,
    zfs_type_t, boolean_t);
_LIBZFS_H int zpool_prop_get_feature(zpool_handle_t *, const char *, char *,
    size_t);
_LIBZFS_H const char *zpool_prop_default_string(zpool_prop_t);
_LIBZFS_H uint64_t zpool_prop_default_numeric(zpool_prop_t);
_LIBZFS_H const char *zpool_prop_column_name(zpool_prop_t);
_LIBZFS_H boolean_t zpool_prop_align_right(zpool_prop_t);

/*
 * Functions shared by zfs and zpool property management.
 */
_LIBZFS_H int zprop_iter(zprop_func func, void *cb, boolean_t show_all,
    boolean_t ordered, zfs_type_t type);
_LIBZFS_H int zprop_get_list(libzfs_handle_t *, char *, zprop_list_t **,
    zfs_type_t);
_LIBZFS_H void zprop_free_list(zprop_list_t *);

#define	ZFS_GET_NCOLS	5

typedef enum {
	GET_COL_NONE,
	GET_COL_NAME,
	GET_COL_PROPERTY,
	GET_COL_VALUE,
	GET_COL_RECVD,
	GET_COL_SOURCE
} zfs_get_column_t;

/*
 * Functions for printing zfs or zpool properties
 */
typedef struct vdev_cbdata {
	int cb_name_flags;
	char **cb_names;
	unsigned int cb_names_count;
} vdev_cbdata_t;

typedef struct zprop_get_cbdata {
	int cb_sources;
	zfs_get_column_t cb_columns[ZFS_GET_NCOLS];
	int cb_colwidths[ZFS_GET_NCOLS + 1];
	boolean_t cb_scripted;
	boolean_t cb_literal;
	boolean_t cb_first;
	zprop_list_t *cb_proplist;
	zfs_type_t cb_type;
	vdev_cbdata_t cb_vdevs;
} zprop_get_cbdata_t;

_LIBZFS_H void zprop_print_one_property(const char *, zprop_get_cbdata_t *,
    const char *, const char *, zprop_source_t, const char *,
    const char *);

/*
 * Iterator functions.
 */
typedef int (*zfs_iter_f)(zfs_handle_t *, void *);
_LIBZFS_H int zfs_iter_root(libzfs_handle_t *, zfs_iter_f, void *);
_LIBZFS_H int zfs_iter_children(zfs_handle_t *, zfs_iter_f, void *);
_LIBZFS_H int zfs_iter_dependents(zfs_handle_t *, boolean_t, zfs_iter_f,
    void *);
_LIBZFS_H int zfs_iter_filesystems(zfs_handle_t *, zfs_iter_f, void *);
_LIBZFS_H int zfs_iter_snapshots(zfs_handle_t *, boolean_t, zfs_iter_f, void *,
    uint64_t, uint64_t);
_LIBZFS_H int zfs_iter_snapshots_sorted(zfs_handle_t *, zfs_iter_f, void *,
    uint64_t, uint64_t);
_LIBZFS_H int zfs_iter_snapspec(zfs_handle_t *, const char *, zfs_iter_f,
    void *);
_LIBZFS_H int zfs_iter_bookmarks(zfs_handle_t *, zfs_iter_f, void *);
_LIBZFS_H int zfs_iter_mounted(zfs_handle_t *, zfs_iter_f, void *);

typedef struct get_all_cb {
	zfs_handle_t	**cb_handles;
	size_t		cb_alloc;
	size_t		cb_used;
} get_all_cb_t;

_LIBZFS_H void zfs_foreach_mountpoint(libzfs_handle_t *, zfs_handle_t **,
    size_t, zfs_iter_f, void *, boolean_t);
_LIBZFS_H void libzfs_add_handle(get_all_cb_t *, zfs_handle_t *);

/*
 * Functions to create and destroy datasets.
 */
_LIBZFS_H int zfs_create(libzfs_handle_t *, const char *, zfs_type_t,
    nvlist_t *);
_LIBZFS_H int zfs_create_ancestors(libzfs_handle_t *, const char *);
_LIBZFS_H int zfs_destroy(zfs_handle_t *, boolean_t);
_LIBZFS_H int zfs_destroy_snaps(zfs_handle_t *, char *, boolean_t);
_LIBZFS_H int zfs_destroy_snaps_nvl(libzfs_handle_t *, nvlist_t *, boolean_t);
_LIBZFS_H int zfs_destroy_snaps_nvl_os(libzfs_handle_t *, nvlist_t *);
_LIBZFS_H int zfs_clone(zfs_handle_t *, const char *, nvlist_t *);
_LIBZFS_H int zfs_snapshot(libzfs_handle_t *, const char *, boolean_t,
    nvlist_t *);
_LIBZFS_H int zfs_snapshot_nvl(libzfs_handle_t *hdl, nvlist_t *snaps,
    nvlist_t *props);
_LIBZFS_H int zfs_rollback(zfs_handle_t *, zfs_handle_t *, boolean_t);

typedef struct renameflags {
	/* recursive rename */
	int recursive : 1;

	/* don't unmount file systems */
	int nounmount : 1;

	/* force unmount file systems */
	int forceunmount : 1;
} renameflags_t;

_LIBZFS_H int zfs_rename(zfs_handle_t *, const char *, renameflags_t);

typedef struct sendflags {
	/* Amount of extra information to print. */
	int verbosity;

	/* recursive send  (ie, -R) */
	boolean_t replicate;

	/* for recursive send, skip sending missing snapshots */
	boolean_t skipmissing;

	/* for incrementals, do all intermediate snapshots */
	boolean_t doall;

	/* if dataset is a clone, do incremental from its origin */
	boolean_t fromorigin;

	/* field no longer used, maintained for backwards compatibility */
	boolean_t pad;

	/* send properties (ie, -p) */
	boolean_t props;

	/* do not send (no-op, ie. -n) */
	boolean_t dryrun;

	/* parsable verbose output (ie. -P) */
	boolean_t parsable;

	/* show progress (ie. -v) */
	boolean_t progress;

	/* large blocks (>128K) are permitted */
	boolean_t largeblock;

	/* WRITE_EMBEDDED records of type DATA are permitted */
	boolean_t embed_data;

	/* compressed WRITE records are permitted */
	boolean_t compress;

	/* raw encrypted records are permitted */
	boolean_t raw;

	/* only send received properties (ie. -b) */
	boolean_t backup;

	/* include snapshot holds in send stream */
	boolean_t holds;

	/* stream represents a partially received dataset */
	boolean_t saved;
} sendflags_t;

typedef boolean_t (snapfilter_cb_t)(zfs_handle_t *, void *);

_LIBZFS_H int zfs_send(zfs_handle_t *, const char *, const char *,
    sendflags_t *, int, snapfilter_cb_t, void *, nvlist_t **);
_LIBZFS_H int zfs_send_one(zfs_handle_t *, const char *, int, sendflags_t *,
    const char *);
_LIBZFS_H int zfs_send_progress(zfs_handle_t *, int, uint64_t *, uint64_t *);
_LIBZFS_H int zfs_send_resume(libzfs_handle_t *, sendflags_t *, int outfd,
    const char *);
_LIBZFS_H int zfs_send_saved(zfs_handle_t *, sendflags_t *, int, const char *);
_LIBZFS_H nvlist_t *zfs_send_resume_token_to_nvlist(libzfs_handle_t *hdl,
    const char *token);

_LIBZFS_H int zfs_promote(zfs_handle_t *);
_LIBZFS_H int zfs_hold(zfs_handle_t *, const char *, const char *,
    boolean_t, int);
_LIBZFS_H int zfs_hold_nvl(zfs_handle_t *, int, nvlist_t *);
_LIBZFS_H int zfs_release(zfs_handle_t *, const char *, const char *,
    boolean_t);
_LIBZFS_H int zfs_get_holds(zfs_handle_t *, nvlist_t **);
_LIBZFS_H uint64_t zvol_volsize_to_reservation(zpool_handle_t *, uint64_t,
    nvlist_t *);

typedef int (*zfs_userspace_cb_t)(void *arg, const char *domain,
    uid_t rid, uint64_t space);

_LIBZFS_H int zfs_userspace(zfs_handle_t *, zfs_userquota_prop_t,
    zfs_userspace_cb_t, void *);

_LIBZFS_H int zfs_get_fsacl(zfs_handle_t *, nvlist_t **);
_LIBZFS_H int zfs_set_fsacl(zfs_handle_t *, boolean_t, nvlist_t *);

typedef struct recvflags {
	/* print informational messages (ie, -v was specified) */
	boolean_t verbose;

	/* the destination is a prefix, not the exact fs (ie, -d) */
	boolean_t isprefix;

	/*
	 * Only the tail of the sent snapshot path is appended to the
	 * destination to determine the received snapshot name (ie, -e).
	 */
	boolean_t istail;

	/* do not actually do the recv, just check if it would work (ie, -n) */
	boolean_t dryrun;

	/* rollback/destroy filesystems as necessary (eg, -F) */
	boolean_t force;

	/* set "canmount=off" on all modified filesystems */
	boolean_t canmountoff;

	/*
	 * Mark the file systems as "resumable" and do not destroy them if the
	 * receive is interrupted
	 */
	boolean_t resumable;

	/* byteswap flag is used internally; callers need not specify */
	boolean_t byteswap;

	/* do not mount file systems as they are extracted (private) */
	boolean_t nomount;

	/* Was holds flag set in the compound header? */
	boolean_t holds;

	/* skip receive of snapshot holds */
	boolean_t skipholds;

	/* mount the filesystem unless nomount is specified */
	boolean_t domount;

	/* force unmount while recv snapshot (private) */
	boolean_t forceunmount;
} recvflags_t;

_LIBZFS_H int zfs_receive(libzfs_handle_t *, const char *, nvlist_t *,
    recvflags_t *, int, avl_tree_t *);

typedef enum diff_flags {
	ZFS_DIFF_PARSEABLE = 1 << 0,
	ZFS_DIFF_TIMESTAMP = 1 << 1,
	ZFS_DIFF_CLASSIFY = 1 << 2,
	ZFS_DIFF_NO_MANGLE = 1 << 3
} diff_flags_t;

_LIBZFS_H int zfs_show_diffs(zfs_handle_t *, int, const char *, const char *,
    int);

/*
 * Miscellaneous functions.
 */
_LIBZFS_H const char *zfs_type_to_name(zfs_type_t);
_LIBZFS_H void zfs_refresh_properties(zfs_handle_t *);
_LIBZFS_H int zfs_name_valid(const char *, zfs_type_t);
_LIBZFS_H zfs_handle_t *zfs_path_to_zhandle(libzfs_handle_t *, const char *,
    zfs_type_t);
_LIBZFS_H int zfs_parent_name(zfs_handle_t *, char *, size_t);
_LIBZFS_H boolean_t zfs_dataset_exists(libzfs_handle_t *, const char *,
    zfs_type_t);
_LIBZFS_H int zfs_spa_version(zfs_handle_t *, int *);
_LIBZFS_H boolean_t zfs_bookmark_exists(const char *path);

/*
 * Mount support functions.
 */
_LIBZFS_H boolean_t is_mounted(libzfs_handle_t *, const char *special, char **);
_LIBZFS_H boolean_t zfs_is_mounted(zfs_handle_t *, char **);
_LIBZFS_H int zfs_mount(zfs_handle_t *, const char *, int);
_LIBZFS_H int zfs_mount_at(zfs_handle_t *, const char *, int, const char *);
_LIBZFS_H int zfs_unmount(zfs_handle_t *, const char *, int);
_LIBZFS_H int zfs_unmountall(zfs_handle_t *, int);
_LIBZFS_H int zfs_mount_delegation_check(void);

#if defined(__linux__) || defined(__APPLE__)
_LIBZFS_H int zfs_parse_mount_options(char *mntopts, unsigned long *mntflags,
    unsigned long *zfsflags, int sloppy, char *badopt, char *mtabopt);
_LIBZFS_H void zfs_adjust_mount_options(zfs_handle_t *zhp, const char *mntpoint,
    char *mntopts, char *mtabopt);
#endif

/*
 * Share support functions.
 */
_LIBZFS_H boolean_t zfs_is_shared(zfs_handle_t *);
_LIBZFS_H int zfs_share(zfs_handle_t *);
_LIBZFS_H int zfs_unshare(zfs_handle_t *);

/*
 * Protocol-specific share support functions.
 */
_LIBZFS_H boolean_t zfs_is_shared_nfs(zfs_handle_t *, char **);
_LIBZFS_H boolean_t zfs_is_shared_smb(zfs_handle_t *, char **);
_LIBZFS_H int zfs_share_nfs(zfs_handle_t *);
_LIBZFS_H int zfs_share_smb(zfs_handle_t *);
_LIBZFS_H int zfs_shareall(zfs_handle_t *);
_LIBZFS_H int zfs_unshare_nfs(zfs_handle_t *, const char *);
_LIBZFS_H int zfs_unshare_smb(zfs_handle_t *, const char *);
_LIBZFS_H int zfs_unshareall_nfs(zfs_handle_t *);
_LIBZFS_H int zfs_unshareall_smb(zfs_handle_t *);
_LIBZFS_H int zfs_unshareall_bypath(zfs_handle_t *, const char *);
_LIBZFS_H int zfs_unshareall_bytype(zfs_handle_t *, const char *, const char *);
_LIBZFS_H int zfs_unshareall(zfs_handle_t *);
_LIBZFS_H int zfs_deleg_share_nfs(libzfs_handle_t *, char *, char *, char *,
    void *, void *, int, zfs_share_op_t);
_LIBZFS_H void zfs_commit_nfs_shares(void);
_LIBZFS_H void zfs_commit_smb_shares(void);
_LIBZFS_H void zfs_commit_all_shares(void);
_LIBZFS_H void zfs_commit_shares(const char *);

_LIBZFS_H int zfs_nicestrtonum(libzfs_handle_t *, const char *, uint64_t *);

/*
 * Utility functions to run an _LIBZFS_Hal process.
 */
#define	STDOUT_VERBOSE	0x01
#define	STDERR_VERBOSE	0x02
#define	NO_DEFAULT_PATH	0x04 /* Don't use $PATH to lookup the command */

_LIBZFS_H int libzfs_run_process(const char *, char **, int);
_LIBZFS_H int libzfs_run_process_get_stdout(const char *, char *[], char *[],
    char **[], int *);
_LIBZFS_H int libzfs_run_process_get_stdout_nopath(const char *, char *[],
    char *[], char **[], int *);

_LIBZFS_H void libzfs_free_str_array(char **, int);

_LIBZFS_H int libzfs_envvar_is_set(char *);

/*
 * Utility functions for zfs version
 */
_LIBZFS_H void zfs_version_userland(char *, int);
_LIBZFS_H int zfs_version_kernel(char *, int);
_LIBZFS_H int zfs_version_print(void);

/*
 * Given a device or file, determine if it is part of a pool.
 */
_LIBZFS_H int zpool_in_use(libzfs_handle_t *, int, pool_state_t *, char **,
    boolean_t *);

/*
 * Label manipulation.
 */
_LIBZFS_H int zpool_clear_label(int);
_LIBZFS_H int zpool_set_bootenv(zpool_handle_t *, const nvlist_t *);
_LIBZFS_H int zpool_get_bootenv(zpool_handle_t *, nvlist_t **);

/*
 * Management interfaces for SMB ACL files
 */

_LIBZFS_H int zfs_smb_acl_add(libzfs_handle_t *, char *, char *, char *);
_LIBZFS_H int zfs_smb_acl_remove(libzfs_handle_t *, char *, char *, char *);
_LIBZFS_H int zfs_smb_acl_purge(libzfs_handle_t *, char *, char *);
_LIBZFS_H int zfs_smb_acl_rename(libzfs_handle_t *, char *, char *, char *,
    char *);

/*
 * Enable and disable datasets within a pool by mounting/unmounting and
 * sharing/unsharing them.
 */
_LIBZFS_H int zpool_enable_datasets(zpool_handle_t *, const char *, int);
_LIBZFS_H int zpool_disable_datasets(zpool_handle_t *, boolean_t);
_LIBZFS_H void zpool_disable_datasets_os(zpool_handle_t *, boolean_t);
_LIBZFS_H void zpool_disable_volume_os(const char *);

/*
 * Parse a features file for -o compatibility
 */
typedef enum {
	ZPOOL_COMPATIBILITY_OK,
	ZPOOL_COMPATIBILITY_WARNTOKEN,
	ZPOOL_COMPATIBILITY_BADTOKEN,
	ZPOOL_COMPATIBILITY_BADFILE,
	ZPOOL_COMPATIBILITY_NOFILES
} zpool_compat_status_t;

_LIBZFS_H zpool_compat_status_t zpool_load_compat(const char *,
    boolean_t *, char *, size_t);

#ifdef __FreeBSD__

/*
 * Attach/detach the given filesystem to/from the given jail.
 */
_LIBZFS_H int zfs_jail(zfs_handle_t *zhp, int jailid, int attach);

/*
 * Set loader options for next boot.
 */
_LIBZFS_H int zpool_nextboot(libzfs_handle_t *, uint64_t, uint64_t,
    const char *);

#endif /* __FreeBSD__ */

#ifdef	__cplusplus
}
#endif

#endif	/* _LIBZFS_H */
