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
#define SYS_WINDOWS_H_INCLUDED


#include <sys/mount.h>

extern PDEVICE_OBJECT ioctlDeviceObject;
extern PDEVICE_OBJECT fsDiskDeviceObject;

#define ZFS_SERIAL (ULONG)'wZFS'
#define VOLUME_LABEL			L"ZFS"
DECLARE_GLOBAL_CONST_UNICODE_STRING(ZFSVolumeName, VOLUME_LABEL);



// We have to remember "query directory" related items, like index and
// search pattern. This is attached in IRP_MJ_CREATE to fscontext2
#define ZFS_DIRLIST_MAGIC 0x6582feac
struct zfs_dirlist {
	uint32_t magic;				// Identifier
	uint32_t dir_eof;			// Directory listing completed?
	uint64_t uio_offset;		// Directory list offset
	uint64_t ea_index;			// EA list offset
	uint32_t deleteonclose;		// Marked for deletion
	uint32_t ContainsWildCards;      // searchname has wildcards
	UNICODE_STRING searchname;  // Search pattern
};

typedef struct zfs_dirlist zfs_dirlist_t;

extern uint64_t zfs_module_busy;

extern CACHE_MANAGER_CALLBACKS CacheManagerCallbacks;

extern NTSTATUS dev_ioctl(PDEVICE_OBJECT DeviceObject, ULONG ControlCode, PVOID InputBuffer, ULONG InputBufferSize,
	PVOID OutputBuffer, ULONG OutputBufferSize, BOOLEAN Override, IO_STATUS_BLOCK* iosb);

extern int zfs_windows_mount(zfs_cmd_t *zc);
extern int zfs_windows_unmount(zfs_cmd_t *zc);
extern NTSTATUS zfsdev_ioctl(PDEVICE_OBJECT DeviceObject, PIRP Irp, int flag);
extern void zfs_windows_vnops_callback(PDEVICE_OBJECT deviceObject);
extern void zfs_send_notify(zfsvfs_t *zfsvfs, char *name, int, ULONG FilterMatch, ULONG Action);
extern void zfs_send_notify_stream(zfsvfs_t *, char *, int, ULONG, ULONG, char *stream);
extern void zfs_set_security(struct vnode *vp, struct vnode *dvp);
extern uint64_t zfs_sid2uid(SID *sid);

BOOLEAN vattr_apply_lx_ea(vattr_t *vap, PFILE_FULL_EA_INFORMATION ea);
NTSTATUS vnode_apply_eas(struct vnode *vp, PFILE_FULL_EA_INFORMATION eas, ULONG eaLength, PULONG pEaErrorOffset);

/* Main function to handle all VFS "vnops" */
 extern _Function_class_(DRIVER_DISPATCH) NTSTATUS
    dispatcher(_In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp);

extern NTSTATUS zfsdev_open(dev_t dev, PIRP Irp);
extern NTSTATUS zfsdev_release(dev_t dev, PIRP Irp);

extern int	zfs_vnop_recycle(znode_t *zp, int force);
extern uint64_t zfs_blksz(znode_t *zp);

extern int	zfs_vnop_mount(PDEVICE_OBJECT DiskDevice, PIRP Irp, PIO_STACK_LOCATION IrpSp);

extern int	zfs_build_path(znode_t *start_zp, znode_t *start_parent, char **fullpath, uint32_t *returnsize, uint32_t *start_zp_offset);

extern int	xattr_protected(char *name);
extern int	xattr_stream(char *name);
extern uint64_t xattr_getsize(struct vnode *vp);
extern char *major2str(int major, int minor);
extern char *common_status_str(NTSTATUS Status);
extern char *create_options(ULONG options);
extern char *create_reply(NTSTATUS, ULONG reply);

/* zfs_vnop_windows_lib.h */
extern int	AsciiStringToUnicodeString(char *in, PUNICODE_STRING out);
extern void	FreeUnicodeString(PUNICODE_STRING s);
extern int	zfs_vfs_uuid_gen(const char *osname, uuid_t uuid);
extern int	zfs_vfs_uuid_unparse(uuid_t uuid, char *dst);
extern int	zfs_vnop_ioctl_fullfsync(struct vnode *, vfs_context_t *, zfsvfs_t *);
extern int	zfs_setwinflags(znode_t *zp, uint32_t winflags);
extern uint32_t zfs_getwinflags(znode_t *zp);
extern NTSTATUS zfs_setunlink(FILE_OBJECT *fo, vnode_t *dvp);
extern int zfs_find_dvp_vp(zfsvfs_t *, char *, int finalpartmaynotexist,
	int finalpartmustnotexist, char **lastname, struct vnode **dvpp,
	struct vnode **vpp, int flags);

/* IRP_MJ_SET_INFORMATION helpers */
extern NTSTATUS file_disposition_information(PDEVICE_OBJECT, PIRP, PIO_STACK_LOCATION);
extern NTSTATUS file_disposition_information_ex(PDEVICE_OBJECT, PIRP, PIO_STACK_LOCATION);
extern NTSTATUS file_endoffile_information(PDEVICE_OBJECT, PIRP, PIO_STACK_LOCATION);
extern NTSTATUS file_link_information(PDEVICE_OBJECT, PIRP, PIO_STACK_LOCATION);
extern NTSTATUS file_rename_information(PDEVICE_OBJECT, PIRP, PIO_STACK_LOCATION);

