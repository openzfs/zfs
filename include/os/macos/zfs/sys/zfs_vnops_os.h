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
 * Copyright (c) 2010, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef	_SYS_FS_ZFS_VNOPS_OS_H
#define	_SYS_FS_ZFS_VNOPS_OS_H

#include <sys/vnode.h>
#include <sys/xvattr.h>
#include <sys/uio.h>
#include <sys/cred.h>
#include <sys/fcntl.h>
#include <sys/pathname.h>

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * Spotlight specific fcntl()'s
 */

// Older defines
#define	SPOTLIGHT_GET_MOUNT_TIME	(FCNTL_FS_SPECIFIC_BASE + 0x00002)
#define	SPOTLIGHT_GET_UNMOUNT_TIME	(FCNTL_FS_SPECIFIC_BASE + 0x00003)

// Newer defines, will these need a OSX version test to compile on older?
#define	SPOTLIGHT_IOC_GET_MOUNT_TIME	_IOR('h', 18, u_int32_t)
#define	SPOTLIGHT_FSCTL_GET_MOUNT_TIME	\
    IOCBASECMD(SPOTLIGHT_IOC_GET_MOUNT_TIME)
#define	SPOTLIGHT_IOC_GET_LAST_MTIME	_IOR('h', 19, u_int32_t)
#define	SPOTLIGHT_FSCTL_GET_LAST_MTIME	\
    IOCBASECMD(SPOTLIGHT_IOC_GET_LAST_MTIME)

/*
 * Account for user timespec structure differences
 */
#ifdef ZFS_LEOPARD_ONLY
typedef struct timespec		timespec_user32_t;
typedef struct user_timespec	timespec_user64_t;
#else
typedef struct user32_timespec	timespec_user32_t;
typedef struct user64_timespec	timespec_user64_t;
#endif

#define	UNKNOWNUID ((uid_t)99)
#define	UNKNOWNGID ((gid_t)99)

#define	DTTOVT(dtype)   (iftovt_tab[(dtype)])
#define	kTextEncodingMacUnicode	0x7e
#define	ZAP_AVENAMELEN  (ZAP_MAXNAMELEN / 4)

/* Finder information */
struct finderinfo {
	u_int32_t  fi_type;	/* files only */
	u_int32_t  fi_creator;	/* files only */
	u_int16_t  fi_flags;
	struct {
		int16_t  v;
		int16_t  h;
	} fi_location;
	int8_t  fi_opaque[18];
} __attribute__((aligned(2), packed));
typedef struct finderinfo finderinfo_t;

enum {
	/* Finder Flags */
	kHasBeenInited		= 0x0100,
	kHasCustomIcon		= 0x0400,
	kIsStationery		= 0x0800,
	kNameLocked		= 0x1000,
	kHasBundle		= 0x2000,
	kIsInvisible		= 0x4000,
	kIsAlias		= 0x8000
};

/* Attribute packing information */
typedef struct attrinfo {
	struct attrlist *ai_attrlist;
	void **ai_attrbufpp;
	void **ai_varbufpp;
	void *ai_varbufend;
	vfs_context_t ai_context;
} attrinfo_t;

/*
 * Attributes that we can get for free from the zap (ie without a znode)
 */
#define	ZFS_DIR_ENT_ATTRS ( \
	    ATTR_CMN_NAME | ATTR_CMN_DEVID | ATTR_CMN_FSID | \
	    ATTR_CMN_OBJTYPE | ATTR_CMN_OBJTAG | ATTR_CMN_OBJID | \
	    ATTR_CMN_OBJPERMANENTID | ATTR_CMN_SCRIPT | \
	    ATTR_CMN_FILEID)

/*
 * Attributes that we support
 */
#define	ZFS_ATTR_BIT_MAP_COUNT  5

