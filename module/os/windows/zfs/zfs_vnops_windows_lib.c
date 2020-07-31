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
 * Portions Copyright 2022 Andrew Innes <andrew.c12@gmail.com>
 */
#define	INITGUID
#include <Ntifs.h>
#include <intsafe.h>
#include <ntddvol.h>
#include <ntdddisk.h>
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
#include <sys/zfs_vnops_os.h>
#include <sys/zfs_ctldir.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/dirent.h>

#include <sys/unistd.h>
#include <sys/uuid.h>

#include <sys/types.h>
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

// BUILTIN\Administrators
static sid_header sid_BA = { 1, 2, SECURITY_NT_AUTHORITY, {32, 544} };
// NT AUTHORITY\SYSTEM
static sid_header sid_SY = { 1, 1, SECURITY_NT_AUTHORITY, {18} };
// BUILTIN\Users
static sid_header sid_BU = { 1, 2, SECURITY_NT_AUTHORITY, {32, 545} };
// NT AUTHORITY\Authenticated Users
static sid_header sid_AU = { 1, 1, SECURITY_NT_AUTHORITY, {11} };

// MandatoryLevel\High
static sid_header sid_MH =
	{ 1, 1, SECURITY_MANDATORY_LABEL_AUTHORITY, {12288} };
// MandatoryLevel\Low
static sid_header sid_ML =
	{ 1, 1, SECURITY_MANDATORY_LABEL_AUTHORITY, {4096} };

typedef struct {
	UCHAR flags;
	ACCESS_MASK mask;
	sid_header* sid;
} dacl;

/*
 *
 * Brand new ntfs:
 * F:\ BUILTIN\Administrators:(F)
 * 	BUILTIN\Administrators:(OI)(CI)(IO)(F)
 *	NT AUTHORITY\SYSTEM:(F)
 *	NT AUTHORITY\SYSTEM:(OI)(CI)(IO)(F)
 *	NT AUTHORITY\Authenticated Users:(M)
 *	NT AUTHORITY\Authenticated Users:(OI)(CI)(IO)(M)
 *	BUILTIN\Users:(RX)
 *	BUILTIN\Users:(OI)(CI)(IO)(GR,GE)
 */
static dacl def_dacls[] = {
	// BUILTIN\Administrators:(F)
	{ 0, FILE_ALL_ACCESS, &sid_BA },
	// BUILTIN\Administrators:(OI)(CI)(IO)(F)
	{ OBJECT_INHERIT_ACE | CONTAINER_INHERIT_ACE | INHERIT_ONLY_ACE,
	    FILE_ALL_ACCESS, &sid_BA },
	// NT AUTHORITY\SYSTEM:(F)
	{ 0, FILE_ALL_ACCESS, &sid_SY },
	// NT AUTHORITY\SYSTEM:(OI)(CI)(IO)(F)
	{ OBJECT_INHERIT_ACE | CONTAINER_INHERIT_ACE | INHERIT_ONLY_ACE,
	    FILE_ALL_ACCESS, &sid_SY },
	// NT AUTHORITY\Authenticated Users:(M)
	{ 0, FILE_GENERIC_READ | FILE_GENERIC_WRITE | DELETE |
	    FILE_GENERIC_EXECUTE, &sid_AU },
	// NT AUTHORITY\Authenticated Users:(OI)(CI)(IO)(M)
	{ OBJECT_INHERIT_ACE | CONTAINER_INHERIT_ACE | INHERIT_ONLY_ACE,
	    FILE_GENERIC_READ | FILE_GENERIC_WRITE | DELETE |
	    FILE_GENERIC_EXECUTE, &sid_AU },
	// BUILTIN\Users:(RX)
	{ 0, FILE_GENERIC_READ | FILE_GENERIC_EXECUTE, &sid_BU },
	// BUILTIN\Users:(OI)(CI)(IO)(GR,GE)
	{ OBJECT_INHERIT_ACE | CONTAINER_INHERIT_ACE | INHERIT_ONLY_ACE,
	    GENERIC_READ | GENERIC_EXECUTE, &sid_BU },
#if 0 // C: only?
	// Mandatory Label\High Mandatory Level:(OI)(NP)(IO)(NW)
	{ OBJECT_INHERIT_ACE | NO_PROPAGATE_INHERIT_ACE | INHERIT_ONLY_ACE,
	    SYSTEM_MANDATORY_LABEL_NO_WRITE_UP, &sid_MH },
#endif
	// END
	{ 0, 0, NULL }
};

// #define	USE_RECYCLE_ACL
#ifdef USE_RECYCLE_ACL
/*
 * Brand new $Recycle.bin
 *
 * Owner: WDKRemoteUser
 * Group: None
 *
 * F : \$Recycle.bin BUILTIN\Administrators:(I)(F)
 *	NT AUTHORITY\SYSTEM : (I)(F)
 *	NT AUTHORITY\Authenticated Users : (I)(M)
 *	BUILTIN\Users : (I)(RX)
 */
static dacl recycle_dacls[] = {
	// BUILTIN\Administrators:(I)(F)
	{ INHERITED_ACE, FILE_ALL_ACCESS, &sid_BA },
	// NT AUTHORITY\SYSTEM : (I)(F)
	{ INHERITED_ACE, FILE_ALL_ACCESS, &sid_SY },
	// NT AUTHORITY\Authenticated Users : (I)(M)
	{ INHERITED_ACE, FILE_GENERIC_READ | FILE_GENERIC_WRITE | DELETE
	    | FILE_GENERIC_EXECUTE, &sid_AU },
	// BUILTIN\Users : (I)(RX)
	{ INHERITED_ACE, FILE_GENERIC_READ | FILE_GENERIC_EXECUTE, &sid_BU },
	// END
	{ 0, 0, NULL }
};
#endif

char *
major2str(int major, int minor)
{
	switch (major) {
	case IRP_MJ_CREATE:
		return ("IRP_MJ_CREATE");
	case IRP_MJ_CREATE_NAMED_PIPE:
		return ("IRP_MJ_CREATE_NAMED_PIPE");
	case IRP_MJ_CLOSE:
		return ("IRP_MJ_CLOSE");
	case IRP_MJ_READ:
		return ("IRP_MJ_READ");
	case IRP_MJ_WRITE:
		return ("IRP_MJ_WRITE");
	case IRP_MJ_QUERY_INFORMATION:
		return ("IRP_MJ_QUERY_INFORMATION");
	case IRP_MJ_SET_INFORMATION:
		return ("IRP_MJ_SET_INFORMATION");
	case IRP_MJ_QUERY_EA:
		return ("IRP_MJ_QUERY_EA");
	case IRP_MJ_SET_EA:
		return ("IRP_MJ_SET_EA");
	case IRP_MJ_FLUSH_BUFFERS:
		return ("IRP_MJ_FLUSH_BUFFERS");
	case IRP_MJ_QUERY_VOLUME_INFORMATION:
		return ("IRP_MJ_QUERY_VOLUME_INFORMATION");
	case IRP_MJ_SET_VOLUME_INFORMATION:
		return ("IRP_MJ_SET_VOLUME_INFORMATION");
	case IRP_MJ_DIRECTORY_CONTROL:
		switch (minor) {
		case IRP_MN_NOTIFY_CHANGE_DIRECTORY:
	return ("IRP_MJ_DIRECTORY_CONTROL(IRP_MN_NOTIFY_CHANGE_DIRECTORY)");
		case IRP_MN_QUERY_DIRECTORY:
	return ("IRP_MJ_DIRECTORY_CONTROL(IRP_MN_QUERY_DIRECTORY)");
		}
		return ("IRP_MJ_DIRECTORY_CONTROL");
	case IRP_MJ_FILE_SYSTEM_CONTROL:
		switch (minor) {
		case IRP_MN_KERNEL_CALL:
	return ("IRP_MJ_FILE_SYSTEM_CONTROL(IRP_MN_KERNEL_CALL)");
		case IRP_MN_MOUNT_VOLUME:
	return ("IRP_MJ_FILE_SYSTEM_CONTROL(IRP_MN_MOUNT_VOLUME)");
		case IRP_MN_USER_FS_REQUEST:
	return ("IRP_MJ_FILE_SYSTEM_CONTROL(IRP_MN_USER_FS_REQUEST)");
		case IRP_MN_VERIFY_VOLUME:
	return ("IRP_MJ_FILE_SYSTEM_CONTROL(IRP_MN_VERIFY_VOLUME)");
		case IRP_MN_LOAD_FILE_SYSTEM:
	return ("IRP_MJ_FILE_SYSTEM_CONTROL(IRP_MN_LOAD_FILE_SYSTEM)");
		}
		return ("IRP_MJ_FILE_SYSTEM_CONTROL");
	case IRP_MJ_DEVICE_CONTROL:
		return ("IRP_MJ_DEVICE_CONTROL");
	case IRP_MJ_INTERNAL_DEVICE_CONTROL:
		return ("IRP_MJ_INTERNAL_DEVICE_CONTROL");
	case IRP_MJ_SHUTDOWN:
		return ("IRP_MJ_SHUTDOWN");
	case IRP_MJ_LOCK_CONTROL:
		switch (minor) {
		case IRP_MN_LOCK:
	return ("IRP_MJ_LOCK_CONTROL(IRP_MN_LOCK)");
		case IRP_MN_UNLOCK_ALL:
	return ("IRP_MJ_LOCK_CONTROL(IRP_MN_UNLOCK_ALL)");
		case IRP_MN_UNLOCK_ALL_BY_KEY:
	return ("IRP_MJ_LOCK_CONTROL(IRP_MN_UNLOCK_ALL_BY_KEY)");
		case IRP_MN_UNLOCK_SINGLE:
	return ("IRP_MJ_LOCK_CONTROL(IRP_MN_UNLOCK_SINGLE)");
		}
		return ("IRP_MJ_LOCK_CONTROL");
	case IRP_MJ_CLEANUP:
		return ("IRP_MJ_CLEANUP");
	case IRP_MJ_CREATE_MAILSLOT:
		return ("IRP_MJ_CREATE_MAILSLOT");
	case IRP_MJ_QUERY_SECURITY:
		return ("IRP_MJ_QUERY_SECURITY");
	case IRP_MJ_SET_SECURITY:
		return ("IRP_MJ_SET_SECURITY");
	case IRP_MJ_POWER:
		return ("IRP_MJ_POWER");
	case IRP_MJ_SYSTEM_CONTROL:
		return ("IRP_MJ_SYSTEM_CONTROL");
	case IRP_MJ_DEVICE_CHANGE:
		return ("IRP_MJ_DEVICE_CHANGE");
	case IRP_MJ_QUERY_QUOTA:
		return ("IRP_MJ_QUERY_QUOTA");
	case IRP_MJ_SET_QUOTA:
		return ("IRP_MJ_SET_QUOTA");
	case IRP_MJ_PNP:
		switch (minor) {
		case IRP_MN_START_DEVICE:
			return ("IRP_MJ_PNP(IRP_MN_START_DEVICE)");
		case IRP_MN_QUERY_REMOVE_DEVICE:
			return ("IRP_MJ_PNP(IRP_MN_QUERY_REMOVE_DEVICE)");
		case IRP_MN_REMOVE_DEVICE:
			return ("IRP_MJ_PNP(IRP_MN_REMOVE_DEVICE)");
		case IRP_MN_CANCEL_REMOVE_DEVICE:
			return ("IRP_MJ_PNP(IRP_MN_CANCEL_REMOVE_DEVICE)");
		case IRP_MN_STOP_DEVICE:
			return ("IRP_MJ_PNP(IRP_MN_STOP_DEVICE)");
		case IRP_MN_QUERY_STOP_DEVICE:
			return ("IRP_MJ_PNP(IRP_MN_QUERY_STOP_DEVICE)");
		case IRP_MN_CANCEL_STOP_DEVICE:
			return ("IRP_MJ_PNP(IRP_MN_CANCEL_STOP_DEVICE)");
		case IRP_MN_QUERY_DEVICE_RELATIONS:
			return ("IRP_MJ_PNP(IRP_MN_QUERY_DEVICE_RELATIONS)");
		case IRP_MN_QUERY_INTERFACE:
			return ("IRP_MJ_PNP(IRP_MN_QUERY_INTERFACE)");
		case IRP_MN_QUERY_RESOURCES:
			return ("IRP_MJ_PNP(IRP_MN_QUERY_RESOURCES)");
		case IRP_MN_QUERY_RESOURCE_REQUIREMENTS:
	return ("IRP_MJ_PNP(IRP_MN_QUERY_RESOURCE_REQUIREMENTS)");
		case IRP_MN_QUERY_CAPABILITIES:
			return ("IRP_MJ_PNP(IRP_MN_QUERY_CAPABILITIES)");
		case IRP_MN_QUERY_DEVICE_TEXT:
			return ("IRP_MJ_PNP(IRP_MN_QUERY_DEVICE_TEXT)");
		case IRP_MN_FILTER_RESOURCE_REQUIREMENTS:
	return ("IRP_MJ_PNP(IRP_MN_FILTER_RESOURCE_REQUIREMENTS)");
		case IRP_MN_READ_CONFIG:
			return ("IRP_MJ_PNP(IRP_MN_READ_CONFIG)");
		case IRP_MN_WRITE_CONFIG:
			return ("IRP_MJ_PNP(IRP_MN_WRITE_CONFIG)");
		case IRP_MN_EJECT:
			return ("IRP_MJ_PNP(IRP_MN_EJECT)");
		case IRP_MN_SET_LOCK:
			return ("IRP_MJ_PNP(IRP_MN_SET_LOCK)");
		case IRP_MN_QUERY_ID:
			return ("IRP_MJ_PNP(IRP_MN_QUERY_ID)");
		case IRP_MN_QUERY_PNP_DEVICE_STATE:
			return ("IRP_MJ_PNP(IRP_MN_QUERY_PNP_DEVICE_STATE)");
		case IRP_MN_QUERY_BUS_INFORMATION:
			return ("IRP_MJ_PNP(IRP_MN_QUERY_BUS_INFORMATION)");
		case IRP_MN_DEVICE_USAGE_NOTIFICATION:
			return ("IRP_MJ_PNP(IRP_MN_DEVICE_USAGE_NOTIFICATION)");
		case IRP_MN_SURPRISE_REMOVAL: // SUPPLIES!
			return ("IRP_MJ_PNP(IRP_MN_SURPRISE_REMOVAL)");
		}
		return ("IRP_MJ_PNP");
	default:
		break;
	}
	return ("Unknown");
}

char *
common_status_str(NTSTATUS Status)
{
	switch (Status) {
	case STATUS_SUCCESS:
		return ("OK");
	case STATUS_BUFFER_OVERFLOW:
		return ("Overflow");
	case STATUS_BUFFER_TOO_SMALL:
		return ("BufferTooSmall");
	case STATUS_END_OF_FILE:
		return ("EOF");
	case STATUS_NO_MORE_FILES:
		return ("NoMoreFiles");
	case STATUS_OBJECT_PATH_NOT_FOUND:
		return ("ObjectPathNotFound");
	case STATUS_NO_SUCH_FILE:
		return ("NoSuchFile");
	case STATUS_ACCESS_DENIED:
		return ("AccessDenied");
	case STATUS_NOT_IMPLEMENTED:
		return ("NotImplemented");
	case STATUS_PENDING:
		return ("STATUS_PENDING");
	case STATUS_INVALID_PARAMETER:
		return ("STATUS_INVALID_PARAMETER");
	case STATUS_OBJECT_NAME_NOT_FOUND:
		return ("STATUS_OBJECT_NAME_NOT_FOUND");
	case STATUS_OBJECT_NAME_COLLISION:
		return ("STATUS_OBJECT_NAME_COLLISION");
	case STATUS_FILE_IS_A_DIRECTORY:
		return ("STATUS_FILE_IS_A_DIRECTORY");
	case STATUS_NOT_A_REPARSE_POINT:
		return ("STATUS_NOT_A_REPARSE_POINT");
	case STATUS_NOT_FOUND:
		return ("STATUS_NOT_FOUND");
	case STATUS_NO_MORE_EAS:
		return ("STATUS_NO_MORE_EAS");
	case STATUS_NO_EAS_ON_FILE:
		return ("STATUS_NO_EAS_ON_FILE");
	case 0xa0000003:
		return ("STATUS_REPARSE_POINT");
	case STATUS_DIRECTORY_IS_A_REPARSE_POINT:
		return ("STATUS_DIRECTORY_IS_A_REPARSE_POINT");
	case STATUS_REPARSE:
		return ("STATUS_REPARSE");
	case STATUS_DISK_QUOTA_EXCEEDED:
		return ("STATUS_DISK_QUOTA_EXCEEDED");
	default:
		return ("<*****>");
	}
}

void
strupper(char *s, size_t max)
{
	while ((max > 0) && *s) {
		*s = toupper(*s);
		s++;
		max--;
	}
}

char *
create_options(ULONG Options)
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
		strlcat(out, "DirectoryFile ", sizeof (out));
	if (BooleanFlagOn(Options, FILE_NON_DIRECTORY_FILE))
		strlcat(out, "NonDirectoryFile ", sizeof (out));
	if (BooleanFlagOn(Options, FILE_NO_INTERMEDIATE_BUFFERING))
		strlcat(out, "NoIntermediateBuffering ", sizeof (out));
	if (BooleanFlagOn(Options, FILE_NO_EA_KNOWLEDGE))
		strlcat(out, "NoEaKnowledge ", sizeof (out));
	if (BooleanFlagOn(Options, FILE_DELETE_ON_CLOSE))
		strlcat(out, "DeleteOnClose ", sizeof (out));
	if (BooleanFlagOn(Options, FILE_OPEN_BY_FILE_ID))
		strlcat(out, "FileOpenByFileId ", sizeof (out));

	CreateDisposition = (Options >> 24) & 0x000000ff;

	switch (CreateDisposition) {
	case FILE_SUPERSEDE:
		strlcat(out, "@FILE_SUPERSEDE ", sizeof (out));
		break;
	case FILE_CREATE:
		strlcat(out, "@FILE_CREATE ", sizeof (out));
		break;
	case FILE_OPEN:
		strlcat(out, "@FILE_OPEN ", sizeof (out));
		break;
	case FILE_OPEN_IF:
		strlcat(out, "@FILE_OPEN_IF ", sizeof (out));
		break;
	case FILE_OVERWRITE:
		strlcat(out, "@FILE_OVERWRITE ", sizeof (out));
		break;
	case FILE_OVERWRITE_IF:
		strlcat(out, "@FILE_OVERWRITE_IF ", sizeof (out));
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
		strlcat(out, "#CreateDirectory ", sizeof (out));
	if (OpenDirectory)
		strlcat(out, "#OpenDirectory ", sizeof (out));
	if (CreateFile)
		strlcat(out, "#CreateFile ", sizeof (out));

	return (out);
}

char *
create_reply(NTSTATUS status, ULONG reply)
{
	switch (reply) {
	case FILE_SUPERSEDED:
		return ("FILE_SUPERSEDED");
	case FILE_OPENED:
		return ("FILE_OPENED");
	case FILE_CREATED:
		return ("FILE_CREATED");
	case FILE_OVERWRITTEN:
		return ("FILE_OVERWRITTEN");
	case FILE_EXISTS:
		return ("FILE_EXISTS");
	case FILE_DOES_NOT_EXIST:
		return ("FILE_DOES_NOT_EXIST");
	default:
		if (status == STATUS_REPARSE)
			return ("ReparseTag");
		return ("FileUnknown");
	}
}

int
AsciiStringToUnicodeString(char *in, PUNICODE_STRING out)
{
	ANSI_STRING conv;
	if (in == NULL) {
		memset(out, 0, sizeof (UNICODE_STRING));
		return (0);
	}
	conv.Buffer = in;
	conv.Length = strlen(in);
	conv.MaximumLength = PATH_MAX;
	return (RtlAnsiStringToUnicodeString(out, &conv, TRUE));
}

void
FreeUnicodeString(PUNICODE_STRING s)
{
	if (s->Buffer) ExFreePool(s->Buffer);
	s->Buffer = NULL;
}

int
zfs_vnop_ioctl_fullfsync(struct vnode *vp, vfs_context_t *ct, zfsvfs_t *zfsvfs)
{
	int error = 0;

	// error = zfs_fsync(VTOZ(vp), /* syncflag */ 0, NULL);
	return (error);
}

uint32_t
zfs_getwinflags(znode_t *zp)
{
	uint32_t  winflags = 0;
	uint64_t zflags = zp->z_pflags;
	zfsvfs_t *zfsvfs = zp->z_zfsvfs;
	if (zflags & ZFS_HIDDEN)
		winflags |= FILE_ATTRIBUTE_HIDDEN;
	if (zflags & ZFS_SYSTEM)
		winflags |= FILE_ATTRIBUTE_SYSTEM;
	if (zflags & ZFS_ARCHIVE)
		winflags |= FILE_ATTRIBUTE_ARCHIVE;
	if (zflags & ZFS_READONLY || zfsvfs->z_rdonly)
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
		return (1);
	}

	return (0);
}

