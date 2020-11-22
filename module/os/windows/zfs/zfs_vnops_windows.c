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

#undef _NTDDK_
#include <ntifs.h>
#include <ntddk.h>
#include <ntddscsi.h>
#include <scsi.h>
#include <ntddcdrm.h>
#include <ntdddisk.h>
#include <ntddstor.h>
#include <ntintsafe.h>
#include <mountmgr.h>
#include <Mountdev.h>
#include <ntddvol.h>
 // I have no idea what black magic is needed to get ntifs.h to define these


#ifndef FsRtlEnterFileSystem
#define FsRtlEnterFileSystem() { \
	KeEnterCriticalRegion();     \
}
#endif
#ifndef FsRtlExitFileSystem
#define FsRtlExitFileSystem() { \
    KeLeaveCriticalRegion();     \
}
#endif

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
#include <sys/vfs.h>
#include <sys/vfs_opreg.h>
#include <sys/zfs_vfsops.h>
#include <sys/zfs_rlock.h>
#include <sys/zfs_ctldir.h>

//#include <sys/xattr.h>
//#include <sys/utfconv.h>
#include <sys/ubc.h>
#include <sys/callb.h>
#include <sys/unistd.h>
#include <sys/zfs_windows.h>
#include <sys/kstat.h>
//#include <miscfs/fifofs/fifo.h>
//#include <miscfs/specfs/specdev.h>
//#include <vfs/vfs_support.h>
//#include <sys/ioccom.h>


PDEVICE_OBJECT ioctlDeviceObject = NULL;
PDEVICE_OBJECT fsDiskDeviceObject = NULL;
#ifdef DEBUG_IOCOUNT
static kmutex_t GIANT_SERIAL_LOCK;
#endif

#ifdef _KERNEL

DRIVER_INITIALIZE DriverEntry;

unsigned int debug_vnop_osx_printf = 0;
unsigned int zfs_vnop_ignore_negatives = 0;
unsigned int zfs_vnop_ignore_positives = 0;
unsigned int zfs_vnop_create_negatives = 1;

#endif

#ifdef ALLOC_PRAGMA
#pragma alloc_text(INIT, DriverEntry)
#endif

#define	DECLARE_CRED(ap) \
	cred_t *cr;
#define	DECLARE_CONTEXT(ap) \
	caller_context_t *ct
#define	DECLARE_CRED_AND_CONTEXT(ap)	\
	DECLARE_CRED(ap);		\
	DECLARE_CONTEXT(ap)

//#define	dprintf if (debug_vnop_osx_printf) kprintf
//#define dprintf kprintf

//#define	dprintf(...) if (debug_vnop_osx_printf) {printf(__VA_ARGS__);delay(hz>>2);}
#ifdef _KERNEL
uint64_t vnop_num_reclaims = 0;
uint64_t vnop_num_vnodes = 0;
uint64_t zfs_disable_wincache = 0;
#endif

extern void UnlockAndFreeMdl(PMDL );

BOOLEAN zfs_AcquireForLazyWrite(void *Context, BOOLEAN Wait)
{
	struct vnode *vp = Context;
	dprintf("%s:\n", __func__);

	if (vp == NULL) return FALSE;

	if (VN_HOLD(vp) == 0) {

		if (!ExAcquireResourceSharedLite(vp->FileHeader.PagingIoResource, Wait)) {
			dprintf("Failed\n");
			VN_RELE(vp);
			return FALSE;
		}

		vnode_ref(vp);
		VN_RELE(vp);
		return TRUE;
	}

	return FALSE;
}

void zfs_ReleaseFromLazyWrite(void *Context)
{
	struct vnode *vp = Context;
	dprintf("%s:\n", __func__);
	if (VN_HOLD(vp) == 0) {
		ExReleaseResourceLite(vp->FileHeader.PagingIoResource);
		vnode_rele(vp);
		VN_RELE(vp);
	}
}

BOOLEAN zfs_AcquireForReadAhead(void *Context, BOOLEAN Wait)
{
	struct vnode *vp = Context;
	dprintf("%s:\n", __func__);

	if (vp == NULL) return FALSE;

	if (VN_HOLD(vp) == 0) {

		if (!ExAcquireResourceSharedLite(vp->FileHeader.Resource, Wait)) {
			dprintf("Failed\n");
			VN_RELE(vp);
			return FALSE;
		}

		vnode_ref(vp);
		VN_RELE(vp);
		return TRUE;
	}

	return FALSE;
}

void zfs_ReleaseFromReadAhead(void *Context)
{
	struct vnode *vp = Context;
	dprintf("%s:\n", __func__);
	if (VN_HOLD(vp) == 0) {
		ExReleaseResourceLite(vp->FileHeader.Resource);
		vnode_rele(vp);
		VN_RELE(vp);
	}
}

CACHE_MANAGER_CALLBACKS CacheManagerCallbacks =
{
	.AcquireForLazyWrite = zfs_AcquireForLazyWrite,
	.ReleaseFromLazyWrite = zfs_ReleaseFromLazyWrite,
	.AcquireForReadAhead = zfs_AcquireForReadAhead,
	.ReleaseFromReadAhead = zfs_ReleaseFromReadAhead
};


/*
 * zfs vfs operations.
 */

/*
 * FileObject->FsContext will point to vnode, many FileObjects can point to same vnode.
 * FileObject->FsContext2 will point to own "zfs_dirlist_t" and be unique to each FileObject.
 * - which could also be done with TSD data, but this appears to be the Windows norm.
 */
void zfs_couplefileobject(vnode_t *vp, FILE_OBJECT *fileobject, uint64_t size)
{
	ASSERT3P(fileobject->FsContext2, == , NULL);
	zfs_dirlist_t *zccb = kmem_zalloc(sizeof(zfs_dirlist_t), KM_SLEEP);
	zccb->magic = ZFS_DIRLIST_MAGIC;
	fileobject->FsContext2 = zccb;

	vnode_couplefileobject(vp, fileobject, size);
}

void zfs_decouplefileobject(vnode_t *vp, FILE_OBJECT *fileobject)
{
	// We release FsContext2 at CLEANUP, but fastfat releases it in
	// CLOSE. Does this matter?
	zfs_dirlist_t* zccb = fileobject->FsContext2;

	ASSERT3P(zccb, != , NULL);

	if (zccb->searchname.Buffer != NULL) {
		kmem_free(zccb->searchname.Buffer, zccb->searchname.MaximumLength);
		zccb->searchname.Buffer = NULL;
		zccb->searchname.MaximumLength = 0;
	}

	kmem_free(zccb, sizeof(zfs_dirlist_t));
	fileobject->FsContext2 = NULL;

	vnode_decouplefileobject(vp, fileobject);
}

/*
 * Take filename, look for colons ":".
 * No colon, return OK.
 * if ends with "::$DATA". Terminate on colon, return OK (regular file open).
 * if ends with anything not ":$DATA", return error. (we don't handle other types)
 * if colon, parse name up until next colon. Assign colonname to point to stream name.
 */
int stream_parse(char *filename, char **streamname)
{
	char *colon, *second;

	// Just a filename, no streams.
	colon = strchr(filename, ':');
	if (colon == NULL)
		return 0;

	// Regular file, with "::$DATA" end?
	if (!strcmp(colon, "::$DATA")) {
		*colon = 0; // Terminate before colon
		return 0;
	}

	// Look for second colon
	second = strchr(&colon[1], ':');

	// No second colon, just stream name. Validity check?
	if (second == NULL) {
		*streamname = &colon[1];
		*colon = 0; // Cut off streamname from filename

		// We now ADD ":$DATA" to the stream name.
		strcat(*streamname, ":$DATA");

		return 0;
	}

	// Have second colon, better be ":$DATA".
	if (!strcmp(second, ":$DATA")) {

		// Terminate at second colon, set streamname
		// We now keep the ":$DATA" extension in the xattr name 
		// *second = 0;

		*streamname = &colon[1];
		*colon = 0; // Cut of streamname from filename
		return 0;
	}

	// Not $DATA
	dprintf("%s: Not handling StreamType '%s'\n", __func__, second);
	return EINVAL;
}

/*
 * Attempt to parse 'filename', descending into filesystem.
 * If start "dvp" is passed in, it is expected to have a HOLD
 * If successful, function will return with:
 * - HOLD on dvp
 * - HOLD on vp
 * - final parsed filename part in 'lastname' (in the case of creating an entry)
 */
int zfs_find_dvp_vp(zfsvfs_t *zfsvfs, char *filename, int finalpartmaynotexist, int finalpartmustnotexist,
	char **lastname, struct vnode **dvpp, struct vnode **vpp, int flags)
{
	int error = ENOENT;
	znode_t *zp;
	struct vnode *dvp = NULL;
	struct vnode *vp = NULL;
	char *word = NULL;
	char *brkt = NULL;
	struct componentname cn;
	int fullstrlen;

	// Iterate from dvp if given, otherwise root
	dvp = *dvpp;

	if (dvp == NULL) {
		// Grab a HOLD
		error = zfs_zget(zfsvfs, zfsvfs->z_root, &zp);
		if (error != 0) return ESRCH;  // No such dir
		dvp = ZTOV(zp);
	} else {
		// Passed in dvp is already HELD, but grab one now
		// since we release dirs as we descend
		dprintf("%s: passed in dvp\n", __func__);
		if (VN_HOLD(dvp) != 0) return ESRCH;
	}

	fullstrlen = strlen(filename);

	// Sometimes we are given a path like "\Directory\directory\" with the final 
	// separator, we want to eat that final character.
	if ((fullstrlen > 2) &&
		(filename[fullstrlen - 1] == '\\'))
		filename[--fullstrlen] = 0;

	for (word = strtok_r(filename, "/\\", &brkt);
		word;
		word = strtok_r(NULL, "/\\", &brkt)) {

		//dprintf("..'%s'..", word);

		// If a component part name is too long
		if (strlen(word) > MAXNAMELEN - 1) {
			VN_RELE(dvp);
			return STATUS_OBJECT_NAME_INVALID;
		}

		cn.cn_nameiop = LOOKUP;
		cn.cn_flags = ISLASTCN;
		cn.cn_namelen = strlen(word);
		cn.cn_nameptr = word;

		error = zfs_lookup(dvp, word,
			&vp, &cn, cn.cn_nameiop, NULL, flags);

		if (error != 0) {
			// If we are creating a file, or looking up parent,
			// allow it not to exist
			if (finalpartmaynotexist) break;
			dprintf("failing out here\n");
			VN_RELE(dvp); // since we weren't successful, we should release dvp here
			dvp = NULL;
			break;
		}

		// If last lookup hit a non-directory type, we stop
		zp = VTOZ(vp);
		ASSERT(zp != NULL);
		if (S_ISDIR(zp->z_mode)) {

			// Quick check to see if we are reparsepoint directory
			if (zp->z_pflags & ZFS_REPARSE) {
				/* How reparse points work from the point of view of the filesystem appears to
				* undocumented. When returning STATUS_REPARSE, MSDN encourages us to return
				* IO_REPARSE in Irp->IoStatus.Information, but that means we have to do our own
				* translation. If we instead return the reparse tag in Information, and store
				* a pointer to the reparse data buffer in Irp->Tail.Overlay.AuxiliaryBuffer,
				* IopSymlinkProcessReparse will do the translation for us.
				* - maharmstone
				*/
				REPARSE_DATA_BUFFER *rpb;
				rpb = ExAllocatePoolWithTag(PagedPool, zp->z_size, '!FSZ');
				uio_t *uio;
				uio = uio_create(1, 0, UIO_SYSSPACE, UIO_READ);
				uio_addiov(uio, (user_addr_t)rpb, zp->z_size);
				zfs_readlink(vp, uio, NULL);
				uio_free(uio);
				VN_RELE(vp);

				// Return in Reserved the amount of path that was parsed.
				/* FileObject->FileName.Length - parsed*/
				rpb->Reserved = (fullstrlen - ( ( word - filename ) + strlen(word))) * sizeof(WCHAR);
				// We overload the lastname thing a bit, to return the reparsebuffer
				if (lastname) *lastname = (char *)rpb;
				dprintf("%s: returning REPARSE\n", __func__);
				VN_RELE(dvp);
				return STATUS_REPARSE;
			}

			// Not reparse
			VN_RELE(dvp);
			dvp = vp;
			vp = NULL;
		} else {
			// If we aren't the final component, descending dirs, and it's a file?
			if (brkt != NULL && *brkt != 0) {
				dprintf("%s: not a DIR triggered '%s'\n", __func__, word);
				VN_RELE(dvp);
				return ENOTDIR;
			}
			break;
		} // is dir or not

	} // for word
	//dprintf("\n");

	if (dvp) {
		// We return with dvp HELD
		//VN_RELE(dvp);
	} else {
		dprintf("%s: failed to find dvp for '%s' word '%s' err %d\n", __func__, filename,
			word?word:"(null)", error);
		VN_RELE(dvp);
		return error;
	}

	if (error != 0 && !vp && !finalpartmaynotexist) {
		VN_RELE(dvp);
		return ENOENT;
	}

	if (!word && finalpartmustnotexist && dvp && !vp) {
		dprintf("CREATE with existing dir exit?\n");
		VN_RELE(dvp);
		return EEXIST;
	}

	// If finalpartmaynotexist is TRUE, make sure we are looking at
	// the finalpart, and not in the middle of descending
	if (finalpartmaynotexist && brkt != NULL && *brkt != 0) {
		dprintf("finalpartmaynotexist, but not at finalpart: %s\n", brkt);
		VN_RELE(dvp);
		return ESRCH;
	}

	if (lastname) {

		*lastname = word /* ? word : filename */;

		// Skip any leading "\"
		while (*lastname != NULL &&
			(**lastname == '\\' || **lastname == '/')) (*lastname)++;

	}

	if (dvpp != NULL)
		*dvpp = dvp;
	if (vpp != NULL)
		*vpp = vp;

	return 0;
}

/*
 * In POSIX, the vnop_lookup() would return with iocount still held
 * for the caller to issue VN_RELE() on when done. 
 * The above zfs_find_dvp_vp() behaves a little like that, in that
 * if a successful "vp" is returned, it has a iocount lock, and
 * is released here when finished.
 * zfs_vnop_lookup serves as the bridge between Windows and Unix
 * and will assign FileObject->FsContext as appropriate, with usecount set
 * when required, but it will not hold iocount.
 */
