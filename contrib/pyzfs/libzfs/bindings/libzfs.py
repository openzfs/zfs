#
# Copyright 2019 Hudson River Trading LLC.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#    http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#

from __future__ import absolute_import, division, print_function

SOURCE = """
#include <libzfs/libzfs.h>
"""

LIBRARY = "zfs"

_added_src = """
///////////////////////////////////////////////////////
// ADDED
///////////////////////////////////////////////////////
#define    MAXPATHLEN       1024
#define    ZFS_MAX_DATASET_NAME_LEN 256
#define    ZFSSHARE_MISS    0x01    /* Didn't find entry in cache */

// These #defines were originally strings.
#define    ZFS_MOUNTPOINT_NONE        0
#define    ZFS_MOUNTPOINT_LEGACY    1
#define    ZFS_FEATURE_DISABLED    2
#define    ZFS_FEATURE_ENABLED        3
#define    ZFS_FEATURE_ACTIVE        4
#define    ZFS_UNSUPPORTED_INACTIVE    5
#define    ZFS_UNSUPPORTED_READONLY    6
// End defines being originally strings


typedef struct uu_avl_pool uu_avl_pool_t;
typedef struct uu_avl uu_avl_t;
/*
 * Type used for the root of the AVL tree.
 */
typedef struct avl_tree avl_tree_t;

/*
 * The data nodes in the AVL tree must have a field of this type.
 */
typedef struct avl_node avl_node_t;

/*
 * Basic handle types
 */
typedef struct zfs_handle zfs_handle_t;
typedef struct zpool_handle zpool_handle_t;
typedef struct libzfs_handle libzfs_handle_t;

// AVL tree node
struct avl_node {
    struct avl_node *avl_child[2];    /* left/right children nodes */
    uintptr_t avl_pcb;        /* parent, child_index, balance */
};

// AVL tree
struct avl_tree {
    struct avl_node *avl_root;    /* root node in tree */
    int (*avl_compar)(const void *, const void *);

    size_t avl_offset;        /* offsetof(type, avl_link_t field) */
    uint64_t avl_numnodes;        /* number of nodes in the tree */
    size_t avl_size;        /* sizeof user type struct */
};


/*
 * vdev states are ordered from least to most healthy.
 * A vdev that's CANT_OPEN or below is considered unusable.
 */
typedef enum vdev_state {
    VDEV_STATE_UNKNOWN = 0,    /* Uninitialized vdev			*/
    VDEV_STATE_CLOSED,    /* Not currently open			*/
    VDEV_STATE_OFFLINE,    /* Not allowed to open			*/
    VDEV_STATE_REMOVED,    /* Explicitly removed from system	*/
    VDEV_STATE_CANT_OPEN,    /* Tried to open, but failed		*/
    VDEV_STATE_FAULTED,    /* External request to fault device	*/
    VDEV_STATE_DEGRADED,    /* Replicated vdev with unhealthy kids	*/
    VDEV_STATE_HEALTHY    /* Presumed good			*/
} vdev_state_t;

/*
 * vdev aux states.  When a vdev is in the CANT_OPEN state, the aux field
 * of the vdev stats structure uses these constants to distinguish why.
 */
typedef enum vdev_aux {
    VDEV_AUX_NONE,        /* no error				*/
    VDEV_AUX_OPEN_FAILED,    /* ldi_open_*() or vn_open() failed	*/
    VDEV_AUX_CORRUPT_DATA,    /* bad label or disk contents		*/
    VDEV_AUX_NO_REPLICAS,    /* insufficient number of replicas	*/
    VDEV_AUX_BAD_GUID_SUM,    /* vdev guid sum doesn't match		*/
    VDEV_AUX_TOO_SMALL,    /* vdev size is too small		*/
    VDEV_AUX_BAD_LABEL,    /* the label is OK but invalid		*/
    VDEV_AUX_VERSION_NEWER,    /* on-disk version is too new		*/
    VDEV_AUX_VERSION_OLDER,    /* on-disk version is too old		*/
    VDEV_AUX_UNSUP_FEAT,    /* unsupported features			*/
    VDEV_AUX_SPARED,    /* hot spare used in another pool	*/
    VDEV_AUX_ERR_EXCEEDED,    /* too many errors			*/
    VDEV_AUX_IO_FAILURE,    /* experienced I/O failure		*/
    VDEV_AUX_BAD_LOG,    /* cannot read log chain(s)		*/
    VDEV_AUX_EXTERNAL,    /* external diagnosis or forced fault	*/
    VDEV_AUX_SPLIT_POOL,    /* vdev was split off into another pool	*/
    VDEV_AUX_BAD_ASHIFT,    /* vdev ashift is invalid		*/
    VDEV_AUX_EXTERNAL_PERSIST,    /* persistent forced fault	*/
    VDEV_AUX_ACTIVE,    /* vdev active on a different host	*/
    VDEV_AUX_CHILDREN_OFFLINE, /* all children are offline		*/
} vdev_aux_t;

/*
 * pool state.  The following states are written to disk as part of the normal
 * SPA lifecycle: ACTIVE, EXPORTED, DESTROYED, SPARE, L2CACHE.  The remaining
 * states are software abstractions used at various levels to communicate
 * pool state.
 */
typedef enum pool_state {
    POOL_STATE_ACTIVE = 0,        /* In active use		*/
    POOL_STATE_EXPORTED,        /* Explicitly exported		*/
    POOL_STATE_DESTROYED,        /* Explicitly destroyed		*/
    POOL_STATE_SPARE,        /* Reserved for hot spare use	*/
    POOL_STATE_L2CACHE,        /* Level 2 ARC device		*/
    POOL_STATE_UNINITIALIZED,    /* Internal spa_t state		*/
    POOL_STATE_UNAVAIL,        /* Internal libzfs state	*/
    POOL_STATE_POTENTIALLY_ACTIVE    /* Internal libzfs state	*/
} pool_state_t;

/*
 * Initialize functions.
 */
typedef enum pool_initialize_func {
    POOL_INITIALIZE_START,
    POOL_INITIALIZE_CANCEL,
    POOL_INITIALIZE_SUSPEND,
    POOL_INITIALIZE_FUNCS
} pool_initialize_func_t;

/*
 * TRIM functions.
 */
typedef enum pool_trim_func {
    POOL_TRIM_START,
    POOL_TRIM_CANCEL,
    POOL_TRIM_SUSPEND,
    POOL_TRIM_FUNCS
} pool_trim_func_t;

/*
 * Scan Functions.
 */
typedef enum pool_scan_func {
    POOL_SCAN_NONE,
    POOL_SCAN_SCRUB,
    POOL_SCAN_RESILVER,
    POOL_SCAN_FUNCS
} pool_scan_func_t;

/*
 * Used to control scrub pause and resume.
 */
typedef enum pool_scrub_cmd {
    POOL_SCRUB_NORMAL = 0,
    POOL_SCRUB_PAUSE,
    POOL_SCRUB_FLAGS_END
} pool_scrub_cmd_t;

typedef enum {
    CS_NONE,
    CS_CHECKPOINT_EXISTS,
    CS_CHECKPOINT_DISCARDING,
    CS_NUM_STATES
} checkpoint_state_t;

typedef struct pool_checkpoint_stat {
    uint64_t pcs_state;        /* checkpoint_state_t */
    uint64_t pcs_start_time;    /* time checkpoint/discard started */
    uint64_t pcs_space;        /* checkpointed space */
} pool_checkpoint_stat_t;


typedef enum {
    ZFS_TYPE_FILESYSTEM = (1),
    ZFS_TYPE_SNAPSHOT = (3),
    ZFS_TYPE_VOLUME = (5),
    ZFS_TYPE_POOL = (7),
    ZFS_TYPE_BOOKMARK = (9)
} zfs_type_t;

/*
 * NB: lzc_dataset_type should be updated whenever a new objset type is added,
 * if it represents a real type of a dataset that can be created from userland.
 */
typedef enum dmu_objset_type {
    DMU_OST_NONE,
    DMU_OST_META,
    DMU_OST_ZFS,
    DMU_OST_ZVOL,
    DMU_OST_OTHER,            /* For testing only! */
    DMU_OST_ANY,            /* Be careful! */
    DMU_OST_NUMTYPES
} dmu_objset_type_t;

typedef struct dmu_objset_stats {
    uint64_t dds_num_clones; /* number of clones of this */
    uint64_t dds_creation_txg;
    uint64_t dds_guid;
    dmu_objset_type_t dds_type;
    uint8_t dds_is_snapshot;
    uint8_t dds_inconsistent;
    char dds_origin[ZFS_MAX_DATASET_NAME_LEN];
} dmu_objset_stats_t;


/*
 * ZIO types.  Needed to interpret vdev statistics below.
 */
typedef enum zio_type {
    ZIO_TYPE_NULL = 0,
    ZIO_TYPE_READ,
    ZIO_TYPE_WRITE,
    ZIO_TYPE_FREE,
    ZIO_TYPE_CLAIM,
    ZIO_TYPE_IOCTL,
    ZIO_TYPE_TRIM,
    ZIO_TYPES
} zio_type_t;
/*
 * Pool properties are identified by these constants and must be added to the
 * end of this list to ensure that external consumers are not affected
 * by the change. If you make any changes to this list, be sure to update
 * the property table in module/zcommon/zpool_prop.c.
 */
typedef enum {
    ZPOOL_PROP_INVAL = -1,
    ZPOOL_PROP_NAME,
    ZPOOL_PROP_SIZE,
    ZPOOL_PROP_CAPACITY,
    ZPOOL_PROP_ALTROOT,
    ZPOOL_PROP_HEALTH,
    ZPOOL_PROP_GUID,
    ZPOOL_PROP_VERSION,
    ZPOOL_PROP_BOOTFS,
    ZPOOL_PROP_DELEGATION,
    ZPOOL_PROP_AUTOREPLACE,
    ZPOOL_PROP_CACHEFILE,
    ZPOOL_PROP_FAILUREMODE,
    ZPOOL_PROP_LISTSNAPS,
    ZPOOL_PROP_AUTOEXPAND,
    ZPOOL_PROP_DEDUPDITTO,
    ZPOOL_PROP_DEDUPRATIO,
    ZPOOL_PROP_FREE,
    ZPOOL_PROP_ALLOCATED,
    ZPOOL_PROP_READONLY,
    ZPOOL_PROP_ASHIFT,
    ZPOOL_PROP_COMMENT,
    ZPOOL_PROP_EXPANDSZ,
    ZPOOL_PROP_FREEING,
    ZPOOL_PROP_FRAGMENTATION,
    ZPOOL_PROP_LEAKED,
    ZPOOL_PROP_MAXBLOCKSIZE,
    ZPOOL_PROP_TNAME,
    ZPOOL_PROP_MAXDNODESIZE,
    ZPOOL_PROP_MULTIHOST,
    ZPOOL_PROP_CHECKPOINT,
    ZPOOL_PROP_LOAD_GUID,
    ZPOOL_PROP_AUTOTRIM,
    ZPOOL_NUM_PROPS
} zpool_prop_t;

typedef enum {
    ZPROP_SRC_NONE = 0x1,
    ZPROP_SRC_DEFAULT = 0x2,
    ZPROP_SRC_TEMPORARY = 0x4,
    ZPROP_SRC_LOCAL = 0x8,
    ZPROP_SRC_INHERITED = 0x10,
    ZPROP_SRC_RECEIVED = 0x20
} zprop_source_t;


/*
 * Pool statistics.  Note: all fields should be 64-bit because this
 * is passed between kernel and userland as an nvlist uint64 array.
 */
typedef struct pool_scan_stat {
    /* values stored on disk */
    uint64_t pss_func;    /* pool_scan_func_t */
    uint64_t pss_state;    /* dsl_scan_state_t */
    uint64_t pss_start_time;    /* scan start time */
    uint64_t pss_end_time;    /* scan end time */
    uint64_t pss_to_examine;    /* total bytes to scan */
    uint64_t pss_examined;    /* total bytes located by scanner */
    uint64_t pss_to_process; /* total bytes to process */
    uint64_t pss_processed;    /* total processed bytes */
    uint64_t pss_errors;    /* scan errors	*/

    /* values not stored on disk */
    uint64_t pss_pass_exam; /* examined bytes per scan pass */
    uint64_t pss_pass_start;    /* start time of a scan pass */
    uint64_t pss_pass_scrub_pause; /* pause time of a scurb pass */
    /* cumulative time scrub spent paused, needed for rate calculation */
    uint64_t pss_pass_scrub_spent_paused;
    uint64_t pss_pass_issued; /* issued bytes per scan pass */
    uint64_t pss_issued;    /* total bytes checked by scanner */
} pool_scan_stat_t;

typedef struct pool_removal_stat {
    uint64_t prs_state; /* dsl_scan_state_t */
    uint64_t prs_removing_vdev;
    uint64_t prs_start_time;
    uint64_t prs_end_time;
    uint64_t prs_to_copy; /* bytes that need to be copied */
    uint64_t prs_copied; /* bytes copied so far */
    /*
     * bytes of memory used for indirect mappings.
     * This includes all removed vdevs.
     */
    uint64_t prs_mapping_memory;
} pool_removal_stat_t;

struct libzfs_handle {
    int libzfs_error;
    int libzfs_fd;
    FILE *libzfs_mnttab;
    FILE *libzfs_sharetab;
    zpool_handle_t *libzfs_pool_handles;
    uu_avl_pool_t *libzfs_ns_avlpool;
    uu_avl_t *libzfs_ns_avl;
    uint64_t libzfs_ns_gen;
    int libzfs_desc_active;
    char libzfs_action[1024];
    char libzfs_desc[1024];
    int libzfs_printerr;
    int libzfs_storeerr; /* stuff error messages into buffer */
    void *libzfs_sharehdl; /* libshare handle */
    uint_t libzfs_shareflags;
    boolean_t libzfs_mnttab_enable;
    /*
     * We need a lock to handle the case where parallel mount
     * threads are populating the mnttab cache simultaneously. The
     * lock only protects the integrity of the avl tree, and does
     * not protect the contents of the mnttab entries themselves.
     */
    // pthread_mutex_t libzfs_mnttab_cache_lock;
    avl_tree_t libzfs_mnttab_cache;
    int libzfs_pool_iter;
    char libzfs_chassis_id[256];
    boolean_t libzfs_prop_debug;
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


struct zpool_handle {
    libzfs_handle_t *zpool_hdl;
    zpool_handle_t *zpool_next;
    char zpool_name[ZFS_MAX_DATASET_NAME_LEN];
    int zpool_state;
    size_t zpool_config_size;
    nvlist_t *zpool_config;
    nvlist_t *zpool_old_config;
    nvlist_t *zpool_props;
    unsigned long long zpool_start_block;
};

///////////////////////////////////////////////////////
// END ADDED
///////////////////////////////////////////////////////

"""