// WSL uses special EAs to interact with uid/gid/mode/device major/minor
// Returns: TRUE if the EA was stored in the vattr.
BOOLEAN
vattr_apply_lx_ea(vattr_t *vap, PFILE_FULL_EA_INFORMATION ea)
{
	BOOLEAN setVap = FALSE;

	if (ea->EaNameLength != 6 || strncmp(ea->EaName, "$LX", 3) != 0)
		return (FALSE);

	void *eaValue = &ea->EaName[0] + ea->EaNameLength + 1;
	if (strncmp(ea->EaName, LX_FILE_METADATA_UID_EA_NAME,
	    ea->EaNameLength) == 0) {
		vap->va_uid = *(PUINT32)eaValue;
		vap->va_active |= ATTR_UID;
		setVap = TRUE;
	} else if (strncmp(ea->EaName, LX_FILE_METADATA_GID_EA_NAME,
	    ea->EaNameLength) == 0) {
		vap->va_gid = *(PUINT32)eaValue;
		vap->va_active |= ATTR_GID;
		setVap = TRUE;
	} else if (strncmp(ea->EaName, LX_FILE_METADATA_MODE_EA_NAME,
	    ea->EaNameLength) == 0) {
		vap->va_mode = *(PUINT32)eaValue;
		vap->va_active |= ATTR_MODE;
		setVap = TRUE;
	} else if (strncmp(ea->EaName, LX_FILE_METADATA_DEVICE_ID_EA_NAME,
	    ea->EaNameLength) == 0) {
		UINT32 *vu32 = (UINT32*)eaValue;
		vap->va_rdev = makedev(vu32[0], vu32[1]);
		vap->va_active |= VNODE_ATTR_va_rdev;
		setVap = TRUE;
	}
	return (setVap);
}

static int
vnode_apply_single_ea(struct vnode *vp, struct vnode *xdvp,
    FILE_FULL_EA_INFORMATION *ea)
{
	int error;
	znode_t *xzp = NULL;
	vnode_t *xvp = NULL;
	dprintf("%s: xattr '%.*s' valuelen %u\n", __func__,
	    ea->EaNameLength, ea->EaName, ea->EaValueLength);

	if (ea->EaValueLength > 0) {
		/* Write data */
		struct iovec iov;
		iov.iov_base = (void *)(ea->EaName + ea->EaNameLength + 1);
		iov.iov_len = ea->EaValueLength;

		zfs_uio_t uio;
		zfs_uio_iovec_init(&uio, &iov, 1, 0, UIO_SYSSPACE,
		    ea->EaValueLength, 0);

		error = zpl_xattr_set(vp, ea->EaName, &uio, 0, NULL);
	} else {
		error = zpl_xattr_set(vp, ea->EaName, NULL, 0, NULL);
	}
	return (error);
}


/*
 * Apply a set of EAs to a vnode, while handling special Windows EAs that
 * set UID/GID/Mode/rdev.
 */
NTSTATUS
vnode_apply_eas(struct vnode *vp, PFILE_FULL_EA_INFORMATION eas,
    ULONG eaLength, PULONG pEaErrorOffset)
{
	NTSTATUS Status = STATUS_SUCCESS;

	if (vp == NULL || eas == NULL)
		return (STATUS_INVALID_PARAMETER);

	// Optional: Check for validity if the caller wants it.
	if (pEaErrorOffset != NULL) {
		Status = IoCheckEaBufferValidity(eas, eaLength, pEaErrorOffset);
		if (!NT_SUCCESS(Status)) {
			dprintf("%s: failed validity: 0x%x\n",
			    __func__, Status);
			return (Status);
		}
	}

	znode_t *zp = VTOZ(vp);
	zfsvfs_t *zfsvfs = zp->z_zfsvfs;

	// We can land here without a sa_hdl, for example .zfs
	if (zp->z_sa_hdl == NULL)
		return (Status);

	struct vnode *xdvp = NULL;
	znode_t *xdzp = NULL;
	vattr_t vap = { 0 };
	int error;
	PFILE_FULL_EA_INFORMATION ea;
	for (ea = eas; /* empty */;
	    ea = (PFILE_FULL_EA_INFORMATION)
	    ((uint8_t *)ea + ea->NextEntryOffset)) {
		if (vattr_apply_lx_ea(&vap, ea)) {
			dprintf("  encountered special attrs EA '%.*s'\n",
			    ea->EaNameLength, ea->EaName);
		} else {

			error = vnode_apply_single_ea(vp, xdvp, ea);
			if (error != 0)
				dprintf("failed to process xattr: %d\n", error);
		}

		if (ea->NextEntryOffset == 0)
			break;
	}

	// We should perhaps translate some of the "error" codes we can
	// get here, into Status return values. Currently, all errors are
	// masked, and we always return OK.

	// Update zp based on LX eas.
	if (vap.va_active != 0)
		zfs_setattr(zp, &vap, 0, NULL, NULL);

	zfs_send_notify(zfsvfs, zp->z_name_cache,
	    zp->z_name_offset,
	    FILE_NOTIFY_CHANGE_EA,
	    FILE_ACTION_MODIFIED);

out:

	vnode_clear_easize(vp);

	return (Status);
}


extern int zfs_vnop_force_formd_normalized_output;

void
zfs_readdir_complete(emitdir_ptr_t *ctx)
{
	// The last eodp should have Next offset of 0
	// This assumes NextEntryOffset is the FIRST entry in all structs
	if (ctx->next_offset != NULL)
		*ctx->next_offset = 0;

	// The outcout += reclen; above unfortunately adds the possibly
	// aligned (to 8 bytes) length. But the last entry should not
	// be rounded-up.
	if ((ctx->outcount > ctx->last_alignment) &&
	    (ctx->last_alignment > 0)) {
		ctx->outcount -= ctx->last_alignment;
	}
}

/*
 * Put out one directory entry to the output buffer, using
 * whatever struct specified in ctx->dirlisttype.
 * Return:
 *    0   : keep iterating
 *  ESRC  : search-pattern in use, and didn't match (keep iterating)
 * ENOSPC : no more room in buffer (but more to come - stop)
 */
