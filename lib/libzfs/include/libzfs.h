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
 * Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef	_LIBZFS_H
#define	_LIBZFS_H

#include <assert.h>
#include <libnvpair.h>
#include <sys/param.h>
#include <sys/types.h>
#include <sys/varargs.h>
#include <sys/fs/zfs.h>
#include <sys/avl.h>
#include <ucred.h>

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * Miscellaneous ZFS constants
 */
#define	ZFS_MAXNAMELEN		MAXNAMELEN
#define	ZPOOL_MAXNAMELEN	MAXNAMELEN
#define	ZFS_MAXPROPLEN		MAXPATHLEN
#define	ZPOOL_MAXPROPLEN	MAXPATHLEN

/*
 * libzfs errors
 */
enum {
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
	EZFS_VOLHASDATA,	/* volume already contains data */
	EZFS_INVALIDNAME,	/* invalid dataset name */
	EZFS_BADRESTORE,	/* unable to restore to destination */
	EZFS_BADBACKUP,		/* backup failed */
	EZFS_BADTARGET,		/* bad attach/detach/replace target */
	EZFS_NODEVICE,		/* no such device in pool */
	EZFS_BADDEV,		/* invalid device to add */
	EZFS_NOREPLICAS,	/* no valid replicas */
	EZFS_RESILVERING,	/* currently resilvering */
	EZFS_BADVERSION,	/* unsupported version */
	EZFS_POOLUNAVAIL,	/* pool is currently unavailable */
	EZFS_DEVOVERFLOW,	/* too many devices in one vdev */
	EZFS_BADPATH,		/* must be an absolute path */
	EZFS_CROSSTARGET,	/* rename or clone across pool or dataset */
	EZFS_ZONED,		/* used improperly in local zone */
	EZFS_MOUNTFAILED,	/* failed to mount dataset */
	EZFS_UMOUNTFAILED,	/* failed to unmount dataset */
	EZFS_UNSHARENFSFAILED,	/* unshare(1M) failed */
	EZFS_SHARENFSFAILED,	/* share(1M) failed */
	EZFS_DEVLINKS,		/* failed to create zvol links */
	EZFS_PERM,		/* permission denied */
	EZFS_NOSPC,		/* out of space */
	EZFS_IO,		/* I/O error */
	EZFS_INTR,		/* signal received */
	EZFS_ISSPARE,		/* device is a hot spare */
	EZFS_INVALCONFIG,	/* invalid vdev configuration */
	EZFS_RECURSIVE,		/* recursive dependency */
	EZFS_NOHISTORY,		/* no history object */
	EZFS_UNSHAREISCSIFAILED, /* iscsitgtd failed request to unshare */
	EZFS_SHAREISCSIFAILED,	/* iscsitgtd failed request to share */
	EZFS_POOLPROPS,		/* couldn't retrieve pool props */
	EZFS_POOL_NOTSUP,	/* ops not supported for this type of pool */
	EZFS_POOL_INVALARG,	/* invalid argument for this pool operation */
	EZFS_NAMETOOLONG,	/* dataset name is too long */
	EZFS_OPENFAILED,	/* open of device failed */
	EZFS_NOCAP,		/* couldn't get capacity */
	EZFS_LABELFAILED,	/* write of label failed */
	EZFS_ISCSISVCUNAVAIL,	/* iscsi service unavailable */
	EZFS_BADWHO,		/* invalid permission who */
	EZFS_BADPERM,		/* invalid permission */
	EZFS_BADPERMSET,	/* invalid permission set name */
	EZFS_NODELEGATION,	/* delegated administration is disabled */
	EZFS_PERMRDONLY,	/* pemissions are readonly */
	EZFS_UNSHARESMBFAILED,	/* failed to unshare over smb */
	EZFS_SHARESMBFAILED,	/* failed to share over smb */
	EZFS_BADCACHE,		/* bad cache file */
	EZFS_ISL2CACHE,		/* device is for the level 2 ARC */
	EZFS_VDEVNOTSUP,	/* unsupported vdev type */
	EZFS_NOTSUP,		/* ops not supported on this dataset */
	EZFS_ACTIVE_SPARE,	/* pool has active shared spare devices */
	EZFS_UNKNOWN
};

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

/*
 * Library initialization
 */