int zfs_vnop_lookup_impl(PIRP Irp, PIO_STACK_LOCATION IrpSp, mount_t *zmo, char *filename, vattr_t *vap)
{
	int error;
	cred_t *cr = NULL;
	char *finalname;
	char *brkt = NULL;
	char *word = NULL;
	PFILE_OBJECT FileObject;
	ULONG outlen;
	struct vnode *dvp = NULL;
	struct vnode *vp = NULL;
	znode_t *zp = NULL;
	struct componentname cn;
	ULONG Options;
	BOOLEAN CreateDirectory;
	BOOLEAN NoIntermediateBuffering;
	BOOLEAN OpenDirectory;
	BOOLEAN IsPagingFile;
	BOOLEAN OpenTargetDirectory;
	BOOLEAN DirectoryFile;
	BOOLEAN NonDirectoryFile;
	BOOLEAN NoEaKnowledge;
	BOOLEAN DeleteOnClose;
	BOOLEAN OpenRequiringOplock;
	BOOLEAN TemporaryFile;
	BOOLEAN OpenRoot;
	BOOLEAN CreateFile;
	BOOLEAN FileOpenByFileId;
	ULONG CreateDisposition;
	zfsvfs_t *zfsvfs = vfs_fsprivate(zmo);
	int flags = 0;
	int dvp_no_rele = 0;
	char *stream_name = NULL;
	boolean_t UndoShareAccess = FALSE;
	NTSTATUS Status = STATUS_SUCCESS;
	ACCESS_MASK granted_access = 0;

	if (zfsvfs == NULL) return STATUS_OBJECT_PATH_NOT_FOUND;

	FileObject = IrpSp->FileObject;
	Options = IrpSp->Parameters.Create.Options;

	dprintf("%s: enter\n", __func__);

	if (FileObject->RelatedFileObject != NULL) {
		FileObject->Vpb = FileObject->RelatedFileObject->Vpb;
		//  A relative open must be via a relative path.
		if (FileObject->FileName.Length != 0 &&
			FileObject->FileName.Buffer[0] == L'\\') {
			return STATUS_INVALID_PARAMETER;
		}
	} else {
		FileObject->Vpb = zmo->vpb;
	}
	
	DirectoryFile = BooleanFlagOn(Options, FILE_DIRECTORY_FILE);
	NonDirectoryFile = BooleanFlagOn(Options, FILE_NON_DIRECTORY_FILE);
	NoIntermediateBuffering = BooleanFlagOn(Options, FILE_NO_INTERMEDIATE_BUFFERING);
	NoEaKnowledge = BooleanFlagOn(Options, FILE_NO_EA_KNOWLEDGE);
	DeleteOnClose = BooleanFlagOn(Options, FILE_DELETE_ON_CLOSE);
	FileOpenByFileId = BooleanFlagOn(Options, FILE_OPEN_BY_FILE_ID);

	// Should be passed an 8 byte FileId instead.
	if (FileOpenByFileId && FileObject->FileName.Length != sizeof(ULONGLONG))
		return STATUS_INVALID_PARAMETER;

	TemporaryFile = BooleanFlagOn(IrpSp->Parameters.Create.FileAttributes,
		FILE_ATTRIBUTE_TEMPORARY);

	CreateDisposition = (Options >> 24) & 0x000000ff;

	IsPagingFile = BooleanFlagOn(IrpSp->Flags, SL_OPEN_PAGING_FILE);
	ASSERT(!IsPagingFile);
	//ASSERT(!OpenRequiringOplock);
	// Open the directory instead of the file
	OpenTargetDirectory = BooleanFlagOn(IrpSp->Flags, SL_OPEN_TARGET_DIRECTORY);
	/*
	 *	CreateDisposition value	Action if file exists	Action if file does not exist  UNIX Perms
		FILE_SUPERSEDE		Replace the file.		    Create the file.               Unlink + O_CREAT | O_TRUNC
		FILE_CREATE		    Return an error.		    Create the file.               O_CREAT | O_EXCL
		FILE_OPEN		    Open the file.		        Return an error.               0
		FILE_OPEN_IF		Open the file.		        Create the file.               O_CREAT
		FILE_OVERWRITE		Open the file, overwrite it.	Return an error.           O_TRUNC
		FILE_OVERWRITE_IF	Open the file, overwrite it.	Create the file.           O_CREAT | O_TRUNC

		Apparently SUPERSEDE is more of less Unlink entry before recreate, so it loses ACLs, XATTRs and NamedStreams.

		IoStatus return codes:
		FILE_CREATED
		FILE_OPENED
		FILE_OVERWRITTEN
		FILE_SUPERSEDED
		FILE_EXISTS
		FILE_DOES_NOT_EXIST

	*/
	
	// Dir create/open is straight forward, do that here
	// Files are harder, do that once we know if it exists.
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


	// If it is a volumeopen, we just grab rootvp so that directory listings work
	if (FileObject->FileName.Length == 0 && FileObject->RelatedFileObject == NULL) {
		// If DirectoryFile return STATUS_NOT_A_DIRECTORY
		// If OpenTargetDirectory return STATUS_INVALID_PARAMETER
		dprintf("Started NULL open, returning root of mount\n");
		error = zfs_zget(zfsvfs, zfsvfs->z_root, &zp);
		if (error != 0) return FILE_DOES_NOT_EXIST;  // No root dir?!

		dvp = ZTOV(zp);
		vnode_ref(dvp); // Hold open reference, until CLOSE

		zfs_couplefileobject(dvp, FileObject, 0ULL);
		VN_RELE(dvp);

		Irp->IoStatus.Information = FILE_OPENED;
		return STATUS_SUCCESS;
	}

	// No name conversion with FileID

	if (!FileOpenByFileId) {

		if (FileObject->FileName.Buffer != NULL && FileObject->FileName.Length > 0) {
			// Convert incoming filename to utf8
			error = RtlUnicodeToUTF8N(filename, PATH_MAX, &outlen,
				FileObject->FileName.Buffer, FileObject->FileName.Length);

			if (error != STATUS_SUCCESS &&
				error != STATUS_SOME_NOT_MAPPED) {
				dprintf("RtlUnicodeToUTF8N returned 0x%x input len %d\n", error, FileObject->FileName.Length);
				return STATUS_OBJECT_NAME_INVALID;
			}
			//ASSERT(error != STATUS_SOME_NOT_MAPPED);
			// Output string is only null terminated if input is, so do so now.
			filename[outlen] = 0;
			dprintf("%s: converted name is '%s' input len bytes %d (err %d) %s %s\n", __func__, filename, FileObject->FileName.Length, error,
				DeleteOnClose ? "DeleteOnClose" : "",
				IrpSp->Flags&SL_CASE_SENSITIVE ? "CaseSensitive" : "CaseInsensitive");

			if (Irp->Overlay.AllocationSize.QuadPart > 0)
				dprintf("AllocationSize requested %llu\n", Irp->Overlay.AllocationSize.QuadPart);

			// Check if we are called as VFS_ROOT();
			OpenRoot = (strncmp("\\", filename, PATH_MAX) == 0 || strncmp("\\*", filename, PATH_MAX) == 0);

			if (OpenRoot) {

				error = zfs_zget(zfsvfs, zfsvfs->z_root, &zp);

				if (error == 0) {
					vp = ZTOV(zp);
					zfs_couplefileobject(vp, FileObject, zp->z_size);
					vnode_ref(vp); // Hold open reference, until CLOSE
					VN_RELE(vp);

					Irp->IoStatus.Information = FILE_OPENED;
					return STATUS_SUCCESS;
				}

				Irp->IoStatus.Information = FILE_DOES_NOT_EXIST;
				return STATUS_OBJECT_PATH_NOT_FOUND;
			} // OpenRoot

		} else { // If got filename

			// If no filename, we should fail, unless related is set.
			if (FileObject->RelatedFileObject == NULL) {
				// Fail
				return STATUS_OBJECT_NAME_INVALID;
			}
			// Related set, return it as opened.
			dvp = FileObject->RelatedFileObject->FsContext;
			zp = VTOZ(dvp);
			dprintf("%s: Relative null-name open: '%s'\n", __func__, zp->z_name_cache);
			// Check types
			if (NonDirectoryFile && vnode_isdir(dvp)) {
				Irp->IoStatus.Information = FILE_DOES_NOT_EXIST;
				return STATUS_FILE_IS_A_DIRECTORY; // wanted file, found dir error
			}
			if (DirectoryFile && !vnode_isdir(dvp)) {
				Irp->IoStatus.Information = FILE_DOES_NOT_EXIST;
				return STATUS_NOT_A_DIRECTORY; // wanted dir, found file error
			}
			// Grab vnode to ref
			if (VN_HOLD(dvp) == 0) {
				vnode_ref(dvp); // Hold open reference, until CLOSE
				zfs_couplefileobject(dvp, FileObject, 0ULL);
				VN_RELE(dvp);
			} else {
				Irp->IoStatus.Information = 0;
				return STATUS_OBJECT_PATH_NOT_FOUND;
			}
			Irp->IoStatus.Information = FILE_OPENED;
			return STATUS_SUCCESS;
		}

		// We have converted the filename, continue..
		if (FileObject->RelatedFileObject && FileObject->RelatedFileObject->FsContext) {
			dvp = FileObject->RelatedFileObject->FsContext;

			// This branch here, if failure, should not release dvp
			dvp_no_rele = 1;
		}

		/* 
		 * Here, we want to check for Streams, which come in the syntax filename.ext:Stream:Type
		 *    Type: appears optional, or we handle ":DATA". All others will be rejected.
		 *  Stream: name of the stream, we convert this into XATTR named Stream
		 * It is valid to create a filename containing colons, so who knows what will
		 * happen here.
		 */
		error = stream_parse(filename, &stream_name);
		if (error) {
			Irp->IoStatus.Information = 0;
			return STATUS_INVALID_PARAMETER;
		}
		if (stream_name != NULL) 
			dprintf("%s: Parsed out streamname '%s'\n", __func__, stream_name);




		// There is a special case, where name is just the stream ":ZoneIdentifier:$DATA", and
		// RelatedFileObject is set to the object.
		if (stream_name != NULL &&
			FileObject->RelatedFileObject && FileObject->RelatedFileObject->FsContext &&
			strlen(filename) == 0) {

			// The RelatedFileObject conditional above will assign "dvp" - but
			// the stream_name check below will expect it in "vp". dvp_no_rele is already set.
			ASSERT(dvp_no_rele == 1);
			vp = FileObject->RelatedFileObject->FsContext;
			dvp = NULL;
			VERIFY0(VN_HOLD(vp));

		} else {

			// If we have dvp, it is HELD
			error = zfs_find_dvp_vp(zfsvfs, filename, (CreateFile || OpenTargetDirectory), (CreateDisposition == FILE_CREATE), &finalname, &dvp, &vp, flags);

		}

	} else {  // Open By File ID

		error = zfs_zget(zfsvfs, *((uint64_t *)IrpSp->FileObject->FileName.Buffer), &zp);
		// Code below assumed dvp is also open
		if (error == 0) {
			uint64_t parent;
			znode_t *dzp;
			error = sa_lookup(zp->z_sa_hdl, SA_ZPL_PARENT(zfsvfs), &parent, sizeof(parent));
			if (error == 0) {
				error = zfs_zget(zfsvfs, parent, &dzp);
			}
			if (error != 0) {
				VN_RELE(ZTOV(zp));
				return error;
			} // failed to get parentid, or find parent
			// Copy over the vp info for below, both are held.
			vp = ZTOV(zp);
			dvp = ZTOV(dzp);
		}
	}
		
	// If successful:
	// - vp is HELD
	// - dvp is HELD
	// we need dvp from here on down.
	

	if (error) {

		if (dvp && !dvp_no_rele) VN_RELE(dvp);
		if (vp) VN_RELE(vp);

		if (error == STATUS_REPARSE) {
			REPARSE_DATA_BUFFER *rpb = (REPARSE_DATA_BUFFER *)finalname;
			Irp->IoStatus.Information = rpb->ReparseTag;
			Irp->Tail.Overlay.AuxiliaryBuffer = (void*)rpb;
			return error;
		}

		if (!dvp && error == ESRCH) {
			dprintf("%s: failed to find dvp for '%s' \n", __func__, filename);
			Irp->IoStatus.Information = FILE_DOES_NOT_EXIST;
			return STATUS_OBJECT_PATH_NOT_FOUND;
		}
		if (error == STATUS_OBJECT_NAME_INVALID) {
			dprintf("%s: filename component too long\n", __func__);
			return error;
		}
		// Open dir with FILE_CREATE but it exists
		if (error == EEXIST) {
			dprintf("%s: dir exists, wont create\n", __func__);
			Irp->IoStatus.Information = FILE_EXISTS;
			return STATUS_OBJECT_NAME_COLLISION;
		}
		// A directory component did not exist, or was a file
		if ((dvp == NULL) || (error == ENOTDIR)) {
			dprintf("%s: failed to find dvp - or dvp is a file\n", __func__);
			Irp->IoStatus.Information = 0;
			return STATUS_OBJECT_NAME_NOT_FOUND;
		}
		dprintf("%s: failed to find vp in dvp\n", __func__);
		Irp->IoStatus.Information = FILE_DOES_NOT_EXIST;
		return STATUS_OBJECT_NAME_NOT_FOUND;
	}

	// Streams
	// If we opened vp, grab its xattrdir, and try to to locate stream
	if (stream_name != NULL && vp != NULL) {
		// Here, we will release dvp, and attempt to open the xattr dir.
		// xattr dir will be the new dvp. Then we will look for streamname
		// in xattrdir, and assign vp.
		if (dvp_no_rele)
			VN_RELE(dvp);
		// Create the xattrdir only if we are to create a new entry
		if (error = zfs_get_xattrdir(VTOZ(vp), &dvp, cr, CreateFile ? CREATE_XATTR_DIR : 0)) {
			VN_RELE(vp);
			Irp->IoStatus.Information = FILE_DOES_NOT_EXIST;
			return STATUS_OBJECT_NAME_NOT_FOUND;
		}
		VN_RELE(vp);
		vp = NULL;
		int direntflags = 0; // To detect ED_CASE_CONFLICT
		error = zfs_dirlook(VTOZ(dvp), stream_name, &vp, 0 /*FIGNORECASE*/, &direntflags, NULL);
		// Here, it may not exist, as we are to create it.
		finalname = stream_name;
	}


	if (OpenTargetDirectory) {
		if (dvp) {

#if 0
			// If we asked for PARENT of a non-existing file, do we return error?
			if (vp == NULL) {
				dprintf("%s: opening PARENT directory - but file is ENOENT\n", __func__);
				VN_RELE(dvp);
				Irp->IoStatus.Information = FILE_DOES_NOT_EXIST;
				return STATUS_OBJECT_NAME_NOT_FOUND;
			}
#endif

			dprintf("%s: opening PARENT directory\n", __func__);
			zfs_couplefileobject(dvp, FileObject, 0ULL);
			vnode_ref(dvp); // Hold open reference, until CLOSE
			if (DeleteOnClose) 
				Status = zfs_setunlink(FileObject, dvp);

			if (Status == STATUS_SUCCESS)
				Irp->IoStatus.Information = FILE_OPENED;

			if (vp) VN_RELE(vp); //xxx
			VN_RELE(dvp);
			return Status;
		}
		ASSERT(vp == NULL);
		ASSERT(dvp == NULL);
		Irp->IoStatus.Information = FILE_DOES_NOT_EXIST;
		return STATUS_OBJECT_NAME_NOT_FOUND;
	}

	// Here we have "dvp" of the directory.
	// "vp" if the final part was a file.



	// Don't create if FILE_OPEN_IF (open existing)
	if ((CreateDisposition == FILE_OPEN_IF) && (vp != NULL))
		CreateDirectory = 0;

	// Fail if FILE_CREATE but target exist
	if ((CreateDisposition == FILE_CREATE) && (vp != NULL)) {
		VN_RELE(vp);
		VN_RELE(dvp);
		Irp->IoStatus.Information = FILE_EXISTS;
		return STATUS_OBJECT_NAME_COLLISION; // create file error
	}

	if (CreateDirectory && finalname) {

		if (TemporaryFile) 
			return STATUS_INVALID_PARAMETER;

		if (zfsvfs->z_rdonly || vfs_isrdonly(zfsvfs->z_vfs) || 
			!spa_writeable(dmu_objset_spa(zfsvfs->z_os))) {
			VN_RELE(dvp);
			Irp->IoStatus.Information = 0; // ?
			return STATUS_MEDIA_WRITE_PROTECTED;
		}

		vap->va_type = VDIR;
		// Set default 777 if something else wasn't passed in
		if (!(vap->va_mask & ATTR_MODE))
			vap->va_mode = 0777;
		vap->va_mask |= (ATTR_MODE | ATTR_TYPE);

		ASSERT(strchr(finalname, '\\') == NULL);
		error = zfs_mkdir(VTOZ(dvp), finalname, vap, &zp, NULL, 
			NULL, 0);
		if (error == 0) {
			vp = ZTOV(zp);
			zfs_couplefileobject(vp, FileObject, 0ULL);
			vnode_ref(vp); // Hold open reference, until CLOSE
			if (DeleteOnClose)
				Status = zfs_setunlink(FileObject, dvp);

			if (Status == STATUS_SUCCESS) {
				Irp->IoStatus.Information = FILE_CREATED;

				// Update pflags, if needed
				zfs_setwinflags(zp, IrpSp->Parameters.Create.FileAttributes);

				IoSetShareAccess(IrpSp->Parameters.Create.SecurityContext->DesiredAccess,
					IrpSp->Parameters.Create.ShareAccess,
					FileObject,
					&vp->share_access);

				zfs_send_notify(zfsvfs, zp->z_name_cache, zp->z_name_offset,
					FILE_NOTIFY_CHANGE_DIR_NAME,
					FILE_ACTION_ADDED);
			}
			VN_RELE(vp);
			VN_RELE(dvp);
			return Status;
		}
		VN_RELE(dvp);
		Irp->IoStatus.Information = FILE_DOES_NOT_EXIST;
		return STATUS_OBJECT_PATH_NOT_FOUND;  // failed to create error?
	}

	// If they requested just directory, fail non directories
	if (DirectoryFile && vp != NULL && !vnode_isdir(vp)) {
		dprintf("%s: asked for directory but found file\n", __func__);
		VN_RELE(vp);
		VN_RELE(dvp);
		Irp->IoStatus.Information = FILE_DOES_NOT_EXIST;
		return STATUS_FILE_IS_A_DIRECTORY; // wanted dir, found file error
	}

	// Asked for non-directory, but we got directory
	if (NonDirectoryFile && !CreateFile && vp == NULL) {
		dprintf("%s: asked for file but found directory\n", __func__);
		VN_RELE(dvp);
		Irp->IoStatus.Information = FILE_DOES_NOT_EXIST;
		return STATUS_FILE_IS_A_DIRECTORY; // wanted file, found dir error
	}

	if (vp) {
		zp = VTOZ(vp);
	}

	// If HIDDEN and SYSTEM are set, then the open of file must also have
	// HIDDEN and SYSTEM set.
	if ((zp != NULL) &&
		((CreateDisposition == FILE_SUPERSEDE) ||
		(CreateDisposition == FILE_OVERWRITE) ||
		(CreateDisposition == FILE_OVERWRITE_IF))) {
		if (((zp->z_pflags&ZFS_HIDDEN) && !FlagOn(IrpSp->Parameters.Create.FileAttributes, FILE_ATTRIBUTE_HIDDEN)) ||
			((zp->z_pflags&ZFS_SYSTEM) && !FlagOn(IrpSp->Parameters.Create.FileAttributes, FILE_ATTRIBUTE_SYSTEM))) {
			VN_RELE(vp);
			VN_RELE(dvp);
			dprintf("%s: denied due to hidden+system combo\n", __func__);
			return STATUS_ACCESS_DENIED;
		}
	}

	// If overwrite, and tagged readonly, fail (note, supersede should succeed)
	if ((zp != NULL) &&
		((CreateDisposition == FILE_OVERWRITE) ||
		(CreateDisposition == FILE_OVERWRITE_IF))) {
		if (zp->z_pflags&ZFS_READONLY) {
			VN_RELE(vp);
			VN_RELE(dvp);
			dprintf("%s: denied due to ZFS_READONLY + OVERWRITE\n", __func__);
			return STATUS_ACCESS_DENIED;
		}
	}

	// If flags are readonly, and tries to open with write, fail
	if ((zp != NULL) && (IrpSp->Parameters.Create.SecurityContext->DesiredAccess&(FILE_WRITE_DATA | FILE_APPEND_DATA)) &&
		(zp->z_pflags&ZFS_READONLY)) {
		VN_RELE(vp);
		VN_RELE(dvp);
		dprintf("%s: denied due to ZFS_READONLY + WRITE_DATA\n", __func__);
		return STATUS_ACCESS_DENIED;
	}


	if (DeleteOnClose &&
		vp && zp &&
		dvp && VTOZ(dvp) &&
		zfs_zaccess_delete(VTOZ(dvp), zp, 0) > 0) {
			VN_RELE(vp);
			if (dvp)
				VN_RELE(dvp);

			dprintf("%s: denied due to ZFS_IMMUTABLE + ZFS_NOUNLINK\n", __func__);
			return STATUS_ACCESS_DENIED;
	}


	// Some cases we always create the file, and sometimes only if
	// it is not there. If the file exists and we are only to create
	// the file if it is not there:
	if ((CreateDisposition == FILE_OPEN_IF) && (vp != NULL))
		CreateFile = 0;


	if (vp || CreateFile == 0) {
//		NTSTATUS Status;

		// Streams do not call SeAccessCheck?
		if (stream_name != NULL) {
			IoSetShareAccess(IrpSp->Parameters.Create.SecurityContext->DesiredAccess, IrpSp->Parameters.Create.ShareAccess,
				FileObject, vp ? &vp->share_access : &dvp->share_access);


		} else if (IrpSp->Parameters.Create.SecurityContext->DesiredAccess != 0 && vp) {


			SeLockSubjectContext(&IrpSp->Parameters.Create.SecurityContext->AccessState->SubjectSecurityContext);
#if 1
			if (!SeAccessCheck(/* (fileref->fcb->ads || fileref->fcb == Vcb->dummy_fcb) ? fileref->parent->fcb->sd : */ vnode_security(vp ? vp : dvp),
				&IrpSp->Parameters.Create.SecurityContext->AccessState->SubjectSecurityContext,
				TRUE, IrpSp->Parameters.Create.SecurityContext->DesiredAccess, 0, NULL,
				IoGetFileObjectGenericMapping(), IrpSp->Flags & SL_FORCE_ACCESS_CHECK ? UserMode : Irp->RequestorMode,
				&granted_access, &Status)) {
				SeUnlockSubjectContext(&IrpSp->Parameters.Create.SecurityContext->AccessState->SubjectSecurityContext);
				if (vp) VN_RELE(vp);
				VN_RELE(dvp);
				dprintf("%s: denied due to SeAccessCheck()\n", __func__);
				return Status;
			}
#endif
			SeUnlockSubjectContext(&IrpSp->Parameters.Create.SecurityContext->AccessState->SubjectSecurityContext);
		} else {
			granted_access = 0;
		}
 
		// Io*ShareAccess(): X is not an atomic operation. Therefore, drivers calling this routine must protect the shared file object
		vnode_lock(vp ? vp : dvp);
		if (vnode_isinuse(vp ? vp : dvp, 0)) {  // 0 is we are the only (usecount added below), 1+ if already open.
			Status = IoCheckShareAccess(granted_access, IrpSp->Parameters.Create.ShareAccess, FileObject, vp ? &vp->share_access : &dvp->share_access, FALSE);
			if (!NT_SUCCESS(Status)) {
				vnode_unlock(vp ? vp : dvp);
				if (vp) VN_RELE(vp);
				VN_RELE(dvp);
				dprintf("%s: denied due to IoCheckShareAccess\n", __func__);
				return Status;
			}
			IoUpdateShareAccess(FileObject, vp ? &vp->share_access : &dvp->share_access);
		} else {
			IoSetShareAccess(granted_access, IrpSp->Parameters.Create.ShareAccess, FileObject, vp ? &vp->share_access : &dvp->share_access);
		}
		// Since we've updated ShareAccess here, if we cancel the open we need to undo it.
		UndoShareAccess = TRUE;
		vnode_unlock(vp ? vp : dvp);
	}

#define UNDO_SHARE_ACCESS(vp) \
	if ((vp) && UndoShareAccess) {    \
		vnode_lock((vp));     \
		IoRemoveShareAccess(FileObject, &(vp)->share_access); \
		vnode_unlock((vp));   \
	}


	// We can not DeleteOnClose if readonly filesystem
	if (DeleteOnClose) {
		if (zfsvfs->z_rdonly || vfs_isrdonly(zfsvfs->z_vfs) ||
			!spa_writeable(dmu_objset_spa(zfsvfs->z_os))) {
			UNDO_SHARE_ACCESS(vp);
			if (vp) VN_RELE(vp);
			VN_RELE(dvp);
			Irp->IoStatus.Information = 0; // ?
			return STATUS_MEDIA_WRITE_PROTECTED;
		}
	}

	if (CreateFile && finalname) {
		int replacing = 0;

		if (zfsvfs->z_rdonly || vfs_isrdonly(zfsvfs->z_vfs) ||
			!spa_writeable(dmu_objset_spa(zfsvfs->z_os))) {
			UNDO_SHARE_ACCESS(vp);
			if (vp) VN_RELE(vp);
			VN_RELE(dvp);
			Irp->IoStatus.Information = 0; // ?
			return STATUS_MEDIA_WRITE_PROTECTED;
		}

		// Would we replace file?
		if (vp) {
			VN_RELE(vp);
			vp = NULL;
			replacing = 1;
		}

		vap->va_type = VREG;
		if (!(vap->va_mask & ATTR_MODE))
			vap->va_mode = 0777;
		vap->va_mask = (ATTR_MODE | ATTR_TYPE);

		// If O_TRUNC:
		switch (CreateDisposition) {
		case FILE_SUPERSEDE:
		case FILE_OVERWRITE_IF:
		case FILE_OVERWRITE:
			vap->va_mask |= ATTR_SIZE;
			vap->va_size = 0;
			break;
		}

		// O_EXCL only if FILE_CREATE
		error = zfs_create(VTOZ(dvp), finalname, vap, CreateDisposition == FILE_CREATE, vap->va_mode, &zp, NULL, 0, NULL);
		if (error == 0) {

			vp = VTOZ(zp);

			zfs_couplefileobject(vp, FileObject, zp?zp->z_size:0ULL);
			vnode_ref(vp); // Hold open reference, until CLOSE

			if (DeleteOnClose) 
				Status = zfs_setunlink(FileObject, dvp);

			if (Status == STATUS_SUCCESS) {

				Irp->IoStatus.Information = replacing ? CreateDisposition == FILE_SUPERSEDE ?
					FILE_SUPERSEDED : FILE_OVERWRITTEN : FILE_CREATED;

				// Update pflags, if needed
				zfs_setwinflags(zp, IrpSp->Parameters.Create.FileAttributes | FILE_ATTRIBUTE_ARCHIVE);

				// Did they ask for an AllocationSize
				if (Irp->Overlay.AllocationSize.QuadPart > 0) {
					uint64_t allocsize = Irp->Overlay.AllocationSize.QuadPart;
					//zp->z_blksz = P2ROUNDUP(allocsize, 512);
				}

				vnode_lock(vp);
				IoSetShareAccess(IrpSp->Parameters.Create.SecurityContext->DesiredAccess,
					IrpSp->Parameters.Create.ShareAccess,
					FileObject,
					&vp->share_access);
				vnode_unlock(vp);

				if (stream_name == NULL)
					zfs_send_notify(zfsvfs, zp->z_name_cache, zp->z_name_offset,
						FILE_NOTIFY_CHANGE_FILE_NAME,
						FILE_ACTION_ADDED);
				else
					zfs_send_notify_stream(zfsvfs, zp->z_name_cache, zp->z_name_offset,
						FILE_NOTIFY_CHANGE_STREAM_NAME,
						FILE_ACTION_ADDED_STREAM,
						stream_name);

			}
			VN_RELE(vp);
			VN_RELE(dvp);
			return Status;
		}
		if (error == EEXIST)
			Irp->IoStatus.Information = FILE_EXISTS;
		else
			Irp->IoStatus.Information = FILE_DOES_NOT_EXIST;

		UNDO_SHARE_ACCESS(dvp);
		VN_RELE(dvp);
		return STATUS_OBJECT_NAME_COLLISION; // create file error
	}


	// Just open it, if the open was to a directory, add ccb
	ASSERT(IrpSp->FileObject->FsContext == NULL);
	if (vp == NULL) {
		zfs_couplefileobject(dvp, FileObject, 0ULL);
		vnode_ref(dvp); // Hold open reference, until CLOSE
		if (DeleteOnClose) 
			Status = zfs_setunlink(FileObject, dvp);

		if(Status == STATUS_SUCCESS) {
			if (UndoShareAccess == FALSE) {
				vnode_lock(dvp);
				IoSetShareAccess(IrpSp->Parameters.Create.SecurityContext->DesiredAccess,
					IrpSp->Parameters.Create.ShareAccess,
					FileObject,
					&dvp->share_access);
				vnode_unlock(dvp);
			}
		} else {
			UNDO_SHARE_ACCESS(dvp);
		}
		VN_RELE(dvp);
	} else {
		// Technically, this should call zfs_open() - but it is mostly empty
		zfs_couplefileobject(vp, FileObject, zp->z_size);
		vnode_ref(vp); // Hold open reference, until CLOSE
		if (DeleteOnClose)
			Status = zfs_setunlink(FileObject, dvp);

		if(Status == STATUS_SUCCESS) {

			Irp->IoStatus.Information = FILE_OPENED;
			// Did they set the open flags (clearing archive?)
			if (IrpSp->Parameters.Create.FileAttributes)
				zfs_setwinflags(zp, IrpSp->Parameters.Create.FileAttributes);
			// If we are to truncate the file:
			if (CreateDisposition == FILE_OVERWRITE) {
				Irp->IoStatus.Information = FILE_OVERWRITTEN;
				zp->z_pflags |= ZFS_ARCHIVE;
				// zfs_freesp() path uses vnode_pager_setsize() so we need to make sure fileobject is set.
				zfs_freesp(zp, 0, 0, FWRITE, B_TRUE);
				// Did they ask for an AllocationSize
				if (Irp->Overlay.AllocationSize.QuadPart > 0) {
					uint64_t allocsize = Irp->Overlay.AllocationSize.QuadPart;
					//zp->z_blksz = P2ROUNDUP(allocsize, 512);
				}
			}
			// Update sizes in header.
			vp->FileHeader.AllocationSize.QuadPart = P2ROUNDUP(zp->z_size, zp->z_blksz);
			vp->FileHeader.FileSize.QuadPart = zp->z_size;
			vp->FileHeader.ValidDataLength.QuadPart = zp->z_size;
			// If we created something new, add this permission
			if (UndoShareAccess == FALSE) {
				vnode_lock(vp);
				IoSetShareAccess(IrpSp->Parameters.Create.SecurityContext->DesiredAccess,
					IrpSp->Parameters.Create.ShareAccess,
					FileObject,
					&vp->share_access);
				vnode_unlock(vp);
			}
		} else {
			UNDO_SHARE_ACCESS(vp);
		}
		VN_RELE(vp);
		VN_RELE(dvp);
	}

	IrpSp->Parameters.Create.SecurityContext->AccessState->PreviouslyGrantedAccess |= granted_access;
	IrpSp->Parameters.Create.SecurityContext->AccessState->RemainingDesiredAccess &= ~(granted_access | MAXIMUM_ALLOWED);

	return Status;
}

int zfs_vnop_lookup(PIRP Irp, PIO_STACK_LOCATION IrpSp, mount_t *zmo)
{
	int status;
	char *filename = NULL;
	vattr_t vap = { 0 };

	// Check the EA buffer is good, if supplied.
	if (Irp->AssociatedIrp.SystemBuffer != NULL &&
		IrpSp->Parameters.Create.EaLength > 0) {
		ULONG offset;
		status = IoCheckEaBufferValidity(Irp->AssociatedIrp.SystemBuffer, IrpSp->Parameters.Create.EaLength, &offset);
		if (!NT_SUCCESS(status)) {
			dprintf("IoCheckEaBufferValidity returned %08x (error at offset %u)\n", status, offset);
			return status;
		}
	}

	// Allocate space to hold name, must be freed from here on
	filename = kmem_alloc(PATH_MAX, KM_SLEEP);

	// Deal with ExtraCreateParameters
#if defined (NTDDI_WIN10_RS5) && (NTDDI_VERSION >= NTDDI_WIN10_RS5)
	/* Check for ExtraCreateParameters */
	PECP_LIST ecp = NULL;
	PQUERY_ON_CREATE_ECP_CONTEXT qocContext = NULL;
	FsRtlGetEcpListFromIrp(Irp, &ecp);
	if (ecp) {
		GUID ecpType;
		VOID *ecpContext = NULL;
		ULONG ecpContextSize;
		while (NT_SUCCESS(FsRtlGetNextExtraCreateParameter(ecp, ecpContext,
			&ecpType, &ecpContext, &ecpContextSize))) {
			if (IsEqualGUID(&ecpType, &GUID_ECP_ATOMIC_CREATE)) {
				dprintf("GUID_ECP_ATOMIC_CREATE\n");
				// More code to come here:
			} else if (IsEqualGUID(&ecpType, &GUID_ECP_QUERY_ON_CREATE)) {
				dprintf("GUID_ECP_QUERY_ON_CREATE\n");
				// It wants a getattr call on success, before we finish up
				qocContext = (PQUERY_ON_CREATE_ECP_CONTEXT)ecpContext;
			} else if (IsEqualGUID(&ecpType, &GUID_ECP_CREATE_REDIRECTION)) {
				dprintf("GUID_ECP_CREATE_REDIRECTION\n");
				// We get this one a lot.
			} else {
				dprintf("Other GUID_ECP type\n"); // IopSymlinkECPGuid "73d5118a-88ba-439f-92f4-46d38952d250"
			}
		}// while
	} // if ecp
#endif

	// The associated buffer on a CreateFile is an EA buffer.
	// Already Verified above - do a quickscan of any EAs we
	// handle in a special way, before we call zfs_vnop_lookup_impl().
	// We handle the regular EAs afterward.
	if (Irp->AssociatedIrp.SystemBuffer != NULL &&
		IrpSp->Parameters.Create.EaLength > 0) {
		PFILE_FULL_EA_INFORMATION ea;
		for (ea = (PFILE_FULL_EA_INFORMATION)Irp->AssociatedIrp.SystemBuffer; 
			; 
			ea = (PFILE_FULL_EA_INFORMATION)((uint8_t*)ea + ea->NextEntryOffset)) {
			// only parse $LX attrs right now -- things we can store before the file
			// gets created.
			if (vattr_apply_lx_ea(&vap, ea)) {
				dprintf("  encountered special attrs EA '%.*s'\n", ea->EaNameLength, ea->EaName);
			}
			if (ea->NextEntryOffset == 0)
				break;
		}
	}



	// Call ZFS
	status = zfs_vnop_lookup_impl(Irp, IrpSp, zmo, filename, &vap);



#if defined (NTDDI_WIN10_RS5) && (NTDDI_VERSION >= NTDDI_WIN10_RS5)
	// Did ECP ask for getattr to be returned? None, one or both can be set.
	// This requires vnode_couplefileobject() was called
	if (NT_SUCCESS(status) && qocContext && IrpSp->FileObject->FsContext) {

		ULONG classes = 0;

		// Handle RS5 >= version < 19H1 when the struct had "Flags".
#if defined (NTDDI_WIN10_19H1) && (NTDDI_VERSION >= NTDDI_WIN10_19H1)
		classes = qocContext->RequestedClasses;
#else
		classes = qocContext->Flags;
#endif

		if (BooleanFlagOn(classes, QoCFileStatInformation)) {
			file_stat_information(IrpSp->DeviceObject, Irp, IrpSp,
				&qocContext->StatInformation);
		}
		if (BooleanFlagOn(classes, QoCFileLxInformation)) {
			file_stat_lx_information(IrpSp->DeviceObject, Irp, IrpSp,
				&qocContext->LxInformation);
		}
		if (BooleanFlagOn(classes, QoCFileEaInformation)) {
			dprintf("%s: unsupported QoC: QoCFileEaInformation\n");
		}
#if defined (NTDDI_WIN10_19H1) && (NTDDI_VERSION >= NTDDI_WIN10_19H1)
		// We should fill this in, right? Only set those we understand.
		qocContext->ClassesProcessed = classes & (QoCFileStatInformation|QoCFileLxInformation);
		qocContext->ClassesWithErrors = 0;
		qocContext->ClassesWithNoData = 0;
#endif

		FsRtlAcknowledgeEcp(qocContext);
	}
#endif

	// Now handle proper EAs properly
	if (NT_SUCCESS(status)) {
		if (Irp->AssociatedIrp.SystemBuffer &&
			IrpSp->FileObject->FsContext) {
			// Second pass: this will apply all EAs that are not only LX EAs
			vnode_apply_eas(IrpSp->FileObject->FsContext,
				(PFILE_FULL_EA_INFORMATION)Irp->AssociatedIrp.SystemBuffer,
				IrpSp->Parameters.Create.EaLength, NULL);
		}

		if (!BooleanFlagOn(IrpSp->Parameters.Create.Options, FILE_NO_INTERMEDIATE_BUFFERING)) {
			IrpSp->FileObject->Flags |= FO_CACHE_SUPPORTED;
		}
	}

	// Free filename
	kmem_free(filename, PATH_MAX);

	dprintf("%s: %s with %s\n", __func__,
		common_status_str(status), create_reply(status, Irp->IoStatus.Information));

	return status;
}


