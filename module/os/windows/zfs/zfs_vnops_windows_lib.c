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
#include <sys/zfs_acl_impl.h>
#include <sys/zfs_ctldir.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/dirent.h>

#include <sys/unistd.h>
#include <sys/uuid.h>

#include <sys/types.h>
#include <sys/zfs_mount.h>

#include <sys/zfs_windows.h>
#include <sys/driver_extension.h>

#include <mountmgr.h>
#include <Mountdev.h>
#include <ntddvol.h>
#include <Storduid.h>

#undef _NTDDK_

/*
 * Translate a POSIX errno returned by a ZFS vop into the NTSTATUS code
 * that best represents the same condition to Windows callers.  Callers
 * that need context-specific overrides (e.g. IRP_MJ_CREATE distinguishing
 * FILE_EXISTS vs FILE_DOES_NOT_EXIST) should handle those cases before or
 * after calling this helper.
 */
NTSTATUS
zfs_error_to_ntstatus(int error)
{
	switch (error) {
	case EPERM:
	case EACCES:
		return (STATUS_ACCESS_DENIED);
	case ENOENT:
		return (STATUS_OBJECT_NAME_NOT_FOUND);
	case EEXIST:
		return (STATUS_OBJECT_NAME_COLLISION);
	case EXDEV:
		return (STATUS_NOT_SAME_DEVICE);
	case ENOTDIR:
		return (STATUS_NOT_A_DIRECTORY);
	case EISDIR:
		return (STATUS_FILE_IS_A_DIRECTORY);
	case ENOTEMPTY:
		return (STATUS_DIRECTORY_NOT_EMPTY);
	case ENAMETOOLONG:
		return (STATUS_OBJECT_NAME_INVALID);
	case ENOSPC:
	case EDQUOT:
		return (STATUS_DISK_FULL);
	case EROFS:
		return (STATUS_MEDIA_WRITE_PROTECTED);
	case EINVAL:
		return (STATUS_INVALID_PARAMETER);
	case ENOMEM:
		return (STATUS_INSUFFICIENT_RESOURCES);
	case ENOTSUP:
	case EOPNOTSUPP:
		return (STATUS_NOT_SUPPORTED);
	case EBUSY:
		return (STATUS_DEVICE_BUSY);
	case EMLINK:
		return (STATUS_TOO_MANY_LINKS);
	case ERANGE:
	case EOVERFLOW:
		return (STATUS_BUFFER_OVERFLOW);
	case EIO:
		return (STATUS_IO_DEVICE_ERROR);
	case ENXIO:
		return (STATUS_NO_SUCH_DEVICE);
	default:
		/*
		 * Every errno is a small positive int, which NT_SUCCESS()
		 * (>= 0) treats as success -- returning it unconverted would
		 * turn an unmapped failure into a fabricated success status.
		 * Log it so the mapping can be extended, but never hand back
		 * something that looks like success.
		 */
		dprintf("%s: unmapped errno %d -> STATUS_UNEXPECTED_IO_ERROR\n",
		    __func__, error);
		return (STATUS_UNEXPECTED_IO_ERROR);
	}
}

uint64_t windows_load_security = 1;
ZFS_MODULE_PARAM(, windows_, load_security, U64, ZMOD_RW,
	"Windows: load Security Descriptors from storage");

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
	sid_header *sid;
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
#if 0
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
#endif

/* Btrfs */

static dacl def_dacls[] = {
	{ 0, FILE_ALL_ACCESS, &sid_BA },
	{ OBJECT_INHERIT_ACE | CONTAINER_INHERIT_ACE | INHERIT_ONLY_ACE,
	FILE_ALL_ACCESS, &sid_BA },
	{ 0, FILE_ALL_ACCESS, &sid_SY },
	{ OBJECT_INHERIT_ACE | CONTAINER_INHERIT_ACE | INHERIT_ONLY_ACE,
	FILE_ALL_ACCESS, &sid_SY },
	{ OBJECT_INHERIT_ACE | CONTAINER_INHERIT_ACE,
	FILE_GENERIC_READ | FILE_GENERIC_EXECUTE, &sid_BU },
	{ OBJECT_INHERIT_ACE | CONTAINER_INHERIT_ACE | INHERIT_ONLY_ACE,
	FILE_GENERIC_READ | FILE_GENERIC_WRITE | FILE_GENERIC_EXECUTE | DELETE,
	&sid_AU },
	{ 0,
	FILE_GENERIC_READ | FILE_GENERIC_WRITE | FILE_GENERIC_EXECUTE | DELETE,
	&sid_AU },
	// FIXME - Mandatory Label\High Mandatory Level:(OI)(NP)(IO)(NW)
	{ 0, 0, NULL }
};

// ChatGPT version
static dacl def_daclsY[] = {
	// Allow Administrators full access
	{ 0, FILE_ALL_ACCESS, &sid_BA },
	{ OBJECT_INHERIT_ACE | CONTAINER_INHERIT_ACE | INHERIT_ONLY_ACE,
	FILE_ALL_ACCESS, &sid_BA },

	// Allow SYSTEM full access
	{ 0, FILE_ALL_ACCESS, &sid_SY },
	{ OBJECT_INHERIT_ACE | CONTAINER_INHERIT_ACE | INHERIT_ONLY_ACE,
	FILE_ALL_ACCESS, &sid_SY },

	// Allow Authenticated Users read/write/execute/delete
	// (typical user permissions)
	{ 0,
	FILE_GENERIC_READ | FILE_GENERIC_WRITE | FILE_GENERIC_EXECUTE |
	DELETE | WRITE_DAC | WRITE_OWNER,
	&sid_AU },
	{ OBJECT_INHERIT_ACE | CONTAINER_INHERIT_ACE | INHERIT_ONLY_ACE,
	FILE_GENERIC_READ | FILE_GENERIC_WRITE | FILE_GENERIC_EXECUTE |
	DELETE | WRITE_DAC | WRITE_OWNER,
	&sid_AU },

	// Optional: Allow Built-in Users read-only
	{ 0, FILE_GENERIC_READ | FILE_GENERIC_EXECUTE, &sid_BU },
	{ OBJECT_INHERIT_ACE | CONTAINER_INHERIT_ACE | INHERIT_ONLY_ACE,
	FILE_GENERIC_READ | FILE_GENERIC_EXECUTE, &sid_BU },

	{ 0, 0, NULL } // Terminator
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
		case 0x18: // No longer used
			return (
			    "IRP_MJ_PNP(IRP_MN_QUERY_LEGACY_BUS_INFORMATION)");
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
	case STATUS_UNRECOGNIZED_VOLUME:
		return ("STATUS_UNRECOGNIZED_VOLUME");
	case STATUS_VOLUME_MOUNTED:
		return ("STATUS_VOLUME_MOUNTED");
	case STATUS_VOLUME_DISMOUNTED:
		return ("STATUS_VOLUME_DISMOUNTED");
	case STATUS_DEVICE_NOT_READY:
		return ("STATUS_DEVICE_NOT_READY");
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

/*
 * Same again, but use NonPaged memory.
 * IRP_MN_MOUNT_VOLUME is called with irql==2, and
 * we try to copy zmo->name.Buffer to VolumeLabel, which
 * is not allowed.
 */
int
AsciiStringToUnicodeStringNP(char *in, PUNICODE_STRING out)
{
	NTSTATUS status;
	ULONG len;

	memset(out, 0, sizeof (UNICODE_STRING));
	if (in == NULL)
		return (0);

	status = RtlUTF8ToUnicodeN(NULL, 0, &len,
	    in, strlen(in));
	if (!NT_SUCCESS(status))
		return (0);

	out->Buffer = (PWSTR)ExAllocatePoolWithTag(NonPagedPoolNx,
	    len + sizeof (WCHAR), 'tag1');

	if (out->Buffer == NULL)
		return (0);

	out->Length = len;
	out->MaximumLength = len + sizeof (WCHAR);

	status = RtlUTF8ToUnicodeN(out->Buffer, out->MaximumLength,
	    NULL,
	    in, strlen(in));

	out->Buffer[out->Length / sizeof (WCHAR)] =
	    UNICODE_NULL;
	return (0);
}

void
FreeUnicodeString(PUNICODE_STRING s)
{
	if (s->Buffer) ExFreePool(s->Buffer);
	s->Buffer = NULL;
	s->Length = 0;
}

int
zfs_vnop_ioctl_fullfsync(struct vnode *vp, vfs_context_t *ct, zfsvfs_t *zfsvfs)
{
	int error = zfs_fsync(VTOZ(vp), /* syncflag */ 0, NULL);
	return (error);
}

/*
 * Take zp, and convert zp->z_pflags into Windows
 * FILE_ATTRIBUTES_ - Can we assume z_pflags is always
 * up-to-date, or is there need to call zfs_getattr().
 */
uint32_t
zfs_getwinflags(uint64_t zflags, boolean_t isdir)
{
	uint32_t winflags = 0;

	if (zflags & ZFS_READONLY)
		winflags |= FILE_ATTRIBUTE_READONLY;
	if (zflags & ZFS_HIDDEN)
		winflags |= FILE_ATTRIBUTE_HIDDEN;
	if (zflags & ZFS_SYSTEM)
		winflags |= FILE_ATTRIBUTE_SYSTEM;
	if (zflags & ZFS_ARCHIVE)
		winflags |= FILE_ATTRIBUTE_ARCHIVE;
	if (zflags & ZFS_SPARSE)
		winflags |= FILE_ATTRIBUTE_SPARSE_FILE;
	if (zflags & ZFS_REPARSE)
		winflags |= FILE_ATTRIBUTE_REPARSE_POINT;

	if (isdir) {
		winflags |= FILE_ATTRIBUTE_DIRECTORY;
		winflags &= ~FILE_ATTRIBUTE_ARCHIVE;
	}

	if (winflags == 0)
		winflags = FILE_ATTRIBUTE_NORMAL;

	dprintf("%s: changing zfs 0x%08llx to win 0x%08lx\n", __func__,
	    zflags, winflags);
	return (winflags);
}

/*
 * Called when a request to change FileAttributes. So we
 * need to populate "vap" with the information changes
 * before calling zfs_setattr().
 */
int
zfs_setwinflags_xva(znode_t *zp, uint32_t win_flags, xvattr_t *xva)
{
	uint64_t zfs_flags = 0ULL;
	xoptattr_t *xoap;

	/* zp is NULL if we have no yet created the file */
	if (zp)
		zfs_flags = zp->z_pflags;

	win_flags &= ~FILE_ATTRIBUTE_NORMAL;

	xva_init(xva);
	xoap = xva_getxoptattr(xva);

#define	FLAG_CHANGE(zflag, wflag, xflag, xfield) do {	\
		if (((win_flags & (wflag)) && !(zfs_flags & (zflag))) || \
			((zfs_flags & (zflag)) && !(win_flags & (wflag)))) { \
			XVA_SET_REQ(xva, (xflag)); \
			(xfield) = ((win_flags & (wflag)) != 0); \
		} \
	} while (0)

	FLAG_CHANGE(ZFS_READONLY, FILE_ATTRIBUTE_READONLY,
	    XAT_READONLY, xoap->xoa_readonly);
	FLAG_CHANGE(ZFS_HIDDEN, FILE_ATTRIBUTE_HIDDEN,
	    XAT_HIDDEN, xoap->xoa_hidden);
	FLAG_CHANGE(ZFS_SYSTEM, FILE_ATTRIBUTE_SYSTEM,
	    XAT_SYSTEM, xoap->xoa_system);
	FLAG_CHANGE(ZFS_ARCHIVE, FILE_ATTRIBUTE_ARCHIVE,
	    XAT_ARCHIVE, xoap->xoa_archive);
#if 0
	/* You are allowed to call set on there, but they should do nothing */
	FLAG_CHANGE(ZFS_SPARSE, FILE_ATTRIBUTE_SPARSE_FILE,
	    XAT_SPARSE, xoap->xoa_sparse);
	FLAG_CHANGE(ZFS_REPARSE, FILE_ATTRIBUTE_REPARSE_POINT,
	    XAT_REPARSE, xoap->xoa_reparse);
#endif

#undef FLAG_CHANGE

	return (STATUS_SUCCESS);
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
	struct iovec iov;

	if (zfs_is_readonly(VTOZ(vp)->z_zfsvfs))
		return (STATUS_MEDIA_WRITE_PROTECTED);

	dprintf("%s: xattr '%.*s' valuelen %u\n", __func__,
	    ea->EaNameLength, ea->EaName, ea->EaValueLength);

	/*
	 * Some Windows subsystems (MSYS2/git-bash in particular) resend
	 * the same marker EAs, e.g. "NfsActOnLink", on every single lookup
	 * or open, not only on create. Without this check, every such
	 * lookup unconditionally rewrites the SA/spill xattr data even
	 * though nothing changed, turning ordinary read traffic into a
	 * steady stream of COW metadata writes. Skip the set entirely
	 * when the stored value already matches.
	 */
	if (ea->EaValueLength == 0) {
		ssize_t retsize = 0;

		if (zpl_xattr_get(vp, ea->EaName, NULL, &retsize, NULL) == 0 &&
		    retsize == 0)
			return (0);
	} else if (ea->EaValueLength <= 512) {
		char existing[512];
		struct iovec cmp_iov = { existing, ea->EaValueLength };
		zfs_uio_t cmp_uio;
		ssize_t retsize = 0;

		zfs_uio_iovec_init(&cmp_uio, &cmp_iov, 1, 0, UIO_SYSSPACE,
		    ea->EaValueLength, 0);
		if (zpl_xattr_get(vp, ea->EaName, &cmp_uio, &retsize,
		    NULL) == 0 && retsize == ea->EaValueLength &&
		    memcmp(existing, ea->EaName + ea->EaNameLength + 1,
		    ea->EaValueLength) == 0)
			return (0);
	}

	// EaValueLength of zero, means no value, not delete.
	if (ea->EaValueLength == 0) {
		iov.iov_base = (void *)NULL;
		iov.iov_len = 0;
	} else {
		iov.iov_base = (void *)(ea->EaName + ea->EaNameLength + 1);
		iov.iov_len = ea->EaValueLength;
	}

	zfs_uio_t uio;
	zfs_uio_iovec_init(&uio, &iov, 1, 0, UIO_SYSSPACE,
	    ea->EaValueLength, 0);

	error = zpl_xattr_set(vp, ea->EaName, &uio, 0, NULL);

	return (0);
}


/*
 * Apply a set of EAs to a vnode, while handling special Windows EAs that
 * set UID/GID/Mode/rdev.
 * According to ChatGPT, if the LIST is empty, all the xattrs should be
 * removed. (Keeping any non-xattr entries, like NTACL etc)
 * Otherwise, update the xattrs for each EA. Note that an
 * EA with EaValueLength==0 is not a delete request, but a
 * no-value request. There is no way to delete a single
 * EA, it is expected that Windows sends an empty list
 * to delete all EAs, then a list of the other EAs to restore.
 */
