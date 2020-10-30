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
#define INITGUID
//#include <ntdef.h>
//#include <wdm.h>
#include <Ntifs.h>
#include <intsafe.h>
#include <ntddvol.h>
//#include <ntddstor.h>
#include <ntdddisk.h>
//#include <wdmguid.h>
#include <mountmgr.h>
#include <sys/cred.h>
#include <sys/vnode.h>
#include <sys/zfs_dir.h>
#include <sys/zfs_ioctl.h>
#include <sys/fs/zfs.h>
#include <sys/dmu.h>
#include <sys/dmu_objset.h>
#include <sys/spa.h>
#include <sys/txg.h>
#include <sys/dbuf.h>
#include <sys/zap.h>
#include <sys/sa.h>
#include <sys/zfs_vnops.h>
#include <sys/stat.h>

#include <sys/unistd.h>
//#include <sys/xattr.h>
#include <sys/uuid.h>
//#include <sys/utfconv.h>

#include <sys/types.h>
#include <sys/w32_types.h>
#include <sys/zfs_mount.h>

#include <sys/zfs_windows.h>


#include <mountmgr.h>
#include <Mountdev.h>
#include <ntddvol.h>
#include <Storduid.h>

#undef _NTDDK_

typedef struct {
	UCHAR revision;
	UCHAR elements;
	UCHAR auth[6];
	UINT32 nums[8];
} sid_header;

static sid_header sid_BA = { 1, 2, SECURITY_NT_AUTHORITY, {32, 544} }; // BUILTIN\Administrators
static sid_header sid_SY = { 1, 1, SECURITY_NT_AUTHORITY, {18} };      // NT AUTHORITY\SYSTEM
static sid_header sid_BU = { 1, 2, SECURITY_NT_AUTHORITY, {32, 545} }; // BUILTIN\Users
static sid_header sid_AU = { 1, 1, SECURITY_NT_AUTHORITY, {11} };      // NT AUTHORITY\Authenticated Users

static sid_header sid_MH = { 1, 1, SECURITY_MANDATORY_LABEL_AUTHORITY, {12288} }; // MandatoryLevel\High
static sid_header sid_ML = { 1, 1, SECURITY_MANDATORY_LABEL_AUTHORITY, {4096} }; // MandatoryLevel\Low

typedef struct {
	UCHAR flags;
	ACCESS_MASK mask;
	sid_header* sid;
} dacl;

/*

Brand new ntfs:
F:\ BUILTIN\Administrators:(F)
	BUILTIN\Administrators:(OI)(CI)(IO)(F)
	NT AUTHORITY\SYSTEM:(F)
	NT AUTHORITY\SYSTEM:(OI)(CI)(IO)(F)
	NT AUTHORITY\Authenticated Users:(M)
	NT AUTHORITY\Authenticated Users:(OI)(CI)(IO)(M)
	BUILTIN\Users:(RX)
	BUILTIN\Users:(OI)(CI)(IO)(GR,GE)
*/
static dacl def_dacls[] = {
	// BUILTIN\Administrators:(F)
	{ 0, FILE_ALL_ACCESS, &sid_BA },
	// BUILTIN\Administrators:(OI)(CI)(IO)(F)
	{ OBJECT_INHERIT_ACE | CONTAINER_INHERIT_ACE | INHERIT_ONLY_ACE, FILE_ALL_ACCESS, &sid_BA },
	// NT AUTHORITY\SYSTEM:(F)
	{ 0, FILE_ALL_ACCESS, &sid_SY },
	// NT AUTHORITY\SYSTEM:(OI)(CI)(IO)(F)
	{ OBJECT_INHERIT_ACE | CONTAINER_INHERIT_ACE | INHERIT_ONLY_ACE, FILE_ALL_ACCESS, &sid_SY },
	// NT AUTHORITY\Authenticated Users:(M)
	{ 0, FILE_GENERIC_READ | FILE_GENERIC_WRITE | DELETE | FILE_GENERIC_EXECUTE, &sid_AU },
	// NT AUTHORITY\Authenticated Users:(OI)(CI)(IO)(M)
	{ OBJECT_INHERIT_ACE | CONTAINER_INHERIT_ACE | INHERIT_ONLY_ACE, FILE_GENERIC_READ | FILE_GENERIC_WRITE | DELETE | FILE_GENERIC_EXECUTE, &sid_AU },
	// BUILTIN\Users:(RX)
	{ 0, FILE_GENERIC_READ | FILE_GENERIC_EXECUTE, &sid_BU },
	// BUILTIN\Users:(OI)(CI)(IO)(GR,GE)
	{ OBJECT_INHERIT_ACE | CONTAINER_INHERIT_ACE | INHERIT_ONLY_ACE, GENERIC_READ | GENERIC_EXECUTE, &sid_BU },
#if 0 // C: only?
	// Mandatory Label\High Mandatory Level:(OI)(NP)(IO)(NW)
	{ OBJECT_INHERIT_ACE | NO_PROPAGATE_INHERIT_ACE | INHERIT_ONLY_ACE, SYSTEM_MANDATORY_LABEL_NO_WRITE_UP, &sid_MH },
#endif
	// END
	{ 0, 0, NULL }
};

//#define USE_RECYCLE_ACL
#ifdef USE_RECYCLE_ACL
/*
Brand new $Recycle.bin

Owner: WDKRemoteUser
Group: None

F : \$Recycle.bin BUILTIN\Administrators:(I)(F)
	NT AUTHORITY\SYSTEM : (I)(F)
	NT AUTHORITY\Authenticated Users : (I)(M)
	BUILTIN\Users : (I)(RX)
*/
static dacl recycle_dacls[] = {
	// BUILTIN\Administrators:(I)(F)
	{ INHERITED_ACE, FILE_ALL_ACCESS, &sid_BA },
	// NT AUTHORITY\SYSTEM : (I)(F)
	{ INHERITED_ACE, FILE_ALL_ACCESS, &sid_SY },
	// NT AUTHORITY\Authenticated Users : (I)(M)
	{ INHERITED_ACE, FILE_GENERIC_READ | FILE_GENERIC_WRITE | DELETE | FILE_GENERIC_EXECUTE, &sid_AU },
	// BUILTIN\Users : (I)(RX)
	{ INHERITED_ACE, FILE_GENERIC_READ | FILE_GENERIC_EXECUTE, &sid_BU },
	// END
	{ 0, 0, NULL }
};
#endif 

char *major2str(int major, int minor)
{
	switch (major) {
	case IRP_MJ_CREATE:
		return "IRP_MJ_CREATE";
	case IRP_MJ_CREATE_NAMED_PIPE:
		return "IRP_MJ_CREATE_NAMED_PIPE";
	case IRP_MJ_CLOSE:
		return "IRP_MJ_CLOSE";
	case IRP_MJ_READ:
		return "IRP_MJ_READ";
	case IRP_MJ_WRITE:
		return "IRP_MJ_WRITE";
	case IRP_MJ_QUERY_INFORMATION:
		return "IRP_MJ_QUERY_INFORMATION";
	case IRP_MJ_SET_INFORMATION:
		return "IRP_MJ_SET_INFORMATION";
	case IRP_MJ_QUERY_EA:
		return "IRP_MJ_QUERY_EA";
	case IRP_MJ_SET_EA:
		return "IRP_MJ_SET_EA";
	case IRP_MJ_FLUSH_BUFFERS:
		return "IRP_MJ_FLUSH_BUFFERS";
	case IRP_MJ_QUERY_VOLUME_INFORMATION:
		return "IRP_MJ_QUERY_VOLUME_INFORMATION";
	case IRP_MJ_SET_VOLUME_INFORMATION:
		return "IRP_MJ_SET_VOLUME_INFORMATION";
	case IRP_MJ_DIRECTORY_CONTROL:
		switch (minor) {
		case IRP_MN_NOTIFY_CHANGE_DIRECTORY:
			return "IRP_MJ_DIRECTORY_CONTROL(IRP_MN_NOTIFY_CHANGE_DIRECTORY)";
		case IRP_MN_QUERY_DIRECTORY:
			return "IRP_MJ_DIRECTORY_CONTROL(IRP_MN_QUERY_DIRECTORY)";
		}
		return "IRP_MJ_DIRECTORY_CONTROL";
	case IRP_MJ_FILE_SYSTEM_CONTROL:
		switch (minor) {
		case IRP_MN_KERNEL_CALL:
			return "IRP_MJ_FILE_SYSTEM_CONTROL(IRP_MN_KERNEL_CALL)";
		case IRP_MN_MOUNT_VOLUME:
			return "IRP_MJ_FILE_SYSTEM_CONTROL(IRP_MN_MOUNT_VOLUME)";
		case IRP_MN_USER_FS_REQUEST:
			return "IRP_MJ_FILE_SYSTEM_CONTROL(IRP_MN_USER_FS_REQUEST)";
		case IRP_MN_VERIFY_VOLUME:
			return "IRP_MJ_FILE_SYSTEM_CONTROL(IRP_MN_VERIFY_VOLUME)";
		case IRP_MN_LOAD_FILE_SYSTEM:
			return "IRP_MJ_FILE_SYSTEM_CONTROL(IRP_MN_LOAD_FILE_SYSTEM)";
		}
		return "IRP_MJ_FILE_SYSTEM_CONTROL";
	case IRP_MJ_DEVICE_CONTROL:
		return "IRP_MJ_DEVICE_CONTROL";
	case IRP_MJ_INTERNAL_DEVICE_CONTROL:
		return "IRP_MJ_INTERNAL_DEVICE_CONTROL";
	case IRP_MJ_SHUTDOWN:
		return "IRP_MJ_SHUTDOWN";
	case IRP_MJ_LOCK_CONTROL:
		switch (minor) {
		case IRP_MN_LOCK:
			return "IRP_MJ_LOCK_CONTROL(IRP_MN_LOCK)";
		case IRP_MN_UNLOCK_ALL:
			return "IRP_MJ_LOCK_CONTROL(IRP_MN_UNLOCK_ALL)";
		case IRP_MN_UNLOCK_ALL_BY_KEY:
			return "IRP_MJ_LOCK_CONTROL(IRP_MN_UNLOCK_ALL_BY_KEY)";
		case IRP_MN_UNLOCK_SINGLE:
			return "IRP_MJ_LOCK_CONTROL(IRP_MN_UNLOCK_SINGLE)";
		}
		return "IRP_MJ_LOCK_CONTROL";
	case IRP_MJ_CLEANUP:
		return "IRP_MJ_CLEANUP";
	case IRP_MJ_CREATE_MAILSLOT:
		return "IRP_MJ_CREATE_MAILSLOT";
	case IRP_MJ_QUERY_SECURITY:
		return "IRP_MJ_QUERY_SECURITY";
	case IRP_MJ_SET_SECURITY:
		return "IRP_MJ_SET_SECURITY";
	case IRP_MJ_POWER:
		return "IRP_MJ_POWER";
	case IRP_MJ_SYSTEM_CONTROL:
		return "IRP_MJ_SYSTEM_CONTROL";
	case IRP_MJ_DEVICE_CHANGE:
		return "IRP_MJ_DEVICE_CHANGE";
	case IRP_MJ_QUERY_QUOTA:
		return "IRP_MJ_QUERY_QUOTA";
	case IRP_MJ_SET_QUOTA:
		return "IRP_MJ_SET_QUOTA";
	case IRP_MJ_PNP:
		switch (minor) {
		case IRP_MN_START_DEVICE:
			return "IRP_MJ_PNP(IRP_MN_START_DEVICE)";
		case IRP_MN_QUERY_REMOVE_DEVICE:
			return "IRP_MJ_PNP(IRP_MN_QUERY_REMOVE_DEVICE)";
		case IRP_MN_REMOVE_DEVICE:
			return "IRP_MJ_PNP(IRP_MN_REMOVE_DEVICE)";
		case IRP_MN_CANCEL_REMOVE_DEVICE:
			return "IRP_MJ_PNP(IRP_MN_CANCEL_REMOVE_DEVICE)";
		case IRP_MN_STOP_DEVICE:
			return "IRP_MJ_PNP(IRP_MN_STOP_DEVICE)";
		case IRP_MN_QUERY_STOP_DEVICE:
			return "IRP_MJ_PNP(IRP_MN_QUERY_STOP_DEVICE)";
		case IRP_MN_CANCEL_STOP_DEVICE:
			return "IRP_MJ_PNP(IRP_MN_CANCEL_STOP_DEVICE)";
		case IRP_MN_QUERY_DEVICE_RELATIONS:
			return "IRP_MJ_PNP(IRP_MN_QUERY_DEVICE_RELATIONS)";
		case IRP_MN_QUERY_INTERFACE:
			return "IRP_MJ_PNP(IRP_MN_QUERY_INTERFACE)";
		case IRP_MN_QUERY_RESOURCES:
			return "IRP_MJ_PNP(IRP_MN_QUERY_RESOURCES)";
		case IRP_MN_QUERY_RESOURCE_REQUIREMENTS:
			return "IRP_MJ_PNP(IRP_MN_QUERY_RESOURCE_REQUIREMENTS)";
		case IRP_MN_QUERY_CAPABILITIES:
			return "IRP_MJ_PNP(IRP_MN_QUERY_CAPABILITIES)";
		case IRP_MN_QUERY_DEVICE_TEXT:
			return "IRP_MJ_PNP(IRP_MN_QUERY_DEVICE_TEXT)";
		case IRP_MN_FILTER_RESOURCE_REQUIREMENTS:
			return "IRP_MJ_PNP(IRP_MN_FILTER_RESOURCE_REQUIREMENTS)";
		case IRP_MN_READ_CONFIG:
			return "IRP_MJ_PNP(IRP_MN_READ_CONFIG)";
		case IRP_MN_WRITE_CONFIG:
			return "IRP_MJ_PNP(IRP_MN_WRITE_CONFIG)";
		case IRP_MN_EJECT:
			return "IRP_MJ_PNP(IRP_MN_EJECT)";
		case IRP_MN_SET_LOCK:
			return "IRP_MJ_PNP(IRP_MN_SET_LOCK)";
		case IRP_MN_QUERY_ID:
			return "IRP_MJ_PNP(IRP_MN_QUERY_ID)";
		case IRP_MN_QUERY_PNP_DEVICE_STATE:
			return "IRP_MJ_PNP(IRP_MN_QUERY_PNP_DEVICE_STATE)";
		case IRP_MN_QUERY_BUS_INFORMATION:
			return "IRP_MJ_PNP(IRP_MN_QUERY_BUS_INFORMATION)";
		case IRP_MN_DEVICE_USAGE_NOTIFICATION:
			return "IRP_MJ_PNP(IRP_MN_DEVICE_USAGE_NOTIFICATION)";
		case IRP_MN_SURPRISE_REMOVAL: // SUPPLIES!
			return "IRP_MJ_PNP(IRP_MN_SURPRISE_REMOVAL)";
		}
		return "IRP_MJ_PNP";
	default:
		break;
	}
	return "Unknown";
}

char *common_status_str(NTSTATUS Status)
{
	switch (Status) {
	case STATUS_SUCCESS:
		return "OK";
	case STATUS_BUFFER_OVERFLOW:
		return "Overflow";
	case STATUS_END_OF_FILE:
		return "EOF";
	case STATUS_NO_MORE_FILES:
		return "NoMoreFiles";
	case STATUS_OBJECT_PATH_NOT_FOUND:
		return "ObjectPathNotFound";
	case STATUS_NO_SUCH_FILE:
		return "NoSuchFile";
	case STATUS_ACCESS_DENIED:
		return "AccessDenied";
	case STATUS_NOT_IMPLEMENTED:
		return "NotImplemented";
	case STATUS_PENDING:
		return "STATUS_PENDING";
	case STATUS_INVALID_PARAMETER:
		return "STATUS_INVALID_PARAMETER";
	case STATUS_OBJECT_NAME_NOT_FOUND:
		return "STATUS_OBJECT_NAME_NOT_FOUND";
	case STATUS_OBJECT_NAME_COLLISION:
		return "STATUS_OBJECT_NAME_COLLISION";
	case STATUS_FILE_IS_A_DIRECTORY:
		return "STATUS_FILE_IS_A_DIRECTORY";
	case STATUS_NOT_A_REPARSE_POINT:
		return "STATUS_NOT_A_REPARSE_POINT";
	case STATUS_NOT_FOUND:
		return "STATUS_NOT_FOUND";
	case STATUS_NO_MORE_EAS:
		return "STATUS_NO_MORE_EAS";
	case STATUS_NO_EAS_ON_FILE:
		return "STATUS_NO_EAS_ON_FILE";
	default:
		return "<*****>";
	}
}

char *create_options(ULONG Options)
{
	static char out[256];

	BOOLEAN CreateDirectory;
	BOOLEAN OpenDirectory;
	BOOLEAN CreateFile;
	ULONG CreateDisposition;

	out[0] = 0;

	BOOLEAN DirectoryFile;
	DirectoryFile = BooleanFlagOn(Options, FILE_DIRECTORY_FILE);

	if (BooleanFlagOn(Options, FILE_DIRECTORY_FILE))
		strncat(out, "DirectoryFile ", sizeof(out));
	if (BooleanFlagOn(Options, FILE_NON_DIRECTORY_FILE))
		strncat(out, "NonDirectoryFile ", sizeof(out));
	if (BooleanFlagOn(Options, FILE_NO_INTERMEDIATE_BUFFERING))
		strncat(out, "NoIntermediateBuffering ", sizeof(out));
	if (BooleanFlagOn(Options, FILE_NO_EA_KNOWLEDGE))
		strncat(out, "NoEaKnowledge ", sizeof(out));
	if (BooleanFlagOn(Options, FILE_DELETE_ON_CLOSE))
		strncat(out, "DeleteOnClose ", sizeof(out));
	if (BooleanFlagOn(Options, FILE_OPEN_BY_FILE_ID))
		strncat(out, "FileOpenByFileId ", sizeof(out));

	CreateDisposition = (Options >> 24) & 0x000000ff;

	switch (CreateDisposition) {
	case FILE_SUPERSEDE:
		strncat(out, "@FILE_SUPERSEDE ", sizeof(out));
		break;
	case FILE_CREATE:
		strncat(out, "@FILE_CREATE ", sizeof(out));
		break;
	case FILE_OPEN:
		strncat(out, "@FILE_OPEN ", sizeof(out));
		break;
	case FILE_OPEN_IF:
		strncat(out, "@FILE_OPEN_IF ", sizeof(out));
		break;
	case FILE_OVERWRITE:
		strncat(out, "@FILE_OVERWRITE ", sizeof(out));
		break;
	case FILE_OVERWRITE_IF:
		strncat(out, "@FILE_OVERWRITE_IF ", sizeof(out));
		break;
	}

	CreateDirectory = (BOOLEAN)(DirectoryFile &&
		((CreateDisposition == FILE_CREATE) ||
		(CreateDisposition == FILE_OPEN_IF)));

	OpenDirectory = (BOOLEAN)(DirectoryFile &&
		((CreateDisposition == FILE_OPEN) ||
		(CreateDisposition == FILE_OPEN_IF)));

	CreateFile = (BOOLEAN)(
		((CreateDisposition == FILE_CREATE) ||
		(CreateDisposition == FILE_OPEN_IF) ||
			(CreateDisposition == FILE_SUPERSEDE) ||
			(CreateDisposition == FILE_OVERWRITE_IF)));
	if (CreateDirectory)
		strncat(out, "#CreateDirectory ", sizeof(out));
	if (OpenDirectory)
		strncat(out, "#OpenDirectory ", sizeof(out));
	if (CreateFile)
		strncat(out, "#CreateFile ", sizeof(out));

	return out;
}