/*
 * reclaim is called when a vnode is to be terminated,
 * VFS (spl-vnode.c) will hold iocount == 1, usecount == 0
 * so release associated ZFS node, and free everything
 */
int zfs_vnop_reclaim(struct vnode *vp)
{
	znode_t *zp = VTOZ(vp);
	if (zp == NULL) {
		ASSERT("NULL zp in reclaim?");
		return 0;
	}

	zfsvfs_t *zfsvfs = zp->z_zfsvfs;
	boolean_t fastpath;

	dprintf("  zfs_vnop_recycle: releasing zp %p and vp %p: '%s'\n", zp, vp,
		zp->z_name_cache ? zp->z_name_cache : "");

	void *sd = vnode_security(vp);
	if (sd != NULL)
		ExFreePool(sd);
	vnode_setsecurity(vp, NULL);

	// Decouple the nodes
	ASSERT(ZTOV(zp) != (vnode_t*)0xdeadbeefdeadbeef);

	mutex_enter(&zp->z_lock);
	ZTOV(zp) = NULL;
	vnode_clearfsnode(vp); /* vp->v_data = NULL */
	mutex_exit(&zp->z_lock);
	//vnode_removefsref(vp); /* ADDREF from vnode_create */

	vp = NULL;

	if (zp->z_name_cache != NULL)
		kmem_free(zp->z_name_cache, zp->z_name_len);
	zp->z_name_cache = NULL;
	zp->z_name_len = 0x12345678; // DBG: show we have been reclaimed

	fastpath = zp->z_fastpath;
	zp->z_fastpath = B_FALSE;

	// Release znode
	/*
	* This will release as much as it can, based on reclaim_reentry,
	* if we are from fastpath, we do not call free here, as zfs_remove
	* calls zfs_znode_delete() directly.
	* zfs_zinactive() will leave earlier if z_reclaim_reentry is true.
	*/
	if (fastpath == B_FALSE) {
		rw_enter(&zfsvfs->z_teardown_inactive_lock, RW_READER);
		if (zp->z_sa_hdl == NULL)
			zfs_znode_free(zp);
		else
			zfs_zinactive(zp);
		rw_exit(&zfsvfs->z_teardown_inactive_lock);
	}

	atomic_dec_64(&vnop_num_vnodes);
	atomic_inc_64(&vnop_num_reclaims);

	if (vnop_num_vnodes % 1000 == 0)
		dprintf("%s: num_vnodes %llu\n", __func__, vnop_num_vnodes);
		
	return 0;
}


/*
 */
void
getnewvnode_reserve(int num)
{
}

void
getnewvnode_drop_reserve()
{
}

/*
 * Get new vnode for znode.
 *
 * This function uses zp->z_zfsvfs, zp->z_mode, zp->z_flags, zp->z_id and sets
 * zp->z_vnode and zp->z_vid.
 * If given parent, dzp, we can save some hassles. If not, looks it up internally.
 */
int
zfs_znode_getvnode(znode_t *zp, znode_t *dzp, zfsvfs_t *zfsvfs)
{
	struct vnode *vp = NULL;
	int flags = 0;
	//dprintf("getvnode zp %p with vp %p zfsvfs %p vfs %p\n", zp, vp,
	//    zfsvfs, zfsvfs->z_vfs);

	if (zp->z_vnode)
		panic("zp %p vnode already set\n", zp->z_vnode);

	// "root" / mountpoint holds long term ref
	if (zp->z_id == zfsvfs->z_root) {
		flags |= VNODE_MARKROOT;
	}

	/*
	 * vnode_create() has a habit of calling both vnop_reclaim() and
	 * vnop_fsync(), which can create havok as we are already holding locks.
	 */
	vnode_create(zfsvfs->z_vfs, zp, IFTOVT((mode_t)zp->z_mode), flags, &vp);

	atomic_inc_64(&vnop_num_vnodes);

	//dprintf("Assigned zp %p with vp %p\n", zp, vp);
	zp->z_vid = vnode_vid(vp);
	zp->z_vnode = vp;

	// Build a fullpath string here, for Notifications and set_name_information
	ASSERT(zp->z_name_cache == NULL);
	if (zfs_build_path(zp, NULL, &zp->z_name_cache, &zp->z_name_len, &zp->z_name_offset) == -1)
		dprintf("%s: failed to build fullpath\n", __func__);

	// Assign security here. But, if we are XATTR, we do not? In Windows, it refers to Streams
	// and they do not have Scurity?
	if (zp->z_pflags & ZFS_XATTR)
		;
	else
		zfs_set_security(vp, dzp && ZTOV(dzp) ? ZTOV(dzp) : NULL);

	return (0);
}


NTSTATUS dev_ioctl(PDEVICE_OBJECT DeviceObject, ULONG ControlCode, PVOID InputBuffer, ULONG InputBufferSize,
	PVOID OutputBuffer, ULONG OutputBufferSize, BOOLEAN Override, IO_STATUS_BLOCK* iosb)
{
	PIRP Irp;
	KEVENT Event;
	NTSTATUS Status;
	PIO_STACK_LOCATION Stack;
	IO_STATUS_BLOCK IoStatus;

	KeInitializeEvent(&Event, NotificationEvent, FALSE);

	Irp = IoBuildDeviceIoControlRequest(ControlCode,
		DeviceObject,
		InputBuffer,
		InputBufferSize,
		OutputBuffer,
		OutputBufferSize,
		FALSE,
		&Event,
		&IoStatus);

	if (!Irp) return STATUS_INSUFFICIENT_RESOURCES;

	if (Override) {
		Stack = IoGetNextIrpStackLocation(Irp);
		Stack->Flags |= SL_OVERRIDE_VERIFY_VOLUME;
	}

	Status = IoCallDriver(DeviceObject, Irp);

	if (Status == STATUS_PENDING) {
		KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
		Status = IoStatus.Status;
	}

	if (iosb)
		*iosb = IoStatus;

	return Status;
}

// THIS IS THE PNP DEVICE ID
NTSTATUS pnp_query_id(PDEVICE_OBJECT DeviceObject, PIRP Irp, PIO_STACK_LOCATION IrpSp)
{
	mount_t *zmo;

	dprintf("%s: query id type %d\n", __func__, IrpSp->Parameters.QueryId.IdType);

	zmo = (mount_t *)DeviceObject->DeviceExtension;

	Irp->IoStatus.Information = (void *)ExAllocatePoolWithTag(PagedPool, zmo->bus_name.Length + sizeof(UNICODE_NULL), '!OIZ');
	if (Irp->IoStatus.Information == NULL) return STATUS_NO_MEMORY;

	RtlCopyMemory(Irp->IoStatus.Information, zmo->bus_name.Buffer, zmo->bus_name.Length);
	dprintf("replying with '%.*S'\n", zmo->uuid.Length/sizeof(WCHAR), Irp->IoStatus.Information);

	return STATUS_SUCCESS;
}

NTSTATUS pnp_device_state(PDEVICE_OBJECT DeviceObject, PIRP Irp, PIO_STACK_LOCATION IrpSp)
{
	dprintf("%s:\n", __func__);

	Irp->IoStatus.Information |= PNP_DEVICE_NOT_DISABLEABLE;

	return STATUS_SUCCESS;
}

NTSTATUS query_volume_information(PDEVICE_OBJECT DeviceObject, PIRP Irp, PIO_STACK_LOCATION IrpSp)
{
	NTSTATUS Status;
	int len;
	Status = STATUS_NOT_IMPLEMENTED;
	int space;

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

	switch (IrpSp->Parameters.QueryVolume.FsInformationClass) {

	case FileFsAttributeInformation:  
		//
		// If overflow, set Information to input_size and NameLength to what we fit.
		//

		dprintf("* %s: FileFsAttributeInformation\n", __func__);
		if (IrpSp->Parameters.QueryVolume.Length < sizeof(FILE_FS_ATTRIBUTE_INFORMATION)) {
			Irp->IoStatus.Information = sizeof(FILE_FS_ATTRIBUTE_INFORMATION);
			Status = STATUS_BUFFER_TOO_SMALL;
			break;
		}

/* Do not enable until we have implemented FileRenameInformationEx method. */
//#define ZFS_FS_ATTRIBUTE_POSIX

#define ZFS_FS_ATTRIBUTE_CLEANUP_INFO

		FILE_FS_ATTRIBUTE_INFORMATION *ffai = Irp->AssociatedIrp.SystemBuffer;
		ffai->FileSystemAttributes = FILE_CASE_PRESERVED_NAMES | FILE_NAMED_STREAMS |
			FILE_PERSISTENT_ACLS | FILE_SUPPORTS_OBJECT_IDS | FILE_SUPPORTS_SPARSE_FILES | FILE_VOLUME_QUOTAS |
			FILE_SUPPORTS_REPARSE_POINTS | FILE_UNICODE_ON_DISK | FILE_SUPPORTS_HARD_LINKS | FILE_SUPPORTS_OPEN_BY_FILE_ID |
			FILE_SUPPORTS_EXTENDED_ATTRIBUTES | FILE_CASE_SENSITIVE_SEARCH ;
#if defined (ZFS_FS_ATTRIBUTE_POSIX)
		ffai->FileSystemAttributes |= FILE_SUPPORTS_POSIX_UNLINK_RENAME;
#endif
#if defined (ZFS_FS_ATTRIBUTE_CLEANUP_INFO)
		ffai->FileSystemAttributes |= FILE_RETURNS_CLEANUP_RESULT_INFO;
#endif

			/*
			// NTFS has these:
			FILE_CASE_SENSITIVE_SEARCH | FILE_FILE_COMPRESSION | FILE_RETURNS_CLEANUP_RESULT_INFO | FILE_SUPPORTS_POSIX_UNLINK_RENAME |
			FILE_SUPPORTS_ENCRYPTION | FILE_SUPPORTS_TRANSACTIONS | FILE_SUPPORTS_USN_JOURNAL;
			*/

		if (zfsvfs->z_case == ZFS_CASE_SENSITIVE) 
			ffai->FileSystemAttributes |= FILE_CASE_SENSITIVE_SEARCH;

		if (zfsvfs->z_rdonly) 
			SetFlag(ffai->FileSystemAttributes, FILE_READ_ONLY_VOLUME);

		ffai->MaximumComponentNameLength = MAXNAMELEN - 1;

		// There is room for one char in the struct
		// Alas, many things compare string to "NTFS".
		space = IrpSp->Parameters.QueryVolume.Length - FIELD_OFFSET(FILE_FS_ATTRIBUTE_INFORMATION, FileSystemName);
			
		UNICODE_STRING                  name;
		RtlInitUnicodeString(&name, L"NTFS");

		space = MIN(space, name.Length);
		ffai->FileSystemNameLength = name.Length;
		RtlCopyMemory(ffai->FileSystemName, name.Buffer, space);
		Irp->IoStatus.Information = FIELD_OFFSET(FILE_FS_ATTRIBUTE_INFORMATION, FileSystemName) + space;
			
		Status = STATUS_SUCCESS;

		ASSERT(Irp->IoStatus.Information <= IrpSp->Parameters.QueryVolume.Length);
		break;
	case FileFsControlInformation:
		dprintf("* %s: FileFsControlInformation NOT IMPLEMENTED\n", __func__);
		break;
	case FileFsDeviceInformation:
		dprintf("* %s: FileFsDeviceInformation NOT IMPLEMENTED\n", __func__);
		break;
	case FileFsDriverPathInformation:
		dprintf("* %s: FileFsDriverPathInformation NOT IMPLEMENTED\n", __func__);
		break;
	case FileFsFullSizeInformation:   //**
		dprintf("* %s: FileFsFullSizeInformation\n", __func__);
		if (IrpSp->Parameters.QueryVolume.Length < sizeof(FILE_FS_FULL_SIZE_INFORMATION)) {
			Irp->IoStatus.Information = sizeof(FILE_FS_FULL_SIZE_INFORMATION);
			Status = STATUS_BUFFER_TOO_SMALL;
			break;
		}
		uint64_t refdbytes, availbytes, usedobjs, availobjs;
		dmu_objset_space(zfsvfs->z_os,
			&refdbytes, &availbytes, &usedobjs, &availobjs);

		FILE_FS_FULL_SIZE_INFORMATION *fffsi = Irp->AssociatedIrp.SystemBuffer;
		fffsi->TotalAllocationUnits.QuadPart = (refdbytes + availbytes) / 512ULL;
		fffsi->ActualAvailableAllocationUnits.QuadPart = availbytes / 512ULL;
		fffsi->CallerAvailableAllocationUnits.QuadPart = availbytes / 512ULL;
		fffsi->BytesPerSector = 512;
		fffsi->SectorsPerAllocationUnit = 1;
		Irp->IoStatus.Information = sizeof(FILE_FS_FULL_SIZE_INFORMATION);
		Status = STATUS_SUCCESS;
		break;
	case FileFsObjectIdInformation:
		dprintf("* %s: FileFsObjectIdInformation\n", __func__);
		FILE_FS_OBJECTID_INFORMATION* ffoi = Irp->AssociatedIrp.SystemBuffer;
		//RtlCopyMemory(ffoi->ObjectId, &Vcb->superblock.uuid.uuid[0], sizeof(UCHAR) * 16);
		RtlZeroMemory(ffoi->ExtendedInfo, sizeof(ffoi->ExtendedInfo));
		Irp->IoStatus.Information = sizeof(FILE_FS_OBJECTID_INFORMATION);
		Status = STATUS_OBJECT_NAME_NOT_FOUND; // returned by NTFS
		break;
	case FileFsVolumeInformation:
		dprintf("* %s: FileFsVolumeInformation\n", __func__);
		if (IrpSp->Parameters.QueryVolume.Length < sizeof(FILE_FS_VOLUME_INFORMATION)) {
			Irp->IoStatus.Information = sizeof(FILE_FS_VOLUME_INFORMATION);
			Status = STATUS_BUFFER_TOO_SMALL;
			break;
		}
		FILE_FS_VOLUME_INFORMATION *ffvi = Irp->AssociatedIrp.SystemBuffer;
		TIME_UNIX_TO_WINDOWS_EX(zfsvfs->z_last_unmount_time, 0,
			ffvi->VolumeCreationTime.QuadPart);
		ffvi->VolumeSerialNumber = 0x19831116;
		ffvi->SupportsObjects = TRUE;
		ffvi->VolumeLabelLength =
			zmo->name.Length;

		int space = IrpSp->Parameters.QueryFile.Length - FIELD_OFFSET(FILE_FS_VOLUME_INFORMATION, VolumeLabel);
		space = MIN(space, ffvi->VolumeLabelLength);

		/* 
		 * This becomes the name displayed in Explorer, so we return the
		 * dataset name here, as much as we can
		 */
		RtlCopyMemory(ffvi->VolumeLabel, zmo->name.Buffer, space);
		
		Irp->IoStatus.Information = FIELD_OFFSET(FILE_FS_VOLUME_INFORMATION, VolumeLabel) + space;

		if (space < ffvi->VolumeLabelLength) 
			Status = STATUS_BUFFER_OVERFLOW;
		else
			Status = STATUS_SUCCESS;

		break;
	case FileFsSizeInformation:   
		dprintf("* %s: FileFsSizeInformation\n", __func__);
		if (IrpSp->Parameters.QueryVolume.Length < sizeof(FILE_FS_SIZE_INFORMATION)) {
			Irp->IoStatus.Information = sizeof(FILE_FS_SIZE_INFORMATION);
			Status = STATUS_BUFFER_TOO_SMALL;
			break;
		}

		FILE_FS_SIZE_INFORMATION *ffsi = Irp->AssociatedIrp.SystemBuffer;
		ffsi->TotalAllocationUnits.QuadPart = 1024 * 1024 * 1024;
		ffsi->AvailableAllocationUnits.QuadPart = 1024 * 1024 * 1024;
		ffsi->SectorsPerAllocationUnit = 1;
		ffsi->BytesPerSector = 512;
		Irp->IoStatus.Information = sizeof(FILE_FS_SIZE_INFORMATION);
		Status = STATUS_SUCCESS;
		break;
	case FileFsSectorSizeInformation:
		dprintf("* %s: FileFsSectorSizeInformation\n", __func__);
		if (IrpSp->Parameters.QueryVolume.Length < sizeof(FILE_FS_SECTOR_SIZE_INFORMATION)) {
			Irp->IoStatus.Information = sizeof(FILE_FS_SECTOR_SIZE_INFORMATION);
			Status = STATUS_BUFFER_TOO_SMALL;
			break;
		}
		FILE_FS_SECTOR_SIZE_INFORMATION *ffssi = Irp->AssociatedIrp.SystemBuffer;
		ffssi->LogicalBytesPerSector = 512;
		ffssi->PhysicalBytesPerSectorForAtomicity = 512;
		ffssi->PhysicalBytesPerSectorForPerformance = 512;
		ffssi->FileSystemEffectivePhysicalBytesPerSectorForAtomicity = 512;
		ffssi->Flags = SSINFO_FLAGS_NO_SEEK_PENALTY;
		ffssi->ByteOffsetForSectorAlignment = SSINFO_OFFSET_UNKNOWN;
		ffssi->ByteOffsetForPartitionAlignment = SSINFO_OFFSET_UNKNOWN;
		Irp->IoStatus.Information = sizeof(FILE_FS_SECTOR_SIZE_INFORMATION);
		Status = STATUS_SUCCESS;
		break;
	default:
		dprintf("* %s: unknown class 0x%x\n", __func__, IrpSp->Parameters.QueryVolume.FsInformationClass);
		Status = STATUS_NOT_IMPLEMENTED;
		break;
	}
	ZFS_EXIT(zfsvfs);
	return Status;
}


NTSTATUS lock_control(PDEVICE_OBJECT DeviceObject, PIRP Irp, PIO_STACK_LOCATION IrpSp)
{
	NTSTATUS Status = STATUS_SUCCESS;

	dprintf("%s: FileObject %p flags 0x%x %s %s\n", __func__,
		IrpSp->FileObject, IrpSp->Flags,
		IrpSp->Flags & SL_EXCLUSIVE_LOCK ? "Exclusive" : "Shared",
		IrpSp->Flags & SL_FAIL_IMMEDIATELY ? "Nowait" : "Wait"
	);

	return Status;
}

NTSTATUS query_information(PDEVICE_OBJECT DeviceObject, PIRP Irp, PIO_STACK_LOCATION IrpSp)
{
	NTSTATUS Status = STATUS_NOT_IMPLEMENTED;
	ULONG usedspace = 0;
	struct vnode *vp = NULL;
	int normalize = 0;

	if (IrpSp->FileObject && IrpSp->FileObject->FsContext) {
		vp = IrpSp->FileObject->FsContext;
		if (VN_HOLD(vp) != 0)
			return STATUS_INVALID_PARAMETER;
	}
	
	switch (IrpSp->Parameters.QueryFile.FileInformationClass) {
			
	case FileAllInformation: 
		dprintf("%s: FileAllInformation: buffer 0x%x\n", __func__, IrpSp->Parameters.QueryFile.Length);

		if (IrpSp->Parameters.QueryFile.Length < sizeof(FILE_ALL_INFORMATION)) {
			Irp->IoStatus.Information = sizeof(FILE_ALL_INFORMATION);  // We should send Plus Filename here, to be nice, but this doesnt happen
			Status = STATUS_BUFFER_TOO_SMALL;
			break;
		}
		FILE_ALL_INFORMATION *all = Irp->AssociatedIrp.SystemBuffer;

		// Even if the name does not fit, the other information should be correct
		Status = file_basic_information(DeviceObject, Irp, IrpSp, &all->BasicInformation);
		if (Status != STATUS_SUCCESS) break;
		Status = file_standard_information(DeviceObject, Irp, IrpSp, &all->StandardInformation);
		if (Status != STATUS_SUCCESS) break;
		Status = file_position_information(DeviceObject, Irp, IrpSp, &all->PositionInformation);
		if (Status != STATUS_SUCCESS) break;
		Status = file_ea_information(DeviceObject, Irp, IrpSp, &all->EaInformation);
		if (Status != STATUS_SUCCESS) break;
#if 0
		all->AccessInformation.AccessFlags = GENERIC_ALL | GENERIC_EXECUTE | GENERIC_READ | GENERIC_WRITE;
		if (vp)
			all->ModeInformation.Mode = vnode_unlink(vp) ? FILE_DELETE_ON_CLOSE : 0;
#endif
		all->AlignmentInformation.AlignmentRequirement = 0;

		// First get the Name, to make sure we have room
		IrpSp->Parameters.QueryFile.Length -= offsetof(FILE_ALL_INFORMATION, NameInformation);
		Status = file_name_information(DeviceObject, Irp, IrpSp, &all->NameInformation, &usedspace, 0);
		IrpSp->Parameters.QueryFile.Length += offsetof(FILE_ALL_INFORMATION, NameInformation);

		// file_name_information sets FileNameLength, so update size to be ALL struct not NAME struct
		// However, there is room for one char in the struct, so subtract that from total.
		Irp->IoStatus.Information = 
			FIELD_OFFSET(FILE_ALL_INFORMATION, NameInformation) +
			FIELD_OFFSET(FILE_NAME_INFORMATION, FileName) + 
			usedspace;
		//FIELD_OFFSET(FILE_ALL_INFORMATION, NameInformation.FileName) + usedspace;

		dprintf("Struct size 0x%x FileNameLen 0x%x Information retsize 0x%x\n",
			sizeof(FILE_ALL_INFORMATION),
			all->NameInformation.FileNameLength,
			Irp->IoStatus.Information);
		break;
	case FileAttributeTagInformation:
		Status = file_attribute_tag_information(DeviceObject, Irp, IrpSp, 
			Irp->AssociatedIrp.SystemBuffer);
		break;
	case FileBasicInformation:
		Status = file_basic_information(DeviceObject, Irp, IrpSp, Irp->AssociatedIrp.SystemBuffer);
		break;
	case FileCompressionInformation:
		dprintf("* %s: FileCompressionInformation NOT IMPLEMENTED\n", __func__);
		break;
	case FileEaInformation:
		Status = file_ea_information(DeviceObject, Irp, IrpSp, 
			Irp->AssociatedIrp.SystemBuffer);
		break;
	case FileInternalInformation:
		Status = file_internal_information(DeviceObject, Irp, IrpSp, 
			Irp->AssociatedIrp.SystemBuffer);
		break;
	case FileNormalizedNameInformation:
		dprintf("FileNormalizedNameInformation\n");
		// IFSTEST AllInformationTest requires this name, and FileAllInformation
		// to be identical, so we no longer return the fullpath.
		normalize = 1; 
		/* According to fastfat, this means never return shortnames */
		/* fall through */
	case FileNameInformation:
		//
		// If overflow, set Information to input_size and NameLength to required size.
		//
		Status = file_name_information(DeviceObject, Irp, IrpSp, 
			Irp->AssociatedIrp.SystemBuffer, &usedspace, normalize);
		Irp->IoStatus.Information = FIELD_OFFSET(FILE_NAME_INFORMATION, FileName) + usedspace;
		break;
	case FileNetworkOpenInformation:   
		Status = file_network_open_information(DeviceObject, Irp, IrpSp, 
			Irp->AssociatedIrp.SystemBuffer);
		break;
	case FilePositionInformation:
		Status = file_position_information(DeviceObject, Irp, IrpSp, 
			Irp->AssociatedIrp.SystemBuffer);
		break;
	case FileStandardInformation:
		Status = file_standard_information(DeviceObject, Irp, IrpSp, 
			Irp->AssociatedIrp.SystemBuffer);
		break;
	case FileStreamInformation:
		Status = file_stream_information(DeviceObject, Irp, IrpSp, 
			Irp->AssociatedIrp.SystemBuffer, &usedspace);
		break;
	case FileHardLinkInformation:
		dprintf("* %s: FileHardLinkInformation NOT IMPLEMENTED\n", __func__);
		break;
	// Not used - not handled by ntfs either
	case FileRemoteProtocolInformation:
		dprintf("* %s: FileRemoteProtocolInformation NOT IMPLEMENTED\n", __func__);
#if 0
		Status = file_remote_protocol_information(DeviceObject, Irp, IrpSp, Irp->AssociatedIrp.SystemBuffer);
#endif
		Status = STATUS_INVALID_PARAMETER;
		break;
	case FileStandardLinkInformation:
		Status = file_standard_link_information(DeviceObject, Irp, IrpSp, 
			Irp->AssociatedIrp.SystemBuffer);
		break;
	case FileReparsePointInformation:
		break;
	case FileIdInformation:
		Status = file_id_information(DeviceObject, Irp, IrpSp, 
			Irp->AssociatedIrp.SystemBuffer);
		break;
	case FileCaseSensitiveInformation:
		Status = file_case_sensitive_information(DeviceObject, Irp, IrpSp, 
			Irp->AssociatedIrp.SystemBuffer);
		break;
	case FileStatInformation:
		// We call these functions from zfs_vnop_lookup, so size testing goes here
		if (IrpSp->Parameters.QueryFile.Length < sizeof(FILE_STAT_INFORMATION)) {
			Irp->IoStatus.Information = sizeof(FILE_STAT_INFORMATION);
			Status = STATUS_BUFFER_TOO_SMALL;
			break;
		}
		Status = file_stat_information(DeviceObject, Irp, IrpSp, 
			Irp->AssociatedIrp.SystemBuffer);
		Irp->IoStatus.Information = sizeof(FILE_STAT_INFORMATION);
		break;
	case FileStatLxInformation:
		// We call these functions from zfs_vnop_lookup, so size testing goes here
		if (IrpSp->Parameters.QueryFile.Length < sizeof(FILE_STAT_LX_INFORMATION)) {
			Irp->IoStatus.Information = sizeof(FILE_STAT_LX_INFORMATION);
			Status = STATUS_BUFFER_TOO_SMALL;
			break;
		}
		Status = file_stat_lx_information(DeviceObject, Irp, IrpSp,
			Irp->AssociatedIrp.SystemBuffer);
		Irp->IoStatus.Information = sizeof(FILE_STAT_LX_INFORMATION);
		break;
	default:
		dprintf("* %s: unknown class 0x%x NOT IMPLEMENTED\n", __func__, IrpSp->Parameters.QueryFile.FileInformationClass);
		break;
	}

	if (vp) {
		VN_RELE(vp);
		vp = NULL;
	}
	return Status;
}

