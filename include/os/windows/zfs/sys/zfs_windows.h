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
 * Copyright (c) 2017 Jorgen Lundman <lundman@lundman.net>
 */

#ifndef SYS_WINDOWS_H_INCLUDED
#define	SYS_WINDOWS_H_INCLUDED


#include <sys/mount.h>
#include <sys/cred.h>

extern PDEVICE_OBJECT ioctlDeviceObject;
extern PDEVICE_OBJECT fsDiskDeviceObject;

#define	ZFS_SERIAL	(ULONG)'wZFS'
#define	VOLUME_LABEL	L"OpenZFSVolume"

#define	ZFS_HAVE_FASTIO

#define	SKIP_CHANGE_TIME	(UIO_SKIP_CHANGETIME)
#define	SKIP_WRITE_TIME		(UIO_SKIP_WRITETIME)
#define	SKIP_SIZE_UPDATE	(UIO_SKIP_SIZE_UPDATE)

// We have to remember "query directory" related items, like index and
// search pattern. This is attached in IRP_MJ_CREATE to fscontext2
#define	ZFS_CCB_MAGIC 0x6582feac
struct zfs_ccb {
	uint32_t magic;			// Identifier
	uint32_t dir_eof;		// Directory listing completed?
	uint64_t dirlist_index;		// Directory list offset
	uint64_t ea_index;		// EA list offset
	uint64_t uio_offset;		// uio offset
	uint32_t deleteonclose;		// Marked for deletion
	uint32_t ContainsWildCards;	// searchname has wildcards

	uint32_t z_name_len;		// name at open
	uint32_t z_name_offset;		// offset to name (skipping dirs)
	char *z_name_cache;		// ptr to full path
	uint64_t z_name_rename;		// last rename time, if any

	UNICODE_STRING searchname;	// Search pattern (dirlist)

	uint64_t cacheinit;
	uint64_t real_file_id;
	boolean_t user_set_creation_time;
	boolean_t user_set_access_time;
	boolean_t user_set_write_time;
	boolean_t user_set_change_time;
	ACCESS_MASK access;

	boolean_t HoldsOplock;

	/*
	 * Credentials of the process that opened this FileObject, captured
	 * at IRP_MJ_CREATE time via spl_fill_cred_from_irp().  Embedded
	 * (not a pointer) so no separate allocation is needed.
	 */
	cred_t cred;
};

typedef struct zfs_ccb zfs_ccb_t;

extern uint64_t zfs_module_busy;

#define	DIR_LINKS(zp) \
	(S_ISDIR((zp)->z_mode) ? (zp)->z_links - 1 : (zp)->z_links)

#define	OPLOCK_SKIP_MAGIC   0x99ff77ee11aa5500 // put skip type in last byte
#define	OPLOCK_SKIP_MASK    0xffffffffffffff00
#define	OPLOCK_SKIP_NONE    (0 << 0)
#define	OPLOCK_SKIP_CREATE  (1 << 0)
#define	OPLOCK_SKIP_LOCK    (1 << 1)
#define	OPLOCK_SKIP_SETINFO (1 << 2)
#define	OPLOCK_SKIP_ZERODATA (1 << 3)

typedef struct ZFS_OPLOCK_CREATE_STRUCT {
    PDEVICE_OBJECT DeviceObject;
    PIRP Irp;
    PIO_WORKITEM WorkItem;
    uint64_t SkipMask;
} ZFS_OPLOCK_CREATE_CTX;
extern void ZfsOplockCreatePostBreak(_In_ PVOID Context, _In_ PIRP Irp);

extern CACHE_MANAGER_CALLBACKS CacheManagerCallbacks;

extern uint64_t zfs_mount_reentry;
extern uint_t zfs_mount_reentry_tsd;

extern NTSTATUS dev_ioctl(PDEVICE_OBJECT DeviceObject, ULONG ControlCode,
    PVOID InputBuffer, ULONG InputBufferSize, PVOID OutputBuffer,
    ULONG OutputBufferSize, BOOLEAN Override, IO_STATUS_BLOCK* iosb);
extern int zfs_vnop_lookup(PIRP Irp, PIO_STACK_LOCATION IrpSp, mount_t *zmo);
extern NTSTATUS fsDispatcher(PDEVICE_OBJECT DeviceObject, PIRP *PIrp,
    PIO_STACK_LOCATION IrpSp);

