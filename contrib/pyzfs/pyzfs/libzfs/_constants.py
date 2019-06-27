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

"""
Important `libzfs` constants.
"""

from __future__ import absolute_import, division, print_function

from enum import Enum


# Todo: Create docstrings for the enums below

# https://stackoverflow.com/a/1695250
class ZPOOL_PROP(Enum):
    ZPOOL_PROP_INVAL = -1
    ZPOOL_PROP_NAME = 0
    ZPOOL_PROP_SIZE = 1
    ZPOOL_PROP_CAPACITY = 2
    ZPOOL_PROP_ALTROOT = 3
    ZPOOL_PROP_HEALTH = 4
    ZPOOL_PROP_GUID = 5
    ZPOOL_PROP_VERSION = 6
    ZPOOL_PROP_BOOTFS = 7
    ZPOOL_PROP_DELEGATION = 8
    ZPOOL_PROP_AUTOREPLACE = 9
    ZPOOL_PROP_CACHEFILE = 10
    ZPOOL_PROP_FAILUREMODE = 11
    ZPOOL_PROP_LISTSNAPS = 12
    ZPOOL_PROP_AUTOEXPAND = 13
    ZPOOL_PROP_DEDUPDITTO = 14
    ZPOOL_PROP_DEDUPRATIO = 15
    ZPOOL_PROP_FREE = 16
    ZPOOL_PROP_ALLOCATED = 17
    ZPOOL_PROP_READONLY = 18
    ZPOOL_PROP_ASHIFT = 19
    ZPOOL_PROP_COMMENT = 20
    ZPOOL_PROP_EXPANDSZ = 21
    ZPOOL_PROP_FREEING = 22
    ZPOOL_PROP_FRAGMENTATION = 23
    ZPOOL_PROP_LEAKED = 24
    ZPOOL_PROP_MAXBLOCKSIZE = 25
    ZPOOL_PROP_TNAME = 26
    ZPOOL_PROP_MAXDNODESIZE = 27
    ZPOOL_PROP_MULTIHOST = 28
    ZPOOL_PROP_CHECKPOINT = 29
    ZPOOL_PROP_LOAD_GUID = 30
    ZPOOL_PROP_AUTOTRIM = 31
    ZPOOL_NUM_PROPS = 32


class ZPOOL_STATUS(Enum):
    """
    From ``libzfs.h``
    """
    """
    /*
     * Pool health statistics.
     */
    typedef enum {
        /*
         * The following correspond to faults as defined in the
         * (fault.fs.zfs.*) event namespace.
         * Each is associated with a corresponding message ID.
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

        /*
         * Finally, the following indicates a healthy pool.
         */
        ZPOOL_STATUS_OK
    } zpool_status_t;
    """

    ZPOOL_STATUS_CORRUPT_CACHE = 0
    ZPOOL_STATUS_MISSING_DEV_R = 1
    ZPOOL_STATUS_MISSING_DEV_NR = 2
    ZPOOL_STATUS_CORRUPT_LABEL_R = 3
    ZPOOL_STATUS_CORRUPT_LABEL_NR = 4
    ZPOOL_STATUS_BAD_GUID_SUM = 5
    ZPOOL_STATUS_CORRUPT_POOL = 6
    ZPOOL_STATUS_CORRUPT_DATA = 7
    ZPOOL_STATUS_FAILING_DEV = 8
    ZPOOL_STATUS_VERSION_NEWER = 9
    ZPOOL_STATUS_HOSTID_MISMATCH = 10
    ZPOOL_STATUS_HOSTID_ACTIVE = 11
    ZPOOL_STATUS_HOSTID_REQUIRED = 12
    ZPOOL_STATUS_IO_FAILURE_WAIT = 13
    ZPOOL_STATUS_IO_FAILURE_CONTINUE = 14
    ZPOOL_STATUS_IO_FAILURE_MMP = 15
    ZPOOL_STATUS_BAD_LOG = 16
    ZPOOL_STATUS_ERRATA = 17
    ZPOOL_STATUS_UNSUP_FEAT_READ = 18
    ZPOOL_STATUS_UNSUP_FEAT_WRITE = 19
    ZPOOL_STATUS_FAULTED_DEV_R = 20
    ZPOOL_STATUS_FAULTED_DEV_NR = 21
    ZPOOL_STATUS_VERSION_OLDER = 22
    ZPOOL_STATUS_FEAT_DISABLED = 23
    ZPOOL_STATUS_RESILVERING = 24
    ZPOOL_STATUS_OFFLINE_DEV = 25
    ZPOOL_STATUS_REMOVED_DEV = 26
    ZPOOL_STATUS_OK = 27