#define	ZFS_ATTR_CMN_VALID (                                    \
	    ATTR_CMN_NAME | ATTR_CMN_DEVID  | ATTR_CMN_FSID |       \
	    ATTR_CMN_OBJTYPE | ATTR_CMN_OBJTAG | ATTR_CMN_OBJID |   \
	    ATTR_CMN_OBJPERMANENTID | ATTR_CMN_PAROBJID |           \
	    ATTR_CMN_SCRIPT | ATTR_CMN_CRTIME | ATTR_CMN_MODTIME |  \
	    ATTR_CMN_CHGTIME | ATTR_CMN_ACCTIME |                   \
	    ATTR_CMN_BKUPTIME | ATTR_CMN_FNDRINFO |                 \
	    ATTR_CMN_OWNERID | ATTR_CMN_GRPID |                     \
	    ATTR_CMN_ACCESSMASK | ATTR_CMN_FLAGS |                  \
	    ATTR_CMN_USERACCESS | ATTR_CMN_FILEID |                 \
	    ATTR_CMN_PARENTID)

#define	ZFS_ATTR_DIR_VALID (                            \
	    ATTR_DIR_LINKCOUNT | ATTR_DIR_ENTRYCOUNT |      \
	    ATTR_DIR_MOUNTSTATUS)

#define	ZFS_ATTR_FILE_VALID (                            \
	    ATTR_FILE_LINKCOUNT |ATTR_FILE_TOTALSIZE |       \
	    ATTR_FILE_ALLOCSIZE | ATTR_FILE_IOBLOCKSIZE |    \
	    ATTR_FILE_DEVTYPE | ATTR_FILE_DATALENGTH |       \
	    ATTR_FILE_DATAALLOCSIZE | ATTR_FILE_RSRCLENGTH | \
	    ATTR_FILE_RSRCALLOCSIZE)

extern int zfs_remove(znode_t *dzp, char *name, cred_t *cr, int flags);
extern int zfs_mkdir(znode_t *dzp, char *dirname, vattr_t *vap,
	znode_t **zpp, cred_t *cr, int flags, vsecattr_t *vsecp,
	zuserns_t *mnt_ns);
extern int zfs_rmdir(znode_t *dzp, char *name, znode_t *cwd,
	cred_t *cr, int flags);
extern int zfs_setattr(znode_t *zp, vattr_t *vap, int flag, cred_t *cr,
	zuserns_t *mnt_ns);
extern int zfs_rename(znode_t *sdzp, char *snm, znode_t *tdzp,
	char *tnm, cred_t *cr, int flags, uint64_t rflags, vattr_t *wo_vap,
	zuserns_t *mnt_ns);
extern int zfs_symlink(znode_t *dzp, char *name, vattr_t *vap,
	char *link, znode_t **zpp, cred_t *cr, int flags, zuserns_t *mnt_ns);
extern int zfs_link(znode_t *tdzp, znode_t *sp,
	char *name, cred_t *cr, int flags);
extern int zfs_space(znode_t *zp, int cmd, struct flock *bfp, int flag,
	offset_t offset, cred_t *cr);
extern int zfs_create(znode_t *dzp, char *name, vattr_t *vap, int excl,
	int mode, znode_t **zpp, cred_t *cr, int flag, vsecattr_t *vsecp,
	zuserns_t *mnt_ns);
extern int zfs_write_simple(znode_t *zp, const void *data, size_t len,
	loff_t pos, size_t *resid);

extern int zfs_open(struct vnode *ip, int mode, int flag, cred_t *cr);
extern int zfs_close(struct vnode *ip, int flag, cred_t *cr);
extern int zfs_lookup(znode_t *dzp, char *nm, znode_t **zpp,
    int flags, cred_t *cr, int *direntflags, struct componentname *realpnp);
extern int zfs_ioctl(vnode_t *vp, ulong_t com, intptr_t data, int flag,
    cred_t *cred, int *rvalp, caller_context_t *ct);
extern int zfs_readdir(vnode_t *vp, zfs_uio_t *uio, cred_t *cr, int *eofp,
    int flags, int *a_numdirent);
extern int zfs_fsync(znode_t *zp, int syncflag, cred_t *cr);
extern int zfs_getattr(vnode_t *vp, vattr_t *vap, int flags,
    cred_t *cr, caller_context_t *ct);
extern int zfs_readlink(vnode_t *vp, zfs_uio_t *uio, cred_t *cr);

extern void   zfs_inactive(vnode_t *vp);

/* zfs_vops_osx.c calls */
extern int    zfs_znode_getvnode(znode_t *zp, zfsvfs_t *zfsvfs);