PVOID
MapUserBuffer(
	IN OUT PIRP Irp
)
{
	//
	// If there is no Mdl, then we must be in the Fsd, and we can simply
	// return the UserBuffer field from the Irp.
	//
	if (Irp->MdlAddress == NULL) {
		return Irp->UserBuffer;
	} else {
		PVOID Address = MmGetSystemAddressForMdlSafe(Irp->MdlAddress, NormalPagePriority | MdlMappingNoExecute);
		return Address;
	}
}

PVOID
BufferUserBuffer(
	IN OUT PIRP Irp,
	IN ULONG BufferLength
)
{
	PUCHAR UserBuffer;
	if (BufferLength == 0) {
		return NULL;
	}
	//
	//  If there is no system buffer we must have been supplied an Mdl
	//  describing the users input buffer, which we will now snapshot.
	//
	if (Irp->AssociatedIrp.SystemBuffer == NULL) {
		UserBuffer = MapUserBuffer(Irp);
		Irp->AssociatedIrp.SystemBuffer = FsRtlAllocatePoolWithQuotaTag(NonPagedPoolNx,
			BufferLength,
			'qtaf');
		//
		// Set the flags so that the completion code knows to deallocate the
		// buffer.
		//
		Irp->Flags |= (IRP_BUFFERED_IO | IRP_DEALLOCATE_BUFFER);

		try {
			RtlCopyMemory(Irp->AssociatedIrp.SystemBuffer,
				UserBuffer,
				BufferLength);
		} except(EXCEPTION_EXECUTE_HANDLER) {
			NTSTATUS Status;
			Status = GetExceptionCode();
		}
	}
	return Irp->AssociatedIrp.SystemBuffer;
}

// Insert an EA into an output buffer, if there is room,
// EAName is always the FULL name length, even when we only
// fit partial.
// Return 0 for OK, 1 for overflow.
int zfswin_insert_xattrname(struct vnode *vp, char *xattrname, uint8_t *outbuffer, DWORD **lastNextEntryOffset,
	uint64_t availablebytes, uint64_t *spaceused)
{
	// The first xattr struct we assume is already aligned, but further ones
	// should be padded here.
	FILE_FULL_EA_INFORMATION *ea = NULL;
	int overflow = 0;

	// If not first struct, align outsize to 4 bytes - 0 aligns to 0.
	*spaceused = (((*spaceused) + 3) & ~3);

	// Convert filename, to get space required.
	ULONG needed_xattrnamelen;
	int error;

	// Check error? Do we care about convertion errors?
	//error = RtlUTF8ToUnicodeN(NULL, 0, &needed_xattrnamelen, xattrname, strlen(xattrname));
	needed_xattrnamelen = strlen(xattrname);

	// Is there room? We have to add the struct if there is room for it
	// and fill it out as much as possible, and copy in as much of the name
	// as we can.

	if (*spaceused + sizeof(FILE_FULL_EA_INFORMATION) <= availablebytes) {
		ea = (FILE_FULL_EA_INFORMATION *)&outbuffer[*spaceused];

		// Room for one more struct, update privious's next ptr
		if (*lastNextEntryOffset != NULL) {
			// Update previous structure to point to this one. 
			**lastNextEntryOffset = (DWORD)*spaceused;
		}


		// Directly set next to 0, assuming this will be last record
		ea->NextEntryOffset = 0;
		ea->Flags = 0; // Fix me?
		ea->EaValueLength = 0;

		// remember this struct's NextEntry, so the next one can fill it in.
		*lastNextEntryOffset = &ea->NextEntryOffset;

		// Return the total name length not counting null
		ea->EaNameLength = needed_xattrnamelen; 
		
		// Consume the space of the struct
		*spaceused += FIELD_OFFSET(FILE_FULL_EA_INFORMATION, EaName);

		uint64_t roomforname;
		if (*spaceused + ea->EaNameLength + 1 <= availablebytes) { // Null byte
			roomforname = ea->EaNameLength + 1;
		} else {
			roomforname = availablebytes - *spaceused;
			overflow = 1;
		}

		// Consume the space of (partial?) filename
		*spaceused += roomforname;

		// Now copy out as much of the filename as can fit.
		// We need to real full length in StreamNameLength
		// There is always room for 1 char
		strlcpy(ea->EaName, xattrname, roomforname);

		// If still room, copy out the xattr value
		uint64_t roomforvalue;
		if (*spaceused >= availablebytes) {
			overflow = 1;
		} else {
			roomforvalue = availablebytes - *spaceused;
			if (overflow == 0 && vp != NULL) {

				if (roomforvalue < VTOZ(vp)->z_size)
					overflow = 1;

				// Read in as much as we can
				uio_t *uio = uio_create(1, 0, UIO_SYSSPACE, UIO_READ);
				uio_addiov(uio, (user_addr_t)&outbuffer[*spaceused], roomforvalue);
				zfs_read(vp, uio, 0, NULL);
				// Consume as many bytes as we read
				*spaceused += roomforvalue - uio_resid(uio);
				// Set the valuelen, should this be the full value or what we would need?
				// That is how the names work.
				ea->EaValueLength = VTOZ(vp)->z_size; // double check!
				uio_free(uio);
			}
		}
		dprintf("%s: added %s xattrname '%s'\n", __func__,
			overflow ? "(partial)" : "", xattrname);
	} else {
		dprintf("%s: no room for  '%s'\n", __func__, xattrname);
		overflow = 1;
	}

	return overflow;
}

/*
 * Iterate through the XATTRs of an object, skipping streams. It works like readdir, with
 * saving index point, restart_scan and single_entry flags.
 * It can optionally supply QueryEa.EaList to query specific set of EAs.
 * Each output structure is 4 byte aligned
 */
NTSTATUS query_ea(PDEVICE_OBJECT DeviceObject, PIRP Irp, PIO_STACK_LOCATION IrpSp)
{
	NTSTATUS Status = STATUS_SUCCESS;

	PUCHAR  Buffer;
	ULONG   UserBufferLength;

	PUCHAR  UserEaList;
	ULONG   UserEaListLength;
	ULONG   UserEaIndex;
	BOOLEAN RestartScan;
	BOOLEAN ReturnSingleEntry;
	BOOLEAN IndexSpecified;
	DWORD *lastNextEntryOffset = NULL;
	uint64_t spaceused = 0;
	znode_t *zp = NULL;
	zfsvfs_t *zfsvfs = NULL;
	zap_cursor_t  zc;
	zap_attribute_t  za;
	int overflow = 0;

	struct vnode *vp = NULL, *xdvp = NULL;

	if (IrpSp->FileObject == NULL) return STATUS_INVALID_PARAMETER;
	vp = IrpSp->FileObject->FsContext;
	if (vp == NULL) return STATUS_INVALID_PARAMETER;

	zp = VTOZ(vp);
	zfsvfs = zp->z_zfsvfs;

	UserBufferLength = IrpSp->Parameters.QueryEa.Length;
	UserEaList = IrpSp->Parameters.QueryEa.EaList;
	UserEaListLength = IrpSp->Parameters.QueryEa.EaListLength;
	UserEaIndex = IrpSp->Parameters.QueryEa.EaIndex;
	RestartScan = BooleanFlagOn(IrpSp->Flags, SL_RESTART_SCAN);
	ReturnSingleEntry = BooleanFlagOn(IrpSp->Flags, SL_RETURN_SINGLE_ENTRY);
	IndexSpecified = BooleanFlagOn(IrpSp->Flags, SL_INDEX_SPECIFIED);

	dprintf("%s\n", __func__);

	// Grab the xattr dir - if any
	if (zfs_get_xattrdir(zp, &xdvp, NULL, 0) != 0) {
		return STATUS_NO_EAS_ON_FILE;
		Status = STATUS_NO_EAS_ON_FILE;
		goto out;
	}

	Buffer = MapUserBuffer(Irp);

	struct vnode *xvp = NULL;
	FILE_GET_EA_INFORMATION *ea;
	int error = 0;

	uint64_t start_index = 0;

	zfs_dirlist_t *zccb = IrpSp->FileObject->FsContext2;

	if (RestartScan)
		start_index = 0;
	else if (IndexSpecified)
		start_index = UserEaIndex;
	else
		start_index = zccb->ea_index;


	/* ********************** */
	if (UserEaList != NULL) {

		uint64_t offset = 0;

		do {
			ea = (FILE_GET_EA_INFORMATION *)&Buffer[offset];
			// Lookup ea if we can
			error = zfs_dirlook(VTOZ(xdvp), ea->EaName, &xvp, 0, NULL, NULL);
			if (error == 0) {
				overflow += zfswin_insert_xattrname(xvp, ea->EaName, Buffer, &lastNextEntryOffset, UserBufferLength, &spaceused);
				VN_RELE(xvp);
			} else {
				// No such xattr, we then "dummy" up an ea
				overflow += zfswin_insert_xattrname(NULL, ea->EaName, Buffer, &lastNextEntryOffset, UserBufferLength, &spaceused);
			}

			if (overflow != 0) break;

			zccb->ea_index++;

			offset = ea->NextEntryOffset;
			if (ReturnSingleEntry) break;

		} while (offset != 0);


		/* ********************** */
	} else {

		objset_t  *os;
		os = zfsvfs->z_os;

		if (start_index == 0)
			zap_cursor_init(&zc, os, VTOZ(xdvp)->z_id);
		else
			zap_cursor_init_serialized(&zc, os, zp->z_id, start_index);


		for ( /* empty */ ;
			zap_cursor_retrieve(&zc, &za) == 0; zap_cursor_advance(&zc)) {
			if (xattr_protected(za.za_name))
				continue;	 /* skip */
			if (xattr_stream(za.za_name))
				continue;	 /* skip */
			error = zfs_dirlook(VTOZ(xdvp), za.za_name, &xvp, 0, NULL, NULL);
			if (error == 0) {
				overflow += zfswin_insert_xattrname(xvp, za.za_name, Buffer, &lastNextEntryOffset, UserBufferLength, &spaceused);
				VN_RELE(xvp);
				if (overflow != 0) break;
				zccb->ea_index++;
			}
			if (ReturnSingleEntry) break;
		}
		zap_cursor_fini(&zc);
	}


out:

	if (xdvp) VN_RELE(xdvp);
	Irp->IoStatus.Information = spaceused;
	if (overflow)
		Status = STATUS_BUFFER_OVERFLOW;
	else if (spaceused == 0)
		Status = STATUS_NO_MORE_EAS;

	return Status;
}

/*
 * Receive an array of structs to set EAs, iterate until Next is null.
 */
NTSTATUS set_ea(PDEVICE_OBJECT DeviceObject, PIRP Irp, PIO_STACK_LOCATION IrpSp)
{
	uint32_t input_len = IrpSp->Parameters.SetEa.Length;
	uint8_t *buffer = NULL, *UserBuffer = NULL;
	NTSTATUS Status = STATUS_SUCCESS;
	struct vnode *vp = NULL, *xdvp = NULL;
	vattr_t vap = { 0 };

	if (IrpSp->FileObject == NULL) return STATUS_INVALID_PARAMETER;

	vp = IrpSp->FileObject->FsContext;
	if (vp == NULL) return STATUS_INVALID_PARAMETER;

	znode_t *zp = VTOZ(vp);

	dprintf("%s\n", __func__);

	if (input_len == 0) return STATUS_INVALID_PARAMETER;

	// This magic is straight out of fastfat
	buffer = BufferUserBuffer(Irp, input_len);

	ULONG eaErrorOffset = 0;
	Status = vnode_apply_eas(vp, (PFILE_FULL_EA_INFORMATION)buffer, input_len, &eaErrorOffset);
	// (Information is ULONG_PTR; as win64 is a LLP64 platform, ULONG isn't the right length.)
	Irp->IoStatus.Information = eaErrorOffset;
	if (!NT_SUCCESS(Status)) {
		dprintf("%s: failed vnode_apply_eas: 0x%x\n", __func__, Status);
		return Status;
	}

	return Status;
}

NTSTATUS get_reparse_point(PDEVICE_OBJECT DeviceObject, PIRP Irp, PIO_STACK_LOCATION IrpSp)
{
	NTSTATUS Status = STATUS_NOT_A_REPARSE_POINT;
	PFILE_OBJECT FileObject = IrpSp->FileObject;
	DWORD outlen = IrpSp->Parameters.FileSystemControl.OutputBufferLength;
	void *buffer = Irp->AssociatedIrp.SystemBuffer;
	struct vnode *vp;

	if (FileObject == NULL) return STATUS_INVALID_PARAMETER;

	vp = FileObject->FsContext;

	if (vp) {
		VN_HOLD(vp);
		znode_t *zp = VTOZ(vp);

		if (zp->z_pflags & ZFS_REPARSE) {
			int err;
			int size = MIN(zp->z_size, outlen);
			uio_t *uio;
			uio = uio_create(1, 0, UIO_SYSSPACE, UIO_READ);
			uio_addiov(uio, (user_addr_t)buffer, size);
			err = zfs_readlink(vp, uio, NULL);
			uio_free(uio);

			if (outlen < zp->z_size)
				Status = STATUS_BUFFER_OVERFLOW;
			else
				Status = STATUS_SUCCESS;

			Irp->IoStatus.Information = size;

			REPARSE_DATA_BUFFER *rdb = buffer;
			dprintf("Returning tag 0x%x\n", rdb->ReparseTag);
		}
		VN_RELE(vp);
	}
	dprintf("%s: returning 0x%x\n", __func__, Status);
	return Status;
}

NTSTATUS set_reparse_point(PDEVICE_OBJECT DeviceObject, PIRP Irp, PIO_STACK_LOCATION IrpSp)
{
	NTSTATUS Status = STATUS_NOT_IMPLEMENTED;
	PFILE_OBJECT FileObject = IrpSp->FileObject;
	DWORD inlen = IrpSp->Parameters.DeviceIoControl.InputBufferLength;
	void *buffer = Irp->AssociatedIrp.SystemBuffer;
	REPARSE_DATA_BUFFER *rdb = buffer;
	ULONG tag;

	if (!FileObject) 
		return STATUS_INVALID_PARAMETER;

	if (Irp->UserBuffer)
		return STATUS_INVALID_PARAMETER;

	if (inlen < sizeof(ULONG)) {
		return STATUS_INVALID_BUFFER_SIZE;
	}

	Status = FsRtlValidateReparsePointBuffer(inlen, rdb);
	if (!NT_SUCCESS(Status)) {
		dprintf("FsRtlValidateReparsePointBuffer returned %08x\n", Status);
		goto out;
	}

	RtlCopyMemory(&tag, buffer, sizeof(ULONG));
	dprintf("Received tag 0x%x\n", tag);

	struct vnode *vp = IrpSp->FileObject->FsContext;
	VN_HOLD(vp);
	znode_t *zp = VTOZ(vp);

	// Like zfs_symlink, write the data as SA attribute.
	zfsvfs_t *zfsvfs = zp->z_zfsvfs;
	int err;
	dmu_tx_t	*tx;

	// Set flags to indicate we are reparse point
	zp->z_pflags |= ZFS_REPARSE;

	// Start TX and save FLAGS, SIZE and SYMLINK to disk.
top:		
	tx = dmu_tx_create(zfsvfs->z_os);
	dmu_tx_hold_sa(tx, zp->z_sa_hdl, B_FALSE);
	err = dmu_tx_assign(tx, TXG_WAIT);
	if (err) {
		dmu_tx_abort(tx);
		if (err == ERESTART)
			goto top;
		goto out;
	}

	(void)sa_update(zp->z_sa_hdl, SA_ZPL_FLAGS(zfsvfs),
		&zp->z_pflags, sizeof(zp->z_pflags), tx);

	mutex_enter(&zp->z_lock);
	if (zp->z_is_sa)
		err = sa_update(zp->z_sa_hdl, SA_ZPL_SYMLINK(zfsvfs),
			buffer, inlen, tx);
	else
		zfs_sa_symlink(zp, buffer, inlen, tx);
	mutex_exit(&zp->z_lock);

	zp->z_size = inlen;
	(void)sa_update(zp->z_sa_hdl, SA_ZPL_SIZE(zfsvfs),
		&zp->z_size, sizeof(zp->z_size), tx);

	dmu_tx_commit(tx);

	if (zfsvfs->z_os->os_sync == ZFS_SYNC_ALWAYS)
		zil_commit(zfsvfs->z_log, 0);

out:
	VN_RELE(vp);

	dprintf("%s: returning 0x%x\n", __func__, Status);

	return Status;
}

NTSTATUS create_or_get_object_id(PDEVICE_OBJECT DeviceObject, PIRP Irp, PIO_STACK_LOCATION IrpSp)
{
	NTSTATUS Status = STATUS_NOT_IMPLEMENTED;
	PFILE_OBJECT FileObject = IrpSp->FileObject;
	DWORD inlen = IrpSp->Parameters.DeviceIoControl.OutputBufferLength;
	void *buffer = Irp->AssociatedIrp.SystemBuffer;
	FILE_OBJECTID_BUFFER *fob = buffer;

	if (!FileObject)
		return STATUS_INVALID_PARAMETER;

	if (!fob || inlen < sizeof(FILE_OBJECTID_BUFFER)) {
		Irp->IoStatus.Information = sizeof(FILE_OBJECTID_BUFFER);
		return STATUS_BUFFER_OVERFLOW;
	}

	struct vnode *vp = IrpSp->FileObject->FsContext;
	VN_HOLD(vp);
	znode_t *zp = VTOZ(vp);
	zfsvfs_t *zfsvfs = zp->z_zfsvfs;

	// ObjectID is 16 bytes to identify the file
	// Should we do endian work here?
	// znode id + pool guid
	RtlCopyMemory(&fob->ObjectId[0], &zp->z_id, sizeof(UINT64));
	uint64_t guid = dmu_objset_fsid_guid(zfsvfs->z_os);
	RtlCopyMemory(&fob->ObjectId[sizeof(UINT64)], &guid, sizeof(UINT64));

	VN_RELE(vp);

	Irp->IoStatus.Information = sizeof(FILE_OBJECTID_BUFFER);
	Status = STATUS_SUCCESS;
	return Status;
}

NTSTATUS user_fs_request(PDEVICE_OBJECT DeviceObject, PIRP Irp, PIO_STACK_LOCATION IrpSp)
{
	NTSTATUS Status = STATUS_NOT_IMPLEMENTED;

	switch (IrpSp->Parameters.FileSystemControl.FsControlCode) {
	case FSCTL_LOCK_VOLUME:
		dprintf("    FSCTL_LOCK_VOLUME\n");
		Status = STATUS_SUCCESS;
		break;
	case FSCTL_UNLOCK_VOLUME:
		dprintf("    FSCTL_UNLOCK_VOLUME\n");
		Status = STATUS_SUCCESS;
		break;
	case FSCTL_DISMOUNT_VOLUME:
		dprintf("    FSCTL_DISMOUNT_VOLUME\n");
		break;
	case FSCTL_MARK_VOLUME_DIRTY:
		dprintf("    FSCTL_MARK_VOLUME_DIRTY\n");
		Status = STATUS_SUCCESS;
		break;
	case FSCTL_IS_VOLUME_MOUNTED:
		dprintf("    FSCTL_IS_VOLUME_MOUNTED\n");
		Status = STATUS_SUCCESS;
		break;
	case FSCTL_SET_COMPRESSION:
		dprintf("    FSCTL_SET_COMPRESSION\n");
		Status = STATUS_SUCCESS;
		break;
	case FSCTL_IS_PATHNAME_VALID:
		dprintf("    FSCTL_IS_PATHNAME_VALID\n");
		Status = STATUS_SUCCESS;
		break;
	case FSCTL_GET_RETRIEVAL_POINTERS:
		dprintf("    FSCTL_GET_RETRIEVAL_POINTERS\n");
		Status = STATUS_INVALID_PARAMETER;
		break;
	case FSCTL_IS_VOLUME_DIRTY:
		dprintf("    FSCTL_IS_VOLUME_DIRTY\n");
		PULONG VolumeState;

		VolumeState = MapUserBuffer(Irp);

		if (VolumeState == NULL) {
			Status = STATUS_INSUFFICIENT_RESOURCES;
			break;
		}

		if (IrpSp->Parameters.FileSystemControl.OutputBufferLength < sizeof(ULONG)) {
			Status = STATUS_INVALID_PARAMETER;
			break;
		}

		*VolumeState = 0;
		if (0)
			SetFlag(*VolumeState, VOLUME_IS_DIRTY);
		Irp->IoStatus.Information = sizeof(ULONG);
		Status = STATUS_SUCCESS;
		break;
	case FSCTL_GET_REPARSE_POINT:
		dprintf("    FSCTL_GET_REPARSE_POINT\n");
		Status = get_reparse_point(DeviceObject, Irp, IrpSp);
		break;
	case FSCTL_SET_REPARSE_POINT:
		dprintf("    FSCTL_SET_REPARSE_POINT\n");
		Status = set_reparse_point(DeviceObject, Irp, IrpSp);
		break;
	case FSCTL_CREATE_OR_GET_OBJECT_ID:
		dprintf("    FSCTL_CREATE_OR_GET_OBJECT_ID\n");
		Status = create_or_get_object_id(DeviceObject, Irp, IrpSp);
		break;
	case FSCTL_REQUEST_OPLOCK:
		dprintf("    FSCTL_REQUEST_OPLOCK: \n" );
#if 0 //not yet, store oplock in znode, init on open etc.
		PREQUEST_OPLOCK_INPUT_BUFFER *req = Irp->AssociatedIrp.SystemBuffer;
		int InputBufferLength = IrpSp->Parameters.FileSystemControl.InputBufferLength;
		int OutputBufferLength = IrpSp->Parameters.FileSystemControl.OutputBufferLength;

		if ((InputBufferLength < sizeof(REQUEST_OPLOCK_INPUT_BUFFER)) || 
			(OutputBufferLength < sizeof(REQUEST_OPLOCK_OUTPUT_BUFFER))) {
			return STATUS_BUFFER_TOO_SMALL;
		}
		OPLOCK oplock;
		FsRtlInitializeOplock(&oplock);
		Status = FsRtlOplockFsctrl(&oplock, Irp, 0);
#endif
		break;
	case FSCTL_FILESYSTEM_GET_STATISTICS:
		dprintf("    FSCTL_FILESYSTEM_GET_STATISTICS: \n");
		FILESYSTEM_STATISTICS *fss = Irp->AssociatedIrp.SystemBuffer;

		// btrfs: This is hideously wrong, but at least it stops SMB from breaking

		if (IrpSp->Parameters.FileSystemControl.OutputBufferLength < sizeof(FILESYSTEM_STATISTICS))
			return STATUS_BUFFER_TOO_SMALL;

		memset(fss, 0, sizeof(FILESYSTEM_STATISTICS));

		fss->Version = 1;
		fss->FileSystemType = FILESYSTEM_STATISTICS_TYPE_NTFS;
		fss->SizeOfCompleteStructure = sizeof(FILESYSTEM_STATISTICS);

		Irp->IoStatus.Information = sizeof(FILESYSTEM_STATISTICS);
		Status = STATUS_SUCCESS;
		break;
	case FSCTL_QUERY_DEPENDENT_VOLUME:
		dprintf("    FSCTL_QUERY_DEPENDENT_VOLUME: \n");
		STORAGE_QUERY_DEPENDENT_VOLUME_REQUEST *req = Irp->AssociatedIrp.SystemBuffer;
		dprintf("RequestLevel %d: RequestFlags 0x%x\n", req->RequestLevel, req->RequestFlags);
//#define QUERY_DEPENDENT_VOLUME_REQUEST_FLAG_HOST_VOLUMES    0x1
//#define QUERY_DEPENDENT_VOLUME_REQUEST_FLAG_GUEST_VOLUMES   0x2
		STORAGE_QUERY_DEPENDENT_VOLUME_LEV1_ENTRY *lvl1 = Irp->AssociatedIrp.SystemBuffer;
		STORAGE_QUERY_DEPENDENT_VOLUME_LEV2_ENTRY *lvl2 = Irp->AssociatedIrp.SystemBuffer;

		switch (req->RequestLevel) {
		case 1:
			if (IrpSp->Parameters.FileSystemControl.OutputBufferLength < sizeof(STORAGE_QUERY_DEPENDENT_VOLUME_LEV1_ENTRY))
				return STATUS_BUFFER_TOO_SMALL;
			memset(lvl1, 0, sizeof(STORAGE_QUERY_DEPENDENT_VOLUME_LEV1_ENTRY));
			lvl1->EntryLength = sizeof(STORAGE_QUERY_DEPENDENT_VOLUME_LEV1_ENTRY);
			Irp->IoStatus.Information = sizeof(STORAGE_QUERY_DEPENDENT_VOLUME_LEV1_ENTRY);
			Status = STATUS_SUCCESS;
			break;
		case 2:
			if (IrpSp->Parameters.FileSystemControl.OutputBufferLength < sizeof(STORAGE_QUERY_DEPENDENT_VOLUME_LEV2_ENTRY))
				return STATUS_BUFFER_TOO_SMALL;
			memset(lvl2, 0, sizeof(STORAGE_QUERY_DEPENDENT_VOLUME_LEV2_ENTRY));
			lvl2->EntryLength = sizeof(STORAGE_QUERY_DEPENDENT_VOLUME_LEV2_ENTRY);
			Irp->IoStatus.Information = sizeof(STORAGE_QUERY_DEPENDENT_VOLUME_LEV2_ENTRY);
			Status = STATUS_SUCCESS;
			break;
		default:
			Status = STATUS_INVALID_PARAMETER;
			break;
		}
		break;

	default:
		dprintf("* %s: unknown class 0x%x\n", __func__, IrpSp->Parameters.FileSystemControl.FsControlCode);
		break;
	}

	return Status;
}