_libzfs_src = """
// These values are MAXPATHLEN, but they have been changed
// to their actual value as CFFI does not like inferring stuff
#define    ZFS_MAXPROPLEN   1024
#define    ZPOOL_MAXPROPLEN 1024

/*
 * libzfs errors
 */
typedef enum zfs_error {
    EZFS_SUCCESS = 0,    /* no error -- success */
    EZFS_NOMEM = 2000,    /* out of memory */
    EZFS_BADPROP,        /* invalid property value */
    EZFS_PROPREADONLY,    /* cannot set readonly property */
    EZFS_PROPTYPE,        /* property does not apply to dataset type */
    EZFS_PROPNONINHERIT,    /* property is not inheritable */
    EZFS_PROPSPACE,        /* bad quota or reservation */
    EZFS_BADTYPE,        /* dataset is not of appropriate type */
    EZFS_BUSY,        /* pool or dataset is busy */
    EZFS_EXISTS,        /* pool or dataset already exists */
    EZFS_NOENT,        /* no such pool or dataset */
    EZFS_BADSTREAM,        /* bad backup stream */
    EZFS_DSREADONLY,    /* dataset is readonly */
    EZFS_VOLTOOBIG,        /* volume is too large for 32-bit system */
    EZFS_INVALIDNAME,    /* invalid dataset name */
    EZFS_BADRESTORE,    /* unable to restore to destination */
    EZFS_BADBACKUP,        /* backup failed */
    EZFS_BADTARGET,        /* bad attach/detach/replace target */
    EZFS_NODEVICE,        /* no such device in pool */
    EZFS_BADDEV,        /* invalid device to add */
    EZFS_NOREPLICAS,    /* no valid replicas */
    EZFS_RESILVERING,    /* currently resilvering */
    EZFS_BADVERSION,    /* unsupported version */
    EZFS_POOLUNAVAIL,    /* pool is currently unavailable */
    EZFS_DEVOVERFLOW,    /* too many devices in one vdev */
    EZFS_BADPATH,        /* must be an absolute path */
    EZFS_CROSSTARGET,    /* rename or clone across pool or dataset */
    EZFS_ZONED,        /* used improperly in local zone */
    EZFS_MOUNTFAILED,    /* failed to mount dataset */
    EZFS_UMOUNTFAILED,    /* failed to unmount dataset */
    EZFS_UNSHARENFSFAILED,    /* unshare(1M) failed */
    EZFS_SHARENFSFAILED,    /* share(1M) failed */
    EZFS_PERM,        /* permission denied */
    EZFS_NOSPC,        /* out of space */
    EZFS_FAULT,        /* bad address */
    EZFS_IO,        /* I/O error */
    EZFS_INTR,        /* signal received */
    EZFS_ISSPARE,        /* device is a hot spare */
    EZFS_INVALCONFIG,    /* invalid vdev configuration */
    EZFS_RECURSIVE,        /* recursive dependency */
    EZFS_NOHISTORY,        /* no history object */
    EZFS_POOLPROPS,        /* couldn't retrieve pool props */
    EZFS_POOL_NOTSUP,    /* ops not supported for this type of pool */
    EZFS_POOL_INVALARG,    /* invalid argument for this pool operation */
    EZFS_NAMETOOLONG,    /* dataset name is too long */
    EZFS_OPENFAILED,    /* open of device failed */
    EZFS_NOCAP,        /* couldn't get capacity */
    EZFS_LABELFAILED,    /* write of label failed */
    EZFS_BADWHO,        /* invalid permission who */
    EZFS_BADPERM,        /* invalid permission */
    EZFS_BADPERMSET,    /* invalid permission set name */
    EZFS_NODELEGATION,    /* delegated administration is disabled */
    EZFS_UNSHARESMBFAILED,    /* failed to unshare over smb */
    EZFS_SHARESMBFAILED,    /* failed to share over smb */
    EZFS_BADCACHE,        /* bad cache file */
    EZFS_ISL2CACHE,        /* device is for the level 2 ARC */
    EZFS_VDEVNOTSUP,    /* unsupported vdev type */
    EZFS_NOTSUP,        /* ops not supported on this dataset */
    EZFS_ACTIVE_SPARE,    /* pool has active shared spare devices */
    EZFS_UNPLAYED_LOGS,    /* log device has unplayed logs */
    EZFS_REFTAG_RELE,    /* snapshot release: tag not found */
    EZFS_REFTAG_HOLD,    /* snapshot hold: tag already exists */
    EZFS_TAGTOOLONG,    /* snapshot hold/rele: tag too long */
    EZFS_PIPEFAILED,    /* pipe create failed */
    EZFS_THREADCREATEFAILED, /* thread create failed */
    EZFS_POSTSPLIT_ONLINE,    /* onlining a disk after splitting it */
    EZFS_SCRUBBING,        /* currently scrubbing */
    EZFS_NO_SCRUB,        /* no active scrub */
    EZFS_DIFF,        /* general failure of zfs diff */
    EZFS_DIFFDATA,        /* bad zfs diff data */
    EZFS_POOLREADONLY,    /* pool is in read-only mode */
    EZFS_SCRUB_PAUSED,    /* scrub currently paused */
    EZFS_ACTIVE_POOL,    /* pool is imported on a different system */
    EZFS_CRYPTOFAILED,    /* failed to setup encryption */
    EZFS_NO_PENDING,    /* cannot cancel, no operation is pending */
    EZFS_CHECKPOINT_EXISTS,    /* checkpoint exists */
    EZFS_DISCARDING_CHECKPOINT,    /* currently discarding a checkpoint */
    EZFS_NO_CHECKPOINT,    /* pool has no checkpoint */
    EZFS_DEVRM_IN_PROGRESS,    /* a device is currently being removed */
    EZFS_VDEV_TOO_BIG,    /* a device is too big to be used */
    EZFS_IOC_NOTSUPPORTED,    /* operation not supported by zfs module */
    EZFS_TOOMANY,        /* argument list too long */
    EZFS_INITIALIZING,    /* currently initializing */
    EZFS_NO_INITIALIZE,    /* no active initialize */
    EZFS_WRONG_PARENT,    /* invalid parent dataset (e.g ZVOL) */
    EZFS_TRIMMING,        /* currently trimming */
    EZFS_NO_TRIM,        /* no active trim */
    EZFS_TRIM_NOTSUP,    /* device does not support trim */
    EZFS_NO_RESILVER_DEFER,    /* pool doesn't support resilver_defer */
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
    char z_key[MAXPATHLEN];        /* name, such as joe */
    avl_tree_t z_localdescend;    /* local+descendent perms */
    avl_tree_t z_local;        /* local permissions */
    avl_tree_t z_descend;        /* descendent permissions */
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
 * Library initialization
 */
extern libzfs_handle_t *libzfs_init(void);

extern void libzfs_fini(libzfs_handle_t *);

extern libzfs_handle_t *zpool_get_handle(zpool_handle_t *);

extern libzfs_handle_t *zfs_get_handle(zfs_handle_t *);

extern void libzfs_print_on_error(libzfs_handle_t *, boolean_t);

extern void zfs_save_arguments(int argc, char **, char *, int);

extern int zpool_log_history(libzfs_handle_t *, const char *);

extern int libzfs_errno(libzfs_handle_t *);

extern const char *libzfs_error_init(int);

extern const char *libzfs_error_action(libzfs_handle_t *);

extern const char *libzfs_error_description(libzfs_handle_t *);

extern int zfs_standard_error(libzfs_handle_t *, int, const char *);

extern void libzfs_mnttab_init(libzfs_handle_t *);

extern void libzfs_mnttab_fini(libzfs_handle_t *);

extern void libzfs_mnttab_cache(libzfs_handle_t *, boolean_t);

extern int libzfs_mnttab_find(libzfs_handle_t *, const char *,
                              struct mnttab *);

extern void libzfs_mnttab_add(libzfs_handle_t *, const char *,
                              const char *, const char *);

extern void libzfs_mnttab_remove(libzfs_handle_t *, const char *);

/*
 * Basic handle functions
 */
extern zpool_handle_t *zpool_open(libzfs_handle_t *, const char *);

extern zpool_handle_t *zpool_open_canfail(libzfs_handle_t *, const char *);

extern void zpool_close(zpool_handle_t *);

extern const char *zpool_get_name(zpool_handle_t *);

extern int zpool_get_state(zpool_handle_t *);

extern const char *zpool_state_to_name(vdev_state_t, vdev_aux_t);

extern const char *zpool_pool_state_to_name(pool_state_t);

extern void zpool_free_handles(libzfs_handle_t *);

/*
 * Iterate over all active pools in the system.
 */
typedef int (*zpool_iter_f)(zpool_handle_t *, void *);

extern int zpool_iter(libzfs_handle_t *, zpool_iter_f, void *);

extern boolean_t zpool_skip_pool(const char *);

/*
 * Functions to create and destroy pools
 */
extern int zpool_create(libzfs_handle_t *, const char *, nvlist_t *,
                        nvlist_t *, nvlist_t *);

extern int zpool_destroy(zpool_handle_t *, const char *);

extern int zpool_add(zpool_handle_t *, nvlist_t *);

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

    /* trim at the requested rate in bytes/second */
    uint64_t rate;
} trimflags_t;

/*
 * Functions to manipulate pool and vdev state
 */
extern int zpool_scan(zpool_handle_t *, pool_scan_func_t, pool_scrub_cmd_t);

extern int zpool_initialize(zpool_handle_t *, pool_initialize_func_t,
                            nvlist_t *);

extern int zpool_trim(zpool_handle_t *, pool_trim_func_t, nvlist_t *,
                      trimflags_t *);

extern int zpool_clear(zpool_handle_t *, const char *, nvlist_t *);

extern int zpool_reguid(zpool_handle_t *);

extern int zpool_reopen_one(zpool_handle_t *, void *);

extern int zpool_sync_one(zpool_handle_t *, void *);

extern int zpool_vdev_online(zpool_handle_t *, const char *, int,
                             vdev_state_t *);

extern int zpool_vdev_offline(zpool_handle_t *, const char *, boolean_t);

extern int zpool_vdev_attach(zpool_handle_t *, const char *,
                             const char *, nvlist_t *, int);

extern int zpool_vdev_detach(zpool_handle_t *, const char *);

extern int zpool_vdev_remove(zpool_handle_t *, const char *);

extern int zpool_vdev_remove_cancel(zpool_handle_t *);

extern int zpool_vdev_indirect_size(zpool_handle_t *, const char *,
                                    uint64_t *);

extern int zpool_vdev_split(zpool_handle_t *, char *, nvlist_t **, nvlist_t *,
                            splitflags_t);

extern int zpool_vdev_fault(zpool_handle_t *, uint64_t, vdev_aux_t);

extern int zpool_vdev_degrade(zpool_handle_t *, uint64_t, vdev_aux_t);

extern int zpool_vdev_clear(zpool_handle_t *, uint64_t);

extern nvlist_t *zpool_find_vdev(zpool_handle_t *, const char *, boolean_t *,
                                 boolean_t *, boolean_t *);

extern nvlist_t *zpool_find_vdev_by_physpath(zpool_handle_t *, const char *,
                                             boolean_t *, boolean_t *,
                                             boolean_t *);

extern int zpool_label_disk(libzfs_handle_t *, zpool_handle_t *, char *);

extern uint64_t zpool_vdev_path_to_guid(zpool_handle_t *zhp, const char *path);

const char *zpool_get_state_str(zpool_handle_t *);

/*
 * Functions to manage pool properties
 */
extern int zpool_set_prop(zpool_handle_t *, const char *, const char *);

extern int zpool_get_prop(zpool_handle_t *, zpool_prop_t, char *,
                          size_t proplen, zprop_source_t *, boolean_t literal);

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
     * This must be kept in sync with the zfs_msgid_table in
     * lib/libzfs/libzfs_status.c.
     */
    ZPOOL_STATUS_CORRUPT_CACHE,    /* corrupt /kernel/drv/zpool.cache */
    ZPOOL_STATUS_MISSING_DEV_R,    /* missing device with replicas */
    ZPOOL_STATUS_MISSING_DEV_NR,    /* missing device with no replicas */
    ZPOOL_STATUS_CORRUPT_LABEL_R,    /* bad device label with replicas */
    ZPOOL_STATUS_CORRUPT_LABEL_NR,    /* bad device label with no replicas */
    ZPOOL_STATUS_BAD_GUID_SUM,    /* sum of device guids didn't match */
    ZPOOL_STATUS_CORRUPT_POOL,    /* pool metadata is corrupted */
    ZPOOL_STATUS_CORRUPT_DATA,    /* data errors in user (meta)data */
    ZPOOL_STATUS_FAILING_DEV,    /* device experiencing errors */
    ZPOOL_STATUS_VERSION_NEWER,    /* newer on-disk version */
    ZPOOL_STATUS_HOSTID_MISMATCH,    /* last accessed by another system */
    ZPOOL_STATUS_HOSTID_ACTIVE,    /* currently active on another system */
    ZPOOL_STATUS_HOSTID_REQUIRED,    /* multihost=on and hostid=0 */
    ZPOOL_STATUS_IO_FAILURE_WAIT,    /* failed I/O, failmode 'wait' */
    ZPOOL_STATUS_IO_FAILURE_CONTINUE, /* failed I/O, failmode 'continue' */
    ZPOOL_STATUS_IO_FAILURE_MMP,    /* failed MMP, failmode not 'panic' */
    ZPOOL_STATUS_BAD_LOG,        /* cannot read log chain(s) */
    ZPOOL_STATUS_ERRATA,        /* informational errata available */

    /*
     * If the pool has unsupported features but can still be opened in
     * read-only mode, its status is ZPOOL_STATUS_UNSUP_FEAT_WRITE. If the
     * pool has unsupported features but cannot be opened at all, its
     * status is ZPOOL_STATUS_UNSUP_FEAT_READ.
     */
    ZPOOL_STATUS_UNSUP_FEAT_READ,    /* unsupported features for read */
    ZPOOL_STATUS_UNSUP_FEAT_WRITE,    /* unsupported features for write */

    /*
     * These faults have no corresponding message ID.  At the time we are
     * checking the status, the original reason for the FMA fault (I/O or
     * checksum errors) has been lost.
     */
            ZPOOL_STATUS_FAULTED_DEV_R,    /* faulted device with replicas */
    ZPOOL_STATUS_FAULTED_DEV_NR,    /* faulted device with no replicas */

    /*
     * The following are not faults per se, but still an error possibly
     * requiring administrative attention.  There is no corresponding
     * message ID.
     */
            ZPOOL_STATUS_VERSION_OLDER,    /* older legacy on-disk version */
    ZPOOL_STATUS_FEAT_DISABLED,    /* supported features are disabled */
    ZPOOL_STATUS_RESILVERING,    /* device being resilvered */
    ZPOOL_STATUS_OFFLINE_DEV,    /* device offline */
    ZPOOL_STATUS_REMOVED_DEV,    /* removed device */

    /*
     * Finally, the following indicates a healthy pool.
     */
            ZPOOL_STATUS_OK
} zpool_status_t;

typedef enum zpool_errata {
    ZPOOL_ERRATA_NONE,
    ZPOOL_ERRATA_ZOL_2094_SCRUB,
    ZPOOL_ERRATA_ZOL_2094_ASYNC_DESTROY,
    ZPOOL_ERRATA_ZOL_6845_ENCRYPTION,
    ZPOOL_ERRATA_ZOL_8308_ENCRYPTION,
} zpool_errata_t;

extern zpool_status_t zpool_get_status(zpool_handle_t *, char **,
                                       zpool_errata_t *);

extern zpool_status_t zpool_import_status(nvlist_t *, char **,
                                          zpool_errata_t *);

/*
 * Statistics and configuration functions.
 */
extern nvlist_t *zpool_get_config(zpool_handle_t *, nvlist_t **);

extern nvlist_t *zpool_get_features(zpool_handle_t *);

extern int zpool_refresh_stats(zpool_handle_t *, boolean_t *);

extern int zpool_get_errlog(zpool_handle_t *, nvlist_t **);

/*
 * Import and export functions
 */
extern int zpool_export(zpool_handle_t *, boolean_t, const char *);

extern int zpool_export_force(zpool_handle_t *, const char *);

extern int zpool_import(libzfs_handle_t *, nvlist_t *, const char *,
                        char *altroot);

extern int zpool_import_props(libzfs_handle_t *, nvlist_t *, const char *,
                              nvlist_t *, int);

extern void zpool_print_unsup_feat(nvlist_t *config);

/*
 * Miscellaneous pool functions
 */
struct zfs_cmd;

extern const char *zfs_history_event_names[];

typedef enum {
    VDEV_NAME_PATH = 1,
    VDEV_NAME_GUID = 3,
    VDEV_NAME_FOLLOW_LINKS = 5,
    VDEV_NAME_TYPE_ID = 7,
} vdev_name_t;

extern char *zpool_vdev_name(libzfs_handle_t *, zpool_handle_t *, nvlist_t *,
                             int name_flags);

extern int zpool_upgrade(zpool_handle_t *, uint64_t);

extern int zpool_get_history(zpool_handle_t *, nvlist_t **);

extern int zpool_events_next(libzfs_handle_t *, nvlist_t **, int *, unsigned,
                             int);

extern int zpool_events_clear(libzfs_handle_t *, int *);

extern int zpool_events_seek(libzfs_handle_t *, uint64_t, int);

extern void zpool_obj_to_path(zpool_handle_t *, uint64_t, uint64_t, char *,
                              size_t len);

extern int zfs_ioctl(libzfs_handle_t *, int, struct zfs_cmd *);

extern int zpool_get_physpath(zpool_handle_t *, char *, size_t);

extern void zpool_explain_recover(libzfs_handle_t *, const char *, int,
                                  nvlist_t *);

extern int zpool_checkpoint(zpool_handle_t *);

extern int zpool_discard_checkpoint(zpool_handle_t *);

/*
 * Basic handle manipulations.  These functions do not create or destroy the
 * underlying datasets, only the references to them.
 */
extern zfs_handle_t *zfs_open(libzfs_handle_t *, const char *, int);

extern zfs_handle_t *zfs_handle_dup(zfs_handle_t *);

extern void zfs_close(zfs_handle_t *);

extern zfs_type_t zfs_get_type(const zfs_handle_t *);

extern const char *zfs_get_name(const zfs_handle_t *);

extern zpool_handle_t *zfs_get_pool_handle(const zfs_handle_t *);

extern const char *zfs_get_pool_name(const zfs_handle_t *);

/*
 * Property management functions.  Some functions are shared with the kernel,
 * and are found in sys/fs/zfs.h.
 */
/*
 * Dataset properties are identified by these constants and must be added to
 * the end of this list to ensure that external consumers are not affected
 * by the change. If you make any changes to this list, be sure to update
 * the property table in module/zcommon/zfs_prop.c.
 */
typedef enum {
    ZPROP_CONT = -2,
    ZPROP_INVAL = -1,
    ZFS_PROP_TYPE = 0,
    ZFS_PROP_CREATION,
    ZFS_PROP_USED,
    ZFS_PROP_AVAILABLE,
    ZFS_PROP_REFERENCED,
    ZFS_PROP_COMPRESSRATIO,
    ZFS_PROP_MOUNTED,
    ZFS_PROP_ORIGIN,
    ZFS_PROP_QUOTA,
    ZFS_PROP_RESERVATION,
    ZFS_PROP_VOLSIZE,
    ZFS_PROP_VOLBLOCKSIZE,
    ZFS_PROP_RECORDSIZE,
    ZFS_PROP_MOUNTPOINT,
    ZFS_PROP_SHARENFS,
    ZFS_PROP_CHECKSUM,
    ZFS_PROP_COMPRESSION,
    ZFS_PROP_ATIME,
    ZFS_PROP_DEVICES,
    ZFS_PROP_EXEC,
    ZFS_PROP_SETUID,
    ZFS_PROP_READONLY,
    ZFS_PROP_ZONED,
    ZFS_PROP_SNAPDIR,
    ZFS_PROP_PRIVATE,        /* not exposed to user, temporary */
    ZFS_PROP_ACLINHERIT,
    ZFS_PROP_CREATETXG,
    ZFS_PROP_NAME,            /* not exposed to the user */
    ZFS_PROP_CANMOUNT,
    ZFS_PROP_ISCSIOPTIONS,        /* not exposed to the user */
    ZFS_PROP_XATTR,
    ZFS_PROP_NUMCLONES,        /* not exposed to the user */
    ZFS_PROP_COPIES,
    ZFS_PROP_VERSION,
    ZFS_PROP_UTF8ONLY,
    ZFS_PROP_NORMALIZE,
    ZFS_PROP_CASE,
    ZFS_PROP_VSCAN,
    ZFS_PROP_NBMAND,
    ZFS_PROP_SHARESMB,
    ZFS_PROP_REFQUOTA,
    ZFS_PROP_REFRESERVATION,
    ZFS_PROP_GUID,
    ZFS_PROP_PRIMARYCACHE,
    ZFS_PROP_SECONDARYCACHE,
    ZFS_PROP_USEDSNAP,
    ZFS_PROP_USEDDS,
    ZFS_PROP_USEDCHILD,
    ZFS_PROP_USEDREFRESERV,
    ZFS_PROP_USERACCOUNTING,    /* not exposed to the user */
    ZFS_PROP_STMF_SHAREINFO,    /* not exposed to the user */
    ZFS_PROP_DEFER_DESTROY,
    ZFS_PROP_USERREFS,
    ZFS_PROP_LOGBIAS,
    ZFS_PROP_UNIQUE,        /* not exposed to the user */
    ZFS_PROP_OBJSETID,
    ZFS_PROP_DEDUP,
    ZFS_PROP_MLSLABEL,
    ZFS_PROP_SYNC,
    ZFS_PROP_DNODESIZE,
    ZFS_PROP_REFRATIO,
    ZFS_PROP_WRITTEN,
    ZFS_PROP_CLONES,
    ZFS_PROP_LOGICALUSED,
    ZFS_PROP_LOGICALREFERENCED,
    ZFS_PROP_INCONSISTENT,        /* not exposed to the user */
    ZFS_PROP_VOLMODE,
    ZFS_PROP_FILESYSTEM_LIMIT,
    ZFS_PROP_SNAPSHOT_LIMIT,
    ZFS_PROP_FILESYSTEM_COUNT,
    ZFS_PROP_SNAPSHOT_COUNT,
    ZFS_PROP_SNAPDEV,
    ZFS_PROP_ACLTYPE,
    ZFS_PROP_SELINUX_CONTEXT,
    ZFS_PROP_SELINUX_FSCONTEXT,
    ZFS_PROP_SELINUX_DEFCONTEXT,
    ZFS_PROP_SELINUX_ROOTCONTEXT,
    ZFS_PROP_RELATIME,
    ZFS_PROP_REDUNDANT_METADATA,
    ZFS_PROP_OVERLAY,
    ZFS_PROP_PREV_SNAP,
    ZFS_PROP_RECEIVE_RESUME_TOKEN,
    ZFS_PROP_ENCRYPTION,
    ZFS_PROP_KEYLOCATION,
    ZFS_PROP_KEYFORMAT,
    ZFS_PROP_PBKDF2_SALT,
    ZFS_PROP_PBKDF2_ITERS,
    ZFS_PROP_ENCRYPTION_ROOT,
    ZFS_PROP_KEY_GUID,
    ZFS_PROP_KEYSTATUS,
    ZFS_PROP_REMAPTXG,        /* not exposed to the user */
    ZFS_PROP_SPECIAL_SMALL_BLOCKS,
    ZFS_PROP_IVSET_GUID,        /* not exposed to the user */
    ZFS_NUM_PROPS
} zfs_prop_t;

typedef enum {
    ZFS_PROP_USERUSED,
    ZFS_PROP_USERQUOTA,
    ZFS_PROP_GROUPUSED,
    ZFS_PROP_GROUPQUOTA,
    ZFS_PROP_USEROBJUSED,
    ZFS_PROP_USEROBJQUOTA,
    ZFS_PROP_GROUPOBJUSED,
    ZFS_PROP_GROUPOBJQUOTA,
    ZFS_PROP_PROJECTUSED,
    ZFS_PROP_PROJECTQUOTA,
    ZFS_PROP_PROJECTOBJUSED,
    ZFS_PROP_PROJECTOBJQUOTA,
    ZFS_NUM_USERQUOTA_PROPS
} zfs_userquota_prop_t;


/*
 * zfs dataset property management
 */
extern const char *zfs_prop_default_string(zfs_prop_t);

extern uint64_t zfs_prop_default_numeric(zfs_prop_t);

extern const char *zfs_prop_column_name(zfs_prop_t);

extern boolean_t zfs_prop_align_right(zfs_prop_t);

extern nvlist_t *zfs_valid_proplist(libzfs_handle_t *, zfs_type_t, nvlist_t *,
                                    uint64_t, zfs_handle_t *, zpool_handle_t *,
                                     boolean_t, const char *);

extern const char *zfs_prop_to_name(zfs_prop_t);

extern int zfs_prop_set(zfs_handle_t *, const char *, const char *);

extern int zfs_prop_set_list(zfs_handle_t *, nvlist_t *);

extern int zfs_prop_get(zfs_handle_t *, zfs_prop_t, char *, size_t,
                        zprop_source_t *, char *, size_t, boolean_t);

extern int zfs_prop_get_recvd(zfs_handle_t *, const char *, char *, size_t,
                              boolean_t);

extern int zfs_prop_get_numeric(zfs_handle_t *, zfs_prop_t, uint64_t *,
                                zprop_source_t *, char *, size_t);

extern int zfs_prop_get_userquota_int(zfs_handle_t *zhp, const char *propname,
                                      uint64_t *propvalue);

extern int zfs_prop_get_userquota(zfs_handle_t *zhp, const char *propname,
                                  char *propbuf, int proplen,
                                  boolean_t literal);

extern int zfs_prop_get_written_int(zfs_handle_t *zhp, const char *propname,
                                    uint64_t *propvalue);

extern int zfs_prop_get_written(zfs_handle_t *zhp, const char *propname,
                                char *propbuf, int proplen, boolean_t literal);

extern int zfs_prop_get_feature(zfs_handle_t *zhp, const char *propname,
                                char *buf, size_t len);

extern uint64_t getprop_uint64(zfs_handle_t *, zfs_prop_t, char **);

extern uint64_t zfs_prop_get_int(zfs_handle_t *, zfs_prop_t);

extern int zfs_prop_inherit(zfs_handle_t *, const char *, boolean_t);

extern const char *zfs_prop_values(zfs_prop_t);

extern int zfs_prop_is_string(zfs_prop_t prop);

extern nvlist_t *zfs_get_all_props(zfs_handle_t *);

extern nvlist_t *zfs_get_user_props(zfs_handle_t *);

extern nvlist_t *zfs_get_recvd_props(zfs_handle_t *);

extern nvlist_t *zfs_get_clones_nvl(zfs_handle_t *);

/*
 * zfs encryption management
 */
extern int zfs_crypto_get_encryption_root(zfs_handle_t *, boolean_t *, char *);

extern int zfs_crypto_create(libzfs_handle_t *, char *, nvlist_t *, nvlist_t *,
                             boolean_t stdin_available, uint8_t **,
                             uint32_t *);

extern int zfs_crypto_clone_check(libzfs_handle_t *, zfs_handle_t *, char *,
                                  nvlist_t *);

extern int zfs_crypto_attempt_load_keys(libzfs_handle_t *, char *);

extern int zfs_crypto_load_key(zfs_handle_t *, boolean_t, char *);

extern int zfs_crypto_unload_key(zfs_handle_t *);

extern int zfs_crypto_rewrap(zfs_handle_t *, nvlist_t *, boolean_t);

typedef struct zprop_list {
    int pl_prop;
    char *pl_user_prop;
    struct zprop_list *pl_next;
    boolean_t pl_all;
    size_t pl_width;
    size_t pl_recvd_width;
    boolean_t pl_fixed;
} zprop_list_t;

extern int zfs_expand_proplist(zfs_handle_t *, zprop_list_t **, boolean_t,
                               boolean_t);

extern void zfs_prune_proplist(zfs_handle_t *, uint8_t *);

typedef int (*zprop_func)(int, void *);

/*
 * zpool property management
 */
extern int zpool_expand_proplist(zpool_handle_t *, zprop_list_t **);

extern int zpool_prop_get_feature(zpool_handle_t *, const char *, char *,
                                  size_t);

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

#define    ZFS_GET_NCOLS    5

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
typedef struct zprop_get_cbdata {
    int cb_sources;
    zfs_get_column_t cb_columns[ZFS_GET_NCOLS];
    int cb_colwidths[ZFS_GET_NCOLS + 1];
    boolean_t cb_scripted;
    boolean_t cb_literal;
    boolean_t cb_first;
    zprop_list_t *cb_proplist;
    zfs_type_t cb_type;
} zprop_get_cbdata_t;

void zprop_print_one_property(const char *, zprop_get_cbdata_t *,
                              const char *, const char *, zprop_source_t,
                              const char *,
                              const char *);

/*
 * Iterator functions.
 */
typedef int (*zfs_iter_f)(zfs_handle_t *, void *);

extern int zfs_iter_root(libzfs_handle_t *, zfs_iter_f, void *);

extern int zfs_iter_children(zfs_handle_t *, zfs_iter_f, void *);

extern int zfs_iter_dependents(zfs_handle_t *, boolean_t, zfs_iter_f, void *);

extern int zfs_iter_filesystems(zfs_handle_t *, zfs_iter_f, void *);

extern int zfs_iter_snapshots(zfs_handle_t *, boolean_t, zfs_iter_f, void *,
                              uint64_t, uint64_t);

extern int zfs_iter_snapshots_sorted(zfs_handle_t *, zfs_iter_f, void *,
                                     uint64_t, uint64_t);

extern int zfs_iter_snapspec(zfs_handle_t *, const char *, zfs_iter_f, void *);

extern int zfs_iter_bookmarks(zfs_handle_t *, zfs_iter_f, void *);

extern int zfs_iter_mounted(zfs_handle_t *, zfs_iter_f, void *);

typedef struct get_all_cb {
    zfs_handle_t **cb_handles;
    size_t cb_alloc;
    size_t cb_used;
} get_all_cb_t;

void zfs_foreach_mountpoint(libzfs_handle_t *, zfs_handle_t **, size_t,
                            zfs_iter_f, void *, boolean_t);

void libzfs_add_handle(get_all_cb_t *, zfs_handle_t *);

/*
 * Functions to create and destroy datasets.
 */
extern int zfs_create(libzfs_handle_t *, const char *, zfs_type_t,
                      nvlist_t *);

extern int zfs_create_ancestors(libzfs_handle_t *, const char *);

extern int zfs_destroy(zfs_handle_t *, boolean_t);

extern int zfs_destroy_snaps(zfs_handle_t *, char *, boolean_t);

extern int zfs_destroy_snaps_nvl(libzfs_handle_t *, nvlist_t *, boolean_t);

extern int zfs_clone(zfs_handle_t *, const char *, nvlist_t *);

extern int zfs_snapshot(libzfs_handle_t *, const char *, boolean_t,
nvlist_t *);

extern int zfs_snapshot_nvl(libzfs_handle_t *hdl, nvlist_t *snaps,
                            nvlist_t *props);

extern int zfs_rollback(zfs_handle_t *, zfs_handle_t *, boolean_t);

extern int zfs_rename(zfs_handle_t *, const char *, boolean_t, boolean_t);

typedef struct sendflags {
    /* print informational messages (ie, -v was specified) */
    boolean_t verbose;

    /* recursive send  (ie, -R) */
    boolean_t replicate;

    /* for incrementals, do all intermediate snapshots */
    boolean_t doall;

    /* if dataset is a clone, do incremental from its origin */
    boolean_t fromorigin;

    /* do deduplication */
    boolean_t dedup;

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
} sendflags_t;

typedef boolean_t (snapfilter_cb_t)(zfs_handle_t *, void *);

extern int zfs_send(zfs_handle_t *, const char *, const char *,
                    sendflags_t *, int, snapfilter_cb_t, void *, nvlist_t **);

extern int zfs_send_one(zfs_handle_t *, const char *, int, sendflags_t flags);

extern int zfs_send_resume(libzfs_handle_t *, sendflags_t *, int outfd,
                           const char *);

extern nvlist_t *zfs_send_resume_token_to_nvlist(libzfs_handle_t *hdl,
                                                 const char *token);

extern int zfs_promote(zfs_handle_t *);

extern int zfs_hold(zfs_handle_t *, const char *, const char *,
                    boolean_t, int);

extern int zfs_hold_nvl(zfs_handle_t *, int, nvlist_t *);

extern int zfs_release(zfs_handle_t *, const char *, const char *, boolean_t);

extern int zfs_get_holds(zfs_handle_t *, nvlist_t **);

extern uint64_t zvol_volsize_to_reservation(uint64_t, nvlist_t *);

typedef int (*zfs_userspace_cb_t)(void *arg, const char *domain,
                                  uint64_t rid, uint64_t space);

extern int zfs_userspace(zfs_handle_t *, zfs_userquota_prop_t,
                         zfs_userspace_cb_t, void *);

extern int zfs_get_fsacl(zfs_handle_t *, nvlist_t **);

extern int zfs_set_fsacl(zfs_handle_t *, boolean_t, nvlist_t *);

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
} recvflags_t;

extern int zfs_receive(libzfs_handle_t *, const char *, nvlist_t *,
                       recvflags_t *, int, avl_tree_t *);

typedef enum diff_flags {
    ZFS_DIFF_PARSEABLE = 0x1,
    ZFS_DIFF_TIMESTAMP = 0x2,
    ZFS_DIFF_CLASSIFY = 0x4
} diff_flags_t;

extern int zfs_show_diffs(zfs_handle_t *, int, const char *, const char *,
                          int);

/*
 * Miscellaneous functions.
 */
extern const char *zfs_type_to_name(zfs_type_t);

extern void zfs_refresh_properties(zfs_handle_t *);

extern int zfs_name_valid(const char *, zfs_type_t);

extern zfs_handle_t *zfs_path_to_zhandle(libzfs_handle_t *, char *,
                                        zfs_type_t);

extern int zfs_parent_name(zfs_handle_t *, char *, size_t);

extern boolean_t zfs_dataset_exists(libzfs_handle_t *, const char *,
                                    zfs_type_t);

extern int zfs_spa_version(zfs_handle_t *, int *);

extern boolean_t zfs_bookmark_exists(const char *path);

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

typedef enum zfs_share_op {
    ZFS_SHARE_NFS = 0,
    ZFS_UNSHARE_NFS = 1,
    ZFS_SHARE_SMB = 2,
    ZFS_UNSHARE_SMB = 3
} zfs_share_op_t;

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

extern int zfs_unshareall_bytype(zfs_handle_t *, const char *, const char *);

extern int zfs_unshareall(zfs_handle_t *);

extern int zfs_deleg_share_nfs(libzfs_handle_t *, char *, char *, char *,
                               void *, void *, int, zfs_share_op_t);

extern int zfs_nicestrtonum(libzfs_handle_t *, const char *, uint64_t *);

/*
 * Utility functions to run an external process.
 */
#define    STDOUT_VERBOSE    0x01
#define    STDERR_VERBOSE    0x02
#define    NO_DEFAULT_PATH    0x04 /* Don't use $PATH to lookup the command */

int libzfs_run_process(const char *, char **, int flags);

int libzfs_run_process_get_stdout(const char *path, char *argv[], char *env[],
                                  char **lines[], int *lines_cnt);

int libzfs_run_process_get_stdout_nopath(const char *path, char *argv[],
                                         char *env[], char **lines[],
                                         int *lines_cnt);

void libzfs_free_str_array(char **strs, int count);

int libzfs_envvar_is_set(char *envvar);

/*
 * Utility functions for zfs version
 */
extern void zfs_version_userland(char *, int);

extern int zfs_version_kernel(char *, int);

extern int zfs_version_print(void);

/*
 * Given a device or file, determine if it is part of a pool.
 */
extern int zpool_in_use(libzfs_handle_t *, int, pool_state_t *, char **,
                        boolean_t *);

/*
 * Label manipulation.
 */
extern int zpool_clear_label(int);

/*
 * Management interfaces for SMB ACL files
 */

int zfs_smb_acl_add(libzfs_handle_t *, char *, char *, char *);

int zfs_smb_acl_remove(libzfs_handle_t *, char *, char *, char *);

int zfs_smb_acl_purge(libzfs_handle_t *, char *, char *);

int zfs_smb_acl_rename(libzfs_handle_t *, char *, char *, char *, char *);

/*
 * Enable and disable datasets within a pool by mounting/unmounting and
 * sharing/unsharing them.
 */
extern int zpool_enable_datasets(zpool_handle_t *, const char *, int);

extern int zpool_disable_datasets(zpool_handle_t *, boolean_t);

extern int zfs_remap_indirects(libzfs_handle_t *hdl, const char *);


"""

CDEF = _added_src + _libzfs_src