class ZPOOL_ERRATA(Enum):
    """
    From ``zfs.h``
    Errata described by http://zfsonlinux.org/msg/ZFS-8000-ER.  The ordering
    of this enum must be maintained to ensure the errata identifiers map to
    the correct documentation.  New errata may only be appended to the list
    and must contain corresponding documentation at the above link.
    """

    ZPOOL_ERRATA_NONE = 0
    ZPOOL_ERRATA_ZOL_2094_SCRUB = 1
    ZPOOL_ERRATA_ZOL_2094_ASYNC_DESTROY = 2
    ZPOOL_ERRATA_ZOL_6845_ENCRYPTION = 3
    ZPOOL_ERRATA_ZOL_8308_ENCRYPTION = 4


class ZPROP_SOURCE(Enum):
    ZPROP_SRC_NONE = (0x1,)
    ZPROP_SRC_DEFAULT = (0x2,)
    ZPROP_SRC_TEMPORARY = (0x4,)
    ZPROP_SRC_LOCAL = (0x8,)
    ZPROP_SRC_INHERITED = (0x10,)
    ZPROP_SRC_RECEIVED = 0x20


class ZFS_TYPE(Enum):
    ZFS_TYPE_FILESYSTEM = 1 << 0
    ZFS_TYPE_SNAPSHOT = 1 << 1
    ZFS_TYPE_VOLUME = 1 << 2
    ZFS_TYPE_POOL = 1 << 3
    ZFS_TYPE_BOOKMARK = 1 << 4