char *create_reply(NTSTATUS status, ULONG reply)
{
	switch (reply) {
	case FILE_SUPERSEDED:
		return "FILE_SUPERSEDED";
	case FILE_OPENED:
		return "FILE_OPENED";
	case FILE_CREATED:
		return "FILE_CREATED";
	case FILE_OVERWRITTEN:
		return "FILE_OVERWRITTEN";
	case FILE_EXISTS:
		return "FILE_EXISTS";
	case FILE_DOES_NOT_EXIST:
		return "FILE_DOES_NOT_EXIST";
	default:
		if (status == STATUS_REPARSE)
			return "ReparseTag";
		return "FileUnknown";
	}
}

int AsciiStringToUnicodeString(char *in, PUNICODE_STRING out)
{
	ANSI_STRING conv;
	conv.Buffer = in;
	conv.Length = strlen(in);
	conv.MaximumLength = PATH_MAX;
	return RtlAnsiStringToUnicodeString(out, &conv, TRUE);
}

void FreeUnicodeString(PUNICODE_STRING s)
{
	if (s->Buffer) ExFreePool(s->Buffer);
	s->Buffer = NULL;
}

int
zfs_vnop_ioctl_fullfsync(struct vnode *vp, vfs_context_t *ct, zfsvfs_t *zfsvfs)
{
	int error;

	error = zfs_fsync(VTOZ(vp), /*syncflag*/0, NULL);
	if (error)
		return (error);

	if (zfsvfs->z_log != NULL)
		zil_commit(zfsvfs->z_log, 0);
	else
		txg_wait_synced(dmu_objset_pool(zfsvfs->z_os), 0);
	return (0);
}

uint32_t
zfs_getwinflags(znode_t *zp)
{
	uint32_t  winflags = 0;
    uint64_t zflags=zp->z_pflags;

	if (zflags & ZFS_HIDDEN)
		winflags |= FILE_ATTRIBUTE_HIDDEN;
	if (zflags & ZFS_SYSTEM)
		winflags |= FILE_ATTRIBUTE_SYSTEM;
	if (zflags & ZFS_ARCHIVE)
		winflags |= FILE_ATTRIBUTE_ARCHIVE;
	if (zflags & ZFS_READONLY)
		winflags |= FILE_ATTRIBUTE_READONLY;
	if (zflags & ZFS_REPARSE)
		winflags |= FILE_ATTRIBUTE_REPARSE_POINT;

	if (S_ISDIR(zp->z_mode)) {
		winflags |= FILE_ATTRIBUTE_DIRECTORY;
		winflags &= ~FILE_ATTRIBUTE_ARCHIVE;
	}

	if (winflags == 0)
		winflags = FILE_ATTRIBUTE_NORMAL;

	dprintf("%s: changing zfs 0x%08llx to win 0x%08lx\n", __func__,
           zflags, winflags);
	return (winflags);
}

int 
zfs_setwinflags(znode_t *zp, uint32_t winflags)
{
    uint64_t zflags = 0;

	zflags = zp->z_pflags;

	if (winflags & FILE_ATTRIBUTE_HIDDEN)
		zflags |= ZFS_HIDDEN;
	else
		zflags &= ~ZFS_HIDDEN;

	if (winflags & FILE_ATTRIBUTE_SYSTEM)
		zflags |= ZFS_SYSTEM;
	else
		zflags &= ~ZFS_SYSTEM;

	if (winflags & FILE_ATTRIBUTE_ARCHIVE)
		zflags |= ZFS_ARCHIVE;
	else
		zflags &= ~ZFS_ARCHIVE;

	if (winflags & FILE_ATTRIBUTE_READONLY)
		zflags |= ZFS_READONLY;
	else
		zflags &= ~ZFS_READONLY;

	if (zp->z_pflags != zflags) {
		zp->z_pflags = zflags;
		dprintf("%s changing win 0x%08lx to zfs 0x%08llx\n", __func__,
			winflags, zflags);
		return 1;
	}

	return 0;
}

// WSL uses special EAs to interact with uid/gid/mode/device major/minor
// Returns: TRUE if the EA was stored in the vattr.
BOOLEAN vattr_apply_lx_ea(vattr_t *vap, PFILE_FULL_EA_INFORMATION ea)
{
	BOOLEAN setVap = FALSE;

	if (ea->EaNameLength != 6 || strncmp(ea->EaName, "$LX", 3) != 0)
		return FALSE;

	void *eaValue = &ea->EaName[0] + ea->EaNameLength + 1;
	if (strncmp(ea->EaName, LX_FILE_METADATA_UID_EA_NAME, ea->EaNameLength) == 0) {
		vap->va_uid = *(PUINT32)eaValue;
		vap->va_active |= ATTR_UID;
		setVap = TRUE;
	} else if (strncmp(ea->EaName, LX_FILE_METADATA_GID_EA_NAME, ea->EaNameLength) == 0) {
		vap->va_gid = *(PUINT32)eaValue;
		vap->va_active |= ATTR_GID;
		setVap = TRUE;
	} else if (strncmp(ea->EaName, LX_FILE_METADATA_MODE_EA_NAME, ea->EaNameLength) == 0) {
		vap->va_mode = *(PUINT32)eaValue;
		vap->va_active |= ATTR_MODE;
		setVap = TRUE;
	} else if (strncmp(ea->EaName, LX_FILE_METADATA_DEVICE_ID_EA_NAME, ea->EaNameLength) == 0) {
		UINT32 *vu32 = (UINT32*)eaValue;
		vap->va_rdev = makedev(vu32[0], vu32[1]);
		vap->va_active |= VNODE_ATTR_va_rdev;
		setVap = TRUE;
	}
	return setVap;
}

static int vnode_apply_single_ea(struct vnode *vp, struct vnode *xdvp, FILE_FULL_EA_INFORMATION *ea)
{
	int error;
	struct vnode *xvp = NULL;

	dprintf("%s: xattr '%.*s' valuelen %u\n", __func__,
		ea->EaNameLength, ea->EaName, ea->EaValueLength);

	if (ea->EaValueLength == 0) {

		// Remove EA
		error = zfs_remove(VTOZ(xdvp), ea->EaName, NULL, /* flags */0);

	} else {
		// Add replace EA

		error = zfs_obtain_xattr(VTOZ(xdvp), ea->EaName, VTOZ(vp)->z_mode, NULL,
			&xvp, 0);
		if (error)
			goto out;

		/* Truncate, if it was existing */
		error = zfs_freesp(VTOZ(xvp), 0, 0, VTOZ(vp)->z_mode, TRUE);

		/* Write data */
		uio_t *uio;
		uio = uio_create(1, 0, UIO_SYSSPACE, UIO_WRITE);
		uio_addiov(uio, ea->EaName + ea->EaNameLength + 1, ea->EaValueLength);
		error = zfs_write(xvp, uio, 0, NULL);
		uio_free(uio);
	}

out:
	if (xvp != NULL)
		VN_RELE(xvp);

	return error;
}


/*
 * Apply a set of EAs to a vnode, while handling special Windows EAs that set UID/GID/Mode/rdev.
 */
NTSTATUS vnode_apply_eas(struct vnode *vp, PFILE_FULL_EA_INFORMATION eas, ULONG eaLength, PULONG pEaErrorOffset)
{
	NTSTATUS Status = STATUS_SUCCESS;

	if (vp == NULL || eas == NULL) return STATUS_INVALID_PARAMETER;

	// Optional: Check for validity if the caller wants it.
	if (pEaErrorOffset != NULL) {
		Status = IoCheckEaBufferValidity(eas, eaLength, pEaErrorOffset);
		if (!NT_SUCCESS(Status)) {
			dprintf("%s: failed validity: 0x%x\n", __func__, Status);
			return Status;
		}
	}

	struct vnode *xdvp = NULL;
	vattr_t vap = { 0 };
	int error;
	PFILE_FULL_EA_INFORMATION ea;
	for (ea = eas; ; ea = (PFILE_FULL_EA_INFORMATION)((uint8_t*)ea + ea->NextEntryOffset)) {
		if (vattr_apply_lx_ea(&vap, ea)) {
			dprintf("  encountered special attrs EA '%.*s'\n", ea->EaNameLength, ea->EaName);
		} else {
			// optimization: defer creating an xattr dir until the first standard EA
			if (xdvp == NULL) {
				// Open (or Create) the xattr directory
				if (zfs_get_xattrdir(VTOZ(vp), &xdvp, NULL, CREATE_XATTR_DIR) != 0) {
					Status = STATUS_EA_CORRUPT_ERROR;
					goto out;
				}
			}
			error = vnode_apply_single_ea(vp, xdvp, ea);
			if (error != 0) dprintf("  failed to process xattr: %d\n", error);
		}

		if (ea->NextEntryOffset == 0)
			break;
	}

	// We should perhaps translate some of the "error" codes we can
	// get here, into Status return values. Currently, all errors are
	// masked, and we always return OK.

	// Update zp based on LX eas.
	if (vap.va_active != 0)
		zfs_setattr(VTOZ(vp), &vap, 0, NULL);

out:
	if (xdvp != NULL) {
		VN_RELE(xdvp);
	}

	return Status;
}

/*
 * Lookup/Create an extended attribute entry.
 *
 * Input arguments:
 *	dzp	- znode for hidden attribute directory
 *	name	- name of attribute
 *	flag	- ZNEW: if the entry already exists, fail with EEXIST.
 *		  ZEXISTS: if the entry does not exist, fail with ENOENT.
 *
 * Output arguments:
 *	vpp	- pointer to the vnode for the entry (NULL if there isn't one)
 *
 * Return value: 0 on success or errno value on failure.
 */
int
zfs_obtain_xattr(znode_t *dzp, const char *name, mode_t mode, cred_t *cr,
                 vnode_t **vpp, int flag)
{
	int error=0;
	znode_t  *xzp = NULL;
	zfsvfs_t  *zfsvfs = dzp->z_zfsvfs;
	zilog_t  *zilog;
	zfs_dirlock_t  *dl;
	dmu_tx_t  *tx;
	struct vnode_attr  vattr = { 0 };
	pathname_t cn = { 0 };
	zfs_acl_ids_t	acl_ids;

	/* zfs_dirent_lock() expects a component name */

    ZFS_ENTER(zfsvfs);
    ZFS_VERIFY_ZP(dzp);
    zilog = zfsvfs->z_log;

	vattr.va_type = VREG;
	vattr.va_mode = mode & ~S_IFMT;
	vattr.va_mask = ATTR_TYPE | ATTR_MODE;

	if ((error = zfs_acl_ids_create(dzp, 0,
                                    &vattr, cr, NULL, &acl_ids)) != 0) {
		ZFS_EXIT(zfsvfs);
		return (error);
	}

	cn.pn_bufsize = strlen(name)+1;
	cn.pn_buf = (char *)kmem_zalloc(cn.pn_bufsize, KM_SLEEP);


 top:
	/* Lock the attribute entry name. */
	if ( (error = zfs_dirent_lock(&dl, dzp, (char *)name, &xzp, flag,
                                  NULL, &cn)) ) {
		goto out;
	}
	/* If the name already exists, we're done. */
	if (xzp != NULL) {
		zfs_dirent_unlock(dl);
		goto out;
	}
	tx = dmu_tx_create(zfsvfs->z_os);
	dmu_tx_hold_sa(tx, dzp->z_sa_hdl, B_FALSE);
	dmu_tx_hold_zap(tx, dzp->z_id, TRUE, (char *)name);
	dmu_tx_hold_zap(tx, DMU_NEW_OBJECT, FALSE, NULL);

#if 1 // FIXME
	if (dzp->z_pflags & ZFS_INHERIT_ACE) {
		dmu_tx_hold_write(tx, DMU_NEW_OBJECT, 0, SPA_MAXBLOCKSIZE);
	}
#endif
    zfs_sa_upgrade_txholds(tx, dzp);
	error = dmu_tx_assign(tx, TXG_WAIT);
	if (error) {
		zfs_dirent_unlock(dl);
		if (error == ERESTART) {
			dmu_tx_wait(tx);
			dmu_tx_abort(tx);
			goto top;
		}
		dmu_tx_abort(tx);
		goto out;
	}

	zfs_mknode(dzp, &vattr, tx, cr, 0, &xzp, &acl_ids);

    /*
      ASSERT(xzp->z_id == zoid);
    */
	(void) zfs_link_create(dl, xzp, tx, ZNEW);
	zfs_log_create(zilog, tx, TX_CREATE, dzp, xzp, (char *)name,
                   NULL /* vsecp */, 0 /*acl_ids.z_fuidp*/, &vattr);
	dmu_tx_commit(tx);

	/*
	 * OS X - attach the vnode _after_ committing the transaction
	 */
	zfs_znode_getvnode(xzp, dzp, zfsvfs);

	zfs_dirent_unlock(dl);
 out:
	zfs_acl_ids_free(&acl_ids);
	if (cn.pn_buf)
		kmem_free(cn.pn_buf, cn.pn_bufsize);

	/* The REPLACE error if doesn't exist is ENOATTR */
	if ((flag & ZEXISTS) && (error == ENOENT))
		error = STATUS_NO_EAS_ON_FILE;

	if (xzp)
		*vpp = ZTOV(xzp);

    ZFS_EXIT(zfsvfs);
	return (error);
}

/*
 * ace_trivial:
 * determine whether an ace_t acl is trivial
 *
 * Trivialness implies that the acl is composed of only
 * owner, group, everyone entries.  ACL can't
 * have read_acl denied, and write_owner/write_acl/write_attributes
 * can only be owner@ entry.
 */
int
ace_trivial_common(void *acep, int aclcnt,
                   uint64_t (*walk)(void *, uint64_t, int aclcnt,
                                    uint16_t *, uint16_t *, uint32_t *))
{
	uint16_t flags;
	uint32_t mask;
	uint16_t type;
	uint64_t cookie = 0;

	while ((cookie = walk(acep, cookie, aclcnt, &flags, &type, &mask))) {
		switch (flags & ACE_TYPE_FLAGS) {
			case ACE_OWNER:
			case ACE_GROUP|ACE_IDENTIFIER_GROUP:
			case ACE_EVERYONE:
				break;
			default:
				return (1);

		}

		if (flags & (ACE_FILE_INHERIT_ACE|
					 ACE_DIRECTORY_INHERIT_ACE|ACE_NO_PROPAGATE_INHERIT_ACE|
					 ACE_INHERIT_ONLY_ACE))
			return (1);

		/*
		 * Special check for some special bits
		 *
		 * Don't allow anybody to deny reading basic
		 * attributes or a files ACL.
		 */
		if ((mask & (ACE_READ_ACL|ACE_READ_ATTRIBUTES)) &&
			(type == ACE_ACCESS_DENIED_ACE_TYPE))
			return (1);

		/*
		 * Delete permission is never set by default
		 */
		if (mask & ACE_DELETE)
			return (1);

		/*
		 * Child delete permission should be accompanied by write
                 */
		if ((mask & ACE_DELETE_CHILD) && !(mask & ACE_WRITE_DATA))
			return (1);
		/*
		 * only allow owner@ to have
		 * write_acl/write_owner/write_attributes/write_xattr/
		 */

		if (type == ACE_ACCESS_ALLOWED_ACE_TYPE &&
			(!(flags & ACE_OWNER) && (mask &
			(ACE_WRITE_OWNER|ACE_WRITE_ACL| ACE_WRITE_ATTRIBUTES|
			ACE_WRITE_NAMED_ATTRS))))
			return (1);

	}

	return (0);
}


void
acl_trivial_access_masks(mode_t mode, boolean_t isdir, trivial_acl_t *masks)
{
    uint32_t read_mask = ACE_READ_DATA;
    uint32_t write_mask = ACE_WRITE_DATA|ACE_APPEND_DATA;
    uint32_t execute_mask = ACE_EXECUTE;

	if (isdir)
		write_mask |= ACE_DELETE_CHILD;

    masks->deny1 = 0;
    if (!(mode & S_IRUSR) && (mode & (S_IRGRP|S_IROTH)))
        masks->deny1 |= read_mask;
    if (!(mode & S_IWUSR) && (mode & (S_IWGRP|S_IWOTH)))
        masks->deny1 |= write_mask;
    if (!(mode & S_IXUSR) && (mode & (S_IXGRP|S_IXOTH)))
        masks->deny1 |= execute_mask;

    masks->deny2 = 0;
    if (!(mode & S_IRGRP) && (mode & S_IROTH))
        masks->deny2 |= read_mask;
    if (!(mode & S_IWGRP) && (mode & S_IWOTH))
        masks->deny2 |= write_mask;
    if (!(mode & S_IXGRP) && (mode & S_IXOTH))
        masks->deny2 |= execute_mask;

    masks->allow0 = 0;
    if ((mode & S_IRUSR) && (!(mode & S_IRGRP) && (mode & S_IROTH)))
        masks->allow0 |= read_mask;
    if ((mode & S_IWUSR) && (!(mode & S_IWGRP) && (mode & S_IWOTH)))
        masks->allow0 |= write_mask;
    if ((mode & S_IXUSR) && (!(mode & S_IXGRP) && (mode & S_IXOTH)))
        masks->allow0 |= execute_mask;

    masks->owner = ACE_WRITE_ATTRIBUTES|ACE_WRITE_OWNER|ACE_WRITE_ACL|
        ACE_WRITE_NAMED_ATTRS|ACE_READ_ACL|ACE_READ_ATTRIBUTES|
        ACE_READ_NAMED_ATTRS|ACE_SYNCHRONIZE;
    if (mode & S_IRUSR)
        masks->owner |= read_mask;
    if (mode & S_IWUSR)
        masks->owner |= write_mask;
    if (mode & S_IXUSR)
        masks->owner |= execute_mask;

    masks->group = ACE_READ_ACL|ACE_READ_ATTRIBUTES|ACE_READ_NAMED_ATTRS|
        ACE_SYNCHRONIZE;
    if (mode & S_IRGRP)
        masks->group |= read_mask;
    if (mode & S_IWGRP)
        masks->group |= write_mask;
    if (mode & S_IXGRP)
        masks->group |= execute_mask;

    masks->everyone = ACE_READ_ACL|ACE_READ_ATTRIBUTES|ACE_READ_NAMED_ATTRS|
        ACE_SYNCHRONIZE;
    if (mode & S_IROTH)
        masks->everyone |= read_mask;
    if (mode & S_IWOTH)
        masks->everyone |= write_mask;
    if (mode & S_IXOTH)
        masks->everyone |= execute_mask;
}



#define KAUTH_DIR_WRITE     (KAUTH_VNODE_ACCESS | KAUTH_VNODE_ADD_FILE | \
                             KAUTH_VNODE_ADD_SUBDIRECTORY | \
                             KAUTH_VNODE_DELETE_CHILD)

#define KAUTH_DIR_READ      (KAUTH_VNODE_ACCESS | KAUTH_VNODE_LIST_DIRECTORY)

