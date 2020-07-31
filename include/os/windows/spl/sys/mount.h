
#ifndef _SPL_MOUNT_H
#define _SPL_MOUNT_H

//#undef vnode_t
//#include_next <sys/mount.h>
//#define vnode_t struct vnode
#define MNT_WAIT        1       /* synchronized I/O file integrity completion */
#define MNT_NOWAIT      2       /* start all I/O, but do not wait for it */

#define MNT_RDONLY      0x00000001      /* read only filesystem */
#define MNT_SYNCHRONOUS 0x00000002      /* file system written synchronously */
#define MNT_NOEXEC      0x00000004      /* can't exec from filesystem */
#define MNT_NOSUID      0x00000008      /* don't honor setuid bits on fs */
#define MNT_NODEV       0x00000010      /* don't interpret special files */
#define MNT_UNION       0x00000020      /* union with underlying filesystem */
#define MNT_ASYNC       0x00000040      /* file system written asynchronously */
#define MNT_CPROTECT    0x00000080      /* file system supports content protection */

#define MNT_LOCAL       0x00001000      /* filesystem is stored locally */
#define MNT_QUOTA       0x00002000      /* quotas are enabled on filesystem */
#define MNT_ROOTFS      0x00004000      /* identifies the root filesystem */
#define MNT_DOVOLFS     0x00008000      /* FS supports volfs (deprecated flag in Mac OS X 10.5) */

#define MNT_DONTBROWSE  0x00100000      /* file system is not appropriate path to user data */
#define MNT_IGNORE_OWNERSHIP 0x00200000 /* VFS will ignore ownership information on filesystem objects */
#define MNT_AUTOMOUNTED 0x00400000      /* filesystem was mounted by automounter */
#define MNT_JOURNALED   0x00800000      /* filesystem is journaled */
#define MNT_NOUSERXATTR 0x01000000      /* Don't allow user extended attributes */
#define MNT_DEFWRITE    0x02000000      /* filesystem should defer writes */
#define MNT_MULTILABEL  0x04000000      /* MAC support for individual labels */
#define MNT_NOATIME     0x10000000      /* disable update of file access time */

#define MNT_UPDATE      0x00010000      /* not a real mount, just an update */
#define MNT_NOBLOCK     0x00020000      /* don't block unmount if not responding */
#define MNT_RELOAD      0x00040000      /* reload filesystem data */
#define MNT_FORCE       0x00080000      /* force unmount or readonly change */
#define MNT_CMDFLAGS    (MNT_UPDATE|MNT_NOBLOCK|MNT_RELOAD|MNT_FORCE)

#define MNT_UNKNOWNPERMISSIONS MNT_IGNORE_OWNERSHIP

#define	MFSTYPENAMELEN	16

// Undo this OSX legacy
typedef struct fsid { int32_t val[2]; } fsid_t;

//#pragma pack(4)

struct vfsstatfs {
	uint32_t        f_bsize;        /* fundamental file system block size */
	size_t          f_iosize;       /* optimal transfer block size */
	uint64_t        f_blocks;       /* total data blocks in file system */
	uint64_t        f_bfree;        /* free blocks in fs */
	uint64_t        f_bavail;       /* free blocks avail to non-superuser */
	uint64_t        f_bused;        /* free blocks avail to non-superuser */
	uint64_t        f_files;        /* total file nodes in file system */
	uint64_t        f_ffree;        /* free file nodes in fs */
	fsid_t          f_fsid;         /* file system id */
	uid_t           f_owner;        /* user that mounted the filesystem */
	uint64_t        f_flags;        /* copy of mount exported flags */
	char            f_fstypename[MFSTYPENAMELEN];/* fs type name inclus */
	char            f_mntonname[MAXPATHLEN];/* directory on which mounted */
	char            f_mntfromname[MAXPATHLEN];/* mounted filesystem */
	uint32_t        f_fssubtype;     /* fs sub-type (flavor) */
	void            *f_reserved[2];         /* For future use == 0 */
};

//#pragma pack()

//enum mount_type {
//	MOUNT_TYPE_DCB = 231, // diskObject (most entries not used, should be own struct?)
//	MOUNT_TYPE_VCB        // fsObject 
//};

typedef enum _FSD_IDENTIFIER_TYPE {
	MOUNT_TYPE_DGL = ':DGL', // Dokan Global
	MOUNT_TYPE_DCB = ':DCB', // Disk Control Block
	MOUNT_TYPE_VCB = ':VCB', // Volume Control Block
	MOUNT_TYPE_FCB = ':FCB', // File Control Block
	MOUNT_TYPE_CCB = ':CCB', // Context Control Block
} FSD_IDENTIFIER_TYPE;


typedef enum mount_type mount_type_t;

struct mount
{
	FSD_IDENTIFIER_TYPE type;
	ULONG size;
//	mount_type_t type;
	void *fsprivate;
	void *parent_device; // Only set so vcd can find dcb
	PDEVICE_OBJECT deviceObject;
	PDEVICE_OBJECT diskDeviceObject;
	UNICODE_STRING bus_name;
	UNICODE_STRING device_name;
	UNICODE_STRING symlink_name;
	UNICODE_STRING fs_name;
	UNICODE_STRING name;
	UNICODE_STRING uuid;
	UNICODE_STRING mountpoint;
	boolean_t justDriveLetter;
	uint64_t volume_opens;
	PVPB vpb;

	uint64_t mountflags;

	// NotifySync is used by notify directory change
	PNOTIFY_SYNC NotifySync;
	LIST_ENTRY DirNotifyList;
};
typedef struct mount mount_t;
#define LK_NOWAIT 1

int     vfs_busy(mount_t *mp, int flags);
void    vfs_unbusy(mount_t *mp);
int     vfs_isrdonly(mount_t *mp);
void *	vfs_fsprivate(mount_t *mp);
void    vfs_setfsprivate(mount_t *mp, void *mntdata);
void    vfs_clearflags(mount_t *mp, uint64_t flags);
void    vfs_setflags(mount_t *mp, uint64_t flags);
struct vfsstatfs *      vfs_statfs(mount_t *mp);
uint64_t vfs_flags(mount_t *mp);
void    vfs_setlocklocal(mount_t *mp);
int     vfs_typenum(mount_t *mp);
void    vfs_getnewfsid(struct mount *mp);
int     vfs_isunmount(mount_t *mp);
#endif /* SPL_MOUNT_H */