class ZFS_PROP(Enum):
    ZPROP_CONT = -2
    ZPROP_INVAL = -1
    ZFS_PROP_TYPE = 0
    ZFS_PROP_CREATION = 1
    ZFS_PROP_USED = 2
    ZFS_PROP_AVAILABLE = 3
    ZFS_PROP_REFERENCED = 4
    ZFS_PROP_COMPRESSRATIO = 5
    ZFS_PROP_MOUNTED = 6
    ZFS_PROP_ORIGIN = 7
    ZFS_PROP_QUOTA = 8
    ZFS_PROP_RESERVATION = 9
    ZFS_PROP_VOLSIZE = 10
    ZFS_PROP_VOLBLOCKSIZE = 11
    ZFS_PROP_RECORDSIZE = 12
    ZFS_PROP_MOUNTPOINT = 13
    ZFS_PROP_SHARENFS = 14
    ZFS_PROP_CHECKSUM = 15
    ZFS_PROP_COMPRESSION = 16
    ZFS_PROP_ATIME = 17
    ZFS_PROP_DEVICES = 18
    ZFS_PROP_EXEC = 19
    ZFS_PROP_SETUID = 20
    ZFS_PROP_READONLY = 21
    ZFS_PROP_ZONED = 22
    ZFS_PROP_SNAPDIR = 23
    ZFS_PROP_PRIVATE = 24
    ZFS_PROP_ACLINHERIT = 25
    ZFS_PROP_CREATETXG = 26
    ZFS_PROP_NAME = 27
    ZFS_PROP_CANMOUNT = 28
    ZFS_PROP_ISCSIOPTIONS = 29
    ZFS_PROP_XATTR = 30
    ZFS_PROP_NUMCLONES = 31
    ZFS_PROP_COPIES = 32
    ZFS_PROP_VERSION = 33
    ZFS_PROP_UTF8ONLY = 34
    ZFS_PROP_NORMALIZE = 35
    ZFS_PROP_CASE = 36
    ZFS_PROP_VSCAN = 37
    ZFS_PROP_NBMAND = 38
    ZFS_PROP_SHARESMB = 39
    ZFS_PROP_REFQUOTA = 40
    ZFS_PROP_REFRESERVATION = 41
    ZFS_PROP_GUID = 42
    ZFS_PROP_PRIMARYCACHE = 43
    ZFS_PROP_SECONDARYCACHE = 44
    ZFS_PROP_USEDSNAP = 45
    ZFS_PROP_USEDDS = 46
    ZFS_PROP_USEDCHILD = 47
    ZFS_PROP_USEDREFRESERV = 48
    ZFS_PROP_USERACCOUNTING = 49
    ZFS_PROP_STMF_SHAREINFO = 50
    ZFS_PROP_DEFER_DESTROY = 51
    ZFS_PROP_USERREFS = 52
    ZFS_PROP_LOGBIAS = 53
    ZFS_PROP_UNIQUE = 54
    ZFS_PROP_OBJSETID = 55
    ZFS_PROP_DEDUP = 56
    ZFS_PROP_MLSLABEL = 57
    ZFS_PROP_SYNC = 58
    ZFS_PROP_DNODESIZE = 59
    ZFS_PROP_REFRATIO = 60
    ZFS_PROP_WRITTEN = 61
    ZFS_PROP_CLONES = 62
    ZFS_PROP_LOGICALUSED = 63
    ZFS_PROP_LOGICALREFERENCED = 64
    ZFS_PROP_INCONSISTENT = 65
    ZFS_PROP_VOLMODE = 66
    ZFS_PROP_FILESYSTEM_LIMIT = 67
    ZFS_PROP_SNAPSHOT_LIMIT = 68
    ZFS_PROP_FILESYSTEM_COUNT = 69
    ZFS_PROP_SNAPSHOT_COUNT = 70
    ZFS_PROP_SNAPDEV = 71
    ZFS_PROP_ACLTYPE = 72
    ZFS_PROP_SELINUX_CONTEXT = 73
    ZFS_PROP_SELINUX_FSCONTEXT = 74
    ZFS_PROP_SELINUX_DEFCONTEXT = 75
    ZFS_PROP_SELINUX_ROOTCONTEXT = 76
    ZFS_PROP_RELATIME = 77
    ZFS_PROP_REDUNDANT_METADATA = 78
    ZFS_PROP_OVERLAY = 79
    ZFS_PROP_PREV_SNAP = 80
    ZFS_PROP_RECEIVE_RESUME_TOKEN = 81
    ZFS_PROP_ENCRYPTION = 82
    ZFS_PROP_KEYLOCATION = 83
    ZFS_PROP_KEYFORMAT = 84
    ZFS_PROP_PBKDF2_SALT = 85
    ZFS_PROP_PBKDF2_ITERS = 86
    ZFS_PROP_ENCRYPTION_ROOT = 87
    ZFS_PROP_KEY_GUID = 88
    ZFS_PROP_KEYSTATUS = 89
    ZFS_PROP_REMAPTXG = 90
    ZFS_PROP_SPECIAL_SMALL_BLOCKS = 91
    ZFS_PROP_IVSET_GUID = 92
    ZFS_NUM_PROPS = 93


class ZFS_USERSPACE_PROP(Enum):
    ZFS_PROP_USERUSED = 0
    ZFS_PROP_USERQUOTA = 1
    ZFS_PROP_GROUPUSED = 2
    ZFS_PROP_GROUPQUOTA = 3
    ZFS_PROP_USEROBJUSED = 4
    ZFS_PROP_USEROBJQUOTA = 5
    ZFS_PROP_GROUPOBJUSED = 6
    ZFS_PROP_GROUPOBJQUOTA = 7
    ZFS_PROP_PROJECTUSED = 8
    ZFS_PROP_PROJECTQUOTA = 9
    ZFS_PROP_PROJECTOBJUSED = 10
    ZFS_PROP_PROJECTOBJQUOTA = 11
    ZFS_NUM_USERQUOTA_PROPS = 12


class ZFS_USERSPACE_PROP_PREFIX(Enum):
    USER_USED = b"userused@"
    USER_QUOTA = b"userquota@"
    GROUP_USED = b"groupused@"
    GROUP_QUOTA = b"groupquota@"
    USER_OBJECT_USED = b"userobjused@"
    USER_OBJECT_QUOTA = b"userobjquota@"
    GROUP_OBJECT_USED = b"groupobjused@"
    GROUP_OBJECT_QUOTA = b"groupobjquota@"
    PROJECT_USED = b"projectused@"
    PROJECT_QUOTA = b"projectquota@"
    PROJECT_OBJECT_USED = b"projectobjused@"
    PROJECT_OBJECT_QUOTA = b"projectobjquota@"