#define KAUTH_DIR_EXECUTE   (KAUTH_VNODE_ACCESS | KAUTH_VNODE_SEARCH)

#define KAUTH_FILE_WRITE    (KAUTH_VNODE_ACCESS | KAUTH_VNODE_WRITE_DATA)

#define KAUTH_FILE_READ     (KAUTH_VNODE_ACCESS | KAUTH_VNODE_READ_DATA)

#define KAUTH_FILE_EXECUTE  (KAUTH_VNODE_ACCESS | KAUTH_VNODE_EXECUTE)

/*
 * Compute the same user access value as getattrlist(2)
 */
uint32_t getuseraccess(znode_t *zp, vfs_context_t ctx)
{
	uint32_t	user_access = 0;
#if 0
	vnode_t	*vp;
	int error = 0;
    zfs_acl_phys_t acl_phys;
	/* Only take the expensive vnode_authorize path when we have an ACL */

    error = sa_lookup(zp->z_sa_hdl, SA_ZPL_ZNODE_ACL(zp->z_zfsvfs),
                      &acl_phys, sizeof (acl_phys));

	if (error || acl_phys.z_acl_count == 0) {
		kauth_cred_t	cred = vfs_context_ucred(ctx);
		uint64_t		obj_uid;
		uint64_t    	obj_mode;

		/* User id 0 (root) always gets access. */
		if (!vfs_context_suser(ctx)) {
			return (R_OK | W_OK | X_OK);
		}

        sa_lookup(zp->z_sa_hdl, SA_ZPL_UID(zp->z_zfsvfs),
                  &obj_uid, sizeof (obj_uid));
        sa_lookup(zp->z_sa_hdl, SA_ZPL_MODE(zp->z_zfsvfs),
                  &obj_mode, sizeof (obj_mode));

		//obj_uid = pzp->zp_uid;
		obj_mode = obj_mode & MODEMASK;
		if (obj_uid == UNKNOWNUID) {
			obj_uid = kauth_cred_getuid(cred);
		}
		if ((obj_uid == kauth_cred_getuid(cred)) ||
		    (obj_uid == UNKNOWNUID)) {
			return (((u_int32_t)obj_mode & S_IRWXU) >> 6);
		}
		/* Otherwise, settle for 'others' access. */
		return ((u_int32_t)obj_mode & S_IRWXO);
	}
	vp = ZTOV(zp);
	if (vnode_isdir(vp)) {
		if (vnode_authorize(vp, NULLVP, KAUTH_DIR_WRITE, ctx) == 0)
			user_access |= W_OK;
		if (vnode_authorize(vp, NULLVP, KAUTH_DIR_READ, ctx) == 0)
			user_access |= R_OK;
		if (vnode_authorize(vp, NULLVP, KAUTH_DIR_EXECUTE, ctx) == 0)
			user_access |= X_OK;
	} else {
		if (vnode_authorize(vp, NULLVP, KAUTH_FILE_WRITE, ctx) == 0)
			user_access |= W_OK;
		if (vnode_authorize(vp, NULLVP, KAUTH_FILE_READ, ctx) == 0)
			user_access |= R_OK;
		if (vnode_authorize(vp, NULLVP, KAUTH_FILE_EXECUTE, ctx) == 0)
			user_access |= X_OK;
	}
#endif
	return (user_access);
}



static unsigned char fingerprint[] = {0xab, 0xcd, 0xef, 0xab, 0xcd, 0xef,
                                      0xab, 0xcd, 0xef, 0xab, 0xcd, 0xef};

/*
 * Convert "Well Known" GUID to enum type.
 */
int kauth_wellknown_guid(guid_t *guid)
{
    uint32_t last = 0;

    if (memcmp(fingerprint, guid->g_guid, sizeof(fingerprint)))
        return KAUTH_WKG_NOT;

    last = BE_32(*((uint32_t *)&guid->g_guid[12]));

    switch(last) {
    case 0x0c:
        return KAUTH_WKG_EVERYBODY;
    case 0x0a:
        return KAUTH_WKG_OWNER;
    case 0x10:
        return KAUTH_WKG_GROUP;
    case 0xFFFFFFFE:
        return KAUTH_WKG_NOBODY;
    }

    return KAUTH_WKG_NOT;
}


/*
 * Set GUID to "well known" guid, based on enum type
 */
void nfsacl_set_wellknown(int wkg, guid_t *guid)
{
    /*
     * All WKGs begin with the same 12 bytes.
     */
    bcopy(fingerprint, (void *)guid, 12);
    /*
     * The final 4 bytes are our code (in network byte order).
     */
    switch (wkg) {
    case 4:
        *((uint32_t *)&guid->g_guid[12]) = BE_32(0x0000000c);
        break;
    case 3:
        *((uint32_t *)&guid->g_guid[12]) = BE_32(0xfffffffe);
        break;
    case 1:
        *((uint32_t *)&guid->g_guid[12]) = BE_32(0x0000000a);
        break;
    case 2:
        *((uint32_t *)&guid->g_guid[12]) = BE_32(0x00000010);
    };
}


/*
 * Convert Darwin ACL list, into ZFS ACL "aces" list.
 */
void aces_from_acl(ace_t *aces, int *nentries, struct kauth_acl *k_acl,
	int *seen_type)
{
#if 0
    int i;
    ace_t *ace;
    guid_t          *guidp;
    kauth_ace_rights_t  *ace_rights;
    uid_t  who;
    uint32_t  mask = 0;
    uint16_t  flags = 0;
    uint16_t  type = 0;
    uint32_t  ace_flags;
    int wkg;
	int err = 0;

    *nentries = k_acl->acl_entrycount;

    //bzero(aces, sizeof(*aces) * *nentries);

    //*nentries = aclp->acl_cnt;

    for (i = 0; i < *nentries; i++) {
        //entry = &(aclp->acl_entry[i]);

        flags = 0;
        mask  = 0;


        ace = &(aces[i]);

        /* Note Mac OS X GUID is a 128-bit identifier */
        guidp = &k_acl->acl_ace[i].ace_applicable;

        who = -1;
        wkg = kauth_wellknown_guid(guidp);

		switch(wkg) {
        case KAUTH_WKG_OWNER:
            flags |= ACE_OWNER;
			if (seen_type) *seen_type |= ACE_OWNER;
            break;
        case KAUTH_WKG_GROUP:
            flags |= ACE_GROUP|ACE_IDENTIFIER_GROUP;
			if (seen_type) *seen_type |= ACE_GROUP;
            break;
        case KAUTH_WKG_EVERYBODY:
            flags |= ACE_EVERYONE;
			if (seen_type) *seen_type |= ACE_EVERYONE;
            break;

        case KAUTH_WKG_NOBODY:
        default:
            /* Try to get a uid from supplied guid */
			err = kauth_cred_guid2uid(guidp, &who);
			if (err) {
				err = kauth_cred_guid2gid(guidp, &who);
				if (!err) {
					flags |= ACE_IDENTIFIER_GROUP;
				}
			}
			if (err) {
				*nentries=0;
				dprintf("ZFS: returning due to guid2gid\n");
				return;
			}

        } // switch

        ace->a_who = who;

        ace_rights = k_acl->acl_ace[i].ace_rights;
        if (ace_rights & KAUTH_VNODE_READ_DATA)
            mask |= ACE_READ_DATA;
        if (ace_rights & KAUTH_VNODE_WRITE_DATA)
            mask |= ACE_WRITE_DATA;
        if (ace_rights & KAUTH_VNODE_APPEND_DATA)
            mask |= ACE_APPEND_DATA;
        if (ace_rights & KAUTH_VNODE_READ_EXTATTRIBUTES)
            mask |= ACE_READ_NAMED_ATTRS;
        if (ace_rights & KAUTH_VNODE_WRITE_EXTATTRIBUTES)
            mask |= ACE_WRITE_NAMED_ATTRS;
        if (ace_rights & KAUTH_VNODE_EXECUTE)
            mask |= ACE_EXECUTE;
        if (ace_rights & KAUTH_VNODE_DELETE_CHILD)
            mask |= ACE_DELETE_CHILD;
        if (ace_rights & KAUTH_VNODE_READ_ATTRIBUTES)
            mask |= ACE_READ_ATTRIBUTES;
        if (ace_rights & KAUTH_VNODE_WRITE_ATTRIBUTES)
            mask |= ACE_WRITE_ATTRIBUTES;
        if (ace_rights & KAUTH_VNODE_DELETE)
            mask |= ACE_DELETE;
        if (ace_rights & KAUTH_VNODE_READ_SECURITY)
            mask |= ACE_READ_ACL;
        if (ace_rights & KAUTH_VNODE_WRITE_SECURITY)
            mask |= ACE_WRITE_ACL;
        if (ace_rights & KAUTH_VNODE_TAKE_OWNERSHIP)
            mask |= ACE_WRITE_OWNER;
        if (ace_rights & KAUTH_VNODE_SYNCHRONIZE)
            mask |= ACE_SYNCHRONIZE;
        ace->a_access_mask = mask;

        ace_flags = k_acl->acl_ace[i].ace_flags;
        if (ace_flags & KAUTH_ACE_FILE_INHERIT)
            flags |= ACE_FILE_INHERIT_ACE;
        if (ace_flags & KAUTH_ACE_DIRECTORY_INHERIT)
            flags |= ACE_DIRECTORY_INHERIT_ACE;
        if (ace_flags & KAUTH_ACE_LIMIT_INHERIT)
            flags |= ACE_NO_PROPAGATE_INHERIT_ACE;
        if (ace_flags & KAUTH_ACE_ONLY_INHERIT)
            flags |= ACE_INHERIT_ONLY_ACE;
        ace->a_flags = flags;

        switch(ace_flags & KAUTH_ACE_KINDMASK) {
        case KAUTH_ACE_PERMIT:
            type = ACE_ACCESS_ALLOWED_ACE_TYPE;
            break;
        case KAUTH_ACE_DENY:
            type = ACE_ACCESS_DENIED_ACE_TYPE;
            break;
        case KAUTH_ACE_AUDIT:
            type = ACE_SYSTEM_AUDIT_ACE_TYPE;
            break;
        case KAUTH_ACE_ALARM:
            type = ACE_SYSTEM_ALARM_ACE_TYPE;
            break;
        }
        ace->a_type = type;
        dprintf("  ACL: %d type %04x, mask %04x, flags %04x, who %d\n",
               i, type, mask, flags, who);
    }
#endif
}



int
zpl_xattr_set_sa(struct vnode *vp, const char *name, const void *value,
				 size_t size, int flags, cred_t *cr)
{
	znode_t *zp = VTOZ(vp);
	nvlist_t *nvl;
	size_t sa_size;
	int error;

	ASSERT(zp->z_xattr_cached);
	nvl = zp->z_xattr_cached;

	if (value == NULL) {
		error = -nvlist_remove(nvl, name, DATA_TYPE_BYTE_ARRAY);
		if (error == -ENOENT)
			return error;
		//error = zpl_xattr_set_dir(vp, name, NULL, 0, flags, cr);
        } else {
                /* Limited to 32k to keep nvpair memory allocations small */
                if (size > DXATTR_MAX_ENTRY_SIZE)
                        return (-EFBIG);

                /* Prevent the DXATTR SA from consuming the entire SA region */
                error = -nvlist_size(nvl, &sa_size, NV_ENCODE_XDR);
                if (error)
                        return (error);

                if (sa_size > DXATTR_MAX_SA_SIZE)
                        return (-EFBIG);
                error = -nvlist_add_byte_array(nvl, name,
                    (uchar_t *)value, size);
                if (error)
                        return (error);
        }

        /* Update the SA for additions, modifications, and removals. */
        if (!error)
                error = -zfs_sa_set_xattr(zp);

        ASSERT3S(error, <=, 0);

        return (error);
}

int
zpl_xattr_get_sa(struct vnode *vp, const char *name, void *value, size_t size)
{
	znode_t *zp = VTOZ(vp);
	uchar_t *nv_value;
	uint_t nv_size;
	int error = 0;

#ifdef __LINUX__
	ASSERT(RW_LOCK_HELD(&zp->z_xattr_lock));
#endif

	mutex_enter(&zp->z_lock);
	if (zp->z_xattr_cached == NULL)
		error = -zfs_sa_get_xattr(zp);
	mutex_exit(&zp->z_lock);

	if (error)
		return (error);

	ASSERT(zp->z_xattr_cached);
	error = -nvlist_lookup_byte_array(zp->z_xattr_cached, name,
									  &nv_value, &nv_size);
	if (error)
		return (error);

	if (!size)
		return (nv_size);
	if (size < nv_size)
		return (-ERANGE);

	memcpy(value, nv_value, nv_size);

	return (nv_size);
}

/* dst buffer must be at least UUID_PRINTABLE_STRING_LENGTH bytes */
int
zfs_vfs_uuid_unparse(uuid_t uuid, char *dst)
{
	if (!uuid || !dst) {
		dprintf("%s missing argument\n", __func__);
		return (EINVAL);
	}

	snprintf(dst, UUID_PRINTABLE_STRING_LENGTH, "%02x%02x%02x%02x-"
	    "%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
	    uuid[0], uuid[1], uuid[2], uuid[3],
	    uuid[4], uuid[5], uuid[6], uuid[7],
	    uuid[8], uuid[9], uuid[10], uuid[11],
	    uuid[12], uuid[13], uuid[14], uuid[15]);

	return (0);
}

#include <sys/md5.h>
int
zfs_vfs_uuid_gen(const char *osname, uuid_t uuid)
{
#if 1
	MD5_CTX  md5c;
	/* namespace (generated by uuidgen) */
	/* 50670853-FBD2-4EC3-9802-73D847BF7E62 */
	char namespace[16] = {0x50, 0x67, 0x08, 0x53, /* - */
	    0xfb, 0xd2, /* - */ 0x4e, 0xc3, /* - */
	    0x98, 0x02, /* - */
	    0x73, 0xd8, 0x47, 0xbf, 0x7e, 0x62};

	/* Validate arguments */
	if (!osname || !uuid || strlen(osname) == 0) {
		dprintf("%s missing argument\n", __func__);
		return (EINVAL);
	}

	/*
	 * UUID version 3 (MD5) namespace variant:
	 * hash namespace (uuid) together with name
	 */
	MD5Init( &md5c );
	MD5Update( &md5c, &namespace, sizeof (namespace));
	MD5Update( &md5c, osname, strlen(osname));
	MD5Final( uuid, &md5c );

	/*
	 * To make UUID version 3, twiddle a few bits:
	 * xxxxxxxx-xxxx-Mxxx-Nxxx-xxxxxxxxxxxx
	 * [uint32]-[uin-t32]-[uin-t32][uint32]
	 * M should be 0x3 to indicate uuid v3
	 * N should be 0x8, 0x9, 0xa, or 0xb
	 */
	uuid[6] = (uuid[6] & 0x0F) | 0x30;
	uuid[8] = (uuid[8] & 0x3F) | 0x80;

	/* Print all caps */
	//dprintf("%s UUIDgen: [%s](%ld)->"
	dprintf("%s UUIDgen: [%s](%ld) -> "
	    "[%02X%02X%02X%02X-%02X%02X-%02X%02X-"
	    "%02X%02X-%02X%02X%02X%02X%02X%02X]\n",
	    __func__, osname, strlen(osname),
	    uuid[0], uuid[1], uuid[2], uuid[3],
	    uuid[4], uuid[5], uuid[6], uuid[7],
	    uuid[8], uuid[9], uuid[10], uuid[11],
	    uuid[12], uuid[13], uuid[14], uuid[15]);
#endif
	return (0);
}


/*
 * Attempt to build a full path from a zp, traversing up through parents.
 * start_zp should already be held (VN_HOLD()) and if parent_zp is
 * not NULL, it too should be held.
 * Returned is an allocated string (kmem_alloc) which should be freed
 * by caller (kmem_free(fullpath, returnsize)).
 * If supplied, start_zp_offset, is the index into fullpath where the 
 * start_zp component name starts. (Point between start_parent/start_zp).
 * returnsize includes the final NULL, so it is strlen(fullpath)+1
 */
int zfs_build_path(znode_t *start_zp, znode_t *start_parent, char **fullpath, uint32_t *returnsize, uint32_t *start_zp_offset)
{
	char *work;
	int index, size, part, error;
	struct vnode *vp = NULL;
	struct vnode *dvp = NULL;
	znode_t *zp = NULL;
	znode_t *dzp = NULL;
	uint64_t parent;
	zfsvfs_t *zfsvfs;
	char name[MAXPATHLEN];
	// No output? nothing to do
	if (!fullpath) return EINVAL;
	// No input? nothing to do
	if (!start_zp) return EINVAL;

	zfsvfs = start_zp->z_zfsvfs;
	zp = start_zp;

	VN_HOLD(ZTOV(zp));

	work = kmem_alloc(MAXPATHLEN * 2, KM_SLEEP);
	index = MAXPATHLEN * 2 - 1;

	work[--index] = 0;
	size = 1;

	while(1) {

		// Fetch parent
		if (start_parent) {
			dzp = start_parent;
			VN_HOLD(ZTOV(dzp));
			parent = dzp->z_id;
			start_parent = NULL;
		} else {
			VERIFY(sa_lookup(zp->z_sa_hdl, SA_ZPL_PARENT(zfsvfs),
				&parent, sizeof(parent)) == 0);
			error = zfs_zget(zfsvfs, parent, &dzp);
			if (error) {
				dprintf("%s: zget failed %d\n", __func__, error);
				goto failed;
			}
		}
		// dzp held from here.

		// Find name
		if (zp->z_id == zfsvfs->z_root)
			strlcpy(name, "", MAXPATHLEN);  // Empty string, as we add "\\" below
		else
			if ((error = zap_value_search(zfsvfs->z_os, parent, zp->z_id,
				ZFS_DIRENT_OBJ(-1ULL), name)) != 0) {
				dprintf("%s: zap_value_search failed %d\n", __func__, error);
				goto failed;
			}
		// Copy in name.
		part = strlen(name);
		// Check there is room
		if (part + 1 > index) {
			dprintf("%s: out of space\n", __func__);
			goto failed;
		}

		index -= part;
		memcpy(&work[index], name, part);

		// If start_zp, remember index (to be adjusted)
		if (zp == start_zp && start_zp_offset)
			*start_zp_offset = index;

		// Prepend "/"
		work[--index] = '\\';
		size += part + 1;

		// Swap dzp and zp to "go up one".
		VN_RELE(ZTOV(zp)); // we are done with zp.
		zp = dzp; // Now focus on parent
		dzp = NULL;

		// If parent, stop, "/" is already copied in.
		if (zp->z_id == zfsvfs->z_root) break;

	}

	// Release "parent" if it was held, now called zp.
	if (zp) VN_RELE(ZTOV(zp));

	// Correct index
	if (start_zp_offset)
		*start_zp_offset = *start_zp_offset - index;
	if (returnsize)
		*returnsize = size;
	ASSERT(size != 0);
	*fullpath = kmem_alloc(size, KM_SLEEP);
	memmove(*fullpath, &work[index], size);
	kmem_free(work, MAXPATHLEN * 2);
	dprintf("%s: set '%s' as name\n", __func__, *fullpath);
	return 0;

failed:
	if (zp) VN_RELE(ZTOV(zp));
	if (dzp) VN_RELE(ZTOV(dzp));
	kmem_free(work, MAXPATHLEN * 2);
	return -1;
}

