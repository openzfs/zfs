# Copyright 2015 ClusterHQ. See LICENSE file for details.

"""
Python bindings for ``libzfs_core``.
"""

CDEF = """
    enum lzc_send_flags {
        LZC_SEND_FLAG_EMBED_DATA =  1,
        LZC_SEND_FLAG_LARGE_BLOCK = 2
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
        uint64_t   zc_word[4];
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

    int libzfs_core_init(void);
    void libzfs_core_fini(void);

    int lzc_snapshot(nvlist_t *, nvlist_t *, nvlist_t **);
    int lzc_create(const char *, dmu_objset_type_t, nvlist_t *);
    int lzc_clone(const char *, const char *, nvlist_t *);
    int lzc_destroy_snaps(nvlist_t *, boolean_t, nvlist_t **);
    int lzc_bookmark(nvlist_t *, nvlist_t **);
    int lzc_get_bookmarks(const char *, nvlist_t *, nvlist_t **);
    int lzc_destroy_bookmarks(nvlist_t *, nvlist_t **);

    int lzc_snaprange_space(const char *, const char *, uint64_t *);

    int lzc_hold(nvlist_t *, int, nvlist_t **);
    int lzc_release(nvlist_t *, nvlist_t **);
    int lzc_get_holds(const char *, nvlist_t **);

    int lzc_send(const char *, const char *, int, enum lzc_send_flags);
    int lzc_send_space(const char *, const char *, enum lzc_send_flags, uint64_t *);
    int lzc_receive(const char *, nvlist_t *, const char *, boolean_t, int);
    int lzc_receive_with_header(const char *, nvlist_t *, const char *, boolean_t,
        boolean_t, int, const struct dmu_replay_record *);

    boolean_t lzc_exists(const char *);

    int lzc_rollback(const char *, char *, int);
    int lzc_rollback_to(const char *, const char *);

    int lzc_promote(const char *, nvlist_t *, nvlist_t **);
    int lzc_rename(const char *, const char *, nvlist_t *, char **);
    int lzc_destroy_one(const char *fsname, nvlist_t *);
    int lzc_inherit(const char *fsname, const char *name, nvlist_t *);
    int lzc_set_props(const char *, nvlist_t *, nvlist_t *, nvlist_t *);
    int lzc_list (const char *, nvlist_t *);
"""

SOURCE = """
#include <libzfs/libzfs_core.h>
"""

LIBRARY = "zfs_core"

# vim: softtabstop=4 tabstop=4 expandtab shiftwidth=4
