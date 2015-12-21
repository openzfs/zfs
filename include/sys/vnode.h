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

#ifndef _SYS_VNODE_H
#define	_SYS_VNODE_H

#include <sys/types.h>
#include <sys/uio.h>
#include <sys/resource.h>
#include <sys/file.h>

/*
 * The vnode AT_ flags are mapped to the Linux ATTR_* flags.
 * This allows them to be used safely with an iattr structure.
 * The AT_XVATTR flag has been added and mapped to the upper
 * bit range to avoid conflicting with the standard Linux set.
 */
#undef AT_UID
#undef AT_GID

#ifdef _KERNEL
#define	AT_TYPE		ATTR_MODE
#define	AT_MODE		ATTR_MODE
#define	AT_UID		ATTR_UID
#define	AT_GID		ATTR_GID
#define	AT_SIZE		ATTR_SIZE
#define	AT_ATIME	ATTR_ATIME
#define	AT_MTIME	ATTR_MTIME
#define	AT_CTIME	ATTR_CTIME
#define	ATTR_XVATTR	(1 << 31)
#define	AT_XVATTR	ATTR_XVATTR
#else
#define	AT_TYPE		0x00001
#define	AT_MODE		0x00002
#define	AT_UID		0x00004
#define	AT_GID		0x00008
#define	AT_FSID		0x00010
#define	AT_NODEID	0x00020
#define	AT_NLINK	0x00040
#define	AT_SIZE		0x00080
#define	AT_ATIME	0x00100
#define	AT_MTIME	0x00200
#define	AT_CTIME	0x00400
#define	AT_RDEV		0x00800
#define	AT_BLKSIZE	0x01000
#define	AT_NBLOCKS	0x02000
#define	AT_SEQ		0x08000
#define	AT_XVATTR	0x10000
#endif

#define	ATTR_IATTR_MASK	(ATTR_MODE | ATTR_UID | ATTR_GID | ATTR_SIZE | \
			ATTR_ATIME | ATTR_MTIME | ATTR_CTIME | ATTR_FILE)

#ifdef _KERNEL
#define	CRCREAT		0x01
#define	RMFILE		0x02
#else
#define	CRCREAT		0x00
#define	RMFILE		0x00
#endif

#define	B_INVAL		0x01
#define	B_TRUNC		0x02

#define	LOOKUP_DIR		0x01
#define	LOOKUP_XATTR		0x02
#define	CREATE_XATTR_DIR	0x04
#define	ATTR_NOACLCHECK		0x20

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

typedef struct vattr {
	enum vtype	va_type;	/* vnode type */
	u_int		va_mask;	/* attribute bit-mask */
	u_short		va_mode;	/* acc mode */
	uid_t		va_uid;		/* owner uid */
	gid_t		va_gid;		/* owner gid */
	long		va_fsid;	/* fs id */
	long		va_nodeid;	/* node # */
	uint32_t	va_nlink;	/* # links */
	uint64_t	va_size;	/* file size */
	struct timespec	va_atime;	/* last acc */
	struct timespec	va_mtime;	/* last mod */
	struct timespec	va_ctime;	/* last chg */
	dev_t		va_rdev;	/* dev */
	uint64_t	va_nblocks;	/* space used */
	uint32_t	va_blksize;	/* block size */
	uint32_t	va_seq;		/* sequence */
	struct dentry	*va_dentry;	/* dentry to wire */
} vattr_t;

/*
 * As a simplification the kernel vnode wrappers treat vnode's and file's as
 * one and the same.  This allows us to directly map the required operations
 * to spl_file_* wrapper functions.  In user space this isn't possible so
 * simplified versions of the vnode and file structures are created.
 */
#ifdef _KERNEL
typedef struct file vnode_t;
typedef struct file file_t;
#else
typedef struct vnode {
	uint64_t	v_size;
	int		v_fd;
	char		*v_path;
} vnode_t;

typedef struct file {
	vnode_t		*f_vnode;
	int		f_fd;
} file_t;
#endif

extern vnode_t *rootdir;

extern vtype_t vn_mode_to_vtype(mode_t mode);
extern mode_t vn_vtype_to_mode(vtype_t vtype);
extern int vn_open(const char *path, uio_seg_t seg, int flags, int mode,
    vnode_t **vpp, int unused1, void *unused2);
extern int vn_openat(const char *path, uio_seg_t seg, int flags, int mode,
    vnode_t **vpp, int unused1, void *unused2, vnode_t *unused3, int unused4);
extern int vn_rdwr(uio_rw_t uio, vnode_t *vp, void *addr, ssize_t len,
    offset_t off, uio_seg_t seg, int flags, rlim64_t unused1,
    void *unused2, ssize_t *residp);
extern int vn_close(vnode_t *vp, int unused1, int unused2, int unused3,
    void *unused4, void *unused5);
extern int vn_seek(vnode_t *vp, offset_t ooff, offset_t *noffp, void *ct);
extern int vn_remove(const char *path, uio_seg_t seg, int flags);
extern int vn_getattr(vnode_t *vp, vattr_t *vap, int flags, void *unused1,
    void *unused2);
extern int vn_fsync(vnode_t *vp, int flags, void *unused1, void *unused2);
extern vnode_t *vn_from_file(struct file *fp);

#endif	/* _SYS_VNODE_H */
