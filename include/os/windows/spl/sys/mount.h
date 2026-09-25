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

#ifndef _SPL_MOUNT_H
#define	_SPL_MOUNT_H

#include <sys/list.h>

#define	MNT_WAIT	1	/* synchronized I/O file integrity completion */
#define	MNT_NOWAIT	2	/* start all I/O, but do not wait for it */

#define	MNT_RDONLY	0x00000001 /* read only filesystem */
#define	MNT_SYNCHRONOUS	0x00000002 /* file system written synchronously */
#define	MNT_NOEXEC	0x00000004 /* can't exec from filesystem */
#define	MNT_NOSUID	0x00000008 /* don't honor setuid bits on fs */
#define	MNT_NODEV	0x00000010 /* don't interpret special files */
#define	MNT_UNION	0x00000020 /* union with underlying filesystem */
#define	MNT_ASYNC	0x00000040 /* file system written asynchronously */
#define	MNT_CPROTECT	0x00000080 /* file system supports content protection */

#define	MNT_LOCAL	0x00001000 /* filesystem is stored locally */
#define	MNT_QUOTA	0x00002000 /* quotas are enabled on filesystem */
#define	MNT_ROOTFS	0x00004000 /* identifies the root filesystem */
#define	MNT_DOVOLFS	0x00008000 /* FS supports volfs (deprecated 10.5) */

#define	MNT_DONTBROWSE	0x00100000 /* fs is not appropriate path to user data */
#define	MNT_IGNORE_OWNERSHIP	0x00200000 /* VFS will ignore ownership */
#define	MNT_AUTOMOUNTED	0x00400000 /* filesystem was mounted by automounter */
#define	MNT_JOURNALED	0x00800000 /* filesystem is journaled */
#define	MNT_NOUSERXATTR	0x01000000 /* Don't allow user extended attributes */
#define	MNT_DEFWRITE	0x02000000 /* filesystem should defer writes */
#define	MNT_MULTILABEL	0x04000000 /* MAC support for individual labels */
#define	MNT_NOATIME	0x10000000 /* disable update of file access time */

#define	MNT_UPDATE	0x00010000 /* not a real mount, just an update */
#define	MNT_NOBLOCK	0x00020000 /* don't block unmount if not responding */
#define	MNT_RELOAD	0x00040000 /* reload filesystem data */
#define	MNT_FORCE	0x00080000 /* force unmount or readonly change */
#define	MNT_CMDFLAGS	(MNT_UPDATE|MNT_NOBLOCK|MNT_RELOAD|MNT_FORCE)

#define	MNT_UNMOUNTING	0x80000000 /* process of unmounting */

#define	MNT_UNKNOWNPERMISSIONS MNT_IGNORE_OWNERSHIP

#define	MFSTYPENAMELEN	16

// Undo this OSX legacy
typedef struct fsid { int32_t val[2]; } fsid_t;

struct vfsstatfs {
	uint32_t	f_bsize;	/* fundamental file system block size */
	size_t		f_iosize;	/* optimal transfer block size */
	uint64_t	f_blocks;	/* total data blocks in file system */
	uint64_t	f_bfree;	/* free blocks in fs */
	uint64_t	f_bavail;	/* free blocks avail to non-superuser */
	uint64_t	f_bused;	/* free blocks avail to non-superuser */
	uint64_t	f_files;	/* total file nodes in file system */
	uint64_t	f_ffree;	/* free file nodes in fs */
	fsid_t		f_fsid;		/* file system id */
	uid_t		f_owner;	/* user that mounted the filesystem */
	uint64_t	f_flags;	/* copy of mount exported flags */
	char		f_fstypename[MFSTYPENAMELEN]; /* fs type name inclus */
	char		f_mntonname[MAXPATHLEN]; /* dir on which mounted */
	char		f_mntfromname[MAXPATHLEN]; /* mounted filesystem */
	uint32_t	f_fssubtype;	/* fs sub-type (flavor) */
	void		*f_reserved[2];	/* For future use == 0 */
};

typedef enum _FSD_IDENTIFIER_TYPE {
	MOUNT_TYPE_DGL = ':DGL', // Global
	MOUNT_TYPE_BUS = ':BUS', // Bus Control
	MOUNT_TYPE_DCB = ':DCB', // Disk Control Block
	MOUNT_TYPE_VCB = ':VCB', // Volume Control Block
	MOUNT_TYPE_FCB = ':FCB', // File Control Block
	MOUNT_TYPE_CCB = ':CCB', // Context Control Block
	MOUNT_TYPE_VSS = ':VSS', // VSS snapshot device
} FSD_IDENTIFIER_TYPE;

// typedef enum mount_type mount_type_t;