extern void   getnewvnode_reserve(int num);
extern void   getnewvnode_drop_reserve(void);
extern int    zfs_vfsops_init(void);
extern int    zfs_vfsops_fini(void);
extern int    zfs_znode_asyncgetvnode(znode_t *zp, zfsvfs_t *zfsvfs);
extern void   zfs_znode_asyncput(znode_t *zp);
extern int    zfs_znode_asyncwait(zfsvfs_t *, znode_t *zp);

/* zfs_vnops_osx_lib calls */
extern int    zfs_ioflags(int ap_ioflag);
extern int    zfs_getattr_znode_unlocked(struct vnode *vp, vattr_t *vap);
extern int    ace_trivial_common(void *acep, int aclcnt,
    uint64_t (*walk)(void *, uint64_t, int aclcnt,
	    uint16_t *, uint16_t *, uint32_t *));
extern void   acl_trivial_access_masks(mode_t mode, boolean_t isdir,
    trivial_acl_t *masks);
extern int    zpl_obtain_xattr(struct znode *, const char *name, mode_t mode,
    cred_t *cr, struct vnode **vpp, int flag);

extern void  commonattrpack(attrinfo_t *aip, zfsvfs_t *zfsvfs, znode_t *zp,
    const char *name, ino64_t objnum, enum vtype vtype,
    boolean_t user64);
extern void  dirattrpack(attrinfo_t *aip, znode_t *zp);
extern void  fileattrpack(attrinfo_t *aip, zfsvfs_t *zfsvfs, znode_t *zp);
extern void  nameattrpack(attrinfo_t *aip, const char *name, int namelen);
extern int   getpackedsize(struct attrlist *alp, boolean_t user64);
extern void  getfinderinfo(znode_t *zp, cred_t *cr, finderinfo_t *fip);
extern uint32_t getuseraccess(znode_t *zp, vfs_context_t ctx);
extern void  finderinfo_update(uint8_t *finderinfo, znode_t *zp);
extern void zfs_zrele_async(znode_t *zp);

/*
 * OSX ACL Helper funcions
 *
 * OSX uses 'guids' for the 'who' part of ACLs, and uses a 'well known'
 * binary sequence to signify the special rules of "owner", "group" and
 * "everybody". We translate between this "well-known" guid and ZFS'
 * flags ACE_OWNER, ACE_GROUP and ACE_EVERYBODY.
 *
 */
#define	KAUTH_WKG_NOT	0	/* not a well-known GUID */
#define	KAUTH_WKG_OWNER	1
#define	KAUTH_WKG_GROUP	2
#define	KAUTH_WKG_NOBODY	3
#define	KAUTH_WKG_EVERYBODY	4

extern int kauth_wellknown_guid(guid_t *guid);
extern void aces_from_acl(ace_t *aces, int *nentries, struct kauth_acl *k_acl,
    int *seen_type);
extern void nfsacl_set_wellknown(int wkg, guid_t *guid);
extern int  zfs_addacl_trivial(znode_t *zp, ace_t *aces, int *nentries,
    int seen_type);

extern struct vnodeopv_desc zfs_dvnodeop_opv_desc;
extern struct vnodeopv_desc zfs_fvnodeop_opv_desc;
extern struct vnodeopv_desc zfs_symvnodeop_opv_desc;
extern struct vnodeopv_desc zfs_xdvnodeop_opv_desc;
extern struct vnodeopv_desc zfs_evnodeop_opv_desc;
extern struct vnodeopv_desc zfs_fifonodeop_opv_desc;
extern struct vnodeopv_desc zfs_ctldir_opv_desc;
extern int (**zfs_ctldirops)(void *);


extern int zpl_xattr_list(struct vnode *dvp, zfs_uio_t *uio,
    ssize_t *, cred_t *cr);
extern int zpl_xattr_get(struct vnode *ip, const char *name,
    zfs_uio_t *uio, ssize_t *, cred_t *cr);
extern int zpl_xattr_set(struct vnode *ip, const char *name,
    zfs_uio_t *uio, int flags, cred_t *cr);

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_FS_ZFS_VNOPS_H */