/*
* This is connected to IRP_MN_NOTIFY_DIRECTORY_CHANGE
* and sending the notifications of changes
*/
void zfs_send_notify_stream(zfsvfs_t *zfsvfs, char *name, int nameoffset, ULONG FilterMatch, ULONG Action, char *stream)
{
	mount_t *zmo;
	zmo = zfsvfs->z_vfs;
	UNICODE_STRING ustr;
	UNICODE_STRING ustream;

	if (name == NULL) return;

	AsciiStringToUnicodeString(name, &ustr);

	dprintf("%s: '%wZ' part '%S' %u %u\n", __func__, &ustr, 
		/*&name[nameoffset],*/ &ustr.Buffer[nameoffset],
		FilterMatch, Action);

	if (stream != NULL) {
		AsciiStringToUnicodeString(stream, &ustream);
		dprintf("%s: with stream '%wZ'\n", __func__, &ustream);
	}

	FsRtlNotifyFullReportChange(zmo->NotifySync, &zmo->DirNotifyList,
		(PSTRING)&ustr, nameoffset * sizeof(WCHAR),
		stream == NULL ? NULL : (PSTRING)&ustream , // StreamName
		NULL, // NormalizedParentName
		FilterMatch, Action,
		NULL); // TargetContext
	FreeUnicodeString(&ustr);
	if (stream != NULL)
		FreeUnicodeString(&ustream);
}

void zfs_send_notify(zfsvfs_t *zfsvfs, char *name, int nameoffset, ULONG FilterMatch, ULONG Action)
{
	zfs_send_notify_stream(zfsvfs, name, nameoffset, FilterMatch, Action, NULL);
}


void zfs_uid2sid(uint64_t uid, SID **sid)
{
	int num;
	SID *tmp;

	ASSERT(sid != NULL);

	// Root?
	num = (uid == 0) ? 1 : 2;

	tmp = ExAllocatePoolWithTag(PagedPool,
		offsetof(SID, SubAuthority) + (num * sizeof(ULONG)), 'zsid');

	tmp->Revision = 1;
	tmp->SubAuthorityCount = num;
	tmp->IdentifierAuthority.Value[0] = 0;
	tmp->IdentifierAuthority.Value[1] = 0;
	tmp->IdentifierAuthority.Value[2] = 0;
	tmp->IdentifierAuthority.Value[3] = 0;
	tmp->IdentifierAuthority.Value[4] = 0;

	if (uid == 0) {
		tmp->IdentifierAuthority.Value[5] = 5;
		tmp->SubAuthority[0] = 18;
	} else {
		tmp->IdentifierAuthority.Value[5] = 22;
		tmp->SubAuthority[0] = 1;
		tmp->SubAuthority[1] = uid; // bits truncation?
	}

	*sid = tmp;
}

uint64_t zfs_sid2uid(SID *sid)
{
	// Root
	if (sid->Revision == 1 && sid->SubAuthorityCount == 1 && 
		sid->IdentifierAuthority.Value[0] == 0 && sid->IdentifierAuthority.Value[1] == 0 && sid->IdentifierAuthority.Value[2] == 0 && 
		sid->IdentifierAuthority.Value[3] == 0 && sid->IdentifierAuthority.Value[4] == 0 && sid->IdentifierAuthority.Value[5] == 18)
		return 0;

	// Samba's SID scheme: S-1-22-1-X
	if (sid->Revision == 1 && sid->SubAuthorityCount == 2 &&
		sid->IdentifierAuthority.Value[0] == 0 && sid->IdentifierAuthority.Value[1] == 0 && sid->IdentifierAuthority.Value[2] == 0 &&
		sid->IdentifierAuthority.Value[3] == 0 && sid->IdentifierAuthority.Value[4] == 0 && sid->IdentifierAuthority.Value[5] == 22 &&
		sid->SubAuthority[0] == 1)
		return sid->SubAuthority[1];
	
	return UID_NOBODY;
}


void zfs_gid2sid(uint64_t gid, SID **sid)
{
	int num = 2;
	SID *tmp;

	ASSERT(sid != NULL);

	tmp = ExAllocatePoolWithTag(PagedPool,
		offsetof(SID, SubAuthority) + (num * sizeof(ULONG)), 'zsid');

	tmp->Revision = 1;
	tmp->SubAuthorityCount = num;
	tmp->IdentifierAuthority.Value[0] = 0;
	tmp->IdentifierAuthority.Value[1] = 0;
	tmp->IdentifierAuthority.Value[2] = 0;
	tmp->IdentifierAuthority.Value[3] = 0;
	tmp->IdentifierAuthority.Value[4] = 0;

	tmp->IdentifierAuthority.Value[5] = 22;
	tmp->SubAuthority[0] = 2;
	tmp->SubAuthority[1] = gid; // bits truncation?

	*sid = tmp;
}

void zfs_freesid(SID *sid)
{
	ASSERT(sid != NULL);
	ExFreePool(sid);
}


static ACL *
zfs_set_acl(dacl *dacls) 
{
	int size, i;
	ACL *acl = NULL;
	ACCESS_ALLOWED_ACE *aaa;

	size = sizeof(ACL);
	i = 0;
	while (dacls[i].sid) {
		size += sizeof(ACCESS_ALLOWED_ACE);
		size += 8 + (dacls[i].sid->elements * sizeof(UINT32)) - sizeof(ULONG);
		i++;
	}

	acl = ExAllocatePoolWithTag(PagedPool, size, 'zacl');
	if (!acl) 
		return NULL;

	acl->AclRevision = ACL_REVISION;
	acl->Sbz1 = 0;
	acl->AclSize = size;
	acl->AceCount = i;
	acl->Sbz2 = 0;

	aaa = (ACCESS_ALLOWED_ACE*)&acl[1];
	i = 0;
	while (dacls[i].sid) {
		aaa->Header.AceType = ACCESS_ALLOWED_ACE_TYPE;
		aaa->Header.AceFlags = dacls[i].flags;
		aaa->Header.AceSize = sizeof(ACCESS_ALLOWED_ACE) - sizeof(ULONG) + 8 + (dacls[i].sid->elements * sizeof(UINT32));
		aaa->Mask = dacls[i].mask;

		RtlCopyMemory(&aaa->SidStart, dacls[i].sid, 8 + (dacls[i].sid->elements * sizeof(UINT32)));

		aaa = (ACCESS_ALLOWED_ACE*)((UINT8*)aaa + aaa->Header.AceSize);
		i++;
	}

	return acl;
}


void zfs_set_security_root(struct vnode *vp)
{
	SECURITY_DESCRIPTOR sd;
	SID *usersid = NULL, *groupsid = NULL;
	znode_t *zp = VTOZ(vp);
	zfsvfs_t *zfsvfs = zp->z_zfsvfs;
	NTSTATUS Status;
	ACL *acl = NULL;

	Status = RtlCreateSecurityDescriptor(&sd, SECURITY_DESCRIPTOR_REVISION);
	if (Status != STATUS_SUCCESS) goto err;

	acl = def_dacls;

	zfs_uid2sid(zp->z_uid, &usersid);
	zfs_gid2sid(zp->z_gid, &groupsid);

	RtlSetOwnerSecurityDescriptor(&sd, usersid, FALSE);
	RtlSetGroupSecurityDescriptor(&sd, groupsid, FALSE);

	acl = zfs_set_acl(acl);

	if (acl) 
		Status = RtlSetDaclSecurityDescriptor(&sd, TRUE, acl, FALSE);

	ULONG buflen = 0;
	Status = RtlAbsoluteToSelfRelativeSD(&sd, NULL, &buflen);
	if (Status != STATUS_SUCCESS &&
		Status != STATUS_BUFFER_TOO_SMALL) goto err;

	ASSERT(buflen != 0);

	void *tmp = ExAllocatePoolWithTag(PagedPool, buflen, 'ZSEC');
	if (tmp == NULL) goto err;

	Status = RtlAbsoluteToSelfRelativeSD(&sd, tmp, &buflen);
	
	vnode_setsecurity(vp, tmp);

err:
	if (acl)
		ExFreePool(acl);
	if (usersid != NULL)
		zfs_freesid(usersid);
	if (groupsid != NULL)
		zfs_freesid(groupsid);
}

void zfs_set_security(struct vnode *vp, struct vnode *dvp)
{
	SECURITY_SUBJECT_CONTEXT subjcont;
	NTSTATUS Status;
	SID *usersid = NULL, *groupsid = NULL;

	if (vp == NULL) return;

	if (vp->security_descriptor != NULL) return;

	znode_t *zp = VTOZ(vp);
	zfsvfs_t *zfsvfs = zp->z_zfsvfs;

	// If we are the rootvp, we don't have a parent, so do different setup
	if (zp->z_id == zfsvfs->z_root) {
		zfs_set_security_root(vp);
		return;
	}

	ZFS_ENTER_IFERROR(zfsvfs)
		return;

	// If no parent, find it. This will take one hold on
	// dvp, either directly or from zget().
	znode_t *dzp = NULL;
	if (dvp == NULL) {
		uint64_t parent;
		if (sa_lookup(zp->z_sa_hdl, SA_ZPL_PARENT(zfsvfs),
			&parent, sizeof(parent)) != 0) {
			goto err;
		}
		if (zfs_zget(zfsvfs, parent, &dzp)) {
			dvp = NULL;
			goto err;
		}
		dvp = ZTOV(dzp);
	} else {
		VN_HOLD(dvp);
		dzp = VTOZ(dvp);
	}

	ASSERT(dvp != NULL);
	ASSERT(dzp != NULL);
	ASSERT(vnode_security(dvp) != NULL);

	SeCaptureSubjectContext(&subjcont);
	void *sd = NULL;
	Status = SeAssignSecurityEx(vnode_security(dvp), NULL, (void**)&sd, NULL,
		vnode_isdir(vp)?TRUE:FALSE, 
		SEF_DACL_AUTO_INHERIT, &subjcont, IoGetFileObjectGenericMapping(), PagedPool);

	if (Status != STATUS_SUCCESS) goto err;

	vnode_setsecurity(vp, sd);

	zfs_uid2sid(zp->z_uid, &usersid);
	RtlSetOwnerSecurityDescriptor(&sd, usersid, FALSE);

	zfs_gid2sid(zp->z_gid, &groupsid);
	RtlSetGroupSecurityDescriptor(&sd, groupsid, FALSE);

err:
	if (dvp) VN_RELE(dvp);
	ZFS_EXIT(zfsvfs);

	if (usersid != NULL)
		zfs_freesid(usersid);
	if (groupsid != NULL)
		zfs_freesid(groupsid);
}

// return true if a XATTR name should be skipped
int xattr_protected(char *name)
{
	return 0;
}

// return true if xattr is a stream (name ends with ":$DATA")
int xattr_stream(char *name)
{
	char tail[] = ":$DATA";
	int taillen = sizeof(tail);
	int len;

	if (name == NULL)
		return 0;
	len = strlen(name);
	if (len < taillen)
		return 0;

	if (strcmp(&name[len - taillen + 1], tail) == 0)
		return 1;

	return 0;
}

// Get the size needed for EA, check first if it is
// cached in vnode. Otherwise, compute it and set.
uint64_t xattr_getsize(struct vnode *vp)
{
	uint64_t ret = 0;
	struct vnode *xdvp = NULL, *xvp = NULL;
	znode_t *zp;
	zfsvfs_t *zfsvfs;
	zap_cursor_t  zc;
	zap_attribute_t  za;
	objset_t  *os;

	if (vp == NULL) return 0;

	// Cached? Easy, use it
	if (vnode_easize(vp, &ret))
		return ret;

	zp = VTOZ(vp);
	zfsvfs = zp->z_zfsvfs;

	/*
	 * Iterate through all the xattrs, adding up namelengths and value sizes.
	 * There was some suggestion that this should be 4 + (5 + name + valuelen)
	 * but that no longer appears to be true. The returned value is used directly
	 * with IRP_MJ_QUERY_EA and we will have to return short.
	 * We will return the true space needed.
	 */
	if (zfs_get_xattrdir(zp, &xdvp, NULL, 0) != 0) {
		goto out;
	}

	os = zfsvfs->z_os;

	for (zap_cursor_init(&zc, os, VTOZ(xdvp)->z_id);
		zap_cursor_retrieve(&zc, &za) == 0; zap_cursor_advance(&zc)) {

		if (xattr_protected(za.za_name))
			continue;	 /* skip */
		if (xattr_stream(za.za_name))
			continue;	 /* skip */

		if (zfs_dirlook(VTOZ(xdvp), za.za_name, &xvp, 0, NULL, NULL) == 0) {
			ret = ((ret + 3) & ~3); // aligned to 4 bytes.
			ret += offsetof(FILE_FULL_EA_INFORMATION, EaName) + strlen(za.za_name) + 1 + VTOZ(xvp)->z_size;
			VN_RELE(xvp);
		}
	}
	zap_cursor_fini(&zc);
	VN_RELE(xdvp);

out:
	// Cache result, even if failure (cached as 0).
	vnode_set_easize(vp, ret);

	return ret;
}

/*
 * Call vnode_setunlink if zfs_zaccess_delete() allows it
 * TODO: provide credentials
 */
NTSTATUS zfs_setunlink(FILE_OBJECT *fo, vnode_t *dvp) 
{
	vnode_t *vp;
	NTSTATUS Status = STATUS_UNSUCCESSFUL;

	if (fo == NULL) {
		Status = STATUS_INVALID_PARAMETER;
		goto err;
	}

	vp = fo->FsContext;

	if (vp == NULL) {
		Status = STATUS_INVALID_PARAMETER;
		goto err;
	}

	znode_t *zp = NULL;
	znode_t *dzp = NULL;
	zfs_dirlist_t *zccb = fo->FsContext2;

	zfsvfs_t *zfsvfs;
	VN_HOLD(vp);
	zp = VTOZ(vp);

	if (vp && zp) {
		zfsvfs = zp->z_zfsvfs;
	} else {
		Status = STATUS_INVALID_PARAMETER;
		goto err;
	}

	if (zfsvfs->z_rdonly || vfs_isrdonly(zfsvfs->z_vfs) ||
		!spa_writeable(dmu_objset_spa(zfsvfs->z_os))) {
		Status = STATUS_MEDIA_WRITE_PROTECTED;
		goto err;
	}

	// Cannot delete a user mapped image.
	if (!MmFlushImageSection(&vp->SectionObjectPointers,
		MmFlushForDelete)) {
		Status = STATUS_CANNOT_DELETE;
		goto err;
	}

	// if dvp == null, find it

	if (dvp == NULL) {
		uint64_t parent;

		if (sa_lookup(zp->z_sa_hdl, SA_ZPL_PARENT(zfsvfs),
			&parent, sizeof(parent)) != 0) {
			goto err;
		}
		if (zfs_zget(zfsvfs, parent, &dzp)) {
			dvp = NULL;
			goto err;
		}
		dvp = ZTOV(dzp);
	}
	else {
		dzp = VTOZ(dvp);
		VN_HOLD(dvp);
	}

	// If we are root
	if (zp->z_id == zfsvfs->z_root) {
		Status = STATUS_CANNOT_DELETE;
		goto err;
	}

	// If we are a dir, and have more than "." and "..", we
	// are not empty.
	if (S_ISDIR(zp->z_mode)) {

		int nodeadlock = 0;

		if (zp->z_size > 2) {
			Status = STATUS_DIRECTORY_NOT_EMPTY;
			goto err;
		}
	}

	int error = zfs_zaccess_delete(dzp, zp, 0);

	if (error == 0) {
		ASSERT3P(zccb, != , NULL);
		zccb->deleteonclose = 1;
		fo->DeletePending = TRUE;
		Status = STATUS_SUCCESS;
	}
	else {
		Status = STATUS_ACCESS_DENIED;
	}

err:
	if (vp) {
		VN_RELE(vp);
		vp = NULL;
	}

	if (dvp) {
		VN_RELE(dvp);
		dvp = NULL;
	}

	return Status;
}

int
uio_prefaultpages(ssize_t n, struct uio *uio)
{
	return (0);
}


/* IRP_MJ_SET_INFORMATION helpers */


NTSTATUS file_disposition_information(PDEVICE_OBJECT DeviceObject, PIRP Irp, PIO_STACK_LOCATION IrpSp)
{
	NTSTATUS Status = STATUS_SUCCESS;

	if (IrpSp->FileObject == NULL || IrpSp->FileObject->FsContext == NULL)
		return STATUS_INVALID_PARAMETER;

	PFILE_OBJECT FileObject = IrpSp->FileObject;
	struct vnode *vp = FileObject->FsContext;
	zfs_dirlist_t *zccb = FileObject->FsContext2;
	znode_t *zp = VTOZ(vp);
	zfsvfs_t *zfsvfs = zp->z_zfsvfs;
	FILE_DISPOSITION_INFORMATION *fdi = Irp->AssociatedIrp.SystemBuffer;
	mount_t *zmo = DeviceObject->DeviceExtension;

	if (vp) {
		dprintf("Deletion %s on '%wZ'\n",
			fdi->DeleteFile ? "set" : "unset",
			IrpSp->FileObject->FileName);
		Status = STATUS_SUCCESS;
		if (fdi->DeleteFile) {
			Status = zfs_setunlink(IrpSp->FileObject, NULL);
		} else {
			if (zccb) zccb->deleteonclose = 0;
			FileObject->DeletePending = FALSE;
		}
		// Dirs marked for Deletion should release all pending Notify events
		if (Status == STATUS_SUCCESS && fdi->DeleteFile) {
			FsRtlNotifyCleanup(zmo->NotifySync, &zmo->DirNotifyList, VTOZ(vp));
		}
	}
	return Status;
}

NTSTATUS file_disposition_information_ex(PDEVICE_OBJECT DeviceObject, PIRP Irp, PIO_STACK_LOCATION IrpSp)
{
	NTSTATUS Status = STATUS_SUCCESS;

	if (IrpSp->FileObject == NULL || IrpSp->FileObject->FsContext == NULL)
		return STATUS_INVALID_PARAMETER;

	PFILE_OBJECT FileObject = IrpSp->FileObject;
	struct vnode *vp = FileObject->FsContext;
	zfs_dirlist_t *zccb = FileObject->FsContext2;
	znode_t *zp = VTOZ(vp);
	zfsvfs_t *zfsvfs = zp->z_zfsvfs;
	FILE_DISPOSITION_INFORMATION_EX *fdie = Irp->AssociatedIrp.SystemBuffer;
	mount_t *zmo = DeviceObject->DeviceExtension;

	if (vp) {

		Status = STATUS_SUCCESS;

		dprintf("%s: Flags 0x%x\n", __func__, fdie->Flags);

		if (fdie->Flags | FILE_DISPOSITION_ON_CLOSE)
			if (fdie->Flags | FILE_DISPOSITION_DELETE)
				Status = zfs_setunlink(FileObject, NULL);
			else
				if (zccb) zccb->deleteonclose = 0;
		
		// Do we care about FILE_DISPOSITION_POSIX_SEMANTICS ?

		// Dirs marked for Deletion should release all pending Notify events
		if (Status == STATUS_SUCCESS && (fdie->Flags | FILE_DISPOSITION_DELETE)) {
			FsRtlNotifyCleanup(zmo->NotifySync, &zmo->DirNotifyList, VTOZ(vp));
		}
	}
	return Status;
}

