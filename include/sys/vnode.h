#ifndef _SPL_VNODE_H
#define _SPL_VNODE_H

#include <linux/module.h>
#include <linux/syscalls.h>
#include <linux/fcntl.h>
#include <linux/uaccess.h>
#include <linux/buffer_head.h>
#include <linux/dcache.h>
#include <linux/namei.h>
#include <linux/fs.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/uio.h>

#define XVA_MAPSIZE     3
#define XVA_MAGIC       0x78766174

#define O_DSYNC		040000000

#define FREAD		1
#define FWRITE		2
#define FCREAT		O_CREAT
#define FTRUNC		O_TRUNC
#define FOFFMAX		O_LARGEFILE
#define FSYNC		O_SYNC
#define FDSYNC		O_DSYNC
#define FRSYNC		O_RSYNC
#define FEXCL		O_EXCL
#define FDIRECT		O_DIRECT

#define FNODSYNC	0x10000 /* fsync pseudo flag */
#define FNOFOLLOW	0x20000 /* don't follow symlinks */

#define AT_TYPE		0x00001
#define AT_MODE		0x00002
#undef  AT_UID		/* Conflicts with linux/auxvec.h */
#define AT_UID          0x00004
#undef  AT_GID		/* Conflicts with linux/auxvec.h */
#define AT_GID          0x00008
#define AT_FSID		0x00010
#define AT_NODEID	0x00020
#define AT_NLINK	0x00040
#define AT_SIZE		0x00080
#define AT_ATIME	0x00100
#define AT_MTIME	0x00200
#define AT_CTIME	0x00400
#define AT_RDEV		0x00800
#define AT_BLKSIZE	0x01000
#define AT_NBLOCKS	0x02000
#define AT_SEQ		0x08000
#define AT_XVATTR	0x10000

#define CRCREAT		0x01
#define RMFILE		0x02

#define B_INVAL		0x01
#define B_TRUNC		0x02

typedef enum vtype {
	VNON		= 0,
	VREG		= 1,
	VDIR		= 2,
	VBLK		= 3,
	VCHR		= 4,
	VLNK		= 5,
	VFIFO		= 6,
	VDOOR		= 7,
	VPROC		= 8,
	VSOCK		= 9,
	VPORT		= 10,
	VBAD		= 11
} vtype_t;

typedef struct vnode {
	struct file	*v_fp;
	vtype_t		v_type;
} vnode_t;

typedef struct vattr {
	enum vtype     va_type;      /* vnode type */
	u_int          va_mask;	     /* attribute bit-mask */
	u_short        va_mode;      /* acc mode */
	short          va_uid;       /* owner uid */
	short          va_gid;       /* owner gid */
	long           va_fsid;      /* fs id */
	long           va_nodeid;    /* node # */
	short          va_nlink;     /* # links */
	u_long         va_size;      /* file size */
	long           va_blocksize; /* block size */
	struct timeval va_atime;     /* last acc */
	struct timeval va_mtime;     /* last mod */
	struct timeval va_ctime;     /* last chg */
	dev_t          va_rdev;      /* dev */
	long           va_blocks;    /* space used */
} vattr_t;

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

extern int vn_open(const char *path, uio_seg_t seg, int flags, int mode,
		   vnode_t **vpp, int x1, void *x2);
extern int vn_openat(const char *path, uio_seg_t seg, int flags, int mode,
		     vnode_t **vpp, int x1, void *x2, vnode_t *vp, int fd);
extern int vn_rdwr(uio_rw_t uio, vnode_t *vp, void *addr, ssize_t len,
		   offset_t off, uio_seg_t seg, int x1, rlim64_t x2,
		   void *x3, ssize_t *residp);
extern int vn_close(vnode_t *vp, int flags, int x1, int x2, void *x3, void *x4);
extern int vn_remove(const char *path, uio_seg_t seg, int flags);
extern int vn_rename(const char *path1, const char *path2, int x1);
extern int vn_getattr(vnode_t *vp, vattr_t *vap, int flags, void *x3, void *x4);
extern int vn_fsync(vnode_t *vp, int flags, void *x3, void *x4);

static __inline__ int
vn_rele(vnode_t *vp)
{
	return 0;
} /* vn_rele() */

static __inline__ int
vn_putpage(vnode_t *vp, offset_t off, ssize_t size,
           int flags, void *x1, void *x2) {
	return 0;
} /* vn_putpage() */

#define VOP_CLOSE				vn_close
#define VN_RELE					vn_rele
#define VOP_GETATTR				vn_getattr
#define VOP_FSYNC				vn_fsync
#define VOP_PUTPAGE				vn_putpage
#define vn_is_readonly(vp)			0

extern void *rootdir;

#endif /* SPL_VNODE_H */