extern libzfs_handle_t *libzfs_init(void);
extern void libzfs_fini(libzfs_handle_t *);

extern libzfs_handle_t *zpool_get_handle(zpool_handle_t *);
extern libzfs_handle_t *zfs_get_handle(zfs_handle_t *);

extern void libzfs_print_on_error(libzfs_handle_t *, boolean_t);

extern int libzfs_errno(libzfs_handle_t *);
extern const char *libzfs_error_action(libzfs_handle_t *);
extern const char *libzfs_error_description(libzfs_handle_t *);

/*
 * Basic handle functions
 */
extern zpool_handle_t *zpool_open(libzfs_handle_t *, const char *);
extern zpool_handle_t *zpool_open_canfail(libzfs_handle_t *, const char *);
extern void zpool_close(zpool_handle_t *);
extern const char *zpool_get_name(zpool_handle_t *);
extern int zpool_get_state(zpool_handle_t *);
extern char *zpool_state_to_name(vdev_state_t, vdev_aux_t);
extern void zpool_free_handles(libzfs_handle_t *);

/*
 * Iterate over all active pools in the system.
 */
typedef int (*zpool_iter_f)(zpool_handle_t *, void *);
extern int zpool_iter(libzfs_handle_t *, zpool_iter_f, void *);

/*
 * Functions to create and destroy pools
 */
extern int zpool_create(libzfs_handle_t *, const char *, nvlist_t *,
    nvlist_t *, nvlist_t *);
extern int zpool_destroy(zpool_handle_t *);
extern int zpool_add(zpool_handle_t *, nvlist_t *);

/*
 * Functions to manipulate pool and vdev state
 */
extern int zpool_scrub(zpool_handle_t *, pool_scrub_type_t);
extern int zpool_clear(zpool_handle_t *, const char *);

extern int zpool_vdev_online(zpool_handle_t *, const char *, int,
    vdev_state_t *);
extern int zpool_vdev_offline(zpool_handle_t *, const char *, boolean_t);
extern int zpool_vdev_attach(zpool_handle_t *, const char *,
    const char *, nvlist_t *, int);
extern int zpool_vdev_detach(zpool_handle_t *, const char *);
extern int zpool_vdev_remove(zpool_handle_t *, const char *);

extern int zpool_vdev_fault(zpool_handle_t *, uint64_t);
extern int zpool_vdev_degrade(zpool_handle_t *, uint64_t);
extern int zpool_vdev_clear(zpool_handle_t *, uint64_t);

extern nvlist_t *zpool_find_vdev(zpool_handle_t *, const char *, boolean_t *,
    boolean_t *, boolean_t *);
extern int zpool_label_disk(libzfs_handle_t *, zpool_handle_t *, char *);

/*
 * Functions to manage pool properties
 */
extern int zpool_set_prop(zpool_handle_t *, const char *, const char *);
extern int zpool_get_prop(zpool_handle_t *, zpool_prop_t, char *,
    size_t proplen, zprop_source_t *);
extern uint64_t zpool_get_prop_int(zpool_handle_t *, zpool_prop_t,
    zprop_source_t *);

extern const char *zpool_prop_to_name(zpool_prop_t);
extern const char *zpool_prop_values(zpool_prop_t);

/*
 * Pool health statistics.
 */
typedef enum {
	/*
	 * The following correspond to faults as defined in the (fault.fs.zfs.*)
	 * event namespace.  Each is associated with a corresponding message ID.
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
	ZPOOL_STATUS_IO_FAILURE_WAIT,	/* failed I/O, failmode 'wait' */
	ZPOOL_STATUS_IO_FAILURE_CONTINUE, /* failed I/O, failmode 'continue' */
	ZPOOL_STATUS_FAULTED_DEV_R,	/* faulted device with replicas */
	ZPOOL_STATUS_FAULTED_DEV_NR,	/* faulted device with no replicas */
	ZPOOL_STATUS_BAD_LOG,		/* cannot read log chain(s) */

	/*
	 * The following are not faults per se, but still an error possibly
	 * requiring administrative attention.  There is no corresponding
	 * message ID.
	 */
	ZPOOL_STATUS_VERSION_OLDER,	/* older on-disk version */
	ZPOOL_STATUS_RESILVERING,	/* device being resilvered */
	ZPOOL_STATUS_OFFLINE_DEV,	/* device online */

	/*
	 * Finally, the following indicates a healthy pool.
	 */
	ZPOOL_STATUS_OK
} zpool_status_t;