NTSTATUS query_directory_FileFullDirectoryInformation(PDEVICE_OBJECT DeviceObject, PIRP Irp, PIO_STACK_LOCATION IrpSp)
{
	//FILE_FULL_DIR_INFORMATION *outptr = Irp->UserBuffer;
	int flag_index_specified     = IrpSp->Flags & SL_INDEX_SPECIFIED     ? 1 : 0;
	int flag_restart_scan        = IrpSp->Flags & SL_RESTART_SCAN        ? 1 : 0;
	int flag_return_single_entry = IrpSp->Flags & SL_RETURN_SINGLE_ENTRY ? 1 : 0;
	int bytes_out = 0;
	int index = 0;
	uio_t *uio;
	int eof = 0;
	int numdirent;
	int ret;
	mount_t *zmo;
	zfsvfs_t *zfsvfs;
	NTSTATUS Status = STATUS_NO_SUCH_FILE;

	if ((Irp->UserBuffer == NULL && Irp->MdlAddress == NULL) ||
		IrpSp->Parameters.QueryDirectory.Length <= 0)
		return STATUS_INSUFFICIENT_RESOURCES;

	if (IrpSp->FileObject == NULL || 
		IrpSp->FileObject->FsContext == NULL ||  // vnode
		IrpSp->FileObject->FsContext2 == NULL)   // ccb
		return STATUS_INVALID_PARAMETER;

	struct vnode *dvp = IrpSp->FileObject->FsContext;
	zfs_dirlist_t *zccb = IrpSp->FileObject->FsContext2;

	if (zccb->magic != ZFS_DIRLIST_MAGIC)
		return STATUS_INVALID_PARAMETER;

	// Restarting listing? Clear EOF
	if (flag_restart_scan) {
		zccb->dir_eof = 0;
		zccb->uio_offset = 0;
		if (zccb->searchname.Buffer != NULL)
			kmem_free(zccb->searchname.Buffer, zccb->searchname.MaximumLength);
		zccb->searchname.Buffer = NULL;
		zccb->searchname.MaximumLength = 0;
	}

	// Did last call complete listing?
	if (zccb->dir_eof)
		return STATUS_NO_MORE_FILES;

	uio = uio_create(1, zccb->uio_offset, UIO_SYSSPACE, UIO_READ);	

	void *SystemBuffer = MapUserBuffer(Irp);
	uio_addiov(uio, (user_addr_t)SystemBuffer, 
		IrpSp->Parameters.QueryDirectory.Length);

	// Grab the root zp
	zmo = DeviceObject->DeviceExtension;
	ASSERT(zmo->type == MOUNT_TYPE_VCB);

	zfsvfs = vfs_fsprivate(zmo); // or from zp

	if (!zfsvfs) return STATUS_INTERNAL_ERROR;

	dprintf("%s: starting vp %p Search pattern '%wZ' type %d: saved search '%wZ'\n", __func__, dvp,
		IrpSp->Parameters.QueryDirectory.FileName,
		IrpSp->Parameters.QueryDirectory.FileInformationClass,
		&zccb->searchname);

	if (IrpSp->Parameters.QueryDirectory.FileName &&
		IrpSp->Parameters.QueryDirectory.FileName->Buffer &&
		IrpSp->Parameters.QueryDirectory.FileName->Length != 0 &&
		wcsncmp(IrpSp->Parameters.QueryDirectory.FileName->Buffer, L"*", 1) != 0) {
		// Save the pattern in the zccb, as it is only given in the first call (citation needed)

		// If exists, we should free?
		if (zccb->searchname.Buffer != NULL)
			kmem_free(zccb->searchname.Buffer, zccb->searchname.MaximumLength);

		zccb->ContainsWildCards =
			FsRtlDoesNameContainWildCards(IrpSp->Parameters.QueryDirectory.FileName);
		zccb->searchname.MaximumLength = IrpSp->Parameters.QueryDirectory.FileName->Length + 2; // Make room for terminator, if needed
		zccb->searchname.Length = IrpSp->Parameters.QueryDirectory.FileName->Length;
		zccb->searchname.Buffer = kmem_alloc(zccb->searchname.MaximumLength, KM_SLEEP);
		if (zccb->ContainsWildCards) {
			Status = RtlUpcaseUnicodeString(&zccb->searchname, IrpSp->Parameters.QueryDirectory.FileName, FALSE);
		} else {
			RtlCopyMemory(zccb->searchname.Buffer, IrpSp->Parameters.QueryDirectory.FileName->Buffer, zccb->searchname.Length);
		}
		dprintf("%s: setting up search '%wZ' (wildcards: %d) status 0x%x\n", __func__, 
			&zccb->searchname, zccb->ContainsWildCards, Status);
	}

	VN_HOLD(dvp);
	ret = zfs_readdir(dvp, uio, NULL, zccb, IrpSp->Flags, IrpSp->Parameters.QueryDirectory.FileInformationClass, &numdirent);
	VN_RELE(dvp);

	if (ret == 0) {

		// Set correct buffer size returned.
		Irp->IoStatus.Information = IrpSp->Parameters.QueryDirectory.Length - uio_resid(uio);

		dprintf("dirlist information in %d out size %d\n", 
			IrpSp->Parameters.QueryDirectory.Length, Irp->IoStatus.Information);

		// Return saying there are entries in buffer, or, ]
		// if we sent same data previously, but now EOF send NO MORE,
		// or if there was nothing sent at all (search pattern failed), send NO SUCH
		if (Irp->IoStatus.Information == 0)
			Status = (zccb->uio_offset == 0) ? STATUS_NO_SUCH_FILE : STATUS_NO_MORE_FILES;
		else
			Status = STATUS_SUCCESS;

		// Remember directory index for next time
		zccb->uio_offset = uio_offset(uio);

	}

	// Release uio
	uio_free(uio);

	return Status;
}


NTSTATUS query_directory(PDEVICE_OBJECT DeviceObject, PIRP Irp, PIO_STACK_LOCATION IrpSp)
{
	NTSTATUS Status = STATUS_NOT_IMPLEMENTED;

	switch (IrpSp->Parameters.QueryDirectory.FileInformationClass) {

		// The type is now passed into zfs_vnop.c/zfs_readdir() so check there for support
	case FileBothDirectoryInformation:
	case FileDirectoryInformation:
	case FileFullDirectoryInformation: // ***
	case FileIdBothDirectoryInformation: // ***
	case FileIdFullDirectoryInformation:
	case FileNamesInformation:
	case FileObjectIdInformation:
		Status = query_directory_FileFullDirectoryInformation(DeviceObject, Irp, IrpSp);
		break;
	case FileQuotaInformation:
		dprintf("   %s FileQuotaInformation *NotImplemented\n", __func__);
		break;
	case FileReparsePointInformation:
		dprintf("   %s FileReparsePointInformation *NotImplemented\n", __func__);
		break;
	default:
		dprintf("   %s unknown 0x%x *NotImplemented\n", __func__, IrpSp->Parameters.QueryDirectory.FileInformationClass);
		break;
	}

	return Status;
}

NTSTATUS notify_change_directory(PDEVICE_OBJECT DeviceObject, PIRP Irp, PIO_STACK_LOCATION IrpSp)
{
	PFILE_OBJECT fileObject = IrpSp->FileObject;
	mount_t *zmo;

	dprintf("%s\n", __func__);
	zmo = DeviceObject->DeviceExtension;
	ASSERT(zmo != NULL);
	if (zmo->type != MOUNT_TYPE_VCB) {
		return STATUS_INVALID_PARAMETER;
	}

	struct vnode *vp = fileObject->FsContext;
	zfs_dirlist_t *zccb = fileObject->FsContext2;
	ASSERT(vp != NULL);

	VN_HOLD(vp);
	znode_t *zp = VTOZ(vp);

	if (!vnode_isdir(vp)) {
		VN_RELE(vp);
		return STATUS_INVALID_PARAMETER;
	}

	if (zccb && zccb->deleteonclose) {
		VN_RELE(vp);
		return STATUS_DELETE_PENDING;
	}
	ASSERT(zmo->NotifySync != NULL);

	dprintf("%s: '%s' for %wZ\n", __func__, zp&&zp->z_name_cache?zp->z_name_cache:"", &fileObject->FileName);
	FsRtlNotifyFullChangeDirectory(
		zmo->NotifySync, &zmo->DirNotifyList, zp, (PSTRING)&fileObject->FileName,
		(IrpSp->Flags & SL_WATCH_TREE) ? TRUE : FALSE, FALSE,
		IrpSp->Parameters.NotifyDirectory.CompletionFilter, Irp, NULL, NULL);

	VN_RELE(vp);
	return STATUS_PENDING;
}

NTSTATUS set_information(PDEVICE_OBJECT DeviceObject, PIRP Irp, PIO_STACK_LOCATION IrpSp)
{
	NTSTATUS Status = STATUS_NOT_IMPLEMENTED;

	Irp->IoStatus.Information = 0;

	switch (IrpSp->Parameters.SetFile.FileInformationClass) {
	case FileAllocationInformation: 
		if (IrpSp->FileObject && IrpSp->FileObject->FsContext) {
			FILE_ALLOCATION_INFORMATION *feofi = Irp->AssociatedIrp.SystemBuffer;
			dprintf("* SET FileAllocationInformation %u\n", feofi->AllocationSize.QuadPart);
			// This is a noop at the moment. It makes Windows Explorer and apps not crash
			// From the documentation, setting the allocation size smaller than EOF should shrink it: 
			// https://msdn.microsoft.com/en-us/library/windows/desktop/aa364214(v=vs.85).aspx
			// However, NTFS doesn't do that! It keeps the size the same.
			// Setting a FileAllocationInformation larger than current EOF size does not have a observable affect from user space.
			Status = STATUS_SUCCESS;
		}
		break;
	case FileBasicInformation: // chmod
		dprintf("* SET FileBasicInformation\n");
		if (IrpSp->FileObject && IrpSp->FileObject->FsContext) {
			FILE_BASIC_INFORMATION *fbi = Irp->AssociatedIrp.SystemBuffer;
			struct vnode *vp = IrpSp->FileObject->FsContext;

			VN_HOLD(vp);
			znode_t *zp = VTOZ(vp);
			vattr_t va = { 0 };
			uint64_t unixtime[2] = { 0 };

			// can request that the file system not update .. LastAccessTime, LastWriteTime, and ChangeTime ..  setting the appropriate members to -1.
			// ie, LastAccessTime = -1 -> atime = disabled - not implemented
			// LastAccessTime = -2 -> cancel the disable (-1), return to normal.
			// a value of "0" means to keep existing value.
			if (fbi->ChangeTime.QuadPart > 0) {
				TIME_WINDOWS_TO_UNIX(fbi->ChangeTime.QuadPart, unixtime);
				va.va_change_time.tv_sec = unixtime[0]; va.va_change_time.tv_nsec = unixtime[1];
				va.va_active |= ATTR_CTIME;
			}
			if (fbi->LastWriteTime.QuadPart > 0) {
				TIME_WINDOWS_TO_UNIX(fbi->LastWriteTime.QuadPart, unixtime);
				va.va_modify_time.tv_sec = unixtime[0]; va.va_modify_time.tv_nsec = unixtime[1];
				va.va_active |= ATTR_MTIME;
			}
			if (fbi->CreationTime.QuadPart > 0) {
				TIME_WINDOWS_TO_UNIX(fbi->CreationTime.QuadPart, unixtime);
				va.va_create_time.tv_sec = unixtime[0]; va.va_create_time.tv_nsec = unixtime[1];
				va.va_active |= ATTR_CRTIME;  // ATTR_CRTIME
			}
			if (fbi->LastAccessTime.QuadPart > 0) 
				TIME_WINDOWS_TO_UNIX(fbi->LastAccessTime.QuadPart, zp->z_atime);

			if (fbi->FileAttributes)
				if (zfs_setwinflags(VTOZ(vp), fbi->FileAttributes))
					va.va_active |= ATTR_MODE;

			Status = zfs_setattr(vp, &va, 0, NULL);

			// zfs_setattr will turn ARCHIVE back on, when perhaps it is set off by this call
			if (fbi->FileAttributes)
				zfs_setwinflags(VTOZ(vp), fbi->FileAttributes);

			VN_RELE(vp);
		}
		break;
	case FileDispositionInformation: // unlink
		dprintf("* SET FileDispositionInformation\n");
		Status = file_disposition_information(DeviceObject, Irp, IrpSp);
		break;
	case FileEndOfFileInformation: // extend?
		Status = file_endoffile_information(DeviceObject, Irp, IrpSp);
		break;
	case FileLinkInformation: // symlink
		Status = file_link_information(DeviceObject, Irp, IrpSp);
		break;
	case FilePositionInformation: // seek
		dprintf("* SET FilePositionInformation NOTIMPLEMENTED\n");
		break;
	case FileRenameInformation: // vnop_rename
		Status = file_rename_information(DeviceObject, Irp, IrpSp);
		break;
	case FileValidDataLengthInformation:  // truncate?
		dprintf("* SET FileValidDataLengthInformation NOTIMPLEMENTED\n");
		break;
	case FileDispositionInformationEx:
		Status = file_disposition_information_ex(DeviceObject, Irp, IrpSp);
		break;
	//case FileRenameInformationEx:
	//	break;
	default:
		dprintf("* %s: unknown type NOTIMPLEMENTED\n", __func__);
		break;
	}

	return Status;
}