extern int zfs_windows_mount(zfs_cmd_t *zc);
extern int zfs_windows_mount_impl(const char *name, char *value,
    size_t valuelen, uint64_t mountflags);
extern int zfs_windows_unmount(zfs_cmd_t *zc);
extern int zfs_windows_unmount_impl(const char *datasetname);
extern NTSTATUS zfsdev_ioctl(PDEVICE_OBJECT DeviceObject, PIRP Irp, int flag);
extern void zfs_windows_vnops_callback(PDEVICE_OBJECT deviceObject);
extern void zfs_send_notify(zfsvfs_t *zfsvfs, char *name, int,
    ULONG FilterMatch, ULONG Action);
extern void zfs_send_notify_stream(zfsvfs_t *, char *, int, ULONG,
    ULONG, char *stream);
extern int zfs_attach_security(struct vnode *vp, struct vnode *dvp,
    PACCESS_STATE);
extern uint64_t zfs_sid2uid(SID *sid);
extern uint64_t zfs_sid2gid(SID *sid);
extern void find_set_gid(struct vnode *vp, struct vnode *dvp,
    PSECURITY_SUBJECT_CONTEXT subjcont);

BOOLEAN vattr_apply_lx_ea(vattr_t *vap, PFILE_FULL_EA_INFORMATION ea);
NTSTATUS vnode_apply_eas(struct vnode *vp, zfs_ccb_t *,
    PFILE_FULL_EA_INFORMATION eas,
    ULONG eaLength, PULONG pEaErrorOffset);

/* Main function to handle all VFS "vnops" */
extern _Function_class_(DRIVER_DISPATCH) NTSTATUS
    dispatcher(_In_ PDEVICE_OBJECT DeviceObject, _Inout_ PIRP Irp);

extern NTSTATUS zfsdev_open(dev_t dev, PIRP Irp);
extern NTSTATUS zfsdev_release(dev_t dev, PIRP Irp);

extern int	zfs_vnop_recycle(znode_t *zp, int force);
extern uint64_t zfs_blksz(znode_t *zp);

extern uint64_t allocationsize(struct znode *zp);

extern int	zfs_vnop_mount(PDEVICE_OBJECT DiskDevice, PIRP Irp,
    PIO_STACK_LOCATION IrpSp);

extern int	zfs_build_path(znode_t *start_zp, znode_t *start_parent,
    char **fullpath, uint32_t *returnsize, uint32_t *start_zp_offset);
extern int	zfs_build_path_stream(znode_t *start_zp, znode_t *start_parent,
    char **fullpath, uint32_t *returnsize, uint32_t *start_zp_offset, char *);

extern int	xattr_protected(const char *name);
extern int	xattr_stream(const char *name);
extern uint64_t xattr_getsize(struct vnode *vp);
extern char *major2str(int major, int minor);
extern char *common_status_str(NTSTATUS Status);
extern char *create_options(ULONG options);
extern char *create_reply(NTSTATUS, ULONG reply);
extern void latency_stats(uint64_t *histo, unsigned int buckets,
    stat_pair *lat);
extern int get_reparse_point_impl(znode_t *zp, char *buffer, size_t bufferlen,
    size_t *retlen);
extern void fastio_init(FAST_IO_DISPATCH **fast);
extern NTSTATUS pnp_query_di(PDEVICE_OBJECT DeviceObject, PIRP Irp,
    PIO_STACK_LOCATION IrpSp);
extern NTSTATUS pnp_query_bus_information(PDEVICE_OBJECT DeviceObject, PIRP Irp,
    PIO_STACK_LOCATION IrpSp);
extern NTSTATUS pnp_device_usage_notification(PDEVICE_OBJECT DeviceObject,
    PIRP Irp, PIO_STACK_LOCATION IrpSp);
extern NTSTATUS pnp_query_device_text(PDEVICE_OBJECT DeviceObject, PIRP Irp,
    PIO_STACK_LOCATION IrpSp);

extern int zfs_init_cache(FILE_OBJECT *fo, struct vnode *vp,
    CC_FILE_SIZES *ccfs);
extern void zfs_couplefileobject(vnode_t *vp, vnode_t *dvp, FILE_OBJECT *,
    uint64_t size, zfs_ccb_t **ccb, uint64_t alloc, ACCESS_MASK, char *stream,
    PIRP Irp);
