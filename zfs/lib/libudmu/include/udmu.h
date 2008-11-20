/* -*- mode: c; c-basic-offset: 8; indent-tabs-mode: nil; -*-
 * vim:expandtab:shiftwidth=8:tabstop=8:
 *
 *  Copyright (c) 2006 Cluster File Systems, Inc.
 *   Author: Alex Tomas <alex@clusterfs.com>
 *   Author: Atul Vidwansa <atul.vidwansa@sun.com>
 *   Author: Manoj Joseph <manoj.joseph@sun.com>
 *
 *   This file is part of the Lustre file system, http://www.lustre.org
 *   Lustre is a trademark of Cluster File Systems, Inc.
 *
 *   You may have signed or agreed to another license before downloading
 *   this software.  If so, you are bound by the terms and conditions
 *   of that agreement, and the following does not apply to you.  See the
 *   LICENSE file included with this distribution for more information.
 *
 *   If you did not agree to a different license, then this copy of Lustre
 *   is open source software; you can redistribute it and/or modify it
 *   under the terms of version 2 of the GNU General Public License as
 *   published by the Free Software Foundation.
 *
 *   In either case, Lustre is distributed in the hope that it will be
 *   useful, but WITHOUT ANY WARRANTY; without even the implied warranty
 *   of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   license text for more details.
 */

#ifndef _DMU_H
#define _DMU_H