class ZPOOL_CONFIG(Enum):
    ZPOOL_CONFIG_VERSION = b"version"
    ZPOOL_CONFIG_POOL_NAME = b"name"
    ZPOOL_CONFIG_POOL_STATE = b"state"
    ZPOOL_CONFIG_POOL_TXG = b"txg"
    ZPOOL_CONFIG_POOL_GUID = b"pool_guid"
    ZPOOL_CONFIG_CREATE_TXG = b"create_txg"
    ZPOOL_CONFIG_TOP_GUID = b"top_guid"
    ZPOOL_CONFIG_VDEV_TREE = b"vdev_tree"
    ZPOOL_CONFIG_TYPE = b"type"
    ZPOOL_CONFIG_CHILDREN = b"children"
    ZPOOL_CONFIG_ID = b"id"
    ZPOOL_CONFIG_GUID = b"guid"
    ZPOOL_CONFIG_INDIRECT_OBJECT = b"com.delphix:indirect_object"
    ZPOOL_CONFIG_INDIRECT_BIRTHS = b"com.delphix:indirect_births"
    ZPOOL_CONFIG_PREV_INDIRECT_VDEV = b"com.delphix:prev_indirect_vdev"
    ZPOOL_CONFIG_PATH = b"path"
    ZPOOL_CONFIG_DEVID = b"devid"
    ZPOOL_CONFIG_METASLAB_ARRAY = b"metaslab_array"
    ZPOOL_CONFIG_METASLAB_SHIFT = b"metaslab_shift"
    ZPOOL_CONFIG_ASHIFT = b"ashift"
    ZPOOL_CONFIG_ASIZE = b"asize"
    ZPOOL_CONFIG_DTL = b"DTL"
    ZPOOL_CONFIG_SCAN_STATS = b"scan_stats"
    ZPOOL_CONFIG_REMOVAL_STATS = b"removal_stats"
    ZPOOL_CONFIG_CHECKPOINT_STATS = b"checkpoint_stats"
    ZPOOL_CONFIG_VDEV_STATS = b"vdev_stats"
    ZPOOL_CONFIG_INDIRECT_SIZE = b"indirect_size"
    ZPOOL_CONFIG_VDEV_STATS_EX = b"vdev_stats_ex"
    ZPOOL_CONFIG_VDEV_SYNC_R_ACTIVE_QUEUE = b"vdev_sync_r_active_queue"
    ZPOOL_CONFIG_VDEV_SYNC_W_ACTIVE_QUEUE = b"vdev_sync_w_active_queue"
    ZPOOL_CONFIG_VDEV_ASYNC_R_ACTIVE_QUEUE = b"vdev_async_r_active_queue"
    ZPOOL_CONFIG_VDEV_ASYNC_W_ACTIVE_QUEUE = b"vdev_async_w_active_queue"
    ZPOOL_CONFIG_VDEV_SCRUB_ACTIVE_QUEUE = b"vdev_async_scrub_active_queue"
    ZPOOL_CONFIG_VDEV_TRIM_ACTIVE_QUEUE = b"vdev_async_trim_active_queue"
    ZPOOL_CONFIG_VDEV_SYNC_R_PEND_QUEUE = b"vdev_sync_r_pend_queue"
    ZPOOL_CONFIG_VDEV_SYNC_W_PEND_QUEUE = b"vdev_sync_w_pend_queue"
    ZPOOL_CONFIG_VDEV_ASYNC_R_PEND_QUEUE = b"vdev_async_r_pend_queue"
    ZPOOL_CONFIG_VDEV_ASYNC_W_PEND_QUEUE = b"vdev_async_w_pend_queue"
    ZPOOL_CONFIG_VDEV_SCRUB_PEND_QUEUE = b"vdev_async_scrub_pend_queue"
    ZPOOL_CONFIG_VDEV_TRIM_PEND_QUEUE = b"vdev_async_trim_pend_queue"
    ZPOOL_CONFIG_VDEV_TOT_R_LAT_HISTO = b"vdev_tot_r_lat_histo"
    ZPOOL_CONFIG_VDEV_TOT_W_LAT_HISTO = b"vdev_tot_w_lat_histo"
    ZPOOL_CONFIG_VDEV_DISK_R_LAT_HISTO = b"vdev_disk_r_lat_histo"
    ZPOOL_CONFIG_VDEV_DISK_W_LAT_HISTO = b"vdev_disk_w_lat_histo"
    ZPOOL_CONFIG_VDEV_SYNC_R_LAT_HISTO = b"vdev_sync_r_lat_histo"
    ZPOOL_CONFIG_VDEV_SYNC_W_LAT_HISTO = b"vdev_sync_w_lat_histo"
    ZPOOL_CONFIG_VDEV_ASYNC_R_LAT_HISTO = b"vdev_async_r_lat_histo"
    ZPOOL_CONFIG_VDEV_ASYNC_W_LAT_HISTO = b"vdev_async_w_lat_histo"
    ZPOOL_CONFIG_VDEV_SCRUB_LAT_HISTO = b"vdev_scrub_histo"
    ZPOOL_CONFIG_VDEV_TRIM_LAT_HISTO = b"vdev_trim_histo"
    ZPOOL_CONFIG_VDEV_SYNC_IND_R_HISTO = b"vdev_sync_ind_r_histo"
    ZPOOL_CONFIG_VDEV_SYNC_IND_W_HISTO = b"vdev_sync_ind_w_histo"
    ZPOOL_CONFIG_VDEV_ASYNC_IND_R_HISTO = b"vdev_async_ind_r_histo"
    ZPOOL_CONFIG_VDEV_ASYNC_IND_W_HISTO = b"vdev_async_ind_w_histo"
    ZPOOL_CONFIG_VDEV_IND_SCRUB_HISTO = b"vdev_ind_scrub_histo"
    ZPOOL_CONFIG_VDEV_IND_TRIM_HISTO = b"vdev_ind_trim_histo"
    ZPOOL_CONFIG_VDEV_SYNC_AGG_R_HISTO = b"vdev_sync_agg_r_histo"
    ZPOOL_CONFIG_VDEV_SYNC_AGG_W_HISTO = b"vdev_sync_agg_w_histo"
    ZPOOL_CONFIG_VDEV_ASYNC_AGG_R_HISTO = b"vdev_async_agg_r_histo"
    ZPOOL_CONFIG_VDEV_ASYNC_AGG_W_HISTO = b"vdev_async_agg_w_histo"
    ZPOOL_CONFIG_VDEV_AGG_SCRUB_HISTO = b"vdev_agg_scrub_histo"
    ZPOOL_CONFIG_VDEV_AGG_TRIM_HISTO = b"vdev_agg_trim_histo"
    ZPOOL_CONFIG_VDEV_SLOW_IOS = b"vdev_slow_ios"
    ZPOOL_CONFIG_VDEV_ENC_SYSFS_PATH = b"vdev_enc_sysfs_path"
    ZPOOL_CONFIG_WHOLE_DISK = b"whole_disk"
    ZPOOL_CONFIG_ERRCOUNT = b"error_count"
    ZPOOL_CONFIG_NOT_PRESENT = b"not_present"
    ZPOOL_CONFIG_SPARES = b"spares"
    ZPOOL_CONFIG_IS_SPARE = b"is_spare"
    ZPOOL_CONFIG_NPARITY = b"nparity"
    ZPOOL_CONFIG_HOSTID = b"hostid"
    ZPOOL_CONFIG_HOSTNAME = b"hostname"
    ZPOOL_CONFIG_LOADED_TIME = b"initial_load_time"
    ZPOOL_CONFIG_UNSPARE = b"unspare"
    ZPOOL_CONFIG_PHYS_PATH = b"phys_path"
    ZPOOL_CONFIG_IS_LOG = b"is_log"
    ZPOOL_CONFIG_L2CACHE = b"l2cache"
    ZPOOL_CONFIG_HOLE_ARRAY = b"hole_array"
    ZPOOL_CONFIG_VDEV_CHILDREN = b"vdev_children"
    ZPOOL_CONFIG_IS_HOLE = b"is_hole"
    ZPOOL_CONFIG_DDT_HISTOGRAM = b"ddt_histogram"
    ZPOOL_CONFIG_DDT_OBJ_STATS = b"ddt_object_stats"
    ZPOOL_CONFIG_DDT_STATS = b"ddt_stats"
    ZPOOL_CONFIG_SPLIT = b"splitcfg"
    ZPOOL_CONFIG_ORIG_GUID = b"orig_guid"
    ZPOOL_CONFIG_SPLIT_GUID = b"split_guid"
    ZPOOL_CONFIG_SPLIT_LIST = b"guid_list"
    ZPOOL_CONFIG_REMOVING = b"removing"
    ZPOOL_CONFIG_RESILVER_TXG = b"resilver_txg"
    ZPOOL_CONFIG_COMMENT = b"comment"
    ZPOOL_CONFIG_SUSPENDED = b"suspended"
    ZPOOL_CONFIG_SUSPENDED_REASON = b"suspended_reason"
    ZPOOL_CONFIG_TIMESTAMP = b"timestamp"
    ZPOOL_CONFIG_BOOTFS = b"bootfs"
    ZPOOL_CONFIG_MISSING_DEVICES = b"missing_vdevs"
    ZPOOL_CONFIG_LOAD_INFO = b"load_info"
    ZPOOL_CONFIG_REWIND_INFO = b"rewind_info"
    ZPOOL_CONFIG_UNSUP_FEAT = b"unsup_feat"
    ZPOOL_CONFIG_ENABLED_FEAT = b"enabled_feat"
    ZPOOL_CONFIG_CAN_RDONLY = b"can_rdonly"
    ZPOOL_CONFIG_FEATURES_FOR_READ = b"features_for_read"
    ZPOOL_CONFIG_FEATURE_STATS = b"feature_stats"
    ZPOOL_CONFIG_ERRATA = b"errata"
    ZPOOL_CONFIG_VDEV_TOP_ZAP = b"com.delphix:vdev_zap_top"
    ZPOOL_CONFIG_VDEV_LEAF_ZAP = b"com.delphix:vdev_zap_leaf"
    ZPOOL_CONFIG_HAS_PER_VDEV_ZAPS = b"com.delphix:has_per_vdev_zaps"
    ZPOOL_CONFIG_RESILVER_DEFER = b"com.datto:resilver_defer"
    ZPOOL_CONFIG_CACHEFILE = b"cachefile"
    ZPOOL_CONFIG_MMP_STATE = b"mmp_state"
    ZPOOL_CONFIG_MMP_TXG = b"mmp_txg"
    ZPOOL_CONFIG_MMP_SEQ = b"mmp_seq"
    ZPOOL_CONFIG_MMP_HOSTNAME = b"mmp_hostname"
    ZPOOL_CONFIG_MMP_HOSTID = b"mmp_hostid"
    ZPOOL_CONFIG_EXPANSION_TIME = b"expansion_time"
    ZPOOL_CONFIG_OFFLINE = b"offline"
    ZPOOL_CONFIG_FAULTED = b"faulted"
    ZPOOL_CONFIG_DEGRADED = b"degraded"
    ZPOOL_CONFIG_REMOVED = b"removed"
    ZPOOL_CONFIG_FRU = b"fru"
    ZPOOL_CONFIG_AUX_STATE = b"aux_state"