extern zpool_status_t zpool_get_status(zpool_handle_t *, char **);
extern zpool_status_t zpool_import_status(nvlist_t *, char **);

/*
 * Statistics and configuration functions.
 */
extern nvlist_t *zpool_get_config(zpool_handle_t *, nvlist_t **);
extern int zpool_refresh_stats(zpool_handle_t *, boolean_t *);
extern int zpool_get_errlog(zpool_handle_t *, nvlist_t **);

/*
 * Import and export functions
 */
extern int zpool_export(zpool_handle_t *, boolean_t);
extern int zpool_import(libzfs_handle_t *, nvlist_t *, const char *,
    char *altroot);
extern int zpool_import_props(libzfs_handle_t *, nvlist_t *, const char *,
    nvlist_t *, boolean_t);

/*
 * Search for pools to import
 */
extern nvlist_t *zpool_find_import(libzfs_handle_t *, int, char **);
extern nvlist_t *zpool_find_import_cached(libzfs_handle_t *, const char *,
    char *, uint64_t);
extern nvlist_t *zpool_find_import_byname(libzfs_handle_t *, int, char **,
    char *);
extern nvlist_t *zpool_find_import_byguid(libzfs_handle_t *, int, char **,
    uint64_t);
extern nvlist_t *zpool_find_import_activeok(libzfs_handle_t *, int, char **);

/*
 * Miscellaneous pool functions
 */
struct zfs_cmd;

extern char *zpool_vdev_name(libzfs_handle_t *, zpool_handle_t *, nvlist_t *);
extern int zpool_upgrade(zpool_handle_t *, uint64_t);
extern int zpool_get_history(zpool_handle_t *, nvlist_t **);
extern void zpool_set_history_str(const char *subcommand, int argc,
    char **argv, char *history_str);
extern int zpool_stage_history(libzfs_handle_t *, const char *);
extern void zpool_obj_to_path(zpool_handle_t *, uint64_t, uint64_t, char *,
    size_t len);
extern int zfs_ioctl(libzfs_handle_t *, int, struct zfs_cmd *);
extern int zpool_get_physpath(zpool_handle_t *, char *);
/*
 * Basic handle manipulations.  These functions do not create or destroy the
 * underlying datasets, only the references to them.
 */
extern zfs_handle_t *zfs_open(libzfs_handle_t *, const char *, int);
extern void zfs_close(zfs_handle_t *);
extern zfs_type_t zfs_get_type(const zfs_handle_t *);
extern const char *zfs_get_name(const zfs_handle_t *);
extern zpool_handle_t *zfs_get_pool_handle(const zfs_handle_t *);

/*
 * Property management functions.  Some functions are shared with the kernel,
 * and are found in sys/fs/zfs.h.
 */

/*
 * zfs dataset property management
 */
extern const char *zfs_prop_default_string(zfs_prop_t);
extern uint64_t zfs_prop_default_numeric(zfs_prop_t);
extern const char *zfs_prop_column_name(zfs_prop_t);
extern boolean_t zfs_prop_align_right(zfs_prop_t);

extern nvlist_t *zfs_valid_proplist(libzfs_handle_t *, zfs_type_t,
    nvlist_t *, uint64_t, zfs_handle_t *, const char *);

extern const char *zfs_prop_to_name(zfs_prop_t);
extern int zfs_prop_set(zfs_handle_t *, const char *, const char *);
extern int zfs_prop_get(zfs_handle_t *, zfs_prop_t, char *, size_t,
    zprop_source_t *, char *, size_t, boolean_t);
extern int zfs_prop_get_numeric(zfs_handle_t *, zfs_prop_t, uint64_t *,
    zprop_source_t *, char *, size_t);
extern uint64_t zfs_prop_get_int(zfs_handle_t *, zfs_prop_t);
extern int zfs_prop_inherit(zfs_handle_t *, const char *);
extern const char *zfs_prop_values(zfs_prop_t);
extern int zfs_prop_is_string(zfs_prop_t prop);
extern nvlist_t *zfs_get_user_props(zfs_handle_t *);