struct mount
{
	FSD_IDENTIFIER_TYPE type;
	ULONG size;
	const unsigned char *ascii_name;
	void *fsprivate;
	void *parent_device; // Only set so vcd can find dcb
	uuid_t rawuuid;
	PDEVICE_OBJECT PhysicalDeviceObject; // From AddDevices
	PDEVICE_OBJECT LowerDeviceObject; // Attaching PDO in AddDevices
	PDEVICE_OBJECT FunctionalDeviceObject; // Created in AddDevices
	PDEVICE_OBJECT VolumeDeviceObject;
	PDEVICE_OBJECT AttachedDevice;
	UNICODE_STRING bus_name;
	UNICODE_STRING device_name;
	UNICODE_STRING symlink_name;
	UNICODE_STRING arc_name;
	UNICODE_STRING fs_name;
	UNICODE_STRING name;
	UNICODE_STRING uuid;
	UNICODE_STRING mountpoint;
	UNICODE_STRING dosdevices_mountpoint;
	UNICODE_STRING deviceInterfaceName;
	UNICODE_STRING fsInterfaceName;
	UNICODE_STRING volumeInterfaceName;
	UNICODE_STRING MountMgr_name;
	UNICODE_STRING MountMgr_mountpoint;
	const char *mounted_on;
	PFILE_OBJECT root_file;
	boolean_t justDriveLetter;
	uint64_t volume_opens;
	PVPB vpb;

	uint64_t mountflags;

	KEVENT volume_removed_event;
	KEVENT volume_adddevice_event; // Until AddDevice is called
	KEVENT volume_mounted_event; // Until full mount is done.

	// Linked list of mounts
	list_node_t mount_node;

	// NotifySync is used by notify directory change
	PNOTIFY_SYNC NotifySync;
	LIST_ENTRY DirNotifyList;

	/* VSS snapshot lazy-mount fields (type == MOUNT_TYPE_VSS only) */
	uint64_t	vss_guid;
	uint64_t	vss_creation;	/* Unix creation timestamp */
	char		vss_snapname[256]; /* ZFS_MAX_DATASET_NAME_LEN */
	kmutex_t	vss_mount_lock;    /* serialise first-access mount */
	/*
	 * Set by zfs_vss_snapshot_remove before IoDeleteDevice.
	 * IRP_MJ_CREATE checks this under vss_mount_lock and rejects with
	 * STATUS_DELETE_PENDING, preventing PnP from opening a new file
	 * object on the device after it has been removed from the driver's
	 * device list.  Without this guard, IopDecrementDeviceObjectRef
	 * from PnP's close would trigger IopCompleteUnloadOrDelete ->
	 * IopInsertRemoveDevice on an already-removed device, BSODing.
	 */
	boolean_t	vss_del_pending;
	/*
	 * Set to B_TRUE once IOCTL_MOUNTDEV_QUERY_UNIQUE_ID is processed.
	 * While this remains B_FALSE (the normal case — MountMgr is never
	 * notified about VSS stub devices), is_dev_open is always true and
	 * IRP_MJ_CREATE on the stub never triggers a lazy mount.  Lazy mount
	 * happens via the ctldir path when the parent volume's .zfs/snapshot
	 * directory is accessed.
	 */
	boolean_t	vss_mountmgr_probed;
	/*
	 * Set in IRP_MN_SURPRISE_REMOVAL for MOUNT_TYPE_DCB disk devices.
	 * IRP_MJ_CREATE checks this and rejects with STATUS_DEVICE_REMOVED,
	 * preventing IopInvalidateVolumesForDevice (called from IopRemoveDevice
	 * after PnP internally deletes the device node) from opening a new file
	 * object on the already-deleted device.  Without this guard the close
	 * of that file object triggers a second IopCompleteUnloadOrDelete ->
	 * ObDereferenceSecurityDescriptor on an SD whose refcount is already 0,
	 * BSODing with INVALID_REFERENCE_COUNT (0x139).
	 */
	boolean_t	dcb_del_pending;
};
typedef struct mount mount_t;
typedef struct mount vfsp_t;
#define	LK_NOWAIT	(1<<0)
#define	LK_UPGRADE	(1<<1)

int spl_vfs_init(void);
void spl_vfs_fini(void);

int   vfs_busy(mount_t *mp, int flags);
void  vfs_unbusy(mount_t *mp);
int   vfs_main_lock_write_held(void);
int   vfs_isrdonly(mount_t *mp);
void  vfs_setrdonly(mount_t *mp);
void  vfs_clearrdonly(mount_t *mp);

void *vfs_fsprivate(mount_t *mp);
void  vfs_setfsprivate(mount_t *mp, void *mntdata);
void  vfs_clearflags(mount_t *mp, uint64_t flags);
void  vfs_setflags(mount_t *mp, uint64_t flags);
struct vfsstatfs *vfs_statfs(mount_t *mp);
uint64_t vfs_flags(mount_t *mp);
void  vfs_setlocklocal(mount_t *mp);
int   vfs_typenum(mount_t *mp);
void  vfs_getnewfsid(struct mount *mp);
int   vfs_isunmount(mount_t *mp);
int	  vfs_iswriteupgrade(mount_t *mp);
void  vfs_setextendedsecurity(mount_t *mp);

void vfs_mount_add(mount_t *mp);
void vfs_mount_remove(mount_t *mp);
int vfs_mount_count(void);
void vfs_mount_setarray(void **array, int max);
void vfs_mount_iterate(int (*func)(void *, void *), void *);
boolean_t vfs_mount_member(void *member);
void vfs_set_mountedon(mount_t *mp, char *rootpath);
const char *vfs_mountedon(mount_t *mp);
mount_t *vfs_has_mount(const char *rpath);


#endif /* SPL_MOUNT_H */