NTSTATUS
vnode_apply_eas(struct vnode *vp, zfs_ccb_t *zccb,
    PFILE_FULL_EA_INFORMATION eas, ULONG eaLength,
    PULONG pEaErrorOffset)
{
	NTSTATUS Status = STATUS_SUCCESS;

	if (vp == NULL)
		return (STATUS_INVALID_PARAMETER);

	if (zfs_is_readonly(VTOZ(vp)->z_zfsvfs))
		return (STATUS_MEDIA_WRITE_PROTECTED);

	if (eas == NULL || eaLength == 0) {
		dprintf("%s: delete all xattr request.\n", __func__);
		// Delete all xattrs.
		// Status = zpl_xattr_remove_all(vp);
		vnode_clear_easize(vp);
		return (Status);
	}

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
		zfs_setattr(zp, &vap, 0, NULL);

	zfs_send_notify(zfsvfs, zccb->z_name_cache,
	    zccb->z_name_offset,
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
    zfs_ccb_t *zccb, ino64_t objnum)
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

	// If it is marked to be deleted it, Windows expects
	// the file to be invisible.
	if (get_zp == 0 && tzp != NULL && ZTOV(tzp) &&
	    (vnode_unlink(ZTOV(tzp)) & DELETE_HIDDEN)) {
		// Return anything but ENOSPC to skip
		zrele(tzp);
		return (ENOTACTIVE);
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

	ULONG reparse_tag = 0;
	ULONG ea_size = 0;
	ea_size = xattr_getsize(ZTOV(tzp));
	reparse_tag = get_reparse_tag(tzp);

	uint64_t AllocationSize;
	AllocationSize = allocationsize(tzp);

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
		    AllocationSize;
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
		eodp->EaSize = reparse_tag ? reparse_tag : ea_size;
		eodp->FileAttributes =
		    zfs_getwinflags(tzp->z_pflags, S_ISDIR(tzp->z_mode));
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
		    AllocationSize;
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
		fibdi->EaSize = reparse_tag ? reparse_tag : ea_size;
		fibdi->FileAttributes =
		    zfs_getwinflags(tzp->z_pflags, S_ISDIR(tzp->z_mode));
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
		    AllocationSize;
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
		fbdi->EaSize = reparse_tag ? reparse_tag : ea_size;
		fbdi->FileAttributes =
		    zfs_getwinflags(tzp->z_pflags, S_ISDIR(tzp->z_mode));
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
		    AllocationSize;
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
		    zfs_getwinflags(tzp->z_pflags, S_ISDIR(tzp->z_mode));
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
		    AllocationSize;
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
		fifdi->EaSize = reparse_tag ? reparse_tag : ea_size;
		fifdi->FileAttributes =
		    zfs_getwinflags(tzp->z_pflags, S_ISDIR(tzp->z_mode));
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
		    AllocationSize;
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
		fiedi->EaSize = ea_size;
		fiedi->ReparsePointTag = reparse_tag;
		fiedi->FileAttributes =
		    zfs_getwinflags(tzp->z_pflags, S_ISDIR(tzp->z_mode));
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
		    AllocationSize;
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
		fiebdi->EaSize = ea_size;
		fiebdi->ReparsePointTag = reparse_tag;
		fiebdi->FileAttributes =
		    zfs_getwinflags(tzp->z_pflags, S_ISDIR(tzp->z_mode));
		fiebdi->ShortNameLength = 0;
		RtlCopyMemory(&fiebdi->FileId.Identifier[0], &tzp->z_id,
		    sizeof (UINT64));
		guid = dmu_objset_fsid_guid(zfsvfs->z_os);
		RtlCopyMemory(&fiebdi->FileId.Identifier[sizeof (UINT64)],
		    &guid, sizeof (UINT64));
		nameptr = fiebdi->FileName;
		fiebdi->FileNameLength = namelenholder;
		break;

	case FileIdAllExtdBothDirectoryInformation:
		structsize = FIELD_OFFSET(
		    FILE_ID_ALL_EXTD_BOTH_DIR_INFORMATION,
		    FileName[0]);
		if (ctx->outcount + structsize +
		    namelenholder > ctx->bufsize)
			break;
		FILE_ID_ALL_EXTD_BOTH_DIR_INFORMATION *fiaebdi =
		    (FILE_ID_ALL_EXTD_BOTH_DIR_INFORMATION *)
		    ctx->bufptr;
		next_offset = &fiebdi->NextEntryOffset;

		fiaebdi->FileIndex = ctx->offset;
		fiaebdi->AllocationSize.QuadPart =
		    AllocationSize;
		fiaebdi->EndOfFile.QuadPart =
		    S_ISDIR(tzp->z_mode) ? 0 :
		    tzp->z_size;
		TIME_UNIX_TO_WINDOWS(mtime,
		    fiaebdi->LastWriteTime.QuadPart);
		TIME_UNIX_TO_WINDOWS(ctime,
		    fiaebdi->ChangeTime.QuadPart);
		TIME_UNIX_TO_WINDOWS(crtime,
		    fiaebdi->CreationTime.QuadPart);
		TIME_UNIX_TO_WINDOWS(tzp->z_atime,
		    fiaebdi->LastAccessTime.QuadPart);
		fiaebdi->EaSize = ea_size;
		fiaebdi->ReparsePointTag = reparse_tag;
		fiaebdi->FileAttributes =
		    zfs_getwinflags(tzp->z_pflags, S_ISDIR(tzp->z_mode));
		fiaebdi->ShortNameLength = 0;
		fiaebdi->FileId.QuadPart = tzp->z_id;

		RtlCopyMemory(&fiaebdi->FileId128.Identifier[0], &tzp->z_id,
		    sizeof (UINT64));
		guid = dmu_objset_fsid_guid(zfsvfs->z_os);
		RtlCopyMemory(&fiaebdi->FileId128.Identifier[sizeof (UINT64)],
		    &guid, sizeof (UINT64));
		nameptr = fiaebdi->FileName;
		fiaebdi->FileNameLength = namelenholder;
		break;

	default:
		panic("%s unknown listing type %d\n",
		    __func__, ctx->dirlisttype);
	}

	// Release the zp
	if (get_zp == 0 && tzp != NULL && ZTOV(tzp) != NULL) {
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

	// If it is going to fit, compute alignment,
	// in case this dir entry is the last one,
	// we don't align last one.
	ctx->last_alignment = reclen - rawsize;

	// If we aren't to skip, advance all pointers
	VERIFY3P(next_offset, !=, NULL);
	ctx->next_offset = next_offset;
	*ctx->next_offset = reclen;

	ctx->outcount += reclen;
	ctx->bufptr += reclen;
	return (0);
}

/*
 * Called by taskq, to call zfs_znode_getvnode( vnode_create( - and
 * attach vnode to znode.
 */
void
zfs_znode_asyncgetvnode_impl(void *arg)
{
	znode_t *zp = (znode_t *)arg;
	VERIFY3P(zp, !=, NULL);
	zfsvfs_t *zfsvfs = zp->z_zfsvfs;
	VERIFY3P(zfsvfs, !=, NULL);

	// Attach vnode, done as different thread. This already wakes up
	// anyone blocked in zfs_znode_asyncwait() once zp->z_vnode is set.
	zfs_znode_getvnode(zp, NULL, zfsvfs);

	// Mark the taskq entry idle again.
	mutex_enter(&zp->z_attach_lock);
	taskq_init_ent(&zp->z_attach_taskq);
	mutex_exit(&zp->z_attach_lock);

}


/*
 * If the znode's vnode is not yet attached (zp->z_vnode == NULL), block
 * until it is. zfs_znode_getvnode() broadcasts z_attach_cv once it sets
 * zp->z_vnode, regardless of whether it was called directly (the
 * synchronous create/mkdir/etc paths) or from the async taskq
 * (zfs_znode_asyncgetvnode_impl()) - so this covers racing either kind
 * of attacher, not just the async one.
 *
 * We guarantee znode has a vnode at the return of function only
 * when return is "0". On failure to wait (e.g. zfsvfs tearing down),
 * it returns -1, and caller may consider waiting by other means.
 */
int
zfs_znode_asyncwait(zfsvfs_t *zfsvfs, znode_t *zp)
{
	if (zp == NULL || zfsvfs == NULL)
		return (-1);

	if (zfs_enter(zfsvfs, FTAG) != 0)
		return (-1);

	if (zfsvfs->z_os == NULL) {
		zfs_exit(zfsvfs, FTAG);
		return (-1);
	}

	mutex_enter(&zp->z_attach_lock);
	while (zp->z_vnode == NULL)
		cv_wait(&zp->z_attach_cv, &zp->z_attach_lock);
	mutex_exit(&zp->z_attach_lock);

	zfs_exit(zfsvfs, FTAG);
	return (0);
}

/*
 * Called in place of VN_RELE() for the places that uses ZGET_FLAG_ASYNC.
 */
void
zfs_znode_asyncput_impl(znode_t *zp)
{
	// Make sure the other thread finished zfs_znode_getvnode();
	// This may block, if waiting is required.
	zfs_znode_asyncwait(zp->z_zfsvfs, zp);

	// This shouldn't happen
	if (ZTOV(zp) == NULL) {
		dprintf("%s: zp %p vp still NULL after wait?\n", __func__, zp);
		return;
	}
	// Safe to release now that it is attached.
	VN_RELE(ZTOV(zp));

}

/*
 * Called in place of VN_RELE() for the places that uses ZGET_FLAG_ASYNC,
 * where we also taskq it - as we come from reclaim.
 */
void
zfs_znode_asyncput(znode_t *zp)
{
	dsl_pool_t *dp = dmu_objset_pool(zp->z_zfsvfs->z_os);
	taskq_t *tq = dsl_pool_zrele_taskq(dp);
	vnode_t *vp = ZTOV(zp);

	VERIFY3P(tq, !=, NULL);

	/* If iocount > 1, AND, vp is set (not async_get) */
	if (vp != NULL && vnode_iocount(vp) > 1) {
		VN_RELE(vp);
		return;
	}

	VERIFY(taskq_dispatch(
	    (taskq_t *)tq,
	    (task_func_t *)zfs_znode_asyncput_impl, zp, TQ_SLEEP) != 0);
}

/*
 * Attach a new vnode to the znode asynchronically. We do this using
 * a taskq to call it, and then wait to release the iocount.
 * Called of zget_ext(..., ZGET_FLAG_ASYNC); will use
 * zfs_znode_asyncput(zp) instead of VN_RELE(vp).
 */
int
zfs_znode_asyncgetvnode(znode_t *zp, zfsvfs_t *zfsvfs)
{
	VERIFY(zp != NULL);
	VERIFY(zfsvfs != NULL);

	// We should not have a vnode here.
	VERIFY3P(ZTOV(zp), ==, NULL);

	dsl_pool_t *dp = dmu_objset_pool(zfsvfs->z_os);
	taskq_t *tq = dsl_pool_zrele_taskq(dp);
	VERIFY3P(tq, !=, NULL);

	mutex_enter(&zp->z_attach_lock);
	taskq_dispatch_ent(tq,
	    (task_func_t *)zfs_znode_asyncgetvnode_impl,
	    zp,
	    TQ_SLEEP,
	    &zp->z_attach_taskq);
	mutex_exit(&zp->z_attach_lock);
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
	    &vattr, cr, NULL, &acl_ids)) != 0) {
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
	error = dmu_tx_assign(tx, DMU_TX_WAIT);
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
 * Note this is a dataset path, starting from its "/", does not
 * include the mountpoint, nor should it.
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
	mount_t *zmo;
	char name[ZAP_MAXNAMELEN];

	// No output? nothing to do
	if (!fullpath || !returnsize)
		return (EINVAL);
	// No input? nothing to do
	if (!start_zp)
		return (EINVAL);

	zfsvfs = start_zp->z_zfsvfs;
	zp = start_zp;
	zmo = zfsvfs->z_vfs;

	VN_HOLD(ZTOV(zp));

	work = kmem_alloc(MAXPATHLEN * 2, KM_SLEEP);
	index = MAXPATHLEN * 2 - 1;

	work[--index] = 0;
	size = 1;

	while (1) {

		// rewrite this using vnode_parent
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
		if (zp->z_id == zfsvfs->z_root) {
#if 0
			if (zmo->mounted_on)
				strlcpy(name, zmo->mounted_on, MAXPATHLEN);
			else
#endif
				strlcpy(name, "", MAXPATHLEN);
		} else if (zp->z_id == ZFSCTL_INO_ROOT)
			strlcpy(name, ZFS_CTLDIR_NAME, MAXPATHLEN);
		else if (zp->z_id == ZFSCTL_INO_SNAPDIR)
			strlcpy(name, ZFS_SNAPDIR_NAME, MAXPATHLEN);
		else if (zfsctl_is_leafnode(zp)) {
			uint64_t id, pos = 0;
			while (error == 0) {
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
				    name, ZAP_MAXNAMELEN)) != 0) {
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

		if (zp == NULL) // No parent
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

	// Free existing, if any
	if (*fullpath != NULL)
		kmem_free(*fullpath, *returnsize);

	// Allocate new
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

	dprintf("%s: set '%s' as name, with offset %d for '%s'\n",
	    __func__, *fullpath, *start_zp_offset,
	    &(*fullpath)[*start_zp_offset]);
	return (0);

failed:
	if (zp != NULL && ZTOV(zp) != NULL)
		VN_RELE(ZTOV(zp));
	if (dzp != NULL && ZTOV(dzp) != NULL)
		VN_RELE(ZTOV(dzp));
	kmem_free(work, MAXPATHLEN * 2);
	return (SET_ERROR(-1));
}

/*
 * Eventually, build_path above could handle streams, but for now lets
 * just set the stream name.
 * Using FileTest on NTFS file:Zone.Identifier:$DATA returns the
 * name "/src/openzfs/zpool.exe:Zone.Identifier"
 *
 * Re-confirmed yet again with NTFS, opening
 * "\README.md:Zone.Identifier:$DATA"
 *
 * FILE_NAME_INFORMATION: "\README.md:Zone.Identifier"
 * FILE_STREAM_INFORMATION: "::$DATA" and ":Zone.Identifier:$DATA"
 *
 * So FULL path for file_name, with no tail DATA. And even though we
 * opened ":Zone.Identifier", FILE_STREAM_INFORMATION shows all streams
 * as if we opened the parent file.
 *
 */
int
zfs_build_path_stream(znode_t *start_zp, znode_t *start_parent, char **fullpath,
    uint32_t *returnsize, uint32_t *start_zp_offset, char *stream)
{
	int error;

	if (start_zp == NULL)
		return (EINVAL);

	error = zfs_build_path(start_zp, start_parent, fullpath,
	    returnsize, start_zp_offset);

	if (!error && stream) {
		// name + ":" + streamname + null
		// TODO: clean this up to not realloc
		// make sure it isn't already the correct name
		if (!*fullpath || strcmp(*fullpath, stream) != 0) {
			char *newname;
			newname = kmem_asprintf("%s:%s",
			    *fullpath ? *fullpath : "", stream);

			// Fetch new offset, before ":stream"
			*start_zp_offset = *returnsize;
			// free previous full name
			kmem_free(*fullpath, *returnsize);
			// assign new size
			*returnsize = strlen(newname) + 1;
			// assign new string
			*fullpath = newname;
		}
	} else if (error) {
		*fullpath = kmem_asprintf("(nameunknown)");
		*returnsize = strlen(*fullpath) + 1;
		*start_zp_offset = 0;
	}

	// If it ends with ":$DATA", truncate.
	if (returnsize && *returnsize >= 7) {
		if (strcmp(&((*fullpath)[(*returnsize) - 7]), ":$DATA") == 0) {
			// dont change returnsize, used to free.
			((*fullpath)[(*returnsize) - 7]) = 0;
		}
	}

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
	int wideoffset = nameoffset / sizeof (WCHAR);
	ULONG allocateBytes = 0;
	int length;
	NTSTATUS status;

	if (name == NULL)
		return;

	length = strlen(name);

	if (nameoffset > length)
		return;

	if (!zmo->NotifySync)
		return;

	RtlUTF8ToUnicodeN(NULL, 0, &allocateBytes, name, length);
	if (allocateBytes == 0)
		return;

	allocateBytes += sizeof (wchar_t); // Add space for the null terminator.
	wchar_t *widepath = (wchar_t *)ExAllocatePoolWithTag(NonPagedPoolNx,
	    allocateBytes, 'znot');
	if (widepath == NULL)
		return;

	// Initialize UNICODE_STRING for conversion.
	RtlInitEmptyUnicodeString(&ustr, widepath, (USHORT)allocateBytes);

	// Convert UTF-8 to Unicode.
	ULONG byteCount;
	status = RtlUTF8ToUnicodeN(ustr.Buffer, ustr.MaximumLength,
	    &byteCount, name, length);
	ustr.Length = (USHORT)byteCount;
	if (!NT_SUCCESS(status)) {
		ExFreePoolWithTag(widepath, 'znot');
		return;
	}

	widepath[ustr.Length / sizeof (WCHAR)] = 0;

	// Now find last backslash
	wchar_t *lastBackslash = wcsrchr(widepath, L'\\');

	// No slash, or only root, then don't skip over it.
	if (lastBackslash == NULL || ustr.Length == sizeof (WCHAR))
		wideoffset = 0;
	else
		wideoffset = (int)((lastBackslash - widepath) + 1);

	// wideoffset is currently in character-offset, for this print
	dprintf("zfs_send_notify_stream: '%S' %lu %u\n", ustr.Buffer,
	    FilterMatch, Action);

	dprintf("%s: offset %d part '%S'\n", __func__,
	    wideoffset, &(ustr.Buffer[wideoffset]));

	// Now wideoffset will go into byte-offset, for the call
	wideoffset *= sizeof (wchar_t);

	// We will destroy many local variables now
	if (stream != NULL) {
		length = strlen(stream);
		allocateBytes = 0;
		RtlUTF8ToUnicodeN(NULL, 0, &allocateBytes, stream, length);
		if (allocateBytes != 0) {
			// Add space for the null terminator.
			allocateBytes += sizeof (wchar_t);
			wchar_t *widepath =
			    (wchar_t *)ExAllocatePoolWithTag(NonPagedPoolNx,
			    allocateBytes, 'znot');
			if (widepath != NULL) {
				RtlInitEmptyUnicodeString(&ustream, widepath,
				    (USHORT)allocateBytes);
				ULONG bytes;
				status = RtlUTF8ToUnicodeN(ustream.Buffer,
				    ustream.MaximumLength, &bytes,
				    stream, length);
				if (NT_SUCCESS(status)) {
					ustream.Length = bytes;
					dprintf("%s: with stream '%wZ'\n",
					    __func__, &ustream);
					widepath[ustream.Length /
					    sizeof (wchar_t)] = 0;
				} else {
					FreeUnicodeString(&ustream);
					stream = NULL;
				}
			}
		}
	}
	/* Is nameoffset in bytes, or in characters? */
	FsRtlNotifyFilterReportChange(zmo->NotifySync, &zmo->DirNotifyList,
	    (PSTRING)&ustr,
	    wideoffset,
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

	dprintf("%s: %llu -> 00:00:00:00:00:%02d:%02d:%02d\n",
	    __func__, uid, tmp->IdentifierAuthority.Value[5],
	    tmp->SubAuthority[0], (uid == 0) ? 0 :
	    tmp->SubAuthority[1]);
}

uint64_t
zfs_sid2uid(SID *sid)
{
	return (spl_sid_to_uid((struct _SID *)sid));
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

uint64_t
zfs_sid2gid(SID *sid)
{
	return (spl_sid_to_gid((struct _SID *)sid));
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
find_set_gid(struct vnode *vp, struct vnode *dvp,
    PSECURITY_SUBJECT_CONTEXT subjcont)
{
	NTSTATUS Status;
	TOKEN_OWNER *to = NULL;
	TOKEN_PRIMARY_GROUP *tpg = NULL;
	TOKEN_GROUPS *tg = NULL;
	znode_t *zp = VTOZ(vp);

	if (dvp && zp) {
		znode_t *dzp = VTOZ(dvp);
		if (dzp && dzp->z_mode & S_ISGID) {
			zp->z_gid = dzp->z_gid;
			return;
		}
	}

	if (!subjcont || !subjcont->PrimaryToken)
		return;
#if 0
	Status = SeQueryInformationToken(subjcont->PrimaryToken,
	    TokenOwner, (void**)&to);
	if (!NT_SUCCESS(Status)) {
		dprintf("SeQueryInformationToken returned %08lx\n", Status);
	} else {
		if (zp) {
			zp->z_uid = zfs_sid2uid(to->Owner);
			dprintf("Using uid %lu\n", zp->z_uid);
			dump_sid(to->Owner);
		}
		ExFreePool(to);
	}
#endif

	// If we one day implement a gid_mapping_list
#if 0
	Status = SeQueryInformationToken(subjcont->PrimaryToken,
	    TokenPrimaryGroup, (void**)&tpg);
	if (!NT_SUCCESS(Status) || !tpg) {
		dprintf("SeQueryInformationToken returned %08lx\n", Status);
	} else {
		if (zp) {
			zp->z_gid = zfs_sid2gid(tpg->PrimaryGroup);
			dprintf("Using gid %lu\n", zp->z_gid);
			dump_sid(tpg->PrimaryGroup);
		}
		ExFreePool(tpg);
	}
#endif
#if 0
	Status = SeQueryInformationToken(subjcont->PrimaryToken,
	    TokenGroups, (void**)&tg);
	if (!NT_SUCCESS(Status)) {
		dprintf("SeQueryInformationToken returned %08lx\n", Status);
	} else {
		ULONG i;

		for (i = 0; i < tg->GroupCount; i++) {
		    // tg->Groups[i].Sid
		}

		ExFreePool(tg);
	}
#endif
}

void
zfs_save_ntsecurity(struct vnode *vp)
{
	int error;
	zfs_uio_t uio;
	struct iovec iov;

	/* Don't set on ctldir */
	if (zfsctl_is_node(VTOZ(vp)))
		return;

	if (zfs_is_readonly(VTOZ(vp)->z_zfsvfs))
		return; // (STATUS_MEDIA_WRITE_PROTECTED);

	iov.iov_base = vnode_security(vp);
	iov.iov_len = RtlLengthSecurityDescriptor(iov.iov_base);

	zfs_uio_iovec_init(&uio, &iov, 1, 0,
	    UIO_SYSSPACE, iov.iov_len, 0);

	error = zpl_xattr_set(vp, EA_NTACL, &uio, 0, NULL);

	if (error == 0) {
		dprintf("%s: wrote NTSecurity\n", __func__);
	} else {
		dprintf("%s: failed to add NTSecurity: %d\n", __func__,
		    error);
	}
}

void
zfs_load_ntsecurity(struct vnode *vp)
{
	int error;
	zfs_uio_t uio;
	struct iovec iov;
	ssize_t retsize;

	if (!windows_load_security)
		return;

	error = zpl_xattr_get(vp, EA_NTACL, NULL,
	    &retsize, NULL);

	if (error || retsize <= 0)
		return;


	void *oldsd = vnode_security(vp);
	vnode_setsecurity(vp, NULL);
	if (oldsd)
		ExFreePool(oldsd);

	void *sd = ExAllocatePoolWithTag(PagedPool, retsize, 'ZSEC');
	if (sd == NULL)
		return;

	iov.iov_base = sd;
	iov.iov_len = retsize;

	zfs_uio_iovec_init(&uio, &iov, 1, 0,
	    UIO_SYSSPACE, retsize, 0);

	error = zpl_xattr_get(vp, EA_NTACL, &uio, &retsize, NULL);

	if (error == 0) {
		if (RtlValidSecurityDescriptor(sd)) {
			dprintf("%s: read NTSecurity\n", __func__);
			vnode_setsecurity(vp, sd);
			return;
		}
		dprintf("%s: invalid NTSecurity xattr\n", __func__);
	}
	ExFreePool(sd);
}

void
zfs_remove_ntsecurity(struct vnode *vp)
{
	int error;
	void *oldsd = vnode_security(vp);
	vnode_setsecurity(vp, NULL);
	if (oldsd)
		ExFreePool(oldsd);

	if (zfs_is_readonly(VTOZ(vp)->z_zfsvfs))
		return;

	error = zpl_xattr_set(vp, EA_NTACL, NULL, 0, NULL);

	if (error == 0) {
		dprintf("%s: removed NTSecurity\n", __func__);
	} else {
		dprintf("%s: failed to remove NTSecurity: %d\n",
		    __func__, error);
	}
}

void
zfs_attach_security_root(struct vnode *vp)
{
	SECURITY_DESCRIPTOR sd;
	SID *usersid = NULL, *groupsid = NULL;
	znode_t *zp = VTOZ(vp);
	NTSTATUS Status;
	ACL *acl = NULL;

	Status = RtlCreateSecurityDescriptor(&sd, SECURITY_DESCRIPTOR_REVISION);
	if (Status != STATUS_SUCCESS)
		goto err;

	zfs_uid2sid(zp->z_uid, &usersid);
	zfs_gid2sid(zp->z_gid, &groupsid);

	RtlSetOwnerSecurityDescriptor(&sd, usersid, FALSE);
	RtlSetGroupSecurityDescriptor(&sd, groupsid, FALSE);

	acl = zfs_set_acl(def_dacls);

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

	zfs_save_ntsecurity(vp);

err:
	if (acl)
		ExFreePool(acl);
	if (usersid != NULL)
		zfs_freesid(usersid);
	if (groupsid != NULL)
		zfs_freesid(groupsid);
}

// Attach security based on parent SD, or
// optionally, passed in SD.
int
zfs_attach_security(struct vnode *vp, struct vnode *dvp,
    PACCESS_STATE access_state)
{
	SECURITY_SUBJECT_CONTEXT subjcont;
	PSECURITY_SUBJECT_CONTEXT psubjcont = NULL;
	NTSTATUS Status = STATUS_INVALID_PARAMETER;
	SID *usersid = NULL, *groupsid = NULL;
	int error = 0;
	BOOLEAN defaulted;
	uint8_t *buf = NULL;
	boolean_t allocated_usersid = B_FALSE;
	boolean_t allocated_groupsid = B_FALSE;

	if (vp == NULL)
		return (Status);

	if (vp->security_descriptor != NULL)
		return (Status);

	znode_t *zp = VTOZ(vp);
	zfsvfs_t *zfsvfs = zp->z_zfsvfs;

	/* First check if we can read in an existing NTSecurity */
	zfs_load_ntsecurity(vp);
	if (vp->security_descriptor != NULL)
		return (0);

	// If we are the rootvp, we don't have a parent, so do different setup
	if (zp->z_id == zfsvfs->z_root ||
	    zp->z_id == ZFSCTL_INO_ROOT) {
		zfs_attach_security_root(vp);
		return (0);
	}

	if ((error = zfs_enter(zfsvfs, FTAG)) != 0)
		return (error);

	// If no parent, find it. This will take one hold on
	// dvp, either directly or from zget().
	znode_t *dzp = NULL;

	if (dvp == NULL) {
		dvp = zfs_parent(vp);
	} else {
		VERIFY0(VN_HOLD(dvp));
	}

	if (dvp == NULL)
		goto err;
	dzp = VTOZ(dvp);

	// We can fail here, if we are processing unlinked-list
	if (vnode_security(dvp) == NULL)
		goto err;

	ASSERT(dvp != NULL);
	ASSERT(dzp != NULL);
	ASSERT(vnode_security(dvp) != NULL);

	ULONG flags = SEF_DEFAULT_DESCRIPTOR_FOR_OBJECT;

	if (access_state != NULL) {
		psubjcont = &access_state->SubjectSecurityContext;
	} else {
		flags |= SEF_DACL_AUTO_INHERIT | SEF_SACL_AUTO_INHERIT;
		SeCaptureSubjectContext(&subjcont);
		psubjcont = &subjcont;
	}

	void *sd = NULL;
	Status = SeAssignSecurityEx(vnode_security(dvp),
	    access_state ? access_state->SecurityDescriptor : NULL,
	    (void**)&sd,
	    NULL, vnode_isdir(vp)?TRUE:FALSE, flags,
	    psubjcont, IoGetFileObjectGenericMapping(), PagedPool);

	if (Status != STATUS_SUCCESS)
		goto err;

	// what OwnerID did we get?
	Status = RtlGetOwnerSecurityDescriptor(sd,
	    (PSID *)&usersid, &defaulted);
	if (!NT_SUCCESS(Status)) {
		dprintf("RtlGetOwnerSecurityDescriptor returned %08lx\n",
		    Status);
	} else {
		zp->z_uid = zfs_sid2uid(usersid);
	}

	// what GroupID did we get?
	Status = RtlGetGroupSecurityDescriptor(sd,
	    (PSID *)&groupsid, &defaulted);
	if (!NT_SUCCESS(Status)) {
		dprintf("RtlGetGroupSecurityDescriptor returned %08lx\n",
		    Status);
	} else {
		zp->z_gid = zfs_sid2gid(groupsid);
	}

	find_set_gid(vp, dvp, &subjcont);
	dprintf("%s: set uid %llu gid %llu\n", __func__,
	    zp->z_uid, zp->z_gid);
#if 0
	if (usersid)
		dump_sid(usersid);
	if (groupsid)
		dump_sid(groupsid);
#endif

	/* Why do we do this rel -> abs -> rel ? */
	ULONG buflen;
	PSECURITY_DESCRIPTOR *abssd;
	PSECURITY_DESCRIPTOR newsd;
	PACL dacl, sacl;
	PSID owner, group;
	ULONG abssdlen = 0, dacllen = 0, sacllen = 0, ownerlen = 0,
	    grouplen = 0;

	Status = RtlSelfRelativeToAbsoluteSD(sd, NULL, &abssdlen, NULL,
	    &dacllen, NULL, &sacllen, NULL, &ownerlen, NULL, &grouplen);
	if (!NT_SUCCESS(Status) && Status != STATUS_BUFFER_TOO_SMALL) {
		dprintf("RtlSelfRelativeToAbsoluteSD returned %08lx\n", Status);
		goto err;
	}

	if (abssdlen + dacllen + sacllen + ownerlen + grouplen == 0) {
		dprintf("RtlSelfRelativeToAbsoluteSD returned zero lengths\n");
		goto err;
	}

	buf = (uint8_t *)ExAllocatePoolWithTag(PagedPool, abssdlen + dacllen +
	    sacllen + ownerlen + grouplen, 'ZACL');
	if (!buf) {
		dprintf("out of memory\n");
		goto err;
	}

	abssd = (PSECURITY_DESCRIPTOR)buf;
	dacl = (PACL)(buf + abssdlen);
	sacl = (PACL)(buf + abssdlen + dacllen);
	owner = (PSID)(buf + abssdlen + dacllen + sacllen);
	group = (PSID)(buf + abssdlen + dacllen + sacllen + ownerlen);

	Status = RtlSelfRelativeToAbsoluteSD(sd, abssd, &abssdlen, dacl,
	    &dacllen, sacl, &sacllen, owner, &ownerlen, group, &grouplen);
	if (!NT_SUCCESS(Status) && Status != STATUS_BUFFER_TOO_SMALL) {
		dprintf("RtlSelfRelativeToAbsoluteSD returned %08lx\n", Status);
		goto err;
	}

	// Only convert over uid if SD does not contain OwnerID
	if (usersid == NULL) {
		zfs_uid2sid(zp->z_uid, &usersid);
		if (!usersid) {
			dprintf("uid_to_sid returned %08lx\n", Status);
			Status = STATUS_NO_MEMORY;
			goto err;
		}
		allocated_usersid = B_TRUE;
	}
	RtlSetOwnerSecurityDescriptor(abssd, usersid, FALSE);

	if (groupsid == NULL) {
		zfs_gid2sid(zp->z_gid, &groupsid);
		if (!groupsid) {
			dprintf("out of memory\n");
			Status = STATUS_NO_MEMORY;
			goto err;
		}
		allocated_groupsid = B_TRUE;
	}
	RtlSetGroupSecurityDescriptor(abssd, groupsid, FALSE);

	buflen = 0;

	Status = RtlAbsoluteToSelfRelativeSD(abssd, NULL, &buflen);
	if (!NT_SUCCESS(Status) && Status != STATUS_BUFFER_TOO_SMALL) {
		dprintf("RtlAbsoluteToSelfRelativeSD returned %08lx\n", Status);
		goto err;
	}

	if (buflen == 0) {
		dprintf("RtlAbsoluteToSelfRelativeSD returned size of 0\n");
		Status = STATUS_INVALID_PARAMETER;
		goto err;
	}

	newsd = ExAllocatePoolWithTag(PagedPool, buflen, 'ZACL');
	if (!newsd) {
		dprintf("out of memory\n");
		Status = STATUS_NO_MEMORY;
		goto err;
	}

	Status = RtlAbsoluteToSelfRelativeSD(abssd, newsd, &buflen);
	if (!NT_SUCCESS(Status)) {
		dprintf("RtlAbsoluteToSelfRelativeSD returned %08lx\n", Status);
		ExFreePool(newsd);
		goto err;
	}

	void *oldsd = vnode_security(vp);
	vnode_setsecurity(vp, newsd);
	if (oldsd)
		ExFreePool(oldsd);

	zfs_save_ntsecurity(vp);

err:

	if (dvp)
		VN_RELE(dvp);
	zfs_exit(zfsvfs, FTAG);

	if (psubjcont == &subjcont)
		SeReleaseSubjectContext(&subjcont);

	if (buf != NULL)
		ExFreePool(buf);
	if (allocated_usersid)
		zfs_freesid(usersid);
	if (allocated_groupsid)
		zfs_freesid(groupsid);
	return (Status);
}

// return true if xattr is a stream (name ends with ":$DATA")
int
xattr_stream(const char *name)
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

	// Fake ctrldir nodes can't have xattr
	if (zfsctl_is_node(zp))
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
 * try harder to get parent, including looking it up.
 * this will return with iocount held on dvp.
 * call, VN_RELE() when done.
 */
struct vnode *
zfs_parent(struct vnode *vp)
{
	struct vnode *dvp;

	if (vp == NULL)
		return (NULL);

	/* easy, do we have it? */
	dvp = vnode_parent(vp);
	if (dvp != NULL) {
		if (VN_HOLD(dvp) == 0)
			return (dvp);
		else
			return (NULL);
	}
	/* .. then look it up */
	znode_t *zp = VTOZ(vp);

	if (zp == NULL)
		return (NULL);

	if (zfsctl_is_node(zp))
		return (zfs_root_dotdot(vp));

	zfsvfs_t *zfsvfs = zp->z_zfsvfs;
	uint64_t parent;

	VERIFY(sa_lookup(zp->z_sa_hdl, SA_ZPL_PARENT(zfsvfs),
	    &parent, sizeof (parent)) == 0);

	znode_t *dzp;
	int error;

	error = zfs_zget(zfsvfs, parent, &dzp);
	if (error)
		return (NULL);

	/*
	 * Tempting as it may be, we can not
	 * call setparent() here as there is
	 * nothing to release parent usecount.
	 */
	// vnode_setparent(vp, ZTOV(dzp));
	return (ZTOV(dzp));
}

/*
 * Call vnode_setunlink if zfs_zaccess_delete() allows it
 * TODO: provide credentials
 * deleteonclose is the open flag, which is a softer delete, and
 * only actioned on fileobject_cleanup. If false, we are from
 * set_disposition, which is more instant, and global.
 */
NTSTATUS
zfs_setunlink(FILE_OBJECT *fo, vnode_t *dvp, boolean_t deleteonclose)
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
	zfs_ccb_t *zccb = fo->FsContext2;

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

	// If it is a reparsepoint, deny if used as mountpoint
	if ((zp->z_pflags & ZFS_REPARSE) && zccb &&
	    vfs_has_mount(zccb->z_name_cache)) {
		Status = STATUS_ACCESS_DENIED;
		goto out;
	}

	/*
	 * Do NOT purge/flush the cache section here.  A file about to be
	 * deleted has no future reads to keep coherent, and
	 * CcPurgeCacheSection can block indefinitely (nt!CcCollisionDelay)
	 * waiting out a collision with an unrelated in-flight write-back on
	 * this section -- confirmed live, see the setunlink purge-gate
	 * history.  NTFS, WinBtrfs, and FastFAT's own disposition-set
	 * handlers all agree: only MmFlushImageSection is checked here;
	 * cache cleanup, if any, happens later at IRP_MJ_CLEANUP.
	 *
	 * Block deletion only when a true IMAGE section exists (the file
	 * is loaded as an executable by some process).  When
	 * MmFlushImageSection returns FALSE but ImageSectionObject is
	 * NULL, the refusal comes from a user-mode DATA mapping (e.g.
	 * Windows Defender or Windows Search has the file open via
	 * CreateFileMapping).  Apply POSIX unlink() semantics: mark the
	 * file delete-pending now so new opens fail immediately, but
	 * defer the actual directory removal to IRP_MJ_CLEANUP /
	 * delete_entry() when the last handle closes.  This lets git
	 * unlink() the old .idx while it still holds a read mapping on
	 * it, then rename the new .idx into place.
	 */
	if (!MmFlushImageSection(&vp->SectionObjectPointers,
	    MmFlushForDelete)) {
		if (vp->SectionObjectPointers.ImageSectionObject != NULL) {
			dprintf("%s: cannot delete image-mapped '%wZ'\n",
			    __func__, &fo->FileName);
			Status = STATUS_CANNOT_DELETE;
			goto out;
		}
		dprintf("%s: data-mapped '%wZ' - allowing POSIX-style "
		    "delete-pending (DataSectionObject=%p)\n", __func__,
		    &fo->FileName,
		    vp->SectionObjectPointers.DataSectionObject);
	}

	// Call out_unlock from now on,
	// zfs_parent() holds dvp
	if (dvp == NULL)
		dvp = zfs_parent(vp);
	else
		VN_HOLD(dvp);

	if (dvp == NULL) {
		Status = STATUS_INVALID_PARAMETER;
		goto out_unlock;
	}

	dzp = VTOZ(dvp);

	// If we are root
	if (zp->z_id == zfsvfs->z_root) {
		Status = STATUS_CANNOT_DELETE;
		goto out_unlock;
	}

	// If we are a dir, and have more than "." and "..", we
	// are not empty.
	if (S_ISDIR(zp->z_mode)) {

		if (!zfs_dirempty(zp)) {
			Status = STATUS_DIRECTORY_NOT_EMPTY;
			goto out_unlock;
		}
	}

	int error = 0;

	if (dzp != NULL) {
		/*
		 * Use Windows SeAccessCheck as the authoritative delete check,
		 * mirroring the mkdir path. If the Windows SD grants DELETE on
		 * the file itself or FILE_DELETE_CHILD on the parent, pass an
		 * elevated cred (uid=0) so the ZFS POSIX ACL is bypassed.
		 * This handles pools whose root is uid=0/0755, where a regular
		 * user owns a subdirectory but the POSIX parent write-bit check
		 * would otherwise deny deletion.
		 */
		cred_t elevated_delete_cr = { .cr_uid = 0, .cr_gid = 0 };
		cred_t *delete_cr = NULL;
		PSECURITY_DESCRIPTOR fileSD = vnode_security(vp);
		PSECURITY_DESCRIPTOR parentSD = vnode_security(dvp);
		if (fileSD != NULL || parentSD != NULL) {
			SECURITY_SUBJECT_CONTEXT subject;
			SeCaptureSubjectContext(&subject);
			ACCESS_MASK grantedAccess;
			NTSTATUS accessStatus;
			BOOLEAN winOK = FALSE;
			KPROCESSOR_MODE mode = ExGetPreviousMode();
			if (fileSD != NULL) {
				SeLockSubjectContext(&subject);
				winOK = SeAccessCheck(fileSD, &subject, TRUE,
				    DELETE, 0, NULL,
				    IoGetFileObjectGenericMapping(),
				    mode, &grantedAccess, &accessStatus);
				SeUnlockSubjectContext(&subject);
			}
			if (!winOK && parentSD != NULL) {
				SeLockSubjectContext(&subject);
				winOK = SeAccessCheck(parentSD, &subject, TRUE,
				    FILE_DELETE_CHILD, 0, NULL,
				    IoGetFileObjectGenericMapping(),
				    mode, &grantedAccess, &accessStatus);
				SeUnlockSubjectContext(&subject);
			}
			SeReleaseSubjectContext(&subject);
			if (winOK)
				delete_cr = &elevated_delete_cr;
		}
		/*
		 * If no Windows SD was available for SeAccessCheck, the
		 * handle was still opened with DELETE access (the I/O
		 * Manager enforces this before dispatching
		 * FileDispositionInformation).  Fall back to uid=0 so
		 * secpolicy_vnode_access2() does not deny non-root callers
		 * who legitimately own the file — the same approach used in
		 * delete_entry() / zfs_fileobject_cleanup().
		 */
		if (delete_cr == NULL)
			delete_cr = &elevated_delete_cr;
		error = zfs_zaccess_delete(dzp, zp, delete_cr);
	}

	if (error == 0) {
		ASSERT3P(zccb, !=, NULL);
		if (deleteonclose)
			zccb->deleteonclose = 1;
		else
			vnode_setunlink(vp, DELETE_PENDING);
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

/*
 * "File System Behavior Overview.pdf" page 33
 * Note that if the DELETE_ON_CLOSE flag is specified on
 * a directory with child files or directories, the
 * create operation will succeed, but the delete on close
 * flag will be silently ignored when processing the
 * cleanup IRP and the directory will not be deleted.
 * The create operation can fail marking the file delete
 * on close for any of the following reasons:
 * The file is marked read-only : STATUS_CANNOT_DELETE
 * The volume is marked read-only : STATUS_CANNOT_DELETE
 * The file backs an image section : STATUS_CANNOT_DELETE
 * The link or stream is already in the delete-pending
 * state : STATUS_DELETE_PENDING
 */
NTSTATUS
zfs_setunlink_masked(FILE_OBJECT *fo, vnode_t *dvp)
{
	NTSTATUS Status;
	Status = zfs_setunlink(fo, dvp, B_TRUE);
	switch (Status) {
	case STATUS_CANNOT_DELETE:
	case STATUS_DELETE_PENDING:
		return (Status);
	default:
		return (STATUS_SUCCESS);
	}
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

	if (IrpSp->FileObject == NULL || IrpSp->FileObject->FsContext == NULL ||
	    IrpSp->FileObject->FsContext == NULL)
		return (STATUS_INVALID_PARAMETER);

	PFILE_OBJECT FileObject = IrpSp->FileObject;
	struct vnode *vp = FileObject->FsContext;
	zfs_ccb_t *zccb = FileObject->FsContext2;
	mount_t *zmo = DeviceObject->DeviceExtension;
	ULONG NotifyFilter = 0;

	zfsvfs_t *zfsvfs = NULL;
	if (zmo != NULL &&
	    (zfsvfs = vfs_fsprivate(zmo)) != NULL &&
	    zfsvfs->z_rdonly)
		return (STATUS_MEDIA_WRITE_PROTECTED);

	if (zfsvfs == NULL)
		return (STATUS_INVALID_PARAMETER);

	if (VTOZ(vp) != NULL && VN_HOLD(vp) == 0) {
		FILE_BASIC_INFORMATION *fbi =
		    Irp->AssociatedIrp.SystemBuffer;
		uint64_t unixtime[2] = { 0 };
		znode_t *zp = VTOZ(vp);
		boolean_t changed = FALSE;
		// READONLY etc are part of extended vattr.
		xvattr_t xva = { 0 };
		vattr_t *vap = &xva.xva_vattr;

		if ((fbi->FileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
		    !vnode_isdir(vp)) {
			VN_RELE(vp);
			return (STATUS_INVALID_PARAMETER);
		}

		/* Set to -1 to disable updating this time until CLOSE */
		if (fbi->CreationTime.QuadPart == -1) {
			fbi->CreationTime.QuadPart = 0;
			zccb->user_set_creation_time = TRUE;
		}
		if (fbi->LastAccessTime.QuadPart == -1) {
			fbi->LastAccessTime.QuadPart = 0;
			zccb->user_set_access_time = TRUE;
		}
		if (fbi->LastWriteTime.QuadPart == -1) {
			fbi->LastWriteTime.QuadPart = 0;
			zccb->user_set_write_time = TRUE;
		}
		if (fbi->ChangeTime.QuadPart == -1) {
			fbi->ChangeTime.QuadPart = 0;
			zccb->user_set_change_time = TRUE;
		}

		/* Set to -2 to undo that disable, and update again */
		if (fbi->CreationTime.QuadPart == -2) {
			fbi->CreationTime.QuadPart = 0;
			zccb->user_set_creation_time = FALSE;
		}
		if (fbi->LastAccessTime.QuadPart == -2) {
			fbi->LastAccessTime.QuadPart = 0;
			zccb->user_set_access_time = FALSE;
		}
		if (fbi->LastWriteTime.QuadPart == -2) {
			fbi->LastWriteTime.QuadPart = 0;
			zccb->user_set_write_time = FALSE;
		}
		if (fbi->ChangeTime.QuadPart == -2) {
			fbi->ChangeTime.QuadPart = 0;
			zccb->user_set_change_time = FALSE;
		}

		/*
		 * FastFAT will set ChangeTime, AccessTime, LastWrite "times"
		 * when IRP_CLOSE is received. If one, or all, are set to "-1"
		 * FastFAT will not change the corresponding values in this
		 * set_information() call, as well as, not update them
		 * at IRP_CLOSE. The same skip is applied when values
		 * are set here (since in FastFAT they would be automatically
		 * overwritten by IRP_CLOSE).
		 * ZFS updates most things as we go, so if they are set here
		 * then closed, those values will remain. But it seems you
		 * can then issue a IRP_WRITE, which should not update
		 * the time members, until IRP_CLOSE, or cleared with -2.
		 */

		if (fbi->ChangeTime.QuadPart > 0) {
			TIME_WINDOWS_TO_UNIX(fbi->ChangeTime.QuadPart,
			    unixtime);
			vap->va_change_time.tv_sec = unixtime[0];
			vap->va_change_time.tv_nsec = unixtime[1];
			vap->va_active |= ATTR_CTIME;
			// change time has no notify
			changed = TRUE;
		}

		if (fbi->LastWriteTime.QuadPart > 0) {
			TIME_WINDOWS_TO_UNIX(
			    fbi->LastWriteTime.QuadPart,
			    unixtime);
			vap->va_modify_time.tv_sec = unixtime[0];
			vap->va_modify_time.tv_nsec = unixtime[1];
			vap->va_active |= ATTR_MTIME;
			NotifyFilter |= FILE_NOTIFY_CHANGE_LAST_WRITE;
			changed = TRUE;
		}

		if (fbi->CreationTime.QuadPart > 0) {
			TIME_WINDOWS_TO_UNIX(fbi->CreationTime.QuadPart,
			    unixtime);
			vap->va_create_time.tv_sec = unixtime[0];
			vap->va_create_time.tv_nsec = unixtime[1];
			vap->va_active |= ATTR_CRTIME;  // ATTR_CRTIME
			NotifyFilter |= FILE_NOTIFY_CHANGE_CREATION;
			changed = TRUE;
		}

		if (fbi->LastAccessTime.QuadPart > 0) {
			TIME_WINDOWS_TO_UNIX(
			    fbi->LastAccessTime.QuadPart,
			    zp->z_atime);
			NotifyFilter |= FILE_NOTIFY_CHANGE_LAST_ACCESS;
			changed = TRUE;
		}

		if (fbi->FileAttributes != 0) {

			if (!zccb->user_set_change_time) {
				gethrestime(&vap->va_change_time);
				vap->va_active |= ATTR_CTIME;
			}

			// Changes vap from vattr into xvattr.
			zfs_setwinflags_xva(VTOZ(vp),
			    fbi->FileAttributes, &xva);

			vap->va_active |= ATTR_MODE;
			NotifyFilter |= FILE_NOTIFY_CHANGE_ATTRIBUTES;
			changed = TRUE;
		}

		if (changed) {
			Status = zfs_setattr(zp, vap, 0, NULL);

			/* Unfortunately, ZFS turns on ARCHIVE sometimes */
			if (!(fbi->FileAttributes & FILE_ATTRIBUTE_ARCHIVE) &&
			    (zccb->user_set_change_time ||
			    zccb->user_set_write_time))
				zp->z_pflags &= ~ZFS_ARCHIVE;
		}

		if (NotifyFilter != 0) {
			zfs_send_notify(zp->z_zfsvfs, zccb->z_name_cache,
			    zccb->z_name_offset,
			    NotifyFilter,
			    FILE_ACTION_MODIFIED);
		}

		VN_RELE(vp);
		Status = STATUS_SUCCESS;
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
	zfs_ccb_t *zccb = FileObject->FsContext2;
	mount_t *zmo = DeviceObject->DeviceExtension;
	ULONG flags = 0;

	zfsvfs_t *zfsvfs = NULL;
	if (zmo != NULL &&
	    (zfsvfs = vfs_fsprivate(zmo)) != NULL &&
	    zfsvfs->z_rdonly)
		return (STATUS_MEDIA_WRITE_PROTECTED);

	if (zfsvfs == NULL)
		return (STATUS_INVALID_PARAMETER);

	if (VTOZ(vp) != NULL && VN_HOLD(vp) == 0) {

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
		    &IrpSp->FileObject->FileName);
		Status = STATUS_SUCCESS;
		if (flags & FILE_DISPOSITION_DELETE) {
			Status = zfs_setunlink(IrpSp->FileObject, NULL,
			    B_FALSE);
		} else {
			vnode_setunlink(vp, DELETE_CLEAR);
			FileObject->DeletePending = FALSE;
		}
		// Dirs marked for Deletion should release all
		// pending Notify events
		if (Status == STATUS_SUCCESS &&
		    (flags & FILE_DISPOSITION_DELETE)) {

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
	dprintf("set_file_disposition_information returning %08lx\n", Status);
	return (Status);
}

static NTSTATUS
set_file_endoffile_information_impl(PDEVICE_OBJECT DeviceObject,
    PFILE_OBJECT FileObject, LARGE_INTEGER end_of_file,
    boolean_t advance_only, boolean_t prealloc,
    uint64_t initial_allocation_size)
{
	NTSTATUS Status = STATUS_SUCCESS;
	uint64_t new_end_of_file;
	struct vnode *vp;
	zfs_ccb_t *zccb;
	int changed = 0;
	int error = 0;
	mount_t *zmo = DeviceObject->DeviceExtension;
	CC_FILE_SIZES ccfs;
	LIST_ENTRY rollback;
	boolean_t set_size = B_FALSE;
	boolean_t paging_lock = B_FALSE;
	ULONG filter = 0UL;

	if (FileObject == NULL || FileObject->FsContext == NULL)
		return (STATUS_INVALID_PARAMETER);
	vp = FileObject->FsContext;
	zccb = FileObject->FsContext2;

	zfsvfs_t *zfsvfs = NULL;
	if (zmo != NULL &&
	    (zfsvfs = vfs_fsprivate(zmo)) != NULL &&
	    (zfsvfs->z_rdonly || vfs_isrdonly(zfsvfs->z_vfs) ||
	    !spa_writeable(dmu_objset_spa(zfsvfs->z_os))))
		return (STATUS_MEDIA_WRITE_PROTECTED);

	if (zfsvfs == NULL)
		return (STATUS_INVALID_PARAMETER);

	if (vnode_isdir(vp))
		return (STATUS_INVALID_PARAMETER);

	dprintf("set_file_eof: entry vp=%p cur_file_sz=%llx "
	    "new_eof=%llx advance_only=%d\n",
	    vp,
	    (unsigned long long)vp->FileHeader.FileSize.QuadPart,
	    (unsigned long long)
	    end_of_file.QuadPart,
	    (int)advance_only);

	if ((error = zfs_enter(zfsvfs, FTAG)) != 0)
		return (error);

	if (!VTOZ(vp) || VN_HOLD(vp) != 0) {
		zfs_exit(zfsvfs, FTAG);
		return (STATUS_INVALID_PARAMETER);
	}

	/*
	 * Also exclude concurrent cached-write copies (zfs_write_wrap,
	 * fastio_write) for the duration of the truncate/extend and the
	 * CcSetFileSizes call below.  Those paths hold PagingIoResource
	 * (shared for an in-place copy, exclusive if extending) and then
	 * call into FsRtlCopyWrite/CcCopyWrite -- which, it turns out,
	 * internally acquires FileHeader.Resource itself as part of its
	 * own operation (confirmed live: a fastio_write thread blocked
	 * inside FsRtlCopyWrite's own ExAcquireResourceExclusiveLite on
	 * FileHeader.Resource).  So every copy path's effective order is
	 * PagingIoResource, then (inside Cc) FileHeader.Resource.  This
	 * truncate must acquire in that SAME order -- PagingIoResource
	 * first, FileHeader.Resource second -- or it AB-BA deadlocks
	 * against an in-progress copy: acquiring them in the opposite
	 * order was tried and live-deadlocked against a fastio_write
	 * thread stuck inside FsRtlCopyWrite waiting on FileHeader.
	 * Resource while this function held it and waited on
	 * PagingIoResource.
	 */
	ExAcquireResourceExclusiveLite(vp->FileHeader.PagingIoResource, TRUE);
	paging_lock = B_TRUE;

	ExAcquireResourceExclusiveLite(
	    vp->FileHeader.Resource, TRUE);

	znode_t *zp = VTOZ(vp);

	new_end_of_file = end_of_file.QuadPart;


	/*
	 * The lazy writer sometimes tries to round files to the
	 * next page size through CcSetValidData -
	 * ignore these. See fastfat!FatSetEndOfFileInfo, where
	 * Microsoft does the same as we're doing below.
	 */
	if (advance_only &&
	    end_of_file.QuadPart >=
	    (uint64_t)vp->FileHeader.FileSize.QuadPart)
		new_end_of_file = vp->FileHeader.FileSize.QuadPart;


	// zfs_freesp() will update MTIME when it shouldnt
	// add checks for ccb->user_set_write_time
	if (new_end_of_file < zp->z_size) {

		if (advance_only) {
			Status = STATUS_SUCCESS;
			goto end;
		}

		dprintf("set_file_eof: TRUNCATE vp=%p old=%llx new=%llx\n",
		    vp, (unsigned long long)zp->z_size,
		    (unsigned long long)new_end_of_file);

		if (!MmCanFileBeTruncated(
		    &vp->SectionObjectPointers,
		    &end_of_file)) {
			Status = STATUS_USER_MAPPED_FILE;
			goto end;
		}

		error = zfs_freesp(zp, new_end_of_file,
		    0, 0, B_TRUE); // Len = 0 is truncate

		if (error != 0) {
			Status = zfs_error_to_ntstatus(error);
			dprintf("error - truncate_file failed\n");
			goto end;
		}

	} else if ((new_end_of_file > zp->z_size) && !prealloc) {
		dprintf("extending file to %I64x bytes\n", new_end_of_file);

		error = zfs_freesp(zp,
		    new_end_of_file,
		    0, 0, B_TRUE);
		if (error != 0) {
			Status = zfs_error_to_ntstatus(error);
			dprintf("error - extend_file failed\n");
			goto end;
		}

	} else if (new_end_of_file == zp->z_size && advance_only) {
		Status = STATUS_SUCCESS;
		goto end;
	}

	/* Preserve NtCreateFile's requested allocation on overwrite-open. */
	vp->FileHeader.AllocationSize.QuadPart =
	    MAX(new_end_of_file, initial_allocation_size);
	if (!prealloc) {
		vp->FileHeader.FileSize.QuadPart = new_end_of_file;
		vp->FileHeader.ValidDataLength.QuadPart = new_end_of_file;
	} else if (new_end_of_file <
	    (uint64_t)vp->FileHeader.FileSize.QuadPart) {
		/* Allocation size may not be smaller than EOF. */
		vp->FileHeader.FileSize.QuadPart = new_end_of_file;
		if (vp->FileHeader.ValidDataLength.QuadPart >
		    new_end_of_file)
			vp->FileHeader.ValidDataLength.QuadPart =
			    new_end_of_file;
	}
	ccfs.AllocationSize.QuadPart =
	    vp->FileHeader.AllocationSize.QuadPart;
	ccfs.FileSize.QuadPart =
	    vp->FileHeader.FileSize.QuadPart;
	ccfs.ValidDataLength.QuadPart =
	    vp->FileHeader.ValidDataLength.QuadPart;
	set_size = B_TRUE;

	if (zp->z_pflags & ZFS_XATTR) {
		filter = FILE_NOTIFY_CHANGE_STREAM_SIZE;
		zfs_send_notify(zfsvfs, zccb->z_name_cache,
		    zccb->z_name_offset,
		    filter,
		    FILE_ACTION_MODIFIED_STREAM);
	} else {
		filter = FILE_NOTIFY_CHANGE_SIZE; // if ccb->user_set_write_time
		filter |= FILE_NOTIFY_CHANGE_LAST_WRITE;
		zfs_send_notify(zfsvfs, zccb->z_name_cache,
		    zccb->z_name_offset,
		    filter,
		    FILE_ACTION_MODIFIED);
	}

	Status = STATUS_SUCCESS;

end:

	ExReleaseResource(vp->FileHeader.Resource);

	if (set_size) {
		try {
			CcSetFileSizes(FileObject, &ccfs);
		} except(EXCEPTION_EXECUTE_HANDLER) {
			Status = GetExceptionCode();
		}

		if (!NT_SUCCESS(Status))
			dprintf("CcSetFileSizes threw exception %08lx\n",
			    Status);
	}

	if (paging_lock)
		ExReleaseResourceLite(vp->FileHeader.PagingIoResource);

	VN_RELE(vp);
	zfs_exit(zfsvfs, FTAG);
	return (Status);
}

NTSTATUS
set_file_endoffile_information(PDEVICE_OBJECT DeviceObject, PIRP Irp,
    PIO_STACK_LOCATION IrpSp, boolean_t advance_only, boolean_t prealloc)
{
	if (Irp == NULL || Irp->AssociatedIrp.SystemBuffer == NULL ||
	    IrpSp == NULL)
		return (STATUS_INVALID_PARAMETER);
	FILE_END_OF_FILE_INFORMATION *feofi = Irp->AssociatedIrp.SystemBuffer;
	return (set_file_endoffile_information_impl(DeviceObject,
	    IrpSp->FileObject, feofi->EndOfFile, advance_only, prealloc, 0));
}

NTSTATUS
zfs_truncate_open_file(PDEVICE_OBJECT DeviceObject, PFILE_OBJECT FileObject,
    uint64_t allocation_size)
{
	LARGE_INTEGER end_of_file = { .QuadPart = 0 };
	return (set_file_endoffile_information_impl(DeviceObject, FileObject,
	    end_of_file, B_FALSE, B_FALSE, allocation_size));
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
	boolean_t ExVariant =
	    IrpSp->Parameters.SetFile.FileInformationClass ==
	    FileLinkInformationEx;
	dprintf("* FileLinkInformation%s: %.*S\n",
	    ExVariant ? "Ex" : "",
	    (int)link->FileNameLength / sizeof (WCHAR), link->FileName);

	// So, use FileObject to get VP.
	// Use VP to lookup parent.
	// Use Filename to find destonation dvp, and vp if it exists.
	if (IrpSp->FileObject == NULL || IrpSp->FileObject->FsContext == NULL)
		return (STATUS_INVALID_PARAMETER);

	mount_t *zmo = DeviceObject->DeviceExtension;

	if (zmo == NULL)
		return (STATUS_INVALID_PARAMETER);

	zfsvfs_t *zfsvfs = vfs_fsprivate(zmo);

	if (zfsvfs == NULL)
		return (STATUS_INVALID_PARAMETER);

	if (zfsvfs->z_rdonly)
		return (STATUS_MEDIA_WRITE_PROTECTED);

	FILE_OBJECT *RootFileObject = NULL;
	PFILE_OBJECT FileObject = IrpSp->FileObject;
	struct vnode *fvp = FileObject->FsContext;
	zfs_ccb_t *fccb = FileObject->FsContext2;
	znode_t *zp = VTOZ(fvp);
	znode_t *dzp = NULL;
	int error;
	ULONG outlen;
	char *remainder = NULL;
	char buffer[PATH_MAX], *filename;
	struct vnode *tdvp = NULL, *tvp = NULL, *fdvp = NULL;
	uint64_t parent = 0;

	// If given a RootDirectory Handle, lookup tdvp
	if (link->RootDirectory != 0) {
		if (ObReferenceObjectByHandle(link->RootDirectory,
		    GENERIC_READ, *IoFileObjectType, KernelMode,
		    (void *)&RootFileObject, NULL) != STATUS_SUCCESS) {
			return (STATUS_INVALID_PARAMETER);
		}
		tdvp = RootFileObject->FsContext;
	} else {
		// Name can be absolute, if so use name, otherwise,
		// use vp's parent.
	}

	// Convert incoming filename to utf8
	error = RtlUnicodeToUTF8N(buffer, PATH_MAX - 1, &outlen,
	    link->FileName, link->FileNameLength);

	if (error != STATUS_SUCCESS &&
	    error != STATUS_SOME_NOT_MAPPED) {
		if (RootFileObject)
			ObDereferenceObject(RootFileObject);
		return (STATUS_ILLEGAL_CHARACTER);
	}

	// Output string is only null terminated if input is, so do so now.
	buffer[outlen] = 0;
	filename = buffer;

	if (/* strchr(filename, '/') ||
	    strchr(filename, '\\') || */
	    /* strchr(&colon[2], ':') || there is one at ":$DATA" */
	    !strcasecmp("DOSATTRIB:$DATA", filename) ||
	    !strcasecmp("EA:$DATA", filename) ||
	    !strcasecmp("reparse:$DATA", filename) ||
	    !strcasecmp("casesensitive:$DATA", filename)) {
		if (RootFileObject)
			ObDereferenceObject(RootFileObject);
		return (STATUS_OBJECT_NAME_INVALID);
	}


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
	    &tvp, 0, 0, fccb != NULL ? &fccb->cred : NULL);
	if (error) {
		if (RootFileObject)
			ObDereferenceObject(RootFileObject);
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
	fdvp = ZTOV(dzp);

	VN_HOLD(fvp);
	// "tvp"(if not NULL) and "tdvp" is held by zfs_find_dvp_vp

	if (tvp != NULL &&
	    (!MmFlushImageSection(&tvp->SectionObjectPointers,
	    MmFlushForWrite))) {
		error = STATUS_ACCESS_DENIED;
		goto out;
	}

	boolean_t replace;
#if defined(FILE_LINK_REPLACE_IF_EXISTS)
	if (ExVariant)
		replace = ((FILE_LINK_INFORMATION_EX *)link)->Flags &
		    FILE_LINK_REPLACE_IF_EXISTS;
	else
#endif
		replace = link->ReplaceIfExists;
	error = zfs_link(VTOZ(tdvp), VTOZ(fvp),
	    remainder ? remainder : filename, NULL,
	    replace ? FLINKREPLACE : 0);

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
	} else {
		error = zfs_error_to_ntstatus(error);
	}

	// Release all holds
out:
	if (RootFileObject)
		ObDereferenceObject(RootFileObject);
	if (tdvp)
		VN_RELE(tdvp);
	if (fdvp)
		VN_RELE(fdvp);
	if (fvp)
		VN_RELE(fvp);
	if (tvp)
		VN_RELE(tvp);

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
 *
 * Oh so, we can get a second FileObject in
 * IrpSp->Parameters.SetFile.FileObject, this is the FO of the destination
 * Directory. If it is set, we only use the last part of the filename.
 *
 * It is also allowed to end the name with "\\", then we should strip that.
 */

	FILE_RENAME_INFORMATION *ren = Irp->AssociatedIrp.SystemBuffer;
	dprintf("* FileRenameInformation: %.*S\n",
	    (int)ren->FileNameLength / sizeof (WCHAR), ren->FileName);

	// ASSERT(ren->RootDirectory == NULL);

	// So, use FileObject to get VP.
	// Use VP to lookup parent.
	// Use Filename to find destonation dvp, and vp if it exists.
	if (IrpSp->FileObject == NULL ||
	    IrpSp->FileObject->FsContext == NULL ||
	    IrpSp->FileObject->FsContext2 == NULL)
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
	zfs_ccb_t *fccb = FileObject->FsContext2;
	zfs_ccb_t *tdccb = NULL;
	znode_t *zp = VTOZ(fvp);
	znode_t *dzp = NULL;
	int error;
	ULONG outlen;
	char *remainder = NULL;
	char buffer[PATH_MAX], *filename;
	struct vnode *tdvp = NULL, *tvp = NULL, *fdvp = NULL;
	uint64_t parent = 0;
	PFILE_OBJECT dFileObject = NULL;
	HANDLE destParentHandle = 0;
	int use_fdvp_for_tdvp = 0;

	// Convert incoming filename to utf8
	error = RtlUnicodeToUTF8N(buffer, PATH_MAX - 1, &outlen,
	    ren->FileName, ren->FileNameLength);

	if (error != STATUS_SUCCESS &&
	    error != STATUS_SOME_NOT_MAPPED) {
		return (STATUS_ILLEGAL_CHARACTER);
	}
	SetFlag(FileObject->Flags, FO_FILE_MODIFIED);
	// Output string is only null terminated if input is, so do so now.
	buffer[outlen] = 0;
	filename = buffer;

	// If it ends with "\", strip it.
	if (outlen > 0 &&
	    buffer[outlen - 1] == '\\')
		buffer[outlen - 1] = 0;

	// Filename is often "\??\E:\lower\name" - and "/lower" might be
	// another dataset so we need to drive a lookup, with
	// SL_OPEN_TARGET_DIRECTORY set so we get the parent of where
	// we are renaming to. This will give us "tdvp", and
	// possibly "tvp" is we are to rename over an item.

	/* Quick check to see if it ends in reserved names */
	char *tail;
	tail = strrchr(filename, '\\');
	if (tail == NULL) {
		tail = filename;
	} else {
		tail++;
		filename = tail;
	}

	if (strchr(tail, ':')) {
		dprintf("file rename to stream should be implemented!\n");
		return (STATUS_NOT_IMPLEMENTED);
	}

	/*
	 * Also check for renaming of streams (xattrs here) and apparently
	 * if (fn.Length == 0)
	 *  return rename_stream_to_file(Vcb);
	 * So that's a thing too.
	 */

	if (strchr(tail, ':') ||
	    !strcasecmp("DOSATTRIB", tail) ||
	    !strcasecmp("EA", tail) ||
	    !strcasecmp("reparse", tail) ||
	    !strcasecmp("casesensitive", tail))
		return (STATUS_OBJECT_NAME_INVALID);

	// If given Target Directory FileObject, it is easy
	if (IrpSp->Parameters.SetFile.FileObject != NULL) {

		dFileObject = IrpSp->Parameters.SetFile.FileObject;
		tdvp = dFileObject->FsContext;
		tdccb = dFileObject->FsContext2;

		// We have tdvp held from the FileObject
		VN_HOLD(tdvp);

		error = zfs_find_dvp_vp(zfsvfs, filename, 1, 0, &remainder,
		    &tdvp, &tvp, 0, 0, &fccb->cred);

		dFileObject = NULL; // Dont ObDereference later

		if (error) {
			Status = STATUS_OBJECTID_NOT_FOUND;
			tdvp = NULL; // did not hold, dont rele
			goto out;
		}

	// If it starts with "\" drive the lookup, if it is just a name
	// like "HEAD", assume tdvp is same as fdvp.
	} else if (filename[0] == '\\') {
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
		    (void *)&dFileObject,
		    NULL);
		if (!NT_SUCCESS(Status)) {
			ZwClose(destParentHandle);
			return (STATUS_INVALID_PARAMETER);
		}

		// All exits need to go through "out:" at this point on.

		// Assign tdvp
		tdvp = dFileObject->FsContext;
		tdccb = dFileObject->FsContext2;

		// Filename is '\??\E:\dir\dir\file' and we only care about
		// the last part.
		if (tail)
			filename = tail;

		error = zfs_find_dvp_vp(zfsvfs, filename, 1, 0, &remainder,
		    &tdvp, &tvp, 0, 0, &fccb->cred);
		if (error) {
			Status = STATUS_OBJECTID_NOT_FOUND;
			tdvp = NULL; // did not hold, dont rele
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
#if defined(FILE_RENAME_REPLACE_IF_EXISTS)
	if (tvp && ExVariant && !(ren->Flags&FILE_RENAME_REPLACE_IF_EXISTS)) {
		error = STATUS_OBJECT_NAME_COLLISION;
		goto out;
	}
#endif
	// May not rename over a file that is mmapped.
	if (tvp &&
	    !MmFlushImageSection(&tvp->SectionObjectPointers,
	    MmFlushForWrite)) {
		error = STATUS_ACCESS_DENIED;
		goto out;
	}

	// Fetch parent, and hold
	fdvp = zfs_parent(fvp);

	// "tvp" (if not NULL) and "tdvp" is held by zfs_find_dvp_vp

	if (use_fdvp_for_tdvp) {
		tdvp = fdvp;
		VERIFY0(VN_HOLD(tdvp));
	}

	/*
	 * The IO Manager verifies the FileObject has DELETE access before
	 * dispatching FileRenameInformation, so if we are here Windows has
	 * already performed the access check. Skip the redundant POSIX ACL
	 * check in zfs_rename so non-admin users can rename files/dirs that
	 * Windows has granted them permission to rename.
	 */
	error = zfs_rename(VTOZ(fdvp), &fccb->z_name_cache[fccb->z_name_offset],
	    VTOZ(tdvp), remainder ? remainder : filename, &fccb->cred,
	    FBYPASS_ZFS_ACL, 0, NULL);
	if (error != 0)
		error = zfs_error_to_ntstatus(error);

	if (error == STATUS_SUCCESS) {

		// rename file in same directory:
		// send dir modified, send OLD_NAME, NEW_NAME
		// Moving to different volume:
		// FILE_ACTION_REMOVED, FILE_ACTION_ADDED
		// send CHANGE_LAST_WRITE

		// remember we renamed, so get-filename on already open
		// files can rescan.
		atomic_inc_64(&zp->z_name_renamed);

		znode_t *tdzp = VTOZ(tdvp);

		// NTFS is the weird kid on the block here, if the parents are
		// the same, it uses RENAME event. When the parents are
		// different, it uses REMOVE and ADD.
		// Remember the old name, so we can cluster the notify events
		// together.

		char *old_z_name_cache = fccb->z_name_cache;
		uint32_t old_z_name_offset = fccb->z_name_offset;
		uint32_t old_z_name_len = fccb->z_name_len;

		fccb->z_name_cache = NULL;
		fccb->z_name_offset = 0;
		fccb->z_name_len = 0;

		if (zfs_build_path(zp, tdzp, &fccb->z_name_cache,
		    &fccb->z_name_len, &fccb->z_name_offset) == 0) {

			// Temporarily building parent path is a bit weak.
			// Should we fetch it some other way?
			char *parent_name_cache = NULL;
			uint32_t parent_name_offset = 0;
			uint32_t parent_name_len = 0;
			int parent_name;
			parent_name = zfs_build_path(VTOZ(fdvp), NULL,
			    &parent_name_cache, &parent_name_len,
			    &parent_name_offset);

			// Send out old name.
			zfs_send_notify(zfsvfs, old_z_name_cache,
			    old_z_name_offset,
			    vnode_isdir(fvp) ?
			    FILE_NOTIFY_CHANGE_DIR_NAME :
			    FILE_NOTIFY_CHANGE_FILE_NAME,
			    fdvp == tdvp ? FILE_ACTION_RENAMED_OLD_NAME :
			    FILE_ACTION_REMOVED);

			// Send out new name.
			zfs_send_notify(zfsvfs, fccb->z_name_cache,
			    fccb->z_name_offset,
			    vnode_isdir(fvp) ?
			    FILE_NOTIFY_CHANGE_DIR_NAME :
			    FILE_NOTIFY_CHANGE_FILE_NAME,
			    fdvp == tdvp ? FILE_ACTION_RENAMED_NEW_NAME :
			    FILE_ACTION_ADDED);

			// Also tell parents they were MODIFIED
			if (parent_name == 0) {
				zfs_send_notify(zfsvfs, parent_name_cache,
				    parent_name_offset,
				    FILE_NOTIFY_CHANGE_LAST_WRITE,
				    FILE_ACTION_MODIFIED);

				kmem_free(parent_name_cache, parent_name_len);
			}

			if (tdccb != NULL) {
				zfs_send_notify(zfsvfs, tdccb->z_name_cache,
				    tdccb->z_name_offset,
				    FILE_NOTIFY_CHANGE_LAST_WRITE,
				    FILE_ACTION_MODIFIED);
			}
		}
		// Free old name
		kmem_free(old_z_name_cache, old_z_name_len);
	}

	// Release all holds
	if (error == EBUSY)
		error = STATUS_ACCESS_DENIED;
	if (error == ENAMETOOLONG)
		error = STATUS_NAME_TOO_LONG;
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
	mount_t *zmo = DeviceObject->DeviceExtension;
	int error;
	CC_FILE_SIZES ccfs;
	boolean_t set_size = B_FALSE;

	dprintf("* FileValidDataLengthInformation: \n");

	if (IrpSp->Parameters.SetFile.Length <
	    sizeof (FILE_VALID_DATA_LENGTH_INFORMATION))
		return (STATUS_INVALID_PARAMETER);

	if (IrpSp->FileObject == NULL ||
	    IrpSp->FileObject->FsContext == NULL)
		return (STATUS_INVALID_PARAMETER);

	struct vnode *vp = IrpSp->FileObject->FsContext;
	zfs_ccb_t *ccb = IrpSp->FileObject->FsContext2;
	znode_t *zp = VTOZ(vp);

	if (zmo == NULL || zp == NULL)
		return (STATUS_INVALID_PARAMETER);

	zfsvfs_t *zfsvfs = vfs_fsprivate(zmo);
	if (zfsvfs == NULL)
		return (STATUS_INVALID_PARAMETER);

	if ((error = zfs_enter(zfsvfs, FTAG)) != 0)
		return (error);

	ExAcquireResourceExclusiveLite(vp->FileHeader.Resource, B_TRUE);

	if (zp->z_pflags & ZFS_SPARSE /* FILE_ATTRIBUTE_SPARSE_FILE */) {
		Status = STATUS_INVALID_PARAMETER;
		goto end;
	}

	if (fvdli->ValidDataLength.QuadPart <
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

	if (zp && zp->z_unlinked) {
		Status = STATUS_FILE_CLOSED;
		goto end;
	}

	ccfs.AllocationSize = vp->FileHeader.AllocationSize;
	ccfs.FileSize = vp->FileHeader.FileSize;
	ccfs.ValidDataLength = fvdli->ValidDataLength;
	set_size = B_TRUE;

	zfs_send_notify(zp->z_zfsvfs, ccb->z_name_cache,
	    ccb->z_name_offset,
	    FILE_NOTIFY_CHANGE_SIZE,
	    FILE_ACTION_MODIFIED);

	Status = STATUS_SUCCESS;

end:
	ExReleaseResourceLite(vp->FileHeader.Resource);

	if (set_size) {
		try {
			CcSetFileSizes(IrpSp->FileObject, &ccfs);
		} except(EXCEPTION_EXECUTE_HANDLER) {
			Status = GetExceptionCode();
		}

		if (!NT_SUCCESS(Status)) {
			dprintf("CcSetFileSizes threw exception %08lx\n",
			    Status);
		} else {
			vp->FileHeader.AllocationSize = ccfs.AllocationSize;
		}
	}

	zfs_exit(zfsvfs, FTAG);
	return (Status);
}

NTSTATUS
set_file_case_sensitive_information(PDEVICE_OBJECT DeviceObject, PIRP Irp,
    PIO_STACK_LOCATION IrpSp)
{
	NTSTATUS Status;
	FILE_CASE_SENSITIVE_INFORMATION *fcsi =
	    Irp->AssociatedIrp.SystemBuffer;
	mount_t *zmo = DeviceObject->DeviceExtension;
	int error;

	dprintf("* FileCaseSensitiveInformation: \n");

	if (IrpSp->Parameters.SetFile.Length <
	    sizeof (FILE_CASE_SENSITIVE_INFORMATION))
		return (STATUS_INFO_LENGTH_MISMATCH);

	if (IrpSp->FileObject == NULL ||
	    IrpSp->FileObject->FsContext == NULL)
		return (STATUS_INVALID_PARAMETER);

	struct vnode *vp = IrpSp->FileObject->FsContext;
	zfs_ccb_t *ccb = IrpSp->FileObject->FsContext2;
	znode_t *zp = VTOZ(vp);

	if (zmo == NULL || zp == NULL)
		return (STATUS_INVALID_PARAMETER);

	if (!vnode_isdir(vp))
		return (STATUS_INVALID_PARAMETER);

	zfsvfs_t *zfsvfs = vfs_fsprivate(zmo);
	if (zfsvfs == NULL)
		return (STATUS_INVALID_PARAMETER);

	if ((error = zfs_enter(zfsvfs, FTAG)) != 0)
		return (error);

	xvattr_t xva = { 0 };
	vattr_t *vap = &xva.xva_vattr;
	xoptattr_t *xoap;

	xva_init(&xva);
	xoap = xva_getxoptattr(&xva);

	/*
	 * We need to check if there are case issues in this dir, if
	 * the request is to make it case insensitive again. Return
	 * the code: STATUS_CASE_DIFFERING_NAMES_IN_DIR
	 */
	xoap->xoa_case_sensitive_dir =
	    (fcsi->Flags & FILE_CS_FLAG_CASE_SENSITIVE_DIR);
	XVA_SET_REQ(&xva, XAT_CASESENSITIVEDIR);

	Status = zfs_setattr(zp, vap, 0, NULL);

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
	dprintf("   New CurrentByteOffset: %I64u\n",
	    fpi->CurrentByteOffset.QuadPart);
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
	REPARSE_DATA_BUFFER tagdata = {0};
	struct iovec iov;
	iov.iov_base = (void *)&tagdata;
	iov.iov_len = sizeof (tagdata);

	zfs_uio_t uio;
	zfs_uio_iovec_init(&uio, &iov, 1, 0, UIO_SYSSPACE,
	    sizeof (tagdata), 0);
	err = zfs_readlink(ZTOV(zp), &uio, NULL);
	if (err != 0)
		return (0);
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

		tag->FileAttributes =
		    zfs_getwinflags(zp->z_pflags, vnode_isdir(vp));

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
		zfs_ccb_t *zccb = IrpSp->FileObject->FsContext2;
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

/*
 * Assumes NULL tests, and VN_HOLD(vp) has already been done.
 */
void
file_basic_information_impl(PDEVICE_OBJECT DeviceObject,
    PFILE_OBJECT FileObject, FILE_BASIC_INFORMATION *fbi,
    PIO_STATUS_BLOCK IoStatus)
{
	struct vnode *vp = FileObject->FsContext;
	int error = 0;
	uint64_t fflags = 0;

	if (vp != NULL) {

		// znode_t *zp = VTOZ(vp);
		mount_t *zmo = DeviceObject->DeviceExtension;
		zfsvfs_t *zfsvfs = vfs_fsprivate(zmo);

		xvattr_t xva;
		vattr_t *vap = &xva.xva_vattr;
		xva_init(&xva);

		// XVA_SET_REQ(&xva, XAT_IMMUTABLE);
		// XVA_SET_REQ(&xva, XAT_APPENDONLY);
		// XVA_SET_REQ(&xva, XAT_NOUNLINK);
		// XVA_SET_REQ(&xva, XAT_NODUMP);
		// XVA_SET_REQ(&xva, XAT_REPARSE);
		// XVA_SET_REQ(&xva, XAT_OFFLINE);
		XVA_SET_REQ(&xva, XAT_READONLY);
		XVA_SET_REQ(&xva, XAT_HIDDEN);
		XVA_SET_REQ(&xva, XAT_SYSTEM);
		XVA_SET_REQ(&xva, XAT_ARCHIVE);
		XVA_SET_REQ(&xva, XAT_SPARSE);
		XVA_SET_REQ(&xva, XAT_REPARSE);

		vap->va_mask |=
		    (ATTR_MTIME | ATTR_CTIME | ATTR_CRTIME | ATTR_ATIME);

		error = zfs_getattr(vp, (vattr_t *)&xva, 0, NULL, NULL);

#define	FLAG_CHECK(fflag, xflag, xfield) do { \
	if (XVA_ISSET_RTN(&xva, (xflag)) && (xfield) != 0) \
		fflags |= (fflag); \
} while (0)

		FLAG_CHECK(FILE_ATTRIBUTE_READONLY, XAT_READONLY,
		    xva.xva_xoptattrs.xoa_readonly);
		FLAG_CHECK(FILE_ATTRIBUTE_HIDDEN, XAT_HIDDEN,
		    xva.xva_xoptattrs.xoa_hidden);
		FLAG_CHECK(FILE_ATTRIBUTE_SYSTEM, XAT_SYSTEM,
		    xva.xva_xoptattrs.xoa_system);
		FLAG_CHECK(FILE_ATTRIBUTE_ARCHIVE, XAT_ARCHIVE,
		    xva.xva_xoptattrs.xoa_archive);
		FLAG_CHECK(FILE_ATTRIBUTE_SPARSE_FILE, XAT_SPARSE,
		    xva.xva_xoptattrs.xoa_sparse);
		FLAG_CHECK(FILE_ATTRIBUTE_REPARSE_POINT, XAT_REPARSE,
		    xva.xva_xoptattrs.xoa_reparse);

#undef FLAG_CHECK

		if (vnode_isdir(vp)) {
			fflags |= FILE_ATTRIBUTE_DIRECTORY;
			fflags &= ~FILE_ATTRIBUTE_ARCHIVE;
		}

		if (fflags == 0)
			fflags = FILE_ATTRIBUTE_NORMAL;

		fbi->FileAttributes = fflags;

		TIME_UNIX_TO_WINDOWS_EX(vap->va_modify_time.tv_sec,
		    vap->va_modify_time.tv_nsec, fbi->LastWriteTime.QuadPart);
		TIME_UNIX_TO_WINDOWS_EX(vap->va_change_time.tv_sec,
		    vap->va_change_time.tv_nsec, fbi->ChangeTime.QuadPart);
		TIME_UNIX_TO_WINDOWS_EX(vap->va_create_time.tv_sec,
		    vap->va_create_time.tv_nsec, fbi->CreationTime.QuadPart);
		TIME_UNIX_TO_WINDOWS_EX(vap->va_access_time.tv_sec,
		    vap->va_access_time.tv_nsec, fbi->LastAccessTime.QuadPart);

		IoStatus->Information = sizeof (FILE_BASIC_INFORMATION);
		IoStatus->Status = STATUS_SUCCESS;
		return;
	}

	// This can be called from diskDispatcher, referring to the volume.
	// if so, make something up. Is this the right thing to do?
	// zp is NULL

	LARGE_INTEGER JanOne1980 = { 0xe1d58000, 0x01a8e79f };
	ExLocalTimeToSystemTime(&JanOne1980,
	    &fbi->LastWriteTime);
	fbi->CreationTime = fbi->LastAccessTime =
	    fbi->LastWriteTime;
	fbi->FileAttributes = FILE_ATTRIBUTE_DIRECTORY;
	IoStatus->Information = sizeof (FILE_BASIC_INFORMATION);
	IoStatus->Status = STATUS_SUCCESS;
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

#if 0
	if (IrpSp->FileObject == NULL ||
	    IrpSp->FileObject->FsContext == NULL ||
	    IrpSp->FileObject->FsContext2 == NULL) {
		Irp->IoStatus.Information = 0;
		Irp->IoStatus.Status = STATUS_INVALID_PARAMETER;
		return (Irp->IoStatus.Status);
	}
#endif
	// This function relies on VN_HOLD in dispatcher.
	file_basic_information_impl(DeviceObject, IrpSp->FileObject,
	    basic, &Irp->IoStatus);

	return (Irp->IoStatus.Status);
}

NTSTATUS
file_compression_information(PDEVICE_OBJECT DeviceObject, PIRP Irp,
    PIO_STACK_LOCATION IrpSp, FILE_COMPRESSION_INFORMATION *fci)
{
	if (IrpSp->Parameters.QueryFile.Length <
	    sizeof (FILE_COMPRESSION_INFORMATION)) {
		Irp->IoStatus.Information =
		    sizeof (FILE_COMPRESSION_INFORMATION);
		return (STATUS_BUFFER_TOO_SMALL);
	}

	if (IrpSp->FileObject == NULL ||
	    IrpSp->FileObject->FsContext == NULL)
		return (STATUS_INVALID_PARAMETER);

	struct vnode *vp = IrpSp->FileObject->FsContext;
	if (VN_HOLD(vp) != 0)
		return (STATUS_INVALID_PARAMETER);

	znode_t *zp = VTOZ(vp);
	memset(fci, 0, sizeof (FILE_COMPRESSION_INFORMATION));

	dmu_object_info_t doi;
	boolean_t compressed = B_FALSE;
	uint64_t phys_bytes = 0;

	objset_t *os = zp->z_zfsvfs->z_os;
	boolean_t ds_compressed = (os != NULL &&
	    os->os_compress != ZIO_COMPRESS_OFF &&
	    os->os_compress != ZIO_COMPRESS_EMPTY);

	if (zp->z_sa_hdl != NULL) {

		dmu_object_info_from_db(sa_get_db(zp->z_sa_hdl), &doi);
		compressed = ds_compressed;
		phys_bytes = doi.doi_physical_blocks_512 * 512ULL;
		fci->CompressedFileSize.QuadPart =
		    (int64_t)(compressed ? phys_bytes : zp->z_size);
		fci->CompressionFormat = compressed ?
		    COMPRESSION_FORMAT_DEFAULT : COMPRESSION_FORMAT_NONE;
		/*
		 * CompressionUnitShift: log2 of the compression unit size.
		 * highbit64 returns 1-based position of highest set bit, so
		 * subtract 1 to get log2.
		 */
		fci->CompressionUnitShift =
		    (UCHAR)(highbit64(doi.doi_data_block_size) - 1);
		fci->ChunkShift = fci->CompressionUnitShift;
		fci->ClusterShift = fci->CompressionUnitShift;
	} else {
		/* Fallback: no object info, report uncompressed */
		fci->CompressedFileSize.QuadPart = (int64_t)zp->z_size;
	}

	dprintf("%s: phys=%llu logical=%llu fmt=%u\n", __func__,
	    (unsigned long long)phys_bytes,
	    (unsigned long long)zp->z_size, fci->CompressionFormat);

	VN_RELE(vp);
	Irp->IoStatus.Information = sizeof (FILE_COMPRESSION_INFORMATION);
	return (STATUS_SUCCESS);
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

uint64_t
allocationsize(znode_t *zp)
{
	if (S_ISDIR(zp->z_mode))
		return (0ULL);

	if (zp->z_size == 0) {
		struct vnode *vp = ZTOV(zp);
		if (vp != NULL &&
		    vp->FileHeader.AllocationSize.QuadPart > 0ULL)
			return (vp->FileHeader.AllocationSize.QuadPart);
		return (0ULL);
	}

	return (P2ROUNDUP(zp->z_size, zfs_blksz(zp)));
}

void
file_standard_information_impl(PDEVICE_OBJECT DeviceObject,
    PFILE_OBJECT FileObject, FILE_STANDARD_INFORMATION *fsi,
    size_t length, PIO_STATUS_BLOCK IoStatus)
{
	struct vnode *vp = FileObject->FsContext;
	zfs_ccb_t *zccb = FileObject->FsContext2;
	znode_t *zp = VTOZ(vp);
	mount_t *zmo = DeviceObject->DeviceExtension;
	zfsvfs_t *zfsvfs = vfs_fsprivate(zmo);
	int error = 0;

	if (zp != NULL) {
		boolean_t is_ads = (zp->z_pflags & ZFS_XATTR) != 0;

		VN_HOLD(vp);

		/*
		 * For ADS (xattr) nodes, EndOfFile and AllocationSize must
		 * reflect the stream's own size, not the parent file's size.
		 * Only look up the parent for link-count and delete-pending,
		 * which are properties of the parent file object.
		 */
		fsi->AllocationSize.QuadPart = allocationsize(zp);
		fsi->EndOfFile.QuadPart = vnode_isdir(vp) ? 0 : zp->z_size;
		fsi->Directory = vnode_isdir(vp) ? TRUE : FALSE;

		if (is_ads && zccb && zccb->real_file_id > 0) {
			znode_t *pzp = NULL;
			if (zfs_zget(zp->z_zfsvfs, zccb->real_file_id,
			    &pzp) == 0) {
				fsi->NumberOfLinks =
				    vnode_unlink(ZTOV(pzp)) ? 0 :
				    DIR_LINKS(pzp);
				fsi->DeletePending =
				    vnode_unlink(ZTOV(pzp)) ? TRUE : FALSE;
				zrele(pzp);
			} else {
				fsi->NumberOfLinks = 1;
				fsi->DeletePending = FALSE;
			}
		} else {
			fsi->NumberOfLinks =
			    vnode_unlink(vp) ? 0 : DIR_LINKS(zp);
			fsi->DeletePending = vnode_unlink(vp) ? TRUE : FALSE;
		}

		IoStatus->Information = sizeof (FILE_STANDARD_INFORMATION);

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
		if (length >= sizeof (FILE_STANDARD_INFORMATION_EX)) {
			FILE_STANDARD_INFORMATION_EX *estandard;
			estandard = (FILE_STANDARD_INFORMATION_EX *)fsi;
			estandard->AlternateStream = is_ads;
			estandard->MetadataAttribute = FALSE;
			IoStatus->Information =
			    sizeof (FILE_STANDARD_INFORMATION_EX);
		}

		VN_RELE(vp);
		IoStatus->Status = STATUS_SUCCESS;
		return;
	}

	IoStatus->Information = 0;
	IoStatus->Status = STATUS_INVALID_PARAMETER;
}

NTSTATUS
file_standard_information(PDEVICE_OBJECT DeviceObject, PIRP Irp,
    PIO_STACK_LOCATION IrpSp, FILE_STANDARD_INFORMATION *fsi)
{
	dprintf("   %s\n", __func__);

	if (IrpSp->Parameters.QueryFile.Length <
	    sizeof (FILE_STANDARD_INFORMATION)) {
		Irp->IoStatus.Information = sizeof (FILE_STANDARD_INFORMATION);
		return (STATUS_BUFFER_TOO_SMALL);
	}
	if (IrpSp->FileObject == NULL ||
	    IrpSp->FileObject->FsContext == NULL ||
	    IrpSp->FileObject->FsContext2 == NULL) {
		Irp->IoStatus.Information = 0;
		Irp->IoStatus.Status = STATUS_INVALID_PARAMETER;
		return (Irp->IoStatus.Status);
	}

	// This function relies on VN_HOLD in dispatcher.
	file_standard_information_impl(DeviceObject, IrpSp->FileObject,
	    fsi, IrpSp->Parameters.QueryFile.Length, &Irp->IoStatus);

	return (Irp->IoStatus.Status);
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

	fai->AlignmentRequirement = FILE_WORD_ALIGNMENT;
	return (STATUS_SUCCESS);
}

void
file_network_open_information_impl(PDEVICE_OBJECT DeviceObject,
    PFILE_OBJECT FileObject, vnode_t *vp,
    FILE_NETWORK_OPEN_INFORMATION *netopen,
    PIO_STATUS_BLOCK IoStatus)
{
	dprintf("   %s\n", __func__);
	znode_t *zp = VTOZ(vp);
	mount_t *zmo = DeviceObject->DeviceExtension;
	zfsvfs_t *zfsvfs = vfs_fsprivate(zmo);

	if (zp != NULL) {
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
		    allocationsize(zp);
		netopen->EndOfFile.QuadPart = vnode_isdir(vp) ? 0 : zp->z_size;
		netopen->FileAttributes =
		    zfs_getwinflags(zp->z_pflags, vnode_isdir(vp));
		IoStatus->Information =
		    sizeof (FILE_NETWORK_OPEN_INFORMATION);
		IoStatus->Status = STATUS_SUCCESS;
		return;
	}

	IoStatus->Information = 0;
	IoStatus->Status = STATUS_INVALID_PARAMETER;
}

NTSTATUS
file_network_open_information(PDEVICE_OBJECT DeviceObject, PIRP Irp,
    PIO_STACK_LOCATION IrpSp, FILE_NETWORK_OPEN_INFORMATION *netopen)
{
	if (IrpSp->FileObject == NULL ||
	    IrpSp->FileObject->FsContext == NULL ||
	    IrpSp->FileObject->FsContext2 == NULL) {
		Irp->IoStatus.Information = 0;
		Irp->IoStatus.Status = STATUS_INVALID_PARAMETER;
		return (Irp->IoStatus.Status);
	}

	// This function relies on VN_HOLD in dispatcher.
	file_network_open_information_impl(DeviceObject, IrpSp->FileObject,
	    IrpSp->FileObject->FsContext, netopen, &Irp->IoStatus);

	return (Irp->IoStatus.Status);
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
		int error = 0;
		struct vnode *vp = IrpSp->FileObject->FsContext;
		zfs_ccb_t *zccb = IrpSp->FileObject->FsContext2;

		znode_t *zp = VTOZ(vp);

		// If we are a steam, we need to grab the parent,
		// either way, we hold iocount.
		if (zccb && (zp->z_pflags & ZFS_XATTR) &&
		    (zccb->real_file_id > 0)) {
			error = zfs_zget(zp->z_zfsvfs, zccb->real_file_id, &zp);
			if (!error)
				vp = ZTOV(zp);
			else
				VN_HOLD(vp); // ah well, fallback
		} else {
			VN_HOLD(vp);
		}

		// Set it again, in case zget fails, and zp = NULL.
		zp = VTOZ(vp);

		fsli->NumberOfAccessibleLinks =
		    vnode_unlink(vp) ? 0 : DIR_LINKS(zp);
		fsli->TotalNumberOfLinks = DIR_LINKS(zp);
		fsli->DeletePending = vnode_unlink(vp) ? TRUE : FALSE;
		fsli->Directory = S_ISDIR(zp->z_mode);

		VN_RELE(vp);
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

		if (zp && (zp->z_pflags & ZFS_CASESENSITIVEDIR))
			fcsi->Flags |= FILE_CS_FLAG_CASE_SENSITIVE_DIR;
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
	zfs_ccb_t *zccb = FileObject->FsContext2;

	if (vp && zccb) {

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
		    allocationsize(zp);
		fsi->EndOfFile.QuadPart = zp->z_size;
		fsi->FileAttributes =
		    zfs_getwinflags(zp->z_pflags, vnode_isdir(vp));
		fsi->ReparseTag = get_reparse_tag(zp);
		fsi->NumberOfLinks = DIR_LINKS(zp);
		fsi->EffectiveAccess = zccb->access;
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
	zfs_ccb_t *zccb = FileObject->FsContext2;

	if (vp && zccb) {
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
		    allocationsize(zp);
		fsli->EndOfFile.QuadPart = zp->z_size;
		fsli->FileAttributes =
		    zfs_getwinflags(zp->z_pflags, vnode_isdir(vp));
		fsli->ReparseTag = get_reparse_tag(zp);
		fsli->NumberOfLinks = DIR_LINKS(zp);
		fsli->EffectiveAccess = zccb->access;

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

NTSTATUS
file_stat_basic_information(PDEVICE_OBJECT DeviceObject, PIRP Irp,
    PIO_STACK_LOCATION IrpSp, FILE_STAT_BASIC_INFORMATION *fsbi)
{
	PFILE_OBJECT FileObject = IrpSp->FileObject;

	dprintf("   %s\n", __func__);

	struct vnode *vp = FileObject->FsContext;
	if (vp == NULL)
		return (STATUS_SUCCESS);

	znode_t *zp = VTOZ(vp);
	if (zp == NULL)
		return (STATUS_SUCCESS);

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

		TIME_UNIX_TO_WINDOWS(crtime, fsbi->CreationTime.QuadPart);
		TIME_UNIX_TO_WINDOWS(zp->z_atime,
		    fsbi->LastAccessTime.QuadPart);
		TIME_UNIX_TO_WINDOWS(mtime, fsbi->LastWriteTime.QuadPart);
		TIME_UNIX_TO_WINDOWS(ctime, fsbi->ChangeTime.QuadPart);
	}

	fsbi->FileId.QuadPart = zp->z_id;
	fsbi->AllocationSize.QuadPart = allocationsize(zp);
	fsbi->EndOfFile.QuadPart = zp->z_size;
	fsbi->FileAttributes =
	    zfs_getwinflags(zp->z_pflags, vnode_isdir(vp));
	fsbi->ReparseTag = get_reparse_tag(zp);
	fsbi->NumberOfLinks = DIR_LINKS(zp);
	fsbi->DeviceType = FILE_DEVICE_DISK;
	fsbi->DeviceCharacteristics = FILE_DEVICE_IS_MOUNTED;
	fsbi->Reserved = 0;
	fsbi->VolumeSerialNumber.QuadPart = 0x19831116;

	RtlCopyMemory(&fsbi->FileId128.Identifier[0], &zp->z_id,
	    sizeof (UINT64));
	uint64_t guid = dmu_objset_fsid_guid(zfsvfs->z_os);
	RtlCopyMemory(&fsbi->FileId128.Identifier[sizeof (UINT64)],
	    &guid, sizeof (UINT64));

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
	zfs_ccb_t *zccb = FileObject->FsContext2;
	char strname[MAXPATHLEN + 2];
	int error = 0;
	uint64_t parent = 0;

	mount_t *mp = VTOM(vp);
	zfsvfs_t *zfsvfs = vfs_fsprivate(mp);
	if (zfsvfs == NULL || zfsvfs->z_unmounted ||
	    (vfs_flags(mp) & MNT_UNMOUNTING))
		return (STATUS_VOLUME_DISMOUNTED);

	znode_t *zp = VTOZ(vp);
	if (zp == NULL)
		return (STATUS_VOLUME_DISMOUNTED);
	NTSTATUS Status = STATUS_SUCCESS;

	VN_HOLD(vp);

	if (zp->z_id == zfsvfs->z_root) {
		strlcpy(strname, "\\", MAXPATHLEN);
	} else {

		// Should never be unset, but detect renames
		if ((zccb->z_name_cache == NULL) ||
		    (zp->z_name_renamed > zccb->z_name_rename)) {

			zccb->z_name_rename = zp->z_name_renamed;

			dprintf("%s: name not set path taken\n", __func__);
			if (zfs_build_path(zp, NULL, &zccb->z_name_cache,
			    &zccb->z_name_len, &zccb->z_name_offset) == -1) {
				dprintf("%s: failed to build fullpath\n",
				    __func__);
				// VN_RELE(vp);
				// return (STATUS_OBJECT_PATH_NOT_FOUND);
			}
		}

		// Safety
		if (zccb->z_name_cache != NULL) {
			// Full path name
			strlcpy(strname, zccb->z_name_cache,
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
	zfs_ccb_t *zccb = FileObject->FsContext2;
	znode_t *zp = VTOZ(vp), *xzp = NULL;
	znode_t *xdzp = NULL;
	zfsvfs_t *zfsvfs = zp->z_zfsvfs;

	if (zp == NULL)
		return (STATUS_INVALID_PARAMETER);

	// This exits when unmounting
	if ((error = zfs_enter(zfsvfs, FTAG)) != 0)
		return (error);

	struct vnode *xdvp = NULL;
	void *cr = NULL;
	uint64_t spaceused = 0;
	zap_cursor_t  zc;
	objset_t  *os;
	zap_attribute_t *za;

	// Iterate the xattrs.
	// Windows can call this on a stream zp, in this case, we
	// need to go find the real parent, and iterate on that.
	if (zccb && (zp->z_pflags & ZFS_XATTR) &&
	    (zccb->real_file_id > 0)) {

		error = zfs_zget(zfsvfs, zccb->real_file_id, &zp);
		if (error)
			goto exit;
		vp = ZTOV(zp);
	} else {
		VN_HOLD(vp);
	}

	/*
	 * For regular files, insert the default "::$DATA" stream entry.
	 * Directories have no default data stream (matches NTFS behaviour:
	 * "dir /r" shows no streams for directories).  The empty-name ""
	 * that was previously passed here also caused a buffer-underrun in
	 * zfswin_insert_streamname (strlen("") == 0, index -6 access).
	 */
	if (!vnode_isdir(vp))
		overflow = zfswin_insert_streamname(":$DATA", outbuffer,
		    &previous_stream, availablebytes, &spaceused,
		    zp->z_size);

	/* Grab the hidden attribute directory vnode. */
	if (zfs_get_xattrdir(zp, &xdzp, cr, 0) != 0) {
		goto out;
	}

	xdvp = ZTOV(xdzp);
	os = zfsvfs->z_os;

	stream = (FILE_STREAM_INFORMATION *)outbuffer;
	za = zap_attribute_alloc();

	for (zap_cursor_init(&zc, os, xdzp->z_id);
	    zap_cursor_retrieve(&zc, za) == 0; zap_cursor_advance(&zc)) {

		if (!xattr_stream(za->za_name))
			continue;	 /* skip */

		// We need to lookup the size of the xattr.
		int error = zfs_dirlook(xdzp, za->za_name, &xzp, 0,
		    NULL, NULL);

		overflow += zfswin_insert_streamname(za->za_name, outbuffer,
		    &previous_stream, availablebytes, &spaceused,
		    xzp ? xzp->z_size : 0);

		if (error == 0)
			zrele(xzp);

	}

	zap_cursor_fini(&zc);
	zap_attribute_free(za);

out:
	if (xdvp) {
		VN_RELE(xdvp);
	}

	zrele(zp);

exit:
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
	uint64_t bytes_needed;
	mount_t *zmo;
	int error;
	zmo = (mount_t *)DeviceObject->DeviceExtension;
	zfsvfs_t *zfsvfs = vfs_fsprivate(zmo);
	znode_t *zp = NULL;
	ULONG namelenholder = 0;
	FILE_LINK_ENTRY_INFORMATION *flei;

	if (zfsvfs == NULL)
		return (SET_ERROR(STATUS_INVALID_PARAMETER));

	dprintf("%s: \n", __func__);

	if (FileObject == NULL ||
	    FileObject->FsContext == NULL ||
	    FileObject->FsContext2 == NULL)
		return (SET_ERROR(STATUS_INVALID_PARAMETER));

	if (IrpSp->Parameters.QueryFile.Length <
	    sizeof (FILE_LINKS_INFORMATION)) {
		Irp->IoStatus.Information = sizeof (FILE_LINKS_INFORMATION);
		return (SET_ERROR(STATUS_BUFFER_TOO_SMALL));
	}

	struct vnode *vp = FileObject->FsContext;
	struct vnode *dvp = NULL;
	zfs_ccb_t *zccb = FileObject->FsContext2;

	bytes_needed = offsetof(FILE_LINKS_INFORMATION, Entry);

	zp = VTOZ(vp);

	if ((zp == NULL) || (zccb == NULL) || (zccb->z_name_cache == NULL))
		return (SET_ERROR(STATUS_INVALID_PARAMETER));

	// This exits when unmounting
	if ((error = zfs_enter(zfsvfs, FTAG)) != 0)
		return (error);

	dvp = zfs_parent(vp);

	char *name = &zccb->z_name_cache[zccb->z_name_offset];
	Status = RtlUTF8ToUnicodeN(NULL, 0, &namelenholder,
	    name,
	    strlen(name)); /* zp->z_name_len - zp->z_name_offset */

	bytes_needed += sizeof (FILE_LINK_ENTRY_INFORMATION) +
	    namelenholder - sizeof (WCHAR);

	fhli->BytesNeeded = bytes_needed;
	Irp->IoStatus.Information = sizeof (FILE_LINKS_INFORMATION);

	if (bytes_needed > availablebytes) {
		Status = STATUS_BUFFER_OVERFLOW;
		goto out;
	}

	flei = &fhli->Entry;

	flei->NextEntryOffset = 0;
	flei->ParentFileId = dvp && VTOZ(dvp) ? VTOZ(dvp)->z_id : 0;
	flei->FileNameLength = namelenholder / sizeof (WCHAR);

	Status = RtlUTF8ToUnicodeN(flei->FileName, namelenholder,
	    &namelenholder, name, strlen(name));

	fhli->EntriesReturned = 1;
	Status = STATUS_SUCCESS;
	Irp->IoStatus.Information = bytes_needed;

out:
	if (dvp != NULL)
		VN_RELE(dvp);

	zfs_exit(zfsvfs, FTAG);

	return (Status);
}

/* IRP_MJ_DEVICE_CONTROL helpers */

NTSTATUS
QueryCapabilities(PDEVICE_OBJECT DeviceObject, PIRP Irp,
    PIO_STACK_LOCATION IrpSp)
{
	NTSTATUS Status;
	PDEVICE_CAPABILITIES DeviceCapabilities;
	DeviceCapabilities =
	    IrpSp->Parameters.DeviceCapabilities.Capabilities;
	DeviceCapabilities->Version = 1;
	DeviceCapabilities->Size = sizeof (DEVICE_CAPABILITIES);
	DeviceCapabilities->DeviceD1 = FALSE;
	DeviceCapabilities->DeviceD2 = FALSE;
	DeviceCapabilities->SurpriseRemovalOK = TRUE;
	DeviceCapabilities->LockSupported = FALSE;
	DeviceCapabilities->EjectSupported = FALSE;
	DeviceCapabilities->Removable = TRUE;
	DeviceCapabilities->DockDevice = FALSE;
	// DeviceCapabilities->UniqueID = FALSE;
	// DeviceCapabilities->SilentInstall = FALSE;
	DeviceCapabilities->UniqueID = TRUE;
	DeviceCapabilities->SilentInstall = TRUE;
	DeviceCapabilities->RawDeviceOK = FALSE;
	DeviceCapabilities->SurpriseRemovalOK = FALSE;
	DeviceCapabilities->WakeFromD0 = FALSE;
	DeviceCapabilities->WakeFromD1 = FALSE;
	DeviceCapabilities->WakeFromD2 = FALSE;
	DeviceCapabilities->WakeFromD3 = FALSE;
	DeviceCapabilities->HardwareDisabled = FALSE;
	DeviceCapabilities->NonDynamic = FALSE;
	DeviceCapabilities->WarmEjectSupported = FALSE;
	DeviceCapabilities->NoDisplayInUI = FALSE;
	DeviceCapabilities->Address = 0xffffffff;
	DeviceCapabilities->UINumber = 0xffffffff;

	DeviceCapabilities->D1Latency =
	    DeviceCapabilities->D2Latency =
	    DeviceCapabilities->D3Latency = 0;
	DeviceCapabilities->DeviceState[PowerSystemWorking] =
	    PowerDeviceD0;
	DeviceCapabilities->DeviceState[PowerSystemSleeping1] =
	    PowerDeviceD3;
	DeviceCapabilities->DeviceState[PowerSystemSleeping2] =
	    PowerDeviceD3;
	DeviceCapabilities->DeviceState[PowerSystemSleeping3] =
	    PowerDeviceD3;
	DeviceCapabilities->DeviceState[PowerSystemHibernate] =
	    PowerDeviceD3;
	DeviceCapabilities->DeviceState[PowerSystemShutdown] =
	    PowerDeviceD3;

	// Irp->IoStatus.Information = sizeof (DEVICE_CAPABILITIES);

	return (STATUS_SUCCESS);
}

#if 0
NTSTATUS
SendBusRelationsRequest(PDEVICE_OBJECT STOR_DeviceObject,
    PDEVICE_RELATIONS *StorportRelations)
{
	NTSTATUS status = STATUS_SUCCESS;
	PIRP irp;
	PIO_STACK_LOCATION irpSp;
	KEVENT event;
	IO_STATUS_BLOCK ioStatus;

	// Initialize event for IRP completion
	KeInitializeEvent(&event, NotificationEvent, FALSE);

	// Allocate an IRP for the BusRelations request
	irp = IoBuildSynchronousFsdRequest(IRP_MJ_PNP,
	    STOR_DeviceObject,
	    NULL,
	    0,
	    NULL,
	    &event,
	    &ioStatus);

	if (irp == NULL)
		return (STATUS_INSUFFICIENT_RESOURCES);

	// Set up the IRP stack location for the BusRelations query
	irpSp = IoGetNextIrpStackLocation(irp);
	irpSp->MajorFunction = IRP_MJ_PNP;
	irpSp->MinorFunction = IRP_MN_QUERY_DEVICE_RELATIONS;
	irpSp->Parameters.QueryDeviceRelations.Type = BusRelations;

	// Call the target driver
	status = IoCallDriver(STOR_DeviceObject, irp);

	// If the IRP is pending, wait for it to complete
	if (status == STATUS_PENDING) {
		KeWaitForSingleObject(&event,
		    Executive,
		    KernelMode,
		    FALSE,
		    NULL);

		// Update status with the final IRP status
		status = ioStatus.Status;
	}

	if (NT_SUCCESS(status) && StorportRelations)
		*StorportRelations = (ULONG_PTR) ioStatus.Information;

	return (status);
}
#endif

NTSTATUS
QueryDeviceRelations(PDEVICE_OBJECT DeviceObject, PIRP *PIrp,
    PIO_STACK_LOCATION IrpSp)
{
	NTSTATUS Status = STATUS_INVALID_DEVICE_REQUEST;
	PIRP Irp = *PIrp;
	mount_t *zmo;
	PDEVICE_OBJECT ReturnDevice = NULL;
	PDEVICE_RELATIONS DeviceRelations;
	ZFS_DRIVER_EXTENSION(WIN_DriverObject, DriverExtension);

	zmo = (mount_t *)DeviceObject->DeviceExtension;

	dprintf("DeviceRelations.Type 0x%x\n",
	    IrpSp->Parameters.QueryDeviceRelations.Type);

	switch (IrpSp->Parameters.QueryDeviceRelations.Type) {
	case TargetDeviceRelation:
	{
		DeviceRelations =
		    (PDEVICE_RELATIONS)ExAllocatePoolWithTag(PagedPool,
		    sizeof (DEVICE_RELATIONS), 'ZTdr');
		if (!DeviceRelations) {
			dprintf("enomem DeviceRelations\n");
			Status = STATUS_INSUFFICIENT_RESOURCES;
			goto out;
		}

		mount_t *zmo_dcb = (mount_t *)zmo->parent_device;

		if (zmo->PhysicalDeviceObject != NULL)
			ReturnDevice = zmo->PhysicalDeviceObject;
		else if (zmo_dcb && zmo_dcb->PhysicalDeviceObject != NULL)
			ReturnDevice = zmo_dcb->PhysicalDeviceObject;
		else {
			ReturnDevice = DeviceObject; // wrong
		}
		ObReferenceObject(ReturnDevice);

		DeviceRelations->Count = 1;
		DeviceRelations->Objects[0] = ReturnDevice;
		Irp->IoStatus.Information =
		    (ULONG_PTR)DeviceRelations;
		dprintf("ZFS TargetDeviceRelation is %p\n",
		    ReturnDevice);
		Status = STATUS_SUCCESS;
		break;
	}
	case BusRelations:
	{
		int count;
		PDEVICE_RELATIONS StorportRelations = NULL;

		// Grab count here, and use only it, since list can
		// change as we process this function.
		count = vfs_mount_count();

		DeviceRelations = ExAllocatePoolWithTag(PagedPool,
		    offsetof(DEVICE_RELATIONS, Objects[count]), 'ZBrl');

		if (DeviceRelations == NULL) {
			if (StorportRelations) // Count can be 0.
				ExFreePool(StorportRelations);
			return (STATUS_INSUFFICIENT_RESOURCES);
		}

		DeviceRelations->Count = 0;

		if (count == 0)
			goto out;

		vfs_mount_setarray((void **)DeviceRelations->Objects, count);

		// Loop array, and grab reference and increment if valid.
		// linked list will only leave NULL at end, not start/middle.
		// list is of mounts, so fetch DeviceObjects.
		// ChatGPT says returning PDO here is more proper, but I can not
		// get AddDevice() to be called if I do. Returning FDO works.
		for (int i = 0; i < count; i++) {
			mount_t *mount;
			mount = (mount_t *)DeviceRelations->Objects[i];
			DeviceRelations->Objects[i] = NULL;
			if (mount == NULL || !mount->FunctionalDeviceObject)
				continue;
			dprintf("mount %p : FDO %p\n",
			    mount, mount->FunctionalDeviceObject);
			DeviceRelations->Objects[DeviceRelations->Count] =
			    mount->FunctionalDeviceObject;
//			    mount->PhysicalDeviceObject;
			ObReferenceObject(
			    DeviceRelations->Objects[DeviceRelations->Count]);
			DeviceRelations->Count++;
		}

out:
		if (StorportRelations) // Count can be 0.
			ExFreePool(StorportRelations);

		Irp->IoStatus.Information =
		    (ULONG_PTR)DeviceRelations;

		dprintf("BusRelations returning %d children in %p\n",
		    DeviceRelations->Count,
		    (void *)Irp->IoStatus.Information);

		Status = STATUS_SUCCESS;
		break;
	}
	default:
	}

	return (Status);
}

NTSTATUS
pnp_query_device_text(PDEVICE_OBJECT DeviceObject, PIRP Irp,
    PIO_STACK_LOCATION IrpSp)
{
	PWCHAR deviceText = NULL;
	ULONG length;
	NTSTATUS status = STATUS_NOT_SUPPORTED;

	switch (IrpSp->Parameters.QueryDeviceText.DeviceTextType) {
	case DeviceTextDescription:
		deviceText = L"ZFS Volume";
		break;
	case DeviceTextLocationInformation:
		deviceText = L"ZFS Virtual Disk";
		break;
	default:
		break;
	}

	if (deviceText) {
		length = (ULONG)(wcslen(deviceText) + 1) * sizeof (WCHAR);
		void *addr;

		addr = (void *)ExAllocatePoolWithTag(PagedPool, length,
		    'txtZ');
		if (addr) {
			RtlCopyMemory(addr, deviceText, length);
			Irp->IoStatus.Information = (ULONG_PTR)addr;
			dprintf("Replying with '%S'\n", addr);
			status = STATUS_SUCCESS;
		} else {
			status = STATUS_INSUFFICIENT_RESOURCES;
		}
	}

	return (status);
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

	Irp->IoStatus.Information =
	    sizeof (VOLUME_GET_GPT_ATTRIBUTES_INFORMATION);

	if (zfsvfs && zfsvfs->z_rdonly)
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
	ULONG OutputBufferLength;

	OutputBufferLength =
	    IrpSp->Parameters.DeviceIoControl.OutputBufferLength;
	if (OutputBufferLength < sizeof (MOUNTDEV_NAME)) {
		Irp->IoStatus.Information = sizeof (MOUNTDEV_NAME);
		return (STATUS_BUFFER_TOO_SMALL);
	}

	zmo = (mount_t *)DeviceObject->DeviceExtension;

	if (vfs_flags(zmo) & MNT_UNMOUNTING)
		return (STATUS_VOLUME_DISMOUNTED);


	/*
	 * Do not restrict to root vnode.  twext.dll (Previous Versions
	 * shell extension) opens an arbitrary file handle on the volume
	 * and sends IOCTL_MOUNTDEV_QUERY_DEVICE_NAME to discover which
	 * volume device hosts it.  NTFS returns the device name for any
	 * file handle; we must do the same.
	 *
	 * When called from a VCB (fsDevice) context, the VCB's device_name
	 * is the filesystem device (\Device\ZFS{uuid}).  MountManager knows
	 * the volume by the disk device name (\Device\zfs-{uuid}, stored in
	 * the DCB).  Return the DCB's name so that twext.dll can map it to a
	 * Volume{} GUID and query VSS for snapshots.
	 */
	UNICODE_STRING *devname = &zmo->device_name;
	if (zmo->type == MOUNT_TYPE_VCB && zmo->parent_device != NULL) {
		mount_t *dcb = (mount_t *)zmo->parent_device;
		if (dcb->device_name.Length > 0)
			devname = &dcb->device_name;
	}

	name = Irp->AssociatedIrp.SystemBuffer;
	ULONG requiredSize = sizeof (MOUNTDEV_NAME) + devname->Length;

	// Check if the output buffer is large enough
	if (OutputBufferLength < requiredSize) {
		Irp->IoStatus.Status = STATUS_BUFFER_TOO_SMALL;
		Irp->IoStatus.Information = requiredSize;
		return (STATUS_BUFFER_TOO_SMALL);
	}

	// Set the length of the device name
	name->NameLength = devname->Length;

	// Copy the device name into the buffer
	RtlCopyMemory(name->Name, devname->Buffer, devname->Length);

	// Set the status and information fields
	Irp->IoStatus.Status = STATUS_SUCCESS;
	Irp->IoStatus.Information = requiredSize;

	dprintf("replying with '%.*S'\n",
	    name->NameLength / sizeof (WCHAR), name->Name);

	return (STATUS_SUCCESS);
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

	DISK_GEOMETRY *diskGeometry = Irp->AssociatedIrp.SystemBuffer;
	RtlZeroMemory(diskGeometry, sizeof (*diskGeometry));
	unsigned long TracksPerCylinder = 255;
	unsigned long SectorsPerTrack = 63;
	unsigned long BytesPerSector = 512;
	unsigned long long Cylinders = 512;

	zfsvfs_t *zfsvfs = vfs_fsprivate(zmo);
	if (zfsvfs != NULL) {

		if ((error = zfs_enter(zfsvfs, FTAG)) != 0)
			return (error); // This returns EIO if fail

		uint64_t refdbytes, availbytes, usedobjs, availobjs;
		dmu_objset_space(zfsvfs->z_os,
		    &refdbytes, &availbytes, &usedobjs, &availobjs);

		uint64_t TotalSizeBytes = availbytes + refdbytes;

		// Calculate total sectors
		unsigned long long TotalSectors =
		    TotalSizeBytes / BytesPerSector;

		// Calculate sectors per cylinder
		unsigned long SectorsPerCylinder =
		    TracksPerCylinder * SectorsPerTrack;

		// Calculate number of cylinders
		Cylinders = TotalSectors / SectorsPerCylinder;
	}

	// Populate the DISK_GEOMETRY structure
	diskGeometry->Cylinders.QuadPart = Cylinders;
	diskGeometry->TracksPerCylinder = TracksPerCylinder;
	diskGeometry->SectorsPerTrack = SectorsPerTrack;
	diskGeometry->BytesPerSector = BytesPerSector;
	diskGeometry->MediaType = FixedMedia;

	if (zfsvfs != NULL) {
		zfs_exit(zfsvfs, FTAG);
	}

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

	// DISK_GEOMETRY_EX_INTERNAL *geom = Irp->AssociatedIrp.SystemBuffer;
	DISK_GEOMETRY_EX *geom = Irp->AssociatedIrp.SystemBuffer;
	RtlZeroMemory(geom,
	    IrpSp->Parameters.DeviceIoControl.OutputBufferLength);

	zfsvfs_t *zfsvfs = vfs_fsprivate(zmo);
	if (zfsvfs == NULL) {
		geom->DiskSize.QuadPart = 1024 * 1024 * 1024;
		geom->Geometry.BytesPerSector = 512;
		geom->Geometry.MediaType = FixedMedia;
	} else {
		if ((error = zfs_enter(zfsvfs, FTAG)) != 0)
			return (error);  // This returns EIO if fail

		uint64_t refdbytes, availbytes, usedobjs, availobjs;
		dmu_objset_space(zfsvfs->z_os,
		    &refdbytes, &availbytes, &usedobjs, &availobjs);

		geom->DiskSize.QuadPart = availbytes + refdbytes;
		geom->Geometry.BytesPerSector = 512;
		geom->Geometry.MediaType = FixedMedia; // or RemovableMedia
		zfs_exit(zfsvfs, FTAG);
	}

	geom->Geometry.Cylinders.QuadPart = 1024;
	geom->Geometry.SectorsPerTrack = 63;
	geom->Geometry.TracksPerCylinder = 255;
	geom->Data[0] = 0;
#if 0
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
#endif
	Irp->IoStatus.Information = sizeof (DISK_GEOMETRY_EX);
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
	RtlZeroMemory(part, sizeof (*part));

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
	RtlZeroMemory(part, sizeof (*part));

	part->PartitionStyle = PARTITION_STYLE_MBR;
	part->RewritePartition = FALSE;
	part->Mbr.RecognizedPartition = FALSE;
	part->Mbr.PartitionType = PARTITION_HUGE;
	part->Mbr.BootIndicator = FALSE;
	part->Mbr.HiddenSectors = 1;
	part->StartingOffset.QuadPart = 0;
	part->PartitionLength.QuadPart = availbytes + refdbytes;
	part->PartitionNumber = 1;

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
	RtlZeroMemory(gli, sizeof (*gli));
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
	NTSTATUS status = STATUS_NOT_SUPPORTED;
	ULONG outputLength;

	dprintf("%s: \n", __func__);

	STORAGE_PROPERTY_QUERY *spq = Irp->AssociatedIrp.SystemBuffer;

	outputLength = IrpSp->Parameters.DeviceIoControl.OutputBufferLength;

// Check Length if not Query type. If query:
// According to MSDN, an output buffer of size 0 can be used to determine
// if a property exists so this must be a success case with no data transferred
	if (spq->QueryType != PropertyExistsQuery) {
		if (outputLength < sizeof (STORAGE_DESCRIPTOR_HEADER)) {
			Irp->IoStatus.Information =
			    sizeof (STORAGE_DESCRIPTOR_HEADER);
			return (STATUS_BUFFER_TOO_SMALL);
		}
	}

	/*
	 * PropertyExistsQuery:
	 * Based on whether your driver supports the requested property,
	 * return STATUS_SUCCESS
	 * if the property exists, or STATUS_NOT_SUPPORTED if it does not.
	 * PropertyStandardQuery:
	 * Return data
	 */

	// ExistsQuery: return OK if exists.
	Irp->IoStatus.Information = 0;

	// Might be NULL
	PSTORAGE_DESCRIPTOR_HEADER Header =
	    (PSTORAGE_DESCRIPTOR_HEADER) Irp->AssociatedIrp.SystemBuffer;
	size_t hdrsize = 0;

	switch (spq->PropertyId) {
	case StorageDeviceUniqueIdProperty:
		dprintf("    PropertyExistsQuery "
		    "StorageDeviceUniqueIdProperty\n");

		if (spq->QueryType == PropertyExistsQuery)
			return (STATUS_SUCCESS);
		dprintf("    PropertyStandardQuery "
		    "StorageDeviceUniqueIdProperty\n");
		break;

	case StorageAccessAlignmentProperty:
		dprintf("    PropertyExistsQuery "
		    "StorageAccessAlignmentProperty\n");
		if (spq->QueryType == PropertyExistsQuery)
			return (STATUS_SUCCESS);
		dprintf("    PropertyStandardQuery "
		    "StorageAccessAlignmentProperty\n");

		hdrsize = sizeof (STORAGE_ACCESS_ALIGNMENT_DESCRIPTOR);
		if (outputLength == sizeof (STORAGE_DESCRIPTOR_HEADER) &&
		    Header) {
			Header->Size = hdrsize;
			Header->Version = hdrsize;
			Irp->IoStatus.Information =
			    sizeof (STORAGE_DESCRIPTOR_HEADER);
			return (STATUS_SUCCESS);
		}

		Irp->IoStatus.Information = hdrsize;
		if (outputLength < hdrsize)
			return (STATUS_BUFFER_TOO_SMALL);

		PSTORAGE_ACCESS_ALIGNMENT_DESCRIPTOR AlignmentDescriptor =
		    (PSTORAGE_ACCESS_ALIGNMENT_DESCRIPTOR)
		    Irp->AssociatedIrp.SystemBuffer;
		RtlZeroMemory(AlignmentDescriptor, hdrsize);
		AlignmentDescriptor->Version = hdrsize;
		AlignmentDescriptor->Size = hdrsize;
		AlignmentDescriptor->BytesPerCacheLine = 64; // Example value
		AlignmentDescriptor->BytesOffsetForCacheAlignment = 0;
		AlignmentDescriptor->BytesPerLogicalSector = 512;
		AlignmentDescriptor->BytesPerPhysicalSector = 512;
		AlignmentDescriptor->BytesOffsetForSectorAlignment = 0;
		status = STATUS_SUCCESS;
		break;

	case StorageDeviceProperty:
		dprintf("    PropertyExistsQuery "
		    "StorageDeviceProperty\n");
		if (spq->QueryType == PropertyExistsQuery)
			return (STATUS_SUCCESS);
		dprintf("    PropertyStandardQuery "
		    "StorageDeviceProperty\n");

		hdrsize = sizeof (STORAGE_DEVICE_DESCRIPTOR);
		if (outputLength == sizeof (STORAGE_DESCRIPTOR_HEADER) &&
		    Header) {
			Header->Size = hdrsize;
			Header->Version = hdrsize;
			Irp->IoStatus.Information =
			    sizeof (STORAGE_DESCRIPTOR_HEADER);
			return (STATUS_SUCCESS);
		}

		Irp->IoStatus.Information = hdrsize;
		if (outputLength < hdrsize)
			return (STATUS_BUFFER_TOO_SMALL);

		PSTORAGE_DEVICE_DESCRIPTOR storage;
		storage = Irp->AssociatedIrp.SystemBuffer;
		storage->Version = hdrsize;
		storage->Size = hdrsize;
		storage->BusType = 0;
		storage->CommandQueueing = 0;
		storage->DeviceType = FILE_DEVICE_DISK;
		storage->DeviceTypeModifier = 0;
		storage->ProductIdOffset = 0;
		storage->ProductRevisionOffset = 0;
		// storage->RawDeviceProperties = 0;
		storage->RawPropertiesLength = 0;
		storage->RemovableMedia = 0;
		storage->SerialNumberOffset = 0;
		storage->VendorIdOffset = 0;
		status = STATUS_SUCCESS;
		break;

	case StorageDeviceAttributesProperty:
		dprintf("    PropertyExistsQuery "
		    "StorageDeviceAttributesProperty\n");
		if (spq->QueryType == PropertyExistsQuery)
			return (STATUS_SUCCESS);
		dprintf("    PropertyStandardQuery "
		    "StorageDeviceAttributesProperty\n");

		hdrsize = sizeof (STORAGE_DEVICE_ATTRIBUTES_DESCRIPTOR);
		if (outputLength == sizeof (STORAGE_DESCRIPTOR_HEADER) &&
		    Header) {
			Header->Size = hdrsize;
			Header->Version = hdrsize;
			Irp->IoStatus.Information =
			    sizeof (STORAGE_DESCRIPTOR_HEADER);
			return (STATUS_SUCCESS);
		}

		Irp->IoStatus.Information = hdrsize;
		if (outputLength < hdrsize)
			return (STATUS_BUFFER_TOO_SMALL);

		STORAGE_DEVICE_ATTRIBUTES_DESCRIPTOR *sdad;
		sdad = Irp->AssociatedIrp.SystemBuffer;
		sdad->Version = hdrsize;
		sdad->Size = hdrsize;
		sdad->Attributes =
		    STORAGE_ATTRIBUTE_BYTE_ADDRESSABLE_IO;
		status = STATUS_SUCCESS;
		break;

	case StorageDeviceLBProvisioningProperty:
		dprintf("    StorageDeviceLBProvisioningProperty\n");
		if (spq->QueryType == PropertyExistsQuery)
			return (STATUS_SUCCESS);

		hdrsize = sizeof (DEVICE_LB_PROVISIONING_DESCRIPTOR);
		if (outputLength == sizeof (STORAGE_DESCRIPTOR_HEADER) &&
		    Header) {
			Header->Size = hdrsize;
			Header->Version = hdrsize;
			Irp->IoStatus.Information =
			    sizeof (STORAGE_DESCRIPTOR_HEADER);
			return (STATUS_SUCCESS);
		}

		Irp->IoStatus.Information = hdrsize;
		if (outputLength < hdrsize)
			return (STATUS_BUFFER_TOO_SMALL);

		DEVICE_LB_PROVISIONING_DESCRIPTOR *lbpd;
		lbpd = Irp->AssociatedIrp.SystemBuffer;
		RtlZeroMemory(lbpd, hdrsize);
		lbpd->Version = hdrsize;
		lbpd->Size = hdrsize;
		/* ZFS on Windows: not thinly provisioned */
		lbpd->ThinProvisioningEnabled = 0;
		lbpd->ThinProvisioningReadZeros = 0;
		lbpd->AnchorSupported = 0;
		status = STATUS_SUCCESS;
		break;

	default:
		// StorageDeviceResiliencyProperty etc.
		if (spq->QueryType == PropertyExistsQuery) {
			dprintf("PropertyExistsQuery not "
			    "supported: %d / 0x%x\n",
			    spq->PropertyId, spq->PropertyId);
			return (STATUS_NOT_SUPPORTED);
		}
		dprintf("PropertyStandardQuery not "
		    "supported: %d / 0x%x\n",
		    spq->PropertyId, spq->PropertyId);
		return (STATUS_NOT_SUPPORTED);
	}

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

#if 1
	RtlUnicodeToUTF8N(osname, MAXPATHLEN - 1, &len, zmo->name.Buffer,
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

#else

//	RtlUnicodeToUTF8N(osname, MAXPATHLEN - 1, &len, zmo->uuid.Buffer,
//	    zmo->uuid.Length);
//	osname[len] = 0;

	// uniqueId appears to be CHARS not WCHARS,
	// so this might need correcting?
	uniqueId = (PMOUNTDEV_UNIQUE_ID)Irp->AssociatedIrp.SystemBuffer;

	uniqueId->UniqueIdLength = sizeof (zmo->rawuuid);

	if (sizeof (USHORT) + uniqueId->UniqueIdLength <= bufferLength) {
		RtlCopyMemory((PCHAR)uniqueId->UniqueId, zmo->rawuuid,
		    uniqueId->UniqueIdLength);
		Irp->IoStatus.Information =
		    FIELD_OFFSET(MOUNTDEV_UNIQUE_ID, UniqueId[0]) +
		    uniqueId->UniqueIdLength;
		// dprintf("replying with '%.*s'\n",
		// uniqueId->UniqueIdLength, uniqueId->UniqueId);
		return (STATUS_SUCCESS);
	} else {
		Irp->IoStatus.Information = sizeof (MOUNTDEV_UNIQUE_ID);
		return (STATUS_BUFFER_OVERFLOW);
	}
#endif

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
		RtlCopyMemory(&mountGuid->StableGuid, zmo->rawuuid,
		    sizeof (zmo->rawuuid));
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

	/*
	 * Only suggest a drive letter for drive-letter-only mounts. Junction
	 * mounts (e.g. BOOM/lower at E:\lower\, snapshots at
	 * \.zfs\snapshot\*) must not suggest E: -- doing so confuses
	 * MountManager into trying to assign the same letter to multiple
	 * volumes.
	 */
	if (!zmo->justDriveLetter)
		return (STATUS_NOT_FOUND);
	Irp->IoStatus.Information = 0;

	if (zmo->mountpoint.Buffer == NULL)
		return (STATUS_OBJECT_NAME_NOT_FOUND);

	// If "?:" then just let windows pick drive letter
	if (zmo->mountpoint.Buffer[4] == L'?')
		return (STATUS_OBJECT_NAME_NOT_FOUND);

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

	Irp->IoStatus.Information = sizeof (MOUNTDEV_SUGGESTED_LINK_NAME) +
	    MountPoint.Length;
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
#if 0
	zfsvfs_t *zfsvfs = vfs_fsprivate(zmo);
	if (zfsvfs == NULL)
		return (STATUS_INVALID_PARAMETER);

	extern int	zfs_vfs_uuid_gen(const char *osname, uuid_t uuid);

	// A bit naughty
	zfs_vfs_uuid_gen(spa_name(dmu_objset_spa(zfsvfs->z_os)),
	    (char *)&guid->StableGuid);
#else
	memcpy(&guid->StableGuid, zmo->rawuuid, sizeof (guid->StableGuid));
#endif
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
	/*
	 * zmo here is the disk's DCB, whose own ->mountflags is set once
	 * at mount time and never touched again. The live flags (kept
	 * current by atime_changed_cb() and friends in zfs_vfsops.c) live
	 * on the mounted volume's VCB, reachable via the zfsvfs that the
	 * DCB's fsprivate points at.
	 */
	{
		zfsvfs_t *zfsvfs = (zfsvfs_t *)vfs_fsprivate(zmo);

		fzvm->flags = (zfsvfs != NULL) ?
		    (uint32_t)vfs_flags(zfsvfs->z_vfs) :
		    (uint32_t)vfs_flags(zmo);
	}
	memcpy(fzvm->buffer, zmo->mountpoint.Buffer, fzvm->len);
	Irp->IoStatus.Information =
	    sizeof (fsctl_zfs_volume_mountpoint_t) +
	    zmo->mountpoint.Length;
	dprintf("%s: returning %.*S\n", __func__, fzvm->len, fzvm->buffer);
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
	zfs_ccb_t *zccb;

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

	// OpLocks might need preflight checks.
	BOOLEAN needsPreflight = FALSE;
	uint64_t skip = (uint64_t)Irp->Tail.Overlay.DriverContext[0];
	const BOOLEAN skipZeroData =
	    (skip == (OPLOCK_SKIP_MAGIC | OPLOCK_SKIP_ZERODATA));
	if (!skipZeroData)
		needsPreflight = TRUE;

	if (needsPreflight) {

		ExAcquireResourceExclusiveLite(vp->FileHeader.Resource, TRUE);

		ZFS_OPLOCK_CREATE_CTX *ctx = ExAllocatePoolZero(NonPagedPoolNx,
		    sizeof (*ctx), 'plkO');
		if (!ctx) {
			ExReleaseResourceLite(vp->FileHeader.Resource);
			return (STATUS_INSUFFICIENT_RESOURCES);
		}
		ctx->DeviceObject = DeviceObject;
		ctx->Irp = Irp;
		ctx->SkipMask = OPLOCK_SKIP_ZERODATA;

		Status = FsRtlCheckOplockEx(vp_oplock(vp), Irp, 0, ctx,
		    ZfsOplockCreatePostBreak, NULL);

		ExReleaseResourceLite(vp->FileHeader.Resource);

		if (Status == STATUS_PENDING) {
			IoMarkIrpPending(Irp);
			return (STATUS_PENDING);
		}

		ExFreePoolWithTag(ctx, 'plkO');

		if (!NT_SUCCESS(Status))
			return (Status);
	}


	znode_t *zp = VTOZ(vp);

	/*
	 * PagingIoResource before FileHeader.Resource, same reasoning as
	 * set_file_endoffile_information(): CcFlushCache/CcCopyWrite/
	 * FsRtlCopyWrite internally acquire FileHeader.Resource themselves
	 * (confirmed live this session), so anything holding both must
	 * take PagingIoResource first or it AB-BA deadlocks against an
	 * in-progress fastio_write/CcCopyWrite.  This also excludes a
	 * concurrent copy from racing zfs_freesp() below, which frees the
	 * underlying blocks for this range -- previously nothing did,
	 * since FileHeader.Resource alone does not exclude fastio_write
	 * (which only takes PagingIoResource).
	 */
	// ExAcquireResourceSharedLite(&zmo->tree_lock, true);
	ExAcquireResourceExclusiveLite(vp->FileHeader.PagingIoResource, TRUE);
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
	ExReleaseResourceLite(vp->FileHeader.PagingIoResource);
	// ExReleaseResourceLite(&Vcb->tree_lock);

	return (Status);
}

NTSTATUS
fsctl_get_retrieval_pointers(PDEVICE_OBJECT DeviceObject, PIRP Irp,
    PIO_STACK_LOCATION IrpSp)
{
	STARTING_VCN_INPUT_BUFFER *in =
	    IrpSp->Parameters.FileSystemControl.Type3InputBuffer;
	ULONG inlen = IrpSp->Parameters.FileSystemControl.InputBufferLength;

	RETRIEVAL_POINTERS_BUFFER *out = Irp->UserBuffer;
	ULONG outlen = IrpSp->Parameters.FileSystemControl.OutputBufferLength;

	PFILE_OBJECT FileObject = IrpSp->FileObject;
	NTSTATUS Status;

	if (!FileObject)
		return (STATUS_INVALID_PARAMETER);

	struct vnode *vp = FileObject->FsContext;

	if (!vp)
		return (STATUS_INVALID_PARAMETER);

	if (inlen < sizeof (STARTING_VCN_INPUT_BUFFER) ||
	    in->StartingVcn.QuadPart < 0)
		return (STATUS_INVALID_PARAMETER);

	if (!out)
		return (STATUS_INVALID_PARAMETER);

	if (outlen < offsetof(RETRIEVAL_POINTERS_BUFFER, Extents[0]))
		return (STATUS_BUFFER_TOO_SMALL);

	// Can we get away with this?
	return (STATUS_NO_MORE_ENTRIES);
}

NTSTATUS
volume_read(PDEVICE_OBJECT DeviceObject, PIRP Irp,
    PIO_STACK_LOCATION IrpSp)
{
	dprintf("%s\n", __func__);

	uint64_t bufferLength;
	void *buffer;
	bufferLength = IrpSp->Parameters.Read.Length;
	LARGE_INTEGER offset = IrpSp->Parameters.Read.ByteOffset;
	PMDL mdl = NULL;

	if (offset.QuadPart < 0 || bufferLength == 0) {
		Irp->IoStatus.Information = 0;
		return (STATUS_INVALID_PARAMETER);
	}

	buffer = MapUserBuffer(Irp, IrpSp->Parameters.Read.Length,
	    IoWriteAccess, &mdl);
	if (buffer == NULL) {
		Irp->IoStatus.Information = 0;
		return (STATUS_INSUFFICIENT_RESOURCES);
	}


	memset(buffer, 0, bufferLength);

	UnMapUserBuffer(mdl);

	Irp->IoStatus.Information = bufferLength;
	dprintf("%s exit (%lld bytes)\n", __func__, bufferLength);
	return (STATUS_SUCCESS);
}