NTSTATUS fs_read(PDEVICE_OBJECT DeviceObject, PIRP Irp, PIO_STACK_LOCATION IrpSp)
{
	PFILE_OBJECT	fileObject;
	ULONG			bufferLength;
	LARGE_INTEGER	byteOffset;
	NTSTATUS Status = STATUS_SUCCESS;
	int error; 
	int nocache = Irp->Flags & IRP_NOCACHE;
	int pagingio = FlagOn(Irp->Flags, IRP_PAGING_IO);
	int releaselock = 0;

	PAGED_CODE();

	if (FlagOn(IrpSp->MinorFunction, IRP_MN_COMPLETE)) {
		dprintf("%s: IRP_MN_COMPLETE\n", __func__);
		CcMdlReadComplete(IrpSp->FileObject, Irp->MdlAddress);
		// Mdl is now deallocated.
		Irp->MdlAddress = NULL;
		return STATUS_SUCCESS;
	}

#if 0
	dprintf("   %s minor type %d flags 0x%x mdl %d System %d User %d paging %d\n", __func__, IrpSp->MinorFunction, 
		DeviceObject->Flags, (Irp->MdlAddress != 0), (Irp->AssociatedIrp.SystemBuffer != 0), 
		(Irp->UserBuffer != 0),
		FlagOn(Irp->Flags, IRP_PAGING_IO));
#endif
	if (zfs_disable_wincache)
		nocache = 1;

	bufferLength = IrpSp->Parameters.Read.Length;
	if (bufferLength == 0)
		return STATUS_SUCCESS;

	fileObject = IrpSp->FileObject;

	// File may have been closed, but CC mgr setting section will ask to read
	if (fileObject == NULL || fileObject->FsContext == NULL) {
		dprintf("  fileObject == NULL\n");
		//ASSERT0("fileobject == NULL");
		return STATUS_INVALID_PARAMETER;
	}

	struct vnode *vp = fileObject->FsContext;
	VN_HOLD(vp);
	znode_t *zp = VTOZ(vp);

	if (IrpSp->Parameters.Read.ByteOffset.LowPart == FILE_USE_FILE_POINTER_POSITION &&
		IrpSp->Parameters.Read.ByteOffset.HighPart == -1) {
		byteOffset = fileObject->CurrentByteOffset;
	} else {
		byteOffset = IrpSp->Parameters.Read.ByteOffset;
	}

	uint64_t filesize = zp->z_size;

	// If the read starts beyond the End of File, return EOF
	// as per fastfat.
	if (byteOffset.QuadPart >= filesize) {
		Status = STATUS_END_OF_FILE;
		goto out;
	}

	// Read is beyond file length? shorten
	if (byteOffset.QuadPart + bufferLength > filesize)
		bufferLength = filesize - byteOffset.QuadPart;

	// nocache transfer, make sure we flush first.
	if (!pagingio && nocache && fileObject->SectionObjectPointer &&
		(fileObject->SectionObjectPointer->DataSectionObject != NULL)) {
		IO_STATUS_BLOCK IoStatus = { 0 };
		ExAcquireResourceExclusiveLite(vp->FileHeader.PagingIoResource, TRUE);
		CcFlushCache(fileObject->SectionObjectPointer,
			&byteOffset,
			bufferLength,
			&IoStatus);
		ExReleaseResourceLite(vp->FileHeader.PagingIoResource);
		VERIFY0(IoStatus.Status);
	}
	// Grab lock if paging
	if (pagingio) {
		ExAcquireResourceSharedLite(vp->FileHeader.PagingIoResource, TRUE);
		releaselock = 1;
	} 

	void *SystemBuffer = MapUserBuffer(Irp);

	if (nocache) {

	} else {
		// Cached
#if 1
		if (fileObject->PrivateCacheMap == NULL) {
			CC_FILE_SIZES ccfs;
			vp->FileHeader.FileSize.QuadPart = zp->z_size;
			vp->FileHeader.ValidDataLength.QuadPart = zp->z_size;
			ccfs.AllocationSize = vp->FileHeader.AllocationSize;
			ccfs.FileSize = vp->FileHeader.FileSize;
			ccfs.ValidDataLength = vp->FileHeader.ValidDataLength;
			CcInitializeCacheMap(fileObject, &ccfs, FALSE,
				&CacheManagerCallbacks, vp);
			CcSetAdditionalCacheAttributes(fileObject, TRUE, TRUE); // FIXME: for now
			dprintf("%s: CcInitializeCacheMap\n", __func__);
		}
#endif

		// DO A NORMAL CACHED READ, if the MDL bit is not set,
		if (!FlagOn(IrpSp->MinorFunction, IRP_MN_MDL)) {

			vnode_pager_setsize(vp, zp->z_size);
			CcSetFileSizes(IrpSp->FileObject, (PCC_FILE_SIZES)&vp->FileHeader.AllocationSize);
			vnode_setsizechange(vp, 0);

#if (NTDDI_VERSION >= NTDDI_WIN8)
			if (!CcCopyReadEx(fileObject,
				&byteOffset,
				bufferLength,
				TRUE,
				SystemBuffer,
				&Irp->IoStatus,
				Irp->Tail.Overlay.Thread)) {
#else
			if (!CcCopyRead(fileObject,
				&byteOffset,
				bufferLength,
				TRUE,
				SystemBuffer,
				&Irp->IoStatus)) {
#endif
				dprintf("CcCopyReadEx error\n");
			}

			Irp->IoStatus.Information = bufferLength;
			Status = Irp->IoStatus.Status;
			goto out;

		} else {

			// MDL read
			CcMdlRead(fileObject,
				&byteOffset,
				bufferLength,
				&Irp->MdlAddress,
				&Irp->IoStatus);
			Status = Irp->IoStatus.Status;
			goto out;
		} // mdl

	} // !nocache


	uio_t *uio;

	uio = uio_create(1, byteOffset.QuadPart, UIO_SYSSPACE, UIO_READ);

	ASSERT(SystemBuffer != NULL);
	uio_addiov(uio, (user_addr_t)SystemBuffer, bufferLength);

	dprintf("%s: offset %llx size %lx\n", __func__, byteOffset.QuadPart, bufferLength);

	error = zfs_read(vp, uio, 0, NULL);

	// Update bytes read
	Irp->IoStatus.Information = bufferLength - uio_resid(uio);

	// apparently we dont use EOF
//	if (Irp->IoStatus.Information == 0)
//		Status = STATUS_END_OF_FILE;

	uio_free(uio);

out:

	VN_RELE(vp);
	// Update the file offset
	if ((Status == STATUS_SUCCESS) &&
		(fileObject->Flags & FO_SYNCHRONOUS_IO) &&
		!(Irp->Flags & IRP_PAGING_IO)) {
		// update current byte offset only when synchronous IO and not pagind IO
		fileObject->CurrentByteOffset.QuadPart =
			byteOffset.QuadPart + Irp->IoStatus.Information;
	}

	if (releaselock) ExReleaseResourceLite(vp->FileHeader.PagingIoResource);

//	dprintf("  FileName: %wZ offset 0x%llx len 0x%lx mdl %p System %p\n", &fileObject->FileName,
	//	byteOffset.QuadPart, bufferLength, Irp->MdlAddress, Irp->AssociatedIrp.SystemBuffer);

	return Status;
}


NTSTATUS fs_write(PDEVICE_OBJECT DeviceObject, PIRP Irp, PIO_STACK_LOCATION IrpSp)
{
	PFILE_OBJECT	fileObject;
	ULONG			bufferLength;
	LARGE_INTEGER	byteOffset;
	NTSTATUS Status = STATUS_SUCCESS;
	int error;
	int nocache = Irp->Flags & IRP_NOCACHE;
	int pagingio = FlagOn(Irp->Flags, IRP_PAGING_IO);

	if (zfs_disable_wincache)
		nocache = 1;

	PAGED_CODE();

	if (FlagOn(IrpSp->MinorFunction, IRP_MN_COMPLETE)) {
		dprintf("%s: IRP_MN_COMPLETE\n", __func__);
		CcMdlWriteComplete(IrpSp->FileObject, &IrpSp->Parameters.Write.ByteOffset, Irp->MdlAddress);
		// Mdl is now deallocated.
		Irp->MdlAddress = NULL;
		return STATUS_SUCCESS;
	}
//	dprintf("   %s paging %d\n", __func__, FlagOn(Irp->Flags, IRP_PAGING_IO));
#if 0
	xprintf("   %s minor type %d flags 0x%x mdl %d System %d User %d paging %d\n", __func__, IrpSp->MinorFunction,
		DeviceObject->Flags, (Irp->MdlAddress != 0), (Irp->AssociatedIrp.SystemBuffer != 0),
		(Irp->UserBuffer != 0),
		FlagOn(Irp->Flags, IRP_PAGING_IO));
#endif

	bufferLength = IrpSp->Parameters.Write.Length;
	if (bufferLength == 0)
		return STATUS_SUCCESS;

	fileObject = IrpSp->FileObject;

	if (fileObject == NULL || fileObject->FsContext == NULL) {
		dprintf("  fileObject == NULL\n");
		ASSERT0("fileObject == NULL");
		return STATUS_INVALID_PARAMETER;
	}

	struct vnode *vp = fileObject->FsContext;
	VN_HOLD(vp);
	znode_t *zp = VTOZ(vp);
	ASSERT(ZTOV(zp) == vp);
	Irp->IoStatus.Information = 0;

	// Special encoding
	byteOffset = IrpSp->Parameters.Write.ByteOffset;
	if (IrpSp->Parameters.Write.ByteOffset.HighPart == -1) {
		if (IrpSp->Parameters.Write.ByteOffset.LowPart == FILE_USE_FILE_POINTER_POSITION) {
			byteOffset = fileObject->CurrentByteOffset;
		} else if (IrpSp->Parameters.Write.ByteOffset.LowPart == FILE_WRITE_TO_END_OF_FILE) { // APPEND
			byteOffset.QuadPart = zp->z_size;
		}
	}

	if (FlagOn(Irp->Flags, IRP_PAGING_IO)) {

		if (byteOffset.QuadPart >= zp->z_size) {
			Status = STATUS_SUCCESS;
			goto out;
		}

		if (byteOffset.QuadPart + bufferLength > zp->z_size)
			bufferLength = zp->z_size - byteOffset.QuadPart;

		//ASSERT(fileObject->PrivateCacheMap != NULL);
	}
	
	if (!nocache && !CcCanIWrite(fileObject, bufferLength, TRUE, FALSE)) {
		Status = STATUS_PENDING;
		DbgBreakPoint();
		goto out;
	}

	if (nocache && !pagingio && fileObject->SectionObjectPointer &&
		fileObject->SectionObjectPointer->DataSectionObject) {
		IO_STATUS_BLOCK iosb;

		ExAcquireResourceExclusiveLite(vp->FileHeader.PagingIoResource, TRUE);

		CcFlushCache(fileObject->SectionObjectPointer, &byteOffset, bufferLength, &iosb);

		if (!NT_SUCCESS(iosb.Status)) {
			ExReleaseResourceLite(vp->FileHeader.PagingIoResource);
			Status = iosb.Status;
			goto out;
		}

		CcPurgeCacheSection(fileObject->SectionObjectPointer, &byteOffset, bufferLength, FALSE);
		ExReleaseResourceLite(vp->FileHeader.PagingIoResource);
	}

	void *SystemBuffer = MapUserBuffer(Irp);

	if (nocache) {

	} else {

		if (fileObject->PrivateCacheMap == NULL) {
			vnode_pager_setsize(vp, zp->z_size);
			vnode_setsizechange(vp, 0);
			CcInitializeCacheMap(fileObject,
				(PCC_FILE_SIZES)&vp->FileHeader.AllocationSize, FALSE,
				&CacheManagerCallbacks, vp);
			CcSetAdditionalCacheAttributes(fileObject, TRUE, TRUE); // FIXME: for now
			dprintf("%s: CcInitializeCacheMap\n", __func__);
			//CcSetReadAheadGranularity(fileObject, READ_AHEAD_GRANULARITY);
		}

		// If beyond valid data, zero between to expand (this is cachedfile, not paging io, extend ok)
		if (byteOffset.QuadPart + bufferLength > zp->z_size) {
#if 0
			LARGE_INTEGER ZeroStart, BeyondZeroEnd;
			ZeroStart.QuadPart = zp->z_size;
			BeyondZeroEnd.QuadPart = IrpSp->Parameters.Write.ByteOffset.QuadPart + IrpSp->Parameters.Write.Length;
			dprintf("%s: growing file\n", __func__);
			//CACHE_MANAGER(34)
			//	See the comment for FAT_FILE_SYSTEM(0x23)
			if (!CcZeroData(fileObject,
				&ZeroStart, &BeyondZeroEnd, 
				TRUE)) {
				dprintf("%s: CcZeroData failed\n", __func__);
			}
#endif
			// We have written "Length" into the "file" by the way of cache, so we need 
			// zp->z_size to reflect the new length, so we extend the file on disk, even though
			// the actual writes will come later (from CcMgr).
			dprintf("%s: growing file\n", __func__);

			// zfs_freesp() calls vnode_pager_setsize();
			Status = zfs_freesp(zp,
				byteOffset.QuadPart,  bufferLength,
				FWRITE, B_TRUE);
			ASSERT0(Status);
			// Confirm size grown
			//ASSERT(byteOffset.QuadPart + bufferLength == zp->z_size);
		} else {
			//vnode_pager_setsize(vp, zp->z_size);
		}

		// DO A NORMAL CACHED WRITE, if the MDL bit is not set,
		if (!FlagOn(IrpSp->MinorFunction, IRP_MN_MDL)) {

			// Since we may have grown the filesize, we need to give CcMgr a head's up.
			vnode_pager_setsize(vp, zp->z_size);
			CcSetFileSizes(fileObject, (PCC_FILE_SIZES)&vp->FileHeader.AllocationSize);
			vnode_setsizechange(vp, 0);

			dprintf("CcWrite:  offset [ 0x%llx - 0x%llx ] len 0x%lx\n", 
				byteOffset.QuadPart, byteOffset.QuadPart + bufferLength, bufferLength);
#if (NTDDI_VERSION >= NTDDI_WIN8)
			if (!CcCopyWriteEx(fileObject,
				&byteOffset,
				bufferLength,
				TRUE,
				SystemBuffer, 
				Irp->Tail.Overlay.Thread)) {
#else
			if (!CcCopyWrite(fileObject,
				&byteOffset,
				bufferLength,
				TRUE,
				SystemBuffer)) {
#endif
				dprintf("Could not wait\n");
				ASSERT0("failed copy");
			}

			//Irp->IoStatus.Status = STATUS_SUCCESS;
			Irp->IoStatus.Information = bufferLength;
			Status = STATUS_SUCCESS;
			goto out;
		} else {
			//  DO AN MDL WRITE
			CcPrepareMdlWrite(fileObject,
				&byteOffset,
				bufferLength,
				&Irp->MdlAddress,
				&Irp->IoStatus);

			Status = Irp->IoStatus.Status;
			goto out;
		}
	}

	uio_t *uio;
	uio = uio_create(1, byteOffset.QuadPart, UIO_SYSSPACE, UIO_WRITE);
	uio_addiov(uio, (user_addr_t)SystemBuffer, bufferLength);

	//dprintf("%s: offset %llx size %lx\n", __func__, byteOffset.QuadPart, bufferLength);

	dprintf("ZfsWrite: offset [ 0x%llx - 0x%llx ] len 0x%lx\n", 
		byteOffset.QuadPart, byteOffset.QuadPart + bufferLength, bufferLength);

	if (FlagOn(Irp->Flags, IRP_PAGING_IO))
		error = zfs_write(vp, uio, 0, NULL);  // Should we call vnop_pageout instead?
	else
		error = zfs_write(vp, uio, 0, NULL);

	//if (error == 0)
	//	zp->z_pflags |= ZFS_ARCHIVE;

	// EOF?
	if ((bufferLength == uio_resid(uio)) && error == ENOSPC)
		Status = STATUS_DISK_FULL;

	// Update bytes written
	Irp->IoStatus.Information = bufferLength - uio_resid(uio);

	uio_free(uio);

	// Update the file offset
out:
	if ((Status == STATUS_SUCCESS) &&
		(fileObject->Flags & FO_SYNCHRONOUS_IO) &&
		!(Irp->Flags & IRP_PAGING_IO)) {
		fileObject->CurrentByteOffset.QuadPart =
			byteOffset.QuadPart + Irp->IoStatus.Information;
	}

	VN_RELE(vp);

//	dprintf("  FileName: %wZ offset 0x%llx len 0x%lx mdl %p System %p\n", &fileObject->FileName,
//		byteOffset.QuadPart, bufferLength, Irp->MdlAddress, Irp->AssociatedIrp.SystemBuffer);

	// Unset the size-change, as we handled it directly in here
	vnode_setsizechange(vp, 0);

	return Status;
}

/*
 * The lifetime of a delete.
 * 1) If a file open is marked DELETE_ON_CLOSE in zfs_vnop_lookup() we will call
 * vnode_setdeleteonclose(vp) to signal the intent. This is so file_standard_information
 * can return DeletePending correctly (as well as a few more)
 * 2) Upon IRP_MJ_CLEANUP (closing a file handle) we are expected to remove the file
 * (as tested by IFStest.exe) we will call vnode_setdeleted(vp), this will:
 * 3) Make zfs_vnop_lookup() return ENOENT when "setdeleted" is set. Making it appear
 * as if the file was deleted - but retaining vp and zp as required by Windows.
 * 4) Eventually IRP_MJ_CLOSE is called, and if final, we can release vp and zp, and
 * if "setdeleted" was active, we can finally call delete_entry() to remove the file.
 */
NTSTATUS delete_entry(PDEVICE_OBJECT DeviceObject, PIRP Irp, PIO_STACK_LOCATION IrpSp)
{
	// In Unix, both zfs_unlink and zfs_rmdir expect a filename, and we do not have that here
	struct vnode *vp = NULL, *dvp = NULL;
	int error;
	char filename[MAXNAMELEN];
	ULONG outlen;
	znode_t *zp = NULL;

	if (IrpSp->FileObject->FsContext == NULL ||
		IrpSp->FileObject->FileName.Buffer == NULL ||
		IrpSp->FileObject->FileName.Length == 0) {
		dprintf("%s: called with missing arguments, can't delete\n", __func__);
		return STATUS_INSTANCE_NOT_AVAILABLE; // FIXME
	}

	vp = IrpSp->FileObject->FsContext;
	zp = VTOZ(vp);
	ASSERT(zp != NULL);
	zfsvfs_t *zfsvfs = zp->z_zfsvfs;

	uint64_t parent = 0;
	znode_t *dzp;

	// No dvp, lookup parent
	VERIFY(sa_lookup(zp->z_sa_hdl, SA_ZPL_PARENT(zp->z_zfsvfs),
		&parent, sizeof(parent)) == 0);
	error = zfs_zget(zp->z_zfsvfs, parent, &dzp);
	if (error)
		return STATUS_INSTANCE_NOT_AVAILABLE;  // FIXME 
	dvp = ZTOV(dzp);

	// Unfortunately, filename is littered with "\", clean it up,
	// or search based on ID to get name?
	dprintf("%s: deleting '%.*S'\n", __func__,
		IrpSp->FileObject->FileName.Length / sizeof(WCHAR),
		IrpSp->FileObject->FileName.Buffer);

	error = RtlUnicodeToUTF8N(filename, MAXNAMELEN, &outlen,
		IrpSp->FileObject->FileName.Buffer, IrpSp->FileObject->FileName.Length);
	
	if (error != STATUS_SUCCESS &&
		error != STATUS_SOME_NOT_MAPPED) {
		VN_RELE(dvp);
		dprintf("%s: some illegal characters\n", __func__);
		return STATUS_ILLEGAL_CHARACTER;
	}
	while (outlen > 0 && filename[outlen - 1] == '\\') outlen--;
	filename[outlen] = 0;

	// FIXME, use z_name_cache and offset
	char *finalname = NULL;
	if ((finalname = strrchr(filename, '\\')) != NULL)
		finalname = &finalname[1];
	else
		finalname = filename;

	// Release final HOLD on item, ready for deletion
	int isdir = vnode_isdir(vp);

	/* ZFS deletes from filename, so RELE last hold on vp. */
	VN_RELE(vp);
	vp = NULL;

	if (isdir) {
		
		error = zfs_rmdir(VTOZ(dvp), finalname, NULL, NULL, 0);

	} else {

		error = zfs_remove(VTOZ(dvp), finalname, NULL, 0);

	}

	if (error == ENOTEMPTY)
		error = STATUS_DIRECTORY_NOT_EMPTY;

	// Release parent.
	VN_RELE(dvp);

	dprintf("%s: returning %d\n", __func__, error);
	return error;
}

NTSTATUS flush_buffers(PDEVICE_OBJECT DeviceObject, PIRP Irp, PIO_STACK_LOCATION IrpSp)
{

	PFILE_OBJECT FileObject = IrpSp->FileObject;
	NTSTATUS Status;

	dprintf("%s: \n", __func__);

	if (FileObject == NULL || FileObject->FsContext == NULL)
		return STATUS_INVALID_PARAMETER;

	struct vnode *vp = FileObject->FsContext;
	if (VN_HOLD(vp) == 0) {
		znode_t *zp = VTOZ(vp);
		zfsvfs_t *zfsvfs = zp->z_zfsvfs;
		Status = zfs_vnop_ioctl_fullfsync(vp, NULL, zfsvfs);
		VN_RELE(vp);
	}
	return Status;
}

NTSTATUS query_security(PDEVICE_OBJECT DeviceObject, PIRP Irp, PIO_STACK_LOCATION IrpSp)
{
	PFILE_OBJECT FileObject = IrpSp->FileObject;
	NTSTATUS Status;

	dprintf("%s: \n", __func__);

	if (FileObject == NULL || FileObject->FsContext == NULL)
		return STATUS_INVALID_PARAMETER;

	void *buf = MapUserBuffer(Irp);

	struct vnode *vp = FileObject->FsContext;
	VN_HOLD(vp);
	PSECURITY_DESCRIPTOR sd;
	sd = vnode_security(vp);
	ULONG buflen = IrpSp->Parameters.QuerySecurity.Length;
	Status = SeQuerySecurityDescriptorInfo(
		&IrpSp->Parameters.QuerySecurity.SecurityInformation,
		buf, 
		&buflen,
		&sd);
	VN_RELE(vp);

	if (Status == STATUS_BUFFER_TOO_SMALL) {
		Status = STATUS_BUFFER_OVERFLOW;
		Irp->IoStatus.Information = buflen;
	} else if (NT_SUCCESS(Status)) {
		Irp->IoStatus.Information = IrpSp->Parameters.QuerySecurity.Length;
	} else {
		Irp->IoStatus.Information = 0;
	}

	return Status;
}

NTSTATUS set_security(PDEVICE_OBJECT DeviceObject, PIRP Irp, PIO_STACK_LOCATION IrpSp)
{
	PFILE_OBJECT FileObject = IrpSp->FileObject;
	NTSTATUS Status = STATUS_SUCCESS;

	dprintf("%s: \n", __func__);

	if (FileObject == NULL || FileObject->FsContext == NULL)
		return STATUS_INVALID_PARAMETER;

	struct vnode *vp = FileObject->FsContext;
	VN_HOLD(vp);
	PSECURITY_DESCRIPTOR oldsd;
	oldsd = vnode_security(vp);


	// READONLY check here
	znode_t *zp = VTOZ(vp);
	zfsvfs_t *zfsvfs = zp->z_zfsvfs;
	if (vfs_isrdonly(zfsvfs->z_vfs)) {
		Status = STATUS_MEDIA_WRITE_PROTECTED;
		goto err;
	}

	Status = SeSetSecurityDescriptorInfo(NULL, 
		&IrpSp->Parameters.SetSecurity.SecurityInformation, 
		IrpSp->Parameters.SetSecurity.SecurityDescriptor, 
		(void **)&vp->security_descriptor,
		PagedPool, 
		IoGetFileObjectGenericMapping());

	if (!NT_SUCCESS(Status))
		goto err;

	ExFreePool(oldsd);

	// Now, we might need to update ZFS ondisk information
	vattr_t vattr;
	vattr.va_mask = 0;
	BOOLEAN defaulted;

	if (IrpSp->Parameters.SetSecurity.SecurityInformation & OWNER_SECURITY_INFORMATION) {
		PSID owner;
		Status = RtlGetOwnerSecurityDescriptor(vnode_security(vp), &owner, &defaulted);
		if (Status == STATUS_SUCCESS) {
			vattr.va_uid = zfs_sid2uid(owner);
			vattr.va_mask |= ATTR_UID;
		}
/*		else
			zp->z_uid = UID_NOBODY;
*/
	}
	if (IrpSp->Parameters.SetSecurity.SecurityInformation & GROUP_SECURITY_INFORMATION) {
		PSID group;
		Status = RtlGetGroupSecurityDescriptor(vnode_security(vp), &group, &defaulted);
		if (Status == STATUS_SUCCESS) {
			vattr.va_gid = zfs_sid2uid(group); // uid/gid reverse is identical
			vattr.va_mask |= ATTR_GID;
		}
	}

	// Do we need to update ZFS?
	if (vattr.va_mask != 0) {
		zfs_setattr(vp, &vattr, 0, NULL);
		Status = STATUS_SUCCESS;
	}

	Irp->IoStatus.Information = 0;
	zfs_send_notify(zfsvfs, zp->z_name_cache, zp->z_name_offset,
		FILE_NOTIFY_CHANGE_SECURITY,
		FILE_ACTION_MODIFIED);

err:
	VN_RELE(vp);
	return Status;
}

//#define IOCTL_VOLUME_BASE ((DWORD) 'V')
//#define IOCTL_VOLUME_GET_GPT_ATTRIBUTES      CTL_CODE(IOCTL_VOLUME_BASE,14,METHOD_BUFFERED,FILE_ANY_ACCESS)

#define IOCTL_VOLUME_POST_ONLINE    CTL_CODE(IOCTL_VOLUME_BASE, 25, METHOD_BUFFERED, FILE_READ_ACCESS | FILE_WRITE_ACCESS)

NTSTATUS ioctl_storage_get_device_number(PDEVICE_OBJECT DeviceObject, PIRP Irp, PIO_STACK_LOCATION IrpSp)
{

	if (IrpSp->Parameters.QueryFile.Length < sizeof(STORAGE_DEVICE_NUMBER)) {
		Irp->IoStatus.Information = sizeof(STORAGE_DEVICE_NUMBER);
		return STATUS_BUFFER_TOO_SMALL;
	}

	PSTORAGE_DEVICE_NUMBER sdn = Irp->AssociatedIrp.SystemBuffer;
	sdn->DeviceNumber = 0;
	sdn->DeviceType = FILE_DEVICE_VIRTUAL_DISK;
	sdn->PartitionNumber = -1; // -1 means can't be partitioned

	Irp->IoStatus.Information = sizeof(STORAGE_DEVICE_NUMBER);
	return STATUS_SUCCESS;
}


NTSTATUS ioctl_volume_get_volume_disk_extents(PDEVICE_OBJECT DeviceObject, PIRP Irp, PIO_STACK_LOCATION IrpSp)
{
	VOLUME_DISK_EXTENTS *vde = Irp->AssociatedIrp.SystemBuffer;

	if (IrpSp->Parameters.QueryFile.Length < sizeof(VOLUME_DISK_EXTENTS)) {
		Irp->IoStatus.Information = sizeof(VOLUME_DISK_EXTENTS);
		return STATUS_BUFFER_TOO_SMALL;
	}

	Irp->IoStatus.Information = sizeof(VOLUME_DISK_EXTENTS);
	RtlZeroMemory(vde, sizeof(VOLUME_DISK_EXTENTS));
	vde->NumberOfDiskExtents = 1;

	return STATUS_SUCCESS;
}

NTSTATUS volume_create(PDEVICE_OBJECT DeviceObject, PIRP Irp, PIO_STACK_LOCATION IrpSp)
{
	mount_t *zmo = DeviceObject->DeviceExtension;

	// This is also called from fsContext when IRP_MJ_CREATE FileName is NULL
	/* VERIFY(zmo->type == MOUNT_TYPE_DCB); */
	if (zmo->vpb != NULL)
		IrpSp->FileObject->Vpb = zmo->vpb;
	else
		IrpSp->FileObject->Vpb = DeviceObject->Vpb;

	//		dprintf("Setting FileObject->Vpb to %p\n", IrpSp->FileObject->Vpb);
	//SetFileObjectForVCB(IrpSp->FileObject, zmo);
	//IrpSp->FileObject->SectionObjectPointer = &zmo->SectionObjectPointers;
	//IrpSp->FileObject->FsContext = &zmo->VolumeFileHeader;

	/*
	 * Check the ShareAccess requested:
	 *         0         : exclusive
	 * FILE_SHARE_READ   : The file can be opened for read access by other threads 
	 * FILE_SHARE_WRITE  : The file can be opened for write access by other threads
	 * FILE_SHARE_DELETE : The file can be opened for delete access by other threads
	 */
	if ((IrpSp->Parameters.Create.ShareAccess == 0) &&
		zmo->volume_opens != 0) {
		dprintf("%s: sharing violation\n", __func__);
		return STATUS_SHARING_VIOLATION;
	}

	atomic_inc_64(&zmo->volume_opens);
	Irp->IoStatus.Information = FILE_OPENED;
	return STATUS_SUCCESS;
}

NTSTATUS volume_close(PDEVICE_OBJECT DeviceObject, PIRP Irp, PIO_STACK_LOCATION IrpSp)
{
	mount_t *zmo = DeviceObject->DeviceExtension;
	VERIFY(zmo->type == MOUNT_TYPE_DCB);
	atomic_dec_64(&zmo->volume_opens);
	return STATUS_SUCCESS;
}

/*
 * IRP_MJ_CLEANUP - sent when Windows is done with FileObject HANDLE (one of many)
 *     the vp is not released here, just decrease a count of vp.
 */
int zfs_fileobject_cleanup(PDEVICE_OBJECT DeviceObject, PIRP Irp, PIO_STACK_LOCATION IrpSp,
	vnode_t **hold_vp)
{
	int Status = STATUS_SUCCESS;
	mount_t *zmo = NULL;

	if (IrpSp->FileObject && IrpSp->FileObject->FsContext) {
		struct vnode* vp = IrpSp->FileObject->FsContext;
		zfs_dirlist_t* zccb = IrpSp->FileObject->FsContext2;

		znode_t* zp = VTOZ(vp); // zp for notify removal

		vnode_rele(vp); // Release longterm hold finally.

		dprintf("IRP_MJ_CLEANUP: '%s' iocount %u usecount %u\n",
			zp && zp->z_name_cache ? zp->z_name_cache : "", vp->v_iocount, vp->v_usecount);

		vnode_lock(vp);
		IoRemoveShareAccess(IrpSp->FileObject, &vp->share_access);
		vnode_unlock(vp);

		int isdir = vnode_isdir(vp);

		zmo = DeviceObject->DeviceExtension;
		VERIFY(zmo->type == MOUNT_TYPE_VCB);

		if (zp != NULL) {

			/* Technically, this should only be called on the FileObject which
			 * opened the file with DELETE_ON_CLOSE - in fastfat, that is stored
			 * in the ccb (context) set in FsContext2, which holds data for each
			 * FileObject context. Possibly, we should as well. (We do for dirs)
			 */
			if (zccb && zccb->deleteonclose) {
				zfsvfs_t* zfsvfs = vfs_fsprivate(zmo);

				zccb->deleteonclose = 0;

				if (zp->z_name_cache != NULL) {
					if (isdir) {
						dprintf("sending DIR notify: FileDeleted '%s' name '%s'\n", zp->z_name_cache, &zp->z_name_cache[zp->z_name_offset]);
						zfs_send_notify(zfsvfs, zp->z_name_cache, zp->z_name_offset,
							FILE_NOTIFY_CHANGE_DIR_NAME,
							FILE_ACTION_REMOVED);
					} else {
						dprintf("sending FILE notify: FileDeleted '%s' name '%s'\n", zp->z_name_cache, &zp->z_name_cache[zp->z_name_offset]);
						zfs_send_notify(zfsvfs, zp->z_name_cache, zp->z_name_offset,
							FILE_NOTIFY_CHANGE_FILE_NAME,
							FILE_ACTION_REMOVED);
					}
				}

				// Windows needs us to unlink it now, since CLOSE can be delayed
				// and parent deletions might fail (ENOTEMPTY).

				// This releases zp!
				Status = delete_entry(DeviceObject, Irp, IrpSp);
				if (Status != 0)
					dprintf("Deletion failed: %d\n", Status);

				zp = NULL;

				// delete_entry will always consume an IOCOUNT.
				*hold_vp = NULL;

				Status = STATUS_SUCCESS;

				// FILE_CLEANUP_UNKNOWN FILE_CLEANUP_WRONG_DEVICE FILE_CLEANUP_FILE_REMAINS
				// FILE_CLEANUP_FILE_DELETED FILE_CLEANUP_LINK_DELETED FILE_CLEANUP_STREAM_DELETED
				// FILE_CLEANUP_POSIX_STYLE_DELETE
#if defined (ZFS_FS_ATTRIBUTE_CLEANUP_INFO) && defined(ZFS_FS_ATTRIBUTE_POSIX)
				Irp->IoStatus.Information = FILE_CLEANUP_FILE_DELETED | FILE_CLEANUP_POSIX_STYLE_DELETE;
#elif defined (ZFS_FS_ATTRIBUTE_CLEANUP_INFO)
				Irp->IoStatus.Information = FILE_CLEANUP_FILE_DELETED;
#endif
			}

		}

		/* The use of "zp" is only used as identity, not referenced. */
		if (isdir) {
			dprintf("Removing all notifications for directory: %p\n", zp);
			FsRtlNotifyCleanup(zmo->NotifySync, &zmo->DirNotifyList, zp);
		}
		// Finish with Notifications
		dprintf("Removing notifications for file\n");
		FsRtlNotifyFullChangeDirectory(zmo->NotifySync, &zmo->DirNotifyList,
			zp, NULL, FALSE, FALSE, 0, NULL, NULL, NULL);

		SetFlag(IrpSp->FileObject->Flags, FO_CLEANUP_COMPLETE);

		Status = STATUS_SUCCESS;
	}

	return Status;
}

/*
 * IRP_MJ_CLOSE - sent when Windows is done with FileObject, and we can free memory.
 */
int zfs_fileobject_close(PDEVICE_OBJECT DeviceObject, PIRP Irp, PIO_STACK_LOCATION IrpSp,
	vnode_t **hold_vp)
{
	int Status = STATUS_SUCCESS;

	ASSERT(hold_vp != NULL);

	if (IrpSp->FileObject) {

		// Careful not to print something before vnode_fileobject_remove -
		// if print is swapped out, we think fileobject is still valid.
		//dprintf("IRP_MJ_CLOSE: '%wZ' \n", &IrpSp->FileObject->FileName);

		if (IrpSp->FileObject->FsContext) {
			//dprintf("CLOSE clearing FsContext of FO 0x%llx\n", IrpSp->FileObject);
			// Mark vnode for cleanup, we grab a HOLD to make sure it isn't
			// released right here, but marked to be released upon reaching 0 count
			vnode_t *vp = IrpSp->FileObject->FsContext;
			znode_t *zp = (VTOZ(vp));

			/*
			 * First encourage Windows to release the FileObject, CcMgr etc, flush everything.
			 */

			// FileObject should/could no longer point to vp.
			zfs_decouplefileobject(vp, IrpSp->FileObject);
			vnode_fileobject_remove(vp, IrpSp->FileObject);

			/* 
			 * If we can release now, do so.
			 * If the reference count for the per-file context structure reaches zero
			 * and both the ImageSectionObject and DataSectionObject of the SectionObjectPointers
			 * field from the FILE_OBJECT is zero, the filter driver may then delete the per-file context data.
			 *
			 */

			if (!vnode_isvroot(vp)) {

				/* Take hold from dispatcher, we will try to release in recycle */
				*hold_vp = NULL;

				// Release vp - vnode_recycle expects iocount==1
				// we don't recycle root (unmount does) or RELE on recycle error
				if (vnode_isvroot(vp) || (vnode_recycle(vp) != 0)) {
					// If recycle failed, manually release dispatcher's HOLD
					dprintf("IRP_CLOSE failed to recycle. is_empty %d\n", vnode_fileobject_empty(vp, 1));
					VN_RELE(vp);
				}

				Status = STATUS_SUCCESS;

				vp = NULL; // Paranoia, signal it is gone.

			} else { /* root node */
				Status = STATUS_SUCCESS;
			}
		}
	}

	return Status;
}

/*
 * We received a long-lived ioctl, so lets setup a taskq to handle it, and return pending
 */
void zfsdev_async_thread(void *arg)
{
	NTSTATUS Status;
	PIRP Irp;
	Irp = (PIRP)arg;
	
	dprintf("%s: starting ioctl\n", __func__);

	/* Use FKIOCTL to make sure it calls bcopy instead */
	Status = zfsdev_ioctl(NULL, Irp, FKIOCTL); 

	dprintf("%s: finished ioctl %d\n", __func__, Status);

	PMDL mdl = Irp->Tail.Overlay.DriverContext[0];
	if (mdl) {
		UnlockAndFreeMdl(mdl);
		Irp->Tail.Overlay.DriverContext[0] = NULL;
	}
	void *fp = Irp->Tail.Overlay.DriverContext[1];
	if (fp) {
		ObDereferenceObject(fp);
		ZwClose(Irp->Tail.Overlay.DriverContext[2]);
	}

	IoCompleteRequest(Irp, Status == STATUS_SUCCESS ? IO_DISK_INCREMENT : IO_NO_INCREMENT);
}

NTSTATUS zfsdev_async(PDEVICE_OBJECT DeviceObject, PIRP Irp)
{
	int error;
	PMDL mdl = NULL;
	PIO_STACK_LOCATION IrpSp;
	zfs_cmd_t *zc;
	void *fp = NULL;

	IrpSp = IoGetCurrentIrpStackLocation(Irp);

	IoMarkIrpPending(Irp);

	/* 
	 * A separate thread to the one that called us may not access the buffer from userland,
	 * So we have to map the in/out buffer, and put that address in its place.
	 */
	error = ddi_copysetup(IrpSp->Parameters.DeviceIoControl.Type3InputBuffer, sizeof(zfs_cmd_t),
		&IrpSp->Parameters.DeviceIoControl.Type3InputBuffer, &mdl);
	if (error) return error;

	/* Save the MDL so we can free it once done */
	Irp->Tail.Overlay.DriverContext[0] = mdl;

	/* We would also need to handle zc->zc_nvlist_src and zc->zc_nvlist_dst 
	 * which is tricker, since they are unpacked into nvlists deep in zfsdev_ioctl 
	 */

	/* The same problem happens for the filedescriptor from userland, also needs to be kernelMode */
	zc = IrpSp->Parameters.DeviceIoControl.Type3InputBuffer;

	if (zc->zc_cookie) {
		error = ObReferenceObjectByHandle((HANDLE)zc->zc_cookie, 0, 0, KernelMode, &fp, 0);
		if (error != STATUS_SUCCESS) goto out;
		Irp->Tail.Overlay.DriverContext[1] = fp;

		HANDLE h = NULL;
		error = ObOpenObjectByPointer(fp, OBJ_FORCE_ACCESS_CHECK | OBJ_KERNEL_HANDLE, NULL, GENERIC_READ|GENERIC_WRITE, *IoFileObjectType, KernelMode, &h);
		if (error != STATUS_SUCCESS) goto out;
		dprintf("mapped filed is 0x%x\n", h);
		zc->zc_cookie = (uint64_t)h;
		Irp->Tail.Overlay.DriverContext[2] = h;
	}

	taskq_dispatch(system_taskq, zfsdev_async_thread, (void*)Irp, TQ_SLEEP);

	return STATUS_PENDING;
out:	
	if (mdl) {
		UnlockAndFreeMdl(mdl);
	}
	if (fp) {
		ObDereferenceObject(fp);
	}
	return error;
}



/*
 * This is the ioctl handler for ioctl done directly on /dev/zfs node. This means
 * all the internal ZFS ioctls, like ZFS_IOC_SEND etc. But, we will also get
 * general Windows ioctls, not specific to volumes, or filesystems.
 */
_Function_class_(DRIVER_DISPATCH)
static NTSTATUS
ioctlDispatcher(
	_In_ PDEVICE_OBJECT DeviceObject,
	_Inout_ PIRP Irp,
	PIO_STACK_LOCATION IrpSp
)
{
	NTSTATUS Status;

	PAGED_CODE();

	dprintf("  %s: enter: major %d: minor %d: %s ioctlDeviceObject\n", __func__, IrpSp->MajorFunction, IrpSp->MinorFunction,
		major2str(IrpSp->MajorFunction, IrpSp->MinorFunction));


	Status = STATUS_NOT_IMPLEMENTED;

	switch (IrpSp->MajorFunction) {

	case IRP_MJ_CREATE:
		dprintf("IRP_MJ_CREATE: zfsdev FileObject %p name '%wZ' length %u flags 0x%x\n",
			IrpSp->FileObject, IrpSp->FileObject->FileName, 
			IrpSp->FileObject->FileName.Length, IrpSp->Flags);
		Status = zfsdev_open(IrpSp->FileObject, Irp);
		break;
	case IRP_MJ_CLOSE:
		Status = zfsdev_release((dev_t)IrpSp->FileObject, Irp);
		break;
	case IRP_MJ_DEVICE_CONTROL:
		{
			/* Is it a ZFS ioctl? */
			u_long cmd = IrpSp->Parameters.DeviceIoControl.IoControlCode;
			if (cmd >= ZFS_IOC_FIRST &&
				cmd < ZFS_IOC_LAST) {

				/* Some IOCTL are very long-living, so we will put them in the
				 * background and return PENDING. Possibly we should always do
				 * this logic, but some ioctls are really short lived.
				 */
				switch (cmd) {
				case ZFS_IOC_UNREGISTER_FS:
					// We abuse returnedBytes to send back busy
					Irp->IoStatus.Information = zfs_ioc_unregister_fs();
					Status = STATUS_SUCCESS;
					break;
					/*
					 * So to do ioctl in async mode is a hassle, we have to do the copyin/copyout
					 * MDL work in *this* thread, as the thread we spawn does not have access.
					 * This would also include zc->zc_nvlist_src / zc->zc_nvlist_dst, so 
					 * zfsdev_ioctl() would need to be changed quite a bit. The file-descriptor
					 * passed in (zfs send/recv) also needs to be opened for kernel mode. This
					 * code is left here as an example on how it can be done (without zc->zc_nvlist_*)
					 * but we currently do not use it. Everything is handled synchronously.
					 *
				case ZFS_IOC_SEND:
					Status = zfsdev_async(DeviceObject, Irp);
					break;
					 *
					 */
				default:
					Status = zfsdev_ioctl(DeviceObject, Irp, 0);
				} // switch cmd for async
				break;
			}
			/* Not ZFS ioctl, handle Windows ones */
			switch (cmd) {
			case IOCTL_VOLUME_GET_GPT_ATTRIBUTES:
				dprintf("IOCTL_VOLUME_GET_GPT_ATTRIBUTES\n");
				Status = 0;
				break;
			case IOCTL_MOUNTDEV_QUERY_DEVICE_NAME:
				dprintf("IOCTL_MOUNTDEV_QUERY_DEVICE_NAME\n");
				Status = ioctl_query_device_name(DeviceObject, Irp, IrpSp);
				break;
			case IOCTL_MOUNTDEV_QUERY_UNIQUE_ID:
				dprintf("IOCTL_MOUNTDEV_QUERY_UNIQUE_ID\n");
				Status = ioctl_query_unique_id(DeviceObject, Irp, IrpSp);
				break;
			case IOCTL_MOUNTDEV_QUERY_STABLE_GUID:
				dprintf("IOCTL_MOUNTDEV_QUERY_STABLE_GUID\n");
				Status = ioctl_query_stable_guid(DeviceObject, Irp, IrpSp);
				break;
			case IOCTL_MOUNTDEV_QUERY_SUGGESTED_LINK_NAME:
				dprintf("IOCTL_MOUNTDEV_QUERY_SUGGESTED_LINK_NAME\n");
				break;
			case IOCTL_VOLUME_ONLINE:
				dprintf("IOCTL_VOLUME_ONLINE\n");
				Status = STATUS_SUCCESS;
				break;
			case IOCTL_DISK_IS_WRITABLE:
				dprintf("IOCTL_DISK_IS_WRITABLE\n");
				Status = STATUS_SUCCESS;
				break;
			case IOCTL_DISK_MEDIA_REMOVAL:
				dprintf("IOCTL_DISK_MEDIA_REMOVAL\n");
				Status = STATUS_SUCCESS;
				break;
			case IOCTL_STORAGE_MEDIA_REMOVAL:
				dprintf("IOCTL_STORAGE_MEDIA_REMOVAL\n");
				Status = STATUS_SUCCESS;
				break;
			case IOCTL_VOLUME_POST_ONLINE:
				dprintf("IOCTL_VOLUME_POST_ONLINE\n");
				Status = STATUS_SUCCESS;
				break;
				/* kstat ioctls */
			case KSTAT_IOC_CHAIN_ID:
				dprintf("KSTAT_IOC_CHAIN_ID\n");
				Status = spl_kstat_chain_id(DeviceObject, Irp, IrpSp);
				break;
			case KSTAT_IOC_READ:
				dprintf("KSTAT_IOC_READ\n");
				Status = spl_kstat_read(DeviceObject, Irp, IrpSp);
				break;
			case KSTAT_IOC_WRITE:
				dprintf("KSTAT_IOC_WRITE\n");
				Status = spl_kstat_write(DeviceObject, Irp, IrpSp);
				break;
			default:
				dprintf("**** unknown Windows IOCTL: 0x%lx\n", cmd);
			}
			
		}
		break;

	case IRP_MJ_CLEANUP:
		Status = STATUS_SUCCESS;
		break;

	case IRP_MJ_FILE_SYSTEM_CONTROL:
		switch (IrpSp->MinorFunction) {
		case IRP_MN_MOUNT_VOLUME:
			dprintf("IRP_MN_MOUNT_VOLUME ioctl\n");
			Status = zfs_vnop_mount(DeviceObject, Irp, IrpSp);
			break;
		default:
			dprintf("IRP_MJ_FILE_SYSTEM_CONTROL default case!\n");
			break;
		}
		break;

	case IRP_MJ_PNP:
		switch (IrpSp->MinorFunction) {
		case IRP_MN_QUERY_CAPABILITIES:
			Status = QueryCapabilities(DeviceObject, Irp, IrpSp);
			break;
		case IRP_MN_QUERY_DEVICE_RELATIONS:
			Status = STATUS_NOT_IMPLEMENTED;
			break;
		case IRP_MN_QUERY_ID:
			Status = pnp_query_id(DeviceObject, Irp, IrpSp);
			break;
		case IRP_MN_QUERY_PNP_DEVICE_STATE:
			Status = pnp_device_state(DeviceObject, Irp, IrpSp);
			break;
		case IRP_MN_QUERY_REMOVE_DEVICE:
			dprintf("IRP_MN_QUERY_REMOVE_DEVICE\n");
			Status = STATUS_UNSUCCESSFUL;
			break;
		case IRP_MN_SURPRISE_REMOVAL:
			dprintf("IRP_MN_SURPRISE_REMOVAL\n");
			Status = STATUS_SUCCESS;
			break;
		case IRP_MN_REMOVE_DEVICE:
			dprintf("IRP_MN_REMOVE_DEVICE\n");
			Status = STATUS_SUCCESS;
			break;
		case IRP_MN_CANCEL_REMOVE_DEVICE:
			dprintf("IRP_MN_CANCEL_REMOVE_DEVICE\n");
			Status = STATUS_SUCCESS;
			break;
		}
		break;

	}

	return Status;
}

/*
 * This is the IOCTL handler for the "virtual" disk volumes we create
 * to mount ZFS, and ZVOLs, things like get partitions, and volume size.
 * But also open/read/write/close requests of volume access (like dd'ing the
 * /dev/diskX node directly).
 */
_Function_class_(DRIVER_DISPATCH)
static NTSTATUS
diskDispatcher(
	_In_ PDEVICE_OBJECT DeviceObject,
	_Inout_ PIRP Irp,
	PIO_STACK_LOCATION IrpSp
)
{
	NTSTATUS Status;

	PAGED_CODE();

	dprintf("  %s: enter: major %d: minor %d: %s diskDeviceObject\n", __func__, IrpSp->MajorFunction, IrpSp->MinorFunction,
		major2str(IrpSp->MajorFunction, IrpSp->MinorFunction));


	Status = STATUS_NOT_IMPLEMENTED;

	switch (IrpSp->MajorFunction) {

	case IRP_MJ_CREATE:
		dprintf("IRP_MJ_CREATE: volume FileObject %p related %p name '%wZ' flags 0x%x\n",
			IrpSp->FileObject, IrpSp->FileObject ? IrpSp->FileObject->RelatedFileObject : NULL,
			IrpSp->FileObject->FileName, IrpSp->Flags);

		Status = volume_create(DeviceObject, Irp, IrpSp);
		break;
	case IRP_MJ_CLOSE:
		Status = volume_close(DeviceObject, Irp, IrpSp);
		break;
	case IRP_MJ_DEVICE_CONTROL:
	{
		u_long cmd = IrpSp->Parameters.DeviceIoControl.IoControlCode;
		/* Not ZFS ioctl, handle Windows ones */
		switch (cmd) {
		case IOCTL_VOLUME_GET_GPT_ATTRIBUTES:
			dprintf("IOCTL_VOLUME_GET_GPT_ATTRIBUTES\n");
			Status = 0;
			break;
		case IOCTL_MOUNTDEV_QUERY_DEVICE_NAME:
			dprintf("IOCTL_MOUNTDEV_QUERY_DEVICE_NAME\n");
			Status = ioctl_query_device_name(DeviceObject, Irp, IrpSp);
			break;
		case IOCTL_MOUNTDEV_QUERY_UNIQUE_ID:
			dprintf("IOCTL_MOUNTDEV_QUERY_UNIQUE_ID\n");
			Status = ioctl_query_unique_id(DeviceObject, Irp, IrpSp);
			break;
		case IOCTL_MOUNTDEV_QUERY_STABLE_GUID:
			dprintf("IOCTL_MOUNTDEV_QUERY_STABLE_GUID\n");
			Status = ioctl_mountdev_query_stable_guid(DeviceObject, Irp, IrpSp);
			break;
		case IOCTL_MOUNTDEV_QUERY_SUGGESTED_LINK_NAME:
			dprintf("IOCTL_MOUNTDEV_QUERY_SUGGESTED_LINK_NAME\n");
			Status = ioctl_mountdev_query_suggested_link_name(DeviceObject, Irp, IrpSp);
			break;
		case IOCTL_VOLUME_ONLINE:
			dprintf("IOCTL_VOLUME_ONLINE\n");
			Status = STATUS_SUCCESS;
			break;
		case IOCTL_VOLUME_OFFLINE:
		case IOCTL_VOLUME_IS_OFFLINE:
			dprintf("IOCTL_VOLUME_OFFLINE\n");
			Status = STATUS_SUCCESS;
			break;
		case IOCTL_DISK_IS_WRITABLE:
			dprintf("IOCTL_DISK_IS_WRITABLE\n");
			Status = STATUS_SUCCESS;
			break;
		case IOCTL_DISK_MEDIA_REMOVAL:
			dprintf("IOCTL_DISK_MEDIA_REMOVAL\n");
			Status = STATUS_SUCCESS;
			break;
		case IOCTL_STORAGE_MEDIA_REMOVAL:
			dprintf("IOCTL_STORAGE_MEDIA_REMOVAL\n");
			Status = STATUS_SUCCESS;
			break;
		case IOCTL_VOLUME_POST_ONLINE:
			dprintf("IOCTL_VOLUME_POST_ONLINE\n");
			Status = STATUS_SUCCESS;
			break;
		case IOCTL_STORAGE_GET_HOTPLUG_INFO:
			dprintf("IOCTL_STORAGE_GET_HOTPLUG_INFO\n");
			Status = ioctl_storage_get_hotplug_info(DeviceObject, Irp, IrpSp);
			break;
		case IOCTL_STORAGE_QUERY_PROPERTY:
			dprintf("IOCTL_STORAGE_QUERY_PROPERTY\n");
			Status = ioctl_storage_query_property(DeviceObject, Irp, IrpSp);
			break;
		case IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS:
			dprintf("IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS\n");
			Status = ioctl_volume_get_volume_disk_extents(DeviceObject, Irp, IrpSp);
			break;
		case IOCTL_STORAGE_GET_DEVICE_NUMBER:
			dprintf("IOCTL_STORAGE_GET_DEVICE_NUMBER\n");
			Status = ioctl_storage_get_device_number(DeviceObject, Irp, IrpSp);
			break;
		case IOCTL_DISK_CHECK_VERIFY:
			Status = STATUS_SUCCESS;
			break;
		case IOCTL_STORAGE_CHECK_VERIFY2:
			dprintf("IOCTL_STORAGE_CHECK_VERIFY2\n");
			Status = STATUS_SUCCESS;
			break;
		case IOCTL_VOLUME_IS_DYNAMIC:
		{
			uint8_t *buf = (UINT8*)Irp->AssociatedIrp.SystemBuffer;
			*buf = 1;
			Irp->IoStatus.Information = 1;
			Status = STATUS_SUCCESS;
			break;
		}
		case IOCTL_MOUNTDEV_LINK_CREATED:
			dprintf("IOCTL_MOUNTDEV_LINK_CREATED\n");
			Status = STATUS_SUCCESS;
			break;
		case 0x4d0010: // Same as IOCTL_MOUNTDEV_LINK_CREATED but bit 14,15 are 0 (access permissions)
			dprintf("IOCTL_MOUNTDEV_LINK_CREATED v2\n");
			Status = STATUS_SUCCESS;
			break;
		case IOCTL_MOUNTDEV_LINK_DELETED:
			dprintf("IOCTL_MOUNTDEV_LINK_DELETED\n");
			Status = STATUS_SUCCESS;
			break;
		case 0x4d0014: // Same as IOCTL_MOUNTDEV_LINK_DELETED but bit 14,15 are 0 (access permissions)
			dprintf("IOCTL_MOUNTDEV_LINK_DELETED v2\n");
			Status = STATUS_SUCCESS;
			break;
		case IOCTL_DISK_GET_PARTITION_INFO_EX:
			dprintf("IOCTL_DISK_GET_PARTITION_INFO_EX\n");
			Status = ioctl_disk_get_partition_info_ex(DeviceObject, Irp, IrpSp);
			break;
		case IOCTL_DISK_GET_DRIVE_GEOMETRY:
			dprintf("IOCTL_DISK_GET_DRIVE_GEOMETRY\n");
			Status = ioctl_disk_get_drive_geometry(DeviceObject, Irp, IrpSp);
			break;
		default:
			dprintf("**** unknown disk Windows IOCTL: 0x%lx\n", cmd);
		}

	}
	break;

	case IRP_MJ_CLEANUP:
		Status = STATUS_SUCCESS;
		break;

		// Technically we don't really let them read from the virtual devices that
		// hold the ZFS filesystem, so we just return all zeros.
	case IRP_MJ_READ:
		dprintf("disk fake read\n");
		uint64_t bufferLength;
		bufferLength = IrpSp->Parameters.Read.Length;
		Irp->IoStatus.Information = bufferLength;
		Status = STATUS_SUCCESS;
		break;

	case IRP_MJ_WRITE:
		dprintf("disk fake write\n");
		Irp->IoStatus.Information = IrpSp->Parameters.Write.Length;
		Status = STATUS_SUCCESS;
		break;

	case IRP_MJ_FILE_SYSTEM_CONTROL:
		switch (IrpSp->MinorFunction) {
		case IRP_MN_MOUNT_VOLUME:
			dprintf("IRP_MN_MOUNT_VOLUME disk\n");
			Status = zfs_vnop_mount(DeviceObject, Irp, IrpSp);
			break;
		case IRP_MN_USER_FS_REQUEST:
			dprintf("IRP_MN_USER_FS_REQUEST: FsControlCode 0x%x\n",
				IrpSp->Parameters.FileSystemControl.FsControlCode);
			Status = user_fs_request(DeviceObject, Irp, IrpSp);
			break;
		}
		break;

	case IRP_MJ_QUERY_INFORMATION:
		dprintf("volume calling query_information warning\n");
		Status = query_information(DeviceObject, Irp, IrpSp);
		break;

	case IRP_MJ_PNP:
		switch (IrpSp->MinorFunction) {
		case IRP_MN_QUERY_CAPABILITIES:
			Status = QueryCapabilities(DeviceObject, Irp, IrpSp);
			break;
		case IRP_MN_QUERY_DEVICE_RELATIONS:
			Status = STATUS_NOT_IMPLEMENTED;
			dprintf("DeviceRelations.Type 0x%x\n", IrpSp->Parameters.QueryDeviceRelations.Type);
			break;
		case IRP_MN_QUERY_ID:
			Status = pnp_query_id(DeviceObject, Irp, IrpSp);
			break;
		case IRP_MN_QUERY_PNP_DEVICE_STATE:
			Status = pnp_device_state(DeviceObject, Irp, IrpSp);
			break;
		case IRP_MN_QUERY_REMOVE_DEVICE:
			dprintf("IRP_MN_QUERY_REMOVE_DEVICE\n");
			Status = STATUS_SUCCESS;
			break;
		case IRP_MN_SURPRISE_REMOVAL:
			dprintf("IRP_MN_SURPRISE_REMOVAL\n");
			Status = STATUS_SUCCESS;
			break;
		case IRP_MN_REMOVE_DEVICE:
			dprintf("IRP_MN_REMOVE_DEVICE\n");
			Status = STATUS_SUCCESS;
			break;
		case IRP_MN_CANCEL_REMOVE_DEVICE:
			dprintf("IRP_MN_CANCEL_REMOVE_DEVICE\n");
			Status = STATUS_SUCCESS;
			break;
		}
		break;

	}

	return Status;
}

/*
 * This is the main FileSystem IOCTL handler. This is where the filesystem
 * vnops happen and we handle everything with files and directories in ZFS.
 */
_Function_class_(DRIVER_DISPATCH)
static NTSTATUS
fsDispatcher(
	_In_ PDEVICE_OBJECT DeviceObject,
	_Inout_ PIRP Irp,
	PIO_STACK_LOCATION IrpSp
)
{
	NTSTATUS Status;
	struct vnode *hold_vp = NULL;
	
	PAGED_CODE();

	dprintf("  %s: enter: major %d: minor %d: %s fsDeviceObject\n", __func__, IrpSp->MajorFunction, IrpSp->MinorFunction,
		major2str(IrpSp->MajorFunction, IrpSp->MinorFunction));

#ifdef DEBUG_IOCOUNT
	int skiplock = 0;
	/*
	 * Watch out for re-entrant calls! MJ_READ, can call CCMGR, which calls MJ_READ!
	 * This could be a bigger issue, in that any mutex calls in the zfs_read()/zfs_write stack
	 * could die - including rangelocks, rwlocks. Investigate. (+zfs_freesp/zfs_trunc)
	 */
	if (mutex_owned(&GIANT_SERIAL_LOCK))
		skiplock = 1;
	else
		mutex_enter(&GIANT_SERIAL_LOCK);

	zfsvfs_t *zfsvfs = NULL;
#endif

	/*
	 * Like VFS layer in upstream, we hold the "vp" here before calling into the VNOP handlers.
	 * There is one special case, IRP_MJ_CREATE / zfs_vnop_lookup, which has no vp to start, 
	 * and assigns the vp on success (held).
	 * We also pass "hold_vp" down to delete_entry, so it can release the last hold to delete
	 */
	if (IrpSp->FileObject && IrpSp->FileObject->FsContext) {
		hold_vp = IrpSp->FileObject->FsContext;
		if (VN_HOLD(hold_vp) != 0) {

			// If we were given a vp, but can't hold the vp, we should fail this OP.
			Irp->IoStatus.Information = 0;
			hold_vp = NULL;
			return STATUS_INVALID_PARAMETER;

		} else {

			// Add FO to vp, if this is the first we've heard of it
			vnode_fileobject_add(IrpSp->FileObject->FsContext, IrpSp->FileObject);

			// This is useful if you have iocount leaks, and do
			// only single-threaded operations
#ifdef DEBUG_IOCOUNT
			if (!vnode_isvroot(hold_vp) && vnode_isdir(hold_vp))
				ASSERT(hold_vp->v_iocount == 1);
			zfsvfs = VTOZ(hold_vp)->z_zfsvfs;
#endif
		}
	}
	/* Inside VNOP handlers, we no longer need to call VN_HOLD() on *this* vp
	 * (but might for dvp etc) and eventually that code will be removed, if this
	 * style works out.
	 */

	Status = STATUS_NOT_IMPLEMENTED;

	switch (IrpSp->MajorFunction) {

	case IRP_MJ_CREATE:
		if (IrpSp->Parameters.Create.Options & FILE_OPEN_BY_FILE_ID)
			dprintf("IRP_MJ_CREATE: FileObject %p related %p FileID 0x%llx flags 0x%x sharing 0x%x options 0x%x\n",
				IrpSp->FileObject, IrpSp->FileObject ? IrpSp->FileObject->RelatedFileObject : NULL,
				*((uint64_t *)IrpSp->FileObject->FileName.Buffer), IrpSp->Flags, IrpSp->Parameters.Create.ShareAccess,
				IrpSp->Parameters.Create.Options);
		else
			dprintf("IRP_MJ_CREATE: FileObject %p related %p name '%wZ' flags 0x%x sharing 0x%x options %s attr 0x%x DesAcc 0x%x\n",
				IrpSp->FileObject, IrpSp->FileObject ? IrpSp->FileObject->RelatedFileObject : NULL,
				IrpSp->FileObject->FileName, IrpSp->Flags, IrpSp->Parameters.Create.ShareAccess,
				create_options(IrpSp->Parameters.Create.Options), IrpSp->Parameters.Create.FileAttributes, IrpSp->Parameters.Create.SecurityContext->DesiredAccess);

		Irp->IoStatus.Information = FILE_OPENED;
		Status = STATUS_SUCCESS;

#if 0
		// Disallow autorun.inf for now
		if (IrpSp && IrpSp->FileObject && IrpSp->FileObject->FileName.Buffer &&
			_wcsicmp(IrpSp->FileObject->FileName.Buffer, L"\\autorun.inf") == 0) {
			Irp->IoStatus.Information = FILE_DOES_NOT_EXIST;
			Status = STATUS_OBJECT_NAME_NOT_FOUND;
			break;
		}
#endif

		mount_t *zmo = DeviceObject->DeviceExtension;
		VERIFY(zmo->type == MOUNT_TYPE_VCB);

		//
		//  Check if we are opening the volume and not a file/directory.
		//  We are opening the volume if the name is empty and there
		//  isn't a related file object.  If there is a related file object
		//  then it is the Vcb itself.
		//

		// We have a name, so we are looking for something specific
		// Attempt to find the requested object
		if (IrpSp && IrpSp->FileObject && /* IrpSp->FileObject->FileName.Buffer && */
			zmo) {

			Status = zfs_vnop_lookup(Irp, IrpSp, zmo);

			if (Status == EROFS)
				Status = STATUS_MEDIA_WRITE_PROTECTED;
		}
		break;

		/*
		 * CLEANUP comes before CLOSE. The IFSTEST.EXE on notifications 
		 * require them to arrive at CLEANUP time, and deemed too late
		 * to be sent from CLOSE. It is required we act on DELETE_ON_CLOSE
		 * in CLEANUP, which means we have to call delete here.
		 * fastfat:
		 * Close is invoked whenever the last reference to a file object is deleted.
		 * Cleanup is invoked when the last handle to a file object is closed, and
		 * is called before close.
		 * The function of close is to completely tear down and remove the fcb/dcb/ccb
		 * structures associated with the file object.
		 * So for ZFS, CLEANUP will leave FsContext=vp around - to have it be freed in
		 * CLOSE.
		 */
	case IRP_MJ_CLEANUP: 
		Status = zfs_fileobject_cleanup(DeviceObject, Irp, IrpSp, &hold_vp);
		break;

	case IRP_MJ_CLOSE:
		Status = zfs_fileobject_close(DeviceObject, Irp, IrpSp, &hold_vp);
		break;

	case IRP_MJ_DEVICE_CONTROL:
	{
		u_long cmd = IrpSp->Parameters.DeviceIoControl.IoControlCode;
		/* Not ZFS ioctl, handle Windows ones */
		switch (cmd) {
		case IOCTL_VOLUME_GET_GPT_ATTRIBUTES:
			dprintf("IOCTL_VOLUME_GET_GPT_ATTRIBUTES\n");
			Status = 0;
			break;
		case IOCTL_MOUNTDEV_QUERY_DEVICE_NAME:
			dprintf("IOCTL_MOUNTDEV_QUERY_DEVICE_NAME\n");
			Status = ioctl_query_device_name(DeviceObject, Irp, IrpSp);
			break;
		case IOCTL_MOUNTDEV_QUERY_UNIQUE_ID:
			dprintf("IOCTL_MOUNTDEV_QUERY_UNIQUE_ID\n");
			Status = ioctl_query_unique_id(DeviceObject, Irp, IrpSp);
			break;
		case IOCTL_MOUNTDEV_QUERY_STABLE_GUID:
			dprintf("IOCTL_MOUNTDEV_QUERY_STABLE_GUID\n");
			Status = ioctl_query_stable_guid(DeviceObject, Irp, IrpSp);
			break;
		case IOCTL_MOUNTDEV_QUERY_SUGGESTED_LINK_NAME:
			dprintf("IOCTL_MOUNTDEV_QUERY_SUGGESTED_LINK_NAME\n");
			break;
		case IOCTL_VOLUME_ONLINE:
			dprintf("IOCTL_VOLUME_ONLINE\n");
			Status = STATUS_SUCCESS;
			break;
		case IOCTL_VOLUME_OFFLINE:
			dprintf("IOCTL_VOLUME_OFFLINE\n");
			Status = STATUS_SUCCESS;
			break;
		case IOCTL_DISK_IS_WRITABLE:
			dprintf("IOCTL_DISK_IS_WRITABLE\n");
			Status = STATUS_SUCCESS;
			break;
		case IOCTL_DISK_MEDIA_REMOVAL:
			dprintf("IOCTL_DISK_MEDIA_REMOVAL\n");
			Status = STATUS_SUCCESS;
			break;
		case IOCTL_STORAGE_MEDIA_REMOVAL:
			dprintf("IOCTL_STORAGE_MEDIA_REMOVAL\n");
			Status = STATUS_SUCCESS;
			break;
		case IOCTL_VOLUME_POST_ONLINE:
			dprintf("IOCTL_VOLUME_POST_ONLINE\n");
			Status = STATUS_SUCCESS;
			break;
		case IOCTL_STORAGE_CHECK_VERIFY:
			dprintf("IOCTL_STORAGE_CHECK_VERIFY\n");
			Status = STATUS_SUCCESS;
			break;
		case IOCTL_DISK_GET_DRIVE_GEOMETRY:
			dprintf("IOCTL_DISK_GET_DRIVE_GEOMETRY\n");
			Status = ioctl_disk_get_drive_geometry(DeviceObject, Irp, IrpSp);
			break;
		case IOCTL_DISK_GET_DRIVE_GEOMETRY_EX:
			dprintf("IOCTL_DISK_GET_DRIVE_GEOMETRY_EX\n");
			Status = ioctl_disk_get_drive_geometry_ex(DeviceObject, Irp, IrpSp);
			break;
		case IOCTL_DISK_GET_PARTITION_INFO:
			dprintf("IOCTL_DISK_GET_PARTITION_INFO\n");
			Status = ioctl_disk_get_partition_info(DeviceObject, Irp, IrpSp);
			break;
		case IOCTL_DISK_GET_PARTITION_INFO_EX:
			dprintf("IOCTL_DISK_GET_PARTITION_INFO_EX\n");
			Status = ioctl_disk_get_partition_info_ex(DeviceObject, Irp, IrpSp);
			break;
		case IOCTL_VOLUME_IS_IO_CAPABLE:
			dprintf("IOCTL_VOLUME_IS_IO_CAPABLE\n");
			Status = ioctl_volume_is_io_capable(DeviceObject, Irp, IrpSp);
			break;
		case IOCTL_STORAGE_GET_HOTPLUG_INFO:
			dprintf("IOCTL_STORAGE_GET_HOTPLUG_INFO\n");
			Status = ioctl_storage_get_hotplug_info(DeviceObject, Irp, IrpSp);
			break;
		case IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS:
			dprintf("IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS\n");
			Status = ioctl_volume_get_volume_disk_extents(DeviceObject, Irp, IrpSp);
			break;
		case IOCTL_DISK_GET_LENGTH_INFO:
			dprintf("IOCTL_DISK_GET_LENGTH_INFO\n");
			Status = ioctl_disk_get_length_info(DeviceObject, Irp, IrpSp);
			break;
		case IOCTL_STORAGE_GET_DEVICE_NUMBER:
			dprintf("IOCTL_STORAGE_GET_DEVICE_NUMBER\n");
			Status = ioctl_storage_get_device_number(DeviceObject, Irp, IrpSp);
			break;
		case IOCTL_STORAGE_QUERY_PROPERTY:
			dprintf("IOCTL_STORAGE_QUERY_PROPERTY\n");
			Status = ioctl_storage_query_property(DeviceObject, Irp, IrpSp);
			break;
		default:
			dprintf("**** unknown fsWindows IOCTL: 0x%lx\n", cmd);
		}

	}
	break;

	case IRP_MJ_FILE_SYSTEM_CONTROL:
		switch (IrpSp->MinorFunction) {
		case IRP_MN_MOUNT_VOLUME:
			dprintf("IRP_MN_MOUNT_VOLUME fs\n");
			Status = zfs_vnop_mount(DeviceObject, Irp, IrpSp);
			break;
		case IRP_MN_USER_FS_REQUEST:
			Status = user_fs_request(DeviceObject, Irp, IrpSp);
			break;
		case IRP_MN_KERNEL_CALL: // FSCTL_QUERY_VOLUME_CONTAINER_STATE 0x90930
			dprintf("IRP_MN_KERNEL_CALL: unknown 0x%x\n", IrpSp->Parameters.FileSystemControl.FsControlCode);
			Status = STATUS_INVALID_DEVICE_REQUEST;
			break;
		default:
			dprintf("IRP_MJ_FILE_SYSTEM_CONTROL: unknown 0x%x\n", IrpSp->MinorFunction);
			Status = STATUS_INVALID_DEVICE_REQUEST;
		}
		break;

	case IRP_MJ_PNP:
		switch (IrpSp->MinorFunction) {
		case IRP_MN_QUERY_CAPABILITIES:
			Status = QueryCapabilities(DeviceObject, Irp, IrpSp);
			break;
		case IRP_MN_QUERY_DEVICE_RELATIONS:
			Status = STATUS_NOT_IMPLEMENTED;

			if (IrpSp->Parameters.QueryDeviceRelations.Type == TargetDeviceRelation) {
				PDEVICE_RELATIONS DeviceRelations;
				DeviceRelations =
					(PDEVICE_RELATIONS)ExAllocatePool(PagedPool,
						sizeof(DEVICE_RELATIONS));
				if (!DeviceRelations) {
					dprintf("  can't allocate DeviceRelations\n");
					Status = STATUS_INSUFFICIENT_RESOURCES;
					break;
				}

				dprintf("DeviceRelations TargetDeviceRelations\n");

				/* The PnP manager will remove this when it is done with device */
				ObReferenceObject(DeviceObject);

				DeviceRelations->Count = 1;
				DeviceRelations->Objects[0] = DeviceObject;
				Irp->IoStatus.Information = (ULONG_PTR)DeviceRelations;

				Status = STATUS_SUCCESS;
				break;
			}

			dprintf("DeviceRelations.Type 0x%x\n", IrpSp->Parameters.QueryDeviceRelations.Type);
			break; 
		case IRP_MN_QUERY_ID:
			Status = pnp_query_id(DeviceObject, Irp, IrpSp);
			break;
		case IRP_MN_QUERY_PNP_DEVICE_STATE:
			Status = pnp_device_state(DeviceObject, Irp, IrpSp);
			break;
		case IRP_MN_QUERY_REMOVE_DEVICE:
			dprintf("IRP_MN_QUERY_REMOVE_DEVICE\n");
			Status = STATUS_SUCCESS;
			break;
		case IRP_MN_SURPRISE_REMOVAL:
			dprintf("IRP_MN_SURPRISE_REMOVAL\n");
			Status = STATUS_SUCCESS;
			break;
		case IRP_MN_REMOVE_DEVICE:
			dprintf("IRP_MN_REMOVE_DEVICE\n");
			Status = STATUS_SUCCESS;
			break;
		case IRP_MN_CANCEL_REMOVE_DEVICE:
			dprintf("IRP_MN_CANCEL_REMOVE_DEVICE\n");
			Status = STATUS_SUCCESS;
			break;
		}
		break;
	case IRP_MJ_QUERY_VOLUME_INFORMATION:
		Status = query_volume_information(DeviceObject, Irp, IrpSp);
		break;

	case IRP_MJ_LOCK_CONTROL:
		Status = lock_control(DeviceObject, Irp, IrpSp);
		break;

	case IRP_MJ_QUERY_INFORMATION:
		Status = query_information(DeviceObject, Irp, IrpSp);
		break;

	case IRP_MJ_DIRECTORY_CONTROL:
		switch (IrpSp->MinorFunction) {
		case IRP_MN_NOTIFY_CHANGE_DIRECTORY:
			Status = notify_change_directory(DeviceObject, Irp, IrpSp);
			break;
		case IRP_MN_QUERY_DIRECTORY:
			Status = query_directory(DeviceObject, Irp, IrpSp);
			break;
		}
		break;
	case IRP_MJ_SET_INFORMATION:
		Status = set_information(DeviceObject, Irp, IrpSp);
		break;
	case IRP_MJ_READ:
		Status = fs_read(DeviceObject, Irp, IrpSp);
		break;
	case IRP_MJ_WRITE:
		Status = fs_write(DeviceObject, Irp, IrpSp);
		break;
	case IRP_MJ_FLUSH_BUFFERS:
		Status = flush_buffers(DeviceObject, Irp, IrpSp);
		break;
	case IRP_MJ_QUERY_SECURITY:
		Status = query_security(DeviceObject, Irp, IrpSp);
		break;
	case IRP_MJ_SET_SECURITY:
		Status = set_security(DeviceObject, Irp, IrpSp);
		break;
	case IRP_MJ_QUERY_EA:
		Status = query_ea(DeviceObject, Irp, IrpSp);
		break;
	case IRP_MJ_SET_EA:
		Status = set_ea(DeviceObject, Irp, IrpSp);
		break;
	case IRP_MJ_SHUTDOWN:
		dprintf("IRP_MJ_SHUTDOWN\n");
		Status = STATUS_SUCCESS;
		break;
	}

	/* Re-check (since MJ_CREATE/vnop_lookup might have set it) vp here, to see if
	 * we should call setsize 
	 */
	if (IrpSp->FileObject && IrpSp->FileObject->FsContext) {
		struct vnode *vp = IrpSp->FileObject->FsContext;

		/* vp "might" be held above, or not (vnop_lookup) so grab another just in case */
		if (vp && vnode_sizechange(vp) &&
			VN_HOLD(vp) == 0) {
			if (CcIsFileCached(IrpSp->FileObject)) {
				CcSetFileSizes(IrpSp->FileObject, (PCC_FILE_SIZES)&vp->FileHeader.AllocationSize);
				dprintf("sizechanged, updated to %llx\n", vp->FileHeader.FileSize);
				vnode_setsizechange(vp, 0);
			}
			VN_RELE(vp);
		}
	}

	/* If we held the vp above, release it now. */
	if (hold_vp != NULL) {
		VN_RELE(hold_vp);
	}

#ifdef DEBUG_IOCOUNT
	// Since we have serialised all fsdispatch() calls, and we are
	// about to leave - all iocounts should be zero, check that is true.
	// ZFSCallbackAcquireForCreateSection() can give false positives, as
	// they are called async, outside IRP dispatcher.
	if (!skiplock) {
		// Wait for all async_rele to finish
		if (zfsvfs) taskq_wait(dsl_pool_vnrele_taskq(dmu_objset_pool(zfsvfs->z_os)));
		vnode_check_iocount();
		mutex_exit(&GIANT_SERIAL_LOCK);
	}
#endif

	return Status;
}

/*
 * ALL ioctl requests come in here, and we do the Windows specific work to handle IRPs
 * then we sort out the type of request (ioctl, volume, filesystem) and call each
 * respective handler.
 */
_Function_class_(DRIVER_DISPATCH)
NTSTATUS
dispatcher(
	_In_ PDEVICE_OBJECT DeviceObject,
	_Inout_ PIRP Irp
)
{
	BOOLEAN TopLevel = FALSE;
	BOOLEAN AtIrqlPassiveLevel;
	PIO_STACK_LOCATION IrpSp;
	NTSTATUS Status = STATUS_NOT_IMPLEMENTED;
	uint64_t validity_check;

	// Storport can call itself (and hence, ourselves) so this isn't always true.
	//PAGED_CODE();

	//dprintf("%s: enter\n", __func__);

	//  If we were called with our file system device object instead of a
	//  volume device object, just complete this request with STATUS_SUCCESS
#if 0
	if (vnop_deviceObject == VolumeDeviceObject) {
		dprintf("%s: own object\n", __func__);
		Irp->IoStatus.Status = STATUS_SUCCESS;
		Irp->IoStatus.Information = FILE_OPENED;
		IoCompleteRequest(Irp, IO_DISK_INCREMENT);
		return STATUS_SUCCESS;
	}
#endif
	validity_check = *((uint64_t *)Irp);
	IrpSp = IoGetCurrentIrpStackLocation(Irp);

	dprintf("%s: enter: major %d: minor %d: %s: type 0x%x\n", __func__, IrpSp->MajorFunction, IrpSp->MinorFunction,
		major2str(IrpSp->MajorFunction, IrpSp->MinorFunction), Irp->Type);

	AtIrqlPassiveLevel = (KeGetCurrentIrql() == PASSIVE_LEVEL);
	if (AtIrqlPassiveLevel) {
		FsRtlEnterFileSystem();
	}
	if (IoGetTopLevelIrp() == NULL) {
		IoSetTopLevelIrp(Irp);
		TopLevel = TRUE;
	}

	if (DeviceObject == ioctlDeviceObject)
		Status = ioctlDispatcher(DeviceObject, Irp, IrpSp);
	else {
		mount_t * zmo = DeviceObject->DeviceExtension;
		if (zmo && zmo->type == MOUNT_TYPE_DCB)
			Status = diskDispatcher(DeviceObject, Irp, IrpSp);
		else if (zmo && zmo->type == MOUNT_TYPE_VCB)
			Status = fsDispatcher(DeviceObject, Irp, IrpSp);
		else {
			extern PDRIVER_DISPATCH STOR_MajorFunction[IRP_MJ_MAXIMUM_FUNCTION + 1];
			if (STOR_MajorFunction[IrpSp->MajorFunction] != NULL) {
				if (TopLevel) {
					IoSetTopLevelIrp(NULL);
				}
				if (AtIrqlPassiveLevel) {
					FsRtlExitFileSystem();
				}
				//dprintf("Relaying IRP to STORport\n");
				return STOR_MajorFunction[IrpSp->MajorFunction](DeviceObject, Irp);
			}

			// Got a request we don't care about?
			Status = STATUS_INVALID_DEVICE_REQUEST;
			Irp->IoStatus.Information = 0;
		}
	}

	if (AtIrqlPassiveLevel) {
		FsRtlExitFileSystem();
	}
	if (TopLevel) {
		IoSetTopLevelIrp(NULL);
	}

	switch (Status) {
	case STATUS_SUCCESS:
	case STATUS_BUFFER_OVERFLOW:
	case STATUS_PENDING:
		break;
	default:
		ASSERT(validity_check == *((uint64_t *)Irp));
		dprintf("%s: exit: 0x%x %s Information 0x%x : %s\n", __func__, Status,
			common_status_str(Status),
			Irp->IoStatus.Information, major2str(IrpSp->MajorFunction, IrpSp->MinorFunction));
	}

	// Complete the request if it isn't pending (ie, we called zfsdev_async())
	if (Status != STATUS_PENDING)
	{
		if (validity_check == *((uint64_t *)Irp)) {
			// IOCTL_STORAGE_GET_HOTPLUG_INFO
			// IOCTL_DISK_CHECK_VERIFY
			// IOCTL_STORAGE_QUERY_PROPERTY
			Irp->IoStatus.Status = Status;
			IoCompleteRequest(Irp, Status == STATUS_SUCCESS ? IO_DISK_INCREMENT : IO_NO_INCREMENT);
		}
		else
			KeBugCheckEx(INCONSISTENT_IRP, (ULONG_PTR)Irp, 0, 0, 0);
	}
	return Status;
}

NTSTATUS ZFSCallbackAcquireForCreateSection(
	IN PFS_FILTER_CALLBACK_DATA CallbackData,
	OUT PVOID *CompletionContext
)
/*++
Routine Description:
This is the callback routine for MM to use to acquire the file exclusively.
NOTE:  This routine expects the default FSRTL routine to be used to release
the resource.  If this routine is ever changed to acquire something
other than main, a corresponding release routine will be required.
Arguments:
FS_FILTER_CALLBACK_DATA - Filter based callback data that provides the file object we
want to acquire.
CompletionContext - Ignored.
Return Value:
On success we return STATUS_FSFILTER_OP_COMPLETED_SUCCESSFULLY.
If SyncType is SyncTypeCreateSection, we return a status that indicates whether there
are any writers to this file.  Note that main is acquired, so new handles cannot be opened.
--*/
{
	//ASSERT(CallbackData->Operation == FS_FILTER_ACQUIRE_FOR_SECTION_SYNCHRONIZATION);
	ASSERT(CallbackData->SizeOfFsFilterCallbackData == sizeof(FS_FILTER_CALLBACK_DATA));

	dprintf("%s: Operation 0x%x \n", __func__, 
		CallbackData->Operation);

	struct vnode *vp;
	vp = CallbackData->FileObject->FsContext;

	ASSERT(vp != NULL);
	if (vp == NULL) return STATUS_INVALID_PARAMETER;

#ifdef DEBUG_IOCOUNT
	int nolock = 0;
	if (mutex_owned(&GIANT_SERIAL_LOCK))
		nolock = 1;
	else
		mutex_enter(&GIANT_SERIAL_LOCK);
#endif
	if (VN_HOLD(vp) == 0) {
		dprintf("%s: locked: %p\n", __func__, vp->FileHeader.Resource);
		ExAcquireResourceExclusiveLite(vp->FileHeader.Resource, TRUE);
		vnode_ref(vp);
		VN_RELE(vp);
	} else {
#ifdef DEBUG_IOCOUNT
		if (!nolock)
			mutex_exit(&GIANT_SERIAL_LOCK);
#endif
		return STATUS_INVALID_PARAMETER;
	}
#ifdef DEBUG_IOCOUNT
	if (!nolock)
		mutex_exit(&GIANT_SERIAL_LOCK);
#endif

	if (CallbackData->Parameters.AcquireForSectionSynchronization.SyncType != SyncTypeCreateSection) {

		return STATUS_FSFILTER_OP_COMPLETED_SUCCESSFULLY;

	} else if (vp->share_access.Writers == 0) {

		return STATUS_FILE_LOCKED_WITH_ONLY_READERS;

	} else {

		return STATUS_FILE_LOCKED_WITH_WRITERS;
	}

}

NTSTATUS ZFSCallbackReleaseForCreateSection(
	IN PFS_FILTER_CALLBACK_DATA CallbackData,
	OUT PVOID *CompletionContext
)
{
	struct vnode *vp;
	vp = CallbackData->FileObject->FsContext;

	dprintf("%s: vp %p\n", __func__, vp);

	ASSERT(vp != NULL);
	if (vp == NULL) return STATUS_INVALID_PARAMETER;

	if (vp->FileHeader.Resource) {
		dprintf("%s: unlocked: %p\n", __func__, vp->FileHeader.Resource);
		ExReleaseResourceLite(vp->FileHeader.Resource);
#ifdef DEBUG_IOCOUNT
		int nolock = 0;
		if (mutex_owned(&GIANT_SERIAL_LOCK))
			nolock = 1;
		else
			mutex_enter(&GIANT_SERIAL_LOCK);
#endif
		if (VN_HOLD(vp) == 0) {
			vnode_rele(vp);
			VN_RELE(vp);
		}
#ifdef DEBUG_IOCOUNT
		if (!nolock)
			mutex_exit(&GIANT_SERIAL_LOCK);
#endif
	}

	return STATUS_FSFILTER_OP_COMPLETED_SUCCESSFULLY;
}

void zfs_windows_vnops_callback(PDEVICE_OBJECT deviceObject)
{

}

int
zfs_vfsops_init(void)
{

#ifdef DEBUG_IOCOUNT
	mutex_init(&GIANT_SERIAL_LOCK, NULL, MUTEX_DEFAULT, NULL);
#endif
	return 0;
}

int
zfs_vfsops_fini(void)
{

#ifdef DEBUG_IOCOUNT
	mutex_destroy(&GIANT_SERIAL_LOCK);
#endif
	return 0;
}