/* IRP_MJ_GET_INFORMATION helpers */
extern NTSTATUS file_basic_information(PDEVICE_OBJECT, PIRP, PIO_STACK_LOCATION,
	FILE_BASIC_INFORMATION *);
extern NTSTATUS file_standard_information(PDEVICE_OBJECT, PIRP, PIO_STACK_LOCATION,
	FILE_STANDARD_INFORMATION *);
extern NTSTATUS file_position_information(PDEVICE_OBJECT, PIRP, PIO_STACK_LOCATION,
	FILE_POSITION_INFORMATION *);
extern NTSTATUS file_ea_information(PDEVICE_OBJECT, PIRP, PIO_STACK_LOCATION,
	FILE_EA_INFORMATION *);
extern NTSTATUS file_network_open_information(PDEVICE_OBJECT, PIRP, PIO_STACK_LOCATION,
	FILE_NETWORK_OPEN_INFORMATION *);
extern NTSTATUS file_standard_link_information(PDEVICE_OBJECT, PIRP, PIO_STACK_LOCATION,
	FILE_STANDARD_LINK_INFORMATION *);
extern NTSTATUS file_id_information(PDEVICE_OBJECT, PIRP, PIO_STACK_LOCATION,
	FILE_ID_INFORMATION *);
extern NTSTATUS file_case_sensitive_information(PDEVICE_OBJECT, PIRP, PIO_STACK_LOCATION,
	FILE_CASE_SENSITIVE_INFORMATION *);
extern NTSTATUS file_stat_information(PDEVICE_OBJECT, PIRP, PIO_STACK_LOCATION,
	FILE_STAT_INFORMATION *);
extern NTSTATUS file_stat_lx_information(PDEVICE_OBJECT, PIRP, PIO_STACK_LOCATION,
	FILE_STAT_LX_INFORMATION *);
extern NTSTATUS file_name_information(PDEVICE_OBJECT, PIRP, PIO_STACK_LOCATION,
	FILE_NAME_INFORMATION *, PULONG usedspace, int normalize);
extern NTSTATUS file_remote_protocol_information(PDEVICE_OBJECT, PIRP, PIO_STACK_LOCATION,
	FILE_REMOTE_PROTOCOL_INFORMATION *);
extern NTSTATUS file_stream_information(PDEVICE_OBJECT, PIRP, PIO_STACK_LOCATION,
	FILE_STREAM_INFORMATION *, PULONG usedspace);
extern NTSTATUS file_attribute_tag_information(PDEVICE_OBJECT, PIRP, PIO_STACK_LOCATION,
	FILE_ATTRIBUTE_TAG_INFORMATION *tag);
extern NTSTATUS file_internal_information(PDEVICE_OBJECT, PIRP, PIO_STACK_LOCATION,
	FILE_INTERNAL_INFORMATION *infernal);

/* IRP_MJ_DEVICE_CONTROL helpers */
extern NTSTATUS QueryCapabilities(PDEVICE_OBJECT, PIRP, PIO_STACK_LOCATION);
extern NTSTATUS ioctl_query_device_name(PDEVICE_OBJECT, PIRP, PIO_STACK_LOCATION);
extern NTSTATUS ioctl_disk_get_drive_geometry(PDEVICE_OBJECT, PIRP, PIO_STACK_LOCATION);
extern NTSTATUS ioctl_disk_get_drive_geometry_ex(PDEVICE_OBJECT, PIRP, PIO_STACK_LOCATION);
extern NTSTATUS ioctl_disk_get_partition_info(PDEVICE_OBJECT, PIRP, PIO_STACK_LOCATION);
extern NTSTATUS ioctl_disk_get_partition_info_ex(PDEVICE_OBJECT, PIRP, PIO_STACK_LOCATION);
extern NTSTATUS ioctl_disk_get_length_info(PDEVICE_OBJECT, PIRP, PIO_STACK_LOCATION);
extern NTSTATUS ioctl_volume_is_io_capable(PDEVICE_OBJECT, PIRP, PIO_STACK_LOCATION);
extern NTSTATUS ioctl_storage_get_hotplug_info(PDEVICE_OBJECT, PIRP, PIO_STACK_LOCATION);
extern NTSTATUS ioctl_storage_query_property(PDEVICE_OBJECT, PIRP, PIO_STACK_LOCATION);
extern NTSTATUS ioctl_query_unique_id(PDEVICE_OBJECT, PIRP, PIO_STACK_LOCATION);
extern NTSTATUS ioctl_mountdev_query_suggested_link_name(PDEVICE_OBJECT, PIRP, PIO_STACK_LOCATION);
extern NTSTATUS ioctl_mountdev_query_stable_guid(PDEVICE_OBJECT, PIRP, PIO_STACK_LOCATION);
extern NTSTATUS ioctl_query_stable_guid(PDEVICE_OBJECT, PIRP, PIO_STACK_LOCATION);


#endif
