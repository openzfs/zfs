#ifndef _SPL_VNODE_H
#define _SPL_VNODE_H

#define XVA_MAPSIZE     3
#define XVA_MAGIC       0x78766174

typedef struct vnode {
        uint64_t        v_size;
        int             v_fd;
        mode_t          v_mode;
        char            *v_path;
} vnode_t;


typedef struct xoptattr {
        timestruc_t     xoa_createtime; /* Create time of file */
        uint8_t         xoa_archive;
        uint8_t         xoa_system;
        uint8_t         xoa_readonly;
        uint8_t         xoa_hidden;
        uint8_t         xoa_nounlink;
        uint8_t         xoa_immutable;
        uint8_t         xoa_appendonly;
        uint8_t         xoa_nodump;
        uint8_t         xoa_settable;
        uint8_t         xoa_opaque;
        uint8_t         xoa_av_quarantined;
        uint8_t         xoa_av_modified;
} xoptattr_t;

typedef struct vattr {
        uint_t          va_mask;        /* bit-mask of attributes */
        u_offset_t      va_size;        /* file size in bytes */
} vattr_t;


typedef struct xvattr {
        vattr_t         xva_vattr;      /* Embedded vattr structure */
        uint32_t        xva_magic;      /* Magic Number */
        uint32_t        xva_mapsize;    /* Size of attr bitmap (32-bit words) */
        uint32_t        *xva_rtnattrmapp;       /* Ptr to xva_rtnattrmap[] */
        uint32_t        xva_reqattrmap[XVA_MAPSIZE];    /* Requested attrs */
        uint32_t        xva_rtnattrmap[XVA_MAPSIZE];    /* Returned attrs */
        xoptattr_t      xva_xoptattrs;  /* Optional attributes */
} xvattr_t;

typedef struct vsecattr {
        uint_t          vsa_mask;       /* See below */
        int             vsa_aclcnt;     /* ACL entry count */
        void            *vsa_aclentp;   /* pointer to ACL entries */
        int             vsa_dfaclcnt;   /* default ACL entry count */
        void            *vsa_dfaclentp; /* pointer to default ACL entries */
        size_t          vsa_aclentsz;   /* ACE size in bytes of vsa_aclentp */
} vsecattr_t;

#define AT_TYPE         0x00001
#define AT_MODE         0x00002
// #define AT_UID          0x00004	/* Conflicts with linux/auxvec.h */
// #define AT_GID          0x00008	/* Conflicts with linux/auxvec.h */
#define AT_FSID         0x00010
#define AT_NODEID       0x00020
#define AT_NLINK        0x00040
#define AT_SIZE         0x00080
#define AT_ATIME        0x00100
#define AT_MTIME        0x00200
#define AT_CTIME        0x00400
#define AT_RDEV         0x00800
#define AT_BLKSIZE      0x01000
#define AT_NBLOCKS      0x02000
#define AT_SEQ          0x08000
#define AT_XVATTR       0x10000

#define CRCREAT         0

#define VOP_CLOSE(vp, f, c, o, cr, ct)  0
#define VOP_PUTPAGE(vp, of, sz, fl, cr, ct)     0
#define VOP_GETATTR(vp, vap, fl, cr, ct)  ((vap)->va_size = (vp)->v_size, 0)

#define VOP_FSYNC(vp, f, cr, ct)        fsync((vp)->v_fd)

#define VN_RELE(vp)     vn_close(vp)

extern int vn_open(char *path, int x1, int oflags, int mode, vnode_t **vpp,
    int x2, int x3);
extern int vn_openat(char *path, int x1, int oflags, int mode, vnode_t **vpp,
    int x2, int x3, vnode_t *vp, int fd);
extern int vn_rdwr(int uio, vnode_t *vp, void *addr, ssize_t len,
    offset_t offset, int x1, int x2, rlim64_t x3, void *x4, ssize_t *residp);
extern void vn_close(vnode_t *vp);

#define vn_remove(path, x1, x2)         remove(path)
#define vn_rename(from, to, seg)        rename((from), (to))
#define vn_is_readonly(vp)              B_FALSE

extern vnode_t *rootdir;

#endif /* SPL_VNODE_H */