int
zfs_readdir_emitdir(zfsvfs_t *zfsvfs, const char *name, emitdir_ptr_t *ctx,
    zfs_dirlist_t *zccb, ino64_t objnum)
{
	znode_t *tzp = NULL;
	int structsize = 0;
	void *nameptr = NULL;
	ULONG namelenholder = 0;
	int get_zp = ENOENT;
	size_t namelen;
	int error;
	int force_formd_normalized_output = 0;
	ushort_t reclen, rawsize;
	ULONG *next_offset;
	uint64_t guid;

	// Windows combines vnop_readdir and vnop_getattr,
	// so we need to lookup a bunch of values, we try
	// to do that as lightweight as possible.

	if ((zfsvfs->z_ctldir != NULL) &&
	    (objnum == ZFSCTL_INO_ROOT) ||
	    (objnum == ZFSCTL_INO_SNAPDIR) ||
	    ((objnum >= zfsvfs->z_ctldir_startid) &&
	    (objnum <= ZFSCTL_INO_SNAPDIRS))) {
		struct vnode *vp;

		get_zp = zfs_vfs_vget(zfsvfs->z_vfs, objnum, &vp, NULL);
		if (get_zp == 0)
			tzp = VTOZ(vp);

	} else {
		get_zp = zfs_zget_ext(zfsvfs,
		    objnum, &tzp,
		    ZGET_FLAG_UNLINKED);
	}

	/*
	 * Could not find it, error out ? print name ?
	 * Can't zget the .zfs dir etc, so we need a dummy
	 * node, so we grab root node instead.
	 */
	if (get_zp != 0 && tzp == NULL) {
		get_zp = zfs_zget_ext(zfsvfs,
		    zfsvfs->z_root, &tzp,
		    ZGET_FLAG_UNLINKED);
	}
	if (get_zp != 0 && tzp == NULL) {
		return (get_zp);
	}

	/*
	 * Check if name will fit.
	 *
	 * Note: non-ascii names may expand (up to 3x) when converted
	 * to NFD
	 */
	namelen = strlen(name);

	/* sysctl to force formD normalization of vnop output */
	if (zfs_vnop_force_formd_normalized_output &&
	    !is_ascii_str(name))
		force_formd_normalized_output = 1;
	else
		force_formd_normalized_output = 0;

	if (force_formd_normalized_output)
		namelen = MIN(MAXNAMLEN, namelen * 3);

	/*
	 * Fetch filename conversion length
	 */

	error = RtlUTF8ToUnicodeN(NULL, 0, &namelenholder,
	    name, namelen);

	// We need to fill in more fields, for getattr
	uint64_t mtime[2] = { 0 };
	uint64_t ctime[2] = { 0 };
	uint64_t crtime[2] = { 0 };
	if (tzp->z_is_sa && tzp->z_sa_hdl != NULL) {
		/* dummy_zp wont have sa_hdl */
		sa_bulk_attr_t bulk[3];
		int count = 0;
		SA_ADD_BULK_ATTR(bulk, count,
		    SA_ZPL_MTIME(zfsvfs), NULL, &mtime, 16);
		SA_ADD_BULK_ATTR(bulk, count,
		    SA_ZPL_CTIME(zfsvfs), NULL, &ctime, 16);
		SA_ADD_BULK_ATTR(bulk, count,
		    SA_ZPL_CRTIME(zfsvfs), NULL, &crtime, 16);
		sa_bulk_lookup(tzp->z_sa_hdl, bulk, count);
		// Is it worth warning about failed lookup here?
	}

	structsize = 0; /* size of win struct desired */
	/* bufptr : output memory area, incrementing */
	/* outcount : amount written to output, incrementing */
	/* bufsize : size of output area - static */

	/* Fill in struct based on desired type. */
	switch (ctx->dirlisttype) {

	case FileFullDirectoryInformation:
		structsize = FIELD_OFFSET(
		    FILE_FULL_DIR_INFORMATION,
		    FileName[0]);
		if (ctx->outcount + structsize +
		    namelenholder > ctx->bufsize)
			break;
		FILE_FULL_DIR_INFORMATION *eodp =
		    (FILE_FULL_DIR_INFORMATION *)ctx->bufptr;
		next_offset = &eodp->NextEntryOffset;

		eodp->FileIndex = ctx->offset;
		eodp->AllocationSize.QuadPart =
		    S_ISDIR(tzp->z_mode) ? 0 :
		    P2ROUNDUP(tzp->z_size,
		    zfs_blksz(tzp));
		eodp->EndOfFile.QuadPart =
		    S_ISDIR(tzp->z_mode) ? 0 :
		    tzp->z_size;
		TIME_UNIX_TO_WINDOWS(mtime,
		    eodp->LastWriteTime.QuadPart);
		TIME_UNIX_TO_WINDOWS(ctime,
		    eodp->ChangeTime.QuadPart);
		TIME_UNIX_TO_WINDOWS(crtime,
		    eodp->CreationTime.QuadPart);
		TIME_UNIX_TO_WINDOWS(tzp->z_atime,
		    eodp->LastAccessTime.QuadPart);
		// Magic code to change dir icon to link
		eodp->EaSize =
		    tzp->z_pflags & ZFS_REPARSE ?
		    0xa0000003 :
		    xattr_getsize(ZTOV(tzp));
		eodp->FileAttributes =
		    zfs_getwinflags(tzp);
		nameptr = eodp->FileName;
		eodp->FileNameLength = namelenholder;

		break;

	case FileIdBothDirectoryInformation:
		structsize = FIELD_OFFSET(
		    FILE_ID_BOTH_DIR_INFORMATION,
		    FileName[0]);
		if (ctx->outcount + structsize +
		    namelenholder > ctx->bufsize)
			break;
		FILE_ID_BOTH_DIR_INFORMATION *fibdi;
		fibdi = (FILE_ID_BOTH_DIR_INFORMATION *)
		    ctx->bufptr;
		next_offset = &fibdi->NextEntryOffset;

		fibdi->AllocationSize.QuadPart =
		    S_ISDIR(tzp->z_mode) ? 0 :
		    P2ROUNDUP(tzp->z_size,
		    zfs_blksz(tzp));
		fibdi->EndOfFile.QuadPart =
		    S_ISDIR(tzp->z_mode) ? 0 :
		    tzp->z_size;
		TIME_UNIX_TO_WINDOWS(mtime,
		    fibdi->LastWriteTime.QuadPart);
		TIME_UNIX_TO_WINDOWS(ctime,
		    fibdi->ChangeTime.QuadPart);
		TIME_UNIX_TO_WINDOWS(crtime,
		    fibdi->CreationTime.QuadPart);
		TIME_UNIX_TO_WINDOWS(tzp->z_atime,
		    fibdi->LastAccessTime.QuadPart);
		fibdi->EaSize =
		    tzp->z_pflags & ZFS_REPARSE ?
		    0xa0000003 :
		    xattr_getsize(ZTOV(tzp));
		fibdi->FileAttributes =
		    zfs_getwinflags(tzp);
		fibdi->FileId.QuadPart = objnum;
		fibdi->FileIndex = ctx->offset;
		fibdi->ShortNameLength = 0;
		nameptr = fibdi->FileName;
		fibdi->FileNameLength = namelenholder;

		break;

	case FileBothDirectoryInformation:
		structsize = FIELD_OFFSET(
		    FILE_BOTH_DIR_INFORMATION,
		    FileName[0]);
		if (ctx->outcount + structsize +
		    namelenholder > ctx->bufsize)
			break;
		FILE_BOTH_DIR_INFORMATION *fbdi =
		    (FILE_BOTH_DIR_INFORMATION *)ctx->bufptr;
		next_offset = &fbdi->NextEntryOffset;

		fbdi->AllocationSize.QuadPart =
		    S_ISDIR(tzp->z_mode) ? 0 :
		    P2ROUNDUP(tzp->z_size,
		    zfs_blksz(tzp));
		fbdi->EndOfFile.QuadPart =
		    S_ISDIR(tzp->z_mode) ? 0 :
		    tzp->z_size;
		TIME_UNIX_TO_WINDOWS(mtime,
		    fbdi->LastWriteTime.QuadPart);
		TIME_UNIX_TO_WINDOWS(ctime,
		    fbdi->ChangeTime.QuadPart);
		TIME_UNIX_TO_WINDOWS(crtime,
		    fbdi->CreationTime.QuadPart);
		TIME_UNIX_TO_WINDOWS(tzp->z_atime,
		    fbdi->LastAccessTime.QuadPart);
		fbdi->EaSize =
		    tzp->z_pflags & ZFS_REPARSE ?
		    0xa0000003 :
		    xattr_getsize(ZTOV(tzp));
		fbdi->FileAttributes =
		    zfs_getwinflags(tzp);
		fbdi->FileIndex = ctx->offset;
		fbdi->ShortNameLength = 0;
		nameptr = fbdi->FileName;
		fbdi->FileNameLength = namelenholder;

		break;

	case FileDirectoryInformation:
		structsize = FIELD_OFFSET(
		    FILE_DIRECTORY_INFORMATION,
		    FileName[0]);
		if (ctx->outcount + structsize +
		    namelenholder > ctx->bufsize)
			break;
		FILE_DIRECTORY_INFORMATION *fdi =
		    (FILE_DIRECTORY_INFORMATION *)
		    ctx->bufptr;
		next_offset = &fdi->NextEntryOffset;

		fdi->AllocationSize.QuadPart =
		    S_ISDIR(tzp->z_mode) ? 0 :
		    P2ROUNDUP(tzp->z_size,
		    zfs_blksz(tzp));
		fdi->EndOfFile.QuadPart =
		    S_ISDIR(tzp->z_mode) ? 0 :
		    tzp->z_size;
		TIME_UNIX_TO_WINDOWS(mtime,
		    fdi->LastWriteTime.QuadPart);
		TIME_UNIX_TO_WINDOWS(ctime,
		    fdi->ChangeTime.QuadPart);
		TIME_UNIX_TO_WINDOWS(crtime,
		    fdi->CreationTime.QuadPart);
		TIME_UNIX_TO_WINDOWS(tzp->z_atime,
		    fdi->LastAccessTime.QuadPart);
		fdi->FileAttributes =
		    zfs_getwinflags(tzp);
		fdi->FileIndex = ctx->offset;
		nameptr = fdi->FileName;
		fdi->FileNameLength = namelenholder;
		break;

	case FileNamesInformation:
		structsize =
		    FIELD_OFFSET(FILE_NAMES_INFORMATION,
		    FileName[0]);
		if (ctx->outcount + structsize +
		    namelenholder > ctx->bufsize)
			break;
		FILE_NAMES_INFORMATION *fni =
		    (FILE_NAMES_INFORMATION *)ctx->bufptr;
		next_offset = &fni->NextEntryOffset;

		fni->FileIndex = ctx->offset;
		nameptr = fni->FileName;
		fni->FileNameLength = namelenholder;
		break;

	case FileIdFullDirectoryInformation:
		structsize = FIELD_OFFSET(
		    FILE_ID_FULL_DIR_INFORMATION,
		    FileName[0]);
		if (ctx->outcount + structsize +
		    namelenholder > ctx->bufsize)
			break;
		FILE_ID_FULL_DIR_INFORMATION *fifdi =
		    (FILE_ID_FULL_DIR_INFORMATION *)
		    ctx->bufptr;
		next_offset = &fifdi->NextEntryOffset;

		fifdi->FileIndex = ctx->offset;
		fifdi->AllocationSize.QuadPart =
		    S_ISDIR(tzp->z_mode) ? 0 :
		    P2ROUNDUP(tzp->z_size,
		    zfs_blksz(tzp));
		fifdi->EndOfFile.QuadPart =
		    S_ISDIR(tzp->z_mode) ? 0 :
		    tzp->z_size;
		TIME_UNIX_TO_WINDOWS(mtime,
		    fifdi->LastWriteTime.QuadPart);
		TIME_UNIX_TO_WINDOWS(ctime,
		    fifdi->ChangeTime.QuadPart);
		TIME_UNIX_TO_WINDOWS(crtime,
		    fifdi->CreationTime.QuadPart);
		TIME_UNIX_TO_WINDOWS(tzp->z_atime,
		    fifdi->LastAccessTime.QuadPart);
		fifdi->EaSize =
		    tzp->z_pflags & ZFS_REPARSE ?
		    0xa0000003 :
		    xattr_getsize(ZTOV(tzp));
		fifdi->FileAttributes =
		    zfs_getwinflags(tzp);
		fifdi->FileId.QuadPart = tzp->z_id;
		nameptr = fifdi->FileName;
		fifdi->FileNameLength = namelenholder;
		break;

	case FileIdExtdDirectoryInformation:
		structsize = FIELD_OFFSET(
		    FILE_ID_EXTD_DIR_INFORMATION,
		    FileName[0]);
		if (ctx->outcount + structsize +
		    namelenholder > ctx->bufsize)
			break;
		FILE_ID_EXTD_DIR_INFORMATION *fiedi =
		    (FILE_ID_EXTD_DIR_INFORMATION *)
		    ctx->bufptr;
		next_offset = &fiedi->NextEntryOffset;

		fiedi->FileIndex = ctx->offset;
		fiedi->AllocationSize.QuadPart =
		    S_ISDIR(tzp->z_mode) ? 0 :
		    P2ROUNDUP(tzp->z_size,
		    zfs_blksz(tzp));
		fiedi->EndOfFile.QuadPart =
		    S_ISDIR(tzp->z_mode) ? 0 :
		    tzp->z_size;
		TIME_UNIX_TO_WINDOWS(mtime,
		    fiedi->LastWriteTime.QuadPart);
		TIME_UNIX_TO_WINDOWS(ctime,
		    fiedi->ChangeTime.QuadPart);
		TIME_UNIX_TO_WINDOWS(crtime,
		    fiedi->CreationTime.QuadPart);
		TIME_UNIX_TO_WINDOWS(tzp->z_atime,
		    fiedi->LastAccessTime.QuadPart);
		fiedi->EaSize =
		    tzp->z_pflags & ZFS_REPARSE ?
		    0xa0000003 :
		    xattr_getsize(ZTOV(tzp));
		fiedi->FileAttributes =
		    zfs_getwinflags(tzp);
		RtlCopyMemory(&fiedi->FileId.Identifier[0], &tzp->z_id,
		    sizeof (UINT64));
		guid = dmu_objset_fsid_guid(zfsvfs->z_os);
		RtlCopyMemory(&fiedi->FileId.Identifier[sizeof (UINT64)],
		    &guid, sizeof (UINT64));
		nameptr = fiedi->FileName;
		fiedi->FileNameLength = namelenholder;
		break;

	case FileIdExtdBothDirectoryInformation:
		structsize = FIELD_OFFSET(
		    FILE_ID_EXTD_BOTH_DIR_INFORMATION,
		    FileName[0]);
		if (ctx->outcount + structsize +
		    namelenholder > ctx->bufsize)
			break;
		FILE_ID_EXTD_BOTH_DIR_INFORMATION *fiebdi =
		    (FILE_ID_EXTD_BOTH_DIR_INFORMATION *)
		    ctx->bufptr;
		next_offset = &fiebdi->NextEntryOffset;

		fiebdi->FileIndex = ctx->offset;
		fiebdi->AllocationSize.QuadPart =
		    S_ISDIR(tzp->z_mode) ? 0 :
		    P2ROUNDUP(tzp->z_size,
		    zfs_blksz(tzp));
		fiebdi->EndOfFile.QuadPart =
		    S_ISDIR(tzp->z_mode) ? 0 :
		    tzp->z_size;
		TIME_UNIX_TO_WINDOWS(mtime,
		    fiebdi->LastWriteTime.QuadPart);
		TIME_UNIX_TO_WINDOWS(ctime,
		    fiebdi->ChangeTime.QuadPart);
		TIME_UNIX_TO_WINDOWS(crtime,
		    fiebdi->CreationTime.QuadPart);
		TIME_UNIX_TO_WINDOWS(tzp->z_atime,
		    fiebdi->LastAccessTime.QuadPart);
		fiebdi->EaSize =
		    xattr_getsize(ZTOV(tzp));
		fiebdi->ReparsePointTag =
		    tzp->z_pflags & ZFS_REPARSE ?
		    get_reparse_tag(tzp) : 0;
		fiebdi->FileAttributes =
		    zfs_getwinflags(tzp);
		fiebdi->ShortNameLength = 0;
		RtlCopyMemory(&fiebdi->FileId.Identifier[0], &tzp->z_id,
		    sizeof (UINT64));
		guid = dmu_objset_fsid_guid(zfsvfs->z_os);
		RtlCopyMemory(&fiebdi->FileId.Identifier[sizeof (UINT64)],
		    &guid, sizeof (UINT64));
		nameptr = fiebdi->FileName;
		fiebdi->FileNameLength = namelenholder;
		break;

	default:
		panic("%s unknown listing type %d\n",
		    __func__, ctx->dirlisttype);
	}

	// Release the zp
	if (get_zp == 0 && tzp != NULL) {
		VN_RELE(ZTOV(tzp));
	}

	// If know we can't fit struct, just leave
	if (ctx->outcount + structsize +
	    namelenholder > ctx->bufsize)
		return (ENOSPC);

	rawsize = structsize + namelenholder;
	reclen = DIRENT_RECLEN(rawsize); /* align to 8 */

	/*
	 * Will this entry fit in the buffer?
	 * This time with alignment
	 */
	if (ctx->outcount + rawsize > ctx->bufsize) {
		return (ENOSPC);
	}

	// If it is going to fit, compute alignment,
	// in case this dir entry is the last one,
	// we don't align last one.
	ctx->last_alignment = reclen - rawsize;

	// Convert the filename over, or as much
	// as we can fit
	ULONG namelenholder2 = 0;
	error = RtlUTF8ToUnicodeN(nameptr,
	    namelenholder, &namelenholder2,
	    name, namelen);
	ASSERT(namelenholder == namelenholder2);
#if 0
	dprintf("%s: '%.*S' -> '%s' (namelen %d bytes: "
	    "structsize %d)\n", __func__,
	    namelenholder / sizeof (WCHAR), nameptr,
	    name, namelenholder, structsize);
#endif

	/* SEARCH PATTERN */
	if (zccb->searchname.Buffer && zccb->searchname.Length) {
		UNICODE_STRING thisname;
		// dprintf("%s: '%.*S' -> '%s'\n", __func__,
		// tmpnamelen / sizeof(WCHAR), tmpname, zap.za_name);

		thisname.Buffer = nameptr;
		thisname.Length = thisname.MaximumLength = namelenholder2;
		// wildcard?
		if (zccb->ContainsWildCards) {
			if (!FsRtlIsNameInExpression(&zccb->searchname,
			    &thisname,
			    !(zfsvfs->z_case == ZFS_CASE_SENSITIVE),
			    NULL))
				return (ESRCH);
		} else {
			if (!FsRtlAreNamesEqual(&thisname,
			    &zccb->searchname,
			    !(zfsvfs->z_case == ZFS_CASE_SENSITIVE),
			    NULL))
				return (ESRCH);
		}
#if 0
		dprintf("comparing names '%.*S' == '%.*S' skip %d\n",
		    thisname.Length / sizeof (WCHAR), thisname.Buffer,
		    zccb->searchname.Length / sizeof (WCHAR),
		    zccb->searchname.Buffer,
		    skip_this_entry);
#endif
	}
	/* SEARCH PATTERN */




	// If we aren't to skip, advance all pointers
	VERIFY3P(next_offset, !=, NULL);
	ctx->next_offset = next_offset;
	*ctx->next_offset = reclen;

	ctx->outcount += reclen;
	ctx->bufptr += reclen;
	return (0);
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
	int error = 0;
	znode_t  *xzp = NULL;
	zfsvfs_t  *zfsvfs = dzp->z_zfsvfs;
	zilog_t  *zilog;
	zfs_dirlock_t  *dl;
	dmu_tx_t  *tx;
	struct vnode_attr  vattr = { 0 };
	struct componentname cn = { 0 };
	zfs_acl_ids_t	acl_ids;

	/* zfs_dirent_lock() expects a component name */

	if ((error = zfs_enter_verify_zp(zfsvfs, dzp, FTAG)) != 0)
		return (error);
	zilog = zfsvfs->z_log;

	vattr.va_type = VREG;
	vattr.va_mode = mode & ~S_IFMT;
	vattr.va_mask = ATTR_TYPE | ATTR_MODE;

	if ((error = zfs_acl_ids_create(dzp, 0,
	    &vattr, cr, NULL, &acl_ids, NULL)) != 0) {
		zfs_exit(zfsvfs, FTAG);
		return (error);
	}

	cn.cn_namelen = cn.cn_pnlen = strlen(name)+1;
	cn.cn_nameptr = cn.cn_pnbuf = (char *)kmem_zalloc(cn.cn_pnlen,
	    KM_SLEEP);

top:
	/* Lock the attribute entry name. */
	if ((error = zfs_dirent_lock(&dl, dzp, (char *)name, &xzp, flag,
	    NULL, &cn))) {
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
	 * ASSERT(xzp->z_id == zoid);
	 */
	(void) zfs_link_create(dl, xzp, tx, ZNEW);
	zfs_log_create(zilog, tx, TX_CREATE, dzp, xzp, (char *)name,
	    NULL /* vsecp */, 0 /* acl_ids.z_fuidp */, &vattr);
	dmu_tx_commit(tx);

	/*
	 * OS X - attach the vnode _after_ committing the transaction
	 */
	zfs_znode_getvnode(xzp, dzp, zfsvfs);

	zfs_dirent_unlock(dl);
out:
	zfs_acl_ids_free(&acl_ids);
	if (cn.cn_pnbuf)
		kmem_free(cn.cn_pnbuf, cn.cn_pnlen);

	/* The REPLACE error if doesn't exist is ENOATTR */
	if ((flag & ZEXISTS) && (error == ENOENT))
		error = STATUS_NO_EAS_ON_FILE;

	if (xzp)
		*vpp = ZTOV(xzp);

	zfs_exit(zfsvfs, FTAG);
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
    uintptr_t (*walk)(void *, uintptr_t, int,
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



#define	KAUTH_DIR_WRITE (KAUTH_VNODE_ACCESS | KAUTH_VNODE_ADD_FILE | \
		KAUTH_VNODE_ADD_SUBDIRECTORY | \
		KAUTH_VNODE_DELETE_CHILD)

#define	KAUTH_DIR_READ  (KAUTH_VNODE_ACCESS | KAUTH_VNODE_LIST_DIRECTORY)

#define	KAUTH_DIR_EXECUTE (KAUTH_VNODE_ACCESS | KAUTH_VNODE_SEARCH)

#define	KAUTH_FILE_WRITE (KAUTH_VNODE_ACCESS | KAUTH_VNODE_WRITE_DATA)

#define	KAUTH_FILE_READ (KAUTH_VNODE_ACCESS | KAUTH_VNODE_READ_DATA)

#define	KAUTH_FILE_EXECUTE (KAUTH_VNODE_ACCESS | KAUTH_VNODE_EXECUTE)

/*
 * Compute the same user access value as getattrlist(2)
 */
uint32_t
getuseraccess(znode_t *zp, vfs_context_t ctx)
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

		// obj_uid = pzp->zp_uid;
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

#define	KAUTH_WKG_NOT	0	/* not a well-known GUID */
#define	KAUTH_WKG_OWNER	1
#define	KAUTH_WKG_GROUP	2
#define	KAUTH_WKG_NOBODY	3
#define	KAUTH_WKG_EVERYBODY	4


static unsigned char fingerprint[] = {0xab, 0xcd, 0xef, 0xab, 0xcd, 0xef,
	0xab, 0xcd, 0xef, 0xab, 0xcd, 0xef};

/*
 * Convert "Well Known" GUID to enum type.
 */
int
kauth_wellknown_guid(guid_t *guid)
{
	uint32_t last = 0;

	if (memcmp(fingerprint, guid->g_guid, sizeof (fingerprint)))
		return (KAUTH_WKG_NOT);

	last = BE_32(*((uint32_t *)&guid->g_guid[12]));

	switch (last) {
		case 0x0c:
			return (KAUTH_WKG_EVERYBODY);
		case 0x0a:
			return (KAUTH_WKG_OWNER);
		case 0x10:
			return (KAUTH_WKG_GROUP);
		case 0xFFFFFFFE:
			return (KAUTH_WKG_NOBODY);
	}

	return (KAUTH_WKG_NOT);
}


/*
 * Set GUID to "well known" guid, based on enum type
 */
void
nfsacl_set_wellknown(int wkg, guid_t *guid)
{
	/*
	 * All WKGs begin with the same 12 bytes.
	 */
	memcpy((void *)guid, fingerprint, 12);
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
void
aces_from_acl(ace_t *aces, int *nentries, struct kauth_acl *k_acl,
    int *seen_type)
{
#if 0
	int i;
	ace_t *ace;
	guid_t *guidp;
	kauth_ace_rights_t *ace_rights;
	uid_t  who;
	uint32_t mask = 0;
	uint16_t flags = 0;
	uint16_t type = 0;
	uint32_t ace_flags;
	int wkg;
	int err = 0;

	*nentries = k_acl->acl_entrycount;

	// memset(aces, 0, sizeof (*aces) * *nentries);

	// *nentries = aclp->acl_cnt;
	for (i = 0; i < *nentries; i++) {
		// entry = &(aclp->acl_entry[i]);

		flags = 0;
		mask  = 0;

		ace = &(aces[i]);

		/* Note Mac OS X GUID is a 128-bit identifier */
		guidp = &k_acl->acl_ace[i].ace_applicable;

		who = -1;
		wkg = kauth_wellknown_guid(guidp);

		switch (wkg) {
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
					*nentries = 0;
					dprintf("ZFS: return to guid2gid\n");
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

		switch (ace_flags & KAUTH_ACE_KINDMASK) {
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
			return (error);
		// error = zpl_xattr_set_dir(vp, name, NULL, 0, flags, cr);
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
		error = -zfs_sa_set_xattr(zp, name, value, size);

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
	MD5Init(&md5c);
	MD5Update(&md5c, &namespace, sizeof (namespace));
	MD5Update(&md5c, osname, strlen(osname));
	MD5Final(uuid, &md5c);

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
	// dprintf("%s UUIDgen: [%s](%ld)->"
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
int
zfs_build_path(znode_t *start_zp, znode_t *start_parent, char **fullpath,
    uint32_t *returnsize, uint32_t *start_zp_offset)
{
	char *work;
	int index, size, part, error = 0;
	znode_t *zp = NULL;
	znode_t *dzp = NULL;
	uint64_t parent;
	zfsvfs_t *zfsvfs;
	char name[MAXPATHLEN];
	// No output? nothing to do
	if (!fullpath || !returnsize)
		return (EINVAL);
	// No input? nothing to do
	if (!start_zp)
		return (EINVAL);

	zfsvfs = start_zp->z_zfsvfs;
	zp = start_zp;

	VN_HOLD(ZTOV(zp));

	work = kmem_alloc(MAXPATHLEN * 2, KM_SLEEP);
	index = MAXPATHLEN * 2 - 1;

	work[--index] = 0;
	size = 1;

	while (1) {

		// Fetch parent
		if (start_parent) {
			dzp = start_parent;
			VN_HOLD(ZTOV(dzp));
			parent = dzp->z_id;
			start_parent = NULL;
		} else if (zp->z_sa_hdl != NULL) {
			VERIFY(sa_lookup(zp->z_sa_hdl, SA_ZPL_PARENT(zfsvfs),
			    &parent, sizeof (parent)) == 0);
			// error = zfs_zget(zfsvfs, parent, &dzp);
			error = zfs_zget_ext(zfsvfs, parent, &dzp,
			    ZGET_FLAG_UNLINKED);
			if (error) {
				dprintf("%s: zget failed %d\n",
				    __func__, error);
				goto failed;
			}
		} else if (zfsctl_is_node(zp)) {
			struct vnode *vp = NULL;
			vp = zfs_root_dotdot(ZTOV(zp));
			// .zfs/snapshot/$name - parent is snapshot
			if (vp == NULL) {
				dprintf("%s: snapshot dotdot failed %d\n",
				    __func__, error);
				goto failed;
			}
			dzp = VTOZ(vp);
		}
		// dzp held from here.

		// Find name
		if (zp->z_id == zfsvfs->z_root)
			strlcpy(name, "", MAXPATHLEN);
		else if (zp->z_id == ZFSCTL_INO_ROOT)
			strlcpy(name, ZFS_CTLDIR_NAME, MAXPATHLEN);
		else if (zp->z_id == ZFSCTL_INO_SNAPDIR)
			strlcpy(name, ZFS_SNAPDIR_NAME, MAXPATHLEN);
		else if (zfsctl_is_leafnode(zp)) {
			while (error == 0) {
				uint64_t id, pos = 0;
				boolean_t case_conflict;
				dsl_pool_config_enter(
				    dmu_objset_pool(zfsvfs->z_os), FTAG);
				error = dmu_snapshot_list_next(zfsvfs->z_os,
				    MAXPATHLEN, name, &id, &pos,
				    &case_conflict);
				dsl_pool_config_exit(
				    dmu_objset_pool(zfsvfs->z_os), FTAG);
				if (error == 0 &&
				    (ZFSCTL_INO_SNAPDIRS - id) == zp->z_id)
					break;
			}
			if (error != 0) {
				dprintf("%s: snapshot search failed %d\n",
				    __func__, error);
				goto failed;
			}
		} else {
			do {
				if ((error = zap_value_search(zfsvfs->z_os,
				    parent, zp->z_id, ZFS_DIRENT_OBJ(-1ULL),
				    name)) != 0) {
					dprintf("%s: zap_value_search %d\n",
					    __func__, error);
					goto failed;
				}
			} while (error == EBUSY);
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

		if (zp == NULL)	// No parent
			break;

		// If parent, stop, "/" is already copied in.
		if (zp->z_id == zfsvfs->z_root)
			break;
	}

	// Release "parent" if it was held, now called zp.
	if (zp != NULL)
		VN_RELE(ZTOV(zp));

	// Correct index
	if (start_zp_offset)
		*start_zp_offset = *start_zp_offset - index;

	*returnsize = size;
	ASSERT(size != 0);
	*fullpath = kmem_alloc(size, KM_SLEEP);
	memmove(*fullpath, &work[index], size);
	kmem_free(work, MAXPATHLEN * 2);

	// If "/" we don't want offset to be "1", but "0".
	if ((*fullpath)[0] == '\\' &&
	    (*fullpath)[1] == 0 &&
	    start_zp_offset)
		*start_zp_offset = 0;

	dprintf("%s: set '%s' as name\n", __func__, *fullpath);
	return (0);

failed:
	if (zp != NULL)
		VN_RELE(ZTOV(zp));
	if (dzp != NULL)
		VN_RELE(ZTOV(dzp));
	kmem_free(work, MAXPATHLEN * 2);
	return (SET_ERROR(-1));
}

/*
 * Eventually, build_path above could handle streams, but for now lets
 * just set the stream name.
 * Using FileTest on NTFS file:Zone.Identifier:$DATA returns the
 * name "/src/openzfs/zpool.exe:Zone.Identifier"
 */
int
zfs_build_path_stream(znode_t *start_zp, znode_t *start_parent, char **fullpath,
    uint32_t *returnsize, uint32_t *start_zp_offset, char *stream)
{

	if (start_zp == NULL)
		return (EINVAL);

	if (stream == NULL)
		return (EINVAL);

	if (start_zp->z_name_cache != NULL) {
		kmem_free(start_zp->z_name_cache, start_zp->z_name_len);
		start_zp->z_name_cache = NULL;
		start_zp->z_name_len = 0;
	}

	// start_parent->name + ":" + streamname + null
	start_zp->z_name_cache = kmem_asprintf("%s:%s",
	    start_parent->z_name_cache, stream);
	start_zp->z_name_len = strlen(start_zp->z_name_cache) + 1;
	// start_zp->z_name_offset = start_parent->z_name_len + 1;
	start_zp->z_name_offset = start_parent->z_name_offset;

	return (0);
}

/*
 * This is connected to IRP_MN_NOTIFY_DIRECTORY_CHANGE
 * and sending the notifications of changes
 *
 * Should be sent as "file0:streamname"
 */
void
zfs_send_notify_stream(zfsvfs_t *zfsvfs, char *name, int nameoffset,
    ULONG FilterMatch, ULONG Action, char *stream)
{
	mount_t *zmo;
	zmo = zfsvfs->z_vfs;
	UNICODE_STRING ustr;
	UNICODE_STRING ustream;

	if (name == NULL)
		return;

	AsciiStringToUnicodeString(name, &ustr);

	dprintf("%s: '%wZ' part '%S' %lu %u\n", __func__, &ustr,
	    /* &name[nameoffset], */ &ustr.Buffer[nameoffset],
	    FilterMatch, Action);

	if (stream != NULL) {
		AsciiStringToUnicodeString(stream, &ustream);
		dprintf("%s: with stream '%wZ'\n", __func__, &ustream);
	}

	/* Is nameoffset in bytes, or in characters? */
	FsRtlNotifyFilterReportChange(zmo->NotifySync, &zmo->DirNotifyList,
	    (PSTRING)&ustr,
	    nameoffset * sizeof (WCHAR),
	    stream == NULL ? NULL : (PSTRING)&ustream, // StreamName
	    NULL, FilterMatch, Action, NULL, NULL);

	FreeUnicodeString(&ustr);
	if (stream != NULL)
		FreeUnicodeString(&ustream);
}

// Filenames should be "/dir/filename:streamname"
// currently it is "streamname:$DATA"
void
zfs_send_notify(zfsvfs_t *zfsvfs, char *name, int nameoffset,
    ULONG FilterMatch, ULONG Action)
{
	zfs_send_notify_stream(zfsvfs, name, nameoffset, FilterMatch,
	    Action, NULL);
}


void
zfs_uid2sid(uint64_t uid, SID **sid)
{
	int num;
	SID *tmp;

	ASSERT(sid != NULL);

	// Root?
	num = (uid == 0) ? 1 : 2;

	tmp = ExAllocatePoolWithTag(PagedPool,
	    offsetof(SID, SubAuthority) + (num * sizeof (ULONG)), 'zsid');

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

uint64_t
zfs_sid2uid(SID *sid)
{
	// Root
	if (sid->Revision == 1 && sid->SubAuthorityCount == 1 &&
	    sid->IdentifierAuthority.Value[0] == 0 &&
	    sid->IdentifierAuthority.Value[1] == 0 &&
	    sid->IdentifierAuthority.Value[2] == 0 &&
	    sid->IdentifierAuthority.Value[3] == 0 &&
	    sid->IdentifierAuthority.Value[4] == 0 &&
	    sid->IdentifierAuthority.Value[5] == 18)
		return (0);

	// Samba's SID scheme: S-1-22-1-X
	if (sid->Revision == 1 && sid->SubAuthorityCount == 2 &&
	    sid->IdentifierAuthority.Value[0] == 0 &&
	    sid->IdentifierAuthority.Value[1] == 0 &&
	    sid->IdentifierAuthority.Value[2] == 0 &&
	    sid->IdentifierAuthority.Value[3] == 0 &&
	    sid->IdentifierAuthority.Value[4] == 0 &&
	    sid->IdentifierAuthority.Value[5] == 22 &&
	    sid->SubAuthority[0] == 1)
		return (sid->SubAuthority[1]);

	return (UID_NOBODY);
}


void
zfs_gid2sid(uint64_t gid, SID **sid)
{
	int num = 2;
	SID *tmp;

	ASSERT(sid != NULL);

	tmp = ExAllocatePoolWithTag(PagedPool,
	    offsetof(SID, SubAuthority) + (num * sizeof (ULONG)), 'zsid');

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

void
zfs_freesid(SID *sid)
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

	size = sizeof (ACL);
	i = 0;
	while (dacls[i].sid) {
		size += sizeof (ACCESS_ALLOWED_ACE);
		size += 8 + (dacls[i].sid->elements * sizeof (UINT32)) -
		    sizeof (ULONG);
		i++;
	}

	acl = ExAllocatePoolWithTag(PagedPool, size, 'zacl');
	if (!acl)
		return (NULL);

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
		aaa->Header.AceSize = sizeof (ACCESS_ALLOWED_ACE) -
		    sizeof (ULONG) + 8 +
		    (dacls[i].sid->elements * sizeof (UINT32));
		aaa->Mask = dacls[i].mask;

		RtlCopyMemory(&aaa->SidStart, dacls[i].sid,
		    8 + (dacls[i].sid->elements * sizeof (UINT32)));

		aaa = (ACCESS_ALLOWED_ACE*)((UINT8*)aaa + aaa->Header.AceSize);
		i++;
	}

	return (acl);
}


void
zfs_set_security_root(struct vnode *vp)
{
	SECURITY_DESCRIPTOR sd;
	SID *usersid = NULL, *groupsid = NULL;
	znode_t *zp = VTOZ(vp);
	NTSTATUS Status;
	ACL *acl = NULL;

	Status = RtlCreateSecurityDescriptor(&sd, SECURITY_DESCRIPTOR_REVISION);
	if (Status != STATUS_SUCCESS)
		goto err;

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
	    Status != STATUS_BUFFER_TOO_SMALL)
		goto err;

	ASSERT(buflen != 0);

	void *tmp = ExAllocatePoolWithTag(PagedPool, buflen, 'ZSEC');
	if (tmp == NULL)
		goto err;

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

int
zfs_set_security(struct vnode *vp, struct vnode *dvp)
{
	SECURITY_SUBJECT_CONTEXT subjcont;
	NTSTATUS Status;
	SID *usersid = NULL, *groupsid = NULL;
	int error = 0;

	if (vp == NULL)
		return (0);

	if (vp->security_descriptor != NULL)
		return (0);

	znode_t *zp = VTOZ(vp);
	zfsvfs_t *zfsvfs = zp->z_zfsvfs;

	// If we are the rootvp, we don't have a parent, so do different setup
	if (zp->z_id == zfsvfs->z_root ||
	    zp->z_id == ZFSCTL_INO_ROOT) {
		zfs_set_security_root(vp);
		return (0);
	}

	if ((error = zfs_enter(zfsvfs, FTAG)) != 0)
		return (error);

	// If no parent, find it. This will take one hold on
	// dvp, either directly or from zget().
	znode_t *dzp = NULL;
	if (dvp == NULL) {
		if (zp->z_sa_hdl != NULL) {
			uint64_t parent;
			if (sa_lookup(zp->z_sa_hdl, SA_ZPL_PARENT(zfsvfs),
			    &parent, sizeof (parent)) != 0) {
				goto err;
			}
			if (zfs_zget(zfsvfs, parent, &dzp)) {
				dvp = NULL;
				goto err;
			}
			dvp = ZTOV(dzp);

		} else { // What to do if no sa_hdl ?
			goto err;
		}
	} else {
		VN_HOLD(dvp);
		dzp = VTOZ(dvp);
	}

	if (vnode_security(dvp) == NULL)
		zfs_set_security(dvp, NULL);

	// We can fail here, if we are processing unlinked-list
	if (vnode_security(dvp) == NULL)
		goto err;

	ASSERT(dvp != NULL);
	ASSERT(dzp != NULL);
	ASSERT(vnode_security(dvp) != NULL);

	SeCaptureSubjectContext(&subjcont);
	void *sd = NULL;
	Status = SeAssignSecurityEx(vnode_security(dvp), NULL, (void**)&sd,
	    NULL, vnode_isdir(vp)?TRUE:FALSE, SEF_DACL_AUTO_INHERIT,
	    &subjcont, IoGetFileObjectGenericMapping(), PagedPool);

	if (Status != STATUS_SUCCESS)
		goto err;

	vnode_setsecurity(vp, sd);

	zfs_uid2sid(zp->z_uid, &usersid);
	RtlSetOwnerSecurityDescriptor(&sd, usersid, FALSE);

	zfs_gid2sid(zp->z_gid, &groupsid);
	RtlSetGroupSecurityDescriptor(&sd, groupsid, FALSE);

err:
	if (dvp)
		VN_RELE(dvp);
	zfs_exit(zfsvfs, FTAG);

	if (usersid != NULL)
		zfs_freesid(usersid);
	if (groupsid != NULL)
		zfs_freesid(groupsid);
	return (0);
}

// return true if a XATTR name should be skipped
int
xattr_protected(char *name)
{
	return (0);
}

// return true if xattr is a stream (name ends with ":$DATA")
int
xattr_stream(char *name)
{
	char tail[] = ":$DATA";
	int taillen = sizeof (tail);
	int len;

	if (name == NULL)
		return (0);
	len = strlen(name);
	if (len < taillen)
		return (0);

	if (strcmp(&name[len - taillen + 1], tail) == 0)
		return (1);

	return (0);
}

// Get the size needed for EA, check first if it is
// cached in vnode. Otherwise, compute it and set.
uint64_t
xattr_getsize(struct vnode *vp)
{
	znode_t *zp;
	zfsvfs_t *zfsvfs;
	ssize_t retsize = 0;

	if (vp == NULL)
		return (0);

#if 0
	boolean_t cached = B_FALSE;
	uint64_t cached_size = 0ULL;
	// To test the caching is correct
	cached = vnode_easize(vp, &cached_size);
#else
	// Cached? Easy, use it
	if (vnode_easize(vp, &retsize))
		return (retsize);
#endif

	zp = VTOZ(vp);
	zfsvfs = zp->z_zfsvfs;

	if (!zp->z_is_sa || zp->z_sa_hdl == NULL)
		return (0);

	zfs_uio_t uio;
	zfs_uio_iovec_init(&uio, NULL, 0, 0,
	    UIO_SYSSPACE, 0, 0);

	zpl_xattr_list(vp, &uio, &retsize, NULL);

	// It appears I should round it up here:
	retsize += (((retsize)+3) & ~3) - (retsize);

	// Cache result, even if failure (cached as 0).
	vnode_set_easize(vp, retsize);

	return (retsize);
}

/*
 * Call vnode_setunlink if zfs_zaccess_delete() allows it
 * TODO: provide credentials
 */
NTSTATUS
zfs_setunlink(FILE_OBJECT *fo, vnode_t *dvp)
{
	vnode_t *vp = NULL;
	NTSTATUS Status = STATUS_UNSUCCESSFUL;

	if (fo == NULL) {
		Status = STATUS_INVALID_PARAMETER;
		goto out;
	}

	vp = fo->FsContext;

	if (vp == NULL) {
		Status = STATUS_INVALID_PARAMETER;
		goto out;
	}

	znode_t *zp = NULL;
	znode_t *dzp = NULL;
	zfs_dirlist_t *zccb = fo->FsContext2;

	zfsvfs_t *zfsvfs;

	zp = VTOZ(vp);

	// Holding vp, not dvp, use "out:" to leave

	if (vp && zp) {
		zfsvfs = zp->z_zfsvfs;
	} else {
		Status = STATUS_INVALID_PARAMETER;
		goto out;
	}

	// If it belongs in .zfs, just reply OK.
	// mounting will attempted to delete directory
	// to replace with reparse point.
	if (zfsctl_is_node(zp)) {
		if (zfsctl_is_leafnode(zp)) {
			fo->DeletePending = TRUE;
			ASSERT3P(zccb, !=, NULL);
			zccb->deleteonclose = 1;
			// We no longer use v_unlink so lets abuse
			// it here until we decide we like it
			vp->v_unlink = 1;
			Status = STATUS_SUCCESS;
			goto out;
		}
		Status = STATUS_CANNOT_DELETE;
		goto out;
	}

	if (zfsvfs->z_rdonly || vfs_isrdonly(zfsvfs->z_vfs) ||
	    !spa_writeable(dmu_objset_spa(zfsvfs->z_os))) {
		Status = STATUS_MEDIA_WRITE_PROTECTED;
		goto out;
	}

	// Cannot delete a user mapped image.
	if (!MmFlushImageSection(&vp->SectionObjectPointers,
	    MmFlushForDelete)) {
		Status = STATUS_CANNOT_DELETE;
		goto out;
	}

	// if dvp == null, find it

	if (dvp == NULL) {
		dvp = vnode_parent(vp);
	}

	dzp = VTOZ(dvp);

	// Call out_unlock from now on
	VN_HOLD(dvp);

	// If we are root
	if (zp->z_id == zfsvfs->z_root) {
		Status = STATUS_CANNOT_DELETE;
		goto out_unlock;
	}

	// If we are a dir, and have more than "." and "..", we
	// are not empty.
	if (S_ISDIR(zp->z_mode)) {

		if (zp->z_size > 2) {
			Status = STATUS_DIRECTORY_NOT_EMPTY;
			goto out_unlock;
		}
	}

	int error = 0;

	if (dzp != NULL)
		error = zfs_zaccess_delete(dzp, zp, 0, NULL);

	if (error == 0) {
		ASSERT3P(zccb, !=, NULL);
		zccb->deleteonclose = 1;
		fo->DeletePending = TRUE;
		Status = STATUS_SUCCESS;
	} else {
		Status = STATUS_ACCESS_DENIED;
	}

out_unlock:
	if (dvp) {
		VN_RELE(dvp);
		dvp = NULL;
	}

out:
	return (Status);
}

int
uio_prefaultpages(ssize_t n, struct uio *uio)
{
	return (0);
}

/* No #pragma weaks here! */
void
dmu_buf_add_ref(dmu_buf_t *db, const void *tag)
{
	dbuf_add_ref((dmu_buf_impl_t *)db, tag);
}

boolean_t
dmu_buf_try_add_ref(dmu_buf_t *db, objset_t *os, uint64_t object,
    uint64_t blkid, const void *tag)
{
	return (dbuf_try_add_ref(db, os, object, blkid, tag));
}

/* IRP_MJ_SET_INFORMATION helpers */


NTSTATUS
set_file_basic_information(PDEVICE_OBJECT DeviceObject, PIRP Irp,
    PIO_STACK_LOCATION IrpSp)
{
	NTSTATUS Status = STATUS_INVALID_PARAMETER;

	if (IrpSp->FileObject == NULL || IrpSp->FileObject->FsContext == NULL)
		return (STATUS_INVALID_PARAMETER);

	PFILE_OBJECT FileObject = IrpSp->FileObject;
	struct vnode *vp = FileObject->FsContext;
	mount_t *zmo = DeviceObject->DeviceExtension;
	ULONG NotifyFilter = 0;

	zfsvfs_t *zfsvfs = NULL;
	if (zmo != NULL &&
	    (zfsvfs = vfs_fsprivate(zmo)) != NULL &&
	    zfsvfs->z_rdonly)
		return (STATUS_MEDIA_WRITE_PROTECTED);

	if (zfsvfs == NULL)
		return (STATUS_INVALID_PARAMETER);

	if (VN_HOLD(vp) == 0 && VTOZ(vp) != NULL) {
		FILE_BASIC_INFORMATION *fbi =
		    Irp->AssociatedIrp.SystemBuffer;
		vattr_t va = { 0 };
		uint64_t unixtime[2] = { 0 };
		znode_t *zp = VTOZ(vp);

// can request that the file system not update .. LastAccessTime,
// LastWriteTime, and ChangeTime ..  setting the appropriate members to -1.
// ie, LastAccessTime = -1 -> atime = disabled - not implemented
// LastAccessTime = -2 -> cancel the disable (-1), return to normal.
// a value of "0" means to keep existing value.
		if (fbi->ChangeTime.QuadPart > 0) {
			TIME_WINDOWS_TO_UNIX(fbi->ChangeTime.QuadPart,
			    unixtime);
			va.va_change_time.tv_sec = unixtime[0];
			va.va_change_time.tv_nsec = unixtime[1];
			va.va_active |= ATTR_CTIME;
		}
		if (fbi->LastWriteTime.QuadPart > 0) {
			TIME_WINDOWS_TO_UNIX(
			    fbi->LastWriteTime.QuadPart,
			    unixtime);
			va.va_modify_time.tv_sec = unixtime[0];
			va.va_modify_time.tv_nsec = unixtime[1];
			va.va_active |= ATTR_MTIME;
			NotifyFilter |= FILE_NOTIFY_CHANGE_LAST_WRITE;
		}
		if (fbi->CreationTime.QuadPart > 0) {
			TIME_WINDOWS_TO_UNIX(fbi->CreationTime.QuadPart,
			    unixtime);
			va.va_create_time.tv_sec = unixtime[0];
			va.va_create_time.tv_nsec = unixtime[1];
			va.va_active |= ATTR_CRTIME;  // ATTR_CRTIME
			NotifyFilter |= FILE_NOTIFY_CHANGE_CREATION;
		}
		if (fbi->LastAccessTime.QuadPart > 0) {
			TIME_WINDOWS_TO_UNIX(
			    fbi->LastAccessTime.QuadPart,
			    zp->z_atime);
			NotifyFilter |= FILE_NOTIFY_CHANGE_LAST_ACCESS;
		}
		if (fbi->FileAttributes)
			if (zfs_setwinflags(VTOZ(vp),
			    fbi->FileAttributes)) {
				va.va_active |= ATTR_MODE;
				NotifyFilter |= FILE_NOTIFY_CHANGE_ATTRIBUTES;
			}
		Status = zfs_setattr(zp, &va, 0, NULL, NULL);

		// zfs_setattr will turn ARCHIVE back on, when perhaps
		// it is set off by this call
		if (fbi->FileAttributes)
			zfs_setwinflags(zp, fbi->FileAttributes);

		if (NotifyFilter != 0)
			zfs_send_notify(zp->z_zfsvfs, zp->z_name_cache,
			    zp->z_name_offset,
			    NotifyFilter,
			    FILE_ACTION_MODIFIED);


		VN_RELE(vp);
	}

	return (Status);
}

NTSTATUS
set_file_disposition_information(PDEVICE_OBJECT DeviceObject, PIRP Irp,
    PIO_STACK_LOCATION IrpSp, boolean_t ex)
{
	NTSTATUS Status = STATUS_INVALID_PARAMETER;

	if (IrpSp->FileObject == NULL || IrpSp->FileObject->FsContext == NULL)
		return (STATUS_INVALID_PARAMETER);

	PFILE_OBJECT FileObject = IrpSp->FileObject;
	struct vnode *vp = FileObject->FsContext;
	zfs_dirlist_t *zccb = FileObject->FsContext2;
	mount_t *zmo = DeviceObject->DeviceExtension;
	ULONG flags = 0;

	zfsvfs_t *zfsvfs = NULL;
	if (zmo != NULL &&
	    (zfsvfs = vfs_fsprivate(zmo)) != NULL &&
	    zfsvfs->z_rdonly)
		return (STATUS_MEDIA_WRITE_PROTECTED);

	if (zfsvfs == NULL)
		return (STATUS_INVALID_PARAMETER);

	if (VN_HOLD(vp) == 0 && VTOZ(vp) != NULL) {

		if (ex) {
			FILE_DISPOSITION_INFORMATION_EX *fdie =
			    Irp->AssociatedIrp.SystemBuffer;
			flags = fdie->Flags;
		} else {
			FILE_DISPOSITION_INFORMATION *fdi =
			    Irp->AssociatedIrp.SystemBuffer;
			flags = fdi->DeleteFile ? FILE_DISPOSITION_DELETE : 0;
		}

		dprintf("Deletion %s on '%wZ'\n",
		    flags & FILE_DISPOSITION_DELETE ? "set" : "unset",
		    IrpSp->FileObject->FileName);
		Status = STATUS_SUCCESS;
		if (flags & FILE_DISPOSITION_DELETE) {
			Status = zfs_setunlink(IrpSp->FileObject, NULL);
		} else {
			if (zccb) zccb->deleteonclose = 0;
			FileObject->DeletePending = FALSE;
		}
		// Dirs marked for Deletion should release all
		// pending Notify events
		if (Status == STATUS_SUCCESS &&
		    (flags & FILE_DISPOSITION_DELETE)) {
//			FsRtlNotifyCleanup(zmo->NotifySync,
//			    &zmo->DirNotifyList, vp);

			FsRtlNotifyFullChangeDirectory(zmo->NotifySync,
			    &zmo->DirNotifyList,
			    FileObject->FsContext2,
			    NULL,
			    FALSE,
			    FALSE,
			    0,
			    NULL,
			    NULL,
			    NULL);
		}

		VN_RELE(vp);
	}
	return (Status);
}

NTSTATUS
set_file_endoffile_information(PDEVICE_OBJECT DeviceObject, PIRP Irp,
    PIO_STACK_LOCATION IrpSp)
{
	NTSTATUS Status = STATUS_SUCCESS;

	if (IrpSp->FileObject == NULL || IrpSp->FileObject->FsContext == NULL)
		return (STATUS_INVALID_PARAMETER);

	PFILE_OBJECT FileObject = IrpSp->FileObject;
	struct vnode *vp = FileObject->FsContext;
	zfs_dirlist_t *zccb = FileObject->FsContext2;
	FILE_END_OF_FILE_INFORMATION *feofi = Irp->AssociatedIrp.SystemBuffer;
	int changed = 0;
	int error = 0;
	mount_t *zmo = DeviceObject->DeviceExtension;

	zfsvfs_t *zfsvfs = NULL;
	if (zmo != NULL &&
	    (zfsvfs = vfs_fsprivate(zmo)) != NULL &&
	    zfsvfs->z_rdonly)
		return (STATUS_MEDIA_WRITE_PROTECTED);

	if (zfsvfs == NULL)
		return (STATUS_INVALID_PARAMETER);

	if (vnode_isdir(vp))
		return (STATUS_INVALID_PARAMETER);

	dprintf("* File_EndOfFile_Information:\n");

	if ((error = zfs_enter(zfsvfs, FTAG)) != 0)
		return (error);

	if (!VTOZ(vp) || VN_HOLD(vp) != 0) {
		zfs_exit(zfsvfs, FTAG);
		return (STATUS_INVALID_PARAMETER);
	}

	znode_t *zp = VTOZ(vp);

	// From FASTFAT
	//  This is kinda gross, but if the file is not cached, but there is
	//  a data section, we have to cache the file to avoid a bunch of
	//  extra work.
	BOOLEAN CacheMapInitialized = FALSE;
	if (FileObject && FileObject->SectionObjectPointer &&
	    (FileObject->SectionObjectPointer->DataSectionObject != NULL) &&
	    (FileObject->SectionObjectPointer->SharedCacheMap == NULL) &&
	    !FlagOn(Irp->Flags, IRP_PAGING_IO)) {

		vnode_pager_setsize(NULL, vp, zp->z_size, TRUE);

		CcInitializeCacheMap(FileObject,
		    (PCC_FILE_SIZES)&vp->FileHeader.AllocationSize,
		    FALSE,
		    &CacheManagerCallbacks, vp);

		// CcSetAdditionalCacheAttributes(FileObject, FALSE, FALSE);
		CacheMapInitialized = TRUE;
	}

	if (!zfsvfs->z_unmounted) {

		// DeleteOnClose just returns OK.
		if (zccb && zccb->deleteonclose) {
			Status = STATUS_SUCCESS;
			goto out;
		}

		// Advance only?
		if (IrpSp->Parameters.SetFile.AdvanceOnly) {
			if (feofi->EndOfFile.QuadPart > zp->z_size) {

				Status = zfs_freesp(zp,
				    feofi->EndOfFile.QuadPart,
				    0, 0, TRUE);
				changed = 1;
			}
			dprintf("%s: AdvanceOnly\n", __func__);
			goto out;
		}
		// Truncation?
		if (zp->z_size > feofi->EndOfFile.QuadPart) {
			// Are we able to truncate?
			if (FileObject->SectionObjectPointer &&
			    !MmCanFileBeTruncated(
			    FileObject->SectionObjectPointer,
			    &feofi->EndOfFile)) {
				Status = STATUS_USER_MAPPED_FILE;
				goto out;
			}
			dprintf("%s: CanTruncate\n", __func__);
		}

		// Set new size
		Status = zfs_freesp(zp, feofi->EndOfFile.QuadPart,
		    0, 0, TRUE); // Len = 0 is truncate
		changed = 1;
	}

out:

	if (NT_SUCCESS(Status) && changed) {

		dprintf("%s: new size 0x%llx set\n", __func__, zp->z_size);

		// zfs_freesp() calls vnode_paget_setsize(), but we need
		// xto update it here.
		if (FileObject->SectionObjectPointer)
			vnode_pager_setsize(FileObject, vp, zp->z_size, FALSE);

		// No notify for XATTR/Stream for now
		if (!(zp->z_pflags & ZFS_XATTR)) {
			zfs_send_notify(zfsvfs, zp->z_name_cache,
			    zp->z_name_offset,
			    FILE_NOTIFY_CHANGE_SIZE,
			    FILE_ACTION_MODIFIED);
		}
	}

	if (CacheMapInitialized) {
		CcUninitializeCacheMap(FileObject, NULL, NULL);
	}

	// We handled setsize in here.
	vnode_setsizechange(vp, 0);

	VN_RELE(vp);
	zfs_exit(zfsvfs, FTAG);
	return (Status);
}

// create hardlink by calling zfs_create
NTSTATUS
set_file_link_information(PDEVICE_OBJECT DeviceObject, PIRP Irp,
    PIO_STACK_LOCATION IrpSp)
{
	/*
	 * typedef struct _FILE_LINK_INFORMATION {
	 * BOOLEAN ReplaceIfExists;
	 * HANDLE  RootDirectory;
	 * ULONG   FileNameLength;
	 * WCHAR   FileName[1];
	 * } FILE_LINK_INFORMATION, *PFILE_LINK_INFORMATION;
	 */

	FILE_LINK_INFORMATION *link = Irp->AssociatedIrp.SystemBuffer;
	dprintf("* FileLinkInformation: %.*S\n",
	    (int)link->FileNameLength / sizeof (WCHAR), link->FileName);

	// So, use FileObject to get VP.
	// Use VP to lookup parent.
	// Use Filename to find destonation dvp, and vp if it exists.
	if (IrpSp->FileObject == NULL || IrpSp->FileObject->FsContext == NULL)
		return (STATUS_INVALID_PARAMETER);

	mount_t *zmo = DeviceObject->DeviceExtension;

	zfsvfs_t *zfsvfs = NULL;
	if (zmo != NULL &&
	    (zfsvfs = vfs_fsprivate(zmo)) != NULL &&
	    zfsvfs->z_rdonly)
		return (STATUS_MEDIA_WRITE_PROTECTED);

	if (zfsvfs == NULL)
		return (STATUS_INVALID_PARAMETER);

	FILE_OBJECT *RootFileObject = NULL;
	PFILE_OBJECT FileObject = IrpSp->FileObject;
	struct vnode *fvp = FileObject->FsContext;
	znode_t *zp = VTOZ(fvp);
	znode_t *dzp = NULL;
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
			return (STATUS_INVALID_PARAMETER);
		}
		tdvp = RootFileObject->FsContext;
		VN_HOLD(tdvp);
	} else {
		// Name can be absolute, if so use name, otherwise,
		// use vp's parent.
	}

	// Convert incoming filename to utf8
	error = RtlUnicodeToUTF8N(buffer, MAXNAMELEN, &outlen,
	    link->FileName, link->FileNameLength);

	if (error != STATUS_SUCCESS &&
	    error != STATUS_SOME_NOT_MAPPED) {
		if (tdvp) VN_RELE(tdvp);
		if (RootFileObject) ObDereferenceObject(RootFileObject);
		return (STATUS_ILLEGAL_CHARACTER);
	}

	// Output string is only null terminated if input is, so do so now.
	buffer[outlen] = 0;
	filename = buffer;

	if (strchr(filename, '/') ||
	    strchr(filename, '\\') ||
	    /* strchr(&colon[2], ':') || there is one at ":$DATA" */
	    !strcasecmp("DOSATTRIB:$DATA", filename) ||
	    !strcasecmp("EA:$DATA", filename) ||
	    !strcasecmp("reparse:$DATA", filename) ||
	    !strcasecmp("casesensitive:$DATA", filename))
		return (STATUS_OBJECT_NAME_INVALID);


	// Filename is often "\??\E:\name" so we want to eat everything
	// up to the "\name"
	if ((filename[0] == '\\') &&
	    (filename[1] == '?') &&
	    (filename[2] == '?') &&
	    (filename[3] == '\\') &&
	    /* [4] drive letter */
	    (filename[5] == ':') &&
	    (filename[6] == '\\'))
		filename = &filename[6];

	error = zfs_find_dvp_vp(zfsvfs, filename, 1, 0, &remainder, &tdvp,
	    &tvp, 0, 0);
	if (error) {
		if (tdvp) VN_RELE(tdvp);
		if (RootFileObject) ObDereferenceObject(RootFileObject);
		return (STATUS_OBJECTID_NOT_FOUND);
	}

	// Fetch parent
	VERIFY(sa_lookup(zp->z_sa_hdl, SA_ZPL_PARENT(zfsvfs),
	    &parent, sizeof (parent)) == 0);

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

	error = zfs_link(VTOZ(tdvp), VTOZ(fvp),
	    remainder ? remainder : filename, NULL, 0);

	if (error == 0) {

		// FIXME, zget to get name?
#if 0
		// Release fromname, and lookup new name
		kmem_free(zp->z_name_cache, zp->z_name_len);
		zp->z_name_cache = NULL;
		if (zfs_build_path(zp, VTOZ(tdvp), &zp->z_name_cache,
		    &zp->z_name_len, &zp->z_name_offset) == 0) {
			zfs_send_notify(zfsvfs, zp->z_name_cache,
			    zp->z_name_offset,
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

	return (error);
}

NTSTATUS
set_file_rename_information(PDEVICE_OBJECT DeviceObject, PIRP Irp,
    PIO_STACK_LOCATION IrpSp)
{
	NTSTATUS Status;
	boolean_t ExVariant =
	    IrpSp->Parameters.SetFile.FileInformationClass ==
	    FileRenameInformationEx;
/*
 * The file name string in the FileName member must be specified in
 * one of the following forms.
 *	A simple file name. (The RootDirectory member is NULL.) In this case,
 * the file is simply renamed within the same directory.
 *	That is, the rename operation changes the name of the file but not its
 * location.
 *
 *	A fully qualified file name. (The RootDirectory member is NULL.)
 * In this case, the rename operation changes the name and location
 * of the file.
 *
 *	A relative file name. In this case, the RootDirectory member contains
 * a handle to the target directory for the rename operation. The file
 * name itself must be a simple file name.
 *
 *	NOTE: The RootDirectory handle thing never happens, and no sample
 * source (including fastfat) handles it.
 */

	FILE_RENAME_INFORMATION *ren = Irp->AssociatedIrp.SystemBuffer;
	dprintf("* FileRenameInformation: %.*S\n",
	    (int)ren->FileNameLength / sizeof (WCHAR), ren->FileName);

	// ASSERT(ren->RootDirectory == NULL);

	// So, use FileObject to get VP.
	// Use VP to lookup parent.
	// Use Filename to find destonation dvp, and vp if it exists.
	if (IrpSp->FileObject == NULL || IrpSp->FileObject->FsContext == NULL)
		return (STATUS_INVALID_PARAMETER);

	mount_t *zmo = DeviceObject->DeviceExtension;

	zfsvfs_t *zfsvfs = NULL;
	if (zmo != NULL &&
	    (zfsvfs = vfs_fsprivate(zmo)) != NULL &&
	    zfsvfs->z_rdonly)
		return (STATUS_MEDIA_WRITE_PROTECTED);

	if (zfsvfs == NULL)
		return (STATUS_INVALID_PARAMETER);

	PFILE_OBJECT FileObject = IrpSp->FileObject;
	struct vnode *fvp = FileObject->FsContext;
	znode_t *zp = VTOZ(fvp);
	znode_t *dzp = NULL;
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
		return (STATUS_ILLEGAL_CHARACTER);
	}
	SetFlag(FileObject->Flags, FO_FILE_MODIFIED);
	// Output string is only null terminated if input is, so do so now.
	buffer[outlen] = 0;
	filename = buffer;

	// Filename is often "\??\E:\lower\name" - and "/lower" might be
	// another dataset so we need to drive a lookup, with
	// SL_OPEN_TARGET_DIRECTORY set so we get the parent of where
	// we are renaming to. This will give us "tdvp", and
	// possibly "tvp" is we are to rename over an item.

	/* Quick check to see if it ends in reserved names */
	char *tail;
	tail = strrchr(filename, '\\');
	if (tail == NULL)
		tail = filename;

	if (strchr(tail, ':') ||
	    !strcasecmp("DOSATTRIB", tail) ||
	    !strcasecmp("EA", tail) ||
	    !strcasecmp("reparse", tail) ||
	    !strcasecmp("casesensitive", tail))
		return (STATUS_OBJECT_NAME_INVALID);

	// If it starts with "\" drive the lookup, if it is just a name
	// like "HEAD", assume tdvp is same as fdvp.
	if (filename[0] == '\\') {
		OBJECT_ATTRIBUTES oa;
		IO_STATUS_BLOCK ioStatus;
		UNICODE_STRING uFileName;
		// RtlInitEmptyUnicodeString(&uFileName, ren->FileName,
		// ren->FileNameLength);  // doesn't set length
		// Is there really no offical wrapper to do this?
		uFileName.Length = uFileName.MaximumLength =
		    ren->FileNameLength;
		uFileName.Buffer = ren->FileName;

		InitializeObjectAttributes(&oa, &uFileName,
		    OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
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
		    IO_FORCE_ACCESS_CHECK | IO_OPEN_TARGET_DIRECTORY |
		    IO_NO_PARAMETER_CHECKING);

		if (!NT_SUCCESS(Status))
			return (STATUS_INVALID_PARAMETER);

		// We have the targetdirectoryparent - get FileObject.
		Status = ObReferenceObjectByHandle(destParentHandle,
		    STANDARD_RIGHTS_REQUIRED,
		    *IoFileObjectType,
		    KernelMode,
		    &dFileObject,
		    NULL);
		if (!NT_SUCCESS(Status)) {
			ZwClose(destParentHandle);
			return (STATUS_INVALID_PARAMETER);
		}

		// All exits need to go through "out:" at this point on.

		// Assign tdvp
		tdvp = dFileObject->FsContext;


		// Hold it
		VERIFY0(VN_HOLD(tdvp));

		// Filename is '\??\E:\dir\dir\file' and we only care about
		// the last part.
		char *r = strrchr(filename, '\\');
		if (r == NULL)
			r = strrchr(filename, '/');
		if (r != NULL) {
			r++;
			filename = r;
		}

		error = zfs_find_dvp_vp(zfsvfs, filename, 1, 0, &remainder,
		    &tdvp, &tvp, 0, 0);
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
	if (tvp && !ExVariant && !ren->ReplaceIfExists) {
		error = STATUS_OBJECT_NAME_COLLISION;
		goto out;
	}
	if (tvp && ExVariant && !(ren->Flags&FILE_RENAME_REPLACE_IF_EXISTS)) {
		error = STATUS_OBJECT_NAME_COLLISION;
		goto out;
	}

	VERIFY(sa_lookup(zp->z_sa_hdl, SA_ZPL_PARENT(zfsvfs),
	    &parent, sizeof (parent)) == 0);

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
	    VTOZ(tdvp), remainder ? remainder : filename, NULL, 0, 0, NULL,
	    NULL);

	if (error == 0) {
		// rename file in same directory:
		// sned dir modified, send OLD_NAME, NEW_NAME
		// Moving to different volume:
		// FILE_ACTION_REMOVED, FILE_ACTION_ADDED
		// send CHANGE_LAST_WRITE
		znode_t *tdzp = VTOZ(tdvp);
		zfs_send_notify(zfsvfs, tdzp->z_name_cache, tdzp->z_name_offset,
		    FILE_NOTIFY_CHANGE_LAST_WRITE,
		    FILE_ACTION_MODIFIED);

		zfs_send_notify(zfsvfs, zp->z_name_cache, zp->z_name_offset,
		    vnode_isdir(fvp) ?
		    FILE_NOTIFY_CHANGE_DIR_NAME :
		    FILE_NOTIFY_CHANGE_FILE_NAME,
		    FILE_ACTION_RENAMED_OLD_NAME);

		// Release fromname, and lookup new name
		kmem_free(zp->z_name_cache, zp->z_name_len);
		zp->z_name_cache = NULL;

		if (zfs_build_path(zp, tdzp, &zp->z_name_cache,
		    &zp->z_name_len, &zp->z_name_offset) == 0) {
			zfs_send_notify(zfsvfs, zp->z_name_cache,
			    zp->z_name_offset,
			    vnode_isdir(fvp) ?
			    FILE_NOTIFY_CHANGE_DIR_NAME :
			    FILE_NOTIFY_CHANGE_FILE_NAME,
			    FILE_ACTION_RENAMED_NEW_NAME);
		}
	}
	// Release all holds
	if (error == EBUSY)
		error = STATUS_ACCESS_DENIED;
out:
	if (destParentHandle != 0)
		ZwClose(destParentHandle);
	if (dFileObject)
		ObDereferenceObject(dFileObject);
	if (tdvp) VN_RELE(tdvp);
	if (fdvp) VN_RELE(fdvp);
	if (fvp) VN_RELE(fvp);
	if (tvp) VN_RELE(tvp);

	return (error);
}

NTSTATUS
set_file_valid_data_length_information(PDEVICE_OBJECT DeviceObject, PIRP Irp,
    PIO_STACK_LOCATION IrpSp)
{
	NTSTATUS Status;
	FILE_VALID_DATA_LENGTH_INFORMATION *fvdli =
	    Irp->AssociatedIrp.SystemBuffer;
	dprintf("* FileValidDataLengthInformation: \n");
	mount_t *zmo = DeviceObject->DeviceExtension;
	int error;

	if (IrpSp->Parameters.SetFile.Length <
	    sizeof (FILE_VALID_DATA_LENGTH_INFORMATION))
		return (STATUS_INVALID_PARAMETER);

	if (IrpSp->FileObject == NULL ||
	    IrpSp->FileObject->FsContext == NULL)
		return (STATUS_INVALID_PARAMETER);

	struct vnode *vp = IrpSp->FileObject->FsContext;
	znode_t *zp = VTOZ(vp);

	if (zmo == NULL || zp == NULL)
		return (STATUS_INVALID_PARAMETER);

	zfsvfs_t *zfsvfs = vfs_fsprivate(zmo);
	if (zfsvfs == NULL)
		return (STATUS_INVALID_PARAMETER);

	if ((error = zfs_enter(zfsvfs, FTAG)) != 0)
		return (error);

	if (fvdli->ValidDataLength.QuadPart <=
	    vp->FileHeader.ValidDataLength.QuadPart ||
	    fvdli->ValidDataLength.QuadPart >
	    vp->FileHeader.FileSize.QuadPart) {
		dprintf("invalid VDL of %I64u (%I64u, file %I64u)\n",
		    fvdli->ValidDataLength.QuadPart,
		    vp->FileHeader.ValidDataLength.QuadPart,
		    vp->FileHeader.FileSize.QuadPart);
		Status = STATUS_INVALID_PARAMETER;
		goto end;
	}

	vp->FileHeader.ValidDataLength = fvdli->ValidDataLength;
	vnode_setsizechange(vp, 1);

	zfs_send_notify(zp->z_zfsvfs, zp->z_name_cache,
	    zp->z_name_offset,
	    FILE_NOTIFY_CHANGE_SIZE,
	    FILE_ACTION_MODIFIED);

	Status = STATUS_SUCCESS;

end:
	zfs_exit(zfsvfs, FTAG);

	return (Status);
}

NTSTATUS
set_file_position_information(PDEVICE_OBJECT DeviceObject, PIRP Irp,
    PIO_STACK_LOCATION IrpSp)
{
	FILE_POSITION_INFORMATION *fpi = Irp->AssociatedIrp.SystemBuffer;
	dprintf("* FilePositionInformation: \n");

	if (IrpSp->FileObject == NULL || IrpSp->FileObject->FsContext == NULL)
		return (STATUS_INVALID_PARAMETER);

	if (IrpSp->Parameters.SetFile.Length <
	    sizeof (FILE_POSITION_INFORMATION))
		return (STATUS_INVALID_PARAMETER);

	IrpSp->FileObject->CurrentByteOffset = fpi->CurrentByteOffset;
	return (STATUS_SUCCESS);
}

/* IRP_MJ_QUERY_INFORMATION helpers */
ULONG
get_reparse_tag(znode_t *zp)
{
	if (!(zp->z_pflags & ZFS_REPARSE))
		return (0);

	if (zfsctl_is_node(zp))
		return (zfsctl_get_reparse_tag(zp));

	int err;
	REPARSE_DATA_BUFFER tagdata;
	struct iovec iov;
	iov.iov_base = (void *)&tagdata;
	iov.iov_len = sizeof (tagdata);

	zfs_uio_t uio;
	zfs_uio_iovec_init(&uio, &iov, 1, 0, UIO_SYSSPACE,
	    sizeof (tagdata), 0);
	err = zfs_readlink(ZTOV(zp), &uio, NULL);
	return (tagdata.ReparseTag);
}

NTSTATUS
file_attribute_tag_information(PDEVICE_OBJECT DeviceObject, PIRP Irp,
    PIO_STACK_LOCATION IrpSp, FILE_ATTRIBUTE_TAG_INFORMATION *tag)
{
	dprintf("   %s\n", __func__);
	if (IrpSp->Parameters.QueryFile.Length <
	    sizeof (FILE_ATTRIBUTE_TAG_INFORMATION)) {
		Irp->IoStatus.Information =
		    sizeof (FILE_ATTRIBUTE_TAG_INFORMATION);
		return (STATUS_BUFFER_TOO_SMALL);
	}

	if (IrpSp->FileObject && IrpSp->FileObject->FsContext) {
		struct vnode *vp = IrpSp->FileObject->FsContext;
		znode_t *zp = VTOZ(vp);
		zfsvfs_t *zfsvfs = zp->z_zfsvfs;

		tag->FileAttributes = zfs_getwinflags(zp);
		tag->ReparseTag = get_reparse_tag(zp);
		Irp->IoStatus.Information =
		    sizeof (FILE_ATTRIBUTE_TAG_INFORMATION);
		ASSERT(tag->FileAttributes != 0);
		return (STATUS_SUCCESS);
	}
	return (STATUS_INVALID_PARAMETER);
}

NTSTATUS
file_internal_information(PDEVICE_OBJECT DeviceObject, PIRP Irp,
    PIO_STACK_LOCATION IrpSp, FILE_INTERNAL_INFORMATION *infernal)
{
	dprintf("   %s\n", __func__);
	if (IrpSp->Parameters.QueryFile.Length <
	    sizeof (FILE_INTERNAL_INFORMATION)) {
		Irp->IoStatus.Information = sizeof (FILE_INTERNAL_INFORMATION);
		return (STATUS_BUFFER_TOO_SMALL);
	}

	if (IrpSp->FileObject && IrpSp->FileObject->FsContext) {
		struct vnode *vp = IrpSp->FileObject->FsContext;
		zfs_dirlist_t *zccb = IrpSp->FileObject->FsContext2;
		znode_t *zp = VTOZ(vp);
		/* For streams, we need to reply with parent file */
		if (zccb && zp->z_pflags & ZFS_XATTR)
			infernal->IndexNumber.QuadPart = zccb->real_file_id;
		else
			infernal->IndexNumber.QuadPart = zp->z_id;
		Irp->IoStatus.Information = sizeof (FILE_INTERNAL_INFORMATION);
		return (STATUS_SUCCESS);
	}

	return (STATUS_NO_SUCH_FILE);
}

NTSTATUS
file_basic_information(PDEVICE_OBJECT DeviceObject, PIRP Irp,
    PIO_STACK_LOCATION IrpSp, FILE_BASIC_INFORMATION *basic)
{
	dprintf("   %s\n", __func__);

	if (IrpSp->Parameters.QueryFile.Length <
	    sizeof (FILE_BASIC_INFORMATION)) {
		Irp->IoStatus.Information = sizeof (FILE_BASIC_INFORMATION);
		return (STATUS_BUFFER_TOO_SMALL);
	}

	if (IrpSp->FileObject && IrpSp->FileObject->FsContext) {
		struct vnode *vp = IrpSp->FileObject->FsContext;
		if (VN_HOLD(vp) == 0) {
			znode_t *zp = VTOZ(vp);
			zfsvfs_t *zfsvfs = zp->z_zfsvfs;
			if (zp->z_is_sa) {
				sa_bulk_attr_t bulk[3];
				int count = 0;
				uint64_t mtime[2];
				uint64_t ctime[2];
				uint64_t crtime[2];
				SA_ADD_BULK_ATTR(bulk, count,
				    SA_ZPL_MTIME(zfsvfs),
				    NULL, &mtime, 16);
				SA_ADD_BULK_ATTR(bulk, count,
				    SA_ZPL_CTIME(zfsvfs),
				    NULL, &ctime, 16);
				SA_ADD_BULK_ATTR(bulk, count,
				    SA_ZPL_CRTIME(zfsvfs),
				    NULL, &crtime, 16);
				sa_bulk_lookup(zp->z_sa_hdl, bulk, count);

				TIME_UNIX_TO_WINDOWS(mtime,
				    basic->LastWriteTime.QuadPart);
				TIME_UNIX_TO_WINDOWS(ctime,
				    basic->ChangeTime.QuadPart);
				TIME_UNIX_TO_WINDOWS(crtime,
				    basic->CreationTime.QuadPart);
				TIME_UNIX_TO_WINDOWS(zp->z_atime,
				    basic->LastAccessTime.QuadPart);
			}
			// FileAttributes == 0 means don't set
			// - undocumented, but seen in fastfat
			// if (basic->FileAttributes != 0)
			basic->FileAttributes = zfs_getwinflags(zp);

			VN_RELE(vp);
		}
		Irp->IoStatus.Information = sizeof (FILE_BASIC_INFORMATION);
		return (STATUS_SUCCESS);
	}

	// This can be called from diskDispatcher, referring to the volume.
	// if so, make something up. Is this the right thing to do?

	if (IrpSp->FileObject && IrpSp->FileObject->FsContext == NULL) {
		mount_t *zmo = DeviceObject->DeviceExtension;
		zfsvfs_t *zfsvfs = vfs_fsprivate(zmo);

		LARGE_INTEGER JanOne1980 = { 0xe1d58000, 0x01a8e79f };
		ExLocalTimeToSystemTime(&JanOne1980,
		    &basic->LastWriteTime);
		basic->CreationTime = basic->LastAccessTime =
		    basic->LastWriteTime;
		basic->FileAttributes = FILE_ATTRIBUTE_DIRECTORY;
		if (zfsvfs->z_rdonly)
			basic->FileAttributes |= FILE_ATTRIBUTE_READONLY;
		Irp->IoStatus.Information = sizeof (FILE_BASIC_INFORMATION);
		return (STATUS_SUCCESS);
	}

	dprintf("   %s failing\n", __func__);
	return (STATUS_OBJECT_NAME_NOT_FOUND);
}
NTSTATUS
file_compression_information(PDEVICE_OBJECT DeviceObject, PIRP Irp,
    PIO_STACK_LOCATION IrpSp, FILE_COMPRESSION_INFORMATION *fci)
{
	dprintf("   %s\n", __func__);

	if (IrpSp->Parameters.QueryFile.Length <
	    sizeof (FILE_COMPRESSION_INFORMATION)) {
		Irp->IoStatus.Information =
		    sizeof (FILE_COMPRESSION_INFORMATION);
		return (STATUS_BUFFER_TOO_SMALL);
	}

	if (IrpSp->FileObject && IrpSp->FileObject->FsContext) {
		struct vnode *vp = IrpSp->FileObject->FsContext;
		if (VN_HOLD(vp) == 0) {
			znode_t *zp = VTOZ(vp);
			zfsvfs_t *zfsvfs = zp->z_zfsvfs;

			memset(fci, 0, sizeof (FILE_COMPRESSION_INFORMATION));

			// Deal with ads here, and send adsdata.length
			if (vnode_isdir(vp))
				fci->CompressedFileSize.QuadPart = zp->z_size;

			VN_RELE(vp);
		}
		Irp->IoStatus.Information =
		    sizeof (FILE_COMPRESSION_INFORMATION);
		return (STATUS_SUCCESS);
	}

	return (STATUS_INVALID_PARAMETER);
}

uint64_t
zfs_blksz(znode_t *zp)
{
	if (zp->z_blksz)
		return (zp->z_blksz);
	if (zp->z_sa_hdl) {
		uint32_t blksize;
		uint64_t nblks;
		sa_object_size(zp->z_sa_hdl, &blksize, &nblks);
		if (blksize)
			return ((uint64_t)blksize);
	}

	if (zp->z_zfsvfs->z_max_blksz)
		return (zp->z_zfsvfs->z_max_blksz);
	return (512ULL);
}

NTSTATUS
file_standard_information(PDEVICE_OBJECT DeviceObject, PIRP Irp,
    PIO_STACK_LOCATION IrpSp, FILE_STANDARD_INFORMATION *standard)
{
	dprintf("   %s\n", __func__);

	if (IrpSp->Parameters.QueryFile.Length <
	    sizeof (FILE_STANDARD_INFORMATION)) {
		Irp->IoStatus.Information = sizeof (FILE_STANDARD_INFORMATION);
		return (STATUS_BUFFER_TOO_SMALL);
	}

	standard->Directory = TRUE;
	standard->AllocationSize.QuadPart = 512;
	standard->EndOfFile.QuadPart = 512;
	standard->DeletePending = FALSE;
	standard->NumberOfLinks = 1;
	if (IrpSp->FileObject && IrpSp->FileObject->FsContext) {
		struct vnode *vp = IrpSp->FileObject->FsContext;
		zfs_dirlist_t *zccb = IrpSp->FileObject->FsContext2;
		VN_HOLD(vp);
		znode_t *zp = VTOZ(vp);
		standard->Directory = vnode_isdir(vp) ? TRUE : FALSE;
		// sa_object_size(zp->z_sa_hdl, &blksize, &nblks);
		uint64_t blk = zfs_blksz(zp);
		// space taken on disk, multiples of block size

		standard->AllocationSize.QuadPart = allocationsize(zp);
		standard->EndOfFile.QuadPart = vnode_isdir(vp) ? 0 : zp->z_size;
		standard->NumberOfLinks = zp->z_links;
		standard->DeletePending = zccb &&
		    zccb->deleteonclose ? TRUE : FALSE;
		Irp->IoStatus.Information = sizeof (FILE_STANDARD_INFORMATION);

#ifndef FILE_STANDARD_INFORMATION_EX
		typedef struct _FILE_STANDARD_INFORMATION_EX {
		    LARGE_INTEGER AllocationSize;
		    LARGE_INTEGER EndOfFile;
		    ULONG NumberOfLinks;
		    BOOLEAN DeletePending;
		    BOOLEAN Directory;
		    BOOLEAN AlternateStream;
		    BOOLEAN MetadataAttribute;
		} FILE_STANDARD_INFORMATION_EX, *PFILE_STANDARD_INFORMATION_EX;
#endif
		if (IrpSp->Parameters.QueryFile.Length >=
		    sizeof (FILE_STANDARD_INFORMATION_EX)) {
			FILE_STANDARD_INFORMATION_EX *estandard;
			estandard = (FILE_STANDARD_INFORMATION_EX *)standard;
			estandard->AlternateStream = zp->z_pflags & ZFS_XATTR;
			estandard->MetadataAttribute = FALSE;
			Irp->IoStatus.Information =
			    sizeof (FILE_STANDARD_INFORMATION_EX);
		}

		VN_RELE(vp);
		dprintf("Returning size %llu and allocsize %llu\n",
		    standard->EndOfFile.QuadPart,
		    standard->AllocationSize.QuadPart);

		return (STATUS_SUCCESS);
	}
	return (STATUS_OBJECT_NAME_NOT_FOUND);
}

NTSTATUS
file_position_information(PDEVICE_OBJECT DeviceObject, PIRP Irp,
    PIO_STACK_LOCATION IrpSp, FILE_POSITION_INFORMATION *position)
{
	dprintf("   %s\n", __func__);

	if (IrpSp->Parameters.QueryFile.Length <
	    sizeof (FILE_POSITION_INFORMATION)) {
		Irp->IoStatus.Information = sizeof (FILE_POSITION_INFORMATION);
		return (STATUS_BUFFER_TOO_SMALL);
	}

	if (IrpSp->FileObject)
		position->CurrentByteOffset.QuadPart =
		    IrpSp->FileObject->CurrentByteOffset.QuadPart;

	Irp->IoStatus.Information = sizeof (FILE_POSITION_INFORMATION);
	return (STATUS_SUCCESS);
}

NTSTATUS
file_ea_information(PDEVICE_OBJECT DeviceObject, PIRP Irp,
    PIO_STACK_LOCATION IrpSp, FILE_EA_INFORMATION *ea)
{
	NTSTATUS Status = STATUS_INVALID_PARAMETER;

	dprintf("   %s\n", __func__);
	if (IrpSp->Parameters.QueryFile.Length < sizeof (FILE_EA_INFORMATION)) {
		Irp->IoStatus.Information = sizeof (FILE_EA_INFORMATION);
		return (STATUS_BUFFER_TOO_SMALL);
	}

	ea->EaSize = 0;

	if (IrpSp->FileObject && IrpSp->FileObject->FsContext) {
		struct vnode *vp = IrpSp->FileObject->FsContext;

		ea->EaSize = xattr_getsize(vp);

		dprintf("%s: returning size %d / 0x%x\n", __func__,
		    ea->EaSize, ea->EaSize);

		Irp->IoStatus.Information = sizeof (FILE_EA_INFORMATION);
		return (STATUS_SUCCESS);
	}

	return (STATUS_INVALID_PARAMETER);
}

NTSTATUS
file_alignment_information(PDEVICE_OBJECT DeviceObject, PIRP Irp,
    PIO_STACK_LOCATION IrpSp, FILE_ALIGNMENT_INFORMATION *fai)
{
	dprintf("   %s\n", __func__);
	if (IrpSp->Parameters.QueryFile.Length <
	    sizeof (FILE_ALIGNMENT_INFORMATION)) {
		Irp->IoStatus.Information = sizeof (FILE_ALIGNMENT_INFORMATION);
		return (STATUS_BUFFER_TOO_SMALL);
	}

	fai->AlignmentRequirement = 0; /* FILE_WORD_ALIGNMENT; */
	return (STATUS_SUCCESS);
}

NTSTATUS
file_network_open_information(PDEVICE_OBJECT DeviceObject, PIRP Irp,
    PIO_STACK_LOCATION IrpSp, FILE_NETWORK_OPEN_INFORMATION *netopen)
{
	dprintf("   %s\n", __func__);

	if (IrpSp->Parameters.QueryFile.Length <
	    sizeof (FILE_NETWORK_OPEN_INFORMATION)) {
		Irp->IoStatus.Information =
		    sizeof (FILE_NETWORK_OPEN_INFORMATION);
		return (STATUS_BUFFER_TOO_SMALL);
	}

	if (IrpSp->FileObject && IrpSp->FileObject->FsContext) {
		struct vnode *vp = IrpSp->FileObject->FsContext;
		znode_t *zp = VTOZ(vp);
		zfsvfs_t *zfsvfs = zp->z_zfsvfs;
		if (zp->z_is_sa) {
			sa_bulk_attr_t bulk[3];
			int count = 0;
			uint64_t mtime[2];
			uint64_t ctime[2];
			uint64_t crtime[2];
			SA_ADD_BULK_ATTR(bulk, count,
			    SA_ZPL_MTIME(zfsvfs), NULL,
			    &mtime, 16);
			SA_ADD_BULK_ATTR(bulk, count,
			    SA_ZPL_CTIME(zfsvfs), NULL,
			    &ctime, 16);
			SA_ADD_BULK_ATTR(bulk, count,
			    SA_ZPL_CRTIME(zfsvfs), NULL,
			    &crtime, 16);
			sa_bulk_lookup(zp->z_sa_hdl, bulk, count);

			TIME_UNIX_TO_WINDOWS(mtime,
			    netopen->LastWriteTime.QuadPart);
			TIME_UNIX_TO_WINDOWS(ctime,
			    netopen->ChangeTime.QuadPart);
			TIME_UNIX_TO_WINDOWS(crtime,
			    netopen->CreationTime.QuadPart);
			TIME_UNIX_TO_WINDOWS(zp->z_atime,
			    netopen->LastAccessTime.QuadPart);
		}
		netopen->AllocationSize.QuadPart =
		    P2ROUNDUP(zp->z_size, zfs_blksz(zp));
		netopen->EndOfFile.QuadPart = vnode_isdir(vp) ? 0 : zp->z_size;
		netopen->FileAttributes = zfs_getwinflags(zp);
		Irp->IoStatus.Information =
		    sizeof (FILE_NETWORK_OPEN_INFORMATION);
		return (STATUS_SUCCESS);
	}

	return (STATUS_OBJECT_PATH_NOT_FOUND);
}

NTSTATUS
file_standard_link_information(PDEVICE_OBJECT DeviceObject, PIRP Irp,
    PIO_STACK_LOCATION IrpSp, FILE_STANDARD_LINK_INFORMATION *fsli)
{
	PFILE_OBJECT FileObject = IrpSp->FileObject;

	dprintf("   %s\n", __func__);

	if (IrpSp->Parameters.QueryFile.Length <
	    sizeof (FILE_STANDARD_LINK_INFORMATION)) {
		Irp->IoStatus.Information =
		    sizeof (FILE_STANDARD_LINK_INFORMATION);
		return (STATUS_BUFFER_TOO_SMALL);
	}

	if (IrpSp->FileObject && IrpSp->FileObject->FsContext) {
		struct vnode *vp = IrpSp->FileObject->FsContext;
		zfs_dirlist_t *zccb = IrpSp->FileObject->FsContext2;

		znode_t *zp = VTOZ(vp);

		fsli->NumberOfAccessibleLinks = zp->z_links;
		fsli->TotalNumberOfLinks = zp->z_links;
		fsli->DeletePending = zccb &&
		    zccb->deleteonclose ? TRUE : FALSE;
		fsli->Directory = S_ISDIR(zp->z_mode);
	}

	Irp->IoStatus.Information = sizeof (FILE_STANDARD_LINK_INFORMATION);
	return (STATUS_SUCCESS);
}

NTSTATUS
file_id_information(PDEVICE_OBJECT DeviceObject, PIRP Irp,
    PIO_STACK_LOCATION IrpSp, FILE_ID_INFORMATION *fii)
{
	PFILE_OBJECT FileObject = IrpSp->FileObject;

	dprintf("   %s\n", __func__);
	if (IrpSp->Parameters.QueryFile.Length < sizeof (FILE_ID_INFORMATION)) {
		Irp->IoStatus.Information = sizeof (FILE_ID_INFORMATION);
		return (STATUS_BUFFER_TOO_SMALL);
	}

	struct vnode *vp = FileObject->FsContext;

	znode_t *zp = VTOZ(vp);
	zfsvfs_t *zfsvfs = zp->z_zfsvfs;

	fii->VolumeSerialNumber = 0x19831116;

	RtlCopyMemory(&fii->FileId.Identifier[0], &zp->z_id, sizeof (UINT64));
	uint64_t guid = dmu_objset_fsid_guid(zfsvfs->z_os);
	RtlCopyMemory(&fii->FileId.Identifier[sizeof (UINT64)],
	    &guid, sizeof (UINT64));

	Irp->IoStatus.Information = sizeof (FILE_ID_INFORMATION);
	return (STATUS_SUCCESS);
}

NTSTATUS
file_case_sensitive_information(PDEVICE_OBJECT DeviceObject, PIRP Irp,
    PIO_STACK_LOCATION IrpSp, FILE_CASE_SENSITIVE_INFORMATION *fcsi)
{
	PFILE_OBJECT FileObject = IrpSp->FileObject;

	dprintf("   %s\n", __func__);

	if (IrpSp->Parameters.QueryFile.Length <
	    sizeof (FILE_CASE_SENSITIVE_INFORMATION)) {
		Irp->IoStatus.Information =
		    sizeof (FILE_CASE_SENSITIVE_INFORMATION);
		return (STATUS_BUFFER_TOO_SMALL);
	}

	fcsi->Flags = 0;

	struct vnode *vp = FileObject->FsContext;
	if (vp != NULL) {
		znode_t *zp = VTOZ(vp);
		if (zp != NULL) {
		zfsvfs_t *zfsvfs = zp->z_zfsvfs;

		if (zfsvfs->z_case == ZFS_CASE_SENSITIVE)
			fcsi->Flags |= FILE_CS_FLAG_CASE_SENSITIVE_DIR;

		}
	}

	Irp->IoStatus.Information = sizeof (FILE_CASE_SENSITIVE_INFORMATION);
	return (STATUS_SUCCESS);
}

NTSTATUS
file_stat_information(PDEVICE_OBJECT DeviceObject, PIRP Irp,
    PIO_STACK_LOCATION IrpSp, FILE_STAT_INFORMATION *fsi)
{
	PFILE_OBJECT FileObject = IrpSp->FileObject;

	dprintf("   %s\n", __func__);

	/* vp is already help in query_information */
	struct vnode *vp = FileObject->FsContext;

	if (vp) {

		znode_t *zp = VTOZ(vp);
		zfsvfs_t *zfsvfs = zp->z_zfsvfs;
		if (zp->z_is_sa) {
			sa_bulk_attr_t bulk[3];
			int count = 0;
			uint64_t mtime[2];
			uint64_t ctime[2];
			uint64_t crtime[2];
			SA_ADD_BULK_ATTR(bulk, count,
			    SA_ZPL_MTIME(zfsvfs), NULL,
			    &mtime, 16);
			SA_ADD_BULK_ATTR(bulk, count,
			    SA_ZPL_CTIME(zfsvfs), NULL,
			    &ctime, 16);
			SA_ADD_BULK_ATTR(bulk, count,
			    SA_ZPL_CRTIME(zfsvfs), NULL,
			    &crtime, 16);
			sa_bulk_lookup(zp->z_sa_hdl, bulk, count);

			TIME_UNIX_TO_WINDOWS(crtime,
			    fsi->CreationTime.QuadPart);
			TIME_UNIX_TO_WINDOWS(zp->z_atime,
			    fsi->LastAccessTime.QuadPart);
			TIME_UNIX_TO_WINDOWS(mtime,
			    fsi->LastWriteTime.QuadPart);
			TIME_UNIX_TO_WINDOWS(ctime,
			    fsi->ChangeTime.QuadPart);
		}
		fsi->FileId.QuadPart = zp->z_id;
		fsi->AllocationSize.QuadPart =
		    P2ROUNDUP(zp->z_size, zfs_blksz(zp));
		fsi->EndOfFile.QuadPart = zp->z_size;
		fsi->FileAttributes = zfs_getwinflags(zp);
		fsi->ReparseTag = get_reparse_tag(zp);
		fsi->NumberOfLinks = zp->z_links;
		fsi->EffectiveAccess = GENERIC_ALL;
	}

	return (STATUS_SUCCESS);
}

// Convert ZFS (Unix) mode to Windows mode.
ULONG
ZMODE2WMODE(mode_t z)
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
	return (w);
}

NTSTATUS
file_stat_lx_information(PDEVICE_OBJECT DeviceObject, PIRP Irp,
    PIO_STACK_LOCATION IrpSp, FILE_STAT_LX_INFORMATION *fsli)
{
	PFILE_OBJECT FileObject = IrpSp->FileObject;

	dprintf("   %s\n", __func__);

	/* vp is already help in query_information */
	struct vnode *vp = FileObject->FsContext;

	if (vp) {
		znode_t *zp = VTOZ(vp);
		zfsvfs_t *zfsvfs = zp->z_zfsvfs;
		if (zp->z_is_sa) {
			sa_bulk_attr_t bulk[3];
			int count = 0;
			uint64_t mtime[2];
			uint64_t ctime[2];
			uint64_t crtime[2];
			SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_MTIME(zfsvfs),
			    NULL, &mtime, 16);
			SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_CTIME(zfsvfs),
			    NULL, &ctime, 16);
			SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_CRTIME(zfsvfs),
			    NULL, &crtime, 16);
			sa_bulk_lookup(zp->z_sa_hdl, bulk, count);

			TIME_UNIX_TO_WINDOWS(crtime,
			    fsli->CreationTime.QuadPart);
			TIME_UNIX_TO_WINDOWS(zp->z_atime,
			    fsli->LastAccessTime.QuadPart);
			TIME_UNIX_TO_WINDOWS(mtime,
			    fsli->LastWriteTime.QuadPart);
			TIME_UNIX_TO_WINDOWS(ctime,
			    fsli->ChangeTime.QuadPart);
		}
		fsli->FileId.QuadPart = zp->z_id;
		fsli->AllocationSize.QuadPart =
		    P2ROUNDUP(zp->z_size, zfs_blksz(zp));
		fsli->EndOfFile.QuadPart = zp->z_size;
		fsli->FileAttributes = zfs_getwinflags(zp);
		fsli->ReparseTag = get_reparse_tag(zp);
		fsli->NumberOfLinks = zp->z_links;
		fsli->EffectiveAccess =
		    SPECIFIC_RIGHTS_ALL | ACCESS_SYSTEM_SECURITY;
		fsli->LxFlags = LX_FILE_METADATA_HAS_UID |
		    LX_FILE_METADATA_HAS_GID | LX_FILE_METADATA_HAS_MODE;
		if (zfsvfs->z_case == ZFS_CASE_SENSITIVE)
			fsli->LxFlags |= LX_FILE_CASE_SENSITIVE_DIR;
		fsli->LxUid = zp->z_uid;
		fsli->LxGid = zp->z_gid;
		fsli->LxMode = ZMODE2WMODE(zp->z_mode);
		fsli->LxDeviceIdMajor = 0;
		fsli->LxDeviceIdMinor = 0;
	}
	return (STATUS_SUCCESS);
}

//
// If overflow, set Information to input_size and NameLength to required size.
//
NTSTATUS
file_name_information(PDEVICE_OBJECT DeviceObject, PIRP Irp,
    PIO_STACK_LOCATION IrpSp, FILE_NAME_INFORMATION *name,
    PULONG usedspace, int normalize)
{
	PFILE_OBJECT FileObject = IrpSp->FileObject;

	dprintf("* %s: (normalize %d)\n", __func__, normalize);

	if (FileObject == NULL || FileObject->FsContext == NULL)
		return (STATUS_INVALID_PARAMETER);

	if (IrpSp->Parameters.QueryFile.Length <
	    (ULONG)FIELD_OFFSET(FILE_NAME_INFORMATION, FileName[0])) {
		Irp->IoStatus.Information = sizeof (FILE_NAME_INFORMATION);
		return (STATUS_BUFFER_TOO_SMALL);
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
			if (zfs_build_path(zp, NULL, &zp->z_name_cache,
			    &zp->z_name_len, &zp->z_name_offset) == -1) {
				dprintf("%s: failed to build fullpath\n",
				    __func__);
				// VN_RELE(vp);
				// return (STATUS_OBJECT_PATH_NOT_FOUND);
			}
		}

		// Safety
		if (zp->z_name_cache != NULL) {
#if 0
			// Just name
			strlcpy(strname, &zp->z_name_cache[zp->z_name_offset],
			    MAXPATHLEN);
#else
			// Full path name
			strlcpy(strname, zp->z_name_cache,
			    MAXPATHLEN);
#endif
			// If it is a DIR, make sure it ends with "\",
			// except for root, that is just "\"
			if (S_ISDIR(zp->z_mode))
				strlcat(strname, "\\",
				    MAXPATHLEN);
		}
	}
	VN_RELE(vp);

	// Convert name, setting FileNameLength to how much we need
	error = RtlUTF8ToUnicodeN(NULL, 0, &name->FileNameLength,
	    strname, strlen(strname));

	dprintf("%s: remaining space %d str.len %d struct size %d\n",
	    __func__, IrpSp->Parameters.QueryFile.Length,
	    name->FileNameLength, sizeof (FILE_NAME_INFORMATION));
	// CHECK ERROR here.
	// Calculate how much room there is for filename, after
	// the struct and its first wchar
	int space = IrpSp->Parameters.QueryFile.Length -
	    FIELD_OFFSET(FILE_NAME_INFORMATION, FileName);
	space = MIN(space, name->FileNameLength);

	ASSERT(space >= 0);

	// Copy over as much as we can, including the first wchar
	error = RtlUTF8ToUnicodeN(name->FileName,
	    space /* + sizeof (name->FileName) */,
	    NULL, strname, strlen(strname));

	if (space < name->FileNameLength)
		Status = STATUS_BUFFER_OVERFLOW;
	else
		Status = STATUS_SUCCESS;

	// name->FileNameLength holds how much is actually there
	// and usedspace how much we needed to have

	// Would this go one byte too far?
	// name->FileName[name->FileNameLength / sizeof (name->FileName)] = 0;

	// Return how much of the filename we copied after the first wchar
	// which is used with sizeof (struct) to work out how much
	// bigger the return is.
	if (usedspace) *usedspace = space;
	// Space will always be 2 or more, since struct has room for 1 wchar

	dprintf("* %s: %s name of '%.*S' struct size 0x%x and "
	    "FileNameLength 0x%x Usedspace 0x%x\n", __func__,
	    Status == STATUS_BUFFER_OVERFLOW ? "partial" : "",
	    space / 2, name->FileName,
	    sizeof (FILE_NAME_INFORMATION), name->FileNameLength, space);

	return (Status);
}

// This function is not used - left in as example. If you think
// something is not working due to missing FileRemoteProtocolInformation
// then think again. This is not the problem.
NTSTATUS
file_remote_protocol_information(PDEVICE_OBJECT DeviceObject, PIRP Irp,
    PIO_STACK_LOCATION IrpSp, FILE_REMOTE_PROTOCOL_INFORMATION *frpi)
{
	dprintf("   %s\n", __func__);

	if (IrpSp->Parameters.QueryFile.Length <
	    sizeof (FILE_REMOTE_PROTOCOL_INFORMATION)) {
		Irp->IoStatus.Information =
		    sizeof (FILE_REMOTE_PROTOCOL_INFORMATION);
		return (STATUS_BUFFER_TOO_SMALL);
	}

	frpi->StructureVersion = 4;
	frpi->StructureSize = sizeof (FILE_REMOTE_PROTOCOL_INFORMATION);
	frpi->Protocol = WNNC_NET_GOOGLE;
	frpi->ProtocolMajorVersion = 1;
	frpi->ProtocolMinorVersion = 0;
	frpi->ProtocolRevision = 3;
	frpi->Flags = REMOTE_PROTOCOL_FLAG_LOOPBACK;
	Irp->IoStatus.Information = sizeof (FILE_REMOTE_PROTOCOL_INFORMATION);
	return (STATUS_SUCCESS);
}

// Insert a streamname into an output buffer, if there is room,
// StreamNameLength is always the FULL name length, even when we only
// fit partial.
// Return 0 for OK, 1 for overflow.
// ADS are returned as ":Zone.Identifier:$DATA".
// EAs are returned as "Zone.Identifier".
// OK, this should only return Streams, but keeping
// the EA code around in case..
int
zfswin_insert_streamname(char *streamname, uint8_t *outbuffer,
    FILE_STREAM_INFORMATION **previous_stream, uint64_t availablebytes,
    uint64_t *spaceused, uint64_t streamsize)
{
	/*
	 *	 typedef struct _FILE_STREAM_INFO {
	 *	  DWORD         NextEntryOffset;
	 *	  DWORD         StreamNameLength;
	 *	  LARGE_INTEGER StreamSize;
	 *	  LARGE_INTEGER StreamAllocationSize;
	 *	  WCHAR         StreamName[1];
	 * } FILE_STREAM_INFO, *PFILE_STREAM_INFO;
	 */
	// The first stream struct we assume is already aligned,
	// but further ones should be padded here.
	FILE_STREAM_INFORMATION *stream = NULL;
	int overflow = 0;
	boolean_t isADS = B_FALSE;

	int len = strlen(streamname);
	if ((toupper(streamname[len - 6]) == ':') &&
	    (toupper(streamname[len - 5]) == '$') &&
	    (toupper(streamname[len - 4]) == 'D') &&
	    (toupper(streamname[len - 3]) == 'A') &&
	    (toupper(streamname[len - 2]) == 'T') &&
	    (toupper(streamname[len - 1]) == 'A'))
		isADS = B_TRUE;

	// If not first struct, align outsize to 8 byte - 0 aligns to 0.
	*spaceused = (((*spaceused) + 7) & ~7);

	// Convert filename, to get space required.
	ULONG needed_streamnamelen;
	int error;

	// Check error? Do we care about convertion errors?
	error = RtlUTF8ToUnicodeN(NULL, 0, &needed_streamnamelen,
	    streamname, strlen(streamname));

	// Is there room? We have to add the struct if there is room for it
	// and fill it out as much as possible, and copy in as much of the name
	// as we can.

	if (*spaceused + sizeof (FILE_STREAM_INFORMATION) <= availablebytes) {

		stream = (FILE_STREAM_INFORMATION *)&outbuffer[*spaceused];

		// Room for one more struct, update previous' next ptr
		if (*previous_stream != NULL) {
			// Update previous structure to point to this one.
			// It is not offset from the buffer, but offset from
			// last "stream" struct.
			// **lastNextEntryOffset = (DWORD)*spaceused;
			(*previous_stream)->NextEntryOffset =
			    (uintptr_t)stream - (uintptr_t)(*previous_stream);
		}


		// Directly set next to 0, assuming this will be last record
		stream->NextEntryOffset = 0;

		// remember this struct's NextEntry, so the next one can
		// fill it in.
		*previous_stream = stream;

		// Set all the fields now
		stream->StreamSize.QuadPart = streamsize;
		stream->StreamAllocationSize.QuadPart =
		    P2ROUNDUP(streamsize, 512);

		// Return the total name length, "needed" is in bytes,
		// so add 2 to fit the ":"
		stream->StreamNameLength =
		    needed_streamnamelen;
		if (isADS) // + ":"
			stream->StreamNameLength += sizeof (WCHAR);

		// Consume the space of the struct
		*spaceused += FIELD_OFFSET(FILE_STREAM_INFORMATION, StreamName);

		uint64_t roomforname;
		if (*spaceused + stream->StreamNameLength <= availablebytes) {
			roomforname = stream->StreamNameLength;
		} else {
			roomforname = availablebytes - *spaceused;
			overflow = 1;
		}

		// Consume the space of (partial?) filename
		*spaceused += roomforname;

		// Now copy out as much of the filename as can fit.
		// We need to real full length in StreamNameLength
		// There is always room for 1 char
		PWSTR out = &stream->StreamName[0];

		if (isADS) {
			*out = L':';
			out++;
			roomforname -= sizeof (WCHAR);
		}

		// Convert as much as we can, accounting for the start ":"
		error = RtlUTF8ToUnicodeN(out, roomforname,
		    NULL, streamname, strlen(streamname));

		dprintf("%s: added %s streamname '%s'\n", __func__,
		    overflow ? "(partial)" : "", streamname);
	} else {
		dprintf("%s: no room for  '%s'\n", __func__, streamname);
		overflow = 1;
	}

	return (overflow);
}

//
// If overflow, set Information to input_size and NameLength to required size.
//
NTSTATUS
file_stream_information(PDEVICE_OBJECT DeviceObject, PIRP Irp,
    PIO_STACK_LOCATION IrpSp, FILE_STREAM_INFORMATION *stream)
{
	PFILE_OBJECT FileObject = IrpSp->FileObject;
	NTSTATUS Status;
	void *outbuffer = Irp->AssociatedIrp.SystemBuffer;
	uint64_t availablebytes = IrpSp->Parameters.QueryFile.Length;
	FILE_STREAM_INFORMATION *previous_stream = NULL;
	int overflow = 0;
	int error = 0;

	dprintf("%s: \n", __func__);

	if (FileObject == NULL || FileObject->FsContext == NULL)
		return (STATUS_INVALID_PARAMETER);

	if (IrpSp->Parameters.QueryFile.Length <
	    sizeof (FILE_STREAM_INFORMATION)) {
		Irp->IoStatus.Information = sizeof (FILE_STREAM_INFORMATION);
		return (STATUS_BUFFER_TOO_SMALL);
	}

	struct vnode *vp = FileObject->FsContext;
	zfs_dirlist_t *zccb = FileObject->FsContext2;
	znode_t *zp = VTOZ(vp), *xzp = NULL;
	znode_t *xdzp = NULL;
	zfsvfs_t *zfsvfs = zp->z_zfsvfs;

	// This exits when unmounting
	if ((error = zfs_enter(zfsvfs, FTAG)) != 0)
		return (error);

	struct vnode *xdvp = NULL;
	void *cr = NULL;
	uint64_t spaceused = 0;
	zap_cursor_t  zc;
	objset_t  *os;
	zap_attribute_t  za;

	// Iterate the xattrs.
	// Windows can call this on a stream zp, in this case, we
	// need to go find the real parent, and iterate on that.
	if (zccb && zp->z_pflags & ZFS_XATTR) {

		error = zfs_zget(zfsvfs, zccb->real_file_id, &zp);
		if (error)
			goto out;
	} else {
		VN_HOLD(vp);
	}

	// Add a record for this name, if there is room. Keep a
	// count of how much space would need.
	// insert_xattrname adds first ":" and ":$DATA"
	if (vnode_isdir(vp))
		overflow = zfswin_insert_streamname("", outbuffer,
		    &previous_stream, availablebytes, &spaceused,
		    vnode_isdir(vp) ? 0 : zp->z_size);
	else
		overflow = zfswin_insert_streamname(":$DATA", outbuffer,
		    &previous_stream, availablebytes, &spaceused,
		    vnode_isdir(vp) ? 0 : zp->z_size);

	/* Grab the hidden attribute directory vnode. */
	if (zfs_get_xattrdir(zp, &xdzp, cr, 0) != 0) {
		goto out;
	}

	xdvp = ZTOV(xdzp);
	os = zfsvfs->z_os;

	stream = (FILE_STREAM_INFORMATION *)outbuffer;

	for (zap_cursor_init(&zc, os, VTOZ(xdvp)->z_id);
	    zap_cursor_retrieve(&zc, &za) == 0; zap_cursor_advance(&zc)) {

		if (!xattr_stream(za.za_name))
			continue;	 /* skip */

		// We need to lookup the size of the xattr.
		int error = zfs_dirlook(VTOZ(xdvp), za.za_name, &xzp, 0,
		    NULL, NULL);

		overflow += zfswin_insert_streamname(za.za_name, outbuffer,
		    &previous_stream, availablebytes, &spaceused,
		    xzp ? xzp->z_size : 0);

		if (error == 0)
			zrele(xzp);

	}

	zap_cursor_fini(&zc);

out:
	if (xdvp) {
		VN_RELE(xdvp);
	}

	zrele(zp);

	zfs_exit(zfsvfs, FTAG);

	if (overflow > 0)
		Status = STATUS_BUFFER_OVERFLOW;
	else
		Status = STATUS_SUCCESS;

	// Set to how space we used.
	Irp->IoStatus.Information = spaceused;

	return (Status);
}

NTSTATUS
file_hard_link_information(PDEVICE_OBJECT DeviceObject, PIRP Irp,
    PIO_STACK_LOCATION IrpSp, FILE_LINKS_INFORMATION *fhli)
{
	PFILE_OBJECT FileObject = IrpSp->FileObject;
	NTSTATUS Status;
	uint64_t availablebytes = IrpSp->Parameters.QueryFile.Length;

	dprintf("%s: \n", __func__);

	if (FileObject == NULL || FileObject->FsContext == NULL)
		return (STATUS_INVALID_PARAMETER);

	if (IrpSp->Parameters.QueryFile.Length <
	    sizeof (FILE_LINKS_INFORMATION)) {
		Irp->IoStatus.Information = sizeof (FILE_LINKS_INFORMATION);
		return (STATUS_BUFFER_TOO_SMALL);
	}

	struct vnode *vp = FileObject->FsContext;
	zfs_dirlist_t *zccb = FileObject->FsContext2;

	fhli->EntriesReturned = 0;
	fhli->BytesNeeded = sizeof (FILE_LINKS_INFORMATION);

	Irp->IoStatus.Information = sizeof (FILE_LINKS_INFORMATION);

	return (STATUS_SUCCESS);
}

/* IRP_MJ_DEVICE_CONTROL helpers */

NTSTATUS
QueryCapabilities(PDEVICE_OBJECT DeviceObject, PIRP Irp,
    PIO_STACK_LOCATION IrpSp)
{
	NTSTATUS				Status;
	PDEVICE_CAPABILITIES	DeviceCapabilities;
	DeviceCapabilities = IrpSp->Parameters.DeviceCapabilities.Capabilities;
	DeviceCapabilities->SurpriseRemovalOK = TRUE;
	DeviceCapabilities->LockSupported = TRUE;
	DeviceCapabilities->EjectSupported = TRUE;
	DeviceCapabilities->Removable = FALSE; // XX
	DeviceCapabilities->DockDevice = FALSE;
	DeviceCapabilities->D1Latency =
	    DeviceCapabilities->D2Latency =
	    DeviceCapabilities->D3Latency = 0;
	DeviceCapabilities->NoDisplayInUI = 0;
	Irp->IoStatus.Information = sizeof (DEVICE_CAPABILITIES);

	return (STATUS_SUCCESS);
}

NTSTATUS
ioctl_get_gpt_attributes(PDEVICE_OBJECT DeviceObject, PIRP Irp,
    PIO_STACK_LOCATION IrpSp)
{
	mount_t *zmo;
	NTSTATUS Status;
	VOLUME_GET_GPT_ATTRIBUTES_INFORMATION *vggai;

	if (IrpSp->Parameters.DeviceIoControl.OutputBufferLength <
	    sizeof (VOLUME_GET_GPT_ATTRIBUTES_INFORMATION)) {
		Irp->IoStatus.Information =
		    sizeof (VOLUME_GET_GPT_ATTRIBUTES_INFORMATION);
		return (STATUS_BUFFER_TOO_SMALL);
	}

	zmo = (mount_t *)DeviceObject->DeviceExtension;
	vggai = (VOLUME_GET_GPT_ATTRIBUTES_INFORMATION *)
	    Irp->AssociatedIrp.SystemBuffer;

	if (!zmo ||
	    (zmo->type != MOUNT_TYPE_VCB &&
	    zmo->type != MOUNT_TYPE_DCB)) {
		return (STATUS_INVALID_PARAMETER);
	}

	zfsvfs_t *zfsvfs = vfs_fsprivate(zmo);
	if (zfsvfs == NULL)
		return (STATUS_INVALID_PARAMETER);

	Irp->IoStatus.Information =
	    sizeof (VOLUME_GET_GPT_ATTRIBUTES_INFORMATION);

	if (zfsvfs->z_rdonly)
		vggai->GptAttributes =
		    GPT_BASIC_DATA_ATTRIBUTE_READ_ONLY;
	else
		vggai->GptAttributes = 0;

	return (STATUS_SUCCESS);
}

//
// If overflow, set Information to sizeof (MOUNTDEV_NAME), and
// NameLength to required size.
//
NTSTATUS
ioctl_query_device_name(PDEVICE_OBJECT DeviceObject, PIRP Irp,
    PIO_STACK_LOCATION IrpSp)
{
	// Return name in MOUNTDEV_NAME
	PMOUNTDEV_NAME name;
	mount_t *zmo;
	NTSTATUS Status;

	if (IrpSp->Parameters.DeviceIoControl.OutputBufferLength <
	    sizeof (MOUNTDEV_NAME)) {
		Irp->IoStatus.Information = sizeof (MOUNTDEV_NAME);
		return (STATUS_BUFFER_TOO_SMALL);
	}

	zmo = (mount_t *)DeviceObject->DeviceExtension;

	/* If given a file, it must be root */
	if (IrpSp->FileObject != NULL && IrpSp->FileObject->FsContext != NULL) {
		struct vnode *vp = IrpSp->FileObject->FsContext;
		if (vp != NULL) {
			znode_t *zp = VTOZ(vp);
			if (zp != NULL) {
				if (zp->z_id != zp->z_zfsvfs->z_root) {
					dprintf("%s on file which isn't root\n",
					    __func__);
					return (STATUS_INVALID_PARAMETER);
				}
			}
		}
	}

	name = Irp->AssociatedIrp.SystemBuffer;

	int space = IrpSp->Parameters.DeviceIoControl.OutputBufferLength -
	    sizeof (MOUNTDEV_NAME);
#if 1
	space = MIN(space, zmo->device_name.Length);
	name->NameLength = zmo->device_name.Length;
	RtlCopyMemory(name->Name, zmo->device_name.Buffer,
	    space + sizeof (name->Name));
	Irp->IoStatus.Information = sizeof (MOUNTDEV_NAME) + space;

	if (space < zmo->device_name.Length - sizeof (name->Name))
		Status = STATUS_BUFFER_OVERFLOW;
	else
		Status = STATUS_SUCCESS;
#else
	if (zmo->parent_device != NULL) {
		DeviceObject = zmo->parent_device;
		zmo = (mount_t *)DeviceObject->DeviceExtension;
	}

	space = MIN(space, zmo->device_name.Length);
	name->NameLength = zmo->device_name.Length;
	RtlCopyMemory(name->Name, zmo->device_name.Buffer,
	    space + sizeof (name->Name));
	Irp->IoStatus.Information = sizeof (MOUNTDEV_NAME) + space;

	if (space < zmo->device_name.Length - sizeof (name->Name))
		Status = STATUS_BUFFER_OVERFLOW;
	else
		Status = STATUS_SUCCESS;
#endif

	ASSERT(Irp->IoStatus.Information <=
	    IrpSp->Parameters.DeviceIoControl.OutputBufferLength);

	dprintf("replying with '%.*S'\n",
	    space + sizeof (name->Name) / sizeof (WCHAR), name->Name);

	return (Status);
}

NTSTATUS
ioctl_disk_get_drive_geometry(PDEVICE_OBJECT DeviceObject, PIRP Irp,
    PIO_STACK_LOCATION IrpSp)
{
	int error = 0;
	dprintf("%s: \n", __func__);
	if (IrpSp->Parameters.DeviceIoControl.OutputBufferLength <
	    sizeof (DISK_GEOMETRY)) {
		Irp->IoStatus.Information = sizeof (DISK_GEOMETRY);
		return (STATUS_BUFFER_TOO_SMALL);
	}

	mount_t *zmo = DeviceObject->DeviceExtension;
	if (!zmo ||
	    (zmo->type != MOUNT_TYPE_VCB &&
	    zmo->type != MOUNT_TYPE_DCB)) {
		return (STATUS_INVALID_PARAMETER);
	}

	zfsvfs_t *zfsvfs = vfs_fsprivate(zmo);
	if (zfsvfs == NULL)
		return (STATUS_INVALID_PARAMETER);

	if ((error = zfs_enter(zfsvfs, FTAG)) != 0)
		return (error);  // This returns EIO if fail

	uint64_t refdbytes, availbytes, usedobjs, availobjs;
	dmu_objset_space(zfsvfs->z_os,
	    &refdbytes, &availbytes, &usedobjs, &availobjs);

	DISK_GEOMETRY *geom = Irp->AssociatedIrp.SystemBuffer;

	geom->BytesPerSector = 512;
	geom->SectorsPerTrack = 1;
	geom->TracksPerCylinder = 1;
	geom->Cylinders.QuadPart = (availbytes + refdbytes) / 512;
	geom->MediaType = FixedMedia;
	zfs_exit(zfsvfs, FTAG);

	Irp->IoStatus.Information = sizeof (DISK_GEOMETRY);
	return (STATUS_SUCCESS);
}

// This is how Windows Samples handle it
typedef struct _DISK_GEOMETRY_EX_INTERNAL {
	DISK_GEOMETRY Geometry;
	LARGE_INTEGER DiskSize;
	DISK_PARTITION_INFO Partition;
	DISK_DETECTION_INFO Detection;
} DISK_GEOMETRY_EX_INTERNAL, *PDISK_GEOMETRY_EX_INTERNAL;

NTSTATUS
ioctl_disk_get_drive_geometry_ex(PDEVICE_OBJECT DeviceObject, PIRP Irp,
    PIO_STACK_LOCATION IrpSp)
{
	int error = 0;
	dprintf("%s: \n", __func__);
	if (IrpSp->Parameters.DeviceIoControl.OutputBufferLength <
	    FIELD_OFFSET(DISK_GEOMETRY_EX, Data)) {
		Irp->IoStatus.Information = sizeof (DISK_GEOMETRY_EX);
		return (STATUS_BUFFER_TOO_SMALL);
	}

	mount_t *zmo = DeviceObject->DeviceExtension;
	if (!zmo ||
	    (zmo->type != MOUNT_TYPE_VCB &&
	    zmo->type != MOUNT_TYPE_DCB)) {
		return (STATUS_INVALID_PARAMETER);
	}

	zfsvfs_t *zfsvfs = vfs_fsprivate(zmo);
	if (zfsvfs == NULL)
		return (STATUS_INVALID_PARAMETER);

	if ((error = zfs_enter(zfsvfs, FTAG)) != 0)
		return (error);  // This returns EIO if fail

	uint64_t refdbytes, availbytes, usedobjs, availobjs;
	dmu_objset_space(zfsvfs->z_os,
	    &refdbytes, &availbytes, &usedobjs, &availobjs);


	DISK_GEOMETRY_EX_INTERNAL *geom = Irp->AssociatedIrp.SystemBuffer;
	geom->DiskSize.QuadPart = availbytes + refdbytes;
	geom->Geometry.BytesPerSector = 512;
	geom->Geometry.MediaType = FixedMedia;

	if (IrpSp->Parameters.DeviceIoControl.OutputBufferLength >=
	    FIELD_OFFSET(DISK_GEOMETRY_EX_INTERNAL, Detection)) {
		geom->Partition.SizeOfPartitionInfo = sizeof (geom->Partition);
		geom->Partition.PartitionStyle = PARTITION_STYLE_GPT;
		// geom->Partition.Gpt.DiskId = 0;
	}
	if (IrpSp->Parameters.DeviceIoControl.OutputBufferLength >=
	    sizeof (DISK_GEOMETRY_EX_INTERNAL)) {
		geom->Detection.SizeOfDetectInfo = sizeof (geom->Detection);
	}
	zfs_exit(zfsvfs, FTAG);

	Irp->IoStatus.Information =
	    MIN(IrpSp->Parameters.DeviceIoControl.OutputBufferLength,
	    sizeof (DISK_GEOMETRY_EX_INTERNAL));
	return (STATUS_SUCCESS);
}

NTSTATUS
ioctl_disk_get_partition_info(PDEVICE_OBJECT DeviceObject, PIRP Irp,
    PIO_STACK_LOCATION IrpSp)
{
	int error = 0;
	dprintf("%s: \n", __func__);

	if (IrpSp->Parameters.DeviceIoControl.OutputBufferLength <
	    sizeof (PARTITION_INFORMATION)) {
		Irp->IoStatus.Information = sizeof (PARTITION_INFORMATION);
		return (STATUS_BUFFER_TOO_SMALL);
	}

	mount_t *zmo = DeviceObject->DeviceExtension;
	if (!zmo ||
	    (zmo->type != MOUNT_TYPE_VCB &&
	    zmo->type != MOUNT_TYPE_DCB)) {
		return (STATUS_INVALID_PARAMETER);
	}

	zfsvfs_t *zfsvfs = vfs_fsprivate(zmo);
	if (zfsvfs == NULL)
		return (STATUS_INVALID_PARAMETER);

	if ((error = zfs_enter(zfsvfs, FTAG)) != 0)
		return (error);  // This returns EIO if fail

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

	zfs_exit(zfsvfs, FTAG);

	Irp->IoStatus.Information = sizeof (PARTITION_INFORMATION);

	return (STATUS_SUCCESS);
}

NTSTATUS
ioctl_disk_get_partition_info_ex(PDEVICE_OBJECT DeviceObject, PIRP Irp,
    PIO_STACK_LOCATION IrpSp)
{
	int error = 0;
	dprintf("%s: \n", __func__);

	if (IrpSp->Parameters.DeviceIoControl.OutputBufferLength <
	    sizeof (PARTITION_INFORMATION_EX)) {
		Irp->IoStatus.Information = sizeof (PARTITION_INFORMATION_EX);
		return (STATUS_BUFFER_TOO_SMALL);
	}

	mount_t *zmo = DeviceObject->DeviceExtension;
	if (!zmo ||
	    (zmo->type != MOUNT_TYPE_VCB &&
	    zmo->type != MOUNT_TYPE_DCB)) {
		return (STATUS_INVALID_PARAMETER);
	}

	zfsvfs_t *zfsvfs = vfs_fsprivate(zmo);
	if (zfsvfs == NULL)
		return (STATUS_INVALID_PARAMETER);

	if ((error = zfs_enter(zfsvfs, FTAG)) != 0)
		return (error);  // This returns EIO if fail

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

	zfs_exit(zfsvfs, FTAG);

	Irp->IoStatus.Information = sizeof (PARTITION_INFORMATION_EX);

	return (STATUS_SUCCESS);
}

NTSTATUS
ioctl_disk_get_length_info(PDEVICE_OBJECT DeviceObject, PIRP Irp,
    PIO_STACK_LOCATION IrpSp)
{
	int error = 0;
	dprintf("%s: \n", __func__);

	if (IrpSp->Parameters.DeviceIoControl.OutputBufferLength <
	    sizeof (GET_LENGTH_INFORMATION)) {
		Irp->IoStatus.Information = sizeof (GET_LENGTH_INFORMATION);
		return (STATUS_BUFFER_TOO_SMALL);
	}

	mount_t *zmo = DeviceObject->DeviceExtension;
	if (!zmo ||
	    (zmo->type != MOUNT_TYPE_VCB &&
	    zmo->type != MOUNT_TYPE_DCB)) {
		return (STATUS_INVALID_PARAMETER);
	}

	zfsvfs_t *zfsvfs = vfs_fsprivate(zmo);
	if (zfsvfs == NULL)
		return (STATUS_INVALID_PARAMETER);

	if ((error = zfs_enter(zfsvfs, FTAG)) != 0)
		return (error);  // This returns EIO if fail

	uint64_t refdbytes, availbytes, usedobjs, availobjs;
	dmu_objset_space(zfsvfs->z_os,
	    &refdbytes, &availbytes, &usedobjs, &availobjs);

	GET_LENGTH_INFORMATION *gli = Irp->AssociatedIrp.SystemBuffer;
	gli->Length.QuadPart = availbytes + refdbytes;

	zfs_exit(zfsvfs, FTAG);

	Irp->IoStatus.Information = sizeof (GET_LENGTH_INFORMATION);

	return (STATUS_SUCCESS);
}

NTSTATUS
ioctl_volume_is_io_capable(PDEVICE_OBJECT DeviceObject, PIRP Irp,
    PIO_STACK_LOCATION IrpSp)
{
	dprintf("%s: \n", __func__);
	return (STATUS_SUCCESS);
}

NTSTATUS
ioctl_storage_get_hotplug_info(PDEVICE_OBJECT DeviceObject, PIRP Irp,
    PIO_STACK_LOCATION IrpSp)
{
	dprintf("%s: \n", __func__);

	if (IrpSp->Parameters.DeviceIoControl.OutputBufferLength <
	    sizeof (STORAGE_HOTPLUG_INFO)) {
		Irp->IoStatus.Information = sizeof (STORAGE_HOTPLUG_INFO);
		return (STATUS_BUFFER_TOO_SMALL);
	}

	STORAGE_HOTPLUG_INFO *hot = Irp->AssociatedIrp.SystemBuffer;
	hot->Size = sizeof (STORAGE_HOTPLUG_INFO);
	hot->MediaRemovable = FALSE; // XX
	hot->DeviceHotplug = TRUE;
	hot->MediaHotplug = FALSE;
	hot->WriteCacheEnableOverride = FALSE;

	Irp->IoStatus.Information = sizeof (STORAGE_HOTPLUG_INFO);
	return (STATUS_SUCCESS);
}

NTSTATUS
ioctl_storage_query_property(PDEVICE_OBJECT DeviceObject, PIRP Irp,
    PIO_STACK_LOCATION IrpSp)
{
	NTSTATUS status;
	ULONG outputLength;

	dprintf("%s: \n", __func__);

	outputLength = IrpSp->Parameters.DeviceIoControl.OutputBufferLength;
	if (outputLength < sizeof (STORAGE_PROPERTY_QUERY)) {
		Irp->IoStatus.Information = sizeof (STORAGE_PROPERTY_QUERY);
		return (STATUS_BUFFER_TOO_SMALL);
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
			Irp->IoStatus.Information =
			    sizeof (STORAGE_DEVICE_DESCRIPTOR);
			if (outputLength < sizeof (STORAGE_DEVICE_DESCRIPTOR)) {
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
			Irp->IoStatus.Information =
			    sizeof (STORAGE_DEVICE_ATTRIBUTES_DESCRIPTOR);
			if (outputLength <
			    sizeof (STORAGE_DEVICE_ATTRIBUTES_DESCRIPTOR)) {
				status = STATUS_BUFFER_TOO_SMALL;
				break;
			}
			STORAGE_DEVICE_ATTRIBUTES_DESCRIPTOR *sdad;
			sdad = Irp->AssociatedIrp.SystemBuffer;
			sdad->Version = 1;
			sdad->Size =
			    sizeof (STORAGE_DEVICE_ATTRIBUTES_DESCRIPTOR);
			sdad->Attributes =
			    STORAGE_ATTRIBUTE_BYTE_ADDRESSABLE_IO;
			status = STATUS_SUCCESS;
			break;
		default:
			dprintf("    PropertyStandardQuery unknown 0x%x\n",
			    spq->PropertyId);
			status = STATUS_NOT_IMPLEMENTED;
			break;
		} // switch propertyId
		break;

	default:
		dprintf("%s: unknown Querytype: 0x%x\n",
		    __func__, spq->QueryType);
		status = STATUS_NOT_IMPLEMENTED;
		break;
	}

	Irp->IoStatus.Information = sizeof (STORAGE_PROPERTY_QUERY);
	return (status);
}

// Query Unique id uses 1 byte chars.
// If overflow, set Information to sizeof (MOUNTDEV_UNIQUE_ID),
// and NameLength to required size.
//
NTSTATUS
ioctl_query_unique_id(PDEVICE_OBJECT DeviceObject, PIRP Irp,
    PIO_STACK_LOCATION IrpSp)
{
	PMOUNTDEV_UNIQUE_ID uniqueId;
	ULONG bufferLength =
	    IrpSp->Parameters.DeviceIoControl.OutputBufferLength;
	mount_t *zmo;
	char osname[MAXNAMELEN];
	ULONG len;

	dprintf("%s: \n", __func__);

	zmo = (mount_t *)DeviceObject->DeviceExtension;

	if (bufferLength < sizeof (MOUNTDEV_UNIQUE_ID)) {
		Irp->IoStatus.Information = sizeof (MOUNTDEV_UNIQUE_ID);
		return (STATUS_BUFFER_TOO_SMALL);
	}

	RtlUnicodeToUTF8N(osname, MAXPATHLEN, &len, zmo->name.Buffer,
	    zmo->name.Length);
	osname[len] = 0;

	// uniqueId appears to be CHARS not WCHARS,
	// so this might need correcting?
	uniqueId = (PMOUNTDEV_UNIQUE_ID)Irp->AssociatedIrp.SystemBuffer;

	uniqueId->UniqueIdLength = strlen(osname);

	if (sizeof (USHORT) + uniqueId->UniqueIdLength <= bufferLength) {
		RtlCopyMemory((PCHAR)uniqueId->UniqueId, osname,
		    uniqueId->UniqueIdLength);
		Irp->IoStatus.Information =
		    FIELD_OFFSET(MOUNTDEV_UNIQUE_ID, UniqueId[0]) +
		    uniqueId->UniqueIdLength;
		dprintf("replying with '%.*s'\n",
		    uniqueId->UniqueIdLength, uniqueId->UniqueId);
		return (STATUS_SUCCESS);
	} else {
		Irp->IoStatus.Information = sizeof (MOUNTDEV_UNIQUE_ID);
		return (STATUS_BUFFER_OVERFLOW);
	}
}

NTSTATUS
ioctl_query_stable_guid(PDEVICE_OBJECT DeviceObject, PIRP Irp,
    PIO_STACK_LOCATION IrpSp)
{
	PMOUNTDEV_STABLE_GUID mountGuid;
	ULONG bufferLength =
	    IrpSp->Parameters.DeviceIoControl.OutputBufferLength;
	mount_t *zmo;

	dprintf("%s: \n", __func__);

	zmo = (mount_t *)DeviceObject->DeviceExtension;

	if (bufferLength < sizeof (MOUNTDEV_STABLE_GUID)) {
		Irp->IoStatus.Information = sizeof (MOUNTDEV_STABLE_GUID);
		return (STATUS_BUFFER_TOO_SMALL);
	}

	mountGuid = (PMOUNTDEV_STABLE_GUID)Irp->AssociatedIrp.SystemBuffer;
	RtlZeroMemory(&mountGuid->StableGuid, sizeof (mountGuid->StableGuid));
	zfsvfs_t *zfsvfs = vfs_fsprivate(zmo);
	if (zfsvfs) {
		uint64_t guid = dmu_objset_fsid_guid(zfsvfs->z_os);
		RtlCopyMemory(&mountGuid->StableGuid, &guid, sizeof (guid));
		Irp->IoStatus.Information = sizeof (MOUNTDEV_STABLE_GUID);
		return (STATUS_SUCCESS);
	}
	return (STATUS_NOT_FOUND);
}

NTSTATUS
ioctl_mountdev_query_suggested_link_name(PDEVICE_OBJECT DeviceObject,
    PIRP Irp, PIO_STACK_LOCATION IrpSp)
{
	MOUNTDEV_SUGGESTED_LINK_NAME *linkName;
	ULONG bufferLength =
	    IrpSp->Parameters.DeviceIoControl.OutputBufferLength;
	//	UNICODE_STRING MountPoint;
	mount_t *zmo = (mount_t *)DeviceObject->DeviceExtension;

	dprintf("%s: \n", __func__);

	if (bufferLength < sizeof (MOUNTDEV_SUGGESTED_LINK_NAME)) {
		Irp->IoStatus.Information =
		    sizeof (MOUNTDEV_SUGGESTED_LINK_NAME);
		return (STATUS_BUFFER_TOO_SMALL);
	}

	// We only reply to strict driveletter mounts, not paths...
	if (!zmo->justDriveLetter)
		return (STATUS_NOT_FOUND);

	// If "?:" then just let windows pick drive letter
	if (zmo->mountpoint.Buffer[4] == L'?')
		return (STATUS_NOT_FOUND);

	// This code works, for driveletters.
	// The mountpoint string is "\\??\\f:" so change
	// that to DosDevicesF:

	DECLARE_UNICODE_STRING_SIZE(MountPoint,
	    ZFS_MAX_DATASET_NAME_LEN); // 36(uuid) + 6 (punct) + 6 (Volume)
	RtlUnicodeStringPrintf(&MountPoint, L"\\DosDevices\\%wc:",
	    towupper(zmo->mountpoint.Buffer[4]));  // "\??\F:"

	// RtlInitUnicodeString(&MountPoint, L"\\DosDevices\\G:");

	linkName =
	    (PMOUNTDEV_SUGGESTED_LINK_NAME)Irp->AssociatedIrp.SystemBuffer;

	linkName->UseOnlyIfThereAreNoOtherLinks = FALSE;
	linkName->NameLength = MountPoint.Length;

	if (sizeof (USHORT) + linkName->NameLength <= bufferLength) {
		RtlCopyMemory((PCHAR)linkName->Name, MountPoint.Buffer,
		    linkName->NameLength);
		Irp->IoStatus.Information =
		    FIELD_OFFSET(MOUNTDEV_SUGGESTED_LINK_NAME, Name[0]) +
		    linkName->NameLength;
		dprintf("  LinkName %wZ (%d)\n", MountPoint, MountPoint.Length);
		return (STATUS_SUCCESS);
	}

	Irp->IoStatus.Information = sizeof (MOUNTDEV_SUGGESTED_LINK_NAME);
	return (STATUS_BUFFER_OVERFLOW);

}

NTSTATUS
ioctl_mountdev_query_stable_guid(PDEVICE_OBJECT DeviceObject, PIRP Irp,
    PIO_STACK_LOCATION IrpSp)
{
	MOUNTDEV_STABLE_GUID	*guid = Irp->UserBuffer;
	ULONG bufferLength =
	    IrpSp->Parameters.DeviceIoControl.OutputBufferLength;
	mount_t *zmo = (mount_t *)DeviceObject->DeviceExtension;

	dprintf("%s: \n", __func__);

	if (bufferLength < sizeof (MOUNTDEV_STABLE_GUID)) {
		Irp->IoStatus.Information = sizeof (MOUNTDEV_STABLE_GUID);
		return (STATUS_BUFFER_TOO_SMALL);
	}

	zfsvfs_t *zfsvfs = vfs_fsprivate(zmo);
	if (zfsvfs == NULL)
		return (STATUS_INVALID_PARAMETER);

	extern int	zfs_vfs_uuid_gen(const char *osname, uuid_t uuid);

	// A bit naughty
	zfs_vfs_uuid_gen(spa_name(dmu_objset_spa(zfsvfs->z_os)),
	    (char *)&guid->StableGuid);

	Irp->IoStatus.Information = sizeof (MOUNTDEV_STABLE_GUID);
	return (STATUS_SUCCESS);
}

NTSTATUS
fsctl_zfs_volume_mountpoint(PDEVICE_OBJECT DeviceObject, PIRP Irp,
    PIO_STACK_LOCATION IrpSp)
{
	mount_t *zmo = (mount_t *)DeviceObject->DeviceExtension;

	ULONG bufferLength =
	    IrpSp->Parameters.DeviceIoControl.OutputBufferLength;

	if (bufferLength < sizeof (fsctl_zfs_volume_mountpoint_t) +
	    zmo->mountpoint.Length) {
		Irp->IoStatus.Information =
		    sizeof (fsctl_zfs_volume_mountpoint_t) +
		    zmo->mountpoint.Length;
		return (STATUS_BUFFER_TOO_SMALL);
	}

	fsctl_zfs_volume_mountpoint_t *fzvm =
	    (fsctl_zfs_volume_mountpoint_t *)Irp->AssociatedIrp.SystemBuffer;

	fzvm->len = zmo->mountpoint.Length;
	memcpy(fzvm->buffer, zmo->mountpoint.Buffer, fzvm->len);
	Irp->IoStatus.Information =
	    sizeof (fsctl_zfs_volume_mountpoint_t) +
	    zmo->mountpoint.Length;
	return (STATUS_SUCCESS);
}

NTSTATUS
fsctl_set_zero_data(PDEVICE_OBJECT DeviceObject, PIRP Irp,
    PIO_STACK_LOCATION IrpSp)
{
	FILE_ZERO_DATA_INFORMATION *fzdi = Irp->AssociatedIrp.SystemBuffer;
	ULONG length = IrpSp->Parameters.FileSystemControl.InputBufferLength;
	PFILE_OBJECT FileObject = IrpSp->FileObject;
	NTSTATUS Status;
	LARGE_INTEGER time;
	uint64_t start, end;
	IO_STATUS_BLOCK iosb;
	zfs_dirlist_t *zccb;

	if (!fzdi || length < sizeof (FILE_ZERO_DATA_INFORMATION))
		return (STATUS_INVALID_PARAMETER);

	if (!FileObject)
		return (STATUS_INVALID_PARAMETER);

	if (fzdi->BeyondFinalZero.QuadPart <= fzdi->FileOffset.QuadPart) {
		dprintf("BeyondFinalZero was <= to Offset (%I64x <= %I64x)\n",
		    fzdi->BeyondFinalZero.QuadPart, fzdi->FileOffset.QuadPart);
		return (STATUS_INVALID_PARAMETER);
	}

	struct vnode *vp = FileObject->FsContext;

	if (!vp)
		return (STATUS_INVALID_PARAMETER);

	zccb = FileObject->FsContext2;

	if (!zccb)
		return (STATUS_INVALID_PARAMETER);

//	if (Irp->RequestorMode == UserMode &&
//	    !(ccb->access & FILE_WRITE_DATA)) {
//		WARN("insufficient privileges\n");
//		return STATUS_ACCESS_DENIED;
//	}

	znode_t *zp = VTOZ(vp);

	// ExAcquireResourceSharedLite(&zmo->tree_lock, true);
	ExAcquireResourceExclusiveLite(vp->FileHeader.Resource, TRUE);

	CcFlushCache(FileObject->SectionObjectPointer, NULL, 0, &iosb);

	if (!vnode_isreg(vp)) {
		dprintf("FileObject did not point to a file\n");
		Status = STATUS_INVALID_PARAMETER;
		goto end;
	}

	/*
	 * btrfs has this test, but MS "test.exe streams" tests that this
	 * works, so we will leave it in.
	 */
#if 0
	if (zp->z_pflags & ZFS_XATTR) {
		dprintf("FileObject is stream\n");
		Status = STATUS_INVALID_PARAMETER;
		goto end;
	}
#endif

	if ((uint64_t)fzdi->FileOffset.QuadPart >= zp->z_size) {
		Status = STATUS_SUCCESS;
		goto end;
	}

	Status = zfs_freesp(zp, fzdi->FileOffset.QuadPart,
	    fzdi->BeyondFinalZero.QuadPart - fzdi->FileOffset.QuadPart,
	    O_RDWR, TRUE);

	CcPurgeCacheSection(FileObject->SectionObjectPointer,
	    &fzdi->FileOffset,
	    (ULONG)(fzdi->BeyondFinalZero.QuadPart - fzdi->FileOffset.QuadPart),
	    FALSE);

	Status = STATUS_SUCCESS;

end:

	ExReleaseResourceLite(vp->FileHeader.Resource);
	// ExReleaseResourceLite(&Vcb->tree_lock);

	return (Status);
}