NTSTATUS file_endoffile_information(PDEVICE_OBJECT DeviceObject, PIRP Irp, PIO_STACK_LOCATION IrpSp)
{
	NTSTATUS Status = STATUS_SUCCESS;

	if (IrpSp->FileObject == NULL || IrpSp->FileObject->FsContext == NULL)
		return STATUS_INVALID_PARAMETER;

	PFILE_OBJECT FileObject = IrpSp->FileObject;
	struct vnode *vp = FileObject->FsContext;
	zfs_dirlist_t *zccb = FileObject->FsContext2;
	znode_t *zp = VTOZ(vp);
	zfsvfs_t *zfsvfs = zp->z_zfsvfs;
	FILE_END_OF_FILE_INFORMATION *feofi = Irp->AssociatedIrp.SystemBuffer;
	int changed = 0;

	if (zfsvfs == NULL)
		return STATUS_INVALID_PARAMETER;

	dprintf("* File_EndOfFile_Information:\n");

	ZFS_ENTER(zfsvfs);

	// From FASTFAT
	//  This is kinda gross, but if the file is not cached, but there is
	//  a data section, we have to cache the file to avoid a bunch of
	//  extra work.
	BOOLEAN CacheMapInitialized = FALSE;
	if (FileObject && FileObject->SectionObjectPointer &&
		(FileObject->SectionObjectPointer->DataSectionObject != NULL) &&
		(FileObject->SectionObjectPointer->SharedCacheMap == NULL) &&
		!FlagOn(Irp->Flags, IRP_PAGING_IO)) {
		vnode_pager_setsize(vp, zp->z_size);
		CcInitializeCacheMap(FileObject,
			(PCC_FILE_SIZES)&vp->FileHeader.AllocationSize,
			FALSE,
			&CacheManagerCallbacks, vp);
		CcSetAdditionalCacheAttributes(FileObject, TRUE, TRUE); // FIXME: for now
		CacheMapInitialized = TRUE;
	}

	VN_HOLD(vp);
	if (!zfsvfs->z_unmounted) {

		// Can't be done on DeleteOnClose
		if (zccb && zccb->deleteonclose)
			goto out;

		// Advance only?
		if (IrpSp->Parameters.SetFile.AdvanceOnly) {
			if (feofi->EndOfFile.QuadPart > zp->z_size) {
				Status = zfs_freesp(zp, feofi->EndOfFile.QuadPart, 0, 0, TRUE);
				changed = 1;
			}
			dprintf("%s: AdvanceOnly\n", __func__);
			goto out;
		}
		// Truncation?
		if (zp->z_size > feofi->EndOfFile.QuadPart) {
			// Are we able to truncate?
			if (FileObject->SectionObjectPointer && !MmCanFileBeTruncated(FileObject->SectionObjectPointer,
				&feofi->EndOfFile)) {
				Status = STATUS_USER_MAPPED_FILE;
				goto out;
			}
			dprintf("%s: CanTruncate\n", __func__);
		}

		// Set new size
		Status = zfs_freesp(zp, feofi->EndOfFile.QuadPart, 0, 0, TRUE); // Len = 0 is truncate
		changed = 1;
	}

out:
	ZFS_EXIT(zfsvfs);
	VN_RELE(vp);

	if (NT_SUCCESS(Status) && changed) {

		dprintf("%s: new size 0x%llx set\n", __func__, zp->z_size);

		// zfs_freesp() calls vnode_paget_setsize(), but we need to update it here.
		if (FileObject->SectionObjectPointer)
			CcSetFileSizes(FileObject,
			(PCC_FILE_SIZES)&vp->FileHeader.AllocationSize);

		// No notify for XATTR/Stream for now
		if (!(zp->z_pflags & ZFS_XATTR)) {
			zfs_send_notify(zfsvfs, zp->z_name_cache, zp->z_name_offset,
				FILE_NOTIFY_CHANGE_SIZE,
				FILE_ACTION_MODIFIED);
		}
	}

	if (CacheMapInitialized) {
		dprintf("other uninit\n");
		CcUninitializeCacheMap(FileObject, NULL, NULL);
		dprintf("done uninit\n");
	}

	// We handled setsize in here.
	vnode_setsizechange(vp, 0);

	return Status;
}

// create hardlink by calling zfs_create
NTSTATUS file_link_information(PDEVICE_OBJECT DeviceObject, PIRP Irp, PIO_STACK_LOCATION IrpSp)
{
	NTSTATUS Status;
	/*
	typedef struct _FILE_LINK_INFORMATION {
	BOOLEAN ReplaceIfExists;
	HANDLE  RootDirectory;
	ULONG   FileNameLength;
	WCHAR   FileName[1];
	} FILE_LINK_INFORMATION, *PFILE_LINK_INFORMATION;
	*/

	FILE_LINK_INFORMATION *link = Irp->AssociatedIrp.SystemBuffer;
	dprintf("* FileLinkInformation: %.*S\n", link->FileNameLength / sizeof(WCHAR), link->FileName);

	// So, use FileObject to get VP.
	// Use VP to lookup parent.
	// Use Filename to find destonation dvp, and vp if it exists.
	if (IrpSp->FileObject == NULL || IrpSp->FileObject->FsContext == NULL)
		return STATUS_INVALID_PARAMETER;

	FILE_OBJECT *RootFileObject = NULL;
	PFILE_OBJECT FileObject = IrpSp->FileObject;
	struct vnode *fvp = FileObject->FsContext;
	znode_t *zp = VTOZ(fvp);
	znode_t *dzp = NULL;
	zfsvfs_t *zfsvfs = zp->z_zfsvfs;
	int error;
	ULONG outlen;
	char *remainder = NULL;
	char buffer[MAXNAMELEN], *filename;
	struct vnode *tdvp = NULL, *tvp = NULL, *fdvp = NULL;
	uint64_t parent = 0;

	// If given a RootDirectory Handle, lookup tdvp
	if (link->RootDirectory != 0) {
		if (ObReferenceObjectByHandle(link->RootDirectory,
			GENERIC_READ, *IoFileObjectType, KernelMode,
			&RootFileObject, NULL) != STATUS_SUCCESS) {
			return STATUS_INVALID_PARAMETER;
		}
		tdvp = RootFileObject->FsContext;
		VN_HOLD(tdvp);
	}
	else {
		// Name can be absolute, if so use name, otherwise, use vp's parent.
	}

	// Convert incoming filename to utf8
	error = RtlUnicodeToUTF8N(buffer, MAXNAMELEN, &outlen,
		link->FileName, link->FileNameLength);

	if (error != STATUS_SUCCESS &&
		error != STATUS_SOME_NOT_MAPPED) {
		if (tdvp) VN_RELE(tdvp);
		if (RootFileObject) ObDereferenceObject(RootFileObject);
		return STATUS_ILLEGAL_CHARACTER;
	}

	// Output string is only null terminated if input is, so do so now.
	buffer[outlen] = 0;
	filename = buffer;

	// Filename is often "\??\E:\name" so we want to eat everything up to the "\name"
	if ((filename[0] == '\\') &&
		(filename[1] == '?') &&
		(filename[2] == '?') &&
		(filename[3] == '\\') &&
		/* [4] drive letter */
		(filename[5] == ':') &&
		(filename[6] == '\\'))
		filename = &filename[6];

	error = zfs_find_dvp_vp(zfsvfs, filename, 1, 0, &remainder, &tdvp, &tvp, 0);
	if (error) {
		if (tdvp) VN_RELE(tdvp);
		if (RootFileObject) ObDereferenceObject(RootFileObject);
		return STATUS_OBJECTID_NOT_FOUND;
	}

	// Fetch parent
	VERIFY(sa_lookup(zp->z_sa_hdl, SA_ZPL_PARENT(zfsvfs),
		&parent, sizeof(parent)) == 0);

	// Fetch fdvp
	error = zfs_zget(zfsvfs, parent, &dzp);
	if (error) {
		error = STATUS_OBJECTID_NOT_FOUND;
		goto out;
	}

	// Lookup name
	if (zp->z_name_cache == NULL) {
		error = STATUS_OBJECTID_NOT_FOUND;
		goto out;
	}

	fdvp = ZTOV(dzp);
	VN_HOLD(fvp);
	// "tvp"(if not NULL) and "tdvp" is held by zfs_find_dvp_vp

	// What about link->ReplaceIfExist ?

	error = zfs_link(VTOZ(tdvp), VTOZ(fvp), remainder ? remainder : filename, NULL, 0);

	if (error == 0) {

		// FIXME, zget to get name?
#if 0
		// Release fromname, and lookup new name
		kmem_free(zp->z_name_cache, zp->z_name_len);
		zp->z_name_cache = NULL;
		if (zfs_build_path(zp, VTOZ(tdvp), &zp->z_name_cache, &zp->z_name_len, &zp->z_name_offset) == 0) {
			zfs_send_notify(zfsvfs, zp->z_name_cache, zp->z_name_offset,
				FILE_NOTIFY_CHANGE_CREATION,
				FILE_ACTION_ADDED);
		}
#endif
	}
	// Release all holds
out:
	if (RootFileObject) ObDereferenceObject(RootFileObject);
	if (tdvp) VN_RELE(tdvp);
	if (fdvp) VN_RELE(fdvp);
	if (fvp) VN_RELE(fvp);
	if (tvp) VN_RELE(tvp);

	return error;
}

NTSTATUS file_rename_information(PDEVICE_OBJECT DeviceObject, PIRP Irp, PIO_STACK_LOCATION IrpSp)
{
	NTSTATUS Status;
	/*
	The file name string in the FileName member must be specified in one of the following forms.
	A simple file name. (The RootDirectory member is NULL.) In this case, the file is simply renamed within the same directory.
	That is, the rename operation changes the name of the file but not its location.

	A fully qualified file name. (The RootDirectory member is NULL.) In this case, the rename operation changes the name and location of the file.

	A relative file name. In this case, the RootDirectory member contains a handle to the target directory for the rename operation. The file name itself must be a simple file name.

	NOTE: The RootDirectory handle thing never happens, and no sample source (including fastfat) handles it.
	*/

	FILE_RENAME_INFORMATION *ren = Irp->AssociatedIrp.SystemBuffer;
	dprintf("* FileRenameInformation: %.*S\n", ren->FileNameLength / sizeof(WCHAR), ren->FileName);

	//ASSERT(ren->RootDirectory == NULL);

	// So, use FileObject to get VP.
	// Use VP to lookup parent.
	// Use Filename to find destonation dvp, and vp if it exists.
	if (IrpSp->FileObject == NULL || IrpSp->FileObject->FsContext == NULL)
		return STATUS_INVALID_PARAMETER;

	PFILE_OBJECT FileObject = IrpSp->FileObject;
	struct vnode *fvp = FileObject->FsContext;
	znode_t *zp = VTOZ(fvp);
	znode_t *dzp = NULL;
	zfsvfs_t *zfsvfs = zp->z_zfsvfs;
	int error;
	ULONG outlen;
	char *remainder = NULL;
	char buffer[MAXNAMELEN], *filename;
	struct vnode *tdvp = NULL, *tvp = NULL, *fdvp = NULL;
	uint64_t parent = 0;
	PFILE_OBJECT dFileObject = NULL;
	HANDLE destParentHandle = 0;
	int use_fdvp_for_tdvp = 0;

	// Convert incoming filename to utf8
	error = RtlUnicodeToUTF8N(buffer, MAXNAMELEN, &outlen,
		ren->FileName, ren->FileNameLength);

	if (error != STATUS_SUCCESS &&
		error != STATUS_SOME_NOT_MAPPED) {
		return STATUS_ILLEGAL_CHARACTER;
	}

	// Output string is only null terminated if input is, so do so now.
	buffer[outlen] = 0;
	filename = buffer;

	// Filename is often "\??\E:\lower\name" - and "/lower" might be another dataset
	// so we need to drive a lookup, with SL_OPEN_TARGET_DIRECTORY set so we get
	// the parent of where we are renaming to. This will give us "tdvp", and
	// possibly "tvp" is we are to rename over an item.
#if 0
	if ((filename[0] == '\\') &&
		(filename[1] == '?') &&
		(filename[2] == '?') &&
		(filename[3] == '\\') &&
		/* [4] drive letter */
		(filename[5] == ':') &&
		(filename[6] == '\\'))
		filename = &filename[6];
#endif

	// If it starts with "\" drive the lookup, if it is just a name like "HEAD", assume
	// tdvp is same as fdvp.
	if ((filename[0] == '\\')) {
		OBJECT_ATTRIBUTES oa;
		IO_STATUS_BLOCK ioStatus;
		UNICODE_STRING uFileName;
		//RtlInitEmptyUnicodeString(&uFileName, ren->FileName, ren->FileNameLength);  // doesn't set length
		// Is there really no offical wrapper to do this?
		uFileName.Length = uFileName.MaximumLength = ren->FileNameLength;
		uFileName.Buffer = ren->FileName;

		InitializeObjectAttributes(&oa, &uFileName, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
			NULL, NULL);
		
		Status = IoCreateFile(
			&destParentHandle,
			FILE_READ_DATA,
			&oa,
			&ioStatus,
			NULL,
			0,
			FILE_SHARE_READ,
			FILE_OPEN,
			FILE_OPEN_FOR_BACKUP_INTENT,
			NULL,
			0,
			CreateFileTypeNone,
			NULL,
			IO_FORCE_ACCESS_CHECK | IO_OPEN_TARGET_DIRECTORY | IO_NO_PARAMETER_CHECKING
		);

		if (!NT_SUCCESS(Status))
			return STATUS_INVALID_PARAMETER;

		// We have the targetdirectoryparent - get FileObject.
		Status = ObReferenceObjectByHandle(destParentHandle,
			STANDARD_RIGHTS_REQUIRED,
			*IoFileObjectType,
			KernelMode,
			&dFileObject,
			NULL);
		if (!NT_SUCCESS(Status)) {
			ZwClose(destParentHandle);
			return STATUS_INVALID_PARAMETER;
		}

		// All exits need to go through "out:" at this point on.

		// Assign tdvp
		tdvp = dFileObject->FsContext;


		// Hold it
		VERIFY0(VN_HOLD(tdvp));

		// Filename is '\??\E:\dir\dir\file' and we only care about the last part.
		char *r = strrchr(filename, '\\');
		if (r == NULL)
			r = strrchr(filename, '/');
		if (r != NULL) {
			r++;
			filename = r;
		}

		error = zfs_find_dvp_vp(zfsvfs, filename, 1, 0, &remainder, &tdvp, &tvp, 0);
		if (error) {
			Status = STATUS_OBJECTID_NOT_FOUND;
			goto out;
		}
	} else {
		// Name might be just "HEAD" so use fdvp
		use_fdvp_for_tdvp = 1;
	}

	// Goto out will release this
	VN_HOLD(fvp);

	// If we have a "tvp" here, then something exists where we are to rename
	if (tvp && !ren->ReplaceIfExists) {
		error = STATUS_OBJECT_NAME_COLLISION;
		goto out;
	}


	VERIFY(sa_lookup(zp->z_sa_hdl, SA_ZPL_PARENT(zfsvfs),
		&parent, sizeof(parent)) == 0);

	// Fetch fdvp
	error = zfs_zget(zfsvfs, parent, &dzp);
	if (error) {
		error = STATUS_OBJECTID_NOT_FOUND;
		goto out;
	}

	// Lookup name
	if (zp->z_name_cache == NULL) {
		error = STATUS_OBJECTID_NOT_FOUND;
		goto out;
	}

	fdvp = ZTOV(dzp);
	// "tvp" (if not NULL) and "tdvp" is held by zfs_find_dvp_vp

	if (use_fdvp_for_tdvp) {
		tdvp = fdvp;
		VERIFY0(VN_HOLD(tdvp));
	}


	error = zfs_rename(VTOZ(fdvp), &zp->z_name_cache[zp->z_name_offset],
		tdvp, remainder ? remainder : filename,
		NULL, 0);

	if (error == 0) {
		// TODO: rename file in same directory, send OLD_NAME, NEW_NAME
		// Moving to different directory, send: FILE_ACTION_REMOVED, FILE_ACTION_ADDED
		// send CHANGE_LAST_WRITE

		zfs_send_notify(zfsvfs, zp->z_name_cache, zp->z_name_offset,
			vnode_isdir(fvp) ?
			FILE_NOTIFY_CHANGE_DIR_NAME :
			FILE_NOTIFY_CHANGE_FILE_NAME,
			FILE_ACTION_RENAMED_OLD_NAME);

		// Release fromname, and lookup new name
		kmem_free(zp->z_name_cache, zp->z_name_len);
		zp->z_name_cache = NULL;
		if (zfs_build_path(zp, VTOZ(tdvp), &zp->z_name_cache, &zp->z_name_len, &zp->z_name_offset) == 0) {
			zfs_send_notify(zfsvfs, zp->z_name_cache, zp->z_name_offset,
				vnode_isdir(fvp) ?
				FILE_NOTIFY_CHANGE_DIR_NAME :
				FILE_NOTIFY_CHANGE_FILE_NAME,
				FILE_ACTION_RENAMED_NEW_NAME);
		}

		znode_t *tdzp = VTOZ(tdvp);
		zfs_send_notify(zfsvfs, tdzp->z_name_cache, tdzp->z_name_offset,
			FILE_NOTIFY_CHANGE_LAST_WRITE, FILE_ACTION_MODIFIED);

	}
	// Release all holds
out:
	if (destParentHandle != 0)
		ZwClose(destParentHandle);
	if (dFileObject)
		ObDereferenceObject(dFileObject);
	if (tdvp) VN_RELE(tdvp);
	if (fdvp) VN_RELE(fdvp);
	if (fvp) VN_RELE(fvp);
	if (tvp) VN_RELE(tvp);

	return error;
}


/* IRP_MJ_QUERY_INFORMATION helpers */


NTSTATUS file_attribute_tag_information(PDEVICE_OBJECT DeviceObject, PIRP Irp, PIO_STACK_LOCATION IrpSp,
	FILE_ATTRIBUTE_TAG_INFORMATION *tag)
{
	if (IrpSp->Parameters.QueryFile.Length < sizeof(FILE_ATTRIBUTE_TAG_INFORMATION)) {
		Irp->IoStatus.Information = sizeof(FILE_ATTRIBUTE_TAG_INFORMATION);
		return STATUS_BUFFER_TOO_SMALL;
	}

	if (IrpSp->FileObject && IrpSp->FileObject->FsContext) {
		struct vnode *vp = IrpSp->FileObject->FsContext;
		znode_t *zp = VTOZ(vp);
		zfsvfs_t *zfsvfs = zp->z_zfsvfs;

		tag->FileAttributes = zfs_getwinflags(zp);
		if (zp->z_pflags & ZFS_REPARSE) {
			int err;
			uio_t *uio;
			REPARSE_DATA_BUFFER tagdata;
			uio = uio_create(1, 0, UIO_SYSSPACE, UIO_READ);
			uio_addiov(uio, (user_addr_t)&tagdata, sizeof(tagdata));
			err = zfs_readlink(vp, uio, NULL);
			tag->ReparseTag = tagdata.ReparseTag;
			dprintf("Returning tag 0x%x\n", tag->ReparseTag);
			uio_free(uio);
		}
		Irp->IoStatus.Information = sizeof(FILE_ATTRIBUTE_TAG_INFORMATION);
		ASSERT(tag->FileAttributes != 0);
		return STATUS_SUCCESS;
	}
	return STATUS_INVALID_PARAMETER;
}