typedef struct zprop_list {
	int		pl_prop;
	char		*pl_user_prop;
	struct zprop_list *pl_next;
	boolean_t	pl_all;
	size_t		pl_width;
	boolean_t	pl_fixed;
} zprop_list_t;

extern int zfs_expand_proplist(zfs_handle_t *, zprop_list_t **);

#define	ZFS_MOUNTPOINT_NONE	"none"
#define	ZFS_MOUNTPOINT_LEGACY	"legacy"

/*
 * zpool property management
 */
extern int zpool_expand_proplist(zpool_handle_t *, zprop_list_t **);
extern const char *zpool_prop_default_string(zpool_prop_t);
extern uint64_t zpool_prop_default_numeric(zpool_prop_t);
extern const char *zpool_prop_column_name(zpool_prop_t);
extern boolean_t zpool_prop_align_right(zpool_prop_t);

/*
 * Functions shared by zfs and zpool property management.
 */
extern int zprop_iter(zprop_func func, void *cb, boolean_t show_all,
    boolean_t ordered, zfs_type_t type);
extern int zprop_get_list(libzfs_handle_t *, char *, zprop_list_t **,
    zfs_type_t);
extern void zprop_free_list(zprop_list_t *);

/*
 * Functions for printing zfs or zpool properties
 */
typedef struct zprop_get_cbdata {
	int cb_sources;
	int cb_columns[4];
	int cb_colwidths[5];
	boolean_t cb_scripted;
	boolean_t cb_literal;
	boolean_t cb_first;
	zprop_list_t *cb_proplist;
	zfs_type_t cb_type;
} zprop_get_cbdata_t;

void zprop_print_one_property(const char *, zprop_get_cbdata_t *,
    const char *, const char *, zprop_source_t, const char *);

#define	GET_COL_NAME		1
#define	GET_COL_PROPERTY	2
#define	GET_COL_VALUE		3
#define	GET_COL_SOURCE		4

/*
 * Iterator functions.
 */
typedef int (*zfs_iter_f)(zfs_handle_t *, void *);
extern int zfs_iter_root(libzfs_handle_t *, zfs_iter_f, void *);
extern int zfs_iter_children(zfs_handle_t *, zfs_iter_f, void *);
extern int zfs_iter_dependents(zfs_handle_t *, boolean_t, zfs_iter_f, void *);
extern int zfs_iter_filesystems(zfs_handle_t *, zfs_iter_f, void *);
extern int zfs_iter_snapshots(zfs_handle_t *, zfs_iter_f, void *);

/*
 * Functions to create and destroy datasets.
 */
extern int zfs_create(libzfs_handle_t *, const char *, zfs_type_t,
    nvlist_t *);
extern int zfs_create_ancestors(libzfs_handle_t *, const char *);
extern int zfs_destroy(zfs_handle_t *);
extern int zfs_destroy_snaps(zfs_handle_t *, char *);
extern int zfs_clone(zfs_handle_t *, const char *, nvlist_t *);
extern int zfs_snapshot(libzfs_handle_t *, const char *, boolean_t, nvlist_t *);
extern int zfs_rollback(zfs_handle_t *, zfs_handle_t *, boolean_t);
extern int zfs_rename(zfs_handle_t *, const char *, boolean_t);
extern int zfs_send(zfs_handle_t *, const char *, const char *,
    boolean_t, boolean_t, boolean_t, boolean_t, int);
extern int zfs_promote(zfs_handle_t *);

typedef struct recvflags {
	/* print informational messages (ie, -v was specified) */
	int verbose : 1;

	/* the destination is a prefix, not the exact fs (ie, -d) */
	int isprefix : 1;

	/* do not actually do the recv, just check if it would work (ie, -n) */
	int dryrun : 1;

	/* rollback/destroy filesystems as necessary (eg, -F) */
	int force : 1;

	/* set "canmount=off" on all modified filesystems */
	int canmountoff : 1;

	/* byteswap flag is used internally; callers need not specify */
	int byteswap : 1;
} recvflags_t;

extern int zfs_receive(libzfs_handle_t *, const char *, recvflags_t,
    int, avl_tree_t *);

/*
 * Miscellaneous functions.
 */
