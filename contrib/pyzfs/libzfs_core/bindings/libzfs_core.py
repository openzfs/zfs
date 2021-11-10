#
# Copyright 2015 ClusterHQ
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
Python bindings for ``libzfs_core``.
"""
from __future__ import absolute_import, division, print_function

CDEF = """

    enum lzc_send_flags {
        LZC_SEND_FLAG_EMBED_DATA = 1,
        LZC_SEND_FLAG_LARGE_BLOCK = 2,
        LZC_SEND_FLAG_COMPRESS = 4,
        LZC_SEND_FLAG_RAW = 8
    };

    typedef enum {
        DMU_OST_NONE,
        DMU_OST_META,
        DMU_OST_ZFS,
        DMU_OST_ZVOL,
        DMU_OST_OTHER,
        DMU_OST_ANY,
        DMU_OST_NUMTYPES
    } dmu_objset_type_t;

    #define MAXNAMELEN 256

    struct drr_begin {
        uint64_t drr_magic;
        uint64_t drr_versioninfo; /* was drr_version */
        uint64_t drr_creation_time;
        dmu_objset_type_t drr_type;
        uint32_t drr_flags;
        uint64_t drr_toguid;
        uint64_t drr_fromguid;
        char drr_toname[MAXNAMELEN];
    };

    typedef struct zio_cksum {
        uint64_t zc_word[4];
    } zio_cksum_t;

    typedef struct dmu_replay_record {
        enum {
            DRR_BEGIN, DRR_OBJECT, DRR_FREEOBJECTS,
            DRR_WRITE, DRR_FREE, DRR_END, DRR_WRITE_BYREF,
            DRR_SPILL, DRR_WRITE_EMBEDDED, DRR_NUMTYPES
        } drr_type;
        uint32_t drr_payloadlen;
        union {
            struct drr_begin drr_begin;
            /* ... */
            struct drr_checksum {
                uint64_t drr_pad[34];
                zio_cksum_t drr_checksum;
            } drr_checksum;
        } drr_u;
    } dmu_replay_record_t;

    typedef enum {
        DCP_CMD_NONE,
        DCP_CMD_RAW_RECV,
        DCP_CMD_NEW_KEY,
        DCP_CMD_INHERIT,
        DCP_CMD_FORCE_NEW_KEY,
        DCP_CMD_FORCE_INHERIT
    } dcp_cmd_t;

    int libzfs_core_init(void);
    void libzfs_core_fini(void);

    int lzc_bookmark(nvlist_t *, nvlist_t **);
    int lzc_change_key(const char *, uint64_t, nvlist_t *, uint8_t *, uint_t);
    int lzc_channel_program(const char *, const char *, uint64_t, uint64_t,
        nvlist_t *, nvlist_t **);
    int lzc_channel_program_nosync(const char *, const char *, uint64_t,
        uint64_t, nvlist_t *, nvlist_t **);
    int lzc_clone(const char *, const char *, nvlist_t *);
    int lzc_create(const char *, dmu_objset_type_t, nvlist_t *, uint8_t *,
        uint_t);
    int lzc_destroy_bookmarks(nvlist_t *, nvlist_t **);
    int lzc_destroy_snaps(nvlist_t *, boolean_t, nvlist_t **);
    boolean_t lzc_exists(const char *);
    int lzc_get_bookmarks(const char *, nvlist_t *, nvlist_t **);
    int lzc_get_holds(const char *, nvlist_t **);
    int lzc_hold(nvlist_t *, int, nvlist_t **);
    int lzc_load_key(const char *, boolean_t, uint8_t *, uint_t);
    int lzc_promote(const char *, nvlist_t *, nvlist_t **);
    int lzc_receive(const char *, nvlist_t *, const char *, boolean_t,
        boolean_t, int);
    int lzc_receive_one(const char *, nvlist_t *, const char *, boolean_t,
        boolean_t, boolean_t, int, const dmu_replay_record_t *, int,
        uint64_t *, uint64_t *, uint64_t *, nvlist_t **);
    int lzc_receive_resumable(const char *, nvlist_t *, const char *,
        boolean_t, boolean_t, int);
    int lzc_receive_with_cmdprops(const char *, nvlist_t *, nvlist_t *,
        uint8_t *, uint_t, const char *, boolean_t, boolean_t,
        boolean_t, int, const dmu_replay_record_t *, int, uint64_t *,
        uint64_t *, uint64_t *, nvlist_t **);
    int lzc_receive_with_header(const char *, nvlist_t *, const char *,
        boolean_t, boolean_t, boolean_t, int, const dmu_replay_record_t *);
    int lzc_release(nvlist_t *, nvlist_t **);
    int lzc_reopen(const char *, boolean_t);
    int lzc_rollback(const char *, char *, int);
    int lzc_rollback_to(const char *, const char *);
    int lzc_send(const char *, const char *, int, enum lzc_send_flags);
    int lzc_send_resume(const char *, const char *, int, enum lzc_send_flags,
        uint64_t, uint64_t);
    int lzc_send_space(const char *, const char *, enum lzc_send_flags,
        uint64_t *);
    int lzc_snaprange_space(const char *, const char *, uint64_t *);
    int lzc_snapshot(nvlist_t *, nvlist_t *, nvlist_t **);
    int lzc_sync(const char *, nvlist_t *, nvlist_t **);
    int lzc_unload_key(const char *);
    int lzc_pool_checkpoint(const char *);
    int lzc_pool_checkpoint_discard(const char *);
    int lzc_rename(const char *, const char *);
    int lzc_destroy(const char *fsname);

    int lzc_inherit(const char *fsname, const char *name, nvlist_t *);
    int lzc_set_props(const char *, nvlist_t *, nvlist_t *, nvlist_t *);
    int lzc_list (const char *, nvlist_t *);
"""

SOURCE = """
#include <libzfs/libzfs_core.h>
"""

LIBRARY = "zfs_core"

# vim: softtabstop=4 tabstop=4 expandtab shiftwidth=4