NTSTATUS file_internal_information(PDEVICE_OBJECT DeviceObject, PIRP Irp, PIO_STACK_LOCATION IrpSp,
	FILE_INTERNAL_INFORMATION *infernal)
{
	if (IrpSp->Parameters.QueryFile.Length < sizeof(FILE_INTERNAL_INFORMATION)) {
		Irp->IoStatus.Information = sizeof(FILE_INTERNAL_INFORMATION);
		return STATUS_BUFFER_TOO_SMALL;
	}

	if (IrpSp->FileObject && IrpSp->FileObject->FsContext) {
		struct vnode *vp = IrpSp->FileObject->FsContext;
		znode_t *zp = VTOZ(vp);
		infernal->IndexNumber.QuadPart = zp->z_id;
		Irp->IoStatus.Information = sizeof(FILE_INTERNAL_INFORMATION);
		return STATUS_SUCCESS;
	}

	return STATUS_NO_SUCH_FILE;
}

NTSTATUS file_basic_information(PDEVICE_OBJECT DeviceObject, PIRP Irp, PIO_STACK_LOCATION IrpSp, FILE_BASIC_INFORMATION *basic)
{
	dprintf("   %s\n", __func__);

	if (IrpSp->Parameters.QueryFile.Length < sizeof(FILE_BASIC_INFORMATION)) {
		Irp->IoStatus.Information = sizeof(FILE_BASIC_INFORMATION);
		return STATUS_BUFFER_TOO_SMALL;
	}

	if (IrpSp->FileObject && IrpSp->FileObject->FsContext) {
		struct vnode *vp = IrpSp->FileObject->FsContext;
		if (VN_HOLD(vp) == 0) {
			znode_t *zp = VTOZ(vp);
			zfsvfs_t *zfsvfs = zp->z_zfsvfs;
			sa_bulk_attr_t bulk[3];
			int count = 0;
			uint64_t mtime[2];
			uint64_t ctime[2];
			uint64_t crtime[2];
			SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_MTIME(zfsvfs), NULL, &mtime, 16);
			SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_CTIME(zfsvfs), NULL, &ctime, 16);
			SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_CRTIME(zfsvfs), NULL, &crtime, 16);
			sa_bulk_lookup(zp->z_sa_hdl, bulk, count);

			TIME_UNIX_TO_WINDOWS(mtime, basic->LastWriteTime.QuadPart);
			TIME_UNIX_TO_WINDOWS(ctime, basic->ChangeTime.QuadPart);
			TIME_UNIX_TO_WINDOWS(crtime, basic->CreationTime.QuadPart);
			TIME_UNIX_TO_WINDOWS(zp->z_atime, basic->LastAccessTime.QuadPart);

			basic->FileAttributes = zfs_getwinflags(zp);
			VN_RELE(vp);
		}
		Irp->IoStatus.Information = sizeof(FILE_BASIC_INFORMATION);
		return STATUS_SUCCESS;
	}

	// This can be called from diskDispatcher, referring to the volume.
	// if so, make something up. Is this the right thing to do?
	if (IrpSp->FileObject && IrpSp->FileObject->FsContext == NULL) {
		LARGE_INTEGER JanOne1980 = { 0xe1d58000,0x01a8e79f };
		ExLocalTimeToSystemTime(&JanOne1980,
			&basic->LastWriteTime);
		basic->CreationTime = basic->LastAccessTime = basic->LastWriteTime;
		basic->FileAttributes = FILE_ATTRIBUTE_NORMAL;
		Irp->IoStatus.Information = sizeof(FILE_BASIC_INFORMATION);
		return STATUS_SUCCESS;
	}

	ASSERT(basic->FileAttributes != 0);
	dprintf("   %s failing\n", __func__);
	return STATUS_OBJECT_NAME_NOT_FOUND;
}

uint64_t zfs_blksz(znode_t *zp)
{
	if (zp->z_blksz)
		return zp->z_blksz;
	if (zp->z_sa_hdl) {
		uint32_t blksize;
		uint64_t nblks;
		sa_object_size(zp->z_sa_hdl, &blksize, &nblks);
		if (blksize)
			return (uint64_t)blksize;
	}

	if (zp->z_zfsvfs->z_max_blksz)
		return zp->z_zfsvfs->z_max_blksz;
	return 512ULL;
}

NTSTATUS file_standard_information(PDEVICE_OBJECT DeviceObject, PIRP Irp, PIO_STACK_LOCATION IrpSp, FILE_STANDARD_INFORMATION *standard)
{
	dprintf("   %s\n", __func__);

	if (IrpSp->Parameters.QueryFile.Length < sizeof(FILE_STANDARD_INFORMATION)) {
		Irp->IoStatus.Information = sizeof(FILE_STANDARD_INFORMATION);
		return STATUS_BUFFER_TOO_SMALL;
	}

	standard->Directory = TRUE;
	standard->AllocationSize.QuadPart = 512;  // space taken on disk, multiples of block size
	standard->EndOfFile.QuadPart = 512;       // byte size of file
	standard->DeletePending = FALSE;
	standard->NumberOfLinks = 1;
	if (IrpSp->FileObject && IrpSp->FileObject->FsContext) {
		struct vnode *vp = IrpSp->FileObject->FsContext;
		zfs_dirlist_t *zccb = IrpSp->FileObject->FsContext2;
		VN_HOLD(vp);
		znode_t *zp = VTOZ(vp);
		standard->Directory = vnode_isdir(vp) ? TRUE : FALSE;
		//         sa_object_size(zp->z_sa_hdl, &blksize, &nblks);
		uint64_t blk = zfs_blksz(zp);
		standard->AllocationSize.QuadPart = P2ROUNDUP(zp->z_size ? zp->z_size : 1, blk);  // space taken on disk, multiples of block size
		//standard->AllocationSize.QuadPart = zp->z_size;  // space taken on disk, multiples of block size
		standard->EndOfFile.QuadPart = vnode_isdir(vp) ? 0 : zp->z_size;       // byte size of file
		standard->NumberOfLinks = zp->z_links;
		standard->DeletePending = zccb && zccb->deleteonclose ? TRUE : FALSE;
		VN_RELE(vp);
		dprintf("Returning size %llu and allocsize %llu\n",
			standard->EndOfFile.QuadPart, standard->AllocationSize.QuadPart);
		Irp->IoStatus.Information = sizeof(FILE_STANDARD_INFORMATION);
		return STATUS_SUCCESS;
	}
	return STATUS_OBJECT_NAME_NOT_FOUND;
}

NTSTATUS file_position_information(PDEVICE_OBJECT DeviceObject, PIRP Irp, PIO_STACK_LOCATION IrpSp, FILE_POSITION_INFORMATION *position)
{
	dprintf("   %s\n", __func__);

	if (IrpSp->Parameters.QueryFile.Length < sizeof(FILE_POSITION_INFORMATION)) {
		Irp->IoStatus.Information = sizeof(FILE_POSITION_INFORMATION);
		return STATUS_BUFFER_TOO_SMALL;
	}

	if (IrpSp->FileObject)
		position->CurrentByteOffset.QuadPart = IrpSp->FileObject->CurrentByteOffset.QuadPart;

	Irp->IoStatus.Information = sizeof(FILE_POSITION_INFORMATION);
	return STATUS_SUCCESS;
}

NTSTATUS file_ea_information(PDEVICE_OBJECT DeviceObject, PIRP Irp, PIO_STACK_LOCATION IrpSp, FILE_EA_INFORMATION *ea)
{
	NTSTATUS Status = STATUS_INVALID_PARAMETER;

	dprintf("   %s\n", __func__);
	if (IrpSp->Parameters.QueryFile.Length < sizeof(FILE_EA_INFORMATION)) {
		Irp->IoStatus.Information = sizeof(FILE_EA_INFORMATION);
		return STATUS_BUFFER_TOO_SMALL;
	}

	ea->EaSize = 0;

	if (IrpSp->FileObject && IrpSp->FileObject->FsContext) {
		struct vnode *vp = IrpSp->FileObject->FsContext;

		ea->EaSize = xattr_getsize(vp);

		dprintf("%s: returning size %d / 0x%x\n", __func__,
			ea->EaSize, ea->EaSize);

		Irp->IoStatus.Information = sizeof(FILE_EA_INFORMATION);
		return STATUS_SUCCESS;
	}

	return STATUS_INVALID_PARAMETER;
}

NTSTATUS file_network_open_information(PDEVICE_OBJECT DeviceObject, PIRP Irp, PIO_STACK_LOCATION IrpSp, FILE_NETWORK_OPEN_INFORMATION *netopen)
{
	dprintf("   %s\n", __func__);

	if (IrpSp->Parameters.QueryFile.Length < sizeof(FILE_NETWORK_OPEN_INFORMATION)) {
		Irp->IoStatus.Information = sizeof(FILE_NETWORK_OPEN_INFORMATION);
		return STATUS_BUFFER_TOO_SMALL;
	}

	if (IrpSp->FileObject && IrpSp->FileObject->FsContext) {
		struct vnode *vp = IrpSp->FileObject->FsContext;
		znode_t *zp = VTOZ(vp);
		zfsvfs_t *zfsvfs = zp->z_zfsvfs;
		sa_bulk_attr_t bulk[3];
		int count = 0;
		uint64_t mtime[2];
		uint64_t ctime[2];
		uint64_t crtime[2];
		SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_MTIME(zfsvfs), NULL, &mtime, 16);
		SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_CTIME(zfsvfs), NULL, &ctime, 16);
		SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_CRTIME(zfsvfs), NULL, &crtime, 16);
		sa_bulk_lookup(zp->z_sa_hdl, bulk, count);

		TIME_UNIX_TO_WINDOWS(mtime, netopen->LastWriteTime.QuadPart);
		TIME_UNIX_TO_WINDOWS(ctime, netopen->ChangeTime.QuadPart);
		TIME_UNIX_TO_WINDOWS(crtime, netopen->CreationTime.QuadPart);
		TIME_UNIX_TO_WINDOWS(zp->z_atime, netopen->LastAccessTime.QuadPart);
		netopen->AllocationSize.QuadPart = P2ROUNDUP(zp->z_size, zfs_blksz(zp));
		netopen->EndOfFile.QuadPart = vnode_isdir(vp) ? 0 : zp->z_size;
		netopen->FileAttributes = zfs_getwinflags(zp);
		Irp->IoStatus.Information = sizeof(FILE_NETWORK_OPEN_INFORMATION);
		return STATUS_SUCCESS;
	}

	return STATUS_OBJECT_PATH_NOT_FOUND;
}

NTSTATUS file_standard_link_information(PDEVICE_OBJECT DeviceObject, PIRP Irp, PIO_STACK_LOCATION IrpSp, FILE_STANDARD_LINK_INFORMATION *fsli)
{
	PFILE_OBJECT FileObject = IrpSp->FileObject;

	dprintf("   %s\n", __func__);

	if (IrpSp->Parameters.QueryFile.Length < sizeof(FILE_STANDARD_LINK_INFORMATION)) {
		Irp->IoStatus.Information = sizeof(FILE_STANDARD_LINK_INFORMATION);
		return STATUS_BUFFER_TOO_SMALL;
	}

	if (IrpSp->FileObject && IrpSp->FileObject->FsContext) {
		struct vnode *vp = IrpSp->FileObject->FsContext;
		zfs_dirlist_t *zccb = IrpSp->FileObject->FsContext2;

		znode_t *zp = VTOZ(vp);

		fsli->NumberOfAccessibleLinks = zp->z_links;
		fsli->TotalNumberOfLinks = zp->z_links;
		fsli->DeletePending = zccb && zccb->deleteonclose ? TRUE : FALSE;
		fsli->Directory = S_ISDIR(zp->z_mode);
	}

	Irp->IoStatus.Information = sizeof(FILE_STANDARD_LINK_INFORMATION);
	return STATUS_SUCCESS;
}

NTSTATUS file_id_information(PDEVICE_OBJECT DeviceObject, PIRP Irp, PIO_STACK_LOCATION IrpSp, FILE_ID_INFORMATION *fii)
{
	PFILE_OBJECT FileObject = IrpSp->FileObject;

	dprintf("   %s\n", __func__);
	if (IrpSp->Parameters.QueryFile.Length < sizeof(FILE_ID_INFORMATION)) {
		Irp->IoStatus.Information = sizeof(FILE_ID_INFORMATION);
		return STATUS_BUFFER_TOO_SMALL;
	}

	struct vnode *vp = FileObject->FsContext;

	znode_t *zp = VTOZ(vp);
	zfsvfs_t *zfsvfs = zp->z_zfsvfs;

	fii->VolumeSerialNumber = 0x19831116;

	RtlCopyMemory(&fii->FileId.Identifier[0], &zp->z_id, sizeof(UINT64));
	uint64_t guid = dmu_objset_fsid_guid(zfsvfs->z_os);
	RtlCopyMemory(&fii->FileId.Identifier[sizeof(UINT64)], &guid, sizeof(UINT64));

	Irp->IoStatus.Information = sizeof(FILE_ID_INFORMATION);
	return STATUS_SUCCESS;
}

NTSTATUS file_case_sensitive_information(PDEVICE_OBJECT DeviceObject, PIRP Irp, PIO_STACK_LOCATION IrpSp, FILE_CASE_SENSITIVE_INFORMATION *fcsi)
{
	PFILE_OBJECT FileObject = IrpSp->FileObject;

	dprintf("   %s\n", __func__);

	if (IrpSp->Parameters.QueryFile.Length < sizeof(FILE_CASE_SENSITIVE_INFORMATION)) {
		Irp->IoStatus.Information = sizeof(FILE_CASE_SENSITIVE_INFORMATION);
		return STATUS_BUFFER_TOO_SMALL;
	}

	struct vnode *vp = FileObject->FsContext;

	znode_t *zp = VTOZ(vp);
	zfsvfs_t *zfsvfs = zp->z_zfsvfs;

	fcsi->Flags = 0;
	if (zfsvfs->z_case == ZFS_CASE_SENSITIVE)
		fcsi->Flags |= FILE_CS_FLAG_CASE_SENSITIVE_DIR;

	Irp->IoStatus.Information = sizeof(FILE_CASE_SENSITIVE_INFORMATION);

	return STATUS_SUCCESS;
}

NTSTATUS file_stat_information(PDEVICE_OBJECT DeviceObject, PIRP Irp, PIO_STACK_LOCATION IrpSp, FILE_STAT_INFORMATION *fsi)
{
	PFILE_OBJECT FileObject = IrpSp->FileObject;

	dprintf("   %s\n", __func__);

	/* vp is already help in query_information */
	struct vnode *vp = FileObject->FsContext;

	if (vp) {

		znode_t *zp = VTOZ(vp);
		zfsvfs_t *zfsvfs = zp->z_zfsvfs;

		sa_bulk_attr_t bulk[3];
		int count = 0;
		uint64_t mtime[2];
		uint64_t ctime[2];
		uint64_t crtime[2];
		SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_MTIME(zfsvfs), NULL, &mtime, 16);
		SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_CTIME(zfsvfs), NULL, &ctime, 16);
		SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_CRTIME(zfsvfs), NULL, &crtime, 16);
		sa_bulk_lookup(zp->z_sa_hdl, bulk, count);

		fsi->FileId.QuadPart = zp->z_id;
		TIME_UNIX_TO_WINDOWS(crtime, fsi->CreationTime.QuadPart);
		TIME_UNIX_TO_WINDOWS(zp->z_atime, fsi->LastAccessTime.QuadPart);
		TIME_UNIX_TO_WINDOWS(mtime, fsi->LastWriteTime.QuadPart);
		TIME_UNIX_TO_WINDOWS(ctime, fsi->ChangeTime.QuadPart);
		fsi->AllocationSize.QuadPart = P2ROUNDUP(zp->z_size, zfs_blksz(zp));
		fsi->EndOfFile.QuadPart = zp->z_size;
		fsi->FileAttributes = zfs_getwinflags(zp);
		fsi->ReparseTag = 0;
		fsi->NumberOfLinks = zp->z_links;
		fsi->EffectiveAccess = GENERIC_ALL;
	}

	return STATUS_SUCCESS;
}

// Convert ZFS (Unix) mode to Windows mode.
ULONG ZMODE2WMODE(mode_t z)
{
	ULONG w = 0;

	if (S_ISDIR(z)) w |= 0x4000; // _S_IFDIR
	if (S_ISREG(z)) w |= 0x8000; // _S_IFREG
	if (S_ISCHR(z)) w |= 0x2000; // _S_IFCHR
	if (S_ISFIFO(z)) w |= 0x1000; // _S_IFIFO
	if ((z&S_IRUSR) == S_IRUSR) w |= 0x0100; // _S_IREAD
	if ((z&S_IWUSR) == S_IWUSR) w |= 0x0080; // _S_IWRITE
	if ((z&S_IXUSR) == S_IXUSR) w |= 0x0040; // _S_IEXEC
	// Couldn't find documentation for the following, but
	// tested in lx/ubuntu to be correct.
	if ((z&S_IRGRP) == S_IRGRP) w |= 0x0020; //
	if ((z&S_IWGRP) == S_IWGRP) w |= 0x0010; //
	if ((z&S_IXGRP) == S_IXGRP) w |= 0x0008; //
	if ((z&S_IROTH) == S_IROTH) w |= 0x0004; //
	if ((z&S_IWOTH) == S_IWOTH) w |= 0x0002; //
	if ((z&S_IXOTH) == S_IXOTH) w |= 0x0001; //
	return w;
}

NTSTATUS file_stat_lx_information(PDEVICE_OBJECT DeviceObject, PIRP Irp, PIO_STACK_LOCATION IrpSp, FILE_STAT_LX_INFORMATION *fsli)
{
	PFILE_OBJECT FileObject = IrpSp->FileObject;

	dprintf("   %s\n", __func__);

	/* vp is already help in query_information */
	struct vnode *vp = FileObject->FsContext;

	if (vp) {
		znode_t *zp = VTOZ(vp);
		zfsvfs_t *zfsvfs = zp->z_zfsvfs;

		sa_bulk_attr_t bulk[3];
		int count = 0;
		uint64_t mtime[2];
		uint64_t ctime[2];
		uint64_t crtime[2];
		SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_MTIME(zfsvfs), NULL, &mtime, 16);
		SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_CTIME(zfsvfs), NULL, &ctime, 16);
		SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_CRTIME(zfsvfs), NULL, &crtime, 16);
		sa_bulk_lookup(zp->z_sa_hdl, bulk, count);

		fsli->FileId.QuadPart = zp->z_id;
		TIME_UNIX_TO_WINDOWS(crtime, fsli->CreationTime.QuadPart);
		TIME_UNIX_TO_WINDOWS(zp->z_atime, fsli->LastAccessTime.QuadPart);
		TIME_UNIX_TO_WINDOWS(mtime, fsli->LastWriteTime.QuadPart);
		TIME_UNIX_TO_WINDOWS(ctime, fsli->ChangeTime.QuadPart);
		fsli->AllocationSize.QuadPart = P2ROUNDUP(zp->z_size, zfs_blksz(zp));
		fsli->EndOfFile.QuadPart = zp->z_size;
		fsli->FileAttributes = zfs_getwinflags(zp);
		fsli->ReparseTag = 0;
		fsli->NumberOfLinks = zp->z_links;
		fsli->EffectiveAccess = SPECIFIC_RIGHTS_ALL | ACCESS_SYSTEM_SECURITY;
		fsli->LxFlags = LX_FILE_METADATA_HAS_UID | LX_FILE_METADATA_HAS_GID | LX_FILE_METADATA_HAS_MODE;
		if (zfsvfs->z_case == ZFS_CASE_SENSITIVE) fsli->LxFlags |= LX_FILE_CASE_SENSITIVE_DIR;
		fsli->LxUid = zp->z_uid;
		fsli->LxGid = zp->z_gid;
		fsli->LxMode = ZMODE2WMODE(zp->z_mode);
		fsli->LxDeviceIdMajor = 0;
		fsli->LxDeviceIdMinor = 0;
	}
	return STATUS_SUCCESS;
}