class SCAN_STATE(Enum):
    """
    From ``zfs.h``
    """
    """
    typedef enum dsl_scan_state {
        DSS_NONE,
        DSS_SCANNING,
        DSS_FINISHED,
        DSS_CANCELED,
        DSS_NUM_STATES
    } dsl_scan_state_t;
    """

    NONE = 0
    SCANNING = 1
    FINISHED = 2
    CANCELED = 3
    NUM_STATES = 4


class VDEV_STATE(Enum):
    """
    From ``zfs.h``
    """
    """
    /*
     * vdev states are ordered from least to most healthy.
     * A vdev that's CANT_OPEN or below is considered unusable.
     */
    typedef enum vdev_state {
        VDEV_STATE_UNKNOWN = 0,	/* Uninitialized vdev			*/
        VDEV_STATE_CLOSED,	/* Not currently open			*/
        VDEV_STATE_OFFLINE,	/* Not allowed to open			*/
        VDEV_STATE_REMOVED,	/* Explicitly removed from system	*/
        VDEV_STATE_CANT_OPEN,	/* Tried to open, but failed		*/
        VDEV_STATE_FAULTED,	/* External request to fault device	*/
        VDEV_STATE_DEGRADED,	/* Replicated vdev with unhealthy kids	*/
        VDEV_STATE_HEALTHY	/* Presumed good			*/
    } vdev_state_t;
    """

    VDEV_STATE_UNKNOWN = 0
    VDEV_STATE_CLOSED = 1
    VDEV_STATE_OFFLINE = 2
    VDEV_STATE_REMOVED = 3
    VDEV_STATE_CANT_OPEN = 4
    VDEV_STATE_FAULTED = 5
    VDEV_STATE_DEGRADED = 6
    VDEV_STATE_HEALTHY = 7