#ifdef  __cplusplus
extern "C" {
#endif

#include <stdint.h>

#define LUSTRE_ZPL_VERSION 1ULL

#ifndef AT_TYPE
#define AT_TYPE    0x0001
#define AT_MODE    0x0002
#define AT_UID     0x0004
#define AT_GID     0x0008
#define AT_FSID    0x0010
#define AT_NODEID  0x0020
#define AT_NLINK   0x0040
#define AT_SIZE    0x0080
#define AT_ATIME   0x0100
#define AT_MTIME   0x0200
#define AT_CTIME   0x0400
#define AT_RDEV    0x0800
#define AT_BLKSIZE 0x1000
#define AT_NBLOCKS 0x2000
#define AT_SEQ     0x8000
#endif

#define ACCESSED                (AT_ATIME)
#define STATE_CHANGED           (AT_CTIME)
#define CONTENT_MODIFIED        (AT_MTIME | AT_CTIME)

#define LOOKUP_DIR              0x01    /* want parent dir vp */
#define LOOKUP_XATTR            0x02    /* lookup up extended attr dir */
#define CREATE_XATTR_DIR        0x04    /* Create extended attr dir */

#define S_IFDOOR        0xD000  /* door */
#define S_IFPORT        0xE000  /* event port */

struct statvfs64;

/* Data structures required for Solaris ZFS compatability */
#if !defined(__sun__)

#ifndef _SOL_SYS_TIME_H
typedef struct timespec timestruc_t;
#endif

#endif

typedef enum vtype {
        VNON    = 0,
        VREG    = 1,
        VDIR    = 2,
        VBLK    = 3,
        VCHR    = 4,
        VLNK    = 5,
        VFIFO   = 6,
        VDOOR   = 7,
        VPROC   = 8,
        VSOCK   = 9,
        VPORT   = 10,
        VBAD    = 11
} vtype_t;

typedef struct vnattr {
        unsigned int    va_mask;        /* bit-mask of attributes */
        vtype_t         va_type;        /* vnode type (for create) */
        mode_t          va_mode;        /* file access mode */
        uid_t           va_uid;         /* owner user id */
        gid_t           va_gid;         /* owner group id */
        dev_t           va_fsid;        /* file system id (dev for now) */
        unsigned long long va_nodeid;   /* node id */
        nlink_t         va_nlink;       /* number of references to file */
        off_t           va_size;        /* file size in bytes */
        timestruc_t     va_atime;       /* time of last access */
        timestruc_t     va_mtime;       /* time of last modification */
        timestruc_t     va_ctime;       /* time of last status change */
        dev_t           va_rdev;        /* device the file represents */
        unsigned int    va_blksize;     /* fundamental block size */
        unsigned int    va_blkbits;
        unsigned long long va_nblocks;  /* # of blocks allocated */
        unsigned int    va_seq;         /* sequence number */
} vnattr_t;

typedef struct udmu_objset {
        struct objset *os;
        struct zilog *zilog;
        uint64_t root;  /* id of root znode */
        uint64_t unlinkedobj;
} udmu_objset_t;


/* definitions from dmu.h */
#ifndef _SYS_DMU_H

typedef struct objset objset_t;
typedef struct dmu_tx dmu_tx_t;
typedef struct dmu_buf dmu_buf_t;

#define DMU_NEW_OBJECT  (-1ULL)
#define DMU_OBJECT_END  (-1ULL)

#endif

#ifndef _SYS_TXG_H
#define TXG_WAIT        1ULL
#define TXG_NOWAIT      2ULL
#endif

#define ZFS_DIRENT_MAKE(type, obj) (((uint64_t)type << 60) | obj)

#define FTAG ((char *)__func__)

void udmu_init();

void udmu_fini();

void udmu_debug(int level);

/* udmu object-set API */

int udmu_objset_open(char *osname, char *import_dir, int import, int force, udmu_objset_t *uos);

void udmu_objset_close(udmu_objset_t *uos, int export_pool);

int udmu_objset_statvfs(udmu_objset_t *uos, struct statvfs64 *statp);

int udmu_objset_root(udmu_objset_t *uos, dmu_buf_t **dbp, void *tag);

void udmu_wait_synced(udmu_objset_t *uos, dmu_tx_t *tx);

/* udmu ZAP API */

int udmu_zap_lookup(udmu_objset_t *uos, dmu_buf_t *zap_db, const char *name,
                    void *value, int value_size, int intsize);

void udmu_zap_create(udmu_objset_t *uos, dmu_buf_t **zap_dbp, dmu_tx_t *tx, void *tag);

int udmu_zap_insert(udmu_objset_t *uos, dmu_buf_t *zap_db, dmu_tx_t *tx,
                    const char *name, void *value, int len);

int udmu_zap_delete(udmu_objset_t *uos, dmu_buf_t *zap_db, dmu_tx_t *tx,
                    const char *name);

/* udmu object API */

void udmu_object_create(udmu_objset_t *uos, dmu_buf_t **dbp, dmu_tx_t *tx, void *tag);

int udmu_object_get_dmu_buf(udmu_objset_t *uos, uint64_t object,
                            dmu_buf_t **dbp, void *tag);

void udmu_object_put_dmu_buf(dmu_buf_t *db, void *tag);

uint64_t udmu_object_get_id(dmu_buf_t *db);

int udmu_object_read(udmu_objset_t *uos, dmu_buf_t *db, uint64_t offset,
                     uint64_t size, void *buf);

void udmu_object_write(udmu_objset_t *uos, dmu_buf_t *db, struct dmu_tx *tx,
                      uint64_t offset, uint64_t size, void *buf);

void udmu_object_getattr(dmu_buf_t *db, vnattr_t *vap);

void udmu_object_setattr(dmu_buf_t *db, dmu_tx_t *tx, vnattr_t *vap);

void udmu_object_punch(udmu_objset_t *uos, dmu_buf_t *db, dmu_tx_t *tx,
                      uint64_t offset, uint64_t len);

int udmu_object_delete(udmu_objset_t *uos, dmu_buf_t **db, dmu_tx_t *tx, void *tag);

/*udmu transaction API */

dmu_tx_t *udmu_tx_create(udmu_objset_t *uos);

void udmu_tx_hold_write(dmu_tx_t *tx, uint64_t object, uint64_t off, int len);

void udmu_tx_hold_free(dmu_tx_t *tx, uint64_t object, uint64_t off,
    uint64_t len);

void udmu_tx_hold_zap(dmu_tx_t *tx, uint64_t object, int add, char *name);

void udmu_tx_hold_bonus(dmu_tx_t *tx, uint64_t object);

void udmu_tx_abort(dmu_tx_t *tx);

int udmu_tx_assign(dmu_tx_t *tx, uint64_t txg_how);

void udmu_tx_wait(dmu_tx_t *tx);

int udmu_indblk_overhead(dmu_buf_t *db, unsigned long *used,
                         unsigned long *overhead);

void udmu_tx_commit(dmu_tx_t *tx);

void * udmu_tx_cb_create(size_t bytes);

int udmu_tx_cb_add(dmu_tx_t *tx, void *func, void *data);

int udmu_tx_cb_destroy(void *data);

int udmu_object_is_zap(dmu_buf_t *);

int udmu_indblk_overhead(dmu_buf_t *db, unsigned long *used, unsigned 
                                long *overhead);

int udmu_get_blocksize(dmu_buf_t *db, long *blksz);

#ifdef  __cplusplus
}
#endif

#endif /* _DMU_H */