extern void zfs_decouplefileobject(vnode_t *vp, FILE_OBJECT *fileobject);

/* zfs_vnop_windows_lib.h */
extern int	AsciiStringToUnicodeString(char *in, PUNICODE_STRING out);
extern int	AsciiStringToUnicodeStringNP(char *in, PUNICODE_STRING out);
extern void	FreeUnicodeString(PUNICODE_STRING s);
extern int	zfs_vfs_uuid_gen(const char *osname, uuid_t uuid);
extern int	zfs_vfs_uuid_unparse(uuid_t uuid, char *dst);
extern int	zfs_vnop_ioctl_fullfsync(struct vnode *, vfs_context_t *,
    zfsvfs_t *);
extern int	zfs_setwinflags(znode_t *zp, uint32_t winflags);
extern int	zfs_setwinflags_xva(znode_t *zp, uint32_t winflags, xvattr_t *);
extern uint32_t zfs_getwinflags(uint64_t zflags, boolean_t isdir);
extern NTSTATUS zfs_setunlink(FILE_OBJECT *fo, vnode_t *dvp, boolean_t);
extern NTSTATUS zfs_setunlink_masked(FILE_OBJECT *fo, vnode_t *dvp);
extern int zfs_find_dvp_vp(zfsvfs_t *, char *, int finalpartmaynotexist,
    int finalpartmustnotexist, char **lastname, struct vnode **dvpp,
    struct vnode **vpp, int flags, ULONG options, cred_t *cr);
extern void spl_fill_cred_from_irp(cred_t *cr, PIRP Irp);
extern ULONG get_reparse_tag(znode_t *zp);
extern void acl_trivial_access_masks(mode_t mode, boolean_t isdir,
    trivial_acl_t *masks);
extern void zfs_save_ntsecurity(struct vnode *vp);
extern void zfs_load_ntsecurity(struct vnode *vp);
extern void zfs_remove_ntsecurity(struct vnode *vp);

extern struct vnode *zfs_parent(struct vnode *);
extern void *MapUserBuffer(PIRP, ULONG, LOCK_OPERATION, PMDL *);
extern void UnMapUserBuffer(PMDL);

extern void mount_add_device(PDRIVER_OBJECT DriverObject,
    PDEVICE_OBJECT PhysicalDeviceObject);
extern void zfs_windows_unmount_free(PUNICODE_STRING symlink_name);
extern void zfs_release_mount(mount_t *zmo);
extern void zfs_unload_ioctl(PDEVICE_OBJECT, PVOID Context);


extern NTSTATUS volume_create(PDEVICE_OBJECT DeviceObject,
    PFILE_OBJECT FileObject, USHORT ShareAccess, uint64_t AllocationSize,
    ACCESS_MASK DesiredAccess);
extern NTSTATUS volume_close(PDEVICE_OBJECT DeviceObject,
    PFILE_OBJECT FileObject);


/* Translate a POSIX errno to the appropriate NTSTATUS code. */
extern NTSTATUS zfs_error_to_ntstatus(int error);

/* IRP_MJ_SET_INFORMATION helpers */
extern NTSTATUS set_file_basic_information(PDEVICE_OBJECT, PIRP,
    PIO_STACK_LOCATION);
extern NTSTATUS set_file_disposition_information(PDEVICE_OBJECT, PIRP,
    PIO_STACK_LOCATION, boolean_t);
extern NTSTATUS set_file_disposition_information_ex(PDEVICE_OBJECT, PIRP,
    PIO_STACK_LOCATION);
extern NTSTATUS set_file_endoffile_information(PDEVICE_OBJECT, PIRP,
    PIO_STACK_LOCATION, boolean_t advance_only, boolean_t prealloc);
extern NTSTATUS zfs_truncate_open_file(PDEVICE_OBJECT, PFILE_OBJECT,
    uint64_t allocation_size);
extern NTSTATUS set_file_link_information(PDEVICE_OBJECT, PIRP,
    PIO_STACK_LOCATION);
extern NTSTATUS set_file_rename_information(PDEVICE_OBJECT, PIRP,
    PIO_STACK_LOCATION);
extern NTSTATUS set_file_valid_data_length_information(
    PDEVICE_OBJECT DeviceObject, PIRP Irp, PIO_STACK_LOCATION IrpSp);
extern NTSTATUS set_file_case_sensitive_information(
    PDEVICE_OBJECT DeviceObject, PIRP Irp, PIO_STACK_LOCATION IrpSp);