class VDEV_AUX_STATE(Enum):
    """
    From ``zfs.h``
    """
    """
    /*
     * vdev aux states.  When a vdev is in the CANT_OPEN state, the aux field
     * of the vdev stats structure uses these constants to distinguish why.
     */
    typedef enum vdev_aux {
        VDEV_AUX_NONE,		/* no error	*/
        VDEV_AUX_OPEN_FAILED,	/* ldi_open_*() or vn_open() failed	*/
        VDEV_AUX_CORRUPT_DATA,	/* bad label or disk contents		*/
        VDEV_AUX_NO_REPLICAS,	/* insufficient number of replicas	*/
        VDEV_AUX_BAD_GUID_SUM,	/* vdev guid sum doesn't match		*/
        VDEV_AUX_TOO_SMALL,	/* vdev size is too small		*/
        VDEV_AUX_BAD_LABEL,	/* the label is OK but invalid		*/
        VDEV_AUX_VERSION_NEWER,	/* on-disk version is too new		*/
        VDEV_AUX_VERSION_OLDER,	/* on-disk version is too old		*/
        VDEV_AUX_UNSUP_FEAT,	/* unsupported features			*/
        VDEV_AUX_SPARED,	/* hot spare used in another pool	*/
        VDEV_AUX_ERR_EXCEEDED,	/* too many errors			*/
        VDEV_AUX_IO_FAILURE,	/* experienced I/O failure		*/
        VDEV_AUX_BAD_LOG,	/* cannot read log chain(s)		*/
        VDEV_AUX_EXTERNAL,	/* external diagnosis or forced fault	*/
        VDEV_AUX_SPLIT_POOL,	/* vdev was split off into another pool	*/
        VDEV_AUX_BAD_ASHIFT,	/* vdev ashift is invalid		*/
        VDEV_AUX_EXTERNAL_PERSIST,	/* persistent forced fault	*/
        VDEV_AUX_ACTIVE,	/* vdev active on a different host	*/
        VDEV_AUX_CHILDREN_OFFLINE, /* all children are offline		*/
    } vdev_aux_t;
    """

    VDEV_AUX_NONE = 0
    VDEV_AUX_OPEN_FAILED = 1
    VDEV_AUX_CORRUPT_DATA = 2
    VDEV_AUX_NO_REPLICAS = 3
    VDEV_AUX_BAD_GUID_SUM = 4
    VDEV_AUX_TOO_SMALL = 5
    VDEV_AUX_BAD_LABEL = 6
    VDEV_AUX_VERSION_NEWER = 7
    VDEV_AUX_VERSION_OLDER = 8
    VDEV_AUX_UNSUP_FEAT = 9
    VDEV_AUX_SPARED = 10
    VDEV_AUX_ERR_EXCEEDED = 11
    VDEV_AUX_IO_FAILURE = 12
    VDEV_AUX_BAD_LOG = 13
    VDEV_AUX_EXTERNAL = 14
    VDEV_AUX_SPLIT_POOL = 15
    VDEV_AUX_BAD_ASHIFT = 16
    VDEV_AUX_EXTERNAL_PERSIST = 17
    VDEV_AUX_ACTIVE = 18
    VDEV_AUX_CHILDREN_OFFLINE = 19

# vim: softtabstop=4 tabstop=4 expandtab shiftwidth=4