extern const char *zfs_type_to_name(zfs_type_t);
extern void zfs_refresh_properties(zfs_handle_t *);
extern int zfs_name_valid(const char *, zfs_type_t);
extern zfs_handle_t *zfs_path_to_zhandle(libzfs_handle_t *, char *, zfs_type_t);
extern boolean_t zfs_dataset_exists(libzfs_handle_t *, const char *,
    zfs_type_t);
extern int zfs_spa_version(zfs_handle_t *, int *);

/*
 * dataset permission functions.
 */
extern int zfs_perm_set(zfs_handle_t *, nvlist_t *);
extern int zfs_perm_remove(zfs_handle_t *, nvlist_t *);
extern int zfs_build_perms(zfs_handle_t *, char *, char *,
    zfs_deleg_who_type_t, zfs_deleg_inherit_t, nvlist_t **nvlist_t);
extern int zfs_perm_get(zfs_handle_t *, zfs_allow_t **);
extern void zfs_free_allows(zfs_allow_t *);
extern void zfs_deleg_permissions(void);

/*
 * Mount support functions.
 */
extern boolean_t is_mounted(libzfs_handle_t *, const char *special, char **);
extern boolean_t zfs_is_mounted(zfs_handle_t *, char **);
extern int zfs_mount(zfs_handle_t *, const char *, int);
extern int zfs_unmount(zfs_handle_t *, const char *, int);
extern int zfs_unmountall(zfs_handle_t *, int);

/*
 * Share support functions.
 */
extern boolean_t zfs_is_shared(zfs_handle_t *);
extern int zfs_share(zfs_handle_t *);
extern int zfs_unshare(zfs_handle_t *);

/*
 * Protocol-specific share support functions.
 */
extern boolean_t zfs_is_shared_nfs(zfs_handle_t *, char **);
extern boolean_t zfs_is_shared_smb(zfs_handle_t *, char **);
extern int zfs_share_nfs(zfs_handle_t *);
extern int zfs_share_smb(zfs_handle_t *);
extern int zfs_shareall(zfs_handle_t *);
extern int zfs_unshare_nfs(zfs_handle_t *, const char *);
extern int zfs_unshare_smb(zfs_handle_t *, const char *);
extern int zfs_unshareall_nfs(zfs_handle_t *);
extern int zfs_unshareall_smb(zfs_handle_t *);
extern int zfs_unshareall_bypath(zfs_handle_t *, const char *);
extern int zfs_unshareall(zfs_handle_t *);
extern boolean_t zfs_is_shared_iscsi(zfs_handle_t *);
extern int zfs_share_iscsi(zfs_handle_t *);
extern int zfs_unshare_iscsi(zfs_handle_t *);
extern int zfs_iscsi_perm_check(libzfs_handle_t *, char *, ucred_t *);
extern int zfs_deleg_share_nfs(libzfs_handle_t *, char *, char *,
    void *, void *, int, zfs_share_op_t);

/*
 * When dealing with nvlists, verify() is extremely useful
 */
#ifdef NDEBUG
#define	verify(EX)	((void)(EX))
#else
#define	verify(EX)	assert(EX)
#endif

/*
 * Utility function to convert a number to a human-readable form.
 */
extern void zfs_nicenum(uint64_t, char *, size_t);
extern int zfs_nicestrtonum(libzfs_handle_t *, const char *, uint64_t *);

/*
 * Given a device or file, determine if it is part of a pool.
 */
extern int zpool_in_use(libzfs_handle_t *, int, pool_state_t *, char **,
    boolean_t *);

/*
 * ftyp special.  Read the label from a given device.
 */
extern int zpool_read_label(int, nvlist_t **);

/*
 * Create and remove zvol /dev links.
 */
extern int zpool_create_zvol_links(zpool_handle_t *);
extern int zpool_remove_zvol_links(zpool_handle_t *);

/* is this zvol valid for use as a dump device? */
extern int zvol_check_dump_config(char *);

/*
 * Enable and disable datasets within a pool by mounting/unmounting and
 * sharing/unsharing them.
 */
extern int zpool_enable_datasets(zpool_handle_t *, const char *, int);
extern int zpool_disable_datasets(zpool_handle_t *, boolean_t);

#ifdef	__cplusplus
}
#endif

#endif	/* _LIBZFS_H */