extern NTSTATUS set_file_position_information(PDEVICE_OBJECT DeviceObject,
    PIRP Irp, PIO_STACK_LOCATION IrpSp);

/* IRP_MJ_GET_INFORMATION helpers */
extern void file_basic_information_impl(PDEVICE_OBJECT, PFILE_OBJECT,
    FILE_BASIC_INFORMATION *, PIO_STATUS_BLOCK);
extern NTSTATUS file_basic_information(PDEVICE_OBJECT, PIRP, PIO_STACK_LOCATION,
	FILE_BASIC_INFORMATION *);
extern NTSTATUS file_compression_information(PDEVICE_OBJECT, PIRP,
	PIO_STACK_LOCATION, FILE_COMPRESSION_INFORMATION *);
extern void file_standard_information_impl(PDEVICE_OBJECT, PFILE_OBJECT,
    FILE_STANDARD_INFORMATION *, size_t, PIO_STATUS_BLOCK);
extern NTSTATUS file_standard_information(PDEVICE_OBJECT, PIRP,
    PIO_STACK_LOCATION,	FILE_STANDARD_INFORMATION *);
extern NTSTATUS file_position_information(PDEVICE_OBJECT, PIRP,
    PIO_STACK_LOCATION,	FILE_POSITION_INFORMATION *);
extern NTSTATUS file_ea_information(PDEVICE_OBJECT, PIRP, PIO_STACK_LOCATION,
	FILE_EA_INFORMATION *);
extern NTSTATUS file_alignment_information(PDEVICE_OBJECT, PIRP,
    PIO_STACK_LOCATION, FILE_ALIGNMENT_INFORMATION *);
extern void file_network_open_information_impl(PDEVICE_OBJECT, PFILE_OBJECT,
    struct vnode *, FILE_NETWORK_OPEN_INFORMATION *, PIO_STATUS_BLOCK);
extern NTSTATUS file_network_open_information(PDEVICE_OBJECT, PIRP,
    PIO_STACK_LOCATION,	FILE_NETWORK_OPEN_INFORMATION *);
extern NTSTATUS file_standard_link_information(PDEVICE_OBJECT, PIRP,
    PIO_STACK_LOCATION,	FILE_STANDARD_LINK_INFORMATION *);
extern NTSTATUS file_id_information(PDEVICE_OBJECT, PIRP, PIO_STACK_LOCATION,
	FILE_ID_INFORMATION *);
extern NTSTATUS file_case_sensitive_information(PDEVICE_OBJECT, PIRP,
    PIO_STACK_LOCATION,	FILE_CASE_SENSITIVE_INFORMATION *);
extern NTSTATUS file_stat_information(PDEVICE_OBJECT, PIRP, PIO_STACK_LOCATION,
	FILE_STAT_INFORMATION *);
extern NTSTATUS file_stat_lx_information(PDEVICE_OBJECT, PIRP,
    PIO_STACK_LOCATION,	FILE_STAT_LX_INFORMATION *);
/*
 * FILE_STAT_BASIC_INFORMATION was added in WDK for Win11 22H2 (NTDDI_WIN11_ZN).
 * Define it ourselves when the build targets an older SDK.
 */
#if !defined(NTDDI_WIN11_ZN) || (NTDDI_VERSION < NTDDI_WIN11_ZN)
typedef struct _FILE_STAT_BASIC_INFORMATION {
	LARGE_INTEGER FileId;
	LARGE_INTEGER CreationTime;
	LARGE_INTEGER LastAccessTime;
	LARGE_INTEGER LastWriteTime;
	LARGE_INTEGER ChangeTime;
	LARGE_INTEGER AllocationSize;
	LARGE_INTEGER EndOfFile;
	ULONG FileAttributes;
	ULONG ReparseTag;
	ULONG NumberOfLinks;
	ULONG DeviceType;
	ULONG DeviceCharacteristics;
	ULONG Reserved;
	LARGE_INTEGER VolumeSerialNumber;
	FILE_ID_128 FileId128;
} FILE_STAT_BASIC_INFORMATION, *PFILE_STAT_BASIC_INFORMATION;
#endif
extern NTSTATUS file_stat_basic_information(PDEVICE_OBJECT, PIRP,
    PIO_STACK_LOCATION,	FILE_STAT_BASIC_INFORMATION *);