//
// If overflow, set Information to input_size and NameLength to required size.
//
NTSTATUS file_name_information(PDEVICE_OBJECT DeviceObject, PIRP Irp, PIO_STACK_LOCATION IrpSp, FILE_NAME_INFORMATION *name, PULONG usedspace, int normalize)
{
	PFILE_OBJECT FileObject = IrpSp->FileObject;

	dprintf("* %s: (normalize %d)\n", __func__, normalize);

	if (FileObject == NULL || FileObject->FsContext == NULL)
		return STATUS_INVALID_PARAMETER;

	if (IrpSp->Parameters.QueryFile.Length < (ULONG)FIELD_OFFSET(FILE_NAME_INFORMATION, FileName[0])) {
		Irp->IoStatus.Information = sizeof(FILE_NAME_INFORMATION);
		return STATUS_BUFFER_TOO_SMALL;
	}

	struct vnode *vp = FileObject->FsContext;
	znode_t *zp = VTOZ(vp);
	char strname[MAXPATHLEN + 2];
	int error = 0;
	uint64_t parent = 0;

	ASSERT(zp != NULL);

	zfsvfs_t *zfsvfs = zp->z_zfsvfs;
	NTSTATUS Status = STATUS_SUCCESS;

	VN_HOLD(vp);

	if (zp->z_id == zfsvfs->z_root) {
		strlcpy(strname, "\\", MAXPATHLEN);
	} else {

		// Should never be unset!
		if (zp->z_name_cache == NULL) {
			dprintf("%s: name not set path taken\n", __func__);
			if (zfs_build_path(zp, NULL, &zp->z_name_cache, &zp->z_name_len, &zp->z_name_offset) == -1) {
				dprintf("%s: failed to build fullpath\n", __func__);
				return STATUS_OBJECT_PATH_NOT_FOUND;
			}
		}

		// Safety
		if (zp->z_name_cache != NULL) {
			strlcpy(strname, zp->z_name_cache,
				MAXPATHLEN);

			// If it is a DIR, make sure it ends with "\", except for
			// root, that is just "\"
			if (S_ISDIR(zp->z_mode))
				strlcat(strname, "\\",
					MAXPATHLEN);
		}
	}
	VN_RELE(vp);

	// Convert name, setting FileNameLength to how much we need
	error = RtlUTF8ToUnicodeN(NULL, 0, &name->FileNameLength, strname, strlen(strname));
	//ASSERT(strlen(strname)*2 == name->FileNameLength);
	dprintf("%s: remaining space %d str.len %d struct size %d\n", __func__, IrpSp->Parameters.QueryFile.Length,
		name->FileNameLength, sizeof(FILE_NAME_INFORMATION));
	// CHECK ERROR here.
	// Calculate how much room there is for filename, after the struct and its first wchar
	int space = IrpSp->Parameters.QueryFile.Length - FIELD_OFFSET(FILE_NAME_INFORMATION, FileName);
	space = MIN(space, name->FileNameLength);

	ASSERT(space >= 0);

	// Copy over as much as we can, including the first wchar
	error = RtlUTF8ToUnicodeN(name->FileName, space /* + sizeof(name->FileName) */, NULL, strname, strlen(strname));

	if (space < name->FileNameLength)
		Status = STATUS_BUFFER_OVERFLOW;
	else
		Status = STATUS_SUCCESS;


	// Return how much of the filename we copied after the first wchar
	// which is used with sizeof(struct) to work out how much bigger the return is.
	if (usedspace) *usedspace = space; // Space will always be 2 or more, since struct has room for 1 wchar

	dprintf("* %s: %s name of '%.*S' struct size 0x%x and FileNameLength 0x%x Usedspace 0x%x\n", __func__,
		Status == STATUS_BUFFER_OVERFLOW ? "partial" : "",
		space / 2, name->FileName,
		sizeof(FILE_NAME_INFORMATION), name->FileNameLength, space);

	return Status;
}

// This function is not used - left in as example. If you think
// something is not working due to missing FileRemoteProtocolInformation
// then think again. This is not the problem.
NTSTATUS file_remote_protocol_information(PDEVICE_OBJECT DeviceObject, PIRP Irp, PIO_STACK_LOCATION IrpSp, FILE_REMOTE_PROTOCOL_INFORMATION *frpi)
{
	dprintf("   %s\n", __func__);

	if (IrpSp->Parameters.QueryFile.Length < sizeof(FILE_REMOTE_PROTOCOL_INFORMATION)) {
		Irp->IoStatus.Information = sizeof(FILE_REMOTE_PROTOCOL_INFORMATION);
		return STATUS_BUFFER_TOO_SMALL;
	}

	frpi->StructureVersion = 4;
	frpi->StructureSize = sizeof(FILE_REMOTE_PROTOCOL_INFORMATION);
	frpi->Protocol = WNNC_NET_GOOGLE;
	frpi->ProtocolMajorVersion = 1;
	frpi->ProtocolMinorVersion = 0;
	frpi->ProtocolRevision = 3;
	frpi->Flags = REMOTE_PROTOCOL_FLAG_LOOPBACK;
	Irp->IoStatus.Information = sizeof(FILE_REMOTE_PROTOCOL_INFORMATION);
	return STATUS_SUCCESS;
}

// Insert a streamname into an output buffer, if there is room,
// StreamNameLength is always the FULL name length, even when we only
// fit partial.
// Return 0 for OK, 1 for overflow.
int zfswin_insert_streamname(char *streamname, uint8_t *outbuffer, DWORD **lastNextEntryOffset,
	uint64_t availablebytes, uint64_t *spaceused, uint64_t streamsize)
{
	/*
	 typedef struct _FILE_STREAM_INFO {
		  DWORD         NextEntryOffset;
		  DWORD         StreamNameLength;
		  LARGE_INTEGER StreamSize;
		  LARGE_INTEGER StreamAllocationSize;
		  WCHAR         StreamName[1];
	 } FILE_STREAM_INFO, *PFILE_STREAM_INFO;
	*/
	// The first stream struct we assume is already aligned, but further ones
	// should be padded here.
	FILE_STREAM_INFORMATION *stream = NULL;
	int overflow = 0;

	// If not first struct, align outsize to 8 byte - 0 aligns to 0.
	*spaceused = (((*spaceused) + 7) & ~7);

	// Convert filename, to get space required.
	ULONG needed_streamnamelen;
	int error;

	// Check error? Do we care about convertion errors?
	error = RtlUTF8ToUnicodeN(NULL, 0, &needed_streamnamelen, streamname, strlen(streamname));

	// Is there room? We have to add the struct if there is room for it
	// and fill it out as much as possible, and copy in as much of the name
	// as we can.

	if (*spaceused + sizeof(FILE_STREAM_INFORMATION) <= availablebytes) {
		stream = (FILE_STREAM_INFORMATION *)&outbuffer[*spaceused];

		// Room for one more struct, update privious's next ptr
		if (*lastNextEntryOffset != NULL) {
			// Update previous structure to point to this one. 
			**lastNextEntryOffset = (DWORD)*spaceused;
		}


		// Directly set next to 0, assuming this will be last record
		stream->NextEntryOffset = 0;

		// remember this struct's NextEntry, so the next one can fill it in.
		*lastNextEntryOffset = &stream->NextEntryOffset;

		// Set all the fields now
		stream->StreamSize.QuadPart = streamsize;
		stream->StreamAllocationSize.QuadPart = P2ROUNDUP(streamsize, 512);

		// Return the total name length
		stream->StreamNameLength = needed_streamnamelen + 1 * sizeof(WCHAR); // + ":"

		// Consume the space of the struct
		*spaceused += FIELD_OFFSET(FILE_STREAM_INFORMATION, StreamName);

		uint64_t roomforname;
		if (*spaceused + stream->StreamNameLength <= availablebytes) {
			roomforname = stream->StreamNameLength;
		}
		else {
			roomforname = availablebytes - *spaceused;
			overflow = 1;
		}

		// Consume the space of (partial?) filename
		*spaceused += roomforname;

		// Now copy out as much of the filename as can fit.
		// We need to real full length in StreamNameLength
		// There is always room for 1 char
		stream->StreamName[0] = L':';
		roomforname -= sizeof(WCHAR);

		// Convert as much as we can, accounting for the start ":"
		error = RtlUTF8ToUnicodeN(&stream->StreamName[1], roomforname, NULL, streamname, strlen(streamname));

		dprintf("%s: added %s streamname '%s'\n", __func__,
			overflow ? "(partial)" : "", streamname);
	}
	else {
		dprintf("%s: no room for  '%s'\n", __func__, streamname);
		overflow = 1;
	}

	return overflow;
}

//
// If overflow, set Information to input_size and NameLength to required size.
//
NTSTATUS file_stream_information(PDEVICE_OBJECT DeviceObject, PIRP Irp, PIO_STACK_LOCATION IrpSp, FILE_STREAM_INFORMATION *stream, PULONG usedspace)
{
	PFILE_OBJECT FileObject = IrpSp->FileObject;
	NTSTATUS Status;
	void *outbuffer = Irp->AssociatedIrp.SystemBuffer;
	uint64_t availablebytes = IrpSp->Parameters.QueryFile.Length;
	DWORD *lastNextEntryOffset = NULL;
	int overflow = 0;

	dprintf("%s: \n", __func__);

	if (FileObject == NULL || FileObject->FsContext == NULL)
		return STATUS_INVALID_PARAMETER;

	if (IrpSp->Parameters.QueryFile.Length < sizeof(FILE_STREAM_INFORMATION)) {
		Irp->IoStatus.Information = sizeof(FILE_STREAM_INFORMATION);
		return STATUS_BUFFER_TOO_SMALL;
	}

	struct vnode *vp = FileObject->FsContext, *xvp = NULL;
	znode_t *zp = VTOZ(vp);
	zfsvfs_t *zfsvfs = zp->z_zfsvfs;

	// This exits when unmounting
	ZFS_ENTER(zfsvfs);

	struct vnode *xdvp = NULL;
	void *cr = NULL;
	uint64_t spaceused = 0;
	zap_cursor_t  zc;
	objset_t  *os;
	zap_attribute_t  za;

	// Iterate the xattrs.

	// Add a record for this name, if there is room. Keep a 
	// count of how much space would need. insert_xattrname adds first ":" and ":$DATA"
	overflow = zfswin_insert_streamname(":$DATA", outbuffer, &lastNextEntryOffset, availablebytes, &spaceused, zp->z_size);

	/* Grab the hidden attribute directory vnode. */
	if (zfs_get_xattrdir(zp, &xdvp, cr, 0) != 0) {
		goto out;
	}
	os = zfsvfs->z_os;

	for (zap_cursor_init(&zc, os, VTOZ(xdvp)->z_id);
		zap_cursor_retrieve(&zc, &za) == 0; zap_cursor_advance(&zc)) {

		if (!xattr_stream(za.za_name))
			continue;	 /* skip */

		// We need to lookup the size of the xattr.
		int error = zfs_dirlook(VTOZ(xdvp), za.za_name, &xvp, 0, NULL, NULL);

		overflow += zfswin_insert_streamname(za.za_name, outbuffer, &lastNextEntryOffset, availablebytes, &spaceused,
			xvp ? VTOZ(xvp)->z_size : 0);

		if (error == 0) VN_RELE(xvp);

	}

	zap_cursor_fini(&zc);

out:
	if (xdvp) {
		VN_RELE(xdvp);
	}

	ZFS_EXIT(zfsvfs);

	if (overflow > 0)
		Status = STATUS_BUFFER_OVERFLOW;
	else
		Status = STATUS_SUCCESS;

	// Set to how space we used.
	Irp->IoStatus.Information = spaceused;

	return Status;
}


/* IRP_MJ_DEVICE_CONTROL helpers */


NTSTATUS QueryCapabilities(PDEVICE_OBJECT DeviceObject, PIRP Irp, PIO_STACK_LOCATION IrpSp)
{
	NTSTATUS				Status;
	PDEVICE_CAPABILITIES	DeviceCapabilities;
	DeviceCapabilities = IrpSp->Parameters.DeviceCapabilities.Capabilities;
	DeviceCapabilities->SurpriseRemovalOK = TRUE;
	DeviceCapabilities->LockSupported = TRUE;
	DeviceCapabilities->EjectSupported = TRUE;
	DeviceCapabilities->Removable = FALSE; // XX
	DeviceCapabilities->DockDevice = FALSE;
	DeviceCapabilities->D1Latency = DeviceCapabilities->D2Latency = DeviceCapabilities->D3Latency = 0;
	DeviceCapabilities->NoDisplayInUI = 0;
	Irp->IoStatus.Information = sizeof(DEVICE_CAPABILITIES);

	return STATUS_SUCCESS;
}

//
// If overflow, set Information to sizeof(MOUNTDEV_NAME), and NameLength to required size.
//
NTSTATUS ioctl_query_device_name(PDEVICE_OBJECT DeviceObject, PIRP Irp, PIO_STACK_LOCATION IrpSp)
{
	// Return name in MOUNTDEV_NAME
	PMOUNTDEV_NAME name;
	mount_t *zmo;
	NTSTATUS Status;

	if (IrpSp->Parameters.DeviceIoControl.OutputBufferLength < sizeof(MOUNTDEV_NAME)) {
		Irp->IoStatus.Information = sizeof(MOUNTDEV_NAME);
		return STATUS_BUFFER_TOO_SMALL;
	}

	zmo = (mount_t *)DeviceObject->DeviceExtension;

	name = Irp->AssociatedIrp.SystemBuffer;

	int space = IrpSp->Parameters.DeviceIoControl.OutputBufferLength - sizeof(MOUNTDEV_NAME);
	space = MIN(space, zmo->device_name.Length);
	name->NameLength = zmo->device_name.Length;
	RtlCopyMemory(name->Name, zmo->device_name.Buffer, space + sizeof(name->Name));
	Irp->IoStatus.Information = sizeof(MOUNTDEV_NAME) + space;

	if (space < zmo->device_name.Length - sizeof(name->Name))
		Status = STATUS_BUFFER_OVERFLOW;
	else
		Status = STATUS_SUCCESS;
	ASSERT(Irp->IoStatus.Information <= IrpSp->Parameters.DeviceIoControl.OutputBufferLength);

	dprintf("replying with '%.*S'\n", space + sizeof(name->Name) / sizeof(WCHAR), name->Name);

	return Status;
}

NTSTATUS ioctl_disk_get_drive_geometry(PDEVICE_OBJECT DeviceObject, PIRP Irp, PIO_STACK_LOCATION IrpSp)
{
	dprintf("%s: \n", __func__);
	if (IrpSp->Parameters.DeviceIoControl.OutputBufferLength < sizeof(DISK_GEOMETRY)) {
		Irp->IoStatus.Information = sizeof(DISK_GEOMETRY);
		return STATUS_BUFFER_TOO_SMALL;
	}

	mount_t *zmo = DeviceObject->DeviceExtension;
	if (!zmo ||
		(zmo->type != MOUNT_TYPE_VCB &&
			zmo->type != MOUNT_TYPE_DCB)) {
		return STATUS_INVALID_PARAMETER;
	}

	zfsvfs_t *zfsvfs = vfs_fsprivate(zmo);
	if (zfsvfs == NULL)
		return STATUS_INVALID_PARAMETER;

	ZFS_ENTER(zfsvfs);  // This returns EIO if fail

	uint64_t refdbytes, availbytes, usedobjs, availobjs;
	dmu_objset_space(zfsvfs->z_os,
		&refdbytes, &availbytes, &usedobjs, &availobjs);

	DISK_GEOMETRY *geom = Irp->AssociatedIrp.SystemBuffer;

	geom->BytesPerSector = 512;
	geom->SectorsPerTrack = 1;
	geom->TracksPerCylinder = 1;
	geom->Cylinders.QuadPart = (availbytes + refdbytes) / 512;
	geom->MediaType = FixedMedia;
	ZFS_EXIT(zfsvfs);

	Irp->IoStatus.Information = sizeof(DISK_GEOMETRY);
	return STATUS_SUCCESS;
}

// This is how Windows Samples handle it
typedef struct _DISK_GEOMETRY_EX_INTERNAL {
	DISK_GEOMETRY Geometry;
	LARGE_INTEGER DiskSize;
	DISK_PARTITION_INFO Partition;
	DISK_DETECTION_INFO Detection;
} DISK_GEOMETRY_EX_INTERNAL, *PDISK_GEOMETRY_EX_INTERNAL;

NTSTATUS ioctl_disk_get_drive_geometry_ex(PDEVICE_OBJECT DeviceObject, PIRP Irp, PIO_STACK_LOCATION IrpSp)
{
	dprintf("%s: \n", __func__);
	if (IrpSp->Parameters.DeviceIoControl.OutputBufferLength < FIELD_OFFSET(DISK_GEOMETRY_EX, Data)) {
		Irp->IoStatus.Information = sizeof(DISK_GEOMETRY_EX);
		return STATUS_BUFFER_TOO_SMALL;
	}

	mount_t *zmo = DeviceObject->DeviceExtension;
	if (!zmo ||
		(zmo->type != MOUNT_TYPE_VCB &&
			zmo->type != MOUNT_TYPE_DCB)) {
		return STATUS_INVALID_PARAMETER;
	}

	zfsvfs_t *zfsvfs = vfs_fsprivate(zmo);
	if (zfsvfs == NULL)
		return STATUS_INVALID_PARAMETER;

	ZFS_ENTER(zfsvfs);  // This returns EIO if fail

	uint64_t refdbytes, availbytes, usedobjs, availobjs;
	dmu_objset_space(zfsvfs->z_os,
		&refdbytes, &availbytes, &usedobjs, &availobjs);


	DISK_GEOMETRY_EX_INTERNAL *geom = Irp->AssociatedIrp.SystemBuffer;
	geom->DiskSize.QuadPart = availbytes + refdbytes;
	geom->Geometry.BytesPerSector = 512;
	geom->Geometry.MediaType = FixedMedia;

	if (IrpSp->Parameters.DeviceIoControl.OutputBufferLength >= FIELD_OFFSET(DISK_GEOMETRY_EX_INTERNAL, Detection)) {
		geom->Partition.SizeOfPartitionInfo = sizeof(geom->Partition);
		geom->Partition.PartitionStyle = PARTITION_STYLE_GPT;
		//geom->Partition.Gpt.DiskId = 0;
	}
	if (IrpSp->Parameters.DeviceIoControl.OutputBufferLength >= sizeof(DISK_GEOMETRY_EX_INTERNAL)) {
		geom->Detection.SizeOfDetectInfo = sizeof(geom->Detection);

	}
	ZFS_EXIT(zfsvfs);

	Irp->IoStatus.Information = MIN(IrpSp->Parameters.DeviceIoControl.OutputBufferLength, sizeof(DISK_GEOMETRY_EX_INTERNAL));
	return STATUS_SUCCESS;
}

NTSTATUS ioctl_disk_get_partition_info(PDEVICE_OBJECT DeviceObject, PIRP Irp, PIO_STACK_LOCATION IrpSp)
{
	dprintf("%s: \n", __func__);

	if (IrpSp->Parameters.DeviceIoControl.OutputBufferLength < sizeof(PARTITION_INFORMATION)) {
		Irp->IoStatus.Information = sizeof(PARTITION_INFORMATION);
		return STATUS_BUFFER_TOO_SMALL;
	}

	mount_t *zmo = DeviceObject->DeviceExtension;
	if (!zmo ||
		(zmo->type != MOUNT_TYPE_VCB &&
			zmo->type != MOUNT_TYPE_DCB)) {
		return STATUS_INVALID_PARAMETER;
	}

	zfsvfs_t *zfsvfs = vfs_fsprivate(zmo);
	if (zfsvfs == NULL)
		return STATUS_INVALID_PARAMETER;

	ZFS_ENTER(zfsvfs);  // This returns EIO if fail

	uint64_t refdbytes, availbytes, usedobjs, availobjs;
	dmu_objset_space(zfsvfs->z_os,
		&refdbytes, &availbytes, &usedobjs, &availobjs);

	PARTITION_INFORMATION *part = Irp->AssociatedIrp.SystemBuffer;

	part->PartitionLength.QuadPart = availbytes + refdbytes;
	part->StartingOffset.QuadPart = 0;
	part->BootIndicator = FALSE;
	part->PartitionNumber = (ULONG)(-1L);
	part->HiddenSectors = (ULONG)(1L);
	part->RecognizedPartition = TRUE;
	part->RewritePartition = FALSE;
	part->PartitionType = 'ZFS';

	ZFS_EXIT(zfsvfs);

	Irp->IoStatus.Information = sizeof(PARTITION_INFORMATION);

	return STATUS_SUCCESS;
}

NTSTATUS ioctl_disk_get_partition_info_ex(PDEVICE_OBJECT DeviceObject, PIRP Irp, PIO_STACK_LOCATION IrpSp)
{
	dprintf("%s: \n", __func__);

	if (IrpSp->Parameters.DeviceIoControl.OutputBufferLength < sizeof(PARTITION_INFORMATION_EX)) {
		Irp->IoStatus.Information = sizeof(PARTITION_INFORMATION_EX);
		return STATUS_BUFFER_TOO_SMALL;
	}

	mount_t *zmo = DeviceObject->DeviceExtension;
	if (!zmo ||
		(zmo->type != MOUNT_TYPE_VCB &&
			zmo->type != MOUNT_TYPE_DCB)) {
		return STATUS_INVALID_PARAMETER;
	}

	zfsvfs_t *zfsvfs = vfs_fsprivate(zmo);
	if (zfsvfs == NULL)
		return STATUS_INVALID_PARAMETER;

	ZFS_ENTER(zfsvfs);  // This returns EIO if fail

	uint64_t refdbytes, availbytes, usedobjs, availobjs;
	dmu_objset_space(zfsvfs->z_os,
		&refdbytes, &availbytes, &usedobjs, &availobjs);

	PARTITION_INFORMATION_EX *part = Irp->AssociatedIrp.SystemBuffer;

	part->PartitionStyle = PARTITION_STYLE_MBR;
	part->RewritePartition = FALSE;
	part->Mbr.RecognizedPartition = FALSE;
	part->Mbr.PartitionType = PARTITION_ENTRY_UNUSED;
	part->Mbr.BootIndicator = FALSE;
	part->Mbr.HiddenSectors = 0;
	part->StartingOffset.QuadPart = 0;
	part->PartitionLength.QuadPart = availbytes + refdbytes;
	part->PartitionNumber = 0;

	ZFS_EXIT(zfsvfs);

	Irp->IoStatus.Information = sizeof(PARTITION_INFORMATION_EX);

	return STATUS_SUCCESS;
}

NTSTATUS ioctl_disk_get_length_info(PDEVICE_OBJECT DeviceObject, PIRP Irp, PIO_STACK_LOCATION IrpSp)
{
	dprintf("%s: \n", __func__);

	if (IrpSp->Parameters.DeviceIoControl.OutputBufferLength < sizeof(GET_LENGTH_INFORMATION)) {
		Irp->IoStatus.Information = sizeof(GET_LENGTH_INFORMATION);
		return STATUS_BUFFER_TOO_SMALL;
	}

	mount_t *zmo = DeviceObject->DeviceExtension;
	if (!zmo ||
		(zmo->type != MOUNT_TYPE_VCB &&
			zmo->type != MOUNT_TYPE_DCB)) {
		return STATUS_INVALID_PARAMETER;
	}

	zfsvfs_t *zfsvfs = vfs_fsprivate(zmo);
	if (zfsvfs == NULL)
		return STATUS_INVALID_PARAMETER;

	ZFS_ENTER(zfsvfs);  // This returns EIO if fail

	uint64_t refdbytes, availbytes, usedobjs, availobjs;
	dmu_objset_space(zfsvfs->z_os,
		&refdbytes, &availbytes, &usedobjs, &availobjs);

	GET_LENGTH_INFORMATION *gli = Irp->AssociatedIrp.SystemBuffer;
	gli->Length.QuadPart = availbytes + refdbytes;

	ZFS_EXIT(zfsvfs);

	Irp->IoStatus.Information = sizeof(GET_LENGTH_INFORMATION);

	return STATUS_SUCCESS;
}

NTSTATUS ioctl_volume_is_io_capable(PDEVICE_OBJECT DeviceObject, PIRP Irp, PIO_STACK_LOCATION IrpSp)
{
	dprintf("%s: \n", __func__);
	return STATUS_SUCCESS;
}

NTSTATUS ioctl_storage_get_hotplug_info(PDEVICE_OBJECT DeviceObject, PIRP Irp, PIO_STACK_LOCATION IrpSp)
{
	dprintf("%s: \n", __func__);

	if (IrpSp->Parameters.DeviceIoControl.OutputBufferLength < sizeof(STORAGE_HOTPLUG_INFO)) {
		Irp->IoStatus.Information = sizeof(STORAGE_HOTPLUG_INFO);
		return STATUS_BUFFER_TOO_SMALL;
	}

	STORAGE_HOTPLUG_INFO *hot = Irp->AssociatedIrp.SystemBuffer;
	hot->Size = sizeof(STORAGE_HOTPLUG_INFO);
	hot->MediaRemovable = FALSE; // XX
	hot->DeviceHotplug = TRUE;
	hot->MediaHotplug = FALSE;
	hot->WriteCacheEnableOverride = FALSE;

	Irp->IoStatus.Information = sizeof(STORAGE_HOTPLUG_INFO);
	return STATUS_SUCCESS;
}

NTSTATUS ioctl_storage_query_property(PDEVICE_OBJECT DeviceObject, PIRP Irp, PIO_STACK_LOCATION IrpSp)
{
	NTSTATUS status;
	ULONG outputLength;

	dprintf("%s: \n", __func__);

	outputLength = IrpSp->Parameters.DeviceIoControl.OutputBufferLength;
	if (outputLength < sizeof(STORAGE_PROPERTY_QUERY)) {
		Irp->IoStatus.Information = sizeof(STORAGE_PROPERTY_QUERY);
		return STATUS_BUFFER_TOO_SMALL;
	}

	STORAGE_PROPERTY_QUERY *spq = Irp->AssociatedIrp.SystemBuffer;

	switch (spq->QueryType) {

	case PropertyExistsQuery:

		// ExistsQuery: return OK if exists.
		Irp->IoStatus.Information = 0;

		switch (spq->PropertyId) {
		case StorageDeviceUniqueIdProperty:
			dprintf("    PropertyExistsQuery StorageDeviceUniqueIdProperty\n");
			status = STATUS_SUCCESS;
			break;
		case StorageDeviceWriteCacheProperty:
		case StorageAdapterProperty:
			dprintf("    PropertyExistsQuery Not implemented 0x%x\n", spq->PropertyId);
			status = STATUS_NOT_IMPLEMENTED;
			break;
		case StorageDeviceAttributesProperty:
			dprintf("    PropertyExistsQuery StorageDeviceAttributesProperty\n");
			status = STATUS_SUCCESS;
			break;
		default:
			dprintf("    PropertyExistsQuery unknown 0x%x\n", spq->PropertyId);
			status = STATUS_NOT_IMPLEMENTED;
			break;
		} // switch PropertyId
		break;

	// Query property, check input buffer size.
	case PropertyStandardQuery:

		switch (spq->PropertyId) {
		case StorageDeviceProperty:
			dprintf("    PropertyStandardQuery StorageDeviceProperty\n");
			Irp->IoStatus.Information = sizeof(STORAGE_DEVICE_DESCRIPTOR);
			if (outputLength < sizeof(STORAGE_DEVICE_DESCRIPTOR)) {
				status = STATUS_BUFFER_TOO_SMALL;
				break;
			}
			PSTORAGE_DEVICE_DESCRIPTOR storage;
			storage = Irp->AssociatedIrp.SystemBuffer;
			status = STATUS_SUCCESS;
			break;
		case StorageAdapterProperty:
			dprintf("    PropertyStandardQuery Not implemented 0x%x\n", spq->PropertyId);
			status = STATUS_NOT_IMPLEMENTED;
			break;
		case StorageDeviceAttributesProperty:
			dprintf("    PropertyStandardQuery StorageDeviceAttributesProperty\n");
			Irp->IoStatus.Information = sizeof(STORAGE_DEVICE_ATTRIBUTES_DESCRIPTOR);
			if (outputLength < sizeof(STORAGE_DEVICE_ATTRIBUTES_DESCRIPTOR)) {
				status = STATUS_BUFFER_TOO_SMALL;
				break;
			}
			STORAGE_DEVICE_ATTRIBUTES_DESCRIPTOR *sdad;
			sdad = Irp->AssociatedIrp.SystemBuffer;
			sdad->Version = 1;
			sdad->Size = sizeof(STORAGE_DEVICE_ATTRIBUTES_DESCRIPTOR);
			sdad->Attributes = STORAGE_ATTRIBUTE_BYTE_ADDRESSABLE_IO;
			status = STATUS_SUCCESS;
			break;
		default:
			dprintf("    PropertyStandardQuery unknown 0x%x\n", spq->PropertyId);
			status = STATUS_NOT_IMPLEMENTED;
			break;
		} // switch propertyId
		break;

	default:
		dprintf("%s: unknown Querytype: 0x%x\n", __func__, spq->QueryType);
		status = STATUS_NOT_IMPLEMENTED;
		break;
	}

	Irp->IoStatus.Information = sizeof(STORAGE_PROPERTY_QUERY);
	return status;
}

// Query Unique id uses 1 byte chars.
// If overflow, set Information to sizeof(MOUNTDEV_UNIQUE_ID), and NameLength to required size.
//
NTSTATUS ioctl_query_unique_id(PDEVICE_OBJECT DeviceObject, PIRP Irp, PIO_STACK_LOCATION IrpSp)
{
	PMOUNTDEV_UNIQUE_ID uniqueId;
	ULONG				bufferLength = IrpSp->Parameters.DeviceIoControl.OutputBufferLength;
	mount_t *zmo;
	char osname[MAXNAMELEN];
	ULONG len;

	dprintf("%s: \n", __func__);

	zmo = (mount_t *)DeviceObject->DeviceExtension;

	if (bufferLength < sizeof(MOUNTDEV_UNIQUE_ID)) {
		Irp->IoStatus.Information = sizeof(MOUNTDEV_UNIQUE_ID);
		return STATUS_BUFFER_TOO_SMALL;
	}

	RtlUnicodeToUTF8N(osname, MAXPATHLEN, &len, zmo->name.Buffer, zmo->name.Length);
	osname[len] = 0;

	// uniqueId appears to be CHARS not WCHARS, so this might need correcting?
	uniqueId = (PMOUNTDEV_UNIQUE_ID)Irp->AssociatedIrp.SystemBuffer;

	uniqueId->UniqueIdLength = strlen(osname);

	if (sizeof(USHORT) + uniqueId->UniqueIdLength < bufferLength) {
		RtlCopyMemory((PCHAR)uniqueId->UniqueId, osname, uniqueId->UniqueIdLength);
		Irp->IoStatus.Information = FIELD_OFFSET(MOUNTDEV_UNIQUE_ID, UniqueId[0]) +
			uniqueId->UniqueIdLength;
		dprintf("replying with '%.*s'\n", uniqueId->UniqueIdLength, uniqueId->UniqueId);
		return STATUS_SUCCESS;
	}
	else {
		Irp->IoStatus.Information = sizeof(MOUNTDEV_UNIQUE_ID);
		return STATUS_BUFFER_OVERFLOW;
	}
}

NTSTATUS ioctl_query_stable_guid(PDEVICE_OBJECT DeviceObject, PIRP Irp, PIO_STACK_LOCATION IrpSp)
{
	PMOUNTDEV_STABLE_GUID mountGuid;
	ULONG bufferLength = IrpSp->Parameters.DeviceIoControl.OutputBufferLength;
	mount_t *zmo;

	dprintf("%s: \n", __func__);

	zmo = (mount_t *)DeviceObject->DeviceExtension;

	if (bufferLength < sizeof(MOUNTDEV_STABLE_GUID)) {
		Irp->IoStatus.Information = sizeof(MOUNTDEV_STABLE_GUID);
		return STATUS_BUFFER_TOO_SMALL;
	}

	mountGuid = (PMOUNTDEV_STABLE_GUID)Irp->AssociatedIrp.SystemBuffer;
	RtlZeroMemory(&mountGuid->StableGuid, sizeof(mountGuid->StableGuid));
	zfsvfs_t *zfsvfs = vfs_fsprivate(zmo);
	if (zfsvfs) {
		uint64_t guid = dmu_objset_fsid_guid(zfsvfs->z_os);
		RtlCopyMemory(&mountGuid->StableGuid, &guid, sizeof(guid));
		Irp->IoStatus.Information = sizeof(MOUNTDEV_STABLE_GUID);
		return STATUS_SUCCESS;
	}
	return STATUS_NOT_FOUND;
}


NTSTATUS ioctl_mountdev_query_suggested_link_name(PDEVICE_OBJECT DeviceObject, PIRP Irp, PIO_STACK_LOCATION IrpSp)
{
	MOUNTDEV_SUGGESTED_LINK_NAME *linkName;
	ULONG				bufferLength = IrpSp->Parameters.DeviceIoControl.OutputBufferLength;
	//	UNICODE_STRING MountPoint;
	mount_t *zmo = (mount_t *)DeviceObject->DeviceExtension;

	dprintf("%s: \n", __func__);

	if (bufferLength < sizeof(MOUNTDEV_SUGGESTED_LINK_NAME)) {
		Irp->IoStatus.Information = sizeof(MOUNTDEV_SUGGESTED_LINK_NAME);
		return STATUS_BUFFER_TOO_SMALL;
	}

	// We only reply to strict driveletter mounts, not paths...
	if (!zmo->justDriveLetter)
		return STATUS_NOT_FOUND;

	// If "?:" then just let windows pick drive letter
	if (zmo->mountpoint.Buffer[4] == L'?')
		return STATUS_NOT_FOUND;

	// This code works, for driveletters.
	// The mountpoint string is "\\??\\f:" so change
	// that to DosDevicesF:

	DECLARE_UNICODE_STRING_SIZE(MountPoint, ZFS_MAX_DATASET_NAME_LEN); // 36(uuid) + 6 (punct) + 6 (Volume)
	RtlUnicodeStringPrintf(&MountPoint, L"\\DosDevices\\%wc:", towupper(zmo->mountpoint.Buffer[4]));  // "\??\F:"

	//RtlInitUnicodeString(&MountPoint, L"\\DosDevices\\G:");

	linkName = (PMOUNTDEV_SUGGESTED_LINK_NAME)Irp->AssociatedIrp.SystemBuffer;

	linkName->UseOnlyIfThereAreNoOtherLinks = FALSE;
	linkName->NameLength = MountPoint.Length;

	if (sizeof(USHORT) + linkName->NameLength <= bufferLength) {
		RtlCopyMemory((PCHAR)linkName->Name, MountPoint.Buffer,
			linkName->NameLength);
		Irp->IoStatus.Information =
			FIELD_OFFSET(MOUNTDEV_SUGGESTED_LINK_NAME, Name[0]) +
			linkName->NameLength;
		dprintf("  LinkName %wZ (%d)\n", MountPoint, MountPoint.Length);
		return 	STATUS_SUCCESS;
	}

	Irp->IoStatus.Information = sizeof(MOUNTDEV_SUGGESTED_LINK_NAME);
	return STATUS_BUFFER_OVERFLOW;

	//return STATUS_NOT_FOUND;

}

NTSTATUS ioctl_mountdev_query_stable_guid(PDEVICE_OBJECT DeviceObject, PIRP Irp, PIO_STACK_LOCATION IrpSp)
{
	MOUNTDEV_STABLE_GUID	*guid = Irp->UserBuffer;
	ULONG					 bufferLength = IrpSp->Parameters.DeviceIoControl.OutputBufferLength;
	mount_t *zmo = (mount_t *)DeviceObject->DeviceExtension;

	dprintf("%s: \n", __func__);

	if (bufferLength < sizeof(MOUNTDEV_STABLE_GUID)) {
		Irp->IoStatus.Information = sizeof(MOUNTDEV_STABLE_GUID);
		return STATUS_BUFFER_TOO_SMALL;
	}

	zfsvfs_t *zfsvfs = vfs_fsprivate(zmo);
	if (zfsvfs == NULL)
		return STATUS_INVALID_PARAMETER;

	extern int	zfs_vfs_uuid_gen(const char *osname, uuid_t uuid);

	// A bit naughty
	zfs_vfs_uuid_gen(spa_name(dmu_objset_spa(zfsvfs->z_os)), (char *)&guid->StableGuid);

	Irp->IoStatus.Information = sizeof(MOUNTDEV_STABLE_GUID);
	return STATUS_SUCCESS;
}

// FFFF9284A054B080: * user_fs_request: unknown class 0x903bc:  CTL_CODE(FILE_DEVICE_FILE_SYSTEM, 238,

// 0x2d118c - MASS_STORAGE 0x463
// disk Windows IOCTL: 0x530018

// fsWindows IOCTL: 0x534058
// FFFF9284A14A9040: **** unknown fsWindows IOCTL: 0x534058 function 0x16
// VOLSNAPCONTROLTYPE : 

/*
 (open Extend\$Reparse:$R:$INDEX_ALLOCATION and use ZwQueryDirectoryFile on that handle to get
 reparse info). It gives you the FileReference (file id) and tag value of all the reparse
 points on the volume (see ntifs.h for FILE_REPARSE_POINT_INFORMATION).
 */