extern NTSTATUS file_name_information(PDEVICE_OBJECT, PIRP, PIO_STACK_LOCATION,
	FILE_NAME_INFORMATION *, PULONG usedspace, int normalize);
extern NTSTATUS file_remote_protocol_information(PDEVICE_OBJECT, PIRP,
    PIO_STACK_LOCATION,	FILE_REMOTE_PROTOCOL_INFORMATION *);
extern NTSTATUS file_stream_information(PDEVICE_OBJECT, PIRP,
	PIO_STACK_LOCATION,	FILE_STREAM_INFORMATION *);
extern NTSTATUS file_attribute_tag_information(PDEVICE_OBJECT, PIRP,
    PIO_STACK_LOCATION,	FILE_ATTRIBUTE_TAG_INFORMATION *tag);
extern NTSTATUS file_internal_information(PDEVICE_OBJECT, PIRP,
    PIO_STACK_LOCATION,	FILE_INTERNAL_INFORMATION *infernal);
extern NTSTATUS file_hard_link_information(PDEVICE_OBJECT, PIRP,
    PIO_STACK_LOCATION, FILE_LINKS_INFORMATION *links);

/* IRP_MJ_DEVICE_CONTROL helpers */
extern NTSTATUS QueryCapabilities(PDEVICE_OBJECT, PIRP, PIO_STACK_LOCATION);
extern NTSTATUS QueryDeviceRelations(PDEVICE_OBJECT, PIRP *,
    PIO_STACK_LOCATION);
extern NTSTATUS ioctl_query_device_name(PDEVICE_OBJECT, PIRP,
    PIO_STACK_LOCATION);
extern NTSTATUS ioctl_disk_get_drive_geometry(PDEVICE_OBJECT, PIRP,
    PIO_STACK_LOCATION);
extern NTSTATUS ioctl_disk_get_drive_geometry_ex(PDEVICE_OBJECT, PIRP,
    PIO_STACK_LOCATION);
extern NTSTATUS ioctl_disk_get_partition_info(PDEVICE_OBJECT, PIRP,
    PIO_STACK_LOCATION);
extern NTSTATUS ioctl_disk_get_partition_info_ex(PDEVICE_OBJECT, PIRP,
    PIO_STACK_LOCATION);
extern NTSTATUS ioctl_disk_get_length_info(PDEVICE_OBJECT, PIRP,
    PIO_STACK_LOCATION);
extern NTSTATUS ioctl_volume_is_io_capable(PDEVICE_OBJECT, PIRP,
    PIO_STACK_LOCATION);
extern NTSTATUS ioctl_storage_get_hotplug_info(PDEVICE_OBJECT, PIRP,
    PIO_STACK_LOCATION);
extern NTSTATUS ioctl_storage_query_property(PDEVICE_OBJECT, PIRP,
    PIO_STACK_LOCATION);
extern NTSTATUS ioctl_query_unique_id(PDEVICE_OBJECT, PIRP, PIO_STACK_LOCATION);
extern NTSTATUS ioctl_mountdev_query_suggested_link_name(PDEVICE_OBJECT, PIRP,
    PIO_STACK_LOCATION);
extern NTSTATUS ioctl_mountdev_query_stable_guid(PDEVICE_OBJECT, PIRP,
    PIO_STACK_LOCATION);
extern NTSTATUS ioctl_query_stable_guid(PDEVICE_OBJECT, PIRP,
    PIO_STACK_LOCATION);
extern void strupper(char *s, size_t max);
extern NTSTATUS fsctl_zfs_volume_mountpoint(PDEVICE_OBJECT DeviceObject,
    PIRP Irp, PIO_STACK_LOCATION IrpSp);
extern NTSTATUS fsctl_set_zero_data(PDEVICE_OBJECT DeviceObject, PIRP Irp,
    PIO_STACK_LOCATION IrpSp);
extern NTSTATUS fsctl_get_retrieval_pointers(PDEVICE_OBJECT DeviceObject,
    PIRP Irp, PIO_STACK_LOCATION IrpSp);
extern NTSTATUS ioctl_get_gpt_attributes(PDEVICE_OBJECT DeviceObject, PIRP Irp,
    PIO_STACK_LOCATION IrpSp);
extern NTSTATUS volume_read(PDEVICE_OBJECT DeviceObject, PIRP Irp,
    PIO_STACK_LOCATION IrpSp);


#endif
