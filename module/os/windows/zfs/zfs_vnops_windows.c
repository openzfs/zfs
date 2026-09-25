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
#include <os/windows/zfs/sys/zfs_ioctl_compat.h>
#include <sys/fs/zfsdi.h>
#include <sys/driver_extension.h>

// I have no idea what black magic is needed to get ntifs.h to define these

#ifndef FsRtlEnterFileSystem
#define	FsRtlEnterFileSystem() { \
	KeEnterCriticalRegion();     \
}
#endif
#ifndef FsRtlExitFileSystem
#define	FsRtlExitFileSystem() { \
    KeLeaveCriticalRegion();     \
}
#endif

#include <sys/cred.h>
#include <sys/vnode.h>
#include <sys/zfs_dir.h>
#include <sys/zfs_ioctl.h>
#include <sys/zfs_ioctl_compat.h>
#include <sys/fs/zfs.h>
#include <sys/dmu.h>
#include <sys/dmu_objset.h>
#include <sys/spa.h>
#include <sys/spa_impl.h>
#include <sys/txg.h>
#include <sys/dbuf.h>
#include <sys/zap.h>
#include <sys/sa.h>
#include <sys/zfs_vnops.h>
#include <sys/zfs_vnops_os.h>
#include <sys/zfs_acl_impl.h>
#include <sys/vfs.h>
#include <sys/vfs_opreg.h>
#include <sys/zfs_vfsops.h>
#include <sys/zfs_rlock.h>
#include <sys/zfs_ctldir.h>

#include <sys/callb.h>
#include <sys/unistd.h>
#include <sys/zfs_windows.h>
#include <sys/kstat.h>
#include <sys/zfs_vss.h>
#include <sys/dsl_pool.h>
#include <sys/dsl_dataset.h>

#ifdef DEBUG_IOCOUNT
static kmutex_t GIANT_SERIAL_LOCK;
#endif

#ifdef _KERNEL

#ifndef STATUS_VOLUME_NOT_MOUNTED
#define	STATUS_VOLUME_NOT_MOUNTED 0xC000001A
#endif

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

// vnode_t *hackvp = NULL;

#ifdef _KERNEL
uint64_t vnop_num_reclaims = 0;
uint64_t vnop_num_vnodes = 0;
uint64_t zfs_disable_wincache = 0;

ZFS_MODULE_RAW(zfs, disable_wincache, zfs_disable_wincache,
    U64, ZMOD_RW, 0, "Disable OS caching.");
#endif

extern void UnlockAndFreeMdl(PMDL);


void CcSetAdditionalCacheAttributesEx(
	[in] PFILE_OBJECT FileObject,
	[in] ULONG Flags
);
void __stdcall PsUpdateDiskCounters(PEPROCESS Process,
    ULONG64 BytesRead, ULONG64 BytesWritten,
    ULONG ReadOperationCount, ULONG WriteOperationCount,
    ULONG FlushOperationCount);

BOOLEAN
zfs_AcquireForLazyWrite(void *Context, BOOLEAN Wait)
{
	FILE_OBJECT *fo = Context;
	BOOLEAN result = FALSE;

	dprintf("%s:fo %p\n", __func__, fo);

	if (fo == NULL)
		return (FALSE);

	mount_t *zmo = fo->DeviceObject->DeviceExtension;
	zfsvfs_t *zfsvfs = vfs_fsprivate(zmo);
	struct vnode *vp = fo->FsContext;

	if (unlikely(zfsvfs == NULL)) {
		dprintf("%s: fo %p already freed zfsvfs\n", __func__, fo);
		return (FALSE);
	}

	/* Confirm we are mounted, and stop unmounting */
	if (vfs_busy(zfsvfs->z_vfs, 0) != 0)
		return (FALSE);

	if (zfsvfs->z_unmounted ||
	    zfs_enter(zfsvfs, FTAG) != 0) {
		vfs_unbusy(zfsvfs->z_vfs);
		return (FALSE);
	}

	vfs_unbusy(zfsvfs->z_vfs);

	if (vp == NULL ||
	    VTOZ(vp) == NULL ||
	    VN_HOLD(vp) != 0) {
		zfs_exit(zfsvfs, FTAG);
		return (FALSE);
	}
	zfs_exit(zfsvfs, FTAG);

	/*
	 * POSIX semantics: rename and delete are non-destructive to
	 * concurrent writers (the inode outlives the directory entry).
	 * The write synchronisation we need — serialising against
	 * truncations — is already provided by PagingIoResource, which
	 * zfs_write_wrap acquires EXCLUSIVE for every paging write.
	 * Holding MainResource SHARED here would cause every IRP_MJ_CREATE
	 * oplock preflight (which needs MainResource EXCLUSIVE per FsRtl
	 * protocol) to block for the full duration of the lazy write,
	 * including any range-lock or ARC wait inside it.  Vnode lifetime
	 * is protected by vnode_ref below, not by MainResource.
	 */
	vnode_ref(vp);
	result = TRUE;
	IoSetTopLevelIrp((PIRP)FSRTL_CACHE_TOP_LEVEL_IRP);

out:
	VN_RELE(vp);

	dprintf("vpb %s %lu\n", __func__,
	    zmo && zmo->vpb ? zmo->vpb->ReferenceCount : -1);


	return (result);
}

void
zfs_ReleaseFromLazyWrite(void *Context)
{
	FILE_OBJECT *fo = Context;

	dprintf("%s:\n", __func__);
	if (fo == NULL)
		return;

	mount_t *zmo = fo->DeviceObject->DeviceExtension;
	zfsvfs_t *zfsvfs = vfs_fsprivate(zmo);
	struct vnode *vp = fo->FsContext;

	dprintf("vpb %s %lu\n", __func__,
	    zmo && zmo->vpb ? zmo->vpb->ReferenceCount : -1);

	if (vp != NULL && VN_HOLD(vp) == 0) {
		vnode_rele(vp);
		VN_RELE(vp);
		if (IoGetTopLevelIrp() ==
		    (PIRP)FSRTL_CACHE_TOP_LEVEL_IRP)
			IoSetTopLevelIrp(NULL);

		return;
	}
	dprintf("%s WARNING FAILED\n", __func__);
}

BOOLEAN
zfs_AcquireForReadAhead(void *Context, BOOLEAN Wait)
{
	FILE_OBJECT *fo = Context;
	BOOLEAN result = FALSE;

	dprintf("%s:\n", __func__);

	if (fo == NULL)
		return (FALSE);

	mount_t *zmo = fo->DeviceObject->DeviceExtension;
	zfsvfs_t *zfsvfs = vfs_fsprivate(zmo);
	struct vnode *vp = fo->FsContext;
	dprintf("vpb %s %lu\n", __func__,
	    zmo && zmo->vpb ? zmo->vpb->ReferenceCount : -1);

	if (unlikely(zfsvfs == NULL)) {
		dprintf("%s: fo %p already freed zfsvfs\n", __func__, fo);
		return (FALSE);
	}

	if (vfs_busy(zfsvfs->z_vfs, 0) != 0)
		return (FALSE);

	if (zfsvfs->z_unmounted ||
	    zfs_enter(zfsvfs, FTAG) != 0) {
		vfs_unbusy(zfsvfs->z_vfs);
		return (FALSE);
	}

	vfs_unbusy(zfsvfs->z_vfs);

	if (vp == NULL ||
	    VTOZ(vp) == NULL ||
	    VN_HOLD(vp) != 0) {
		zfs_exit(zfsvfs, FTAG);
		return (FALSE);
	}
	zfs_exit(zfsvfs, FTAG);

	/* Same reasoning as AcquireForLazyWrite: no MainResource needed. */
	vnode_ref(vp);
	IoSetTopLevelIrp((PIRP)FSRTL_CACHE_TOP_LEVEL_IRP);
	result = TRUE;

out:
	VN_RELE(vp);

	return (result);
}

void
zfs_ReleaseFromReadAhead(void *Context)
{
	FILE_OBJECT *fo = Context;

	dprintf("%s:\n", __func__);
	if (fo == NULL)
		return;

	mount_t *zmo = fo->DeviceObject->DeviceExtension;
	// zfsvfs_t *zfsvfs = vfs_fsprivate(zmo);
	dprintf("vpb %s %lu\n", __func__,
	    zmo && zmo->vpb ? zmo->vpb->ReferenceCount : -1);

	struct vnode *vp = fo->FsContext;

	if (vp != NULL && VN_HOLD(vp) == 0) {
		vnode_rele(vp);
		VN_RELE(vp);
		if (IoGetTopLevelIrp() ==
		    (PIRP)FSRTL_CACHE_TOP_LEVEL_IRP)
			IoSetTopLevelIrp(NULL);

		return;
	}
	dprintf("%s WARNING FAILED\n", __func__);
}

CACHE_MANAGER_CALLBACKS CacheManagerCallbacks =
{
	.AcquireForLazyWrite = zfs_AcquireForLazyWrite,
	.ReleaseFromLazyWrite = zfs_ReleaseFromLazyWrite,
	.AcquireForReadAhead = zfs_AcquireForReadAhead,
	.ReleaseFromReadAhead = zfs_ReleaseFromReadAhead
};

int
zfs_init_cache(FILE_OBJECT *fo, struct vnode *vp, CC_FILE_SIZES *ccfs)
{
	zfs_ccb_t *zccb = fo->FsContext2;


	if (fo->Flags & FO_VOLUME_OPEN)
		return (0);

	try {
		if (fo->PrivateCacheMap == NULL) {
			if (atomic_cas_64(&zccb->cacheinit, 0, 1) == 0) {
				/*
				 * We won the race: MainResource is held SHARED
				 * by all concurrent writers so multiple threads
				 * can reach here simultaneously.  Only one must
				 * call CcInitializeCacheMap.
				 */
				CcInitializeCacheMap(fo,
				    ccfs,
				    FALSE,
				    &CacheManagerCallbacks, fo);
				dprintf("CcInitializeCacheMap called on vp"
				    " %p\n", vp);
				CcSetAdditionalCacheAttributesEx(fo,
				    CC_ENABLE_DISK_IO_ACCOUNTING);
				dprintf("%s: CcInitializeCacheMap\n", __func__);
			} else {
				/*
				 * Another thread is initializing the cache.
				 * Spin until CcInitializeCacheMap completes
				 * and PrivateCacheMap is set; CcCopyWrite
				 * requires it to be non-NULL.
				 */
				while (fo->PrivateCacheMap == NULL)
					kpreempt(KPREEMPT_SYNC);
			}
		}
	} except(EXCEPTION_EXECUTE_HANDLER) {
		return (GetExceptionCode());
	}

	return (0);
}


/*
 * zfs vfs operations.
 */

/*
 * FileObject->FsContext will point to vnode, many FileObjects can point
 * to same vnode.
 * FileObject->FsContext2 will point to own "zfs_ccb_t" and be unique
 * to each FileObject.
 * - which could also be done with TSD data, but this appears to be
 * the Windows norm.
 */
void
zfs_couplefileobject(vnode_t *vp, vnode_t *dvp, FILE_OBJECT *fileobject,
    uint64_t size, zfs_ccb_t **ccb, uint64_t alloc, ACCESS_MASK access,
    char *stream, PIRP Irp)
{
	zfs_ccb_t *zccb;
	znode_t *zp = NULL;
	if (VTOZ(vp) != NULL)
		zp = VTOZ(vp);

	if (fileobject->FsContext2 == NULL) {
		zccb = kmem_zalloc(sizeof (zfs_ccb_t), KM_SLEEP);
		zccb->magic = ZFS_CCB_MAGIC;
		spl_fill_cred_from_irp(&zccb->cred, Irp);
		fileobject->FsContext2 = zccb;
	} else {
		zccb = fileobject->FsContext2;
	}
	zccb->access = access;

	if (ccb != NULL)
		*ccb = zccb;

	vnode_ref(vp);
	vnode_couplefileobject(vp, fileobject, size);
	if (!(fileobject->Flags & FO_VOLUME_OPEN))
		fileobject->SectionObjectPointer =
		    vnode_sectionpointer(vp);

	if (dvp)
		vnode_setparent(vp, dvp);

	uint64_t s = 0ULL;
	uint64_t a = 0ULL;
	if (zp != NULL) {
		s = zp->z_size;
		a = P2ROUNDUP(zp->z_size, zp->z_blksz);
	}

	/*
	 * Serialise against fastio_write/zfs_write_wrap/
	 * set_file_endoffile_information, which all acquire PagingIoResource
	 * before relying on FileHeader.FileSize/AllocationSize/
	 * ValidDataLength being stable, or before changing them in lockstep
	 * with CcSetFileSizes.
	 */
	ExAcquireResourceExclusiveLite(vp->FileHeader.PagingIoResource, TRUE);

	/*
	 * Never decrease FileSize or VDL on an open.  zp->z_size reflects
	 * committed (paged-out) data while FileSize tracks the full extent
	 * that the cache manager knows about.  Clamping to zp->z_size would
	 * shrink FileSize below cached dirty pages, causing the next paging
	 * flush to fire changed_length=TRUE and produce a spurious extension.
	 *
	 * Conversely, only *grow* FileSize/AllocationSize here if Cc has not
	 * yet established a cache map for this vnode's shared section
	 * (SharedCacheMap == NULL, i.e. this is the first open).  If a cache
	 * map already exists -- set up by some other, already-open
	 * FileObject sharing this same SectionObjectPointer -- growing these
	 * fields without also calling CcSetFileSizes on the FileObject(s)
	 * that own that cache map would leave Cc's VACB/section bookkeeping
	 * smaller than what we now claim.  A later fastio_write/CcCopyWrite
	 * trusting the enlarged bound then asks Cc to map a region it
	 * doesn't think the section covers, crashing MmMapViewInSystemCache
	 * (BSOD MEMORY_MANAGEMENT, subtype 0x103087).  zfs_write_wrap grows
	 * these fields correctly, in lockstep with CcSetFileSizes, whenever
	 * an actual write needs to -- safe to just defer to that here.
	 */
	uint64_t cur_fs = (uint64_t)vp->FileHeader.FileSize.QuadPart;
	uint64_t new_fs = cur_fs;
	if (s > cur_fs && vp->SectionObjectPointers.SharedCacheMap == NULL)
		new_fs = s;
	uint64_t new_alloc = alloc ? alloc : a;
	if (new_alloc < new_fs)
		new_alloc = new_fs;
	vp->FileHeader.AllocationSize.QuadPart = new_alloc;
	vp->FileHeader.FileSize.QuadPart = new_fs;
	vp->FileHeader.ValidDataLength.QuadPart = new_fs;

	ExReleaseResourceLite(vp->FileHeader.PagingIoResource);

#ifdef ZFS_HAVE_FASTIO
	vp->FileHeader.IsFastIoPossible = fast_io_possible(vp);
#endif

	// When xattr, fetch grandparent instead, the owner of the
	// xattr dir.
	if (zp != NULL && dvp != NULL &&
	    (zp->z_pflags & ZFS_XATTR)) {
		znode_t *dzp;

		zccb->real_file_id = VTOZ(dvp)->z_xattr_parent;

		int error = zfs_zget(zp->z_zfsvfs, zccb->real_file_id, &dzp);
		if (!error) {
			// Build from gparent, ie the filename,
			// after it appends stream name.
			zfs_build_path_stream(dzp, NULL,
			    &zccb->z_name_cache,
			    &zccb->z_name_len,
			    &zccb->z_name_offset, stream);
			zrele(dzp);
		}
	} else {

		zfs_build_path_stream(VTOZ(vp), dvp ? VTOZ(dvp) : NULL,
		    &zccb->z_name_cache,
		    &zccb->z_name_len,
		    &zccb->z_name_offset,
		    stream);

		/*
		 * For hardlinks (z_links > 1), zfs_build_path resolves the
		 * inode's primary name via ZAP lookup, which may differ from
		 * the name the caller used to open this file.  Override
		 * z_name_cache with the verbatim FileObject->FileName so that
		 * FileNameInformation, FileNormalizedNameInformation, and
		 * directory-change notifications all reflect the opened link
		 * name rather than an arbitrary alternate name for the inode.
		 *
		 * Streams are excluded: stream opens use a synthesised name
		 * and the caller-supplied FileObject->FileName includes the
		 * ":stream:$DATA" suffix, which needs separate handling.
		 *
		 * FILE_OPEN_BY_FILE_ID opens are excluded: FileName.Buffer
		 * contains raw inode bytes, not a path.  The leading-backslash
		 * check catches this since real paths always start with L'\\'.
		 */
		if (zp != NULL && zp->z_links > 1 && stream == NULL &&
		    fileobject->FileName.Buffer != NULL &&
		    fileobject->FileName.Length >= sizeof (WCHAR) &&
		    fileobject->FileName.Buffer[0] == L'\\') {
			ULONG bytes_needed =
			    (fileobject->FileName.Length / sizeof (WCHAR))
			    * 4 + 1;
			char *fo_name = kmem_alloc(bytes_needed, KM_SLEEP);
			ULONG fo_len = 0;
			NTSTATUS ns = RtlUnicodeToUTF8N(fo_name,
			    bytes_needed - 1, &fo_len,
			    fileobject->FileName.Buffer,
			    fileobject->FileName.Length);
			if (NT_SUCCESS(ns) || ns == STATUS_SOME_NOT_MAPPED) {
				fo_name[fo_len] = '\0';
				if (zccb->z_name_cache != NULL)
					kmem_free(zccb->z_name_cache,
					    zccb->z_name_len);
				zccb->z_name_cache = fo_name;
				zccb->z_name_len = bytes_needed;
				/* offset past the last backslash */
				char *last_bs = strrchr(fo_name, '\\');
				zccb->z_name_offset = last_bs ?
				    (uint32_t)(last_bs - fo_name + 1) : 0;
			} else {
				kmem_free(fo_name, bytes_needed);
			}
		}
	}

	// Debug, remember what Vpb we returned
	mount_t *zmo = vnode_mount(vp);
	fileobject->Vpb = zmo->vpb ? zmo->vpb : fileobject->DeviceObject->Vpb;
	// fileobject->Vpb = fileobject->DeviceObject->Vpb;
	dprintf("FO %p zmo %wZ Vpb %p Volume %S: %s\n",
	    fileobject, &zmo->name, fileobject->Vpb,
	    fileobject->Vpb ? fileobject->Vpb->VolumeLabel : L"",
	    zccb->z_name_cache);

	if (zmo->vpb)
		VERIFY3U(zmo->vpb->ReferenceCount, >, 0);
}

void
zfs_decouplefileobject(vnode_t *vp, FILE_OBJECT *fileobject)
{
	// We release FsContext2 at CLEANUP, but fastfat releases it in
	// CLOSE. Does this matter?
	zfs_ccb_t *zccb = fileobject->FsContext2;

	if (zccb != NULL) {

		if (zccb->searchname.Buffer != NULL) {
			kmem_free(zccb->searchname.Buffer,
			    zccb->searchname.MaximumLength);
			zccb->searchname.Buffer = NULL;
			zccb->searchname.MaximumLength = 0;
		}

		if (zccb->z_name_cache != NULL)
			kmem_free(zccb->z_name_cache, zccb->z_name_len);
		zccb->z_name_cache = NULL;
		zccb->z_name_len = 0;
		kmem_free(zccb, sizeof (zfs_ccb_t));
		fileobject->FsContext2 = NULL;
	}

	vnode_decouplefileobject(vp, fileobject);
}

static BOOLEAN
ends_with_suffix(PUNICODE_STRING name, PCWSTR suffix)
{
	size_t name_len = name->Length / sizeof (WCHAR);
	size_t suffix_len = wcslen(suffix);

	if (name_len < suffix_len)
		return (FALSE);

	return (_wcsnicmp(&name->Buffer[name_len - suffix_len], suffix,
	    suffix_len) == 0);
}

static void
allocate_reparse(struct vnode *vp, char *finalname, char *stream_name, PIRP Irp)
{
	znode_t *zp;
	REPARSE_DATA_BUFFER *rpb;
	size_t size;
	PIO_STACK_LOCATION IrpSp = IoGetCurrentIrpStackLocation(Irp);
	PFILE_OBJECT FileObject = IrpSp->FileObject;

	zp = VTOZ(vp);
	// fix me, direct vp access
	size = zfsctl_is_node(zp) ? vp->v_reparse_size :
	    zp->z_size;
	rpb = ExAllocatePoolWithTag(PagedPool,
	    size, '!FSZ');
	get_reparse_point_impl(zp, (char *)rpb, size, NULL);

	/*
	 * Length, in bytes, of the unparsed portion of the
	 * file name pointed to by the FileName member of the
	 * associated file object.
	 * Should include the leading "/", when finalname
	 * here would be "lower".
	 * Also note, if the looking was for filename:Zone.Identifier,
	 * or similar stream name, we need to include the stream name
	 * part. Ie, from the full FileObject->FileName, whatever that
	 * may have been.
	 */
	ULONG len = 0;
	if (finalname && *finalname) {
		RtlUTF8ToUnicodeN(NULL, 0, &len,
		    finalname, strlen(finalname));
		if (stream_name != NULL) {
			ULONG len2 = 0;
			RtlUTF8ToUnicodeN(NULL, 0, &len2,
			    stream_name, strlen(stream_name));
			len += len2 + sizeof (WCHAR); // for ':'

			// Sadly the ":$DATA" can be implied
			if (!ends_with_suffix(&IrpSp->FileObject->FileName,
			    L":$DATA")) {
				len -= sizeof (WCHAR) * 6;
			}
		}

		len += sizeof (WCHAR);
	}
	rpb->Reserved = len;

	dprintf("%s: returning REPARSE (remainder %d)\n",
	    __func__, rpb->Reserved);
	Irp->IoStatus.Information = rpb->ReparseTag;
	Irp->Tail.Overlay.AuxiliaryBuffer = (void *)rpb;

#if 0
	/* Unknown why, but btrfs does this */
	if (FileObject) {
		UNICODE_STRING *fn = &FileObject->FileName;
		if (fn->Buffer[(fn->Length / sizeof (WCHAR)) - 1] == '\\')
			rpb->Reserved = sizeof (WCHAR);
	}
#endif
}
// Change if you want to debug SecurityDescriptors
#if 1

#define	DUMP_SD(sid)
#undef	USE_DUMP_SD
void dump_sd(PSECURITY_DESCRIPTOR sd) { }

#else

#define	DUMP_SD(sid) dump_sd(sid)

void
dump_sid(PSID sid)
{
	UNICODE_STRING sidString;
	RtlConvertSidToUnicodeString(&sidString, sid, TRUE);
	dprintf("SID: %wZ\n", &sidString);
}

void
DumpAcl(PACL acl)
{
	if (acl == NULL) {
		dprintf("NULL ACL\n");
		return;
	}

	// Dump basic ACL info
	dprintf("ACL Size: %u\n", acl->AclSize);
	dprintf("ACL Revision: %u\n", acl->AclRevision);
	dprintf("ACE Count: %u\n", acl->AceCount);

	// Iterate through all the ACEs in the ACL
	PACE_HEADER aceHeader = NULL;
	ULONG aceOffset = sizeof (ACL);  // Starting after ACL header

	for (ULONG i = 0; i < acl->AceCount; i++) {
		// Get the ACE pointer by calculating its offset within the ACL
		aceHeader = (PACE_HEADER)((PUCHAR)acl + aceOffset);

		if (aceHeader == NULL) {
			dprintf("Failed to get ACE #%u\n", i);
			continue;
		}

		dprintf("  ACE #%u: ", i);
		dprintf("ACE Type: %u ", aceHeader->AceType);
		dprintf("ACE Size: %u ", aceHeader->AceSize);

		// Print the Access Mask based on the ACE type
		switch (aceHeader->AceType) {
		case ACCESS_ALLOWED_ACE_TYPE:
		case ACCESS_DENIED_ACE_TYPE:
		{
			PACCESS_ALLOWED_ACE ace =
			    (PACCESS_ALLOWED_ACE)aceHeader;
			dprintf("Access Mask: 0x%08X\n", ace->Mask);
			dump_sid(&ace->SidStart);
		}
		break;
		case SYSTEM_AUDIT_ACE_TYPE:
		{
			PSYSTEM_AUDIT_ACE ace =
			    (PSYSTEM_AUDIT_ACE)aceHeader;
			dprintf("Audit Mask: 0x%08X\n", ace->Mask);
			dump_sid(&ace->SidStart);
		}
		break;
		case ACCESS_ALLOWED_COMPOUND_ACE_TYPE:
			dprintf("Access Allowed Compound ACE\n");
			break;
	//	case ACCESS_DENIED_COMPOUND_ACE_TYPE:
	//	    dprintf("Access Denied Compound ACE\n");
	//	    break;
		default:
			dprintf("Unknown ACE Type %u\n",
			    aceHeader->AceType);
		}

		aceOffset += aceHeader->AceSize;  // Move to the next ACE
	}
}

void
dump_sd(PSECURITY_DESCRIPTOR sd)
{
	NTSTATUS status = STATUS_SUCCESS;
	PSECURITY_DESCRIPTOR absoluteSD = NULL;
	BOOLEAN daclPresent = FALSE, saclPresent = FALSE,
	    ownerDefaulted = FALSE, groupDefaulted = FALSE;
	PACL dacl = NULL, sacl = NULL;
	PSID owner = NULL, primaryGroup = NULL;

	// If SD is in self-relative format, convert it to absolute
	if (!RtlValidSecurityDescriptor(sd)) {
		dprintf("Invalid security descriptor!\n");
		return;
	}

	if (RtlValidRelativeSecurityDescriptor(sd,
	    RtlLengthSecurityDescriptor(sd), 0)) {

		ULONG sdSize = 0;
		ULONG daclSize = 0;
		ULONG saclSize = 0;
		ULONG ownerSize = 0;
		ULONG primaryGroupSize = 0;

		// Get the required sizes for the absolute SD and associated
		// fields
		status = RtlSelfRelativeToAbsoluteSD(sd, absoluteSD, &sdSize,
		    dacl, &daclSize, sacl, &saclSize, owner, &ownerSize,
		    primaryGroup, &primaryGroupSize);
		if (status == STATUS_BUFFER_TOO_SMALL) {
			// Allocate memory for absolute SD and associated
			// components
			absoluteSD = ExAllocatePoolWithTag(NonPagedPoolNx,
			    sdSize, 'SDAB');
			if (!absoluteSD) {
				dprintf("Failed to allocate memory\n");
				return;
			}

			dacl = ExAllocatePoolWithTag(NonPagedPoolNx, daclSize,
			    'DACL');
			sacl = ExAllocatePoolWithTag(NonPagedPoolNx, saclSize,
			    'SACL');
			owner = ExAllocatePoolWithTag(NonPagedPoolNx, ownerSize,
			    'OWNR');
			primaryGroup = ExAllocatePoolWithTag(NonPagedPoolNx,
			    primaryGroupSize, 'PGRP');

			if (!dacl || !sacl || !owner || !primaryGroup) {
				dprintf("Failed to allocate memory\n");
				ExFreePool(absoluteSD);
				return;
			}

			// Now, perform the conversion
			status = RtlSelfRelativeToAbsoluteSD(sd, absoluteSD,
			    &sdSize, dacl, &daclSize, sacl, &saclSize, owner,
			    &ownerSize, primaryGroup, &primaryGroupSize);
			if (!NT_SUCCESS(status)) {
				dprintf("failed with status: 0x%X\n", status);
				ExFreePool(absoluteSD);
				ExFreePool(dacl);
				ExFreePool(sacl);
				ExFreePool(owner);
				ExFreePool(primaryGroup);
				return;
			}

			// Print the absolute SD details
			dprintf("Absolute Security Descriptor:\n");
			dprintf("  Owner SID: ");
			dump_sid(owner);
			dprintf("  Primary Group SID: ");
			dump_sid(primaryGroup);
			if (daclSize) {
				dprintf("  DACL: ");
				DumpAcl(dacl);
			}
			if (saclSize) {
				dprintf("  SACL: ");
				DumpAcl(sacl);
			}
			// Free memory after printing
			ExFreePool(absoluteSD);
			ExFreePool(dacl);
			ExFreePool(sacl);
			ExFreePool(owner);
			ExFreePool(primaryGroup);
		} else {
			dprintf("Failed to retrieve buffer size: 0x%X\n",
			    status);
		}
		return;
	}
	dprintf("SD is Absolute\n");
}
#endif

void
zfs_security_context_pre(vattr_t *vap,
    PIO_SECURITY_CONTEXT SecurityContext)
{
	NTSTATUS status;
	if (SecurityContext &&
	    SecurityContext->AccessState &&
	    SecurityContext->AccessState->SecurityDescriptor) {
		PSECURITY_DESCRIPTOR sd;
		PSID ownerSid, groupSid;
		BOOLEAN ownerDefaulted, groupDefaulted;

		sd = SecurityContext->AccessState->SecurityDescriptor;

		// Retrieve the Owner SID using the API
		status = RtlGetOwnerSecurityDescriptor(sd, &ownerSid,
		    &ownerDefaulted);
		if (NT_SUCCESS(status) && ownerSid) {
			// Translate the SID to UID for ZFS
			vap->va_uid = zfs_sid2uid(ownerSid);
			vap->va_mask |= ATTR_UID;
		}

		// Retrieve the Group SID using the API
		status = RtlGetGroupSecurityDescriptor(sd, &groupSid,
		    &groupDefaulted);
		if (NT_SUCCESS(status) && groupSid) {
			// Translate the SID to GID for ZFS
			vap->va_gid = zfs_sid2gid(groupSid);
			vap->va_mask |= ATTR_GID;
		}
	} else {
		// If no security context, use the current process token
		SECURITY_SUBJECT_CONTEXT subject;
		SeCaptureSubjectContext(&subject);
		PACCESS_TOKEN token = subject.ClientToken ?
		    subject.ClientToken : subject.PrimaryToken;

		PTOKEN_USER tokenUser = NULL;
		PTOKEN_PRIMARY_GROUP tokenGroup = NULL;
		ULONG len;

		if (NT_SUCCESS(SeQueryInformationToken(token, TokenUser,
		    (PVOID *)&tokenUser))) {
			vap->va_uid = zfs_sid2uid(tokenUser->User.Sid);
			vap->va_mask |= ATTR_UID;
		}

		if (NT_SUCCESS(SeQueryInformationToken(token, TokenPrimaryGroup,
		    (PVOID *)&tokenGroup))) {
			vap->va_gid = zfs_sid2gid(tokenGroup->PrimaryGroup);
			vap->va_mask |= ATTR_GID;
		}

		if (tokenUser)
			ExFreePool(tokenUser);
		if (tokenGroup)
			ExFreePool(tokenGroup);
		SeReleaseSubjectContext(&subject);
	}

	if (!(vap->va_mask & ATTR_UID)) {
		vap->va_uid = UID_NOBODY;
		vap->va_mask |= ATTR_UID;
	}
	if (!(vap->va_mask & ATTR_GID)) {
		vap->va_gid = GID_NOBODY;
		vap->va_mask |= ATTR_GID;
	}
	dprintf("%s using uid, gid: (%llu, %llu)\n", __func__,
	    vap->va_uid, vap->va_gid);
}


void
zfs_security_context_post(vnode_t *vp, vnode_t *dvp,
    PIO_SECURITY_CONTEXT SecurityContext)
{
	NTSTATUS status;

	if (SecurityContext != NULL &&
	    SecurityContext->AccessState &&
	    SecurityContext->AccessState->SecurityDescriptor != NULL) {
		// zfs_attach_security() will only do work if we do
		// not have a security descriptor already
		zfs_remove_ntsecurity(vp);
		zfs_attach_security(vp, dvp,
		    SecurityContext->AccessState);
	}
}

/*
 * Take filename, look for colons ":".
 * No colon, return OK.
 * if ends with "::$DATA". Terminate on colon, return OK (regular file open).
 * if ends with anything not ":$DATA", return error.
 * (we don't handle other types)
 * if colon, parse name up until next colon. Assign colonname to
 * point to stream name.
 */
int
stream_parse(char *filename, char **streamname)
{
	char *colon, *second;

	// Just a filename, no streams.
	colon = strchr(filename, ':');
	if (colon == NULL)
		return (0);

	// Regular file, with "::$DATA" end?
	if (strcasecmp(colon, "::$DATA") == 0) {
		*colon = 0; // Terminate before colon
		return (0);
	}

	// Look for second colon
	second = strchr(&colon[1], ':');

	// No second colon, just stream name. Validity check?
	if (second == NULL) {
		*streamname = &colon[1];
		*colon = 0; // Cut off streamname from filename

		// We now ADD ":$DATA" to the stream name.
		strcat(*streamname, ":$DATA");

		goto checkname;
	}

	// Have second colon, better be ":$DATA".
	if (strcasecmp(second, ":$DATA") == 0) {

		// Terminate at second colon, set streamname
		// We now keep the ":$DATA" extension in the xattr name
		// *second = 0;

		*streamname = &colon[1];
		*colon = 0; // Cut of streamname from filename

		goto checkname;
	}

	// Not $DATA
	dprintf("%s: Not handling StreamType '%s'\n", __func__, second);
	return (EINVAL);

checkname:
	if (strlen(*streamname) >= 512)
		return (STATUS_OBJECT_NAME_INVALID);

	if (strchr(*streamname, '/') ||
	    /* strchr(&colon[2], ':') || there is one at ":$DATA" */
	    !strcasecmp("DOSATTRIB:$DATA", *streamname) ||
	    !strcasecmp("EA:$DATA", *streamname) ||
	    !strcasecmp("reparse:$DATA", *streamname) ||
	    !strcasecmp("casesensitive:$DATA", *streamname))
		return (STATUS_OBJECT_NAME_INVALID);

	return (0);
}

/*
 * OpLock magic
 */

// When oplock has been resolved, and we are at the right
// level, this worker will be called to finish the IRP.
static void
ZfsOplockCreateWorker(_In_ PDEVICE_OBJECT DevObj, _In_ PVOID Context)
{
	ZFS_OPLOCK_CREATE_CTX *ctx = (ZFS_OPLOCK_CREATE_CTX *)Context;
	PIRP Irp = ctx->Irp;
	NTSTATUS status;

	if (ctx->WorkItem)
		IoFreeWorkItem(ctx->WorkItem);

	// Mark this Irp has having already been through OpLock, and not
	// go through the same test again.
	Irp->Tail.Overlay.DriverContext[0] =
	    (void *)(OPLOCK_SKIP_MAGIC | ctx->SkipMask);

	ExFreePoolWithTag(ctx, 'plkO');

	dprintf("%s: calling dispatcher\n", __func__);
	status = dispatcher(DevObj, Irp);
	dprintf("%s: dispatcher returned %ld\n", __func__, status);
}

// When oplock has been resolved, but running at wrong
// level, punt it off to a work item.
void
ZfsOplockCreatePostBreak(_In_ PVOID Context, _In_ PIRP Irp)
{
	ZFS_OPLOCK_CREATE_CTX *ctx = (ZFS_OPLOCK_CREATE_CTX *)Context;

	dprintf("%s: punting to WorkItem\n", __func__);

	ctx->WorkItem = IoAllocateWorkItem(ctx->DeviceObject);
	if (!ctx->WorkItem) {
		// Fail the IRP if we can�t resume safely
		Irp->IoStatus.Status = STATUS_INSUFFICIENT_RESOURCES;
		Irp->IoStatus.Information = 0;
		// FsRtlCompleteRequest(Irp, Irp->IoStatus.Status);
		IoCompleteRequest(Irp, IO_NO_INCREMENT);
		ExFreePoolWithTag(ctx, 'plkO');
		return;
	}

	IoQueueWorkItem(
	    ctx->WorkItem,
	    ZfsOplockCreateWorker,
	    DelayedWorkQueue,
	    ctx);
}

/*
 * Call from CREATE path *after* vp is known, *before* coupling,
 * holding/releasing vp->FileHeader.Resource around this call as you do now.
 */
static
NTSTATUS
zfs_preflight_oplock_on_open_existing(
    PDEVICE_OBJECT DeviceObject,
    vnode_t *vp,
    PIRP Irp,
    PIO_STACK_LOCATION IrpSp,
    uint32_t vp_usecount,
    BOOLEAN skipCreate)
{
	NTSTATUS st;
	ULONG fsrtlFlags = 0;
	ZFS_OPLOCK_CREATE_CTX *ctx = NULL;

	const ULONG Options = IrpSp->Parameters.Create.Options;
	const UCHAR disp = (UCHAR)((Options >> 24) & 0xFF);
	const ACCESS_MASK da = IrpSp->Parameters.Create.SecurityContext
	    ? IrpSp->Parameters.Create.SecurityContext->DesiredAccess
	    : 0;

	const BOOLEAN requiring =
	    (Options & FILE_OPEN_REQUIRING_OPLOCK) ? TRUE : FALSE;
	const BOOLEAN callerComplete =
	    (Options & FILE_COMPLETE_IF_OPLOCKED) ? TRUE : FALSE;
	const BOOLEAN delOnClose =
	    (Options & FILE_DELETE_ON_CLOSE) != 0;

	const BOOLEAN willModifyData =
	    (disp == FILE_SUPERSEDE) ||
	    (disp == FILE_OVERWRITE) ||
	    (disp == FILE_OVERWRITE_IF);

	const BOOLEAN writeIntent =
	    (da & (FILE_WRITE_DATA | FILE_APPEND_DATA |
	    FILE_WRITE_ATTRIBUTES | FILE_WRITE_EA)) ? TRUE : FALSE;

	const BOOLEAN deleteIntent =
	    (da & DELETE) ? TRUE : FALSE;

	const BOOLEAN willWriteOrDelete = writeIntent || deleteIntent ||
	    delOnClose || willModifyData;


	// 4th: plain second-open (no special flags, not modifying) allow pend
	const BOOLEAN isGenericSecondOpen =
	    (!requiring && !callerComplete && !willModifyData &&
	    (vp_usecount > 0));

	// Decide how we'll call FsRtl:
	BOOLEAN needCheck = FALSE; // whether to call FsRtlCheckOplockEx at all
	BOOLEAN allowPend = FALSE; // if TRUE provide ctx/callback

	// (1) REQUIRING: no pend, conservative fast-fail first
	if (requiring) {
		if (vp->OplockRefCount > 0)
			return (STATUS_CANNOT_BREAK_OPLOCK);
		needCheck = TRUE;
		allowPend = FALSE;
		fsrtlFlags |= OPLOCK_FLAG_COMPLETE_IF_OPLOCKED;
	} else if (!skipCreate && willWriteOrDelete) {
		// (2) Data-modifying pend if needed
		// (ignore COMPLETE_IF_OPLOCKED)
		needCheck = TRUE;
		allowPend = TRUE;
	} else if (!skipCreate && isGenericSecondOpen) {
		// (4) Generic second-open pend if needed
		needCheck = TRUE;
		allowPend = TRUE;
	} else if (callerComplete) {
		// (3) Caller COMPLETE_IF_OPLOCKED (non-modifying) no pend
		needCheck = TRUE;
		allowPend = FALSE;
		fsrtlFlags |= OPLOCK_FLAG_COMPLETE_IF_OPLOCKED;
	}

	if (!needCheck)
		return (STATUS_SUCCESS);

	if (allowPend) {
		ctx = ExAllocatePoolZero(NonPagedPoolNx, sizeof (*ctx), 'plkO');
		if (!ctx)
			return (STATUS_INSUFFICIENT_RESOURCES);
		ctx->DeviceObject = DeviceObject;
		ctx->SkipMask = OPLOCK_SKIP_CREATE;
		ctx->Irp = Irp;
	}

	st = FsRtlCheckOplockEx(
	    vp_oplock(vp),
	    Irp,
	    fsrtlFlags,
	    ctx,
	    allowPend ? ZfsOplockCreatePostBreak : NULL,
	    NULL);

	if (st == STATUS_PENDING) {
		if (allowPend) {
			IoMarkIrpPending(Irp);
			return (STATUS_PENDING);
		}
		// Defensive: no-pend mode should not return PENDING
		IoCancelIrp(Irp);
		if (requiring)
			return (STATUS_CANNOT_BREAK_OPLOCK);
		return (STATUS_OPLOCK_BREAK_IN_PROGRESS);
	}

	if (ctx != NULL)
		ExFreePoolWithTag(ctx, 'plkO');

	if (requiring) {
		if (st == STATUS_OPLOCK_BREAK_IN_PROGRESS)
			return (STATUS_CANNOT_BREAK_OPLOCK);
	}

	return (st); // SUCCESS or terminal error
}

/* End of oplock */

/*
 * Attempt to parse 'filename', descending into filesystem.
 * If start "dvp" is passed in, it is expected to have a HOLD
 * If successful, function will return with:
 * - HOLD on dvp
 * - HOLD on vp
 * - final parsed filename part in 'lastname' (in the case of creating an entry)
 *
 * IRP_MJ_CREATE calls
 *
 * zfsvfs  Filename
 * --------------------------------------------------------
 * IRP_MJ_CREATE(pool, "/lower/today.txt")
 *     : lookup "lower", return STATUS_REPARSE
 *     : Set unparsed length rdp->Reserved = 10 ("/today.txt") * 2
 * --------------------------------------------------------
 * IRP_MJ_CREATE(lower, "/today.txt")
 *     : lookup "today.txt", return SUCCESS
 */

int
zfs_find_dvp_vp(zfsvfs_t *zfsvfs, char *filename, int finalpartmaynotexist,
    int finalpartmustnotexist, char **lastname, struct vnode **dvpp,
    struct vnode **vpp, int flags, ULONG options, cred_t *cr)
{
	int error = ENOENT;
	znode_t *zp;
	struct vnode *dvp = NULL;
	struct vnode *vp = NULL;
	char *word = NULL;
	char *brkt = NULL;
	struct componentname cn;
	int fullstrlen;
	char namebuffer[MAXNAMELEN];
	BOOLEAN has_trailing_separator = FALSE;

	// Iterate from dvp if given, otherwise root
	dvp = *dvpp;

	if (dvp == NULL) {
		// Grab a HOLD
		error = zfs_zget(zfsvfs, zfsvfs->z_root, &zp);
		if (error != 0)
			return (ESRCH);  // No such dir
		dvp = ZTOV(zp);
	} else {
		// Passed in dvp is already HELD, but grab one now
		// since we release dirs as we descend
		dprintf("%s: passed in dvp\n", __func__);
		if (VN_HOLD(dvp) != 0)
			return (ESRCH);
	}

	fullstrlen = strlen(filename);

	// Sometimes we are given a path like "\Directory\directory\"
	// with the final separator, we want to eat that final character.
	if ((fullstrlen > 2) &&
	    (filename[fullstrlen - 1] == '\\')) {
		filename[--fullstrlen] = 0;
		has_trailing_separator = TRUE;
	}

	for (word = strtok_r(filename, "/\\", &brkt);
	    word;
	    word = strtok_r(NULL, "/\\", &brkt)) {
		int direntflags = 0;
		// dprintf("..'%s'..", word);
		// If a component part name is too long
		if (strlen(word) > MAXNAMELEN - 1) {
			VN_RELE(dvp);
			if (vp)
				VN_RELE(vp);
			return (STATUS_OBJECT_NAME_INVALID);
		}
		strlcpy(namebuffer, word, sizeof (namebuffer));
		// Dont forget zfs_lookup() modifies
		// "cn" here, so size needs to be max, if
		// formD is in effect.
		cn.cn_nameiop = LOOKUP;
		cn.cn_flags = ISLASTCN;
		cn.cn_namelen = strlen(namebuffer);
		cn.cn_nameptr = namebuffer;
		cn.cn_pnlen = MAXNAMELEN;
		cn.cn_pnbuf = namebuffer;

		error = zfs_lookup(VTOZ(dvp), namebuffer,
		    &zp, flags, cr, &direntflags, &cn);

		// If snapshot dir and we are pretending it is deleted...
		if (error == 0 && zp->z_vnode != NULL &&
		    (vnode_unlink(ZTOV(zp)) & DELETE_HIDDEN)) {
			VN_RELE(ZTOV(zp));
			error = ENOENT;
		}
		if (error != 0) {
			// If we are creating a file, or looking up parent,
			// allow it not to exist
			if (finalpartmaynotexist)
				break;
			dprintf("failing out here\n");
			// since we weren't successful, release dvp here
			VN_RELE(dvp);
			dvp = NULL;
			break;
		}

		// If last lookup hit a non-directory type, we stop
		vp = ZTOV(zp);
		ASSERT(zp != NULL);

		/*
		 * If we come across a REPARSE, we stop processing here
		 * and pass the "zp" back for caller to do more processing,
		 * which might include returning "zp" (FILE_OPEN_REPARSE_POINT)
		 * and ReparseTag.
		 * If they requested FileOpenReparsePoint, AND we are at the
		 * final-part, we open it normally.
		 * Other cases we need to ask for redriving the query
		 */

		if (zp->z_pflags & ZFS_REPARSE) {
			/*
			 * Indicate if reparse was final part,
			 * caller will handle this case
			 */
			if (lastname)
				*lastname = brkt;

			if (dvpp != NULL)
				*dvpp = dvp;
			if (vpp != NULL)
				*vpp = vp;

			return (STATUS_REPARSE);
		}

		if (vp && vnode_isdir(vp)) {
			// Not reparse
			VN_RELE(dvp);
			dvp = vp;
			vp = NULL;
		} else {
			// If we aren't the final component, descending dirs,
			// and it's a file?
			if (brkt != NULL && *brkt != 0) {
				dprintf("%s: not a DIR triggered '%s'\n",
				    __func__, word);
				if (vp)
					VN_RELE(vp);
				VN_RELE(dvp);
				return (ENOTDIR);
			}
			break;
		} // is dir or not

	} // for word
	// dprintf("\n");

	if (dvp != NULL) {
		// We return with dvp HELD
		// VN_RELE(dvp);
	} else {
		dprintf("%s: failed to find dvp for '%s' word '%s' err %d\n",
		    __func__, filename, word?word:"(null)", error);
		return (error);
	}

	if (error != 0 && !vp && !finalpartmaynotexist) {
		VN_RELE(dvp);
		return (ENOENT);
	}

	/* finalpartmustnotexist and we got a vp? */
	if (!word && finalpartmustnotexist && dvp && vp) {
		dprintf("CREATE with existing dir exit?\n");

		VN_RELE(vp);
		VN_RELE(dvp);

		if (zp && !S_ISDIR(zp->z_mode))
			return (ENOTDIR);
		return (EEXIST);
	}

	// If finalpartmaynotexist is TRUE, make sure we are looking at
	// the finalpart, and not in the middle of descending
	if (finalpartmaynotexist && brkt != NULL && *brkt != 0) {
		dprintf("finalpartmaynotexist, but not at finalpart: %s\n",
		    brkt);
		VN_RELE(dvp);
		return (ESRCH);
	}

	// Check if we got a file, but request had trailing slash
	if (vp != NULL && !vnode_isdir(vp) && has_trailing_separator) {
		VN_RELE(vp);
		VN_RELE(dvp);
		// NTFS returns STATUS_OBJECT_NAME_INVALID
		return (STATUS_OBJECT_NAME_INVALID); // ENOTDIR
	}

	if (lastname) {

		*lastname = word /* ? word : filename */;

		// Skip any leading "\"
		while (*lastname != NULL &&
		    (**lastname == '\\' || **lastname == '/'))
			(*lastname)++;

	}

	if (dvpp != NULL)
		*dvpp = dvp;
	if (vpp != NULL)
		*vpp = vp;

	return (0);
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
int
zfs_vnop_lookup_impl(PIRP Irp, PIO_STACK_LOCATION IrpSp, mount_t *zmo,
    char *filename, xvattr_t *xvap)
{
	int error;
	cred_t cr_buf;
	cred_t *cr = &cr_buf;
	char *finalname = NULL;
	PFILE_OBJECT FileObject;
	ULONG outlen;
	struct vnode *dvp = NULL;
	struct vnode *vp = NULL;
	znode_t *zp = NULL;
	znode_t *dzp = NULL;
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
	BOOLEAN FileOpenReparsePoint;
	ULONG CreateDisposition;
	zfsvfs_t *zfsvfs = vfs_fsprivate(zmo);
	int flags = 0;
	int dvp_no_rele = 0;
	char *stream_name = NULL;
	boolean_t UndoShareAccess = FALSE;
	NTSTATUS Status = STATUS_SUCCESS;
	ACCESS_MASK granted_access = 0;
	ACCESS_MASK DesiredAccess =
	    IrpSp->Parameters.Create.SecurityContext->DesiredAccess;
	zfs_ccb_t *zccb = NULL;
	vattr_t *vap = &xvap->xva_vattr;
	boolean_t return_break_in_progress = B_FALSE; // oplock special case

	if (zfsvfs == NULL)
		return (STATUS_OBJECT_PATH_NOT_FOUND);

	if (vfs_isunmount(zmo))
		return (STATUS_DEVICE_NOT_READY);

	spl_fill_cred_from_irp(cr, Irp);

	FileObject = IrpSp->FileObject;
	Options = IrpSp->Parameters.Create.Options;

	dprintf("%s: enter on dataset '%wZ'\n", __func__,
	    &zmo->name);

	if (FileObject->RelatedFileObject != NULL) {
		//  A relative open must be via a relative path.
		if (FileObject->FileName.Length != 0 &&
		    FileObject->FileName.Buffer[0] == L'\\') {
			return (STATUS_INVALID_PARAMETER);
		}
	}

	if (FileObject->Vpb == NULL &&
	    FileObject->RelatedFileObject != NULL)
		FileObject->Vpb = FileObject->RelatedFileObject->Vpb;

	DirectoryFile =
	    BooleanFlagOn(Options, FILE_DIRECTORY_FILE);
	NonDirectoryFile =
	    BooleanFlagOn(Options, FILE_NON_DIRECTORY_FILE);
	NoIntermediateBuffering =
	    BooleanFlagOn(Options, FILE_NO_INTERMEDIATE_BUFFERING);
	NoEaKnowledge =
	    BooleanFlagOn(Options, FILE_NO_EA_KNOWLEDGE);
	DeleteOnClose =
	    BooleanFlagOn(Options, FILE_DELETE_ON_CLOSE);
	FileOpenByFileId =
	    BooleanFlagOn(Options, FILE_OPEN_BY_FILE_ID);
	FileOpenReparsePoint =
	    BooleanFlagOn(Options, FILE_OPEN_REPARSE_POINT);


	// Should be passed an 8/16 byte FileId instead.
	if (FileOpenByFileId) {
		if (FileObject->FileName.Length !=
		    sizeof (ULONGLONG) &&
		    FileObject->FileName.Length !=
		    sizeof (FILE_ID_128))
			return (STATUS_INVALID_PARAMETER);
	}

	TemporaryFile = BooleanFlagOn(IrpSp->Parameters.Create.FileAttributes,
	    FILE_ATTRIBUTE_TEMPORARY);

	CreateDisposition = (Options >> 24) & 0x000000ff;

	IsPagingFile = BooleanFlagOn(IrpSp->Flags, SL_OPEN_PAGING_FILE);
	// ASSERT(!IsPagingFile);
	// ASSERT(!OpenRequiringOplock);
	// Open the directory instead of the file
	OpenTargetDirectory = BooleanFlagOn(IrpSp->Flags,
	    SL_OPEN_TARGET_DIRECTORY);
/*
 *	CreateDisposition value	Action if file exists
 *	Action if file does not exist  UNIX Perms
 *		FILE_SUPERSEDE		Replace the file.
 *		    Create the file.        *        Unlink + O_CREAT | O_TRUNC
 *		FILE_CREATE		    Return an error.
 *		    Create the file.        *        O_CREAT | O_EXCL
 *		FILE_OPEN		    Open the file.
 *		        Return an error.        *        0
 *		FILE_OPEN_IF		Open the file.
 *		        Create the file.        *        O_CREAT
 *		FILE_OVERWRITE		Open the file, overwrite it.
 *	Return an error.    *        O_TRUNC
 *		FILE_OVERWRITE_IF	Open the file, overwrite it.
 *	Create the file.    *        O_CREAT | O_TRUNC
 *
 *	Apparently SUPERSEDE is more or less Unlink entry before recreate,
 * so it loses ACLs, XATTRs and NamedStreams.
 *
 *		IoStatus return codes:
 *		FILE_CREATED
 *		FILE_OPENED
 *		FILE_OVERWRITTEN
 *		FILE_SUPERSEDED
 *		FILE_EXISTS
 *		FILE_DOES_NOT_EXIST
 *
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

	// If it is a volumeopen, we just grab rootvp so that directory
	// listings work - most Options are ignored with VolumeOpens
	if (FileObject->FileName.Length == 0 &&
	    FileObject->RelatedFileObject == NULL) {

		// don't allow root to be opened on unmounted FS
		if (vfs_fsprivate(zmo) == NULL)
			return (STATUS_DEVICE_NOT_READY);

		if (CreateDisposition == FILE_CREATE ||
		    CreateDisposition == FILE_OPEN_IF)
			return (STATUS_ACCESS_DENIED);

		dprintf("Started NULL open, returning root of mount\n");
		error = zfs_zget(zfsvfs, zfsvfs->z_root, &zp);
		if (error != 0)
			return (FILE_DOES_NOT_EXIST);  // No root dir?!

		dvp = ZTOV(zp);

		zfs_couplefileobject(dvp, NULL, FileObject, 0ULL, &zccb,
		    Irp->Overlay.AllocationSize.QuadPart, DesiredAccess,
		    stream_name, Irp);

		VN_RELE(dvp);

		Irp->IoStatus.Information = FILE_OPENED;
		return (STATUS_SUCCESS);
	}

	// No name conversion with FileID

	if (!FileOpenByFileId) {

		if (FileObject->FileName.Buffer != NULL &&
		    FileObject->FileName.Length > 0) {
			// Convert incoming filename to utf8
			error = RtlUnicodeToUTF8N(filename, PATH_MAX - 1,
			    &outlen,
			    FileObject->FileName.Buffer,
			    FileObject->FileName.Length);

			if (error != STATUS_SUCCESS &&
			    error != STATUS_SOME_NOT_MAPPED) {
				dprintf("RtlUnicodeToUTF8N returned 0x%x "
				    "input len %d\n",
				    error, FileObject->FileName.Length);
				return (STATUS_OBJECT_NAME_INVALID);
			}
			// ASSERT(error != STATUS_SOME_NOT_MAPPED);
			// Output string is only null terminated if input is,
			// so do so now.
			filename[outlen] = 0;
			dprintf("%s: converted name is '%s' input len bytes %d "
			    "(err %d) %s %s\n", __func__, filename,
			    FileObject->FileName.Length, error,
			    DeleteOnClose ? "DeleteOnClose" : "",
			    IrpSp->Flags&SL_CASE_SENSITIVE ? "CaseSensitive" :
			    "CaseInsensitive");

			if (!(IrpSp->Flags & SL_CASE_SENSITIVE) &&
			    (zfsvfs->z_case != ZFS_CASE_SENSITIVE))
				flags |= FIGNORECASE;

			/*
			 * Block MountManager from probing System Volume
			 * Information on snapshots.  MountManager reads
			 * MountPointManagerRemoteDatabase from the snapshot,
			 * gets confused by stale data, and truncates the live
			 * volume's copy, losing all junction mount points.
			 * Snapshots are read-only and have no valid mount
			 * database of their own, so deny early.
			 */
			if (zfsvfs->z_issnap &&
			    _strnicmp(filename,
			    "\\System Volume Information",
			    sizeof ("\\System Volume Information") - 1) == 0)
				return (STATUS_OBJECT_NAME_NOT_FOUND);

			if (Irp->Overlay.AllocationSize.QuadPart > 0)
				dprintf("AllocationSize requested %llu\n",
				    Irp->Overlay.AllocationSize.QuadPart);

			// Check if we are called as VFS_ROOT();
			OpenRoot = (strncmp("\\", filename, PATH_MAX) == 0 ||
			    strncmp("\\*", filename, PATH_MAX) == 0 ||
			    strncmp("\\\\", filename, PATH_MAX) == 0);

			if (OpenRoot) {

				if (NonDirectoryFile)
					return (STATUS_FILE_IS_A_DIRECTORY);

				if (CreateDisposition == FILE_CREATE ||
				    CreateDisposition == FILE_OPEN_IF)
					return (STATUS_ACCESS_DENIED);

				error = zfs_zget(zfsvfs, zfsvfs->z_root, &zp);

				if (error == 0) {
					vp = ZTOV(zp);
					zfs_couplefileobject(vp, NULL,
					    FileObject, zp->z_size, &zccb,
					    Irp->
					    Overlay.AllocationSize.QuadPart,
					    DesiredAccess,
					    stream_name, Irp);
					VN_RELE(vp);

					Irp->IoStatus.Information = FILE_OPENED;
					return (STATUS_SUCCESS);
				}

				Irp->IoStatus.Information = FILE_DOES_NOT_EXIST;
				return (STATUS_OBJECT_PATH_NOT_FOUND);
			} // OpenRoot

		} else { // If got filename

			// If no filename, we should fail,
			// unless related is set.
			if (FileObject->RelatedFileObject == NULL) {
				// Fail
				return (STATUS_OBJECT_NAME_INVALID);
			}
			// Related set, return it as opened.
			dvp = FileObject->RelatedFileObject->FsContext;
			if (dvp == NULL) {
				/* RelatedFileObject is a raw device, not ZFS */
				return (STATUS_INVALID_PARAMETER);
			}
			zp = VTOZ(dvp);
			dprintf("%s: Relative null-name open\n",
			    __func__);
			// Check types
			if (NonDirectoryFile && vnode_isdir(dvp)) {
				Irp->IoStatus.Information = FILE_DOES_NOT_EXIST;
				return (STATUS_FILE_IS_A_DIRECTORY);
			}
			if (DirectoryFile && !vnode_isdir(dvp)) {
				Irp->IoStatus.Information = FILE_DOES_NOT_EXIST;
				return (STATUS_NOT_A_DIRECTORY);
			}
			// Grab vnode to ref
			if (VN_HOLD(dvp) == 0) {
				zfs_couplefileobject(dvp, NULL, FileObject,
				    0ULL, &zccb,
				    Irp->Overlay.AllocationSize.QuadPart,
				    DesiredAccess,
				    stream_name, Irp);
				VN_RELE(dvp);
			} else {
				Irp->IoStatus.Information = 0;
				return (STATUS_OBJECT_PATH_NOT_FOUND);
			}
			Irp->IoStatus.Information = FILE_OPENED;
			return (STATUS_SUCCESS);
		}

		// We have converted the filename, continue..
		if (FileObject->RelatedFileObject &&
		    FileObject->RelatedFileObject->FsContext) {
			dvp = FileObject->RelatedFileObject->FsContext;

			// This branch here, if failure, should not release dvp
			dvp_no_rele = 1;
		}

		/*
		 * Redirect the NTFS reparse-point index to our root.
		 * MountManager opens \$Extend\$Reparse:$R:$INDEX_ALLOCATION
		 * to enumerate volume mount-point junctions.  We return root
		 * so DIRECTORY_CONTROL(FileReparsePointInformation) can answer
		 * correctly; otherwise MountManager truncates
		 * MountPointManagerRemoteDatabase when the open fails.
		 */
		if (_strnicmp(filename, "\\$Extend\\$Reparse", 17) == 0 &&
		    (filename[17] == '\0' || filename[17] == ':')) {
			error = zfs_zget(zfsvfs, zfsvfs->z_root, &zp);
			if (error == 0) {
				vp = ZTOV(zp);
				zfs_couplefileobject(vp, NULL, FileObject,
				    zp->z_size, &zccb,
				    Irp->Overlay.AllocationSize.QuadPart,
				    DesiredAccess, NULL, Irp);
				VN_RELE(vp);
				Irp->IoStatus.Information = FILE_OPENED;
				return (STATUS_SUCCESS);
			}
			Irp->IoStatus.Information = FILE_DOES_NOT_EXIST;
			return (STATUS_OBJECT_NAME_NOT_FOUND);
		}

/*
 * Here, we want to check for Streams, which come in the syntax
 * filename.ext:Stream:Type
 *  Type: appears optional, or we handle ":DATA". All others will be rejected.
 *  Stream: name of the stream, we convert this into XATTR named Stream
 * It is valid to create a filename containing colons, so who knows what will
 * happen here.
 */
		error = stream_parse(filename, &stream_name);
		if (error) {
			/*
			 * stream_parse returns a UNIX errno, not NTSTATUS.
			 * NT_SUCCESS(errno) can be TRUE, so the I/O manager
			 * would treat the CREATE as success with a NULL
			 * FsContext, then panic or corrupt
			 * MountPointManagerRemoteDatabase on the next IRP.
			 */
			Irp->IoStatus.Information = FILE_DOES_NOT_EXIST;
			return (STATUS_OBJECT_NAME_INVALID);
		}
		if (stream_name != NULL)
			dprintf("%s: Parsed out streamname '%s'\n",
			    __func__, stream_name);

		// There is a special case, where name is just the stream
		// ":ZoneIdentifier:$DATA", and
		// RelatedFileObject is set to the object.
		if (stream_name != NULL &&
		    FileObject->RelatedFileObject &&
		    FileObject->RelatedFileObject->FsContext &&
		    strlen(filename) == 0) {

			// The RelatedFileObject conditional above will
			// assign "dvp" - but
			// the stream_name check below will expect it in "vp".
			// dvp_no_rele is already set.
			// So dvp should be "filename.txt", and streamname
			// has ":streamname" - short hand.
			dprintf("special case Zone.Identifier\n");
			dvp_no_rele = 1;
			vp = FileObject->RelatedFileObject->FsContext;
			zp = VTOZ(vp);
			dvp = NULL;
			VERIFY0(VN_HOLD(vp));

		} else {

			/*
			 * Suppress ctldir auto-mount for attribute-only opens
			 * (Explorer stat/thumbnail queries).  A genuine read or
			 * write access has rights beyond FILE_READ_ATTRIBUTES
			 * and SYNCHRONIZE, so those still trigger the mount.
			 */
			if ((DesiredAccess & ~(FILE_READ_ATTRIBUTES |
			    SYNCHRONIZE | READ_CONTROL |
			    FILE_TRAVERSE | ACCESS_SYSTEM_SECURITY)) == 0)
				flags |= LOOKUP_NO_AUTOMOUNT;

			// If we have dvp, it is HELD
			error = zfs_find_dvp_vp(zfsvfs, filename,
			    (CreateFile || OpenTargetDirectory),
			    (CreateDisposition == FILE_CREATE),
			    &finalname, &dvp, &vp, flags, Options, cr);

		}

	} else {  // Open By File ID FileOpenByFileId

		// Filename.Length is 16 (ObjectID) should we
		// verify the VolumeID matches? Lookup VolumeID?
		// or can we rely on zfsvfs being correct?
		error = zfs_zget(zfsvfs,
		    *((uint64_t *)IrpSp->FileObject->FileName.Buffer), &zp);
		// Code below assumed dvp is also , so we need to
		// open parent. We can not trust vnode_parent() here since
		// links can have different parents. Possibly speed this up
		// in future with a z_links > 1 test?
		if (error == 0) {
			uint64_t parent;
			error = sa_lookup(zp->z_sa_hdl, SA_ZPL_PARENT(zfsvfs),
			    &parent, sizeof (parent));
			if (error == 0) {
				error = zfs_zget(zfsvfs, parent, &dzp);
			}
			vp = ZTOV(zp);
			if (error != 0) {
				VN_RELE(vp);
				dprintf("Missing parent error\n");
				return (error);
			} // failed to get parentid, or find parent
			// Copy over the vp info for below, both are held.
			// dzp/dvp held by zget()
			dvp = ZTOV(dzp);
			dprintf("getid start %d\n", vp->v_iocount);
		}
	}

	// If successful:
	// - vp is HELD
	// - dvp is HELD
	// we need dvp from here on down.

	// If asked to open reparse point instead of following it, and
	// it was the final part of the path, then just open it.
	if (error == STATUS_REPARSE && FileOpenReparsePoint &&
	    (!finalname || !*finalname))
		error = STATUS_SUCCESS;

	if (error) {

		/*
		 * With REPARSE, we are given "zp" to read the ReparseTag, and
		 * if they asked for it returned, do so, or free it.
		 */
		if (error == STATUS_REPARSE) {
			/*
			 * How reparse points work from the point of
			 * view of the filesystem appears to undocumented.
			 * When returning STATUS_REPARSE, MSDN encourages
			 * us to return IO_REPARSE in
			 * Irp->IoStatus.Information, but that means we
			 * have to do our own translation. If we instead
			 * return the reparse tag in Information, and
			 * store a pointer to the reparse data buffer in
			 * Irp->Tail.Overlay.AuxiliaryBuffer,
			 * IopSymlinkProcessReparse will do the
			 * translation for us.
			 * - maharmstone
			 */
			Irp->IoStatus.Information = 0;
			Irp->IoStatus.Status = 0;
			allocate_reparse(vp, finalname, stream_name, Irp);

			// should this only work on the final component?
#if 0
			if (Options & FILE_OPEN_REPARSE_POINT) {
				// Hold open reference, until CLOSE
				error = STATUS_SUCCESS;
				zfs_couplefileobject(vp, NULL, FileObject,
				    zp ? zp->z_size : 0ULL,
				    &zccb,
				    Irp->Overlay.AllocationSize.QuadPart,
				    DesiredAccess,
				    stream_name, Irp);
			}
#endif
			VN_RELE(vp);
			if (dvp)
				VN_RELE(dvp);

			return (error); // STATUS_REPARSE
		}

		if (dvp && !dvp_no_rele) VN_RELE(dvp);
		if (vp) VN_RELE(vp);

		if (!dvp && error == ESRCH) {
			dprintf("%s: failed to find dvp for '%s' \n",
			    __func__, filename);
			Irp->IoStatus.Information = FILE_DOES_NOT_EXIST;
			return (STATUS_OBJECT_PATH_NOT_FOUND);
		}
		if (error == STATUS_OBJECT_NAME_INVALID ||
		    error == ENAMETOOLONG) {
			dprintf("%s: filename component too long\n", __func__);
			return (STATUS_OBJECT_NAME_INVALID);
		}
		if (error == STATUS_IO_REPARSE_TAG_NOT_HANDLED) {
			dprintf("%s: reparse but asked not to handle\n",
			    __func__);
			return (error);
		}
		// Open dir with FILE_CREATE but it exists
		if (error == EEXIST) {
			dprintf("%s: dir exists, wont create\n", __func__);
			Irp->IoStatus.Information = FILE_EXISTS;
			if (OpenTargetDirectory)
				return (STATUS_NOT_A_DIRECTORY);
			return (STATUS_FILE_IS_A_DIRECTORY); // 2
		}
		if (error == ENOTDIR) {
			dprintf("%s: file exists, wont create\n", __func__);
			Irp->IoStatus.Information = FILE_EXISTS;
			return (STATUS_OBJECT_NAME_COLLISION); // 3
		}
		// A directory component did not exist, or was a file
		if ((dvp == NULL) || (error == ENOTDIR)) {
			dprintf("%s: failed to find dvp - or dvp is a file\n",
			    __func__);
			Irp->IoStatus.Information = FILE_DOES_NOT_EXIST;
			return (STATUS_OBJECT_NAME_NOT_FOUND);
		}
		dprintf("%s: failed to find vp in dvp\n", __func__);
		Irp->IoStatus.Information = FILE_DOES_NOT_EXIST;
		return (STATUS_OBJECT_NAME_NOT_FOUND);
	}

	// Streams
	// If we opened vp, grab its xattrdir, and try to to locate stream
	if (stream_name != NULL && vp != NULL) {
		// Here, we will release dvp, and attempt to open the xattr dir.
		// xattr dir will be the new dvp. Then we will look for
		// streamname in xattrdir, and assign vp.

		VERIFY3P(dvp, !=, vp);

		// Create the xattrdir only if we are to create a new entry
		zp = VTOZ(vp);
		if ((error = zfs_get_xattrdir(zp, &dzp, cr,
		    CreateFile ? CREATE_XATTR_DIR : 0))) {
			Irp->IoStatus.Information = FILE_DOES_NOT_EXIST;
			VN_RELE(vp);
			if (dvp && !dvp_no_rele)
				VN_RELE(dvp);
			dprintf("No xattr dir - and not creating one\n");
			return (STATUS_OBJECT_NAME_NOT_FOUND);
		}
		VN_RELE(vp);
		if (dvp && !dvp_no_rele)
			VN_RELE(dvp);
		vp = NULL;
		zp = NULL;
		dvp = ZTOV(dzp);
		int direntflags = 0; // To detect ED_CASE_CONFLICT
		error = zfs_dirlook(dzp, stream_name, &zp, FIGNORECASE,
		    &direntflags, NULL);
		if (!CreateFile && error) {
			zrele(dzp);
			Irp->IoStatus.Information = FILE_DOES_NOT_EXIST;
			dprintf("xattr dir - but no entry\n");
			return (STATUS_OBJECT_NAME_NOT_FOUND);
		}
		// Here, it may not exist, as we are to create it.
		// If it exists, keep vp, otherwise, it is NULL
		if (!error) {
			vp = ZTOV(zp);
		} // else vp is NULL from above

		finalname = stream_name;
	}

	if (OpenTargetDirectory) {
		if (dvp) {

#if 0
			// If we asked for PARENT of a non-existing file,
			// do we return error?
			if (vp == NULL) {
				dprintf("%s: opening PARENT dir, is ENOENT\n",
				    __func__);
				VN_RELE(dvp);
				Irp->IoStatus.Information = FILE_DOES_NOT_EXIST;
				return (STATUS_OBJECT_NAME_NOT_FOUND);
			}
#endif

			dprintf("%s: opening PARENT directory\n", __func__);
			zfs_couplefileobject(dvp, NULL, FileObject, 0ULL,
			    &zccb,
			    Irp->Overlay.AllocationSize.QuadPart,
			    DesiredAccess,
			    stream_name, Irp);
			if (DeleteOnClose)
				Status = zfs_setunlink_masked(FileObject, NULL);
			if (Status == STATUS_SUCCESS)
				Irp->IoStatus.Information = FILE_OPENED;

			if (vp) VN_RELE(vp);
			VN_RELE(dvp);
			return (Status);
		}
		ASSERT(vp == NULL);
		ASSERT(dvp == NULL);
		Irp->IoStatus.Information = FILE_DOES_NOT_EXIST;
		return (STATUS_OBJECT_NAME_NOT_FOUND);
	}

	// Here we have "dvp" of the directory.
	// "vp" if the final part was a file.

	// vv OPLOCK
	if (vp != NULL) {
		const uint32_t vp_usecount = atomic_load_32(&vp->v_usecount);
		uint64_t oplock_skip =
		    (uint64_t)Irp->Tail.Overlay.DriverContext[0];
		dprintf("oplock_skip is 0x%llx\n", oplock_skip);
		BOOLEAN skipCreate =
		    (oplock_skip == (OPLOCK_SKIP_MAGIC | OPLOCK_SKIP_CREATE));

		if (BooleanFlagOn(Options, FILE_RESERVE_OPFILTER)) {
			// Must be the first user handle on the stream.
			if (vp_usecount > 0) {
				VN_RELE(vp);
				VN_RELE(dvp);
				return (STATUS_OPLOCK_NOT_GRANTED);
			}
		}

		ExAcquireResourceExclusiveLite(vp->FileHeader.Resource, TRUE);

		Status = zfs_preflight_oplock_on_open_existing(
		    IrpSp->DeviceObject,
		    vp,
		    Irp,
		    IrpSp,
		    vp_usecount,
		    skipCreate);

		ExReleaseResourceLite(vp->FileHeader.Resource);

		if (Status == STATUS_PENDING) {
			VN_RELE(vp);
			VN_RELE(dvp);
			return (STATUS_PENDING);
		}

		if (Status == STATUS_OPLOCK_BREAK_IN_PROGRESS) {
			// FILE_COMPLETE_IF_OPLOCKED path: return as-is (!pend)
			// STATUS_OPLOCK_BREAK_IN_PROGRESS will pass
			// NT_SUCCESS() so everything better use it above us.
			// We continue to open the file as normal.
			return_break_in_progress = B_TRUE;
		}

		if (!NT_SUCCESS(Status)) {
			// STATUS_CANNOT_BREAK_OPLOCK from REQUIRING_OPLOCK,
			// or any other terminal error from FsRtl.
			VN_RELE(vp);
			VN_RELE(dvp);
			return (Status);
		}
	}

	// ^^ OPLOCK

	// Don't create if FILE_OPEN_IF (open existing)
	if ((CreateDisposition == FILE_OPEN_IF) && (vp != NULL))
		CreateDirectory = 0;

	// Fail if FILE_CREATE but file target exist
	if ((CreateDisposition == FILE_CREATE) && (vp != NULL)) {
		VN_RELE(vp);
		if (dvp)
			VN_RELE(dvp);
		Irp->IoStatus.Information = FILE_EXISTS;
		if (CreateDirectory && !vnode_isdir(vp))
			return (STATUS_NOT_A_DIRECTORY);
		return (STATUS_OBJECT_NAME_COLLISION); // create file error
	}

	// Fail if CreateDirectory, FILE_CREATE and dir target exists
	if (CreateDirectory &&
	    (CreateDisposition == FILE_CREATE) &&
	    (finalname == NULL)) {
		if (vp) // vp is probably NULL
			VN_RELE(vp);
		if (dvp)
			VN_RELE(dvp);
		Irp->IoStatus.Information = FILE_EXISTS;
		return (STATUS_OBJECT_NAME_COLLISION);
	}

	if (vp && vnode_unlink(vp)) {
		dprintf("%s: file marked unlinked error\n", __func__);
		VN_RELE(vp);
		if (dvp)
			VN_RELE(dvp);
		Irp->IoStatus.Information = FILE_DOES_NOT_EXIST;
		return (STATUS_DELETE_PENDING);
	}

	if (CreateDirectory && finalname) {

		if (TemporaryFile)
			return (STATUS_INVALID_PARAMETER);

		if (zfsvfs->z_rdonly || vfs_isrdonly(zfsvfs->z_vfs) ||
		    !spa_writeable(dmu_objset_spa(zfsvfs->z_os))) {
			VN_RELE(dvp);
			Irp->IoStatus.Information = 0; // ?
			return (STATUS_MEDIA_WRITE_PROTECTED);
		}

		zfs_setwinflags_xva(NULL,
		    IrpSp->Parameters.Create.FileAttributes, xvap);
		vap->va_type = VDIR;

		// Set default 777 if something else wasn't passed in
		if (!(vap->va_mask & ATTR_MODE))
			vap->va_mode = 0777;
		vap->va_mode |= S_IFDIR;
		vap->va_mask |= (ATTR_MODE | ATTR_TYPE);

		/* Set UID,GID from IRP security context for new ownership */
		zfs_security_context_pre(vap,
		    IrpSp->Parameters.Create.SecurityContext);

		/*
		 * Use SeAccessCheck on the parent's Windows SD as the
		 * authoritative access check for creates.  The ZFS POSIX ACL
		 * reflects Unix permissions that may not match Windows volume
		 * root expectations (e.g. $RECYCLE.BIN on a root-owned pool).
		 * When SeAccessCheck grants access, pass a uid=0 cred so the
		 * ZFS POSIX ACL check inside zfs_mkdir succeeds regardless of
		 * parent ownership.  Ownership of the new object comes from
		 * vap->va_uid set above by zfs_security_context_pre.
		 */
		cred_t elevated_mkdir_cr = { .cr_uid = 0, .cr_gid = 0 };
		cred_t *mkdir_cr = cr;
		PSECURITY_DESCRIPTOR parentSD = vnode_security(dvp);
		if (parentSD != NULL) {
			ACCESS_MASK grantedAccess;
			NTSTATUS accessStatus;
			PIO_SECURITY_CONTEXT sc =
			    IrpSp->Parameters.Create.SecurityContext;
			SeLockSubjectContext(
			    &sc->AccessState->SubjectSecurityContext);
			BOOLEAN winOK = SeAccessCheck(parentSD,
			    &sc->AccessState->SubjectSecurityContext,
			    TRUE, FILE_ADD_SUBDIRECTORY, 0, NULL,
			    IoGetFileObjectGenericMapping(),
			    IrpSp->Flags & SL_FORCE_ACCESS_CHECK ?
			    UserMode : Irp->RequestorMode,
			    &grantedAccess, &accessStatus);
			SeUnlockSubjectContext(
			    &sc->AccessState->SubjectSecurityContext);
			if (!winOK) {
				dprintf("%s: SeAccessCheck denied mkdir\n",
				    __func__);
				if (vp != NULL)
					VN_RELE(vp);
				VN_RELE(dvp);
				return (accessStatus);
			}
			mkdir_cr = &elevated_mkdir_cr;
		}

		/* If parent is CaseSensitive, sub-Dir should be too */
		if (VTOZ(dvp)->z_pflags & ZFS_CASESENSITIVEDIR) {
			xoptattr_t *xoap;
			xoap = xva_getxoptattr(xvap);
			xoap->xoa_case_sensitive_dir = 1;
			XVA_SET_REQ(xvap, XAT_CASESENSITIVEDIR);
		}

		ASSERT(strchr(finalname, '\\') == NULL);
		error = zfs_mkdir(VTOZ(dvp), finalname, vap, &zp, mkdir_cr,
		    flags, NULL);
		if (error == 0) {
			vp = ZTOV(zp);
			zfs_couplefileobject(vp, NULL, FileObject, 0ULL,
			    &zccb,
			    Irp->Overlay.AllocationSize.QuadPart,
			    DesiredAccess,
			    stream_name, Irp);

			if (DeleteOnClose)
				Status = zfs_setunlink_masked(FileObject, dvp);

			if (NT_SUCCESS(Status)) {

				Irp->IoStatus.Information = FILE_CREATED;

				IoSetShareAccess(
				    DesiredAccess,
				    IrpSp->Parameters.Create.ShareAccess,
				    FileObject,
				    &vp->share_access);

				// Merge SecurityDescriptors if given one.
				zfs_security_context_post(vp, dvp,
				    IrpSp->Parameters.Create.SecurityContext);

				zfs_send_notify(zfsvfs, zccb->z_name_cache,
				    zccb->z_name_offset,
				    FILE_NOTIFY_CHANGE_DIR_NAME,
				    FILE_ACTION_ADDED);
			}
			VN_RELE(vp);
			VN_RELE(dvp);
			return (Status);
		}
		dprintf("%s: zfs_mkdir('%s') failed error %d\n",
		    __func__, finalname ? finalname : "(null)", error);
		VN_RELE(dvp);
		Irp->IoStatus.Information = FILE_DOES_NOT_EXIST;
		switch (error) {
		case ENOSPC:
		case EDQUOT:
			return (STATUS_DISK_FULL);
		case EACCES:
		case EPERM:
			return (STATUS_ACCESS_DENIED);
		case EROFS:
			return (STATUS_MEDIA_WRITE_PROTECTED);
		case EEXIST:
			Irp->IoStatus.Information = FILE_EXISTS;
			return (STATUS_OBJECT_NAME_COLLISION);
		case ENAMETOOLONG:
			return (STATUS_OBJECT_NAME_INVALID);
		case EIO:
			return (STATUS_IO_DEVICE_ERROR);
		default:
			dprintf("%s: zfs_mkdir unhandled error %d -> "
			    "STATUS_UNEXPECTED_IO_ERROR\n", __func__, error);
			return (STATUS_UNEXPECTED_IO_ERROR);
		}
	}

	// If they requested just directory, fail non directories
	if (DirectoryFile && vp != NULL && !vnode_isdir(vp)) {
		dprintf("%s: asked for directory but found file\n", __func__);
		VN_RELE(vp);
		if (dvp)
			VN_RELE(dvp);
		Irp->IoStatus.Information = FILE_DOES_NOT_EXIST;
		return (STATUS_FILE_IS_A_DIRECTORY);
	}

	// Asked for non-directory, but we got directory
	if (NonDirectoryFile && !CreateFile && vp == NULL) {
		dprintf("%s: asked for file but found directory\n", __func__);
		if (dvp)
			VN_RELE(dvp);
		Irp->IoStatus.Information = FILE_DOES_NOT_EXIST;
		return (STATUS_FILE_IS_A_DIRECTORY);
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
		if (((zp->z_pflags&ZFS_HIDDEN) &&
		    !FlagOn(IrpSp->Parameters.Create.FileAttributes,
		    FILE_ATTRIBUTE_HIDDEN)) ||
		    ((zp->z_pflags&ZFS_SYSTEM) &&
		    !FlagOn(IrpSp->Parameters.Create.FileAttributes,
		    FILE_ATTRIBUTE_SYSTEM))) {
			VN_RELE(vp);
			if (dvp)
				VN_RELE(dvp);
			dprintf("%s: denied due to hidden+system combo\n",
			    __func__);
			return (STATUS_ACCESS_DENIED);
		}
	}

	// If overwrite, and tagged readonly, fail
	// (note, supersede should succeed)
	if ((zp != NULL) &&
	    ((CreateDisposition == FILE_OVERWRITE) ||
	    (CreateDisposition == FILE_OVERWRITE_IF))) {
		if (zp->z_pflags&ZFS_READONLY) {
			VN_RELE(vp);
			VN_RELE(dvp);
			dprintf("%s: denied due to ZFS_READONLY + OVERWRITE\n",
			    __func__);
			return (STATUS_ACCESS_DENIED);
		}
	}

	// If flags are readonly, and tries to open with write, fail
	if ((zp != NULL) &&
	    (DesiredAccess&(FILE_WRITE_DATA | FILE_APPEND_DATA)) &&
	    (zp->z_pflags&ZFS_READONLY)) {
		VN_RELE(vp);
		VN_RELE(dvp);
		dprintf("%s: denied due to ZFS_READONLY + WRITE_DATA\n",
		    __func__);
		return (STATUS_ACCESS_DENIED);
	}


	if (DeleteOnClose &&
	    vp && zp &&
	    dvp && VTOZ(dvp) &&
	    !zfsctl_is_node(VTOZ(dvp)) &&
	    zfs_zaccess_delete(VTOZ(dvp), zp, 0) > 0) {
			VN_RELE(vp);
			if (dvp)
				VN_RELE(dvp);

			dprintf("%s: denied due to IMMUTABLE+NOUNLINK\n",
			    __func__);
			return (STATUS_ACCESS_DENIED);
	}


	// Some cases we always create the file, and sometimes only if
	// it is not there. If the file exists and we are only to create
	// the file if it is not there:
	if ((CreateDisposition == FILE_OPEN_IF) && (vp != NULL))
		CreateFile = 0;


	if (vp || CreateFile == 0) {
//		NTSTATUS Status;

		// Streams do not call SeAccessCheck?
		if (stream_name != NULL && vp != NULL) {
			IoSetShareAccess(
			    DesiredAccess,
			    IrpSp->Parameters.Create.ShareAccess,
			    FileObject, vp ? &vp->share_access :
			    &dvp->share_access);

		} else if (
		    DesiredAccess != 0 && vp) {

			SeLockSubjectContext(
			    &IrpSp->Parameters.Create.SecurityContext->
			    AccessState->SubjectSecurityContext);
#if 0
			if (!FileOpenReparsePoint &&
			    !SeAccessCheck(vnode_security(vp ? vp : dvp),
			    &IrpSp->Parameters.Create.SecurityContext->
			    AccessState->SubjectSecurityContext,
			    TRUE,
			    DesiredAccess,
			    0, NULL,
			    IoGetFileObjectGenericMapping(),
			    IrpSp->Flags & SL_FORCE_ACCESS_CHECK ? UserMode :
			    Irp->RequestorMode,
			    &granted_access, &Status)) {
				SeUnlockSubjectContext(
				    &IrpSp->Parameters.Create.SecurityContext->
				    AccessState->SubjectSecurityContext);
				if (vp) VN_RELE(vp);
				VN_RELE(dvp);
				dprintf("%s: denied due to SeAccessCheck()\n",
				    __func__);
				DUMP_SD(vnode_security(vp ? vp : dvp));
				if (NT_SUCCESS(Status) &&
				    return_break_in_progress)
					Status =
					    STATUS_OPLOCK_BREAK_IN_PROGRESS;
				return (Status);
			}
#endif
			SeUnlockSubjectContext(
			    &IrpSp->Parameters.Create.SecurityContext->
			    AccessState->SubjectSecurityContext);
		} else {
			granted_access = 0;
		}

		// Io*ShareAccess(): X is not an atomic operation. Therefore,
		// drivers calling this routine must protect the shared
		// file object
		vnode_lock(vp ? vp : dvp);
		if (vnode_isinuse(vp ? vp : dvp, 0)) {
// 0 is we are the only (usecount added below), 1+ if already open.
			Status = IoCheckShareAccess(granted_access,
			    IrpSp->Parameters.Create.ShareAccess, FileObject,
			    vp ? &vp->share_access : &dvp->share_access, FALSE);
			if (!NT_SUCCESS(Status)) {
				vnode_unlock(vp ? vp : dvp);
				if (vp) VN_RELE(vp);
				VN_RELE(dvp);
				dprintf("%s: denied IoCheckShareAccess\n",
				    __func__);
				return (Status);
			}
			IoUpdateShareAccess(FileObject,
			    vp ? &vp->share_access : &dvp->share_access);
		} else {
			IoSetShareAccess(granted_access,
			    IrpSp->Parameters.Create.ShareAccess,
			    FileObject,
			    vp ? &vp->share_access : &dvp->share_access);
		}
		// Since we've updated ShareAccess here, if we cancel
		// the open we need to undo it.
		UndoShareAccess = TRUE;
		vnode_unlock(vp ? vp : dvp);
	}

#define	UNDO_SHARE_ACCESS(vp) \
	if ((vp) && UndoShareAccess) {    \
		vnode_lock((vp));     \
		IoRemoveShareAccess(FileObject, &(vp)->share_access); \
		vnode_unlock((vp));   \
	}


	/*
	 * Check an existing file before zfs_create() can change its attributes
	 * and before coupling the FileObject.  The size helper repeats the
	 * truncation check under PagingIoResource to cover a new mapping that
	 * appears after this preflight.
	 */
	if (vp && (CreateDisposition == FILE_SUPERSEDE ||
	    CreateDisposition == FILE_OVERWRITE ||
	    CreateDisposition == FILE_OVERWRITE_IF)) {
		LARGE_INTEGER zero_size = { .QuadPart = 0 };
		Status = STATUS_SUCCESS;
		if (zfsvfs->z_rdonly || vfs_isrdonly(zfsvfs->z_vfs) ||
		    !spa_writeable(dmu_objset_spa(zfsvfs->z_os)))
			Status = STATUS_MEDIA_WRITE_PROTECTED;
		else if (!MmFlushImageSection(&vp->SectionObjectPointers,
		    MmFlushForWrite))
			Status = STATUS_SHARING_VIOLATION;
		else if (!MmCanFileBeTruncated(&vp->SectionObjectPointers,
		    &zero_size))
			Status = STATUS_USER_MAPPED_FILE;

		if (!NT_SUCCESS(Status)) {
			UNDO_SHARE_ACCESS(vp);
			VN_RELE(vp);
			VN_RELE(dvp);
			Irp->IoStatus.Information = 0;
			return (Status);
		}
	}

	// We can not DeleteOnClose if readonly filesystem
	if (DeleteOnClose) {
		if (zfsvfs->z_rdonly || vfs_isrdonly(zfsvfs->z_vfs) ||
		    !spa_writeable(dmu_objset_spa(zfsvfs->z_os))) {
			UNDO_SHARE_ACCESS(vp);
			if (vp) VN_RELE(vp);
			VN_RELE(dvp);
			Irp->IoStatus.Information = 0; // ?
			return (STATUS_MEDIA_WRITE_PROTECTED);
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
			return (STATUS_MEDIA_WRITE_PROTECTED);
		}

		// Would we replace file?
		if (vp) {
			VN_RELE(vp);
			vp = NULL;
			replacing = 1;
		}

		zfs_setwinflags_xva(NULL,
		    IrpSp->Parameters.Create.FileAttributes, xvap);
		vap->va_type = VREG;

		if (!(vap->va_mask & ATTR_MODE))
			vap->va_mode = 0777 | S_IFREG;
		vap->va_mask |= (ATTR_MODE | ATTR_TYPE);

		/*
		 * Defer overwrite truncation until FileObject is coupled.  A
		 * zfs_create(ATTR_SIZE=0) truncates z_size, but on Windows
		 * vnode_pager_setsize() is a no-op. FileHeader and Cc retain
		 * the previous size, and dirty pages can restore the old tail.
		 */
		if (replacing && (CreateDisposition == FILE_SUPERSEDE ||
		    CreateDisposition == FILE_OVERWRITE_IF))
			vap->va_mask &= ~ATTR_SIZE;

		/* Set UID,GID from IRP security context for new ownership */
		zfs_security_context_pre(vap,
		    IrpSp->Parameters.Create.SecurityContext);

		/*
		 * Use SeAccessCheck on the parent's Windows SD as the
		 * authoritative access check (same rationale as mkdir path).
		 */
		cred_t elevated_create_cr = { .cr_uid = 0, .cr_gid = 0 };
		cred_t *create_cr = cr;
		PSECURITY_DESCRIPTOR create_parentSD = vnode_security(dvp);
		if (create_parentSD != NULL) {
			ACCESS_MASK grantedAccess;
			NTSTATUS accessStatus;
			PIO_SECURITY_CONTEXT sc =
			    IrpSp->Parameters.Create.SecurityContext;
			SeLockSubjectContext(
			    &sc->AccessState->SubjectSecurityContext);
			BOOLEAN winOK = SeAccessCheck(create_parentSD,
			    &sc->AccessState->SubjectSecurityContext,
			    TRUE, FILE_ADD_FILE, 0, NULL,
			    IoGetFileObjectGenericMapping(),
			    IrpSp->Flags & SL_FORCE_ACCESS_CHECK ?
			    UserMode : Irp->RequestorMode,
			    &grantedAccess, &accessStatus);
			SeUnlockSubjectContext(
			    &sc->AccessState->SubjectSecurityContext);
			if (!winOK) {
				dprintf("%s: SeAccessCheck denied create\n",
				    __func__);
				if (vp != NULL)
					VN_RELE(vp);
				VN_RELE(dvp);
				return (accessStatus);
			}
			create_cr = &elevated_create_cr;
		}

		/* O_EXCL only if FILE_CREATE */
		error = zfs_create(VTOZ(dvp), finalname, vap,
		    CreateDisposition == FILE_CREATE, vap->va_mode,
		    &zp, create_cr, flags, NULL);
		if (error == 0) {
			boolean_t reenter_for_xattr = B_FALSE;

			// if (!hackvp)
			//	hackvp = ZTOV(zp);

			// Creating two things? Don't attach until 2nd item.
			if (!(zp->z_pflags & ZFS_XATTR) && stream_name != NULL)
				reenter_for_xattr = B_TRUE;

			vp = ZTOV(zp);

			if (!reenter_for_xattr) {
				uint64_t allocation_size =
				    Irp->Overlay.AllocationSize.QuadPart;
				zfs_couplefileobject(vp, dvp, FileObject,
				    zp ? zp->z_size : 0ULL, &zccb,
				    allocation_size,
				    granted_access ?
				    granted_access : DesiredAccess,
				    stream_name, Irp);
				if (replacing && (CreateDisposition ==
				    FILE_SUPERSEDE || CreateDisposition ==
				    FILE_OVERWRITE_IF))
					Status = zfs_truncate_open_file(
					    IrpSp->DeviceObject, FileObject,
					    allocation_size);

				if (NT_SUCCESS(Status) && DeleteOnClose)
					Status =
					    zfs_setunlink_masked(FileObject,
					    dvp);

				if (!NT_SUCCESS(Status)) {
					UNDO_SHARE_ACCESS(vp);
					Irp->IoStatus.Information = 0;
					goto create_done;
				}

				Irp->IoStatus.Information = replacing ?
				    CreateDisposition == FILE_SUPERSEDE ?
				    FILE_SUPERSEDED : FILE_OVERWRITTEN :
				    FILE_CREATED;

				vnode_lock(vp);
				IoSetShareAccess(
				    DesiredAccess,
				    IrpSp->Parameters.Create.ShareAccess,
				    FileObject,
				    &vp->share_access);
				vnode_unlock(vp);

				// Did we create file, or stream?
				if (!(zp->z_pflags & ZFS_XATTR)) {

					// Merge SecurityDescriptors
					zfs_security_context_post(vp, dvp,
					    IrpSp->Parameters.Create.
					    SecurityContext);

					zfs_send_notify(zfsvfs,
					    zccb->z_name_cache,
					    zccb->z_name_offset,
					    FILE_NOTIFY_CHANGE_FILE_NAME,
					    FILE_ACTION_ADDED);
				} else {

					zfs_send_notify_stream(zfsvfs, // WOOT
					    zccb->z_name_cache,
					    zccb->z_name_offset,
					    FILE_NOTIFY_CHANGE_STREAM_NAME,
					    FILE_ACTION_ADDED_STREAM,
					    NULL);
				}

			create_done:
			}

			if (NT_SUCCESS(Status) && return_break_in_progress)
				Status = STATUS_OPLOCK_BREAK_IN_PROGRESS;

		/* Windows lets you create a file, and stream, in one. */
		/* Call this function again, lets hope, only once */
			if (NT_SUCCESS(Status) && reenter_for_xattr) {
				Status = EAGAIN;
			}

			VN_RELE(vp);
			VN_RELE(dvp);

			return (Status);
		}
		if (error == EEXIST)
			Irp->IoStatus.Information = FILE_EXISTS;
		else
			Irp->IoStatus.Information = FILE_DOES_NOT_EXIST;

		dprintf("%s: zfs_create('%s') failed error %d\n",
		    __func__, finalname ? finalname : "(null)", error);

		UNDO_SHARE_ACCESS(dvp);
		VN_RELE(dvp);
		switch (error) {
		case ENOSPC:
			return (STATUS_DISK_FULL);
		case EDQUOT:
			return (STATUS_DISK_FULL);
			// return (STATUS_DISK_QUOTA_EXCEEDED);
		case EACCES:
		case EPERM:
			return (STATUS_ACCESS_DENIED);
		case EROFS:
			return (STATUS_MEDIA_WRITE_PROTECTED);
		case EEXIST:
			return (STATUS_OBJECT_NAME_COLLISION);
		case ENOENT:
			return (STATUS_OBJECT_NAME_NOT_FOUND);
		case ENOTDIR:
			return (STATUS_NOT_A_DIRECTORY);
		case ENAMETOOLONG:
			return (STATUS_OBJECT_NAME_INVALID);
		case EIO:
			return (STATUS_IO_DEVICE_ERROR);
		default:
			/* Log unexpected error so it can be mapped properly */
			dprintf("%s: zfs_create unhandled error %d -> "
			    "STATUS_UNEXPECTED_IO_ERROR\n", __func__, error);
			return (STATUS_UNEXPECTED_IO_ERROR);
		}
	}


	// Just open it, if the open was to a directory, add ccb
	ASSERT(IrpSp->FileObject->FsContext == NULL);
	if (vp == NULL) {
		zfs_couplefileobject(dvp, NULL, FileObject, 0ULL,
		    &zccb,
		    Irp->Overlay.AllocationSize.QuadPart,
		    granted_access ? granted_access : DesiredAccess,
		    stream_name, Irp);

		if (DeleteOnClose)
			Status = zfs_setunlink_masked(FileObject, NULL);

		if (NT_SUCCESS(Status)) {
			if (UndoShareAccess == FALSE) {
				vnode_lock(dvp);
				IoSetShareAccess(
				    DesiredAccess,
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

		// Technically, this should call zfs_open() -
		// but zfs_open is mostly empty

		zfs_couplefileobject(vp, dvp, FileObject, zp->z_size,
		    &zccb,
		    Irp->Overlay.AllocationSize.QuadPart,
		    granted_access ? granted_access : DesiredAccess,
		    stream_name, Irp);

		// Now that vp is set, check delete
		if (DeleteOnClose)
			Status = zfs_setunlink_masked(FileObject, dvp);

		if (NT_SUCCESS(Status)) {

			Irp->IoStatus.Information = FILE_OPENED;

			// If we are to truncate the file:
			if (CreateDisposition == FILE_OVERWRITE) {
				Status = zfs_truncate_open_file(
				    IrpSp->DeviceObject, FileObject,
				    Irp->Overlay.AllocationSize.QuadPart);
				if (!NT_SUCCESS(Status)) {
					UNDO_SHARE_ACCESS(vp);
					Irp->IoStatus.Information = 0;
					goto open_done;
				}
				zp->z_pflags |= ZFS_ARCHIVE;
				Irp->IoStatus.Information = FILE_OVERWRITTEN;
			}

			// If we created something new, add this permission.
			if (UndoShareAccess == FALSE) {
				vnode_lock(vp);
				IoSetShareAccess(
				    DesiredAccess,
				    IrpSp->Parameters.Create.ShareAccess,
				    FileObject,
				    &vp->share_access);
				vnode_unlock(vp);
			}
		} else {
			UNDO_SHARE_ACCESS(vp);
			Irp->IoStatus.Information = 0;
		}
	open_done:
		VN_RELE(vp);
		VN_RELE(dvp);
	}

	IrpSp->Parameters.Create.SecurityContext->AccessState->
	    PreviouslyGrantedAccess |= granted_access;
	IrpSp->Parameters.Create.SecurityContext->AccessState->
	    RemainingDesiredAccess &= ~(granted_access | MAXIMUM_ALLOWED);

	if (NT_SUCCESS(Status) && return_break_in_progress)
		Status = STATUS_OPLOCK_BREAK_IN_PROGRESS;

	return (Status);
}

int
zfs_vnop_lookup(PIRP Irp, PIO_STACK_LOCATION IrpSp, mount_t *zmo)
{
	int status;
	char *filename = NULL;
	xvattr_t xva = { 0 };
	vattr_t *vap = &xva.xva_vattr;

	// Check the EA buffer is good, if supplied.
	if (Irp->AssociatedIrp.SystemBuffer != NULL &&
	    IrpSp->Parameters.Create.EaLength > 0) {
		ULONG offset;
		status = IoCheckEaBufferValidity(
		    Irp->AssociatedIrp.SystemBuffer,
		    IrpSp->Parameters.Create.EaLength, &offset);
		if (!NT_SUCCESS(status)) {
			dprintf("IoCheckEaBufferValidity returned %08x "
			    "(error at offset %lu)\n", status, offset);
			return (status);
		}
	}

	// Allocate space to hold name, must be freed from here on
	filename = kmem_alloc(PATH_MAX, KM_SLEEP);

	// Deal with ExtraCreateParameters
#if defined(NTDDI_WIN10_RS5) && (NTDDI_VERSION >= NTDDI_WIN10_RS5)
	/* Check for ExtraCreateParameters */
	PECP_LIST ecp = NULL;
	ATOMIC_CREATE_ECP_CONTEXT *acec = NULL;
	PQUERY_ON_CREATE_ECP_CONTEXT qocContext = NULL;
	FsRtlGetEcpListFromIrp(Irp, &ecp);
	if (ecp) {
		GUID ecpType;
		void *ecpContext = NULL;
		ULONG ecpContextSize;
		while (NT_SUCCESS(FsRtlGetNextExtraCreateParameter(ecp,
		    ecpContext, &ecpType, &ecpContext, &ecpContextSize))) {
			if (IsEqualGUID(&ecpType, &GUID_ECP_ATOMIC_CREATE)) {
				dprintf("GUID_ECP_ATOMIC_CREATE\n");
				// More code to come here:
				acec = ecpContext;
			} else if (IsEqualGUID(&ecpType,
			    &GUID_ECP_QUERY_ON_CREATE)) {
				dprintf("GUID_ECP_QUERY_ON_CREATE\n");
				// It wants a getattr call on success,
				// before we finish up
				qocContext =
				    (PQUERY_ON_CREATE_ECP_CONTEXT)ecpContext;
			} else if (IsEqualGUID(&ecpType,
			    &GUID_ECP_CREATE_REDIRECTION)) {
				dprintf("GUID_ECP_CREATE_REDIRECTION\n");
				// We get this one a lot.
			} else {
				dprintf("Other GUID_ECP type\n");
// IopSymlinkECPGuid "73d5118a-88ba-439f-92f4-46d38952d250"
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
		for (ea =
		    (PFILE_FULL_EA_INFORMATION)Irp->AssociatedIrp.SystemBuffer;
		    /* empty */;
		    ea = (PFILE_FULL_EA_INFORMATION)((uint8_t *)ea +
		    ea->NextEntryOffset)) {
			// only parse $LX attrs right now -- things we can store
			// before the file gets created.
			if (vattr_apply_lx_ea(vap, ea)) {
				dprintf("encountered special attrs EA '%.*s'\n",
				    ea->EaNameLength, ea->EaName);
			}
			if (ea->NextEntryOffset == 0)
				break;
		}
	}

	/*
	 * Time Warp: intercept \@GMT-YYYY.MM.DD-HH.MM.SS\... paths.
	 * Windows SMB (srv2.sys) and Explorer pass these when a user
	 * opens a Previous Versions file.  We map the UTC timestamp to the
	 * nearest ZFS snapshot at-or-before that time and issue a
	 * STATUS_REPARSE redirect to \Device\ZfsSnapshot<hex16>\<rest>.
	 *
	 * \@GMT-YYYY.MM.DD-HH.MM.SS is exactly 25 WCHARs.  After that, the
	 * remainder of the path (including its leading \) is passed via the
	 * REPARSE_DATA_BUFFER Reserved field so the IO Manager appends it
	 * to the substitute device name on re-parse.
	 */
	{
		PUNICODE_STRING gmt_fn = &IrpSp->FileObject->FileName;
		if (gmt_fn->Length >= 25 * sizeof (WCHAR) &&
		    gmt_fn->Buffer[0] == L'\\' &&
		    gmt_fn->Buffer[1] == L'@' &&
		    gmt_fn->Buffer[2] == L'G' &&
		    gmt_fn->Buffer[3] == L'M' &&
		    gmt_fn->Buffer[4] == L'T' &&
		    gmt_fn->Buffer[5] == L'-') {
			WCHAR *gb = gmt_fn->Buffer;
			TIME_FIELDS gmt_tf;
			RtlZeroMemory(&gmt_tf, sizeof (gmt_tf));
			gmt_tf.Year   = (CSHORT)(
			    (gb[6] - L'0') * 1000 + (gb[7] - L'0') * 100 +
			    (gb[8] - L'0') * 10   + (gb[9] - L'0'));
			gmt_tf.Month  = (CSHORT)(
			    (gb[11] - L'0') * 10 + (gb[12] - L'0'));
			gmt_tf.Day    = (CSHORT)(
			    (gb[14] - L'0') * 10 + (gb[15] - L'0'));
			gmt_tf.Hour   = (CSHORT)(
			    (gb[17] - L'0') * 10 + (gb[18] - L'0'));
			gmt_tf.Minute = (CSHORT)(
			    (gb[20] - L'0') * 10 + (gb[21] - L'0'));
			gmt_tf.Second = (CSHORT)(
			    (gb[23] - L'0') * 10 + (gb[24] - L'0'));

			LARGE_INTEGER gmt_li;
			if (RtlTimeFieldsToTime(&gmt_tf, &gmt_li)) {
				uint64_t unix_ts = (uint64_t)
				    ((gmt_li.QuadPart -
				    116444736000000000LL) / 10000000LL);

				uint64_t snap_guid = zfs_vss_find_by_time(
				    unix_ts, (const char *)zmo->ascii_name);

				dprintf("%s: @GMT %04d.%02d.%02d-%02d.%02d.%02d"
				    " -> unix %llu guid %016llx\n", __func__,
				    gmt_tf.Year, gmt_tf.Month, gmt_tf.Day,
				    gmt_tf.Hour, gmt_tf.Minute, gmt_tf.Second,
				    (unsigned long long)unix_ts,
				    (unsigned long long)snap_guid);

				if (snap_guid != 0) {
					/*
					 * Build \Device\ZfsSnapshot<hex16>
					 * (35 WCHARs = 70 bytes).
					 */
					static const WCHAR pfx[] =
					    L"\\Device\\ZfsSnapshot";
					static const WCHAR hexch[] =
					    L"0123456789abcdef";
					WCHAR subst[36]; /* 35 + NUL */
					RtlCopyMemory(subst, pfx,
					    19 * sizeof (WCHAR));
					for (int hi = 0; hi < 16; hi++) {
						subst[19 + hi] = hexch[
						    (snap_guid >>
						    (60 - hi * 4)) & 0xf];
					}
					subst[35] = L'\0';

					/* Remaining path after \@GMT-...\  */
					USHORT rem = (gmt_fn->Length >
					    25 * sizeof (WCHAR)) ?
					    gmt_fn->Length -
					    25 * sizeof (WCHAR) :
					    sizeof (WCHAR);

					/* Allocate REPARSE_DATA_BUFFER */
					USHORT subst_b = 35 * sizeof (WCHAR);
					USHORT rdb_hdr =
					    REPARSE_DATA_BUFFER_HEADER_SIZE;
					ULONG rpb_sz = rdb_hdr +
					    4 * sizeof (USHORT) + subst_b;
					REPARSE_DATA_BUFFER *rpb =
					    ExAllocatePoolWithTag(
					    PagedPool, rpb_sz, '!TZR');
					if (rpb != NULL) {
						rpb->ReparseTag =
						    IO_REPARSE_TAG_MOUNT_POINT;
						rpb->ReparseDataLength =
						    (USHORT)(rpb_sz - rdb_hdr);
						rpb->Reserved = rem;
						rpb->MountPointReparseBuffer.
						    SubstituteNameOffset = 0;
						rpb->MountPointReparseBuffer.
						    SubstituteNameLength =
						    subst_b;
						rpb->MountPointReparseBuffer.
						    PrintNameOffset = subst_b;
						rpb->MountPointReparseBuffer.
						    PrintNameLength = 0;
						RtlCopyMemory(rpb->
						    MountPointReparseBuffer.
						    PathBuffer,
						    subst, subst_b);

						Irp->IoStatus.Information =
						    IO_REPARSE_TAG_MOUNT_POINT;
						Irp->Tail.Overlay.
						    AuxiliaryBuffer =
						    (void *)rpb;
						kmem_free(filename, PATH_MAX);
						return (STATUS_REPARSE);
					}
				}
			}
		}
	}

	do {

		// Call ZFS
		status = zfs_vnop_lookup_impl(Irp, IrpSp, zmo, filename, &xva);

	} while (status == EAGAIN);

#if defined(NTDDI_WIN10_RS5) && (NTDDI_VERSION >= NTDDI_WIN10_RS5)
	// Did ECP ask for getattr to be returned? None, one or both can be set.
	// This requires vnode_couplefileobject() was called
	if (NT_SUCCESS(status) && qocContext && IrpSp->FileObject->FsContext) {

		ULONG classes = 0;

		// Handle RS5 >= version < 19H1 when the struct had "Flags".
#if defined(NTDDI_WIN10_19H1) && (NTDDI_VERSION >= NTDDI_WIN10_19H1)
		classes = qocContext->RequestedClasses;
#else
		classes = qocContext->Flags;
#endif

		if (BooleanFlagOn(classes, QoCFileStatInformation)) {
			file_stat_information(IrpSp->DeviceObject, Irp, IrpSp,
			    &qocContext->StatInformation);
		}
		if (BooleanFlagOn(classes, QoCFileLxInformation)) {
			file_stat_lx_information(IrpSp->DeviceObject, Irp,
			    IrpSp, &qocContext->LxInformation);
		}
		if (BooleanFlagOn(classes, QoCFileEaInformation)) {
			dprintf("%s: unsupported QoC: QoCFileEaInformation\n");
		}
#if defined(NTDDI_WIN10_19H1) && (NTDDI_VERSION >= NTDDI_WIN10_19H1)
		// We should fill this in, right? Only set those we understand.
		qocContext->ClassesProcessed =
		    classes & (QoCFileStatInformation|QoCFileLxInformation);
		qocContext->ClassesWithErrors = 0;
		qocContext->ClassesWithNoData = 0;
#endif

		FsRtlAcknowledgeEcp(qocContext);
	}

	if (NT_SUCCESS(status) && acec && acec->
	    InFlags & ATOMIC_CREATE_ECP_IN_FLAG_REPARSE_POINT_SPECIFIED) {
		panic("Implement me: atomic reparse point");
		// acec->OutFlags |=
		// 	ATOMIC_CREATE_ECP_OUT_FLAG_REPARSE_POINT_SET;
	}
#endif

	// Now handle proper EAs properly
	if (NT_SUCCESS(status)) {
		if (Irp->AssociatedIrp.SystemBuffer &&
		    IrpSp->FileObject->FsContext) {
			// Second pass: this will apply all EAs that are
			// not only LX EAs
			vnode_apply_eas(IrpSp->FileObject->FsContext,
			    IrpSp->FileObject->FsContext2,
			    (PFILE_FULL_EA_INFORMATION)
			    Irp->AssociatedIrp.SystemBuffer,
			    IrpSp->Parameters.Create.EaLength, NULL);
		}

		if (!BooleanFlagOn(IrpSp->Parameters.Create.Options,
		    FILE_NO_INTERMEDIATE_BUFFERING)) {
			IrpSp->FileObject->Flags |= FO_CACHE_SUPPORTED;
		}

	}

	// Free filename
	kmem_free(filename, PATH_MAX);

	dprintf("%s: %s with %s\n", __func__,
	    common_status_str(status),
	    create_reply(status, Irp->IoStatus.Information));

	return (status);
}


/*
 * reclaim is called when a vnode is to be terminated,
 * VFS (spl-vnode.c) will hold iocount == 1, usecount == 0
 * so release associated ZFS node, and free everything
 */
int
zfs_vnop_reclaim(struct vnode *vp)
{
	znode_t *zp = VTOZ(vp);
	if (zp == NULL) {
		ASSERT("NULL zp in reclaim?");
		return (0);
	}

	zfsvfs_t *zfsvfs = zp->z_zfsvfs;

	dprintf("  zfs_vnop_recycle: releasing zp %p and vp %p\n", zp, vp);

	// Decouple the nodes
	ASSERT(ZTOV(zp) != (vnode_t *)0xdeadbeefdeadbeef);

	mutex_enter(&zp->z_lock);
	// lost the race?
	if (VTOZ(vp) == NULL) {
		mutex_exit(&zp->z_lock);
		return (0);
	}
	ZTOV(zp) = NULL;
	vnode_clearfsnode(vp); /* vp->v_data = NULL */
	mutex_exit(&zp->z_lock);
	// vnode_removefsref(vp); /* ADDREF from vnode_create */

	void *sd = vnode_security(vp);
	if (sd != NULL)
		ExFreePool(sd);
	vnode_setsecurity(vp, NULL);

	vp = NULL;

	// Release znode
	/*
	 * This will release as much as it can, based on reclaim_reentry,
	 * if we are from fastpath, we do not call free here, as zfs_remove
	 * calls zfs_znode_delete() directly.
	 * zfs_zinactive() will leave earlier if z_reclaim_reentry is true.
	 */
	rw_enter(&zfsvfs->z_teardown_inactive_lock, RW_READER);
	if (zp->z_sa_hdl == NULL) {
		zfs_znode_free(zp);
	} else {
		zfs_zinactive(zp);
		zfs_znode_free(zp);
	}
	rw_exit(&zfsvfs->z_teardown_inactive_lock);

	atomic_dec_64(&vnop_num_vnodes);
	atomic_inc_64(&vnop_num_reclaims);

	if (vnop_num_vnodes % 1000 == 0)
		dprintf("%s: num_vnodes %llu\n", __func__, vnop_num_vnodes);

	return (0);
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
 * This function uses zp->z_zfsvfs, zp->z_mode, zp->z_flags, zp->z_id
 * and sets zp->z_vnode and zp->z_vid.
 * If given parent, dzp, we can save some hassles. If not, looks it
 * up internally.
 */
int
zfs_znode_getvnode(znode_t *zp, znode_t *dzp, zfsvfs_t *zfsvfs)
{
	struct vnode *vp = NULL;
	int flags = 0;
	struct vnode *parentvp = NULL;
	// dprintf("getvnode zp %p with vp %p zfsvfs %p vfs %p\n", zp, vp,
	//    zfsvfs, zfsvfs->z_vfs);

	if (zp->z_vnode)
		panic("zp %p vnode already set\n", zp->z_vnode);

	// "root" / mountpoint holds long term ref
	if (zp->z_id == zfsvfs->z_root) {
		flags |= VNODE_MARKROOT;
	} else {

		/*
		 * To maintain a well-defined vnode tree,
		 * we need the parent here.
		 * This could cascade?
		 * Ah so unlinkeddrain zget() will NOT
		 * have parents, so we need to let those pass.
		 * Also, nothing seems to check the returncode.
		 */
		if (dzp != NULL)
			parentvp = ZTOV(dzp);
		if (parentvp != NULL) {
			VERIFY0(VN_HOLD(parentvp));
		} else {
			uint64_t parent;
			znode_t *parentzp;
			VERIFY(sa_lookup(zp->z_sa_hdl, SA_ZPL_PARENT(zfsvfs),
			    &parent, sizeof (parent)) == 0);
			if (zfs_zget(zfsvfs, parent, &parentzp) == 0) {
				parentvp = ZTOV(parentzp);
				dprintf("Warning, no parent.\n");
			}
		}
	}

	/*
	 * vnode_create() has a habit of calling both vnop_reclaim() and
	 * vnop_fsync(), which can create havok as we are already holding locks.
	 */
	vnode_create(zfsvfs->z_vfs, parentvp,
	    zp, IFTOVT((mode_t)zp->z_mode), flags, &vp);

	/* We also get here with xdvp on the file, can be NULL */
	if (parentvp != NULL) {

		boolean_t isanyxattr = B_FALSE;

		znode_t *dzp = VTOZ(parentvp);
		if (dzp && (dzp->z_pflags & ZFS_XATTR))
			isanyxattr = B_TRUE;

		if (zp->z_pflags & ZFS_XATTR)
			isanyxattr = B_TRUE;

		if (vnode_isdir(parentvp) &&
		    !isanyxattr)
			vnode_setparent(vp, parentvp);

		VN_RELE(parentvp);
	}

	atomic_inc_64(&vnop_num_vnodes);

	// dprintf("Assigned zp %p with vp %p\n", zp, vp);
	/*
	 * Publish under z_attach_lock and wake anyone blocked in
	 * zfs_znode_asyncwait() (called from zfs_zget_ext() when it finds
	 * a znode with no vnode yet attached, racing this function). This
	 * is the only place a znode's vnode is ever attached synchronously
	 * - zfs_znode_asyncgetvnode_impl() calls this same function from
	 * its taskq, so covering it here is enough for both paths.
	 */
	mutex_enter(&zp->z_attach_lock);
	zp->z_vid = vnode_vid(vp);
	zp->z_vnode = vp;
	cv_broadcast(&zp->z_attach_cv);
	mutex_exit(&zp->z_attach_lock);

	// Assign security here. But, if we are XATTR, we do not? In Windows,
	// it refers to Streams and they do not have Security?
	if (zp->z_pflags & ZFS_XATTR)
		;
	else {
		NTSTATUS Status;
		Status = zfs_attach_security(vp, dzp && ZTOV(dzp) ?
		    ZTOV(dzp) : NULL, NULL);
		if (!NT_SUCCESS(Status))
			dprintf("zfs_attach_security failed: 0x%lx\n", Status);
		dprintf("After zfs_attach_security: \n");
		dump_sd(vp->security_descriptor);
	}
	return (0);
}


NTSTATUS
dev_ioctl(PDEVICE_OBJECT DeviceObject, ULONG ControlCode, PVOID InputBuffer,
    ULONG InputBufferSize, PVOID OutputBuffer, ULONG OutputBufferSize,
    BOOLEAN Override, IO_STATUS_BLOCK* iosb)
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

	if (!Irp)
		return (STATUS_INSUFFICIENT_RESOURCES);

	if (Override) {
		Stack = IoGetNextIrpStackLocation(Irp);
		Stack->Flags |= SL_OVERRIDE_VERIFY_VOLUME;
	}

	Status = IoCallDriver(DeviceObject, Irp);

	if (Status == STATUS_PENDING) {
		KeWaitForSingleObject(&Event, Executive, KernelMode,
		    FALSE, NULL);
		Status = IoStatus.Status;
	}

	if (iosb)
		*iosb = IoStatus;

	return (Status);
}

static WCHAR
hex_digit(uint8_t u)
{
	if (u >= 0xa && u <= 0xf)
		return ((uint8_t)(u - 0xa + 'a'));
	else
		return ((uint8_t)(u + '0'));
}

// THIS IS THE PNP DEVICE ID
NTSTATUS
pnp_query_id(PDEVICE_OBJECT DeviceObject, PIRP Irp, PIO_STACK_LOCATION IrpSp)
{
	mount_t *zmo;
	WCHAR *idString = NULL;
	int idLen = 0;
	NTSTATUS Status = STATUS_SUCCESS;
	DECLARE_UNICODE_STRING_SIZE(mpt, 100);

	dprintf("%s: query id type %d\n", __func__,
	    IrpSp->Parameters.QueryId.IdType);

	Irp->IoStatus.Information = (ULONG_PTR) NULL;

	zmo = (mount_t *)DeviceObject->DeviceExtension;
	/*
	 * Hark. So BusQueryHardwareIDs and BusQueryCompatibleIDs do not
	 * take a single string, but a MULTI_SZ - list of strings.
	 * Each string is null-terminated, and the last string is
	 * double null-terminated. Oh the fun we had figuring that out.
	 */
	switch (IrpSp->Parameters.QueryId.IdType) {
	case BusQueryDeviceID:
		if (zmo->type == MOUNT_TYPE_BUS) {
			RtlUnicodeStringPrintf(&mpt,
			    L"OpenZFS_bus\\GenericBus%lc", 0);
		} else {
			RtlUnicodeStringPrintf(&mpt,
			    L"OpenZFS_bus\\%wZ%lc", &zmo->uuid, 0);
		}
		idString = mpt.Buffer;
		idLen = mpt.Length;
		break;
	case BusQueryHardwareIDs: // IDs, plural
		RtlUnicodeStringPrintf(&mpt,
		    L"ROOT\\OpenZFS%lc%lc", 0, 0); // double nulls
		idString = mpt.Buffer;
		idLen = mpt.Length;
		break;
	case BusQueryContainerID:
		RtlUnicodeStringPrintf(&mpt,
		    L"{%wZ}%lc", &zmo->uuid, 0);
		    // L"{00000001-0002-0003-0004-000000000088}%lc", 0);
		idString = mpt.Buffer;
		idLen = mpt.Length;
		break;
#if 1
		// If these are included, AddDevice() does not get called.
	case BusQueryCompatibleIDs:
		RtlUnicodeStringPrintf(&mpt,
		    L"OpenZFS\\Generic%lc%lc", 0, 0);
		idString = mpt.Buffer;
		idLen = mpt.Length;
		break;
#endif
	case BusQueryInstanceID: // Needs to be unique.
		idString = zmo->uuid.Buffer;
		idLen = zmo->uuid.Length;
		break;
	default:
		// Status = Irp->IoStatus.Status;
		Status = STATUS_INVALID_DEVICE_REQUEST;
		break;
	}

	if (idLen > 0) {
		WCHAR *str;
		str = (WCHAR *)ExAllocatePoolWithTag(PagedPool,
		    idLen + sizeof (WCHAR), '!OIZ');
		if (str == NULL)
			return (STATUS_INSUFFICIENT_RESOURCES);

		RtlCopyMemory((void *)str, idString,
		    idLen);
		str[idLen / sizeof (WCHAR)] = UNICODE_NULL;

		Irp->IoStatus.Information = (ULONG_PTR)str;

		dprintf("replying with '%.*S'\n",
		    (int)(idLen/sizeof (WCHAR)),
		    (WCHAR *)str);

	}

	return (Status);
}

NTSTATUS
pnp_device_state(PDEVICE_OBJECT DeviceObject, PIRP Irp,
    PIO_STACK_LOCATION IrpSp)
{
	dprintf("%s:\n", __func__);
	PPNP_DEVICE_STATE pDeviceState =
	    (PPNP_DEVICE_STATE)&Irp->IoStatus.Information;

	pDeviceState = 0;

	if (vfs_isunmount(DeviceObject->DeviceExtension))
		Irp->IoStatus.Information |= PNP_DEVICE_REMOVED;
	// Irp->IoStatus.Information |= PNP_DEVICE_NOT_DISABLEABLE;

	return (STATUS_SUCCESS);
}

NTSTATUS
query_volume_information(PDEVICE_OBJECT DeviceObject, PIRP Irp,
    PIO_STACK_LOCATION IrpSp)
{
	NTSTATUS Status;
	Status = STATUS_NOT_IMPLEMENTED;
	int space;
	int error = 0;
	uint64_t refdbytes, availbytes, usedobjs, availobjs;
	uint64_t guid = 0ULL;

	mount_t *zmo = DeviceObject->DeviceExtension;
	if (!zmo ||
	    (zmo->type != MOUNT_TYPE_VCB &&
	    zmo->type != MOUNT_TYPE_DCB &&
	    zmo->type != MOUNT_TYPE_VSS)) {
		return (STATUS_INVALID_PARAMETER);
	}

	zfsvfs_t *zfsvfs = vfs_fsprivate(zmo);
	if (zfsvfs == NULL)
		return (STATUS_INVALID_PARAMETER);

	if ((error = zfs_enter(zfsvfs, FTAG)) != 0)
		return (error);  // This returns EIO if fail

	uint64_t sectorsz = 512ULL;
	if (zfsvfs->z_os && zfsvfs->z_os->os_spa)
		sectorsz = zfsvfs->z_os->os_spa->spa_min_alloc;

	if (zfsvfs->z_os)
		guid = dmu_objset_fsid_guid(zfsvfs->z_os);

	switch (IrpSp->Parameters.QueryVolume.FsInformationClass) {

	case FileFsAttributeInformation:
		//
		// If overflow, set Information to input_size and NameLength
		// to what we fit.
		//
		dprintf("* %s: FileFsAttributeInformation\n", __func__);
		if (IrpSp->Parameters.QueryVolume.Length <
		    sizeof (FILE_FS_ATTRIBUTE_INFORMATION)) {
			Irp->IoStatus.Information =
			    sizeof (FILE_FS_ATTRIBUTE_INFORMATION);
			Status = STATUS_BUFFER_TOO_SMALL;
			break;
		}

/* Do not enable until we have implemented FileRenameInformationEx method. */
#if (NTDDI_VERSION >= NTDDI_WIN10_RS1)
#define	ZFS_FS_ATTRIBUTE_POSIX
#endif

#define	ZFS_FS_ATTRIBUTE_CLEANUP_INFO

		FILE_FS_ATTRIBUTE_INFORMATION *ffai =
		    Irp->AssociatedIrp.SystemBuffer;
		ffai->FileSystemAttributes =
		    FILE_CASE_PRESERVED_NAMES | FILE_NAMED_STREAMS |
		    FILE_PERSISTENT_ACLS | FILE_SUPPORTS_OBJECT_IDS |
		    FILE_SUPPORTS_SPARSE_FILES | FILE_VOLUME_QUOTAS |
		    FILE_SUPPORTS_REPARSE_POINTS | FILE_UNICODE_ON_DISK |
		    FILE_SUPPORTS_HARD_LINKS | FILE_SUPPORTS_OPEN_BY_FILE_ID |
		    FILE_SUPPORTS_EXTENDED_ATTRIBUTES |
		    FILE_CASE_SENSITIVE_SEARCH;
#if defined(ZFS_FS_ATTRIBUTE_POSIX)
		ffai->FileSystemAttributes |= FILE_SUPPORTS_POSIX_UNLINK_RENAME;
#endif
#if defined(ZFS_FS_ATTRIBUTE_CLEANUP_INFO)
		ffai->FileSystemAttributes |= FILE_RETURNS_CLEANUP_RESULT_INFO;
#endif
#if defined(FILE_SUPPORTS_BLOCK_REFCOUNTING)
		/* Block-cloning, from FSCTL_DUPLICATE_EXTENTS */
		if (zfsvfs->z_os && zfsvfs->z_os->os_spa &&
		    spa_feature_is_enabled(dmu_objset_spa(zfsvfs->z_os),
		    SPA_FEATURE_BLOCK_CLONING)) {
			ffai->FileSystemAttributes |=
			    FILE_SUPPORTS_BLOCK_REFCOUNTING;
		}
#endif


		ffai->FileSystemAttributes |= FILE_FILE_COMPRESSION |
		    FILE_VOLUME_QUOTAS | FILE_SUPPORTS_SPARSE_VDL;


		/*
		 * Advertise USN journal support so that the Windows shell
		 * Previous Versions extension (twext.dll) will query VSS
		 * providers for snapshots. twext.dll gates on this flag
		 * and on FSCTL_QUERY_USN_JOURNAL succeeding before it
		 * enumerates VSS shadow copies.
		 *
		 * NTFS has these:
		 * FILE_CASE_SENSITIVE_SEARCH | FILE_FILE_COMPRESSION |
		 * FILE_RETURNS_CLEANUP_RESULT_INFO |
		 * FILE_SUPPORTS_POSIX_UNLINK_RENAME |
		 * FILE_SUPPORTS_ENCRYPTION | FILE_SUPPORTS_TRANSACTIONS |
		 * FILE_SUPPORTS_USN_JOURNAL;
		 */
		ffai->FileSystemAttributes |= FILE_SUPPORTS_USN_JOURNAL;

		if (zfsvfs->z_case == ZFS_CASE_SENSITIVE)
			ffai->FileSystemAttributes |=
			    FILE_CASE_SENSITIVE_SEARCH;

		if (zfsvfs->z_rdonly) {
			SetFlag(ffai->FileSystemAttributes,
			    FILE_READ_ONLY_VOLUME);
		}
		ffai->MaximumComponentNameLength = zfsvfs->z_longname ?
		    (ZAP_MAXNAMELEN_NEW - 1) : (MAXNAMELEN - 1);
		// ffai->FileSystemAttributes = 0x3E706FF; // ntfs 2023

		// There is room for one char in the struct
		// Alas, many things compare string to "NTFS".
		space = IrpSp->Parameters.QueryVolume.Length -
		    FIELD_OFFSET(FILE_FS_ATTRIBUTE_INFORMATION, FileSystemName);

		UNICODE_STRING name;
		if (zfsvfs->z_mimic == ZFS_MIMIC_OFF)
			RtlInitUnicodeString(&name, L"ZFS");
		else
			RtlInitUnicodeString(&name, L"NTFS");
		dprintf("Replying as %wZ\n", &name);

		space = MIN(space, name.Length);
		ffai->FileSystemNameLength = name.Length;
		RtlCopyMemory(ffai->FileSystemName, name.Buffer, space);
		Irp->IoStatus.Information =
		    FIELD_OFFSET(FILE_FS_ATTRIBUTE_INFORMATION,
		    FileSystemName) + space;

		if (space < name.Length)
			Status = STATUS_BUFFER_OVERFLOW;
		else
			Status = STATUS_SUCCESS;

		ASSERT(Irp->IoStatus.Information <=
		    IrpSp->Parameters.QueryVolume.Length);
		break;

	case FileFsControlInformation:
		dprintf("* %s: FileFsControlInformation NOT IMPLEMENTED\n",
		    __func__);
		break;

	case FileFsDeviceInformation:
		dprintf("* %s: FileFsDeviceInformation\n",
		    __func__);
		FILE_FS_DEVICE_INFORMATION *ffdi;
		ffdi = Irp->AssociatedIrp.SystemBuffer;
		ffdi->DeviceType = FILE_DEVICE_DISK;
		ffdi->Characteristics = 0; // FILE_REMOVABLE_MEDIA |
		    // FILE_DEVICE_IS_MOUNTED /* | FILE_READ_ONLY_DEVICE */;
		Irp->IoStatus.Information = sizeof (FILE_FS_DEVICE_INFORMATION);
		Status = STATUS_SUCCESS;
		break;

	case FileFsDriverPathInformation:
		dprintf("* %s: FileFsDriverPathInformation NOT IMPLEMENTED\n",
		    __func__);
		break;

	case FileFsFullSizeInformation:
		dprintf("* %s: FileFsFullSizeInformation\n", __func__);
		if (IrpSp->Parameters.QueryVolume.Length <
		    sizeof (FILE_FS_FULL_SIZE_INFORMATION)) {
			Irp->IoStatus.Information =
			    sizeof (FILE_FS_FULL_SIZE_INFORMATION);
			Status = STATUS_BUFFER_TOO_SMALL;
			break;
		}

		dmu_objset_space(zfsvfs->z_os,
		    &refdbytes, &availbytes, &usedobjs, &availobjs);

		FILE_FS_FULL_SIZE_INFORMATION *fffsi =
		    Irp->AssociatedIrp.SystemBuffer;
		fffsi->TotalAllocationUnits.QuadPart =
		    (refdbytes + availbytes) / sectorsz;
		fffsi->ActualAvailableAllocationUnits.QuadPart =
		    availbytes / sectorsz;
		fffsi->CallerAvailableAllocationUnits.QuadPart =
		    availbytes / sectorsz;
		fffsi->BytesPerSector = sectorsz;
		fffsi->SectorsPerAllocationUnit = 1;
		Irp->IoStatus.Information =
		    sizeof (FILE_FS_FULL_SIZE_INFORMATION);
		Status = STATUS_SUCCESS;
		break;

	case FileFsObjectIdInformation:
		dprintf("* %s: FileFsObjectIdInformation\n", __func__);
		FILE_FS_OBJECTID_INFORMATION *ffoi =
		    Irp->AssociatedIrp.SystemBuffer;
		RtlZeroMemory(ffoi->ObjectId, sizeof (ffoi->ObjectId));
		RtlCopyMemory(ffoi->ObjectId, &guid, sizeof (ffoi->ObjectId));
		RtlZeroMemory(ffoi->ExtendedInfo, sizeof (ffoi->ExtendedInfo));
		Irp->IoStatus.Information =
		    sizeof (FILE_FS_OBJECTID_INFORMATION);
		Status = STATUS_SUCCESS;
		break;

	case FileFsVolumeInformation:
		// Confirmed this call beha
		dprintf("* %s: FileFsVolumeInformation\n", __func__);

		if (IrpSp->Parameters.QueryVolume.Length <
		    sizeof (FILE_FS_VOLUME_INFORMATION)) {
			Irp->IoStatus.Information =
			    sizeof (FILE_FS_VOLUME_INFORMATION);
			Status = STATUS_BUFFER_TOO_SMALL;
			break;
		}
		FILE_FS_VOLUME_INFORMATION *ffvi =
		    Irp->AssociatedIrp.SystemBuffer;
		TIME_UNIX_TO_WINDOWS_EX(zfsvfs->z_last_unmount_time, 0,
		    ffvi->VolumeCreationTime.QuadPart);
		ffvi->SupportsObjects = TRUE;
		// PVPB Vpb = zmo->vpb;
		WCHAR *wstr;

		uint32_t serial = 0x19831116;
		if (guid)
			serial = (uint32_t)(guid ^ (guid >> 32));

		ffvi->VolumeSerialNumber = serial;
#if 0
		ffvi->VolumeLabelLength =
		    sizeof (VOLUME_LABEL) - sizeof (WCHAR);
		wstr = VOLUME_LABEL;
#else
		ffvi->VolumeLabelLength =
		    zmo->name.Length;
		wstr = zmo->name.Buffer;
#endif
		int space =
		    IrpSp->Parameters.QueryFile.Length -
		    FIELD_OFFSET(FILE_FS_VOLUME_INFORMATION, VolumeLabel);
		space = MIN(space, ffvi->VolumeLabelLength);

		/*
		 * This becomes the name displayed in Explorer, so we return the
		 * dataset name here, as much as we can
		 */
		RtlCopyMemory(ffvi->VolumeLabel, wstr, space);

		Irp->IoStatus.Information =
		    FIELD_OFFSET(FILE_FS_VOLUME_INFORMATION,
		    VolumeLabel) + space;

		if (space < ffvi->VolumeLabelLength)
			Status = STATUS_BUFFER_OVERFLOW;
		else
			Status = STATUS_SUCCESS;

		break;

	case FileFsSizeInformation:
		dprintf("* %s: FileFsSizeInformation\n", __func__);
		if (IrpSp->Parameters.QueryVolume.Length <
		    sizeof (FILE_FS_SIZE_INFORMATION)) {
			Irp->IoStatus.Information =
			    sizeof (FILE_FS_SIZE_INFORMATION);
			Status = STATUS_BUFFER_TOO_SMALL;
			break;
		}

		dmu_objset_space(zfsvfs->z_os,
		    &refdbytes, &availbytes, &usedobjs, &availobjs);

		FILE_FS_SIZE_INFORMATION *ffsi =
		    Irp->AssociatedIrp.SystemBuffer;
		ffsi->TotalAllocationUnits.QuadPart =
		    (refdbytes + availbytes) / sectorsz;
		ffsi->AvailableAllocationUnits.QuadPart =
		    availbytes / sectorsz;
		ffsi->SectorsPerAllocationUnit = 1;
		ffsi->BytesPerSector = sectorsz;
		Irp->IoStatus.Information = sizeof (FILE_FS_SIZE_INFORMATION);
		Status = STATUS_SUCCESS;
		break;

	case FileFsSectorSizeInformation:
		dprintf("* %s: FileFsSectorSizeInformation\n", __func__);
		if (IrpSp->Parameters.QueryVolume.Length <
		    sizeof (FILE_FS_SECTOR_SIZE_INFORMATION)) {
			Irp->IoStatus.Information =
			    sizeof (FILE_FS_SECTOR_SIZE_INFORMATION);
			Status = STATUS_BUFFER_TOO_SMALL;
			break;
		}
		FILE_FS_SECTOR_SIZE_INFORMATION *ffssi =
		    Irp->AssociatedIrp.SystemBuffer;
		ffssi->LogicalBytesPerSector = sectorsz;
		ffssi->PhysicalBytesPerSectorForAtomicity = sectorsz;
		ffssi->PhysicalBytesPerSectorForPerformance = sectorsz;
		ffssi->FileSystemEffectivePhysicalBytesPerSectorForAtomicity =
		    sectorsz;
		ffssi->Flags = SSINFO_FLAGS_NO_SEEK_PENALTY;
		ffssi->ByteOffsetForSectorAlignment = SSINFO_OFFSET_UNKNOWN;
		ffssi->ByteOffsetForPartitionAlignment = SSINFO_OFFSET_UNKNOWN;
		Irp->IoStatus.Information =
		    sizeof (FILE_FS_SECTOR_SIZE_INFORMATION);
		Status = STATUS_SUCCESS;
		break;

	default:
		dprintf("* %s: unknown class 0x%x\n", __func__,
		    IrpSp->Parameters.QueryVolume.FsInformationClass);
		Status = STATUS_NOT_IMPLEMENTED;
		break;
	}
	zfs_exit(zfsvfs, FTAG);
	return (Status);
}

NTSTATUS
lock_control(PDEVICE_OBJECT DeviceObject, PIRP *PIrp, PIO_STACK_LOCATION IrpSp)
{
	PIRP Irp = *PIrp;
	NTSTATUS Status = STATUS_SUCCESS;
	PFILE_OBJECT Fo = IrpSp->FileObject;
	vnode_t *vp = Fo ? Fo->FsContext : NULL;
	ZFS_OPLOCK_CREATE_CTX *ctx = NULL;

	dprintf("%s: FileObject %p flags 0x%x %s %s\n", __func__,
	    IrpSp->FileObject, IrpSp->Flags,
	    IrpSp->Flags & SL_EXCLUSIVE_LOCK ? "Exclusive" : "Shared",
	    IrpSp->Flags & SL_FAIL_IMMEDIATELY ? "Nowait" : "Wait");

	if (!vp)
		return (STATUS_INVALID_PARAMETER);

	uint64_t skip = (uint64_t)Irp->Tail.Overlay.DriverContext[0];
	dprintf("%s skip is set to 0x%llx\n", __func__, skip);

	switch (IrpSp->MinorFunction) {
	case IRP_MN_LOCK: {

		// If we�re re-entering from our work item (resume),
		// skip the preflight
		const BOOLEAN skipPreflight =
		    (skip == (OPLOCK_SKIP_MAGIC | OPLOCK_SKIP_LOCK));

		if (!skipPreflight) {

			// === Oplock preflight (may pend) ===
			ExAcquireResourceExclusiveLite(vp->FileHeader.Resource,
			    TRUE);

			ctx = ExAllocatePoolZero(NonPagedPoolNx, sizeof (*ctx),
			    'plkO');
			if (!ctx) {
				ExReleaseResourceLite(vp->FileHeader.Resource);
				return (STATUS_INSUFFICIENT_RESOURCES);
			}

			ctx->DeviceObject = DeviceObject;
			ctx->Irp = Irp;
			ctx->SkipMask = OPLOCK_SKIP_LOCK;

			// No COMPLETE_IF_OPLOCKED here
			// we want to wait for break if needed
			Status = FsRtlCheckOplockEx(
			    vp_oplock(vp),
			    Irp,
			    0,
			    ctx,
			    ctx ? ZfsOplockCreatePostBreak : NULL,
			    NULL);

			ExReleaseResourceLite(vp->FileHeader.Resource);

			if (Status == STATUS_PENDING) {
				IoMarkIrpPending(Irp);
				return (STATUS_PENDING);
			}

			ExFreePoolWithTag(ctx, 'plkO');

			if (!NT_SUCCESS(Status))
				return (Status);

		} // skipPreflight

		// === Lock tail: actually install the BRL and complete ===
		Status = FsRtlProcessFileLock(&vp->lock, Irp, NULL);

		// if (Status == STATUS_PENDING)
			// IoMarkIrpPending(Irp);

		*PIrp = NULL; // FsRtlProcessFileLock completes
		return (Status);
	} // IRP_MN_LOCK

	case IRP_MN_UNLOCK_SINGLE:
	case IRP_MN_UNLOCK_ALL:
	case IRP_MN_UNLOCK_ALL_BY_KEY: {
		// No oplock preflight for unlocks
		Status = FsRtlProcessFileLock(&vp->lock, Irp, NULL);

		// if (Status == STATUS_PENDING)
		//	IoMarkIrpPending(Irp);

		*PIrp = NULL;
		return (Status);
	}

	default:
		break;
	}

	return (STATUS_INVALID_DEVICE_REQUEST);
}


NTSTATUS
query_information(PDEVICE_OBJECT DeviceObject, PIRP Irp,
    PIO_STACK_LOCATION IrpSp)
{
	NTSTATUS Status = STATUS_NOT_IMPLEMENTED;
	ULONG usedspace = 0;
	struct vnode *vp = NULL;
	int normalize = 0;

	if (IrpSp->FileObject && IrpSp->FileObject->FsContext) {
		vp = IrpSp->FileObject->FsContext;
		if (VN_HOLD(vp) != 0)
			return (STATUS_INVALID_PARAMETER);
	}

	switch (IrpSp->Parameters.QueryFile.FileInformationClass) {

	case FileAllInformation:
		dprintf("%s: FileAllInformation: buffer 0x%lx\n", __func__,
		    IrpSp->Parameters.QueryFile.Length);

		if (IrpSp->Parameters.QueryFile.Length <
		    sizeof (FILE_ALL_INFORMATION)) {
			Irp->IoStatus.Information =
			    sizeof (FILE_ALL_INFORMATION);
// We should send Plus Filename here, to be nice, but this doesnt happen
			Status = STATUS_BUFFER_TOO_SMALL;
			break;
		}
		FILE_ALL_INFORMATION *all = Irp->AssociatedIrp.SystemBuffer;

		// Even if the name does not fit, the other information
		// should be correct
		Status = file_basic_information(DeviceObject, Irp, IrpSp,
		    &all->BasicInformation);
		if (Status != STATUS_SUCCESS)
			break;
		Status = file_standard_information(DeviceObject, Irp, IrpSp,
		    &all->StandardInformation);
		if (Status != STATUS_SUCCESS)
			break;
		Status = file_position_information(DeviceObject, Irp, IrpSp,
		    &all->PositionInformation);
		if (Status != STATUS_SUCCESS)
			break;
		Status = file_ea_information(DeviceObject, Irp, IrpSp,
		    &all->EaInformation);
		if (Status != STATUS_SUCCESS)
			break;
#if 1
		zfs_ccb_t *zccb = IrpSp->FileObject->FsContext2;
		all->AccessInformation.AccessFlags =
		    GENERIC_ALL | GENERIC_EXECUTE |
		    GENERIC_READ | GENERIC_WRITE;
		all->ModeInformation.Mode =
		    zccb && zccb->deleteonclose ? FILE_DELETE_ON_CLOSE : 0;
#endif
		Status = file_alignment_information(DeviceObject, Irp, IrpSp,
		    &all->AlignmentInformation);
		if (Status != STATUS_SUCCESS)
			break;

		Status = file_internal_information(DeviceObject, Irp, IrpSp,
		    &all->InternalInformation);
		if (Status != STATUS_SUCCESS)
			break;

		// First get the Name, to make sure we have room
		IrpSp->Parameters.QueryFile.Length -=
		    offsetof(FILE_ALL_INFORMATION, NameInformation);
		Status = file_name_information(DeviceObject, Irp, IrpSp,
		    &all->NameInformation, &usedspace, 0);
		IrpSp->Parameters.QueryFile.Length +=
		    offsetof(FILE_ALL_INFORMATION, NameInformation);

		// file_name_information sets FileNameLength, so update size
		// to be ALL struct not NAME struct
		// However, there is room for one char in the struct,
		// so subtract that from total.
		Irp->IoStatus.Information =
		    FIELD_OFFSET(FILE_ALL_INFORMATION, NameInformation) +
		    FIELD_OFFSET(FILE_NAME_INFORMATION, FileName) +
		    usedspace;
		// FIELD_OFFSET(FILE_ALL_INFORMATION, NameInformation.FileName)
		// + usedspace;

		dprintf("Struct size 0x%x FileNameLen 0x%lx "
		    "Information retsize 0x%llx\n",
		    (int)sizeof (FILE_ALL_INFORMATION),
		    all->NameInformation.FileNameLength,
		    Irp->IoStatus.Information);
		break;
	case FileAttributeTagInformation:
		Status = file_attribute_tag_information(DeviceObject, Irp,
		    IrpSp, Irp->AssociatedIrp.SystemBuffer);
		break;
	case FileBasicInformation:
		Status = file_basic_information(DeviceObject, Irp, IrpSp,
		    Irp->AssociatedIrp.SystemBuffer);
		break;
	case FileCompressionInformation:
		Status = file_compression_information(DeviceObject, Irp, IrpSp,
		    Irp->AssociatedIrp.SystemBuffer);
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
	/*
	 * According to chatGPT, the difference between FileNameInformation
	 * and FileNormalizedNameInformation is that the latter will
	 * return a more "portable" name. For example;
	 * "My Photos (2022)" -> "my_photos_2022", as the FS desires.
	 * In this example, unified case, no spaces and limited charset.
	 *
	 * The complications start when the normalized name is passed to
	 * lookup (CreateFile->zfs_vnop_lookup()) as it is expected to
	 * work. Uniqueness would have to be guaranteed (per directory).
	 * And filename matching would be more complicated.
	 *
	 * For now, let's return identical names for Normalized.
	 *
	 */

		normalize = 1;

		zfs_fallthrough;
	case FileNameInformation:
		//
		// If overflow, set Information to input_size and NameLength
		// to required size.
		//
		Status = file_name_information(DeviceObject, Irp, IrpSp,
		    Irp->AssociatedIrp.SystemBuffer, &usedspace, normalize);
		Irp->IoStatus.Information =
		    FIELD_OFFSET(FILE_NAME_INFORMATION, FileName) + usedspace;
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
	case FileAlignmentInformation:
		Status = file_alignment_information(DeviceObject, Irp, IrpSp,
		    Irp->AssociatedIrp.SystemBuffer);
		break;
	case FileStreamInformation:
		Status = file_stream_information(DeviceObject, Irp, IrpSp,
		    Irp->AssociatedIrp.SystemBuffer);
		break;
	case FileHardLinkInformation:
		Status = file_hard_link_information(DeviceObject, Irp, IrpSp,
		    Irp->AssociatedIrp.SystemBuffer);
		break;
	// Not used - not handled by ntfs either
	case FileRemoteProtocolInformation:
		dprintf("* %s: FileRemoteProtocolInformation NOT IMPLEMENTED\n",
		    __func__);
#if 0
		Status = file_remote_protocol_information(DeviceObject, Irp,
		    IrpSp, Irp->AssociatedIrp.SystemBuffer);
#endif
		Status = STATUS_INVALID_PARAMETER;
		break;
	case FileStandardLinkInformation:
		Status = file_standard_link_information(DeviceObject, Irp,
		    IrpSp, Irp->AssociatedIrp.SystemBuffer);
		break;
	case FileReparsePointInformation:
		dprintf("* %s: FileReparsePointInformation NOT IMPLEMENTED\n",
		    __func__);
		break;
	case FileIdInformation:
		Status = file_id_information(DeviceObject, Irp, IrpSp,
		    Irp->AssociatedIrp.SystemBuffer);
		break;
	case FileCaseSensitiveInformation:
		Status = file_case_sensitive_information(DeviceObject, Irp,
		    IrpSp, Irp->AssociatedIrp.SystemBuffer);
		break;
	case FileStatInformation:
		// We call these functions from zfs_vnop_lookup, so size
		// testing goes here
		if (IrpSp->Parameters.QueryFile.Length <
		    sizeof (FILE_STAT_INFORMATION)) {
			Irp->IoStatus.Information =
			    sizeof (FILE_STAT_INFORMATION);
			Status = STATUS_BUFFER_TOO_SMALL;
			break;
		}
		Status = file_stat_information(DeviceObject, Irp, IrpSp,
		    Irp->AssociatedIrp.SystemBuffer);
		Irp->IoStatus.Information = sizeof (FILE_STAT_INFORMATION);
		break;
	case FileStatLxInformation:
		// We call these functions from zfs_vnop_lookup, so size
		// testing goes here
		if (IrpSp->Parameters.QueryFile.Length <
		    sizeof (FILE_STAT_LX_INFORMATION)) {
			Irp->IoStatus.Information =
			    sizeof (FILE_STAT_LX_INFORMATION);
			Status = STATUS_BUFFER_TOO_SMALL;
			break;
		}
		Status = file_stat_lx_information(DeviceObject, Irp, IrpSp,
		    Irp->AssociatedIrp.SystemBuffer);
		Irp->IoStatus.Information = sizeof (FILE_STAT_LX_INFORMATION);
		break;
	case FileStatBasicInformation:
		if (IrpSp->Parameters.QueryFile.Length <
		    sizeof (FILE_STAT_BASIC_INFORMATION)) {
			Irp->IoStatus.Information =
			    sizeof (FILE_STAT_BASIC_INFORMATION);
			Status = STATUS_BUFFER_TOO_SMALL;
			break;
		}
		Status = file_stat_basic_information(DeviceObject, Irp, IrpSp,
		    Irp->AssociatedIrp.SystemBuffer);
		Irp->IoStatus.Information =
		    sizeof (FILE_STAT_BASIC_INFORMATION);
		break;
	default:
		dprintf("* %s: unknown class 0x%x NOT IMPLEMENTED\n", __func__,
		    IrpSp->Parameters.QueryFile.FileInformationClass);
		break;
	}

	if (vp) {
		VN_RELE(vp);
		vp = NULL;
	}
	return (Status);
}

boolean_t
LockUserBuffer(
    IN OUT PIRP Irp,
    IN LOCK_OPERATION Operation,
    IN ULONG BufferLength)
{
	PMDL Mdl = NULL;

	PAGED_CODE();

	if (Irp->MdlAddress == NULL) {

		Mdl = IoAllocateMdl(Irp->UserBuffer, BufferLength, FALSE, FALSE,
		    Irp);

		if (Mdl == NULL)
			return (B_FALSE);

		try {

			MmProbeAndLockPages(Mdl,
			    Irp->RequestorMode,
			    Operation);

		} except(EXCEPTION_EXECUTE_HANDLER) {
			NTSTATUS Status;

			Status = GetExceptionCode();

			IoFreeMdl(Mdl);
			Irp->MdlAddress = NULL;
			(void) Status;
		}
	}

	return (B_TRUE);
}

void *
MapUserBuffer(
    PIRP Irp,
    ULONG Length,
    LOCK_OPERATION AccessMode,
    PMDL *outMdl)
{
	PMDL mdl;

	if (outMdl)
		*outMdl = NULL;  // default: "nothing to free later"

	mdl = Irp->MdlAddress;
	if (mdl != NULL) {
		return (MmGetSystemAddressForMdlSafe(mdl,
		    NormalPagePriority | MdlMappingNoExecute));
	}

	// Kernel caller, no MDL: treat UserBuffer as a real kernel VA.
	// Or if no outMdl passed along, assume caller handles it. read/write
	if (Irp->RequestorMode == KernelMode || !outMdl) {
		return (Irp->UserBuffer);
	}

	// User-mode + no MDL: we must build one.
	mdl = IoAllocateMdl(Irp->UserBuffer, Length, FALSE, FALSE, NULL);
	if (mdl == NULL)
		return (NULL);

	__try {
		MmProbeAndLockPages(mdl, UserMode, AccessMode);
	}
	__except(EXCEPTION_EXECUTE_HANDLER) {
		IoFreeMdl(mdl);
		return (NULL);
	}

	*outMdl = mdl;   // we own this, caller must unlock+free later.

	return (MmGetSystemAddressForMdlSafe(mdl,
	    NormalPagePriority | MdlMappingNoExecute));
}

void
UnMapUserBuffer(PMDL mdl)
{
	if (mdl) {
		MmUnlockPages(mdl);
		IoFreeMdl(mdl);
	}
}

PVOID
BufferUserBuffer(IN OUT PIRP Irp, IN ULONG BufferLength)
{
	PUCHAR UserBuffer;
	if (BufferLength == 0)
		return (NULL);

	//
	//  If there is no system buffer we must have been supplied an Mdl
	//  describing the users input buffer, which we will now snapshot.
	//
	if (Irp->AssociatedIrp.SystemBuffer == NULL) {
		UserBuffer = MapUserBuffer(Irp, 0, 0, NULL);
		Irp->AssociatedIrp.SystemBuffer =
		    FsRtlAllocatePoolWithQuotaTag(NonPagedPoolNx,
		    BufferLength,
		    'qtaf');
		//
		// Set the flags so that the completion code knows to
		// deallocate the buffer.
		//
		Irp->Flags |= (IRP_BUFFERED_IO | IRP_DEALLOCATE_BUFFER);

		try {
			RtlCopyMemory(Irp->AssociatedIrp.SystemBuffer,
			    UserBuffer,
			    BufferLength);
		} except(EXCEPTION_EXECUTE_HANDLER) {
			NTSTATUS Status;
			Status = GetExceptionCode();
			(void) Status;
		}
	}
	return (Irp->AssociatedIrp.SystemBuffer);
}

/*
 * ** This is how I thought it worked:
 * Iterate through the XATTRs of an object, skipping streams. It works
 * like readdir, with saving index point, restart_scan and single_entry flags.
 * It can optionally supply QueryEa.EaList to query specific set of EAs.
 * Each output structure is 4 byte aligned
 *
 * 1: While EAs fit, including name and value-data, we keep packing them
 * in. If we have a non-zero number of valid EAs in the output buffer,
 * we return STATUS_SUCCESS, and IoStatus.Information is set to the
 * number of bytes in the output buffer.
 *
 * 2: If we can't fit (the next) EA at all, and there are no prior valid
 * EAs (they would handled by 1: above) we return STATUS_BUFFER_OVERFLOW
 * and IoStatus.Information is set to what it needs to fit it.
 *
 * 3: If we finished with all EAs, we return the valid records
 * with status STATUS_NO_MORE_EAS.
 *
 *
 * ** But it actually wants everything in one-shot, fit all in the buffer
 * or return STATUS_BUFFER_OVERFLOW.
 */
NTSTATUS
query_ea(PDEVICE_OBJECT DeviceObject, PIRP Irp, PIO_STACK_LOCATION IrpSp)
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
	FILE_FULL_EA_INFORMATION *previous_ea = NULL;
	uint64_t spaceused = 0;
	znode_t *zp = NULL;
	PMDL mdl = NULL;

	// zfsvfs_t *zfsvfs = NULL;
	int overflow = 0;

	struct vnode *vp = NULL, *xdvp = NULL;

	if (IrpSp->FileObject == NULL)
		return (STATUS_INVALID_PARAMETER);
	vp = IrpSp->FileObject->FsContext;
	if (vp == NULL)
		return (STATUS_INVALID_PARAMETER);

	zp = VTOZ(vp);
	// zfsvfs = zp->z_zfsvfs;

	UserBufferLength = IrpSp->Parameters.QueryEa.Length;
	UserEaList = IrpSp->Parameters.QueryEa.EaList;
	UserEaListLength = IrpSp->Parameters.QueryEa.EaListLength;
	UserEaIndex = IrpSp->Parameters.QueryEa.EaIndex;
	RestartScan = BooleanFlagOn(IrpSp->Flags, SL_RESTART_SCAN);
	ReturnSingleEntry = BooleanFlagOn(IrpSp->Flags, SL_RETURN_SINGLE_ENTRY);
	IndexSpecified = BooleanFlagOn(IrpSp->Flags, SL_INDEX_SPECIFIED);

	dprintf("%s\n", __func__);

	Buffer = MapUserBuffer(Irp, IrpSp->Parameters.QueryEa.Length,
	    IoWriteAccess, &mdl);

	if (UserBufferLength < sizeof (FILE_FULL_EA_INFORMATION)) {

		if (UserBufferLength == 0) {
			Irp->IoStatus.Information = 0;
			UnMapUserBuffer(mdl);
			return (STATUS_NO_MORE_EAS);
		}

		Irp->IoStatus.Information = sizeof (FILE_FULL_EA_INFORMATION);
		UnMapUserBuffer(mdl);
		return (STATUS_BUFFER_OVERFLOW);
		// Docs say to return too-small, but some callers get stuck
		// calling this in a cpu loop if we return it.
		return (STATUS_BUFFER_TOO_SMALL);
	}

	FILE_GET_EA_INFORMATION *ea;
	int error = 0;

	uint64_t start_index = 0;

	zfs_ccb_t *zccb = IrpSp->FileObject->FsContext2;

	if (RestartScan) {
		start_index = 0;
		zccb->ea_index = 0;
	} else if (IndexSpecified)
		start_index = UserEaIndex;
	else
		start_index = zccb->ea_index;

	struct iovec iov;
	iov.iov_base =
	    (void *)Buffer;
	iov.iov_len = UserBufferLength;

	zfs_uio_t uio;
	zfs_uio_iovec_init(&uio, &iov, 1, 0,
	    UIO_SYSSPACE, UserBufferLength, 0);

	// Pass Flags along for ReturnSingleEntry, so
	// lets abuse uio->extflg - no idea what it is for
	// it got copied across to Windows so it's there.
	uio.uio_extflg = IrpSp->Flags;

	/* ********************** */
	if (UserEaList != NULL) {

		uint64_t offset = 0;
		uint64_t current_index = 0;

		do {
			/* bounds check: offset is on INPUT list */
			if (offset > UserEaListLength) {
				if (xdvp) VN_RELE(xdvp);
				UnMapUserBuffer(mdl);
				return (STATUS_INVALID_PARAMETER);
			}

			ea = (FILE_GET_EA_INFORMATION *)&UserEaList[offset];

			if (offset + ea->EaNameLength > UserEaListLength) {
				if (xdvp) VN_RELE(xdvp);
				UnMapUserBuffer(mdl);
				return (STATUS_INVALID_PARAMETER);
			}

			/* scan until we get to the index wanted */
			if (current_index >= start_index) {

				error = zpl_xattr_filldir(vp, &uio, ea->EaName,
				    ea->EaNameLength, &previous_ea);

				if (error == ENOENT)
					error = 0;
				else if (error != 0)
					break;

				if (ReturnSingleEntry) {
					current_index++;
					break;
				}
			}
			// if (overflow != 0)
			//	break;

			current_index++;
			offset += ea->NextEntryOffset;

			if (ea->NextEntryOffset == 0)
				break;

		} while (offset != 0);

		if (current_index >= start_index)
			zccb->ea_index = current_index;

		/* ********************** */
	} else {

		zfs_uio_setindex(&uio, start_index);
		Status = zpl_xattr_list(vp, &uio, (ssize_t *)&spaceused, NULL);
		zccb->ea_index = zfs_uio_index(&uio);
	}


out:

	if (xdvp) VN_RELE(xdvp);

	Irp->IoStatus.Information = spaceused;

	// Didn't fit even one
	if (overflow)
		Status = STATUS_BUFFER_OVERFLOW;
	else if (spaceused == 0 && Status == 0)
		Status = STATUS_NO_EAS_ON_FILE;

	UnMapUserBuffer(mdl);

	return (Status);
}

/*
 * Receive an array of structs to set EAs, iterate until Next is null.
 */
NTSTATUS
set_ea(PDEVICE_OBJECT DeviceObject, PIRP Irp, PIO_STACK_LOCATION IrpSp)
{
	uint32_t input_len = IrpSp->Parameters.SetEa.Length;
	uint8_t *buffer = NULL;
	NTSTATUS Status = STATUS_SUCCESS;
	struct vnode *vp = NULL;
	zfs_ccb_t *zccb = NULL;

	if (IrpSp->FileObject == NULL)
		return (STATUS_INVALID_PARAMETER);

	vp = IrpSp->FileObject->FsContext;
	if (vp == NULL)
		return (STATUS_INVALID_PARAMETER);

	zccb = IrpSp->FileObject->FsContext2;
	if (zccb == NULL)
		return (STATUS_INVALID_PARAMETER);

	dprintf("%s\n", __func__);

	if (input_len == 0)
		return (STATUS_INVALID_PARAMETER);

	mount_t *zmo = DeviceObject->DeviceExtension;
	zfsvfs_t *zfsvfs;
	if (zmo != NULL &&
	    (zfsvfs = vfs_fsprivate(zmo)) != NULL &&
	    zfsvfs->z_rdonly)
		return (STATUS_MEDIA_WRITE_PROTECTED);

	// This magic is straight out of fastfat
	buffer = BufferUserBuffer(Irp, input_len);

	ULONG eaErrorOffset = 0;
	Status = vnode_apply_eas(vp, zccb,
	    (PFILE_FULL_EA_INFORMATION)buffer,
	    input_len, &eaErrorOffset);
	// (Information is ULONG_PTR; as win64 is a LLP64 platform,
	// ULONG isn't the right length.)
	Irp->IoStatus.Information = 0;
	if (!NT_SUCCESS(Status)) {
		dprintf("%s: failed vnode_apply_eas: 0x%lx\n",
		    __func__, Status);
		return (Status);
	}
	return (Status);
}

int
get_reparse_point_impl(znode_t *zp, char *buffer, size_t bufferlen,
    size_t *returnlen)
{
	int err = 0;
	if (zp->z_pflags & ZFS_REPARSE) {

		// Return the needed total size, but only copy as
		// much as we can fit.
		// WEIRDLY, Explorer will crash if we return
		// neededbytes in Information. It should be 0.
		if (zfsctl_is_node(zp)) {
			REPARSE_DATA_BUFFER *rdb = NULL;
			NTSTATUS Status;
			size_t size = 0;

			Status = zfsctl_get_reparse_point(zp, &rdb, &size);
			if (Status == 0 && bufferlen >= size)
				memcpy(buffer, rdb, size);
			if (returnlen)
				*returnlen = 0; // size
		} else {
			struct iovec iov;
			iov.iov_base = (void *)buffer;
			iov.iov_len = MIN(zp->z_size, bufferlen);

			zfs_uio_t uio;
			zfs_uio_iovec_init(&uio, &iov, 1, 0, UIO_SYSSPACE,
			    iov.iov_len, 0);
			err = zfs_readlink(ZTOV(zp), &uio, NULL);
			if (!err && returnlen)
				*returnlen = zp->z_size - zfs_uio_resid(&uio);
		}
	}
	return (err);
}

NTSTATUS
get_reparse_point(PDEVICE_OBJECT DeviceObject, PIRP Irp,
    PIO_STACK_LOCATION IrpSp)
{
	NTSTATUS Status = STATUS_NOT_A_REPARSE_POINT;
	PFILE_OBJECT FileObject = IrpSp->FileObject;
	DWORD outlen = IrpSp->Parameters.FileSystemControl.OutputBufferLength;
	void *buffer = Irp->AssociatedIrp.SystemBuffer;
	REPARSE_DATA_BUFFER *rdb = buffer;
	struct vnode *vp;
	DWORD reqlen;

	if (FileObject == NULL)
		return (STATUS_INVALID_PARAMETER);

	vp = FileObject->FsContext;
	if (vp == NULL)
		return (STATUS_INVALID_PARAMETER);

	znode_t *zp = VTOZ(vp);
	zfsvfs_t *zfsvfs = (zp != NULL) ? zp->z_zfsvfs : NULL;

	/*
	 * Snapshot root: do not return reparse data. Ctldir path resolution
	 * handles snapshot navigation without the Windows junction-following
	 * mechanism. Synthesizing a reparse buffer here causes Explorer, via
	 * the Previous Versions COM shell extension, to call
	 * BasepGetVolumeNameFromReparsePoint, which overflows its stack buffer
	 * and crashes with a GS cookie failure (c0000409).
	 */
	if (zfsvfs != NULL && zfsvfs->z_issnap && zp->z_id == zfsvfs->z_root)
		goto end;

	if (vnode_islnk(vp)) {
		reqlen = offsetof(REPARSE_DATA_BUFFER,
		    GenericReparseBuffer.DataBuffer) + sizeof (uint32_t);

		if (outlen < reqlen) {
			Status = STATUS_BUFFER_OVERFLOW;
			goto end;
		}

		rdb->ReparseTag = IO_REPARSE_TAG_LX_SYMLINK;
		rdb->ReparseDataLength = offsetof(REPARSE_DATA_BUFFER,
		    GenericReparseBuffer.DataBuffer) + sizeof (uint32_t);
		rdb->Reserved = 0;

		*((uint32_t *)rdb->GenericReparseBuffer.DataBuffer) = 1;

		Irp->IoStatus.Information = reqlen;
		goto end;
	}

	Irp->IoStatus.Information = 0;
	if (zp->z_pflags & ZFS_REPARSE) {
		int err;
		size_t size = 0;

		err = get_reparse_point_impl(zp, buffer, outlen, &size);

		if (err)
			Status = STATUS_UNEXPECTED_IO_ERROR;

		if (outlen < size) {
			Status = STATUS_BUFFER_OVERFLOW;
		} else {
			Status = STATUS_SUCCESS;
			Irp->IoStatus.Information = size;

		}
	}

end:

	dprintf("%s: returning 0x%lx\n", __func__, Status);
	return (Status);
}

NTSTATUS
set_reparse_point(PDEVICE_OBJECT DeviceObject, PIRP Irp,
    PIO_STACK_LOCATION IrpSp)
{
	NTSTATUS Status = STATUS_SUCCESS;
	PFILE_OBJECT FileObject = IrpSp->FileObject;
	DWORD inlen = IrpSp->Parameters.DeviceIoControl.InputBufferLength;
	void *buffer = Irp->AssociatedIrp.SystemBuffer;
	REPARSE_DATA_BUFFER *rdb = buffer;

	if (!FileObject || !IrpSp->FileObject->FsContext)
		return (STATUS_INVALID_PARAMETER);

	struct vnode *vp = IrpSp->FileObject->FsContext;

	if (!vp || !VTOZ(vp))
		return (STATUS_INVALID_PARAMETER);

	if (Irp->UserBuffer)
		return (STATUS_INVALID_PARAMETER);

	if (inlen < sizeof (ULONG)) {
		return (STATUS_INVALID_BUFFER_SIZE);
	}

	Status = FsRtlValidateReparsePointBuffer(inlen, rdb);
	if (!NT_SUCCESS(Status)) {
		dprintf("FsRtlValidateReparsePointBuffer returned %08lx\n",
		    Status);
		return (Status);
	}

	znode_t *zp = VTOZ(vp);
	zfsvfs_t *zfsvfs = zp->z_zfsvfs;

	if (zfsctl_is_node(zp))
		return (zfsctl_set_reparse_point(zp, rdb, inlen));

	if (zfsvfs == NULL)
		return (STATUS_INVALID_PARAMETER);

	/*
	 * Snapshot root accessed via .zfs\snapshot\<name>: MountManager
	 * is registering the junction mount point.  Accept without writing
	 * (the snapshot VCB is read-only; our ctldir already handles path
	 * redirection so the stored reparse point is not needed).
	 */
	if (zfsvfs->z_issnap && zp->z_id == zfsvfs->z_root)
		return (STATUS_SUCCESS);

	if (zfsvfs->z_rdonly)
		return (STATUS_MEDIA_WRITE_PROTECTED);

	znode_t *dzp = NULL;
	int error;

	vnode_t *dvp = NULL;
	dvp = zfs_parent(vp);
	dzp = VTOZ(dvp);

	/*
	 * If a reparse point is already set, the caller's tag must match
	 * the existing tag, otherwise Windows requires
	 * STATUS_IO_REPARSE_TAG_MISMATCH.
	 */
	if (zp->z_pflags & ZFS_REPARSE) {
		ULONG existing_tag = get_reparse_tag(zp);
		if (existing_tag != 0 && rdb->ReparseTag != existing_tag) {
			VN_RELE(dvp);
			return (STATUS_IO_REPARSE_TAG_MISMATCH);
		}
	}
	// error = zfs_symlink(dzp, , vattr_t * vap, char *link,
	// 	znode_t * *zpp, cred_t * cr, int flags)


	// Like zfs_symlink, write the data as SA attribute.
	dmu_tx_t	*tx;
	boolean_t	fuid_dirtied;

	// Set flags to indicate we are reparse point
	zp->z_pflags |= ZFS_REPARSE;

	// Start TX and save FLAGS, SIZE and SYMLINK to disk.
	// This code should probably call zfs_symlink()
top:
	tx = dmu_tx_create(zfsvfs->z_os);
	fuid_dirtied = zfsvfs->z_fuid_dirty;
	dmu_tx_hold_write(tx, DMU_NEW_OBJECT, 0, MAX(1, inlen));
	dmu_tx_hold_zap(tx, dzp->z_id, TRUE, NULL);
	dmu_tx_hold_sa_create(tx, inlen);
	dmu_tx_hold_sa(tx, zp->z_sa_hdl, B_FALSE);
	if (fuid_dirtied)
		zfs_fuid_txhold(zfsvfs, tx);

	error = dmu_tx_assign(tx, DMU_TX_WAIT);
	if (error) {
		dmu_tx_abort(tx);
		if (error == ERESTART)
			goto top;
		goto out;
	}

	(void) sa_update(zp->z_sa_hdl, SA_ZPL_FLAGS(zfsvfs),
	    &zp->z_pflags, sizeof (zp->z_pflags), tx);

	mutex_enter(&zp->z_lock);
	if (zp->z_is_sa)
		error = sa_update(zp->z_sa_hdl, SA_ZPL_SYMLINK(zfsvfs),
		    buffer, inlen, tx);
	else
		zfs_sa_symlink(zp, buffer, inlen, tx);
	mutex_exit(&zp->z_lock);

	zp->z_size = inlen;
	(void) sa_update(zp->z_sa_hdl, SA_ZPL_SIZE(zfsvfs),
	    &zp->z_size, sizeof (zp->z_size), tx);

	dmu_tx_commit(tx);

	if (error == 0 && zfsvfs->z_os->os_sync == ZFS_SYNC_ALWAYS)
		error = zil_commit(zfsvfs->z_log, 0);

out:
	VN_RELE(dvp);

	if (error != 0)
		Status = zfs_error_to_ntstatus(error);

	dprintf("%s: returning 0x%lx\n", __func__, Status);

	return (Status);
}

NTSTATUS
delete_reparse_point(PDEVICE_OBJECT DeviceObject, PIRP Irp,
    PIO_STACK_LOCATION IrpSp)
{
	NTSTATUS Status = STATUS_SUCCESS;
	PFILE_OBJECT FileObject = IrpSp->FileObject;
	DWORD inlen = IrpSp->Parameters.DeviceIoControl.InputBufferLength;
	void *buffer = Irp->AssociatedIrp.SystemBuffer;
	REPARSE_DATA_BUFFER *rdb = buffer;
	struct vnode *vp = IrpSp->FileObject->FsContext;

	if (!FileObject)
		return (STATUS_INVALID_PARAMETER);

	if (Irp->UserBuffer)
		return (STATUS_INVALID_PARAMETER);

	if (inlen < sizeof (ULONG)) {
		return (STATUS_INVALID_BUFFER_SIZE);
	}

	if (inlen < offsetof(REPARSE_DATA_BUFFER,
	    GenericReparseBuffer.DataBuffer))
		return (STATUS_INVALID_PARAMETER);

	if (rdb->ReparseDataLength > 0)
		return (STATUS_INVALID_PARAMETER);

	if (VN_HOLD(vp) != 0)
		return (STATUS_INVALID_PARAMETER);

	znode_t *zp = VTOZ(vp);
	zfs_ccb_t *zccb = FileObject->FsContext2;
	zfsvfs_t *zfsvfs = zp->z_zfsvfs;

	if (vfs_isrdonly(zfsvfs->z_vfs)) {
		/*
		 * Snapshot root: MountManager cleans up the junction at
		 * unmount.  Accept so the cleanup path completes without error.
		 */
		if (zfsvfs->z_issnap && zp->z_id == zfsvfs->z_root) {
			VN_RELE(vp);
			return (STATUS_SUCCESS);
		}
		VN_RELE(vp);
		return (STATUS_MEDIA_WRITE_PROTECTED);
	}

	// Is something mounted on here? We deny it, so that
	// it has to be unmounted by us first. We will remove
	// from list of mounts, before deleting reparse point
	if (zccb &&
	    vfs_has_mount(zccb->z_name_cache)) {
		dprintf("Denied due to being mountpoint\n");
		VN_RELE(vp);
		return (STATUS_CANNOT_DELETE);
	}

	if (zfsctl_is_node(zp)) {
		VN_RELE(vp);
		return (zfsctl_delete_reparse_point(zp));
	}

	/* Tag in the request must match what is stored on the file. */
	if (zp->z_pflags & ZFS_REPARSE) {
		ULONG existing_tag = get_reparse_tag(zp);
		if (existing_tag != 0 && rdb->ReparseTag != existing_tag) {
			VN_RELE(vp);
			return (STATUS_IO_REPARSE_TAG_MISMATCH);
		}
	}

	// Like zfs_symlink, write the data as SA attribute.
	znode_t *dzp = NULL;
	uint64_t parent;
	int error;

	// Fetch parent
	VERIFY(sa_lookup(zp->z_sa_hdl, SA_ZPL_PARENT(zfsvfs),
	    &parent, sizeof (parent)) == 0);
	error = zfs_zget(zfsvfs, parent, &dzp);
	if (error) {
		Status = STATUS_INVALID_PARAMETER;
		goto out;
	}

	dmu_tx_t	*tx;
	// boolean_t	fuid_dirtied;

	// Remove flags to indicate we are reparse point
	zp->z_pflags &= ~ZFS_REPARSE;

	// Start TX and save FLAGS, SIZE and SYMLINK to disk.
	// This code should probably call zfs_symlink()
top:
	tx = dmu_tx_create(zfsvfs->z_os);
	// fuid_dirtied = zfsvfs->z_fuid_dirty;

	dmu_tx_hold_zap(tx, dzp->z_id, FALSE, NULL); // name
	dmu_tx_hold_sa(tx, zp->z_sa_hdl, B_TRUE);
	zfs_sa_upgrade_txholds(tx, zp);
	zfs_sa_upgrade_txholds(tx, dzp);

	error = dmu_tx_assign(tx, DMU_TX_WAIT);
	if (error) {
		dmu_tx_abort(tx);
		if (error == ERESTART)
			goto top;
		goto out;
	}

	mutex_enter(&zp->z_lock);

	(void) sa_update(zp->z_sa_hdl, SA_ZPL_FLAGS(zfsvfs),
	    &zp->z_pflags, sizeof (zp->z_pflags), tx);

	if (zp->z_is_sa)
		error = sa_remove(zp->z_sa_hdl, SA_ZPL_SYMLINK(zfsvfs),
		    tx);
	else
		zfs_sa_symlink(zp, buffer, 0, tx);

	zp->z_size = 0;	// If dir size > 2 -> ENOTEMPTY
	(void) sa_update(zp->z_sa_hdl, SA_ZPL_SIZE(zfsvfs),
	    &zp->z_size, sizeof (zp->z_size), tx);

	mutex_exit(&zp->z_lock);

	dmu_tx_commit(tx);

	if (error == 0 && zfsvfs->z_os->os_sync == ZFS_SYNC_ALWAYS)
		error = zil_commit(zfsvfs->z_log, 0);

out:
	if (dzp != NULL)
		zrele(dzp);
	VN_RELE(vp);

	if (error != 0)
		Status = zfs_error_to_ntstatus(error);

	dprintf("%s: returning 0x%lx\n", __func__, Status);

	return (Status);
}

NTSTATUS
create_or_get_object_id(PDEVICE_OBJECT DeviceObject, PIRP Irp,
    PIO_STACK_LOCATION IrpSp)
{
	NTSTATUS Status = STATUS_NOT_IMPLEMENTED;
	PFILE_OBJECT FileObject = IrpSp->FileObject;
	DWORD inlen = IrpSp->Parameters.DeviceIoControl.OutputBufferLength;
	void *buffer = Irp->AssociatedIrp.SystemBuffer;
	FILE_OBJECTID_BUFFER *fob = buffer;

	if (!FileObject)
		return (STATUS_INVALID_PARAMETER);

	if (!fob || inlen < sizeof (FILE_OBJECTID_BUFFER)) {
		Irp->IoStatus.Information = sizeof (FILE_OBJECTID_BUFFER);
		return (STATUS_BUFFER_OVERFLOW);
	}

	struct vnode *vp = IrpSp->FileObject->FsContext;
	znode_t *zp = VTOZ(vp);
	zfsvfs_t *zfsvfs = zp->z_zfsvfs;

	// ObjectID is 16 bytes to identify the file
	// Should we do endian work here?
	// znode id + pool guid
	RtlCopyMemory(&fob->ObjectId[0], &zp->z_id, sizeof (UINT64));
	uint64_t guid = dmu_objset_fsid_guid(zfsvfs->z_os);
	RtlCopyMemory(&fob->ObjectId[sizeof (UINT64)], &guid, sizeof (UINT64));
	RtlZeroMemory(fob->ExtendedInfo, sizeof (fob->ExtendedInfo));

	Irp->IoStatus.Information = sizeof (FILE_OBJECTID_BUFFER);
	Status = STATUS_SUCCESS;
	return (Status);
}

NTSTATUS
set_sparse(PDEVICE_OBJECT DeviceObject, PIRP Irp,
    PIO_STACK_LOCATION IrpSp)
{
	uint64_t datalen =
	    IrpSp->Parameters.FileSystemControl.InputBufferLength;
	struct vnode *vp;
	znode_t *zp;

	if (!IrpSp->FileObject)
		return (STATUS_INVALID_PARAMETER);

	/* Buffer is optional */
	if (Irp->AssociatedIrp.SystemBuffer != NULL &&
	    datalen < sizeof (FILE_SET_SPARSE_BUFFER))
		return (STATUS_INVALID_PARAMETER);

	/* if given */
	FILE_SET_SPARSE_BUFFER *fssb = Irp->AssociatedIrp.SystemBuffer;

	vp = IrpSp->FileObject->FsContext;
	if (vp == NULL)
		return (STATUS_INVALID_PARAMETER);

	zp = VTOZ(vp);
	if (zp == NULL)
		return (STATUS_INVALID_PARAMETER);

	/* We should at least send events */

	return (STATUS_SUCCESS);
}

#if _WIN32_WINNT < _WIN32_WINNT_WIN8
typedef struct _FSCTL_GET_INTEGRITY_INFORMATION_BUFFER {
    USHORT ChecksumAlgorithm;
    USHORT Reserved;
    ULONG Flags;
    ULONG ChecksumChunkSizeInBytes;
    ULONG ClusterSizeInBytes;
} FSCTL_GET_INTEGRITY_INFORMATION_BUFFER,
	*PFSCTL_GET_INTEGRITY_INFORMATION_BUFFER;
#endif

NTSTATUS
get_integrity_information(PDEVICE_OBJECT DeviceObject, PIRP Irp,
    PIO_STACK_LOCATION IrpSp)
{
	uint64_t datalen =
	    IrpSp->Parameters.DeviceIoControl.OutputBufferLength;
	FSCTL_GET_INTEGRITY_INFORMATION_BUFFER *fgiib;

	fgiib = (FSCTL_GET_INTEGRITY_INFORMATION_BUFFER *)
	    Irp->AssociatedIrp.SystemBuffer;

	if (!IrpSp->FileObject)
		return (STATUS_INVALID_PARAMETER);

	if (!fgiib ||
	    datalen < sizeof (FSCTL_GET_INTEGRITY_INFORMATION_BUFFER))
		return (STATUS_INVALID_PARAMETER);

	fgiib->ChecksumAlgorithm = 0;
	fgiib->Reserved = 0;
	fgiib->Flags = 0;
	fgiib->ChecksumChunkSizeInBytes = 512;
	fgiib->ClusterSizeInBytes = 512;

	Irp->IoStatus.Information =
	    sizeof (FSCTL_GET_INTEGRITY_INFORMATION_BUFFER);

	return (STATUS_SUCCESS);
}

#if _WIN32_WINNT < _WIN32_WINNT_WIN8
typedef struct _FSCTL_SET_INTEGRITY_INFORMATION_BUFFER {
    USHORT ChecksumAlgorithm;
    USHORT Reserved;
    ULONG Flags;
} FSCTL_SET_INTEGRITY_INFORMATION_BUFFER,
	*PFSCTL_SET_INTEGRITY_INFORMATION_BUFFER;
#endif

NTSTATUS
set_integrity_information(PDEVICE_OBJECT DeviceObject, PIRP Irp,
    PIO_STACK_LOCATION IrpSp)
{
	uint64_t datalen =
	    IrpSp->Parameters.DeviceIoControl.InputBufferLength;

	if (!IrpSp->FileObject)
		return (STATUS_INVALID_PARAMETER);

	if (!Irp->AssociatedIrp.SystemBuffer ||
	    datalen < sizeof (FSCTL_SET_INTEGRITY_INFORMATION_BUFFER))
		return (STATUS_INVALID_PARAMETER);

	return (STATUS_SUCCESS);
}

NTSTATUS
duplicate_extents_to_file(PDEVICE_OBJECT DeviceObject, PIRP Irp,
    PIO_STACK_LOCATION IrpSp, boolean_t extended)
{
	NTSTATUS Status = STATUS_NOT_IMPLEMENTED;
	PFILE_OBJECT sourcefo = NULL;
	PFILE_OBJECT FileObject = IrpSp->FileObject;
	DWORD datalen = IrpSp->Parameters.DeviceIoControl.InputBufferLength;
	void *buffer = Irp->AssociatedIrp.SystemBuffer;
	struct vnode *outvp = NULL, *invp = NULL;
	znode_t *outzp = NULL, *inzp = NULL;
	zfsvfs_t *zfsvfs;
	uint64_t inoff, outoff;
	uint64_t length;

	dprintf("%s\n", extended ? "duplicate_extents_to_file_ex" :
	    "duplicate_extents_to_file");

	if (!FileObject)
		return (STATUS_INVALID_PARAMETER);
	outvp = FileObject->FsContext;
	if (outvp == NULL)
		return (STATUS_INVALID_PARAMETER);

	outzp = VTOZ(outvp);
	if (outzp == NULL)
		return (STATUS_INVALID_PARAMETER);

	zfsvfs = outzp->z_zfsvfs;

	if (vfs_isrdonly(zfsvfs->z_vfs))
		return (STATUS_MEDIA_WRITE_PROTECTED);

#if 0
	if (Irp->RequestorMode == UserMode &&
	    !(ccb->access & FILE_WRITE_DATA)) {
		return (STATUS_ACCESS_DENIED);
	}
#endif
	if (!vnode_isreg(outvp) && !vnode_islnk(outvp))
		return (STATUS_INVALID_PARAMETER);


	if (extended) {
#ifndef _DUPLICATE_EXTENTS_DATA_EX
		typedef struct _DUPLICATE_EXTENTS_DATA_EX {
		    SIZE_T Size;
		    HANDLE FileHandle;
		    LARGE_INTEGER SourceFileOffset;
		    LARGE_INTEGER TargetFileOffset;
		    LARGE_INTEGER ByteCount;
		    ULONG Flags;
		} DUPLICATE_EXTENTS_DATA_EX, *PDUPLICATE_EXTENTS_DATA_EX;
#endif
		DUPLICATE_EXTENTS_DATA_EX *dede =
		    (DUPLICATE_EXTENTS_DATA_EX *)buffer;

		if (!buffer || datalen < sizeof (DUPLICATE_EXTENTS_DATA_EX) ||
		    dede->Size != sizeof (DUPLICATE_EXTENTS_DATA_EX))
			return (STATUS_BUFFER_TOO_SMALL);

		if (dede->ByteCount.QuadPart == 0)
			return (STATUS_SUCCESS);

		inoff = dede->SourceFileOffset.QuadPart;
		outoff = dede->TargetFileOffset.QuadPart;
		length = dede->ByteCount.QuadPart;
		Status = ObReferenceObjectByHandle(dede->FileHandle, 0,
		    *IoFileObjectType, Irp->RequestorMode,
		    (void **)&sourcefo, NULL);

	} else {
#ifndef _DUPLICATE_EXTENTS_DATA
		typedef struct _DUPLICATE_EXTENTS_DATA {
		    HANDLE FileHandle;
		    LARGE_INTEGER SourceFileOffset;
		    LARGE_INTEGER TargetFileOffset;
		    LARGE_INTEGER ByteCount;
		} DUPLICATE_EXTENTS_DATA, *PDUPLICATE_EXTENTS_DATA;
#endif

		DUPLICATE_EXTENTS_DATA *ded =
		    (DUPLICATE_EXTENTS_DATA *)buffer;
		if (!buffer || datalen < sizeof (DUPLICATE_EXTENTS_DATA))
			return (STATUS_BUFFER_TOO_SMALL);
		if (ded->ByteCount.QuadPart == 0)
			return (STATUS_SUCCESS);

		inoff = ded->SourceFileOffset.QuadPart;
		outoff = ded->TargetFileOffset.QuadPart;
		length = ded->ByteCount.QuadPart;
		Status = ObReferenceObjectByHandle(ded->FileHandle, 0,
		    *IoFileObjectType, Irp->RequestorMode,
		    (void **)&sourcefo, NULL);
	}

	if (!NT_SUCCESS(Status)) {
		dprintf("ObReferenceObjectByHandle returned %08lx\n", Status);
		return (Status);
	}

	invp = sourcefo->FsContext;
	if (invp == NULL || VN_HOLD(invp) != 0) {
		invp = NULL;
		Status = STATUS_INVALID_PARAMETER;
		goto out;
	}
	/* Holding invp */

	inzp = VTOZ(invp);
	if (inzp == NULL) {
		Status = STATUS_INVALID_PARAMETER;
		goto out;
	}

	/* From here, release sourcefo */

	/*
	 * zfs_clone_range(znode_t *inzp, uint64_t *inoffp, znode_t *outzp,
	 *    uint64_t *outoffp, uint64_t *lenp, cred_t *cr)
	 */

	Status = zfs_clone_range(inzp, &inoff, outzp, &outoff,
	    &length, NULL);

out:
	ObDereferenceObject(sourcefo);
	if (invp != NULL)
		VN_RELE(invp);
	return (SET_ERROR(Status));
}

#ifndef FILE_REGION_INFO
typedef struct _FILE_REGION_INFO {
	LONGLONG FileOffset;
	LONGLONG Length;
	ULONG Usage;
	ULONG Reserved;
} FILE_REGION_INFO, *PFILE_REGION_INFO;
#endif

#ifndef FILE_REGION_OUTPUT
typedef struct _FILE_REGION_OUTPUT {
	ULONG Flags;
	ULONG TotalRegionEntryCount;
	ULONG RegionEntryCount;
	ULONG Reserved;
	FILE_REGION_INFO Region[1];
} FILE_REGION_OUTPUT, *PFILE_REGION_OUTPUT;
#endif

#ifndef FILE_REGION_USAGE_VALID_CACHED_DATA
#define	FILE_REGION_USAGE_VALID_CACHED_DATA	0x00000001
#endif

NTSTATUS
query_file_regions(PDEVICE_OBJECT DeviceObject, PIRP Irp,
    PIO_STACK_LOCATION IrpSp)
{
	uint64_t inlen =
	    IrpSp->Parameters.FileSystemControl.InputBufferLength;
	uint64_t outlen =
	    IrpSp->Parameters.FileSystemControl.OutputBufferLength;

	if (!IrpSp->FileObject)
		return (STATUS_INVALID_PARAMETER);

	/*
	 * Input is optional.  Only validate size when the caller
	 * actually provides an input buffer (inlen > 0).  For
	 * METHOD_BUFFERED, SystemBuffer is always allocated regardless
	 * of inlen, so we cannot use its NULL-ness to detect input.
	 */
	if (inlen > 0 && inlen < sizeof (FILE_REGION_INFO))
		return (STATUS_BUFFER_TOO_SMALL);

	if (Irp->AssociatedIrp.SystemBuffer == NULL ||
	    outlen < sizeof (FILE_REGION_OUTPUT)) {
		Irp->IoStatus.Information = sizeof (FILE_REGION_OUTPUT);
		return (STATUS_BUFFER_TOO_SMALL);
	}

	struct vnode *vp = IrpSp->FileObject->FsContext;
	if (vp == NULL)
		return (STATUS_INVALID_PARAMETER);

	znode_t *zp = VTOZ(vp);
	if (zp == NULL)
		return (STATUS_INVALID_PARAMETER);

	/*
	 * If the caller supplies an input region filter, validate it.
	 * Save the values now because writing the output reuses the
	 * same SystemBuffer (METHOD_BUFFERED).
	 */
	LONGLONG valid_extent =
	    (LONGLONG)vp->FileHeader.ValidDataLength.QuadPart;
	LONGLONG filter_offset = 0;
	LONGLONG filter_length = valid_extent;
	if (inlen > 0) {
		FILE_REGION_INFO *fri =
		    (FILE_REGION_INFO *)Irp->AssociatedIrp.SystemBuffer;
		if ((ULONGLONG)fri->FileOffset > (ULONGLONG)INT64_MAX ||
		    (ULONGLONG)fri->Length > (ULONGLONG)INT64_MAX)
			return (STATUS_INVALID_PARAMETER);
		if ((fri->FileOffset + fri->Length) > INT64_MAX)
			return (STATUS_INVALID_PARAMETER);
		if ((fri->Usage & 3) == 0)
			return (STATUS_INVALID_PARAMETER);
		filter_offset = fri->FileOffset;
		filter_length = fri->Length;

		/*
		 * Never claim a region is valid cached data beyond what the
		 * file actually has committed.  This used to echo the
		 * caller's own query straight back as "confirmed valid"
		 * with no check at all -- which can tell a caller (e.g. VHD
		 * creation/attach tooling, which uses this FSCTL to verify
		 * a fixed-size VHD is fully realized) that data exists when
		 * it does not.  Clamp to what ValidDataLength actually
		 * supports, or report zero regions if the query starts
		 * beyond it entirely.
		 */
		if (filter_offset >= valid_extent)
			filter_length = 0;
		else if (filter_offset + filter_length > valid_extent)
			filter_length = valid_extent - filter_offset;
	}

	FILE_REGION_OUTPUT *fro =
	    (FILE_REGION_OUTPUT *)Irp->AssociatedIrp.SystemBuffer;

	fro->Flags = 0;
	fro->TotalRegionEntryCount = (filter_length > 0) ? 1 : 0;
	fro->RegionEntryCount = (filter_length > 0) ? 1 : 0;
	fro->Reserved = 0;

	if (filter_length > 0) {
		fro->Region[0].FileOffset = filter_offset;
		fro->Region[0].Length = filter_length;
		fro->Region[0].Usage = FILE_REGION_USAGE_VALID_CACHED_DATA;
		fro->Region[0].Reserved = 0;
	}

	Irp->IoStatus.Information = sizeof (FILE_REGION_OUTPUT);
	return (STATUS_SUCCESS);
}

#ifndef OPLOCK_LEVEL_CACHE_NONE
#define	OPLOCK_LEVEL_CACHE_NONE    0x00000000
#endif

NTSTATUS
request_oplock(PDEVICE_OBJECT DeviceObject, PIRP *PIrp,
    PIO_STACK_LOCATION IrpSp)
{
	NTSTATUS Status = 0;
	uint32_t fsctl = IrpSp->Parameters.FileSystemControl.FsControlCode;
	PFILE_OBJECT FileObject = IrpSp->FileObject;
	PREQUEST_OPLOCK_INPUT_BUFFER buf = NULL;
	boolean_t oplock_request = FALSE, oplock_ack = FALSE;
	ULONG oplock_count = 0;
	PIRP Irp = *PIrp;
	int error = 0;

	if (FileObject == NULL)
		return (STATUS_INVALID_PARAMETER);

	struct vnode *vp = IrpSp->FileObject->FsContext;
	zfs_ccb_t *zccb = IrpSp->FileObject->FsContext2;

	if (vp == NULL || zccb == NULL)
		return (STATUS_INVALID_PARAMETER);

	znode_t *zp = VTOZ(vp);
	zfsvfs_t *zfsvfs = zp->z_zfsvfs;

	if ((error = zfs_enter(zfsvfs, FTAG)) != 0)
		return (error);  // This returns EIO if fail
	/* HOLD count, no returns from here. */

	if (VN_HOLD(vp) != 0) {
		Status = STATUS_INVALID_PARAMETER;
		goto exit;
	}

	if (!vnode_isreg(vp) && !vnode_isdir(vp)) {
		Status = STATUS_INVALID_PARAMETER;
		goto out;
	}

	//
	// Determine shared vs exclusive request using FsRtl helper
	//
	const BOOLEAN isShared = FsRtlOplockIsSharedRequest(Irp);

	if (fsctl == FSCTL_REQUEST_OPLOCK) {
		if (IrpSp->Parameters.FileSystemControl.InputBufferLength <
		    sizeof (REQUEST_OPLOCK_INPUT_BUFFER)) {
			Status = STATUS_BUFFER_TOO_SMALL;
			goto out;
		}
		if (IrpSp->Parameters.FileSystemControl.OutputBufferLength <
		    sizeof (REQUEST_OPLOCK_OUTPUT_BUFFER)) {
			Status = STATUS_BUFFER_TOO_SMALL;
			goto out;
		}

		if (vnode_isdir(vp)) {
			if (!isShared) {
				Status = STATUS_INVALID_PARAMETER;
				goto out;
			}
		}

		buf = Irp->AssociatedIrp.SystemBuffer;

		// flags are mutually exclusive
		if (buf->Flags & REQUEST_OPLOCK_INPUT_FLAG_REQUEST &&
		    buf->Flags & REQUEST_OPLOCK_INPUT_FLAG_ACK) {
			Status = STATUS_INVALID_PARAMETER;
			goto out;
		}

		oplock_request = buf->Flags & REQUEST_OPLOCK_INPUT_FLAG_REQUEST;
		oplock_ack = buf->Flags & REQUEST_OPLOCK_INPUT_FLAG_ACK;

		if (!oplock_request && !oplock_ack) {
			Status = STATUS_INVALID_PARAMETER;
			goto out;
		}
	}

	if (vnode_isdir(vp)) {
		switch (fsctl) {
		case FSCTL_REQUEST_OPLOCK_LEVEL_1:
		case FSCTL_REQUEST_OPLOCK_LEVEL_2:
		case FSCTL_REQUEST_BATCH_OPLOCK:
		case FSCTL_REQUEST_FILTER_OPLOCK:
			Status = STATUS_INVALID_PARAMETER;
			goto out;
		default:
			break;
		}
	}

	//
	// OPEN COUNT per docs:
	//  - Exclusive request: number of *user* handles to this stream.
	//  - Shared request: 0 if no byte-range locks are present.
	//
	ULONG openCount = 0;
	if (isShared) {
		openCount = FsRtlAreThereCurrentOrInProgressFileLocks(
		    &vp->lock) ? 1 : 0;
	} else {
		// Your per-FCB user handle count
		openCount = (int)vp->v_usecount;
	}

	//
	// Is this an ACK path for v2 FSCTL_REQUEST_OPLOCK?
	//
	BOOLEAN isAck = FALSE;
	if (fsctl == FSCTL_REQUEST_OPLOCK && buf) {
		isAck = (buf->Flags & REQUEST_OPLOCK_INPUT_FLAG_ACK) ?
		    TRUE : FALSE;
	}

	//
	// Acquire per Microsoft's oplock sync guidance:
	//  - Exclusive for request
	//  - Shared for ACK
	//
	if (isAck) {
		ExAcquireResourceSharedLite(vp->FileHeader.Resource, TRUE);
	} else {
		ExAcquireResourceExclusiveLite(vp->FileHeader.Resource, TRUE);
	}

	ULONG fsctrlFlags = 0;

	Status = FsRtlOplockFsctrlEx(
	    vp_oplock(vp),   // your FSRTL_OPLOCK*
	    Irp,
	    openCount,
	    fsctrlFlags);

	ExReleaseResourceLite(vp->FileHeader.Resource);

	if (NT_SUCCESS(Status)) {
		if (fsctl == FSCTL_REQUEST_OPLOCK) {
			REQUEST_OPLOCK_INPUT_BUFFER *in =
			    (REQUEST_OPLOCK_INPUT_BUFFER *)
			    Irp->AssociatedIrp.SystemBuffer;
			REQUEST_OPLOCK_OUTPUT_BUFFER *out =
			    (REQUEST_OPLOCK_OUTPUT_BUFFER *)
			    Irp->AssociatedIrp.SystemBuffer;

			if (in && (in->Flags &
			    REQUEST_OPLOCK_INPUT_FLAG_REQUEST) &&
			    out &&
			    out->NewOplockLevel != OPLOCK_LEVEL_CACHE_NONE) {
				if (!zccb->HoldsOplock) {
					atomic_inc_64(&vp->OplockRefCount);
					zccb->HoldsOplock = B_TRUE;
				}
			} else if (in && (in->Flags &
			    REQUEST_OPLOCK_INPUT_FLAG_ACK) &&
			    out &&
			    out->NewOplockLevel == OPLOCK_LEVEL_CACHE_NONE) {
				if (zccb->HoldsOplock) {
					zccb->HoldsOplock = B_FALSE;
					atomic_dec_64(&vp->OplockRefCount);
				}
			}
		} else {
			// Legacy l1/l2/filter/batch requests: success == grant
			switch (fsctl) {
			case FSCTL_REQUEST_OPLOCK_LEVEL_1:
			case FSCTL_REQUEST_OPLOCK_LEVEL_2:
			case FSCTL_REQUEST_FILTER_OPLOCK:
			case FSCTL_REQUEST_BATCH_OPLOCK:
				if (!zccb->HoldsOplock) {
					atomic_inc_64(&vp->OplockRefCount);
					zccb->HoldsOplock = B_TRUE;
				}
				break;
			default:
				break;
			}
		}
	} else if (Status == STATUS_PENDING) {
		// Don't bump here; grant isn't established yet.
		// CLEANUP will backstop decrement if needed.
	}

	*PIrp = NULL; // FsRtl has completed it already

out:
	VN_RELE(vp);
exit:
	zfs_exit(zfsvfs, FTAG);

	if (Status == STATUS_PENDING) {
		// IoMarkIrpPending(Irp);
	}

	return (Status);
}

/*
 * FSCTL_OFFLOAD_READ / FSCTL_OFFLOAD_WRITE
 *
 * Encode source-file identity in the 512-byte opaque token Windows passes
 * between the two calls.  On OFFLOAD_WRITE we first attempt a ZFS block
 * clone (zero-copy reflink); if the pool feature is inactive we fall back
 * to a kernel read/write loop so that applications that do not fall back
 * to regular ReadFile/WriteFile (cmd.exe, older copy utilities) still work.
 */

#ifndef FSCTL_OFFLOAD_READ
#define	FSCTL_OFFLOAD_READ \
    CTL_CODE(FILE_DEVICE_FILE_SYSTEM, 153, METHOD_BUFFERED, FILE_READ_ACCESS)
#define	FSCTL_OFFLOAD_WRITE \
    CTL_CODE(FILE_DEVICE_FILE_SYSTEM, 154, METHOD_BUFFERED, FILE_WRITE_ACCESS)

typedef struct _FSCTL_OFFLOAD_READ_INPUT {
	ULONG Size;
	ULONG Flags;
	ULONG TokenTimeToLive;
	ULONG Reserved;
	ULONGLONG FileOffset;
	ULONGLONG CopyLength;
} FSCTL_OFFLOAD_READ_INPUT, *PFSCTL_OFFLOAD_READ_INPUT;

typedef struct _FSCTL_OFFLOAD_READ_OUTPUT {
	ULONG Size;
	ULONG Flags;
	ULONGLONG TransferLength;
	UCHAR Token[512];
} FSCTL_OFFLOAD_READ_OUTPUT, *PFSCTL_OFFLOAD_READ_OUTPUT;

typedef struct _FSCTL_OFFLOAD_WRITE_INPUT {
	ULONG Size;
	ULONG Flags;
	ULONGLONG FileOffset;
	ULONGLONG CopyLength;
	ULONGLONG TransferOffset;
	UCHAR Token[512];
} FSCTL_OFFLOAD_WRITE_INPUT, *PFSCTL_OFFLOAD_WRITE_INPUT;

typedef struct _FSCTL_OFFLOAD_WRITE_OUTPUT {
	ULONG Size;
	ULONG Flags;
	ULONGLONG LengthWritten;
} FSCTL_OFFLOAD_WRITE_OUTPUT, *PFSCTL_OFFLOAD_WRITE_OUTPUT;

#endif /* FSCTL_OFFLOAD_READ */

#define	ZFS_OFFLOAD_TOKEN_MAGIC	0x5A46534F46464C44ULL	/* "ZFSOFFLD" */
#define	ZFS_OFFLOAD_COPY_CHUNK	(256 * 1024)

typedef struct {
	uint64_t magic;
	uint64_t spa_guid;
	uint64_t objset_id;
	uint64_t z_id;		/* source znode object ID */
	uint64_t src_offset;
	uint64_t src_length;
} zfs_offload_token_t;

static NTSTATUS
fsctl_offload_read(PDEVICE_OBJECT DeviceObject, PIRP Irp,
    PIO_STACK_LOCATION IrpSp)
{
	PFILE_OBJECT FileObject = IrpSp->FileObject;
	ULONG inlen = IrpSp->Parameters.FileSystemControl.InputBufferLength;
	ULONG outlen = IrpSp->Parameters.FileSystemControl.OutputBufferLength;
	void *buf = Irp->AssociatedIrp.SystemBuffer;
	FSCTL_OFFLOAD_READ_INPUT in;
	FSCTL_OFFLOAD_READ_OUTPUT *out;
	zfs_offload_token_t *tok;
	vnode_t *vp;
	znode_t *zp;
	zfsvfs_t *zfsvfs;
	uint64_t xfer_len;

	if (!FileObject || !buf)
		return (STATUS_INVALID_PARAMETER);

	if (inlen < sizeof (FSCTL_OFFLOAD_READ_INPUT) ||
	    outlen < sizeof (FSCTL_OFFLOAD_READ_OUTPUT))
		return (STATUS_BUFFER_TOO_SMALL);

	/* Copy input before we overwrite the shared SystemBuffer */
	RtlCopyMemory(&in, buf, sizeof (in));

	if (in.Size != sizeof (FSCTL_OFFLOAD_READ_INPUT))
		return (STATUS_INVALID_PARAMETER);

	vp = FileObject->FsContext;
	if (!vp || !vnode_isreg(vp))
		return (STATUS_INVALID_PARAMETER);

	zp = VTOZ(vp);
	if (!zp)
		return (STATUS_INVALID_PARAMETER);

	zfsvfs = zp->z_zfsvfs;

	if (in.FileOffset >= zp->z_size)
		return (STATUS_END_OF_FILE);

	xfer_len = in.CopyLength;
	if (in.FileOffset + xfer_len > zp->z_size)
		xfer_len = zp->z_size - in.FileOffset;

	out = (FSCTL_OFFLOAD_READ_OUTPUT *)buf;
	RtlZeroMemory(out, sizeof (*out));
	out->Size = sizeof (FSCTL_OFFLOAD_READ_OUTPUT);
	out->TransferLength = xfer_len;

	tok = (zfs_offload_token_t *)out->Token;
	tok->magic = ZFS_OFFLOAD_TOKEN_MAGIC;
	tok->spa_guid = spa_guid(dmu_objset_spa(zfsvfs->z_os));
	tok->objset_id = dmu_objset_id(zfsvfs->z_os);
	tok->z_id = zp->z_id;
	tok->src_offset = in.FileOffset;
	tok->src_length = xfer_len;

	Irp->IoStatus.Information = sizeof (FSCTL_OFFLOAD_READ_OUTPUT);
	return (STATUS_SUCCESS);
}

static NTSTATUS
fsctl_offload_write(PDEVICE_OBJECT DeviceObject, PIRP Irp,
    PIO_STACK_LOCATION IrpSp)
{
	PFILE_OBJECT FileObject = IrpSp->FileObject;
	ULONG inlen = IrpSp->Parameters.FileSystemControl.InputBufferLength;
	ULONG outlen = IrpSp->Parameters.FileSystemControl.OutputBufferLength;
	void *buf = Irp->AssociatedIrp.SystemBuffer;
	FSCTL_OFFLOAD_WRITE_INPUT in;
	FSCTL_OFFLOAD_WRITE_OUTPUT *out;
	zfs_offload_token_t tok;
	vnode_t *dst_vp;
	znode_t *dst_zp, *src_zp = NULL;
	zfsvfs_t *zfsvfs;
	uint64_t src_offset, dst_offset, copy_len, done = 0;
	NTSTATUS Status = STATUS_SUCCESS;
	IO_STATUS_BLOCK iosb;
	int err;

	if (!FileObject || !buf)
		return (STATUS_INVALID_PARAMETER);

	if (inlen < sizeof (FSCTL_OFFLOAD_WRITE_INPUT) ||
	    outlen < sizeof (FSCTL_OFFLOAD_WRITE_OUTPUT))
		return (STATUS_BUFFER_TOO_SMALL);

	/* Copy input before we overwrite the shared SystemBuffer */
	RtlCopyMemory(&in, buf, sizeof (in));
	RtlCopyMemory(&tok, in.Token, sizeof (tok));

	if (in.Size != sizeof (FSCTL_OFFLOAD_WRITE_INPUT))
		return (STATUS_INVALID_PARAMETER);

	/* Not our token — tell the caller to fall back to regular copy */
	if (tok.magic != ZFS_OFFLOAD_TOKEN_MAGIC)
		return (STATUS_NOT_SUPPORTED);

	dst_vp = FileObject->FsContext;
	if (!dst_vp || !vnode_isreg(dst_vp))
		return (STATUS_INVALID_PARAMETER);

	dst_zp = VTOZ(dst_vp);
	if (!dst_zp)
		return (STATUS_INVALID_PARAMETER);

	zfsvfs = dst_zp->z_zfsvfs;

	/* Cross-volume: token is not serviceable here */
	if (tok.spa_guid != spa_guid(dmu_objset_spa(zfsvfs->z_os)) ||
	    tok.objset_id != dmu_objset_id(zfsvfs->z_os))
		return (STATUS_NOT_SUPPORTED);

	if (vfs_isrdonly(zfsvfs->z_vfs))
		return (STATUS_MEDIA_WRITE_PROTECTED);

	if (in.CopyLength == 0)
		goto fill_output;

	if (in.TransferOffset > tok.src_length)
		return (STATUS_INVALID_PARAMETER);

	src_offset = tok.src_offset + in.TransferOffset;
	dst_offset = in.FileOffset;
	copy_len = in.CopyLength;
	if (in.TransferOffset + copy_len > tok.src_length)
		copy_len = tok.src_length - in.TransferOffset;

	err = zfs_zget(zfsvfs, tok.z_id, &src_zp);
	if (err != 0)
		return (STATUS_INVALID_PARAMETER);

	/*
	 * Flush destination cache before we write beneath it.  Use
	 * PagingIoResource, not FileHeader.Resource: this is the same
	 * "flush before a direct write" pattern the no-cache write path in
	 * zfs_write_wrap uses (see its PagingIoResource acquisition around
	 * CcFlushCache/CcPurgeCacheSection), and it keeps this path on the
	 * one resource fastio_write/zfs_write_wrap/zfs_couplefileobject all
	 * serialise FileHeader.FileSize/AllocationSize/ValidDataLength
	 * changes through.
	 */
	ExAcquireResourceExclusiveLite(dst_vp->FileHeader.PagingIoResource,
	    TRUE);

	CcFlushCache(FileObject->SectionObjectPointer, NULL, 0, &iosb);

	/*
	 * Release before the copy itself: the copy (block clone, or the
	 * chunked read/write loop below) can run for a long time on a large
	 * transfer, and there is no coherency reason to hold
	 * PagingIoResource across it -- doing so would serialise every
	 * concurrent fastio_write/cached write to this file for the
	 * duration, same rationale as the no-cache write path.
	 */
	ExReleaseResourceLite(dst_vp->FileHeader.PagingIoResource);

	/* Attempt zero-copy block clone first */
	{
		uint64_t inoff = src_offset, outoff = dst_offset;
		uint64_t clone_len = copy_len;
		err = zfs_clone_range(src_zp, &inoff, dst_zp,
		    &outoff, &clone_len, NULL);
		if (err == 0)
			done = clone_len;
	}

	if (done == 0) {
		/* Block clone unavailable: kernel read/write loop */
		uint8_t *kbuf = kmem_alloc(ZFS_OFFLOAD_COPY_CHUNK, KM_SLEEP);

		while (done < copy_len) {
			uint64_t chunk = MIN(ZFS_OFFLOAD_COPY_CHUNK,
			    copy_len - done);
			struct iovec iov;
			zfs_uio_t uio;
			uint64_t xferred;

			iov.iov_base = kbuf;
			iov.iov_len = (size_t)chunk;
			zfs_uio_iovec_init(&uio, &iov, 1,
			    src_offset + done, UIO_SYSSPACE, chunk, 0);
			Status = zfs_read(src_zp, &uio, 0, NULL);
			if (Status != STATUS_SUCCESS)
				break;
			xferred = chunk - zfs_uio_resid(&uio);
			if (xferred == 0)
				break;

			iov.iov_base = kbuf;
			iov.iov_len = (size_t)xferred;
			zfs_uio_iovec_init(&uio, &iov, 1,
			    dst_offset + done, UIO_SYSSPACE, xferred, 0);
			Status = zfs_write(dst_zp, &uio, 0, NULL);
			if (Status != STATUS_SUCCESS)
				break;
			if (zfs_uio_resid(&uio) != 0) {
				done += xferred - zfs_uio_resid(&uio);
				break;
			}
			done += xferred;
		}

		kmem_free(kbuf, ZFS_OFFLOAD_COPY_CHUNK);
	}

	/*
	 * Update Windows header if the destination was extended.  Re-acquire
	 * PagingIoResource here (never FileHeader.Resource -- CcSetFileSizes
	 * can trigger a recursive fault-in that needs FileHeader.Resource
	 * internally, the same self-recursion hazard documented in
	 * fastio_write/zfs_write_wrap) so this size bump and CcSetFileSizes
	 * call are atomic with respect to a concurrent fastio_write's
	 * FileHeader.FileSize bound check.  Without this, fastio_write on
	 * another handle to the same vnode could observe FileSize grown to
	 * cover data this offload write hasn't finished registering with the
	 * Cache Manager's own section bookkeeping, and race
	 * CcMapAndCopyInToCache into MmMapViewInSystemCache over a view Cc
	 * doesn't think exists yet (BSOD MEMORY_MANAGEMENT, subtype
	 * 0x103087) -- the same failure mode zfs_couplefileobject's
	 * SharedCacheMap check exists to prevent.
	 */
	ExAcquireResourceExclusiveLite(dst_vp->FileHeader.PagingIoResource,
	    TRUE);

	if (NT_SUCCESS(Status) &&
	    (LONGLONG)(dst_offset + done) >
	    dst_vp->FileHeader.FileSize.QuadPart) {
		dst_vp->FileHeader.FileSize.QuadPart =
		    (LONGLONG)(dst_offset + done);
		dst_vp->FileHeader.ValidDataLength =
		    dst_vp->FileHeader.FileSize;
		if (FileObject->SectionObjectPointer &&
		    CcIsFileCached(FileObject)) {
			CcSetFileSizes(FileObject,
			    (PCC_FILE_SIZES)
			    &dst_vp->FileHeader.AllocationSize);
		}
	}

	ExReleaseResourceLite(dst_vp->FileHeader.PagingIoResource);

	if (FileObject->SectionObjectPointer &&
	    FileObject->SectionObjectPointer->DataSectionObject) {
		LARGE_INTEGER dst_li;
		dst_li.QuadPart = (LONGLONG)dst_offset;
		CcPurgeCacheSection(FileObject->SectionObjectPointer,
		    &dst_li, (ULONG)MIN(done, MAXULONG), FALSE);
	}

	zrele(src_zp);

	if (!NT_SUCCESS(Status))
		return (Status);

fill_output:
	out = (FSCTL_OFFLOAD_WRITE_OUTPUT *)buf;
	out->Size = sizeof (FSCTL_OFFLOAD_WRITE_OUTPUT);
	out->Flags = 0;
	out->LengthWritten = done;
	Irp->IoStatus.Information = sizeof (FSCTL_OFFLOAD_WRITE_OUTPUT);
	return (STATUS_SUCCESS);
}

NTSTATUS
user_fs_request(PDEVICE_OBJECT DeviceObject, PIRP *PIrp,
    PIO_STACK_LOCATION IrpSp)
{
	NTSTATUS Status = STATUS_INVALID_DEVICE_REQUEST;
	PIRP Irp = *PIrp;

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
		{
			Status = STATUS_VOLUME_NOT_MOUNTED;
			mount_t *zmo;
			zmo = DeviceObject->DeviceExtension;
			zfsvfs_t *zfsvfs = vfs_fsprivate(zmo);
			if (zfsvfs) {
				Status = STATUS_VOLUME_MOUNTED;
				if (zfsvfs->z_unmounted)
					Status = STATUS_VERIFY_REQUIRED;
			}
		}
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
		PMDL mdl = NULL;

		VolumeState = MapUserBuffer(Irp,
		    IrpSp->Parameters.FileSystemControl.OutputBufferLength,
		    IoWriteAccess, &mdl);

		if (VolumeState == NULL) {
			Status = STATUS_INSUFFICIENT_RESOURCES;
			break;
		}

		if (IrpSp->Parameters.FileSystemControl.OutputBufferLength <
		    sizeof (ULONG)) {
			Status = STATUS_INVALID_PARAMETER;
			UnMapUserBuffer(mdl);
			break;
		}

		*VolumeState = 0;
		if (0)
			SetFlag(*VolumeState, VOLUME_IS_DIRTY);
		Irp->IoStatus.Information = sizeof (ULONG);
		UnMapUserBuffer(mdl);
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
	case FSCTL_DELETE_REPARSE_POINT:
		dprintf("    FSCTL_DELETE_REPARSE_POINT\n");
		Status = delete_reparse_point(DeviceObject, Irp, IrpSp);
		break;
	case FSCTL_CREATE_OR_GET_OBJECT_ID:
		dprintf("    FSCTL_CREATE_OR_GET_OBJECT_ID\n");
		Status = create_or_get_object_id(DeviceObject, Irp, IrpSp);
		break;
	case FSCTL_REQUEST_OPLOCK:
	case FSCTL_REQUEST_OPLOCK_LEVEL_1:
	case FSCTL_REQUEST_OPLOCK_LEVEL_2:
	case FSCTL_REQUEST_BATCH_OPLOCK:
	case FSCTL_REQUEST_FILTER_OPLOCK:
	case FSCTL_OPBATCH_ACK_CLOSE_PENDING:
	case FSCTL_OPLOCK_BREAK_ACK_NO_2:
	case FSCTL_OPLOCK_BREAK_ACKNOWLEDGE:
	case FSCTL_OPLOCK_BREAK_NOTIFY:
		dprintf("    FSCTL_REQUEST_OPLOCK: \n");
		Status = request_oplock(DeviceObject, PIrp, IrpSp);
		break;
	case FSCTL_FILESYSTEM_GET_STATISTICS:
		dprintf("    FSCTL_FILESYSTEM_GET_STATISTICS: \n");
		{
		/*
		 * srv2.sys checks that the per-CPU block contains both the
		 * base FILESYSTEM_STATISTICS header AND the NTFS_STATISTICS
		 * extension.  Returning only the 56-byte header (SizeOf-
		 * CompleteStructure == 56) causes srv2.sys to bail before
		 * issuing FSCTL_SRV_ENUMERATE_SNAPSHOTS, producing
		 * STATUS_INSUFFICIENT_RESOURCES at the SMB client.
		 *
		 * Layout: [FILESYSTEM_STATISTICS | NTFS_STATISTICS] × ncpus
		 * SizeOfCompleteStructure is the per-CPU block size.
		 */
		/*
		 * Per-CPU block: FILESYSTEM_STATISTICS + NTFS_STATISTICS.
		 * Always return STATUS_SUCCESS — srv2.sys loops forever on
		 * STATUS_BUFFER_TOO_SMALL without growing its buffer.
		 * If the buffer only holds the base header, fill that and
		 * set SizeOfCompleteStructure so the caller knows to retry
		 * with blksz * ncpus bytes.
		 */
		ULONG blksz = (ULONG)(sizeof (FILESYSTEM_STATISTICS) +
		    sizeof (NTFS_STATISTICS));
		ULONG ncpus =
		    KeQueryMaximumProcessorCountEx(ALL_PROCESSOR_GROUPS);
		ULONG outlen =
		    IrpSp->Parameters.FileSystemControl.OutputBufferLength;

		if (outlen < sizeof (FILESYSTEM_STATISTICS)) {
			Irp->IoStatus.Information = blksz * ncpus;
			Status = STATUS_BUFFER_TOO_SMALL;
			break;
		}

		ULONG fill = min(ncpus, outlen / blksz);

		if (fill > 0) {
			memset(Irp->AssociatedIrp.SystemBuffer, 0,
			    fill * blksz);
			for (ULONG i = 0; i < fill; i++) {
				FILESYSTEM_STATISTICS *fss =
				    (FILESYSTEM_STATISTICS *)
				    ((UCHAR *)Irp->AssociatedIrp.SystemBuffer +
				    i * blksz);
				fss->Version = 1;
				fss->FileSystemType =
				    FILESYSTEM_STATISTICS_TYPE_NTFS;
				fss->SizeOfCompleteStructure = blksz;
			}
			Irp->IoStatus.Information = fill * blksz;
		} else {
			/* Buffer holds header but not NTFS extension. */
			memset(Irp->AssociatedIrp.SystemBuffer, 0,
			    sizeof (FILESYSTEM_STATISTICS));
			FILESYSTEM_STATISTICS *fss =
			    Irp->AssociatedIrp.SystemBuffer;
			fss->Version = 1;
			fss->FileSystemType = FILESYSTEM_STATISTICS_TYPE_NTFS;
			fss->SizeOfCompleteStructure = blksz;
			Irp->IoStatus.Information =
			    sizeof (FILESYSTEM_STATISTICS);
		}

		Status = STATUS_SUCCESS;
		dprintf("    FSCTL_FILESYSTEM_GET_STATISTICS: blksz=%lu "
		    "ncpus=%lu fill=%lu outlen=%lu\n", blksz, ncpus, fill,
		    outlen);
		}
		break;
	case FSCTL_QUERY_DEPENDENT_VOLUME:
		/*
		 * ZFS volumes are not backed by virtual disk files; there
		 * are zero dependent volumes.  Return SUCCESS with an empty
		 * array (Information = 0).
		 */
		dprintf("    FSCTL_QUERY_DEPENDENT_VOLUME: 0 entries\n");
		Irp->IoStatus.Information = 0;
		Status = STATUS_SUCCESS;
		break;

	case FSCTL_SET_SPARSE:
		dprintf("    FSCTL_SET_SPARSE\n");
		Status = set_sparse(DeviceObject, Irp, IrpSp);
		break;

#ifndef FSCTL_GET_INTEGRITY_INFORMATION
#define	FSCTL_GET_INTEGRITY_INFORMATION CTL_CODE(FILE_DEVICE_FILE_SYSTEM, \
	159, METHOD_BUFFERED, FILE_ANY_ACCESS)
#endif

#ifndef FSCTL_SET_INTEGRITY_INFORMATION
#define	FSCTL_SET_INTEGRITY_INFORMATION CTL_CODE(FILE_DEVICE_FILE_SYSTEM, \
	160, METHOD_BUFFERED, FILE_READ_DATA | FILE_WRITE_DATA)
#endif
	case FSCTL_GET_INTEGRITY_INFORMATION:
		dprintf("    FSCTL_GET_INTEGRITY_INFORMATION_BUFFER\n");
		Status = get_integrity_information(DeviceObject, Irp, IrpSp);
		break;

	case FSCTL_SET_INTEGRITY_INFORMATION:
		dprintf("    FSCTL_SET_INTEGRITY_INFORMATION_BUFFER\n");
		Status = set_integrity_information(DeviceObject, Irp, IrpSp);
		break;

#ifndef FSCTL_DUPLICATE_EXTENTS_TO_FILE
#define	FSCTL_DUPLICATE_EXTENTS_TO_FILE	CTL_CODE(FILE_DEVICE_FILE_SYSTEM, \
	209, METHOD_BUFFERED, FILE_WRITE_DATA)
#endif
	case FSCTL_DUPLICATE_EXTENTS_TO_FILE:
		dprintf("    FSCTL_DUPLICATE_EXTENTS_TO_FILE\n");
		Status = duplicate_extents_to_file(DeviceObject, Irp, IrpSp,
		    FALSE);
		break;

	case FSCTL_DUPLICATE_EXTENTS_TO_FILE_EX:
		dprintf("    FSCTL_DUPLICATE_EXTENTS_TO_FILE_EX\n");
		Status = duplicate_extents_to_file(DeviceObject, Irp, IrpSp,
		    TRUE);
		break;

#ifndef FSCTL_QUERY_FILE_REGIONS
#define	FSCTL_QUERY_FILE_REGIONS CTL_CODE(FILE_DEVICE_FILE_SYSTEM, \
	161, METHOD_BUFFERED, FILE_ANY_ACCESS)
#endif
	case FSCTL_QUERY_FILE_REGIONS:
		dprintf("    FSCTL_QUERY_FILE_REGIONS\n");
		Status = query_file_regions(DeviceObject, Irp, IrpSp);
		break;

	case FSCTL_ZFS_VOLUME_MOUNTPOINT: // backward compatible
		dprintf("    FSCTL_ZFS_VOLUME_MOUNTPOINT\n");
		Status = fsctl_zfs_volume_mountpoint(DeviceObject, Irp, IrpSp);
		break;

/*
 * FSCTL_QUERY_USN_JOURNAL (0x900F4): WDK defines this as
 * FILE_DEVICE_FILE_SYSTEM function 61, METHOD_BUFFERED.
 * twext.dll (Windows Previous Versions shell extension) checks for
 * FILE_SUPPORTS_USN_JOURNAL in FileFsAttributeInformation, then sends
 * this FSCTL. Return a fake but valid USN_JOURNAL_DATA so twext.dll
 * believes the volume has an active USN journal and proceeds to query
 * VSS providers for shadow copies.
 */
	case FSCTL_QUERY_USN_JOURNAL: /* 0x900F4 */
		dprintf("    FSCTL_QUERY_USN_JOURNAL:"
		    " returning fake journal\n");
		{
		ULONG outlen =
		    IrpSp->Parameters.FileSystemControl.OutputBufferLength;
		if (outlen < sizeof (USN_JOURNAL_DATA)) {
			Irp->IoStatus.Information = sizeof (USN_JOURNAL_DATA);
			Status = STATUS_BUFFER_TOO_SMALL;
			break;
		}
		USN_JOURNAL_DATA *ujd =
		    (USN_JOURNAL_DATA *)Irp->AssociatedIrp.SystemBuffer;
		RtlZeroMemory(ujd, sizeof (USN_JOURNAL_DATA));
		ujd->UsnJournalID = 1; /* non-zero = journal active */
		ujd->FirstUsn = 0;
		ujd->NextUsn = 1;
		ujd->LowestValidUsn = 0;
		ujd->MaxUsn = MAXLONGLONG;
		ujd->MaximumSize = 0x2000000; /* 32 MB */
		ujd->AllocationDelta = 0x800000; /* 8 MB */
		Irp->IoStatus.Information = sizeof (USN_JOURNAL_DATA);
		Status = STATUS_SUCCESS;
		}
		break;

/*
 * FSCTL_QUERY_VOLUME_CONTAINER_STATE (0x90390): twext.dll (Previous
 * Versions shell extension) sends this FSCTL and uses the return code
 * to decide whether to attempt the SMB loopback path.  NTFS returns
 * STATUS_INVALID_DEVICE_REQUEST; twext.dll sees that as the signal to
 * proceed with \\localhost\<share>\ → FSCTL_SRV_ENUMERATE_SNAPSHOTS.
 * Returning STATUS_SUCCESS here causes twext.dll to silently skip the
 * SMB loopback, so Previous Versions never appears for ZFS volumes.
 * Must stay INVALID_DEVICE_REQUEST to match NTFS behaviour.
 */
#ifndef FSCTL_QUERY_VOLUME_CONTAINER_STATE
#define	FSCTL_QUERY_VOLUME_CONTAINER_STATE \
	CTL_CODE(FILE_DEVICE_FILE_SYSTEM, 228, METHOD_BUFFERED, FILE_ANY_ACCESS)
#endif
	case FSCTL_QUERY_VOLUME_CONTAINER_STATE: /* 0x90390 */
		dprintf("    FSCTL_QUERY_VOLUME_CONTAINER_STATE:"
		    " returning INVALID_DEVICE_REQUEST (like NTFS)\n");
		Status = STATUS_INVALID_DEVICE_REQUEST;
		break;

/*
 * FSCTL_SRV_ENUMERATE_SNAPSHOTS (0x144064):
 * srv2.sys (Windows SMB Server) forwards this FSCTL from an SMB2 IOCTL
 * request to the underlying filesystem.  twext.dll (Previous Versions
 * shell extension) sends it when accessing a file via SMB loopback
 * (\\localhost\<share>\file) to discover shadow copies.  We enumerate
 * ZFS snapshots of the current dataset and return their creation times
 * formatted as @GMT-YYYY.MM.DD-HH.MM.SS UTC strings.
 *
 * Response layout (MS-SMB2 section 3.3.5.15.1):
 *   ULONG NumberOfSnapshots      - total count of snapshots
 *   ULONG NumberOfSnapshotsReturned - count of strings in this reply
 *   ULONG SnapshotArraySize      - byte size of Snapshots[] field
 *   WCHAR Snapshots[]            - multi-sz @GMT timestamps, NUL-terminated
 *
 * If the output buffer is too small for strings, we fill the header and
 * return STATUS_BUFFER_OVERFLOW so srv2.sys can re-issue with the right
 * buffer size (12 + SnapshotArraySize).
 */
#ifndef FSCTL_SRV_ENUMERATE_SNAPSHOTS
#define	FSCTL_SRV_ENUMERATE_SNAPSHOTS \
	CTL_CODE(FILE_DEVICE_NETWORK_FILE_SYSTEM, 25, METHOD_BUFFERED, \
	FILE_READ_ACCESS)
#endif
	case FSCTL_SRV_ENUMERATE_SNAPSHOTS: /* 0x144064 */
		dprintf("    FSCTL_SRV_ENUMERATE_SNAPSHOTS\n");
		{
		ULONG outlen =
		    IrpSp->Parameters.FileSystemControl.OutputBufferLength;
		ULONG *hdr = (ULONG *)Irp->AssociatedIrp.SystemBuffer;

		mount_t *vss_zmo = DeviceObject->DeviceExtension;
		zfsvfs_t *vss_zfsvfs = vfs_fsprivate(vss_zmo);

		if (vss_zfsvfs == NULL || vss_zfsvfs->z_os == NULL) {
			Status = STATUS_INVALID_DEVICE_REQUEST;
			break;
		}

		/*
		 * First pass: count snapshots.
		 * Each @GMT timestamp: 24 WCHAR chars + 1 NUL = 25 WCHARs.
		 * The multi-sz array ends with an extra NUL (1 WCHAR).
		 */
		ULONG nsnaps = 0;
		dsl_pool_t *vss_dp = dmu_objset_pool(vss_zfsvfs->z_os);

		dsl_pool_config_enter(vss_dp, FTAG);
		uint64_t vss_pos = 0;
		char vss_snapname[MAXNAMELEN];
		uint64_t vss_id;
		boolean_t vss_cc;
		while (dmu_snapshot_list_next(vss_zfsvfs->z_os,
		    sizeof (vss_snapname), vss_snapname, &vss_id,
		    &vss_pos, &vss_cc) == 0)
			nsnaps++;
		dsl_pool_config_exit(vss_dp, FTAG);

		/* SnapshotArraySize: N * 25 WCHARs + 2 terminating NULs */
		ULONG arr_size =
		    nsnaps * 25 * sizeof (WCHAR) + 2 * sizeof (WCHAR);
		ULONG needed = 3 * sizeof (ULONG) + arr_size;

		dprintf("    FSCTL_SRV_ENUMERATE_SNAPSHOTS: %lu snaps, "
		    "arr_size=%lu, outlen=%lu\n", nsnaps, arr_size, outlen);

		if (outlen < 3 * sizeof (ULONG)) {
			/*
			 * Buffer too small for even the 12-byte header.
			 * Return STATUS_BUFFER_OVERFLOW (not BUFFER_TOO_SMALL)
			 * so that srv2.sys can use IoStatus.Information to
			 * determine the needed size and retry.
			 * STATUS_BUFFER_TOO_SMALL is non-retryable and causes
			 * srv2.sys to return STATUS_INSUFFICIENT_RESOURCES to
			 * the SMB client.
			 */
			Irp->IoStatus.Information = needed;
			Status = STATUS_BUFFER_OVERFLOW;
			break;
		}

		hdr[0] = nsnaps;	/* NumberOfSnapshots */
		hdr[1] = 0;		/* NumberOfSnapshotsReturned */
		hdr[2] = arr_size;	/* SnapshotArraySize */

		if (nsnaps == 0 || outlen < needed) {
			Irp->IoStatus.Information = 3 * sizeof (ULONG);
			Status = (nsnaps == 0) ?
			    STATUS_SUCCESS : STATUS_BUFFER_OVERFLOW;
			break;
		}

		/*
		 * Second pass: fill in @GMT-YYYY.MM.DD-HH.MM.SS strings.
		 * Unix UTC to Windows FILETIME (100ns since 1601-01-01):
		 *   nt = unix * 10,000,000 + 116,444,736,000,000,000
		 */
		WCHAR *wp =
		    (WCHAR *)((UCHAR *)Irp->AssociatedIrp.SystemBuffer +
		    3 * sizeof (ULONG));
		ULONG returned = 0;

		dsl_pool_config_enter(vss_dp, FTAG);
		vss_pos = 0;
		while (dmu_snapshot_list_next(vss_zfsvfs->z_os,
		    sizeof (vss_snapname), vss_snapname, &vss_id,
		    &vss_pos, &vss_cc) == 0) {
			uint64_t creation = 0;
			dsl_dataset_t *vss_ds;
			if (dsl_dataset_hold_obj(vss_dp, vss_id,
			    FTAG, &vss_ds) == 0) {
				creation = dsl_get_creation(vss_ds);
				dsl_dataset_rele(vss_ds, FTAG);
			}

			LARGE_INTEGER li;
			TIME_UNIX_TO_WINDOWS_EX(creation, 0, li.QuadPart);

			TIME_FIELDS tf;
			RtlTimeToTimeFields(&li, &tf);

			/* @GMT-YYYY.MM.DD-HH.MM.SS (24 chars + NUL = 25) */
			wp[0]  = L'@'; wp[1]  = L'G'; wp[2]  = L'M';
			wp[3]  = L'T'; wp[4]  = L'-';
			wp[5]  = L'0' + (WCHAR)(tf.Year / 1000);
			wp[6]  = L'0' + (WCHAR)((tf.Year / 100) % 10);
			wp[7]  = L'0' + (WCHAR)((tf.Year / 10) % 10);
			wp[8]  = L'0' + (WCHAR)(tf.Year % 10);
			wp[9]  = L'.';
			wp[10] = L'0' + (WCHAR)(tf.Month / 10);
			wp[11] = L'0' + (WCHAR)(tf.Month % 10);
			wp[12] = L'.';
			wp[13] = L'0' + (WCHAR)(tf.Day / 10);
			wp[14] = L'0' + (WCHAR)(tf.Day % 10);
			wp[15] = L'-';
			wp[16] = L'0' + (WCHAR)(tf.Hour / 10);
			wp[17] = L'0' + (WCHAR)(tf.Hour % 10);
			wp[18] = L'.';
			wp[19] = L'0' + (WCHAR)(tf.Minute / 10);
			wp[20] = L'0' + (WCHAR)(tf.Minute % 10);
			wp[21] = L'.';
			wp[22] = L'0' + (WCHAR)(tf.Second / 10);
			wp[23] = L'0' + (WCHAR)(tf.Second % 10);
			wp[24] = L'\0';

			dprintf("    FSCTL_SRV_ENUMERATE_SNAPSHOTS: snap '%s' "
			    "@GMT-%04d.%02d.%02d-%02d.%02d.%02d\n",
			    vss_snapname, tf.Year, tf.Month, tf.Day,
			    tf.Hour, tf.Minute, tf.Second);

			wp += 25;
			returned++;
		}
		dsl_pool_config_exit(vss_dp, FTAG);

		/* Multi-sz double-NUL terminator */
		*wp++ = L'\0';
		*wp   = L'\0';

		hdr[1] = returned;
		Irp->IoStatus.Information = needed;
		Status = STATUS_SUCCESS;
		}
		break;

	case FSCTL_READ_FILE_USN_DATA:
		dprintf("    FSCTL_READ_FILE_USN_DATA\n");

		Status = STATUS_INVALID_DEVICE_REQUEST;
		break;

	case FSCTL_QUERY_PERSISTENT_VOLUME_STATE:
		dprintf("    FSCTL_QUERY_PERSISTENT_VOLUME_STATE\n");
		PVOID Buffer = Irp->AssociatedIrp.SystemBuffer;
		ULONG InputBufferLength =
		    IrpSp->Parameters.FileSystemControl.InputBufferLength;
		ULONG OutputBufferLength =
		    IrpSp->Parameters.FileSystemControl.OutputBufferLength;
		PFILE_FS_PERSISTENT_VOLUME_INFORMATION Info;

		if (NULL == Buffer)
			return (STATUS_INVALID_PARAMETER);

		if (sizeof (FILE_FS_PERSISTENT_VOLUME_INFORMATION) >
		    InputBufferLength ||
		    sizeof (FILE_FS_PERSISTENT_VOLUME_INFORMATION) >
		    OutputBufferLength)
			return (STATUS_BUFFER_TOO_SMALL);

		Info = Buffer;
#if 0
		if (1 != Info->Version ||
		    !FlagOn(Info->FlagMask,
		    PERSISTENT_VOLUME_STATE_SHORT_NAME_CREATION_DISABLED))
			return (STATUS_INVALID_PARAMETER);
#endif
		RtlZeroMemory(Info,
		    sizeof (FILE_FS_PERSISTENT_VOLUME_INFORMATION));
		Info->VolumeFlags =
		    PERSISTENT_VOLUME_STATE_SHORT_NAME_CREATION_DISABLED;
		Irp->IoStatus.Information =
		    sizeof (FILE_FS_PERSISTENT_VOLUME_INFORMATION);

		Status = STATUS_SUCCESS;
		break;

	case FSCTL_SET_ZERO_DATA:
		dprintf("    FSCTL_SET_ZERO_DATA\n");
		Status = fsctl_set_zero_data(DeviceObject, Irp, IrpSp);
		break;

	case FSCTL_OFFLOAD_READ:
		dprintf("    FSCTL_OFFLOAD_READ\n");
		Status = fsctl_offload_read(DeviceObject, Irp, IrpSp);
		break;

	case FSCTL_OFFLOAD_WRITE:
		dprintf("    FSCTL_OFFLOAD_WRITE\n");
		Status = fsctl_offload_write(DeviceObject, Irp, IrpSp);
		break;

	default:
		dprintf("* %s: unknown class 0x%lx\n", __func__,
		    IrpSp->Parameters.FileSystemControl.FsControlCode);
		break;
	}

	return (Status);
}

NTSTATUS
query_directory_FileFullDirectoryInformation(PDEVICE_OBJECT DeviceObject,
    PIRP Irp, PIO_STACK_LOCATION IrpSp)
{
	// FILE_FULL_DIR_INFORMATION *outptr = Irp->UserBuffer;
	int flag_index_specified =
	    IrpSp->Flags & SL_INDEX_SPECIFIED ? 1 : 0;
	int flag_restart_scan =
	    IrpSp->Flags & SL_RESTART_SCAN ? 1 : 0;
	int flag_return_single_entry =
	    IrpSp->Flags & SL_RETURN_SINGLE_ENTRY ? 1 : 0;
	int ret;
	boolean_t initial = B_FALSE;
	mount_t *zmo;
	zfsvfs_t *zfsvfs;
	NTSTATUS Status = STATUS_NO_SUCH_FILE;
	PMDL mdl = NULL;

	if ((Irp->UserBuffer == NULL && Irp->MdlAddress == NULL) ||
	    IrpSp->Parameters.QueryDirectory.Length <= 0)
		return (STATUS_INSUFFICIENT_RESOURCES);

	if (IrpSp->FileObject == NULL ||
	    IrpSp->FileObject->FsContext == NULL || // vnode
	    IrpSp->FileObject->FsContext2 == NULL)  // ccb
		return (STATUS_INVALID_PARAMETER);

	struct vnode *dvp = IrpSp->FileObject->FsContext;
	zfs_ccb_t *zccb = IrpSp->FileObject->FsContext2;

	if (zccb->magic != ZFS_CCB_MAGIC)
		return (STATUS_INVALID_PARAMETER);

	if (flag_index_specified) {
		zccb->dirlist_index =
		    IrpSp->Parameters.QueryDirectory.FileIndex;
	} else if (flag_restart_scan) {
		zccb->dir_eof = 0;
		zccb->dirlist_index = 0;
		if (zccb->searchname.Buffer != NULL)
			kmem_free(zccb->searchname.Buffer,
			    zccb->searchname.MaximumLength);
		zccb->searchname.Buffer = NULL;
		zccb->searchname.MaximumLength = 0;
	} else {
		/* Just use zccb->dirlist_index from last call */
	}

	// Did last call complete listing?
	if (zccb->dir_eof)
		return (STATUS_NO_MORE_FILES);

	void *SystemBuffer = MapUserBuffer(Irp,
	    IrpSp->Parameters.QueryDirectory.Length, IoWriteAccess, &mdl);

	if (SystemBuffer == NULL) {
		UnMapUserBuffer(mdl);
		return (STATUS_INSUFFICIENT_RESOURCES);
	}

	// Grab the root zp
	zmo = DeviceObject->DeviceExtension;
	ASSERT(zmo->type == MOUNT_TYPE_VCB || zmo->type == MOUNT_TYPE_VSS);

	zfsvfs = vfs_fsprivate(zmo); // or from zp

	if (!zfsvfs) {
		UnMapUserBuffer(mdl);
		return (STATUS_INTERNAL_ERROR);
	}

	if (zccb->searchname.Buffer == NULL)
		initial = B_TRUE;

	dprintf("%s: starting vp %p Search pattern '%wZ' type %d: "
	    "saved search '%wZ'\n", __func__, dvp,
	    IrpSp->Parameters.QueryDirectory.FileName,
	    IrpSp->Parameters.QueryDirectory.FileInformationClass,
	    &zccb->searchname);

	WCHAR Fat8QMdot3QM[12] = { DOS_QM, DOS_QM, DOS_QM, DOS_QM,
	    DOS_QM, DOS_QM, DOS_QM, DOS_QM, L'.', DOS_QM, DOS_QM, DOS_QM };

	/*
	 * FileName is only meaningful on the first call (initial == TRUE)
	 * or when SL_RESTART_SCAN is set.  On subsequent FindNextFile calls
	 * (FileName == NULL / empty / trivial wildcard), the existing
	 * searchname must NOT be cleared — clearing it strips the filter
	 * and causes entries that do not match the original pattern to leak
	 * into the output.
	 *
	 * Example: `dir "New Document (2).txt"` (non-wildcard) correctly
	 * returns one entry on the first call, but without this fix the
	 * second call (FindNextFile, FileName=NULL) wiped the searchname and
	 * returned all remaining directory entries unfiltered.
	 *
	 * We still fall through here so the initial/dir_eof checks below
	 * work normally.
	 */
	if ((IrpSp->Parameters.QueryDirectory.FileName == NULL) ||
	    (IrpSp->Parameters.QueryDirectory.FileName->Length == 0) ||
	    (IrpSp->Parameters.QueryDirectory.FileName->Buffer == NULL) ||
	    ((IrpSp->Parameters.QueryDirectory.FileName->Length ==
	    sizeof (WCHAR)) &&
	    (IrpSp->Parameters.QueryDirectory.FileName->Buffer[0] == L'*')) ||
	    ((IrpSp->Parameters.QueryDirectory.FileName->Length ==
	    12 * sizeof (WCHAR)) &&
	    (RtlEqualMemory(IrpSp->Parameters.QueryDirectory.FileName->Buffer,
	    Fat8QMdot3QM, /* "????????.???" */
	    12 * sizeof (WCHAR))))) {

	/* keep existing searchname — continuation of the current scan */

	} else if (IrpSp->Parameters.QueryDirectory.FileName &&
	    IrpSp->Parameters.QueryDirectory.FileName->Buffer) {

		/*
		 * FileName is provided and non-trivial: save it as the search
		 * pattern.  Per spec, FileName is only honoured on the first
		 * call; subsequent calls with a non-wildcard FileName (and no
		 * SL_RESTART_SCAN) return STATUS_NO_MORE_FILES immediately
		 * because a specific name can only match once.
		 */
		zccb->ContainsWildCards = FsRtlDoesNameContainWildCards(
		    IrpSp->Parameters.QueryDirectory.FileName);

		if (!zccb->ContainsWildCards && !initial) {
			UnMapUserBuffer(mdl);
			return (STATUS_NO_MORE_FILES);
		}

		// If exists, we should free before setting
		if (zccb->searchname.Buffer != NULL)
			kmem_free(zccb->searchname.Buffer,
			    zccb->searchname.MaximumLength);

		zccb->searchname.MaximumLength =
		    IrpSp->Parameters.QueryDirectory.FileName->Length +
		    sizeof (WCHAR);
		zccb->searchname.Length =
		    IrpSp->Parameters.QueryDirectory.FileName->Length;
		zccb->searchname.Buffer =
		    kmem_alloc(zccb->searchname.MaximumLength,
		    KM_SLEEP);

		if (zccb->ContainsWildCards) {
			Status = RtlUpcaseUnicodeString(&zccb->searchname,
			    IrpSp->Parameters.QueryDirectory.FileName, FALSE);
		} else {
			RtlCopyMemory(zccb->searchname.Buffer,
			    IrpSp->Parameters.QueryDirectory.FileName->Buffer,
			    zccb->searchname.Length);
		}
		dprintf("%s: setting up search '%wZ' (wildcards: %d) "
		    "status 0x%lx\n", __func__,
		    &zccb->searchname, zccb->ContainsWildCards, Status);
	} else {
		if (!flag_restart_scan)
			initial = B_FALSE;
	}

	emitdir_ptr_t ctx;
	ctx.bufsize = IrpSp->Parameters.QueryDirectory.Length;
	ctx.alloc_buf = SystemBuffer;
	ctx.bufptr = ctx.alloc_buf;
	ctx.outcount = 0;
	ctx.next_offset = NULL;
	ctx.last_alignment = 0;
	ctx.offset = zccb->dirlist_index;
	ctx.numdirent = 0;
	ctx.dirlisttype = IrpSp->Parameters.QueryDirectory.FileInformationClass;

	VN_HOLD(dvp);
	ret = zfs_readdir(dvp, &ctx, NULL, zccb, IrpSp->Flags);
	VN_RELE(dvp);

	/* finished listing dir? */
	if (ret == ENOENT) {
		zccb->dir_eof = 1;
		ret = 0;
	} else if (ret == ENOSPC) {

		/*
		 * If we have no "outcount" then buffer is too small
		 * for the first record. If we do have "outcount", we
		 * return what we have, and wait to be called again.
		 */
		if (ctx.outcount > 0)
			ret = 0;
		else {
			Status = STATUS_BUFFER_OVERFLOW;
			zccb->dirlist_index = ctx.offset;
		}
	}

	if (ret == 0) {
		if (ctx.outcount > 0) {
			/*
			 * Data was written directly into SystemBuffer —
			 * no intermediate copy needed.
			 */
			Status = STATUS_SUCCESS;
		} else { // outcount == 0
			Status = initial ?
			    STATUS_NO_SUCH_FILE :
			    STATUS_NO_MORE_FILES;
		}
		// Set correct buffer size returned.
		Irp->IoStatus.Information = ctx.outcount;

		dprintf("dirlist information in %ld out size %llu\n",
		    IrpSp->Parameters.QueryDirectory.Length,
		    Irp->IoStatus.Information);

		// Remember directory index for next time
		zccb->dirlist_index = ctx.offset;
	}

	UnMapUserBuffer(mdl);

	return (Status);
}


/*
 * Enumerate volume mount-point reparse entries for MountManager.
 *
 * MountManager queries NtQueryDirectoryFile(FileReparsePointInformation) on
 * the volume root to discover all IO_REPARSE_TAG_MOUNT_POINT junctions.
 * We scan our mount list for child datasets/snapshots whose mounted_on path
 * is a single component (direct child of the root), look up the corresponding
 * ZFS inode, and return FILE_REPARSE_POINT_INFORMATION entries for each one
 * that has ZFS_REPARSE set.
 */

#define	FRPI_MAX_MOUNTS	32

typedef struct {
	const char	*vcb_name;
	char		paths[FRPI_MAX_MOUNTS][MAXNAMELEN];
	int		count;
} frpi_collect_ctx_t;

static int
frpi_collect_cb(void *mp, void *arg)
{
	mount_t *m = mp;
	frpi_collect_ctx_t *ctx = arg;

	if (m->type != MOUNT_TYPE_DCB || m->ascii_name == NULL)
		return (0);
	const char *mo = m->mounted_on;
	if (mo == NULL || mo[0] != '\\')
		return (0);

	/* Only direct children of root: no additional '\' after first */
	if (strchr(mo + 1, '\\') != NULL)
		return (0);

	/* ascii_name must start with vcb_name followed by '/' or '@' */
	size_t vlen = strlen(ctx->vcb_name);
	if (strncmp(m->ascii_name, ctx->vcb_name, vlen) != 0)
		return (0);
	char sep = m->ascii_name[vlen];
	if (sep != '/' && sep != '@')
		return (0);

	if (ctx->count < FRPI_MAX_MOUNTS) {
		strlcpy(ctx->paths[ctx->count], mo + 1, MAXNAMELEN);
		ctx->count++;
	}
	return (0);
}

static NTSTATUS
query_directory_FileReparsePointInformation(PDEVICE_OBJECT DeviceObject,
    PIRP Irp, PIO_STACK_LOCATION IrpSp)
{
	mount_t *zmo = DeviceObject->DeviceExtension;
	if (zmo == NULL ||
	    (zmo->type != MOUNT_TYPE_VCB && zmo->type != MOUNT_TYPE_VSS))
		return (STATUS_NO_MORE_FILES);

	zfsvfs_t *zfsvfs = vfs_fsprivate(zmo);
	if (zfsvfs == NULL)
		return (STATUS_NO_MORE_FILES);

	const char *vcb_name = zmo->ascii_name;
	if (vcb_name == NULL)
		return (STATUS_NO_MORE_FILES);

	if (IrpSp->FileObject == NULL || IrpSp->FileObject->FsContext == NULL)
		return (STATUS_INVALID_PARAMETER);

	struct vnode *dvp = IrpSp->FileObject->FsContext;
	zfs_ccb_t *zccb = IrpSp->FileObject->FsContext2;

	int flag_restart_scan = (IrpSp->Flags & SL_RESTART_SCAN) ? 1 : 0;
	if (zccb != NULL) {
		if (flag_restart_scan)
			zccb->dir_eof = 0;
		if (zccb->dir_eof)
			return (STATUS_NO_MORE_FILES);
	}

	znode_t *dzp = VTOZ(dvp);

	/* Only enumerate from the volume root directory */
	if (dzp == NULL || dzp->z_id != zfsvfs->z_root)
		return (STATUS_NO_MORE_FILES);

	/* Collect mounted_on paths for child DCBs (direct root children) */
	frpi_collect_ctx_t coll;
	coll.vcb_name = vcb_name;
	coll.count = 0;
	vfs_mount_iterate(frpi_collect_cb, &coll);

	if (zccb != NULL)
		zccb->dir_eof = 1;

	if (coll.count == 0)
		return (STATUS_NO_MORE_FILES);

	PMDL mdl = NULL;
	void *SystemBuffer = MapUserBuffer(Irp,
	    IrpSp->Parameters.QueryDirectory.Length, IoWriteAccess, &mdl);
	if (SystemBuffer == NULL) {
		UnMapUserBuffer(mdl);
		return (STATUS_INSUFFICIENT_RESOURCES);
	}

	FILE_REPARSE_POINT_INFORMATION *out = SystemBuffer;
	ULONG bufsize = IrpSp->Parameters.QueryDirectory.Length;
	ULONG used = 0;
	int emitted = 0;

	if (zfs_enter(zfsvfs, FTAG) != 0) {
		UnMapUserBuffer(mdl);
		return (STATUS_NO_MORE_FILES);
	}

	for (int i = 0; i < coll.count; i++) {
		if (used + sizeof (FILE_REPARSE_POINT_INFORMATION) > bufsize)
			break;

		znode_t *root_zp = NULL;
		if (zfs_zget(zfsvfs, zfsvfs->z_root, &root_zp) != 0)
			continue;

		char namebuf[MAXNAMELEN];
		struct componentname cn;
		strlcpy(namebuf, coll.paths[i], sizeof (namebuf));
		cn.cn_nameiop = LOOKUP;
		cn.cn_flags = ISLASTCN;
		cn.cn_namelen = strlen(namebuf);
		cn.cn_nameptr = namebuf;
		cn.cn_pnlen = MAXNAMELEN;
		cn.cn_pnbuf = namebuf;

		cred_t cr = { .cr_uid = 0, .cr_gid = 0 };
		int direntflags = 0;
		znode_t *zp = NULL;
		int err = zfs_lookup(root_zp, namebuf, &zp, 0, &cr,
		    &direntflags, &cn);
		VN_RELE(ZTOV(root_zp));

		if (err != 0 || zp == NULL)
			continue;

		if (zp->z_pflags & ZFS_REPARSE) {
			out->FileReference = (LONGLONG)zp->z_id;
			out->Tag = IO_REPARSE_TAG_MOUNT_POINT;
			out++;
			used += sizeof (FILE_REPARSE_POINT_INFORMATION);
			emitted++;
		}
		VN_RELE(ZTOV(zp));
	}

	zfs_exit(zfsvfs, FTAG);
	UnMapUserBuffer(mdl);

	if (emitted == 0)
		return (STATUS_NO_MORE_FILES);

	Irp->IoStatus.Information = used;
	return (STATUS_SUCCESS);
}

NTSTATUS
query_directory(PDEVICE_OBJECT DeviceObject, PIRP Irp,
    PIO_STACK_LOCATION IrpSp)
{
	NTSTATUS Status = STATUS_NOT_IMPLEMENTED;

	switch (IrpSp->Parameters.QueryDirectory.FileInformationClass) {

		// The type is now passed into zfs_vnop.c/zfs_readdir()
		// so check there for support
	case FileBothDirectoryInformation:
	case FileDirectoryInformation:
	case FileFullDirectoryInformation: // ***
	case FileIdBothDirectoryInformation: // ***
	case FileIdFullDirectoryInformation:
	case FileNamesInformation:
	// case FileObjectIdInformation:  // Do we need this one?
	case FileIdExtdDirectoryInformation:
	case FileIdExtdBothDirectoryInformation:
		Status =
		    query_directory_FileFullDirectoryInformation(DeviceObject,
		    Irp, IrpSp);
		break;
	case FileQuotaInformation:
		dprintf("   %s FileQuotaInformation *NotImplemented\n",
		    __func__);
		break;
	case FileReparsePointInformation:
		Status =
		    query_directory_FileReparsePointInformation(DeviceObject,
		    Irp, IrpSp);
		break;
	default:
		dprintf("   %s unknown 0x%x *NotImplemented\n",
		    __func__,
		    IrpSp->Parameters.QueryDirectory.FileInformationClass);
		break;
	}

	return (Status);
}

NTSTATUS
notify_change_directory(PDEVICE_OBJECT DeviceObject, PIRP Irp,
    PIO_STACK_LOCATION IrpSp)
{
	PFILE_OBJECT fileObject = IrpSp->FileObject;
	mount_t *zmo;

	dprintf("%s\n", __func__);
	zmo = DeviceObject->DeviceExtension;

	if (zmo == NULL ||
	    (zmo->type != MOUNT_TYPE_VCB &&
	    zmo->type != MOUNT_TYPE_VSS)) {
		return (STATUS_INVALID_PARAMETER);
	}

	struct vnode *vp = fileObject->FsContext;
	zfs_ccb_t *zccb = fileObject->FsContext2;
	ASSERT(vp != NULL);

	VN_HOLD(vp);

	if (!vnode_isdir(vp)) {
		VN_RELE(vp);
		return (STATUS_INVALID_PARAMETER);
	}

	if (vnode_unlink(vp)) {
		VN_RELE(vp);
		return (STATUS_DELETE_PENDING);
	}
	ASSERT(zmo->NotifySync != NULL);

	dprintf("%s: '%s' for %wZ\n", __func__,
	    zccb&&zccb->z_name_cache?zccb->z_name_cache:"",
	    &fileObject->FileName);

	FsRtlNotifyFilterChangeDirectory(
	    zmo->NotifySync, &zmo->DirNotifyList, zccb,
	    (PSTRING)&fileObject->FileName,
	    (IrpSp->Flags & SL_WATCH_TREE), FALSE,
	    IrpSp->Parameters.NotifyDirectory.CompletionFilter, Irp,
	    NULL, NULL, NULL);

	VN_RELE(vp);
	return (STATUS_PENDING);
}

NTSTATUS
set_information(PDEVICE_OBJECT DeviceObject, PIRP Irp,
    PIO_STACK_LOCATION IrpSp)
{
	NTSTATUS Status = STATUS_NOT_IMPLEMENTED;

	Irp->IoStatus.Information = 0;

	// OpLocks might need preflight checks.
	BOOLEAN needsPreflight = FALSE;
	switch (IrpSp->Parameters.SetFile.FileInformationClass) {
	case FileEndOfFileInformation:
	case FileAllocationInformation:
	case FileValidDataLengthInformation:
	{
		uint64_t skip = (uint64_t)Irp->Tail.Overlay.DriverContext[0];
		const BOOLEAN skipSetInfo =
		    (skip == (OPLOCK_SKIP_MAGIC | OPLOCK_SKIP_SETINFO));
		if (!skipSetInfo)
			needsPreflight = TRUE;
		break;
	}
	default:
		break;
	}

	if (needsPreflight) {
		PFILE_OBJECT FileObject = IrpSp->FileObject;
		struct vnode *vp = FileObject->FsContext;

		ExAcquireResourceExclusiveLite(vp->FileHeader.Resource, TRUE);

		ZFS_OPLOCK_CREATE_CTX *ctx = ExAllocatePoolZero(NonPagedPoolNx,
		    sizeof (*ctx), 'plkO');
		if (!ctx) {
			ExReleaseResourceLite(vp->FileHeader.Resource);
			return (STATUS_INSUFFICIENT_RESOURCES);
		}
		ctx->DeviceObject = DeviceObject;
		ctx->Irp = Irp;
		ctx->SkipMask = OPLOCK_SKIP_SETINFO;

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

	switch (IrpSp->Parameters.SetFile.FileInformationClass) {
	case FileAllocationInformation:
		Status = set_file_endoffile_information(DeviceObject, Irp,
		    IrpSp, B_FALSE, B_TRUE);
		break;
	case FileBasicInformation: // chmod
		dprintf("* SET FileBasicInformation\n");
		Status = set_file_basic_information(DeviceObject, Irp, IrpSp);
		break;
	case FileDispositionInformation: // unlink
		dprintf("* SET FileDispositionInformation\n");
		Status = set_file_disposition_information(DeviceObject, Irp,
		    IrpSp, B_FALSE);
		break;
	case FileDispositionInformationEx: // unlink
		dprintf("* SET FileDispositionInformationEx\n");
		Status = set_file_disposition_information(DeviceObject, Irp,
		    IrpSp, B_TRUE);
		break;
	case FileEndOfFileInformation: // extend?
		Status = set_file_endoffile_information(DeviceObject, Irp,
		    IrpSp, IrpSp->Parameters.SetFile.AdvanceOnly, B_FALSE);
		break;
	case FileLinkInformation: // hardlink
	case FileLinkInformationEx:
		Status = set_file_link_information(DeviceObject, Irp, IrpSp);
		break;
	case FilePositionInformation: // seek
		Status = set_file_position_information(DeviceObject, Irp,
		    IrpSp);
		break;
	case FileRenameInformation: // vnop_rename
	case FileRenameInformationEx:
		Status = set_file_rename_information(DeviceObject, Irp, IrpSp);
		break;
	case FileValidDataLengthInformation:
		Status = set_file_valid_data_length_information(DeviceObject,
		    Irp, IrpSp);
		break;
	case FileCaseSensitiveInformation:
		Status = set_file_case_sensitive_information(DeviceObject,
		    Irp, IrpSp);
		break;
	default:
		dprintf("* %s: unknown type NOTIMPLEMENTED\n", __func__);
		break;
	}

	return (Status);
}

boolean_t
is_top_level(_In_ PIRP Irp)
{
	if (!IoGetTopLevelIrp()) {
		IoSetTopLevelIrp(Irp);
		return (TRUE);
	}

	return (FALSE);
}

NTSTATUS
zfs_read_wrap(vnode_t *vp, uint8_t *data, uint64_t start,
    uint64_t length, uint64_t *pbr, PIRP Irp)
{
	znode_t *zp = VTOZ(vp);

	VERIFY3P(zp, !=, NULL);

	if (pbr)
		*pbr = 0;

	if (start >= zp->z_size) {
		dprintf("Tried to read beyond end of file\n");
		return (STATUS_END_OF_FILE);
	}

	// pool_type = vp->Header.Flags2 & FSRTL_FLAG2_IS_PAGING_FILE ?
	// NonPagedPool : PagedPool;
	struct iovec iov;
	iov.iov_base = (void *)data;
	iov.iov_len = length;

	zfs_uio_t uio;
	zfs_uio_iovec_init(&uio, &iov, 1, start, UIO_SYSSPACE,
	    length, 0);

	if (Irp->MdlAddress == NULL &&
	    Irp->UserBuffer != NULL) {
		if (!LockUserBuffer(Irp, IoWriteAccess, length)) {
			dprintf("Locking UserBuffer failed.");
			return (STATUS_INVALID_USER_BUFFER);
		}
	}

	int error = zfs_read(zp, &uio, 0, NULL);

	// Update bytes read
	if (pbr)
		*pbr = length - zfs_uio_resid(&uio);

	/*
	 * zfs_read() returns a POSIX errno, not an NTSTATUS -- callers here
	 * test NT_SUCCESS(), which is true for any value >= 0, so returning
	 * the errno directly would turn EIO(5)/ENXIO(6)/etc into "success".
	 */
	if (error != 0)
		return (zfs_error_to_ntstatus(error));

	return (STATUS_SUCCESS);
}

NTSTATUS
fs_read_impl(PIRP Irp, boolean_t wait, uint64_t *bytes_read)
{
	PIO_STACK_LOCATION IrpSp = IoGetCurrentIrpStackLocation(Irp);
	PFILE_OBJECT FileObject = IrpSp->FileObject;
	vnode_t *vp = FileObject->FsContext;
	uint8_t *data = NULL;
	ULONG length = IrpSp->Parameters.Read.Length, addon = 0;
	uint64_t start = IrpSp->Parameters.Read.ByteOffset.QuadPart;
	NTSTATUS Status = STATUS_SUCCESS;
	boolean_t nocache = (Irp->Flags & IRP_NOCACHE);

	if (zfs_disable_wincache)
		nocache = TRUE;

	*bytes_read = 0;

	if (!vp || !VTOZ(vp))
		return (STATUS_INTERNAL_ERROR);

	if (!vnode_isreg(vp))
		return (STATUS_INVALID_DEVICE_REQUEST);

	if (!(Irp->Flags & IRP_PAGING_IO) &&
	    !FsRtlCheckLockForReadAccess(&vp->lock, Irp)) {
		dprintf("tried to read locked region\n");
		return (STATUS_FILE_LOCK_CONFLICT);
	}

	if (length == 0) {
		dprintf("tried to read zero bytes\n");
		return (STATUS_SUCCESS);
	}

	if (start >= (uint64_t)vp->FileHeader.FileSize.QuadPart) {
		dprintf("read with offset > end (%I64x >= %I64x)\n",
		    start, vp->FileHeader.FileSize.QuadPart);
		return (STATUS_END_OF_FILE);
	}

	if (!nocache && (IrpSp->MinorFunction & IRP_MN_MDL)) {
		NTSTATUS Status = STATUS_SUCCESS;

		try {
			if (!FileObject->PrivateCacheMap) {
				CC_FILE_SIZES ccfs;

				ccfs.AllocationSize =
				    vp->FileHeader.AllocationSize;
				ccfs.FileSize =
				    vp->FileHeader.FileSize;
				ccfs.ValidDataLength =
				    vp->FileHeader.ValidDataLength;

				zfs_init_cache(FileObject, vp, &ccfs);
			}

			CcMdlRead(FileObject,
			    &IrpSp->Parameters.Read.ByteOffset,
			    length, &Irp->MdlAddress, &Irp->IoStatus);
		} except(EXCEPTION_EXECUTE_HANDLER) {
			Status = GetExceptionCode();
		}

		if (NT_SUCCESS(Status)) {
			Status = Irp->IoStatus.Status;
			Irp->IoStatus.Information += addon;
			*bytes_read = (uint64_t)Irp->IoStatus.Information;
		} else
			dprintf("EXCEPTION - %08lx\n", Status);

		return (Status);
	}

	data = MapUserBuffer(Irp, 0, 0, NULL);
	// vp->FileHeader.Flags2 & FSRTL_FLAG2_IS_PAGING_FILE ?
	// HighPagePriority : NormalPagePriority);

	if (Irp->MdlAddress && !data) {
		dprintf("MmGetSystemAddressForMdlSafe returned NULL\n");
		return (STATUS_INSUFFICIENT_RESOURCES);
	}

	if (start >= (uint64_t)vp->FileHeader.ValidDataLength.QuadPart) {
		length = (ULONG)min(length,
		    min(start + length,
		    (uint64_t)vp->FileHeader.FileSize.QuadPart) -
		    vp->FileHeader.ValidDataLength.QuadPart);
		RtlZeroMemory(data, length);
		Irp->IoStatus.Information = *bytes_read = length;
		return (STATUS_SUCCESS);
	}

	if (length + start >
	    (uint64_t)vp->FileHeader.ValidDataLength.QuadPart) {
		addon = (ULONG)(min(start + length,
		    (uint64_t)vp->FileHeader.FileSize.QuadPart) -
		    vp->FileHeader.ValidDataLength.QuadPart);
		RtlZeroMemory(data +
		    (vp->FileHeader.ValidDataLength.QuadPart - start),
		    addon);
		length = (ULONG)
		    (vp->FileHeader.ValidDataLength.QuadPart - start);
	}

	if (!nocache) {

		try {
			if (!FileObject->PrivateCacheMap) {
				CC_FILE_SIZES ccfs;

				ccfs.AllocationSize =
				    vp->FileHeader.AllocationSize;
				ccfs.FileSize =
				    vp->FileHeader.FileSize;
				ccfs.ValidDataLength =
				    vp->FileHeader.ValidDataLength;

				zfs_init_cache(FileObject, vp, &ccfs);
			}

#if (NTDDI_VERSION >= NTDDI_WIN8)
			if (!CcCopyReadEx(FileObject,
			    &IrpSp->Parameters.Read.ByteOffset,
			    length, wait, data, &Irp->IoStatus,
			    Irp->Tail.Overlay.Thread)) {
				dprintf("CcCopyReadEx could not wait\n");

				IoMarkIrpPending(Irp);
				return (STATUS_PENDING);
			}
#else
			if (!CcCopyRead(FileObject,
			    &IrpSp->Parameters.Read.ByteOffset,
			    length, wait, data, &Irp->IoStatus)) {
				dprintf("CcCopyRead could not wait\n");

				IoMarkIrpPending(Irp);
				return (STATUS_PENDING);
			}
#endif
		} except(EXCEPTION_EXECUTE_HANDLER) {
			Status = GetExceptionCode();
		}

		if (NT_SUCCESS(Status)) {
			Status = Irp->IoStatus.Status;
			Irp->IoStatus.Information += addon;
			*bytes_read = (uint64_t)Irp->IoStatus.Information;
		} else
			dprintf("EXCEPTION - %08lx\n", Status);

		return (Status);

	} else { /* NOCACHE */

		if (!wait) {
			IoMarkIrpPending(Irp);
			return (STATUS_PENDING);
		}

		Status = zfs_read_wrap(vp, data, start, length, bytes_read,
		    Irp);

		if (!NT_SUCCESS(Status))
			dprintf("read_file returned %08lx\n", Status);
	}

	*bytes_read += addon;
	Irp->IoStatus.Information = *bytes_read;

	return (Status);
}

NTSTATUS
fs_read(PDEVICE_OBJECT DeviceObject, PIRP Irp, PIO_STACK_LOCATION IrpSp)
{
	NTSTATUS Status;
	boolean_t top_level;
	PFILE_OBJECT FileObject = IrpSp->FileObject;
	boolean_t acquired_vp_lock = FALSE, wait;
	uint64_t bytes_read;

	top_level = is_top_level(Irp);

	mount_t *zmo = DeviceObject->DeviceExtension;

	/*
	 * Raw sector reads on the volume device (VCB, \Device\ZFS{guid})
	 * come from the VSS coordinator reading sector 0 before provider
	 * selection, or from SWPRV reading the boot sector for geometry.
	 * Return zeros so the coordinator doesn't abort with an I/O error.
	 * This covers both FileObject==NULL and the case where VSS opens the
	 * device with FILE_NON_DIRECTORY_FILE leaving FsContext non-NULL but
	 * pointing to the root vnode (which is a directory, not a file).
	 */
	if (zmo && zmo->type == MOUNT_TYPE_VCB &&
	    (FileObject == NULL || FileObject->FsContext == NULL ||
	    (FileObject->FsContext != NULL &&
	    vnode_isdir((struct vnode *)FileObject->FsContext)))) {
		Status = volume_read(DeviceObject, Irp, IrpSp);
		goto exit;
	}

	Irp->IoStatus.Information = 0;

	if (IrpSp->MinorFunction & IRP_MN_COMPLETE) {
		CcMdlReadComplete(IrpSp->FileObject, Irp->MdlAddress);

		Irp->MdlAddress = NULL;
		Status = STATUS_SUCCESS;

		goto exit;
	}


	struct vnode *vp = FileObject->FsContext;
	zfs_ccb_t *zccb = FileObject->FsContext2;

	if (!vp || !zccb || !zmo) {
		Status = STATUS_INVALID_PARAMETER;
		goto exit;
	}

	zfsvfs_t *zfsvfs = vfs_fsprivate(zmo);

	/* No return without zfs_exit() */
	if ((Status = zfs_enter(zfsvfs, FTAG)) != 0) {
		Status = STATUS_INVALID_PARAMETER;
		goto exit;
	}

	VERIFY3U(VN_HOLD(vp), ==, 0);

#if 0
	if (Irp->RequestorMode == UserMode && !(ccb->access & FILE_READ_DATA)) {
		dprintf("insufficient privileges\n");
		Status = STATUS_ACCESS_DENIED;
		goto unlock;
	}
#endif

#if 0
	if (vp == Vcb->volume_vp) {
		dprintf("reading volume vp\n");

		IoSkipCurrentIrpStackLocation(Irp);

		Status = IoCallDriver(Vcb->Vpb->RealDevice, Irp);

		goto unlock;
	}
#endif

	/*
	 * For async (overlapped) NOCACHE reads, fs_read_impl returns
	 * STATUS_PENDING immediately (see the !wait check in the NOCACHE
	 * path), which lets the calling thread post all Q>1 IRPs before any
	 * block, achieving true concurrent I/O via CriticalWorkQueue.
	 * For synchronous or paging reads, wait=TRUE is correct.
	 */
	wait = IoIsOperationSynchronous(Irp);

	if (!(Irp->Flags & IRP_PAGING_IO))
		FsRtlCheckOplock(vp_oplock(vp), Irp, NULL, NULL, NULL);
	// Don't offload jobs when doing paging IO - otherwise this can lead to
	// deadlocks in CcCopyRead.

	if ((Irp->Flags & IRP_NOCACHE) &&
	    !(Irp->Flags & IRP_PAGING_IO) &&
	    FileObject->SectionObjectPointer &&
	    FileObject->SectionObjectPointer->DataSectionObject) {
		IO_STATUS_BLOCK iosb;

		CcFlushCache(FileObject->SectionObjectPointer,
		    &IrpSp->Parameters.Read.ByteOffset,
		    IrpSp->Parameters.Read.Length,
		    &iosb);
		if (!NT_SUCCESS(iosb.Status)) {
			dprintf("CcFlushCache returned %08lx\n", iosb.Status);
			Status = iosb.Status;
			goto end;
		}
	}

	/*
	 * PagingIoResource, not FileHeader.Resource: this read can be
	 * entered recursively, on the same thread, from inside a write's
	 * CcCopyWrite when a cache miss forces a synchronous page fault-in
	 * (IoPageReadEx -> fs_read).  If this acquired FileHeader.Resource
	 * instead, that recursive acquisition would contend against any
	 * unrelated thread doing a structural op (e.g.
	 * set_file_endoffile_information) that holds FileHeader.Resource
	 * exclusive while itself waiting on PagingIoResource -- an AB-BA
	 * deadlock across threads.  PagingIoResource matches what the
	 * write paths (zfs_write_wrap, fastio_write) and NTFS's own
	 * NtfsCopyReadA use for the same reason.
	 */
	if (!ExIsResourceAcquiredSharedLite(vp->FileHeader.PagingIoResource)) {
		if (!ExAcquireResourceSharedLite(
		    vp->FileHeader.PagingIoResource, wait)) {
			Status = STATUS_PENDING;
			IoMarkIrpPending(Irp);
			goto end;
		}
		acquired_vp_lock = TRUE;
	}

	Status = fs_read_impl(Irp, wait, &bytes_read);

	if (acquired_vp_lock)
		ExReleaseResourceLite(vp->FileHeader.PagingIoResource);

update:
	if (FileObject->Flags & FO_SYNCHRONOUS_IO &&
	    !(Irp->Flags & IRP_PAGING_IO)) {


		dprintf("updating file offset from %llx to %llx\n",
		    FileObject->CurrentByteOffset.QuadPart,
		    IrpSp->Parameters.Read.ByteOffset.QuadPart +
		    (NT_SUCCESS(Status) ? bytes_read : 0));
		FileObject->CurrentByteOffset.QuadPart =
		    IrpSp->Parameters.Read.ByteOffset.QuadPart +
		    (NT_SUCCESS(Status) ? bytes_read : 0);
	}
end:
	switch (Status) {
	case 0:
	case STATUS_PENDING:
	default:
		break;
	case EISDIR:
		Status = STATUS_FILE_IS_A_DIRECTORY;
		break;
	}

	Irp->IoStatus.Status = Status;

	VN_RELE(vp);

	zfs_exit(zfsvfs, FTAG);

exit:
	if (top_level)
		IoSetTopLevelIrp(NULL);

	return (Status);
}

/*
 * Persistent exception record for the outer zfs_write / zfs_read SEH guard.
 * Survives cbuf.txt overwrites; inspect in WinDbg:
 *   dt OpenZFS!zfs_write_last_except
 *   dq OpenZFS!zfs_write_last_except+0x10 L8   (stack frames)
 */
volatile struct {
	ULONG   code;   /* ExceptionCode */
	ULONG   count;  /* how many times fired */
	PVOID   addr;   /* ExceptionAddress */
	ULONG64 rip;    /* ContextRecord->Rip */
	ULONG64 rsp;    /* ContextRecord->Rsp */
	ULONG64 stack[8]; /* raw return addrs from Rsp */
} zfs_write_last_except;

LONG
zfs_write_fault_filter(PEXCEPTION_POINTERS ep)
{
	ULONG code = ep->ExceptionRecord->ExceptionCode;

	/*
	 * Never catch kernel panics or assertion failures — let them reach
	 * KeBugCheckEx so a crash dump is produced for diagnosis.
	 * Catching these was masking real ZFS invariant violations.
	 */
	if (code == STATUS_BREAKPOINT || code == STATUS_ASSERTION_FAILURE)
		return (EXCEPTION_CONTINUE_SEARCH);

	/* Only handle user-buffer access faults. */
	if (code != STATUS_ACCESS_VIOLATION && code != STATUS_IN_PAGE_ERROR)
		return (EXCEPTION_CONTINUE_SEARCH);

	PULONG64 sp = (PULONG64)(ULONG_PTR)ep->ContextRecord->Rsp;

	zfs_write_last_except.code  = code;
	zfs_write_last_except.addr  = ep->ExceptionRecord->ExceptionAddress;
	zfs_write_last_except.rip   = ep->ContextRecord->Rip;
	zfs_write_last_except.rsp   = ep->ContextRecord->Rsp;
	zfs_write_last_except.count++;
	for (int i = 0; i < 8; i++)
		zfs_write_last_except.stack[i] = sp[i];

	dprintf("ZFS: zfs_write/read exception #%u code=0x%08x addr=%p "
	    "stack: %p %p %p %p %p %p %p %p\n",
	    zfs_write_last_except.count,
	    zfs_write_last_except.code,
	    zfs_write_last_except.addr,
	    (PVOID)zfs_write_last_except.stack[0],
	    (PVOID)zfs_write_last_except.stack[1],
	    (PVOID)zfs_write_last_except.stack[2],
	    (PVOID)zfs_write_last_except.stack[3],
	    (PVOID)zfs_write_last_except.stack[4],
	    (PVOID)zfs_write_last_except.stack[5],
	    (PVOID)zfs_write_last_except.stack[6],
	    (PVOID)zfs_write_last_except.stack[7]);

	return (EXCEPTION_EXECUTE_HANDLER);
}

NTSTATUS
zfs_write_wrap(PDEVICE_OBJECT DeviceObject, PIRP Irp,
    LARGE_INTEGER offset, void *buf, ULONG *length,
    boolean_t paging_io, boolean_t no_cache,
    boolean_t wait, boolean_t deferred_write, boolean_t write_irp)
{
	PIO_STACK_LOCATION IrpSp = IoGetCurrentIrpStackLocation(Irp);
	PFILE_OBJECT FileObject = IrpSp->FileObject;
	uint64_t off64, newlength;
	boolean_t changed_length = FALSE;
	NTSTATUS Status;
	vnode_t *vp;
	zfs_ccb_t *ccb;
	boolean_t paging_lock = FALSE, acquired_vp_lock = FALSE, pagefile;
	ULONG filter = 0;

	if (*length == 0) {
		dprintf("returning success for zero-length write\n");
		return (STATUS_SUCCESS);
	}

	if (!FileObject) {
		dprintf("error - FileObject was NULL\n");
		return (STATUS_ACCESS_DENIED);
	}

	vp = FileObject->FsContext;
	ccb = FileObject->FsContext2;


	znode_t *zp = VTOZ(vp);

	// fileref = ccb ? ccb->fileref : NULL;

	if (!vnode_isreg(vp) && !vnode_islnk(vp)) {
		dprintf("tried to write !file nor !symlink (vp %p)\n",
		    vp);
		return (STATUS_INVALID_DEVICE_REQUEST);
	}

	boolean_t eof_write = (offset.LowPart == FILE_WRITE_TO_END_OF_FILE &&
	    offset.HighPart == -1);

	if (eof_write) {
		/*
		 * Reserve a sequential write slot before CcCanIWrite so that
		 * concurrent EOF writes from multiple connections get unique,
		 * non-overlapping offsets even before any call CcSetFileSizes.
		 * Stamp ByteOffset now so a STATUS_PENDING retry uses the same
		 * slot rather than re-resolving -1 against a stale FileSize.
		 */
		uint64_t old_pending, new_pending, slot;
		do {
			old_pending = vp->z_eof_pending;
			uint64_t cur_sz =
			    (uint64_t)vp->FileHeader.FileSize.QuadPart;
			slot = (old_pending > cur_sz) ? old_pending : cur_sz;
			new_pending = slot + *length;
		} while (atomic_cas_64(&vp->z_eof_pending, old_pending,
		    new_pending) != old_pending);
		offset.QuadPart = (LONGLONG)slot;
		IrpSp->Parameters.Write.ByteOffset = offset;
		dprintf("zfs_write_wrap: eof slot=%llx new_pending=%llx\n",
		    slot, new_pending);
	}

	dprintf("zfs_write_wrap: raw_off=%llx len=%x "
	    "file_sz=%llx paging=%d nocache=%d\n",
	    offset.QuadPart, *length,
	    vp->FileHeader.FileSize.QuadPart, paging_io, no_cache);

	if (!no_cache && !CcCanIWrite(FileObject, *length, wait,
	    deferred_write)) {
		/*
		 * Cache throttling.  ByteOffset was already stamped with the
		 * reserved slot, so the retry lands at the correct offset.
		 */
		IoMarkIrpPending(Irp);
		return (STATUS_PENDING);
	}
	if (!wait && no_cache) {
		IoMarkIrpPending(Irp);
		return (STATUS_PENDING);
	}

	if (no_cache && !paging_io &&
	    FileObject->SectionObjectPointer->DataSectionObject) {
		IO_STATUS_BLOCK iosb;

		ExAcquireResourceExclusiveLite(
		    vp->FileHeader.PagingIoResource, TRUE);

		CcFlushCache(FileObject->SectionObjectPointer, &offset,
		    *length, &iosb);

		if (!NT_SUCCESS(iosb.Status)) {
			ExReleaseResourceLite(
			    vp->FileHeader.PagingIoResource);
			dprintf("CcFlushCache returned %08lx\n",
			    iosb.Status);
			return (iosb.Status);
		}

		CcPurgeCacheSection(FileObject->SectionObjectPointer,
		    &offset, *length, FALSE);

		/*
		 * Release PagingIoResource immediately after the flush/purge.
		 * The cache section is now clean; there is no coherency reason
		 * to hold PagingIoResource across the ZFS write (which includes
		 * a potentially multi-second TXG sync wait).  Holding it that
		 * long serialises all concurrent no-cache writers on the same
		 * file, limiting write parallelism to one writer per TXG cycle.
		 * paging_lock is intentionally left FALSE so end: does not
		 * attempt a second release.
		 */
		ExReleaseResourceLite(vp->FileHeader.PagingIoResource);
	}

	pagefile = vp->FileHeader.Flags2 & FSRTL_FLAG2_IS_PAGING_FILE &&
	    paging_io;

#if 0 // figure out what treelock is
	if (!pagefile &&
	    !ExIsResourceAcquiredExclusiveLite(&Vcb->tree_lock)) {
		if (!ExAcquireResourceSharedLite(&Vcb->tree_lock, wait)) {
			Status = STATUS_PENDING;
			goto end;
		} else
			acquired_tree_lock = true;
	}
#endif

	/* 1) pagefile: MainResource EXCLUSIVE, PagingIoResource EXCLUSIVE */
	/* 2) pagingio: PagingIoResource EXCLUSIVE only if extending, else */
	/*    SHARED (range locks handle ordering) */
	/* 3) normal: PagingIoResource SHARED (range locks handle ordering) */

	if (paging_io) {

		wait = TRUE;

		if (pagefile) {
			if (!ExAcquireResourceExclusiveLite(
			    vp->FileHeader.Resource,
			    wait)) {
				Status = STATUS_PENDING;
				IoMarkIrpPending(Irp);
				goto end;
			} else {
				acquired_vp_lock = TRUE;
			}
		}

		/*
		 * Do NOT take this unconditionally exclusive: fastio_write
		 * holds PagingIoResource shared across FsRtlCopyWrite, which
		 * internally calls CcCanIWrite() and can block waiting for
		 * the Lazy Writer (CcWriteBehind) to flush dirty pages --
		 * i.e. waiting for this exact paging-write path to run.
		 * Requiring exclusive here deadlocks against that: confirmed
		 * live via VeraCrypt Format.exe's fastio_write thread
		 * holding PagingIoResource shared inside CcCanIWrite while
		 * CcWriteBehind's worker thread blocked here waiting for
		 * exclusive access. Only a paging write that extends the
		 * file (FileSize growing under it, same condition the
		 * non-paging branch below checks) needs exclusive.
		 */
		boolean_t need_excl = pagefile ||
		    (offset.QuadPart + (LONGLONG)*length >
		    vp->FileHeader.FileSize.QuadPart);

		if (need_excl) {
			if (!ExAcquireResourceExclusiveLite(
			    vp->FileHeader.PagingIoResource, wait)) {
				Status = STATUS_PENDING;
				IoMarkIrpPending(Irp);
				goto end;
			}
		} else {
			if (!ExAcquireResourceSharedLite(
			    vp->FileHeader.PagingIoResource, wait)) {
				Status = STATUS_PENDING;
				IoMarkIrpPending(Irp);
				goto end;
			}
		}
		paging_lock = TRUE;
	} else {
		/*
		 * Non-paging write: acquire PagingIoResource, EXCLUSIVE if
		 * the write extends the file (must be atomic with
		 * CcSetFileSizes/CcCopyWrite and exclude a concurrent
		 * truncate/extend via set_file_endoffile_information),
		 * SHARED otherwise.
		 *
		 * Deliberately NOT FileHeader.Resource: CcCopyWrite's
		 * cache-miss fault-in can recurse into our own IRP_MJ_READ
		 * dispatch on this same thread (IoPageReadEx -> fs_read),
		 * which would try to reacquire FileHeader.Resource and
		 * inflate this write's critical section across an entire
		 * nested read (dbuf/disk I/O) instead of just the copy --
		 * the same self-recursion hazard fixed in fastio_write.
		 */
		if (!ExIsResourceAcquiredExclusiveLite(
		    vp->FileHeader.PagingIoResource)) {
			boolean_t need_excl =
			    (offset.QuadPart + (LONGLONG)*length >
			    vp->FileHeader.FileSize.QuadPart);
			if (need_excl) {
				ExAcquireResourceExclusiveLite(
				    vp->FileHeader.PagingIoResource, TRUE);
			} else {
				if (!ExAcquireResourceSharedLite(
				    vp->FileHeader.PagingIoResource, wait)) {
					Status = STATUS_PENDING;
					IoMarkIrpPending(Irp);
					goto end;
				}
			}
			paging_lock = TRUE;
		}
	}

	off64 = offset.QuadPart;

	newlength = zp->z_size;

	if (zp->z_unlinked)
		newlength = 0;

	/*
	 * For paging I/O from CcFlushCache, FsRtlCopyWrite/CcFastCopyWrite
	 * may have extended the file in cache without updating zp->z_size.
	 * Use ValidDataLength as the authoritative upper bound so dirty cache
	 * pages written beyond the stale zp->z_size are not silently dropped.
	 *
	 * The same staleness applies to concurrent cached EOF writes: a
	 * later-slot write that acquired the exclusive resource first may
	 * have advanced FileHeader.FileSize before we got the lock.  If we
	 * computed newlength purely from the stale zp->z_size we would set
	 * FileSize smaller than it already is, causing CcSetFileSizes to
	 * truncate the concurrent write's cache pages and lose that data.
	 */
	if (paging_io && !zp->z_unlinked) {
		uint64_t vdl =
		    (uint64_t)vp->FileHeader.ValidDataLength.QuadPart;
		if (vdl > newlength)
			newlength = vdl;
	} else if (!paging_io && !zp->z_unlinked) {
		uint64_t cur_sz = (uint64_t)vp->FileHeader.FileSize.QuadPart;
		if (cur_sz > newlength) {
			dprintf("zfs_write_wrap: [newlength-guard] "
			    "zp_sz=%llx FileSize=%llx slot=%llx "
			    "— would have shrunk, clamping\n",
			    (unsigned long long)zp->z_size,
			    (unsigned long long)cur_sz,
			    (unsigned long long)off64);
			newlength = cur_sz;
		}
	}

	if (paging_io)
		dprintf("paging_io write: off %I64x len %lu zp_sz %I64x "
		    "newlen %I64x\n", off64, *length,
		    (uint64_t)zp->z_size, newlength);

	if (off64 + *length > newlength) {
		if (paging_io) {
			if (off64 >= newlength) {
				dprintf(
"paging tried beyond EOF (size = %I64x, off = %I64x, len = %lx)\n",
				    newlength, off64, *length);
				dprintf(
"AllocationSize = %I64x, Size = %I64x, ValidDataLength = %I64x\n",
				    vp->FileHeader.AllocationSize.QuadPart,
				    vp->FileHeader.FileSize.QuadPart,
				    vp->FileHeader.ValidDataLength.QuadPart);
				Irp->IoStatus.Information = 0;

				Status = STATUS_SUCCESS;
				goto end;
			}

			*length = (ULONG)(newlength - off64);
		} else {
			newlength = off64 + *length;
			changed_length = TRUE;

			dprintf("extending length to %I64x\n", newlength);
		}
	}

	if (changed_length) {
		/*
		 * Update z_size in memory so zfs_write sees the new end.
		 * We deliberately skip zfs_freesp() here: that function
		 * acquires [0, UINT64_MAX] range lock inside zfs_extend()
		 * and then calls dmu_tx_hold_sa(), which can block on a
		 * disk read for the SA spill block.  Under heavy I/O load
		 * (large TXG sync) that read can stall for tens of seconds,
		 * holding the range lock and blocking CcWorkerThread paging
		 * writes to this file.  zfs_write() already handles the
		 * extension atomically (including block-size growth via the
		 * lr->lr_length == UINT64_MAX over-lock path), so the
		 * zfs_freesp(B_FALSE) pre-extend was redundant and unsafe.
		 */
		zp->z_size = newlength;

		vp->FileHeader.AllocationSize.QuadPart = newlength;
		vp->FileHeader.FileSize.QuadPart = newlength;
		/*
		 * Advance VDL to the new end now, before CcCopyWrite.
		 * CcCopyWrite will immediately populate the cache pages,
		 * satisfying any paging read the CM might issue for the
		 * newly-valid region.  The dnode block-size is guaranteed
		 * initialised by this point (z_blksz==0 guard in zfs_write).
		 * Advancing here collapses the two CcSetFileSizes calls into
		 * one, halving the latency for extending writes.
		 */
		vp->FileHeader.ValidDataLength.QuadPart = newlength;

		dprintf("AllocationSize = %I64x\n",
		    vp->FileHeader.AllocationSize.QuadPart);
		dprintf("FileSize = %I64x\n",
		    vp->FileHeader.FileSize.QuadPart);
		dprintf("ValidDataLength = %I64x\n",
		    vp->FileHeader.ValidDataLength.QuadPart);
	}

	/*
	 * Paging I/O (IRP_PAGING_IO) is how CcFlushCache pushes dirty pages
	 * from the Cache Manager to ZFS.  It does NOT have IRP_NOCACHE set,
	 * but it must bypass the cache and write to ZFS directly.  Routing
	 * it back through CcCopyWriteEx would re-dirty the same pages and
	 * prevent the data from ever reaching ZFS.
	 */
	if (!no_cache && !paging_io) {
		Status = STATUS_SUCCESS;

		try {
			if (!FileObject->PrivateCacheMap || changed_length) {
				CC_FILE_SIZES ccfs;

				ccfs.AllocationSize =
				    vp->FileHeader.AllocationSize;
				ccfs.FileSize =
				    vp->FileHeader.FileSize;
				ccfs.ValidDataLength =
				    vp->FileHeader.ValidDataLength;

				if (!FileObject->PrivateCacheMap)
					zfs_init_cache(FileObject, vp, &ccfs);

				CcSetFileSizes(FileObject, &ccfs);
			}

			if (IrpSp->MinorFunction & IRP_MN_MDL) {
				if (changed_length)
					vp->FileHeader.ValidDataLength
					    .QuadPart = newlength;
				CcPrepareMdlWrite(FileObject, &offset, *length,
				    &Irp->MdlAddress, &Irp->IoStatus);

				Status = Irp->IoStatus.Status;
				goto end;
			} else {
/*
 * We have to wait in CcCopyWrite - if we return STATUS_PENDING
 * and add this to the work queue, it can result in CcFlushCache
 * being called before the job has run. See ifstest ReadWriteTest.
 */

#if (NTDDI_VERSION >= NTDDI_WIN8)
				if (!CcCopyWriteEx(FileObject, &offset, *length,
				    TRUE, buf, Irp->Tail.Overlay.Thread)) {
					Status = STATUS_PENDING;
					IoMarkIrpPending(Irp);
					goto end;
				}
#else
				if (!CcCopyWrite(FileObject, &offset, *length,
				    TRUE, buf)) {
					Status = STATUS_PENDING;
					IoMarkIrpPending(Irp);
					goto end;
				}
#endif
				Irp->IoStatus.Information = *length;
			}
		} except(EXCEPTION_EXECUTE_HANDLER) {
			Status = GetExceptionCode();
		}

		/*
		 * For pre-allocated files (changed_length=FALSE, FileSize
		 * already at the pre-allocated size) VDL may still lag
		 * behind the write end.  Advance it now if needed.
		 * Extending writes already set VDL=newlength above, so for
		 * them this block is a no-op.
		 */
		if (NT_SUCCESS(Status) &&
		    newlength >
		    (uint64_t)vp->FileHeader.ValidDataLength.QuadPart) {
			CC_FILE_SIZES ccfs;
			vp->FileHeader.ValidDataLength.QuadPart = newlength;
			ccfs.AllocationSize = vp->FileHeader.AllocationSize;
			ccfs.FileSize = vp->FileHeader.FileSize;
			ccfs.ValidDataLength = vp->FileHeader.ValidDataLength;
			try {
				CcSetFileSizes(FileObject, &ccfs);
			} except(EXCEPTION_EXECUTE_HANDLER) {
			}
		}

		if (changed_length) {
			if (zp->z_pflags & ZFS_XATTR) {
				zfs_send_notify_stream(zp->z_zfsvfs,
				    ccb->z_name_cache,
				    ccb->z_name_offset,
				    FILE_NOTIFY_CHANGE_STREAM_SIZE,
				    FILE_ACTION_MODIFIED_STREAM,
				    NULL);
			} else {
				zfs_send_notify(zp->z_zfsvfs, ccb->z_name_cache,
				    ccb->z_name_offset, FILE_NOTIFY_CHANGE_SIZE,
				    FILE_ACTION_MODIFIED);
			}
		}

		goto end;
	}

	struct iovec iov;
	iov.iov_base = (void *)buf;
	iov.iov_len = *length;

	zfs_uio_t uio;
	zfs_uio_iovec_init(&uio, &iov, 1, off64, UIO_SYSSPACE,
	    *length, 0);

	boolean_t locked = FALSE;

	if (write_irp && Irp->MdlAddress) {
		/*
		 * MDL_SOURCE_IS_NONPAGED_POOL (eg. an MDL built by
		 * MmBuildMdlForNonPagedPool(), as kernel-mode callers such
		 * as vhdmp.sys use for their own block buffers) describes
		 * memory that is never pageable -- it is unconditionally
		 * resident, so there is nothing to probe or lock.  Calling
		 * MmProbeAndLockPages() on it is a Driver Verifier bugcheck
		 * (0xC4/0xB1), confirmed live.
		 */
		locked = Irp->MdlAddress->MdlFlags &
		    (MDL_PAGES_LOCKED | MDL_PARTIAL |
		    MDL_SOURCE_IS_NONPAGED_POOL);

		if (!locked) {
			Status = STATUS_SUCCESS;

			try {
				MmProbeAndLockPages(Irp->MdlAddress, KernelMode,
				    IoReadAccess);
			} except(EXCEPTION_EXECUTE_HANDLER) {
				Status = GetExceptionCode();
			}

			if (!NT_SUCCESS(Status)) {
				dprintf("MmProbeAndLockPages except %08lx\n",
				    Status);
				goto end;
			}
		}
	}

	if (Irp->MdlAddress == NULL &&
	    Irp->UserBuffer != NULL) {
		if (!LockUserBuffer(Irp, IoReadAccess, *length)) {
			dprintf("Locking UserBuffer failed.");
			goto end;
		}
	}

	if (paging_io) {
		uio.uio_extflg |= SKIP_CHANGE_TIME;
		uio.uio_extflg |= SKIP_WRITE_TIME;
		uio.uio_extflg |= SKIP_SIZE_UPDATE;
	} else {
		if (ccb->user_set_change_time)
			uio.uio_extflg |= SKIP_CHANGE_TIME;
		if (ccb->user_set_write_time)
			uio.uio_extflg |= SKIP_WRITE_TIME;
	}

	int error = zfs_write(zp, &uio, 0, NULL);

	if (error != 0) {
		/*
		 * zfs_write() returns a POSIX errno, not an NTSTATUS -- small
		 * positive values like ENOSPC(28) or EIO(5) pass NT_SUCCESS()
		 * (defined as >= 0), so testing Status directly here would
		 * silently treat every write failure as success. Convert
		 * before it reaches Irp->IoStatus.Status.
		 */
		Status = zfs_error_to_ntstatus(error);
		dprintf("zfs_write returned %08lx\n", Status);
		goto end;
	}

	/*
	 * Advance ValidDataLength to cover the bytes just committed to ZFS.
	 * Not gated on changed_length: paging writes (flushed from cache)
	 * always have changed_length==FALSE, and writes into a pre-allocated
	 * file also have changed_length==FALSE because FileSize was already
	 * set.  Any write that advances past the current VDL must update it
	 * so that subsequent reads are served from ZFS rather than zeroed.
	 */
	if (!pagefile && FileObject &&
	    newlength > (uint64_t)vp->FileHeader.ValidDataLength.QuadPart) {
		vp->FileHeader.ValidDataLength.QuadPart = newlength;
		if (FileObject->PrivateCacheMap) {
			CC_FILE_SIZES ccfs;
			ccfs.AllocationSize = vp->FileHeader.AllocationSize;
			ccfs.FileSize = vp->FileHeader.FileSize;
			ccfs.ValidDataLength = vp->FileHeader.ValidDataLength;
			try {
				CcSetFileSizes(FileObject, &ccfs);
			} except(EXCEPTION_EXECUTE_HANDLER) {
			}
		}
	}

	// gethrestime(&now);

	if (!pagefile) {

		if (!ccb->user_set_write_time)
			filter |= FILE_NOTIFY_CHANGE_LAST_WRITE;

		if (!(zp->z_pflags & ZFS_XATTR)) {
			if (changed_length) {
				dprintf("setting st_size to %I64x\n",
				    newlength);
				zp->z_size = newlength;
				filter |= FILE_NOTIFY_CHANGE_SIZE;
			}

		} else {

			if (changed_length)
				filter |= FILE_NOTIFY_CHANGE_STREAM_SIZE;

			filter |= FILE_NOTIFY_CHANGE_STREAM_WRITE;
		}
	}

	if (changed_length) {
		CC_FILE_SIZES ccfs;

		ccfs.AllocationSize = vp->FileHeader.AllocationSize;
		ccfs.FileSize = vp->FileHeader.FileSize;
		ccfs.ValidDataLength = vp->FileHeader.ValidDataLength;

		try {
			CcSetFileSizes(FileObject, &ccfs);
		} except(EXCEPTION_EXECUTE_HANDLER) {
			Status = GetExceptionCode();
			goto end;
		}
	}

	Status = STATUS_SUCCESS;
	Irp->IoStatus.Information = *length - (ULONG)zfs_uio_resid(&uio);

	if (filter != 0) {
		zfsvfs_t *zfsvfs = zp->z_zfsvfs;

		if (zp->z_pflags & ZFS_XATTR) {
			zfs_send_notify_stream(zfsvfs,
			    ccb->z_name_cache,
			    ccb->z_name_offset,
			    filter,
			    FILE_ACTION_MODIFIED_STREAM,
			    NULL);
		} else {
			zfs_send_notify(zfsvfs, ccb->z_name_cache,
			    ccb->z_name_offset, filter,
			    FILE_ACTION_MODIFIED);
		}
	}

end:
	if (NT_SUCCESS(Status) && FileObject->Flags & FO_SYNCHRONOUS_IO &&
	    !paging_io) {
		FileObject->CurrentByteOffset.QuadPart =
		    offset.QuadPart + Irp->IoStatus.Information;
		dprintf("zfs_write_wrap: CurrentByteOffset->%llx "
		    "(wrote %lx of %lx)\n",
		    (unsigned long long)
		    FileObject->CurrentByteOffset.QuadPart,
		    Irp->IoStatus.Information, *length);
	}

	if (paging_lock)
		ExReleaseResourceLite(vp->FileHeader.PagingIoResource);

	if (acquired_vp_lock)
		ExReleaseResourceLite(vp->FileHeader.Resource);

	return (Status);
}

NTSTATUS
fs_write_impl(PDEVICE_OBJECT DeviceObject, PIRP Irp, PIO_STACK_LOCATION IrpSp,
    boolean_t wait, boolean_t deferred_write)
{
	void *buf;
	void *kbuf = NULL;
	NTSTATUS Status;
	LARGE_INTEGER offset = IrpSp->Parameters.Write.ByteOffset;
	PFILE_OBJECT FileObject = IrpSp->FileObject;
	vnode_t *vp = FileObject ? FileObject->FsContext : NULL;
	boolean_t nocache = (Irp->Flags & IRP_NOCACHE);

	if (zfs_disable_wincache)
		nocache = TRUE;

	Irp->IoStatus.Information = 0;

	if (IrpSp->Parameters.Write.Length == 0) {
		Status = STATUS_SUCCESS;
		goto exit;
	}

	if (!Irp->AssociatedIrp.SystemBuffer) {
		buf = MapUserBuffer(Irp, 0, 0, NULL);
		// , vp && vp->FileHeader.Flags2 & FSRTL_FLAG2_IS_PAGING_FILE
		// ? HighPagePriority : NormalPagePriority);

		if (Irp->MdlAddress && !buf) {
			dprintf("MmGetSystemAddressForMdlSafe returned NULL\n");
			Status = STATUS_INSUFFICIENT_RESOURCES;
			goto exit;
		}
		/*
		 * buf points to MDL-mapped user pages (or raw user VA).
		 * Those pages are volatile: another CPU can overwrite them
		 * while we hold locks or block inside CcCopyWrite.  Snapshot
		 * into kernel memory before any blocking operation.
		 * Skip paging I/O — CM buffers are stable for the write.
		 *
		 * Pin the copy in AssociatedIrp.SystemBuffer so it survives
		 * a STATUS_PENDING deferral: the retry (do_write_job) will
		 * reuse this snapshot instead of re-mapping the user buffer.
		 * do_write_job frees it before IoCompleteRequest.
		 */
		if (buf && !(Irp->Flags & IRP_PAGING_IO)) {
			kbuf = ExAllocatePoolWithTag(NonPagedPoolNx,
			    IrpSp->Parameters.Write.Length, 'ZBFC');
			if (!kbuf) {
				Status = STATUS_INSUFFICIENT_RESOURCES;
				goto exit;
			}
			RtlCopyMemory(kbuf, buf,
			    IrpSp->Parameters.Write.Length);
			buf = kbuf;
			Irp->AssociatedIrp.SystemBuffer = kbuf;
		}
	} else {
		/*
		 * Either a true DO_BUFFERED_IO kernel copy (rare for an FSD),
		 * or our saved snapshot from the first (deferred) call above.
		 * Either way the buffer is stable.
		 */
		buf = Irp->AssociatedIrp.SystemBuffer;
	}

	if (vp && !(Irp->Flags & IRP_PAGING_IO) &&
	    !FsRtlCheckLockForWriteAccess(&vp->lock, Irp)) {
		dprintf("tried to write to locked region\n");
		Status = STATUS_FILE_LOCK_CONFLICT;
		goto exit;
	}

	Status = zfs_write_wrap(DeviceObject, Irp, offset, buf,
	    &IrpSp->Parameters.Write.Length,
	    (Irp->Flags & IRP_PAGING_IO), nocache,
	    wait, deferred_write, TRUE);

	if (Status == STATUS_PENDING) {
		goto exit;
	} else if (!NT_SUCCESS(Status)) {
		dprintf("write_file2 returned %08lx\n", Status);
		goto exit;
	}

	if (NT_SUCCESS(Status)) {
#if 1 // Updating benchmark stats?
		if (/*diskacc && */ Status != STATUS_PENDING &&
		    nocache) {
			PETHREAD thread = NULL;

			if (Irp->Tail.Overlay.Thread &&
			    !IoIsSystemThread(Irp->Tail.Overlay.Thread))
				thread = Irp->Tail.Overlay.Thread;
			else if (!IoIsSystemThread(PsGetCurrentThread()))
				thread = PsGetCurrentThread();
			else if (IoIsSystemThread(PsGetCurrentThread()) &&
			    IoGetTopLevelIrp() == Irp)
				thread = PsGetCurrentThread();

			if (thread)
				PsUpdateDiskCounters(
				    PsGetThreadProcess(thread),
				    0, IrpSp->Parameters.Write.Length, 0, 1, 0);
		}
#endif
	}

exit:
	if (kbuf) {
		if (Status == STATUS_PENDING) {
			/*
			 * IRP deferred to do_write_job worker thread.
			 * kbuf stays in AssociatedIrp.SystemBuffer so
			 * the retry sees stable data; do_write_job frees
			 * it after the deferred write completes.
			 */
		} else {
			/* Write done. Drop our kernel snapshot. */
			ExFreePoolWithTag(kbuf, 'ZBFC');
			Irp->AssociatedIrp.SystemBuffer = NULL;
		}
	}

	return (Status);
}

NTSTATUS
fs_write(PDEVICE_OBJECT DeviceObject, PIRP Irp, PIO_STACK_LOCATION IrpSp)
{
	PFILE_OBJECT	fileObject;
	NTSTATUS Status = STATUS_SUCCESS;
	int error;
	int pagingio = FlagOn(Irp->Flags, IRP_PAGING_IO);

#if 0
	dprintf("   %s minor type %d flags 0x%x mdl %d System %d "
	    "User %d paging %d\n", __func__, IrpSp->MinorFunction,
	    DeviceObject->Flags, (Irp->MdlAddress != 0),
	    (Irp->AssociatedIrp.SystemBuffer != 0),
	    (Irp->UserBuffer != 0),
	    FlagOn(Irp->Flags, IRP_PAGING_IO));
#endif

	fileObject = IrpSp->FileObject;

	if (fileObject == NULL) {
		dprintf("fileObject == NULL\n");
		return (SET_ERROR(STATUS_INVALID_PARAMETER));
	}

	if (fileObject->FsContext == NULL) {
		dprintf("FsContext == NULL\n");
		return (SET_ERROR(STATUS_INVALID_PARAMETER));
	}

	struct vnode *vp = fileObject->FsContext;
	zfs_ccb_t *zccb = fileObject->FsContext2;

	if (zccb == NULL) {
		dprintf("zccb == NULL\n");
		return (SET_ERROR(STATUS_INVALID_PARAMETER));
	}

	if (VTOZ(vp) == NULL) {
		dprintf("zp == NULL\n");
		return (SET_ERROR(STATUS_INVALID_PARAMETER));
	}

	mount_t *zmo = DeviceObject->DeviceExtension;
	zfsvfs_t *zfsvfs = vfs_fsprivate(zmo);
	/*
	 * For async (overlapped) NOCACHE writes, zfs_write_wrap returns
	 * STATUS_PENDING immediately (see the !wait && no_cache check),
	 * enabling concurrent Q>1 writes via CriticalWorkQueue.
	 * For synchronous writes or paging IO, wait=TRUE is correct.
	 */
	boolean_t wait = fileObject ? IoIsOperationSynchronous(Irp) : TRUE;

	/* No returns from here */
	if ((error = zfs_enter(zfsvfs, FTAG)) != 0)
		return (SET_ERROR(error));

	VERIFY3U(VN_HOLD(vp), ==, 0);

	Irp->IoStatus.Information = 0;

	try {
		if (FlagOn(IrpSp->MinorFunction, IRP_MN_COMPLETE)) {
			dprintf("%s: IRP_MN_COMPLETE\n", __func__);
			/*
			 * CcPrepareMdlWrite (zfs_write_wrap, the earlier,
			 * separate IRP that started this MDL-mode write) runs
			 * under PagingIoResource.  This completion runs as
			 * its own later IRP, after that hold has long since
			 * been released, with no resource protection at all.
			 * If CcMdlWriteComplete touches FileHeader.Resource
			 * internally the same way CcCopyWrite does (confirmed
			 * live earlier for the fastio_write/CcCopyWrite case),
			 * anything that changes FileSize/AllocationSize in the
			 * gap between prepare and complete -- another write,
			 * a truncate, zfs_couplefileobject -- races it
			 * unprotected.  Close that gap the same way: hold
			 * PagingIoResource, not FileHeader.Resource.
			 */
			ExAcquireResourceExclusiveLite(
			    vp->FileHeader.PagingIoResource, TRUE);
			CcMdlWriteComplete(IrpSp->FileObject,
			    &IrpSp->Parameters.Write.ByteOffset,
			    Irp->MdlAddress);
			ExReleaseResourceLite(vp->FileHeader.PagingIoResource);
			// Mdl is now deallocated.
			Irp->MdlAddress = NULL;
			Status = STATUS_SUCCESS;

		} else {

			if (!FlagOn(Irp->Flags, IRP_PAGING_IO))
				FsRtlCheckOplock(vp_oplock(vp), Irp, NULL,
				    NULL, NULL);

			if (FlagOn(Irp->Flags, IRP_PAGING_IO))
				wait = TRUE;

			Status = fs_write_impl(DeviceObject, Irp, IrpSp,
			    wait, FALSE);
		}

	} except(EXCEPTION_EXECUTE_HANDLER) {
		Status = GetExceptionCode();
	}

	/*
	 * `error` here is zfs_enter()'s return, already checked non-zero
	 * above (which returns early) -- it is always 0 by this point, so
	 * a switch on it can never convert anything.  The real write
	 * result is `Status`, already a proper NTSTATUS: fs_write_impl()
	 * returns zfs_write_wrap()'s converted status directly.
	 */

	VN_RELE(vp);

	zfs_exit(zfsvfs, FTAG);

	// dprintf("Name: %wZ offset 0x%llx len 0x%lx mdl %p System %p\n",
	// &fileObject->FileName, byteOffset.QuadPart, bufferLength,
	// Irp->MdlAddress, Irp->AssociatedIrp.SystemBuffer);

	Irp->IoStatus.Status = Status;

	return (SET_ERROR(Status));
}

NTSTATUS
do_read_job(PDEVICE_OBJECT DeviceObject, PIRP Irp)
{
	boolean_t top_level = is_top_level(Irp);
	NTSTATUS Status;
	uint64_t bytes_read;

	try {
		Status = fs_read_impl(Irp, TRUE, &bytes_read);
	} except(EXCEPTION_EXECUTE_HANDLER) {
		Status = GetExceptionCode();
	}

	if (!NT_SUCCESS(Status))
		dprintf("read_file returned %08lx\n", Status);

	/*
	 * do_read_job is the deferred (wait=TRUE) retry: it is expected to
	 * produce a final result and complete the IRP below.  STATUS_PENDING
	 * is not a valid IoStatus.Status for IoCompleteRequest -- it has a
	 * separate meaning to the I/O manager (see IoMarkIrpPending) and a
	 * caller blocked on this IRP would see undefined behavior instead of
	 * ever being woken.  Treat an unexpected PENDING here as a failure.
	 */
	if (Status == STATUS_PENDING) {
		dprintf("read_file: fs_read_impl returned PENDING on the "
		    "wait=TRUE retry, completing as STATUS_UNSUCCESSFUL\n");
		Status = STATUS_UNSUCCESSFUL;
	}

	Irp->IoStatus.Status = Status;

	IoCompleteRequest(Irp, IO_NO_INCREMENT);

	if (top_level)
		IoSetTopLevelIrp(NULL);

	return (Status);
}

NTSTATUS
do_write_job(PDEVICE_OBJECT DeviceObject, PIRP Irp)
{
	boolean_t top_level = is_top_level(Irp);
	NTSTATUS Status;

	PIO_STACK_LOCATION IrpSp;
	IrpSp = IoGetCurrentIrpStackLocation(Irp);

	try {
		Status = fs_write_impl(DeviceObject, Irp, IrpSp, TRUE, TRUE);
	} except(EXCEPTION_EXECUTE_HANDLER) {
		Status = GetExceptionCode();
	}

	if (!NT_SUCCESS(Status))
		dprintf("write_file returned %08lx\n", Status);

	/*
	 * do_write_job is the deferred (wait=TRUE, deferred_write=TRUE)
	 * retry: it is expected to produce a final result and complete the
	 * IRP below.  STATUS_PENDING is not a valid IoStatus.Status for
	 * IoCompleteRequest -- it has a separate meaning to the I/O manager
	 * (see IoMarkIrpPending) and a caller blocked on this IRP would see
	 * undefined behavior instead of ever being woken.  Treat an
	 * unexpected PENDING here as a failure.
	 */
	if (Status == STATUS_PENDING) {
		dprintf("write_file: fs_write_impl returned PENDING on the "
		    "wait=TRUE retry, completing as STATUS_UNSUCCESSFUL\n");
		Status = STATUS_UNSUCCESSFUL;
	}

	Irp->IoStatus.Status = Status;

	/*
	 * Free the kbuf snapshot saved in AssociatedIrp.SystemBuffer
	 * during the initial dispatch.  Must happen before
	 * IoCompleteRequest, which may free the IRP itself.
	 */
	if (Irp->AssociatedIrp.SystemBuffer) {
		ExFreePoolWithTag(Irp->AssociatedIrp.SystemBuffer, 'ZBFC');
		Irp->AssociatedIrp.SystemBuffer = NULL;
	}

	IoCompleteRequest(Irp, IO_NO_INCREMENT);

	if (top_level)
		IoSetTopLevelIrp(NULL);

	return (Status);
}

struct do_job_s {
	PIRP Irp;
	ULONG len;
	uint8_t work_item[0];
};

void
do_job(PDEVICE_OBJECT DeviceObject, struct do_job_s *job)
{
	NTSTATUS Status;
	PIO_STACK_LOCATION IrpSp;
	PIRP Irp = job->Irp;
	IrpSp = IoGetCurrentIrpStackLocation(Irp);

	switch (IrpSp->MajorFunction) {
	case IRP_MJ_READ:
		Status = do_read_job(DeviceObject, Irp);
		break;
	case IRP_MJ_WRITE:
		Status = do_write_job(DeviceObject, Irp);
		break;
	default:
		panic("Unknown do_job(IRP_MJ_ %d (0x%x)",
		    IrpSp->MajorFunction, IrpSp->MajorFunction);
	}
	(void) Status;
	IoUninitializeWorkItem((PIO_WORKITEM)&job->work_item);
	ExFreePoolWithTag(job, 'ZJOB');
}

boolean_t
add_thread_job(PDEVICE_OBJECT DeviceObject, PIRP Irp, int len,
    LOCK_OPERATION IoStyle)
{
	struct do_job_s *job;

	job = ExAllocatePoolWithTag(NonPagedPoolNx,
	    sizeof (struct do_job_s) + IoSizeofWorkItem(),
	    'ZJOB');

	if (job == NULL)
		return (FALSE);

	if (Irp->MdlAddress == NULL &&
	    Irp->UserBuffer != NULL) {

		if (!LockUserBuffer(Irp, IoStyle, len)) {
			ExFreePoolWithTag(job, 'ZJOB');
			return (FALSE);
		}
	}

	IoInitializeWorkItem(DeviceObject, (PIO_WORKITEM)&job->work_item);
	job->Irp = Irp;
	job->len = len;

	IoQueueWorkItem((PIO_WORKITEM)&job->work_item,
	    (PIO_WORKITEM_ROUTINE)do_job,
	    CriticalWorkQueue, job);

	return (TRUE);
}

/*
 * The lifetime of a delete.
 * 1) If a file open is marked DELETE_ON_CLOSE in zfs_vnop_lookup() we will
 * call vnode_setdeleteonclose(vp) to signal the intent. This is so
 * file_standard_information can return DeletePending correctly
 * (as well as a few more)
 * 2) Upon IRP_MJ_CLEANUP (closing a file handle) we are expected to remove
 * the file (as tested by IFStest.exe) we will call vnode_setdeleted(vp),
 * this will:
 * 3) Make zfs_vnop_lookup() return ENOENT when "setdeleted" is set.
 * Making it appear as if the file was deleted - but retaining vp and zp
 * as required by Windows.
 * 4) Eventually IRP_MJ_CLOSE is called, and if final, we can release
 * vp and zp, and if "setdeleted" was active, we can finally call
 * delete_entry() to remove the file.
 */
NTSTATUS
delete_entry(PDEVICE_OBJECT DeviceObject, PIRP Irp, PIO_STACK_LOCATION IrpSp,
    vnode_t *parent_vp)
{
	// In Unix, both zfs_unlink and zfs_rmdir expect a filename,
	// and we do not have that here
	struct vnode *vp = NULL, *dvp = NULL;
	int error;
	char filename[MAXPATHLEN];
	ULONG outlen;
	znode_t *zp = NULL;
	mount_t *zmo = DeviceObject->DeviceExtension;

	if (IrpSp->FileObject->FsContext == NULL ||
	    IrpSp->FileObject->FileName.Buffer == NULL ||
	    IrpSp->FileObject->FileName.Length == 0) {
		dprintf("%s: called with missing arguments, can't delete\n",
		    __func__);
		return (STATUS_INSTANCE_NOT_AVAILABLE); // FIXME
	}

	vp = IrpSp->FileObject->FsContext;
	zfsvfs_t *zfsvfs = vfs_fsprivate(zmo);
	if (zfsvfs->z_unmounted || (vfs_flags(zmo) & MNT_UNMOUNTING))
		return (STATUS_VOLUME_DISMOUNTED);
	zp = VTOZ(vp);
	if (zp == NULL)
		return (STATUS_VOLUME_DISMOUNTED);

	if (zp->z_is_ctldir)
		return (STATUS_SUCCESS);

	// find parent and hold.
	if (parent_vp != NULL) {
		VERIFY0(VN_HOLD(parent_vp));
		dvp = parent_vp;
	} else {
		dvp = zfs_parent(vp);
	}

	if (dvp == NULL)
		return (STATUS_INSTANCE_NOT_AVAILABLE);

	// Unfortunately, filename is littered with "\", clean it up,
	// or search based on ID to get name?
#if 0
	dprintf("%s: deleting '%.*S'\n", __func__,
	    (int)(IrpSp->FileObject->FileName.Length / sizeof (WCHAR)),
	    IrpSp->FileObject->FileName.Buffer);
#endif
	error = RtlUnicodeToUTF8N(filename, MAXPATHLEN - 1, &outlen,
	    IrpSp->FileObject->FileName.Buffer,
	    IrpSp->FileObject->FileName.Length);

	if (error != STATUS_SUCCESS &&
	    error != STATUS_SOME_NOT_MAPPED) {
		VN_RELE(dvp);
		VN_RELE(vp);
		dprintf("%s: some illegal characters\n", __func__);
		return (STATUS_INVALID_PARAMETER); // test.exe
		return (STATUS_ILLEGAL_CHARACTER);
	}
	while (outlen > 0 && filename[outlen - 1] == '\\') outlen--;
	filename[outlen] = 0;

	dprintf("%s: deleting '%s'\n", __func__, filename);

	// FIXME, use z_name_cache and offset
	char *finalname = NULL;
	if ((finalname = strrchr(filename, '\\')) != NULL)
		finalname = &finalname[1];
	else
		finalname = filename;

	// Check if it has :stream
	char *stream_name = NULL;
	error = stream_parse(filename, &stream_name);
	if (error == 0 && stream_name != NULL)
		finalname = stream_name;

	dprintf("final delete name as '%s'\n", finalname);

	// Release final HOLD on item, ready for deletion
	int isdir = vnode_isdir(vp);

	/* ZFS deletes from filename, so RELE last hold on vp. */
	// vnode_flushcache(vp, IrpSp->FileObject, B_TRUE);

	vp->FileHeader.AllocationSize.QuadPart = 0;
	vp->FileHeader.FileSize.QuadPart = 0;
	vp->FileHeader.ValidDataLength.QuadPart = 0;

	if (IrpSp->FileObject) {
		CC_FILE_SIZES ccfs;
		NTSTATUS Status = STATUS_SUCCESS;

		ccfs.AllocationSize = vp->FileHeader.AllocationSize;
		ccfs.FileSize = vp->FileHeader.FileSize;
		ccfs.ValidDataLength = vp->FileHeader.ValidDataLength;

		try {
			CcSetFileSizes(IrpSp->FileObject, &ccfs);
		} except(EXCEPTION_EXECUTE_HANDLER) {
			Status = GetExceptionCode();
		}

		if (!NT_SUCCESS(Status)) {
			dprintf("CcSetFileSizes threw exception %08lx\n",
			    Status);
		}
	}

	VN_RELE(vp);
	vp = NULL;

	/*
	 * Use a kernel credential (uid=0) for the ZFS-level delete.  Windows
	 * already verified that the opener had DELETE access on this handle
	 * (via SeAccessCheck at IRP_MJ_CREATE time and again when
	 * FileDispositionInformation(Ex) was processed), so the ZFS POSIX ACL
	 * re-check here is redundant.  Without a kernel cred, callers running
	 * as non-root uid (e.g. git at uid 1000) hit secpolicy_vnode_access2()
	 * which only permits uid==0, causing delete_entry to return EACCES and
	 * leaving the file in the directory with z_unlinked set — the root
	 * cause of "config.lock: File exists" errors in git clone on ZFS.
	 */
	cred_t kcr = { .cr_uid = 0, .cr_gid = 0 };

	if (isdir) {

		error = zfs_rmdir(VTOZ(dvp), finalname, NULL, &kcr, 0);

	} else {

		error = zfs_remove(VTOZ(dvp), finalname, &kcr, 0);

	}

	if (error == ENOTEMPTY)
		error = STATUS_DIRECTORY_NOT_EMPTY;

	// Release parent.
	VN_RELE(dvp);

	dprintf("%s: returning %d\n", __func__, error);
	return (error);
}

NTSTATUS
flush_buffers(PDEVICE_OBJECT DeviceObject, PIRP Irp, PIO_STACK_LOCATION IrpSp)
{
	PFILE_OBJECT FileObject = IrpSp->FileObject;
	NTSTATUS Status = 0;
	IO_STATUS_BLOCK iosb = {0};

	dprintf("%s: \n", __func__);

	if (FileObject == NULL || FileObject->FsContext == NULL)
		return (STATUS_INVALID_PARAMETER);

	/*
	 * Push Cache Manager dirty pages into ZFS before calling fsync.
	 * Without this, lazy-written data may not yet be in ZFS when
	 * the caller (e.g. git's FlushFileBuffers) reads back the file.
	 */
	if (CcIsFileCached(FileObject))
		CcFlushCache(FileObject->SectionObjectPointer, NULL, 0, &iosb);

	struct vnode *vp = FileObject->FsContext;
	if (VN_HOLD(vp) == 0) {
		znode_t *zp = VTOZ(vp);
		zfsvfs_t *zfsvfs = zp->z_zfsvfs;
		Status = zfs_vnop_ioctl_fullfsync(vp, NULL, zfsvfs);
		VN_RELE(vp);
	}
	return (Status);
}

NTSTATUS
query_security(PDEVICE_OBJECT DeviceObject, PIRP Irp, PIO_STACK_LOCATION IrpSp)
{
	PFILE_OBJECT FileObject = IrpSp->FileObject;
	NTSTATUS Status;

	dprintf("%s: \n", __func__);

	if (FileObject == NULL || FileObject->FsContext == NULL)
		return (STATUS_INVALID_PARAMETER);

	PMDL mdl = NULL;
	void *buf = MapUserBuffer(Irp,
	    IrpSp->Parameters.QuerySecurity.Length,
	    IoWriteAccess, &mdl);

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
		Status = STATUS_BUFFER_OVERFLOW; // Needed, checked.
		Irp->IoStatus.Information = buflen;
	} else if (NT_SUCCESS(Status)) {
		Irp->IoStatus.Information = buflen;
		dump_sd(sd);
	} else {
		dprintf("%s: failed 0x%lx\n", __func__, Status);
		Irp->IoStatus.Information = 0;
	}

	UnMapUserBuffer(mdl);
	return (Status);
}

// Set Security should save the blob and do nothing else.
NTSTATUS
set_security(PDEVICE_OBJECT DeviceObject, PIRP Irp, PIO_STACK_LOCATION IrpSp)
{
	PFILE_OBJECT FileObject = IrpSp->FileObject;
	NTSTATUS Status = STATUS_SUCCESS;

	dprintf("%s: SecurityInformation 0x%x\n", __func__,
	    (uint32_t)IrpSp->Parameters.SetSecurity.SecurityInformation);

	if (FileObject == NULL || FileObject->FsContext == NULL)
		return (STATUS_INVALID_PARAMETER);

	struct vnode *vp = FileObject->FsContext;
	zfs_ccb_t *zccb = FileObject->FsContext2;
	VN_HOLD(vp);
	PSECURITY_DESCRIPTOR oldsd;
	oldsd = vnode_security(vp);


	// READONLY check here
	znode_t *zp = VTOZ(vp);
	zfsvfs_t *zfsvfs = zp->z_zfsvfs;
	if (vfs_isrdonly(zfsvfs->z_vfs) || zfsctl_is_node(zp)) {
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

	if (IrpSp->Parameters.SetSecurity.SecurityInformation &
	    OWNER_SECURITY_INFORMATION) {
		PSID owner;
		Status = RtlGetOwnerSecurityDescriptor(vnode_security(vp),
		    &owner, &defaulted);
		if (Status == STATUS_SUCCESS) {
			vattr.va_uid = zfs_sid2uid(owner);
			vattr.va_mask |= ATTR_UID;
			dprintf("%s: OWNER -> uid=%u (was z_uid=%u)\n",
			    __func__, vattr.va_uid, zp->z_uid);
		}
	}
	if (IrpSp->Parameters.SetSecurity.SecurityInformation &
	    GROUP_SECURITY_INFORMATION) {
		PSID group;
		Status = RtlGetGroupSecurityDescriptor(vnode_security(vp),
		    &group, &defaulted);
		if (Status == STATUS_SUCCESS) {
			/* uid/gid reverse is identical */
			vattr.va_gid = zfs_sid2uid(group);
			vattr.va_mask |= ATTR_GID;
			dprintf("%s: GROUP -> gid=%u (was z_gid=%u)\n",
			    __func__, vattr.va_gid, zp->z_gid);
		}
	}

	/* Do we need to update ZFS? */
	if (vattr.va_mask != 0) {
		zfs_setattr(zp, &vattr, 0, NULL);
		Status = STATUS_SUCCESS;
	}

	Irp->IoStatus.Information = 0;
	zfs_send_notify(zfsvfs, zccb->z_name_cache, zccb->z_name_offset,
	    FILE_NOTIFY_CHANGE_SECURITY,
	    FILE_ACTION_MODIFIED);

	zfs_save_ntsecurity(vp);
	dump_sd(vp->security_descriptor);
err:
	VN_RELE(vp);
	return (Status);
}

// #define	IOCTL_VOLUME_BASE ((DWORD) 'V')
// #define	IOCTL_VOLUME_GET_GPT_ATTRIBUTES
//   CTL_CODE(IOCTL_VOLUME_BASE,14,METHOD_BUFFERED,FILE_ANY_ACCESS)

#define	IOCTL_VOLUME_POST_ONLINE \
	CTL_CODE(IOCTL_VOLUME_BASE, 25, METHOD_BUFFERED, \
    FILE_READ_ACCESS | FILE_WRITE_ACCESS)

NTSTATUS
ioctl_storage_get_device_number(PDEVICE_OBJECT DeviceObject, PIRP Irp,
    PIO_STACK_LOCATION IrpSp)
{

	if (IrpSp->Parameters.QueryFile.Length <
	    sizeof (STORAGE_DEVICE_NUMBER)) {
		Irp->IoStatus.Information = sizeof (STORAGE_DEVICE_NUMBER);
		return (STATUS_BUFFER_TOO_SMALL);
	}

	PSTORAGE_DEVICE_NUMBER sdn = Irp->AssociatedIrp.SystemBuffer;
	sdn->DeviceNumber = 0;
	sdn->DeviceType = FILE_DEVICE_DISK;
	sdn->PartitionNumber = -1; // -1 means can't be partitioned

	Irp->IoStatus.Information = sizeof (STORAGE_DEVICE_NUMBER);
	return (STATUS_SUCCESS);
}


NTSTATUS
ioctl_volume_get_volume_disk_extents(PDEVICE_OBJECT DeviceObject,
    PIRP Irp, PIO_STACK_LOCATION IrpSp)
{
	VOLUME_DISK_EXTENTS *vde = Irp->AssociatedIrp.SystemBuffer;
	mount_t *zmo = DeviceObject->DeviceExtension;
	int error;

	// One DISK_EXTENT is included, and we only ever reply with one.
	ULONG requiredSize = sizeof (VOLUME_DISK_EXTENTS);

	if (IrpSp->Parameters.QueryFile.Length < requiredSize) {
		Irp->IoStatus.Information = requiredSize;
		return (STATUS_BUFFER_TOO_SMALL);
	}

	Irp->IoStatus.Information = requiredSize;
	vde->NumberOfDiskExtents = 1;
	vde->Extents[0].DiskNumber = 0;
	vde->Extents[0].StartingOffset.QuadPart = 0ULL;
	vde->Extents[0].ExtentLength.QuadPart = 1024 * 1024 * 1024ULL;

	zfsvfs_t *zfsvfs = vfs_fsprivate(zmo);
	if (zfsvfs != NULL) {
		if ((error = zfs_enter(zfsvfs, FTAG)) == 0) {
			uint64_t refdbytes, availbytes, usedobjs, availobjs;
			dmu_objset_space(zfsvfs->z_os,
			    &refdbytes, &availbytes, &usedobjs, &availobjs);
			vde->Extents[0].ExtentLength.QuadPart =
			    refdbytes + availbytes;
			zfs_exit(zfsvfs, FTAG);
		}
	}

	return (STATUS_SUCCESS);
}

NTSTATUS
volume_create(PDEVICE_OBJECT DeviceObject, PFILE_OBJECT FileObject,
    USHORT ShareAccess, uint64_t AllocationSize, ACCESS_MASK DesiredAccess)
{
	mount_t *zmo = DeviceObject->DeviceExtension;

	if ((zmo->type != MOUNT_TYPE_VCB) &&
	    (zmo->type != MOUNT_TYPE_DCB) &&
	    (zmo->type != MOUNT_TYPE_VSS))
		return (STATUS_INVALID_PARAMETER);

#if 1
	if (FileObject->Vpb == NULL)
		FileObject->Vpb = zmo->vpb ? zmo->vpb : DeviceObject->Vpb;
#endif

	if (vfs_flags(zmo) & MNT_UNMOUNTING)
		return (STATUS_VOLUME_DISMOUNTED);

	int error;
	zfsvfs_t *zfsvfs;
	znode_t *zp;
	struct vnode *vp;
	zfs_ccb_t *zccb = NULL;
	NTSTATUS status = STATUS_VOLUME_DISMOUNTED;

#if 0
	zfsvfs = (zfsvfs_t *)vfs_fsprivate(zmo);

	if (!zfsvfs)
		return (status);

	if ((error = zfs_enter(zfsvfs, FTAG)) != 0)
		return (error);

	error = zfs_zget(zfsvfs, zfsvfs->z_root, &zp);

	if (error == 0) {
		vp = ZTOV(zp);

		if (vp == NULL) {
			status = STATUS_INSUFFICIENT_RESOURCES;
			goto out;
		}
		ASSERT0(FileObject->FsContext);
		dprintf("%s increasing %p\n", __func__, vp);
		zfs_couplefileobject(vp, NULL,
		    FileObject, zp->z_size, &zccb,
		    AllocationSize,
		    DesiredAccess,
		    NULL, Irp);
		// Undo the ref inside couplefileobject.
		vnode_rele(vp);
		atomic_inc_64(&zmo->volume_opens);
		VN_RELE(vp);

		status = STATUS_SUCCESS;
	}

out:
	zfs_exit(zfsvfs, FTAG);
#else

	if (vfs_isunmount(zmo))
		return (status);

	atomic_inc_64(&zmo->volume_opens);
	status = STATUS_SUCCESS;

#endif
	return (status);
}

NTSTATUS
volume_close(PDEVICE_OBJECT DeviceObject, PFILE_OBJECT FileObject)
{
	mount_t *zmo = DeviceObject->DeviceExtension;
	VERIFY(zmo->type == MOUNT_TYPE_DCB);

	int error;
	zfsvfs_t *zfsvfs;
	znode_t *zp;
	struct vnode *vp;
	zfs_ccb_t *zccb = NULL;

#if 0
	zfsvfs = (zfsvfs_t *)vfs_fsprivate(zmo);

	// This shouldnt happen, but it does sometimes
	if (zfsvfs == NULL)
		return (STATUS_DEVICE_NOT_READY);

	error = zfs_zget(zfsvfs, zfsvfs->z_root, &zp);

	if (error == 0) {
		vp = ZTOV(zp);

		dprintf("%s decreasing %p\n", __func__, vp);
		zfs_decouplefileobject(vp, FileObject);
		// vnode_rele(vp);
		atomic_dec_64(&zmo->volume_opens);
		dprintf("%s zmo->volume_opens %llu\n", __func__,
		    zmo->volume_opens);
		VN_RELE(vp);
		dprintf("vp %p iocount %d\n", vp, vp->v_iocount);

		return (STATUS_SUCCESS);
	}
#else

	atomic_dec_64(&zmo->volume_opens);
	return (STATUS_SUCCESS);

#endif
	return (STATUS_DEVICE_NOT_READY);
}

/*
 * IRP_MJ_CLEANUP - sent when Windows is done with FileObject HANDLE
 * (one of many)
 * the vp is not released here, just decrease a count of vp.
 */
int
zfs_fileobject_cleanup(PDEVICE_OBJECT DeviceObject, PIRP Irp,
    PIO_STACK_LOCATION IrpSp, vnode_t **hold_vp)
{
	int Status = STATUS_SUCCESS;
	mount_t *zmo = DeviceObject->DeviceExtension;
	mount_t *fzmo = NULL;
	PFILE_OBJECT FileObject = IrpSp->FileObject;
	struct vnode *vp = NULL;
	znode_t *zp = NULL;
	zfs_ccb_t *zccb = NULL;
	boolean_t locked = B_FALSE;
	vnode_t *dvp = NULL;

	/* captured decisions */
	boolean_t need_notify_cleanup = B_FALSE;
	boolean_t need_delete = B_FALSE;
	boolean_t need_flush = B_FALSE;
	boolean_t need_purge = B_FALSE;
	boolean_t need_cache_uninit = B_FALSE;
	boolean_t need_hardlink_delete = B_FALSE;
	boolean_t deleted = B_FALSE;

	if (zmo->type != MOUNT_TYPE_VCB) {
		Status = STATUS_SUCCESS;
		goto out;
	}

	if (FileObject == NULL || FileObject->FsContext == NULL) {
		Status = STATUS_SUCCESS;
		goto out;
	}

	if (FileObject->Flags & FO_CLEANUP_COMPLETE) {
		dprintf("FileObject %p already cleaned up\n", FileObject);
		Status = STATUS_SUCCESS;
		goto out;
	}

	vp = FileObject->FsContext;
	zccb = FileObject->FsContext2;
	fzmo = vnode_mount(vp);
	zp = VTOZ(vp);

	FsRtlCheckOplockEx(
	    vp_oplock(vp),
	    Irp,
	    OPLOCK_FLAG_COMPLETE_IF_OPLOCKED,
	    NULL, NULL, NULL);

	dprintf("IRP_MJ_CLEANUP: '%s' iocount %u usecount %u unlink %u "
	    "zccb %p\n",
	    zccb && zccb->z_name_cache ? zccb->z_name_cache : "",
	    vp->v_iocount, vp->v_usecount, vnode_unlink(vp), zccb);

	if (zccb && zccb->HoldsOplock) {
		zccb->HoldsOplock = FALSE;
		atomic_dec_64(&vp->OplockRefCount);
	}

	ExAcquireResourceExclusiveLite(vp->FileHeader.Resource, TRUE);
	locked = B_TRUE;

	/*
	 * Protect only Windows/FCB-ish per-file state here.
	 */
	IoRemoveShareAccess(FileObject, &vp->share_access);

	FsRtlFastUnlockAll(&vp->lock, FileObject,
	    IoGetRequestorProcess(Irp), NULL);

	if (fzmo && zccb && fzmo->NotifySync)
		need_notify_cleanup = B_TRUE;

	if (zccb && zccb->cacheinit && FileObject->PrivateCacheMap)
		need_cache_uninit = B_TRUE;

	/*
	 * Flush dirty CM pages before uninitialising the cache, but ONLY
	 * when this specific file object owns the cache
	 * (has a PrivateCacheMap).
	 *
	 * CcIsFileCached() checks SharedCacheMap, which is shared across all
	 * file objects for the same vnode.  Using it here would trigger
	 * CcFlushCache for read-only or attribute-query file objects (e.g.
	 * from NtQueryAttributesFile) whenever another handle has dirty cached
	 * writes.  CcFlushCache then calls MiWaitForPageWriteCompletion which
	 * blocks until the Lazy Writer finishes its paging I/O.  If those
	 * paging IRPs get ERESTART from dmu_tx_assign and enter dmu_tx_wait
	 * while holding PagingIoResource, other paging writes to the same
	 * file are serialised, and if txg_quiesce is simultaneously waiting
	 * for tc_count to drop to 0 (held by a concurrent write to a second
	 * file), the result is a permanent livelock.
	 *
	 * The correct guard is FileObject->PrivateCacheMap: it is non-NULL
	 * only when *this* file object called CcInitializeCacheMap, i.e. it
	 * was opened for cached writes.  Only that file object should flush.
	 */
	if (need_cache_uninit || FileObject->PrivateCacheMap != NULL)
		need_flush = B_TRUE;

	/*
	 * Release long-term open hold for this FILE_OBJECT.
	 * After this, last-close conditions should be represented by your
	 * vnode usecount model.
	 */
	vnode_rele(vp);

	// Promote to global delete
	if (zccb && zccb->deleteonclose) {
		if (zp != NULL && zp->z_links > 1) {
			/*
			 * Hardlink: only this specific directory entry is
			 * being removed; the inode survives under its other
			 * names.  Do NOT mark the vnode DELETE_HIDDEN —
			 * that flag is vnode-wide and would hide every name
			 * for this inode from directory listings.  Instead,
			 * remove just the one name after the lock drops.
			 */
			need_hardlink_delete = B_TRUE;
		} else {
			vnode_setunlink(vp, DELETE_PENDING|DELETE_HIDDEN);
		}
		zccb->deleteonclose = 0;
	}

	/*
	 * Real delete is global to the vnode, not this particular FILE_OBJECT.
	 * We only do it on last close.
	 *
	 * vnode_isinuse() only tracks v_usecount; it knows nothing about
	 * v_fileobjects, the separate list of FILE_OBJECTs coupled to this
	 * vnode.  A second, still-open FILE_OBJECT can leave v_usecount at 0
	 * while remaining registered in v_fileobjects (its own decoupling
	 * happens later, at its own IRP_MJ_CLOSE) -- without this check,
	 * delete_entry() below frees the underlying storage and reclaims
	 * the vnode while that other FILE_OBJECT is still live, so its
	 * eventual close/decouple then touches an already-DEAD vnode.
	 *
	 * Mark this FILE_OBJECT cleaned-up first so vnode_fileobject_count()
	 * excludes it too: a FO's own IRP_MJ_CLOSE can be delayed well past
	 * its IRP_MJ_CLEANUP (e.g. while Cc finishes tearing down its cache
	 * map asynchronously), during which it is a "zombie" still sitting
	 * in v_fileobjects but no longer genuinely in use.  Without excluding
	 * cleaned-up FOs, a vnode that churns opens/closes fast enough (seen
	 * with FreeFileSync's lock-file pattern) can find a zombie FO still
	 * present when the real delete's cleanup runs, permanently deferring
	 * need_delete and leaving vnode_unlink() stuck set -- every later
	 * open of that name then gets STATUS_DELETE_PENDING (surfaces to
	 * callers as generic access-denied) forever.  "Nobody else genuinely
	 * attached" is therefore count == 0, not <= 1.  Mirrors the same
	 * three-way check (iocount/usecount/fileobjects) vnode_drain_
	 * delayclose() already requires before it will recycle a vnode.
	 */
	vnode_fileobject_mark_cleanedup(vp, FileObject);
	if (!vnode_isinuse(vp, 0) && vnode_unlink(vp) &&
	    vnode_fileobject_count(vp, 0) == 0) {
		need_delete = B_TRUE;
		need_purge = need_flush;
	} else {

	}

	ExReleaseResourceLite(vp->FileHeader.Resource);
	locked = B_FALSE;

	/*
	 * Outside FileHeader.Resource from here down.
	 * Avoid calling MM/CC/FsRtl namespace-ish work while holding it.
	 */

	/*
	 * We need to disconnect the parent first, since it is holding a
	 * dvp->usecount while parent is referenced. If we do not, then
	 * calling zfs_rmdir() will return ENOTEMPTY.
	 */
	dvp = vnode_parent(vp); // Best effort, can be NULL.
	vnode_setparent(vp, NULL);

	dprintf("FO %p CacheSup %lu SecObjPtr %p and DatSecObj %p\n",
	    FileObject,
	    FileObject->Flags & FO_CACHE_SUPPORTED,
	    FileObject->SectionObjectPointer,
	    FileObject->SectionObjectPointer &&
	    FileObject->SectionObjectPointer->DataSectionObject);

	if (need_flush) {
		IO_STATUS_BLOCK iosb = {0};

		dprintf("CcFlushCache: %s cacheinit %llu PrivCM %p "
		    "zp_sz %I64x VDL %I64x\n",
		    zccb && zccb->z_name_cache ? zccb->z_name_cache : "?",
		    zccb ? zccb->cacheinit : 0,
		    FileObject->PrivateCacheMap,
		    zp ? (uint64_t)zp->z_size : 0,
		    vp->FileHeader.ValidDataLength.QuadPart);
		CcFlushCache(FileObject->SectionObjectPointer, NULL, 0, &iosb);
		dprintf("CcFlushCache done: status %08lx info %Iu\n",
		    iosb.Status, iosb.Information);
	}

	if (need_purge) {
		dprintf("Truncating cache to zero due to delete\n");
		/*
		 * Match NTFS/WinBtrfs/FastFAT: tell the Cache Manager the
		 * data is gone by shrinking sizes to zero via
		 * CcSetFileSizes, the same non-blocking mechanism ordinary
		 * truncate-to-zero already uses successfully -- not
		 * CcPurgeCacheSection, which can block indefinitely on
		 * nt!CcCollisionDelay.  Acquire+release PagingIoResource as
		 * a barrier to drain in-flight paging I/O before changing
		 * the sizes; never hold it across CcSetFileSizes.
		 */
		ExAcquireResourceExclusiveLite(vp->FileHeader.PagingIoResource,
		    TRUE);
		vp->FileHeader.AllocationSize.QuadPart = 0;
		vp->FileHeader.FileSize.QuadPart = 0;
		vp->FileHeader.ValidDataLength.QuadPart = 0;
		ExReleaseResourceLite(vp->FileHeader.PagingIoResource);

		if (FileObject->SectionObjectPointer &&
		    CcIsFileCached(FileObject)) {
			CC_FILE_SIZES ccfs;
			ccfs.AllocationSize = vp->FileHeader.AllocationSize;
			ccfs.FileSize = vp->FileHeader.FileSize;
			ccfs.ValidDataLength =
			    vp->FileHeader.ValidDataLength;
			try {
				CcSetFileSizes(FileObject, &ccfs);
			} except(EXCEPTION_EXECUTE_HANDLER) {
				dprintf("CcSetFileSizes threw exception "
				    "%08lx\n", GetExceptionCode());
			}
		}
	}

	if (need_cache_uninit) {
		CACHE_UNINITIALIZE_EVENT cc_uninit_event;

		dprintf("CcUninitializeCacheMap on vp %p fo %p, Vpb %p\n",
		    vp, FileObject, FileObject->Vpb);
		atomic_dec_64(&zccb->cacheinit);

		KeInitializeEvent(&cc_uninit_event.Event,
		    SynchronizationEvent, FALSE);

		/*
		 * If teardown can't complete synchronously (eg it still
		 * needs one more lazy-write pass to let go of the section),
		 * CcUninitializeCacheMap defers and signals cc_uninit_event
		 * once truly done. We must wait for that here, while zfsvfs
		 * is still guaranteed valid: if the containing pool is
		 * exported/unmounted first, zfs_AcquireForLazyWrite starts
		 * returning FALSE (zfsvfs gone), the deferred lazy-write
		 * pass can never complete, and this vnode's SharedCacheMap
		 * is stuck forever -- reproduced via a Recycle Bin $I file
		 * left mid-teardown when zpool export ran shortly after.
		 */
		if (!CcUninitializeCacheMap(FileObject, NULL,
		    &cc_uninit_event)) {
			dprintf("CcUninitializeCacheMap deferred on vp %p "
			    "fo %p, waiting\n", vp, FileObject);
			KeWaitForSingleObject(&cc_uninit_event.Event,
			    Executive, KernelMode, FALSE, NULL);
			dprintf("CcUninitializeCacheMap wait complete on vp "
			    "%p fo %p\n", vp, FileObject);
		}
	}

	/*
	 * Hardlink deletion: remove only the opened directory entry.
	 * Unlike the single-link delete_entry() path, we must NOT zero
	 * vp->FileHeader sizes or call CcSetFileSizes — the inode is
	 * still alive under its other names and those handles must
	 * continue to work normally.
	 *
	 * zccb->z_name_cache is per-file-object (not per-vnode) and was
	 * set at open time to the exact name used to open this link, so
	 * z_name_cache + z_name_offset gives the correct component name
	 * to pass to zfs_remove without any further conversion.
	 */
	if (need_hardlink_delete) {
		zfsvfs_t *zfsvfs = vfs_fsprivate(fzmo);

		if (zccb && zccb->z_name_cache != NULL && dvp != NULL) {
			char *hl_name =
			    zccb->z_name_cache + zccb->z_name_offset;
			dprintf("%s: hardlink remove '%s'\n",
			    __func__, hl_name);
			zfs_send_notify(zfsvfs, zccb->z_name_cache,
			    zccb->z_name_offset,
			    FILE_NOTIFY_CHANGE_FILE_NAME,
			    FILE_ACTION_REMOVED);
			VERIFY0(VN_HOLD(dvp));
			int hl_err = zfs_remove(VTOZ(dvp),
			    hl_name, NULL, 0);
			VN_RELE(dvp);
			dvp = NULL;
			if (hl_err != 0)
				dprintf("%s: hardlink remove failed %d\n",
				    __func__, hl_err);
		} else {
			dprintf("%s: hardlink delete: no zccb/dvp\n",
			    __func__);
		}
	}

	if (need_delete) {
		zfsvfs_t *zfsvfs = vfs_fsprivate(fzmo);

		if (zccb && zccb->z_name_cache != NULL) {
			/*
			 * Use the filename from the FileObject (the original
			 * open path) rather than zccb->z_name_cache (the
			 * inode's primary name).  For hard links, z_name_cache
			 * reflects whichever name was first resolved for this
			 * inode (e.g. 'file2.txt') and not the link name that
			 * was actually opened for deletion (e.g. 'file3.txt').
			 * Sending the wrong name in FILE_ACTION_REMOVED
			 * misleads Explorer + other directory-watch consumers.
			 */
			char _notify_buf[MAXPATHLEN];
			char *notify_name = zccb->z_name_cache;
			int notify_offset = zccb->z_name_offset;

			if (FileObject->FileName.Buffer != NULL &&
			    FileObject->FileName.Length > 0) {
				ULONG outlen = 0;
				NTSTATUS ns = RtlUnicodeToUTF8N(_notify_buf,
				    MAXPATHLEN - 1, &outlen,
				    FileObject->FileName.Buffer,
				    FileObject->FileName.Length);
				if (NT_SUCCESS(ns) ||
				    ns == STATUS_SOME_NOT_MAPPED) {
					_notify_buf[outlen] = '\0';
					notify_name = _notify_buf;
					notify_offset = 0;
				}
			}

			if (vnode_isdir(vp)) {
				dprintf("DIR: FileDelete '%s' name '%s'\n",
				    notify_name,
				    notify_name + notify_offset);
				zfs_send_notify(zfsvfs,
				    notify_name,
				    notify_offset,
				    FILE_NOTIFY_CHANGE_DIR_NAME,
				    FILE_ACTION_REMOVED);
			} else {
				dprintf("FILE: FileDelete '%s' name '%s'\n",
				    notify_name,
				    notify_name + notify_offset);
				zfs_send_notify(zfsvfs,
				    notify_name,
				    notify_offset,
				    FILE_NOTIFY_CHANGE_FILE_NAME,
				    FILE_ACTION_REMOVED);
			}
		}

		/*
		 * delete_entry() consumes the final iocount and releases vp.
		 * Do not touch vp after this call succeeds or fails.
		 */
		*hold_vp = NULL;
		Status = delete_entry(DeviceObject, Irp, IrpSp, dvp);
		if (Status != STATUS_SUCCESS) {
			dprintf("Deletion failed: %08x\n", Status);
			vnode_setunlink(vp, DELETE_PENDING);

			/*
			 * Optional retry behavior: re-arm unlink on failure.
			 * Uncomment if that matches your intended semantics.
			 *
			 * vnode_t *tmpvp = FileObject->FsContext;
			 * if (tmpvp != NULL)
			 *     vnode_setunlink(tmpvp, 1);
			 */
		} else {
			deleted = B_TRUE;

#if defined(ZFS_FS_ATTRIBUTE_CLEANUP_INFO) && defined(ZFS_FS_ATTRIBUTE_POSIX)
			Irp->IoStatus.Information =
			    FILE_CLEANUP_FILE_DELETED |
			    FILE_CLEANUP_POSIX_STYLE_DELETE;
#elif defined(ZFS_FS_ATTRIBUTE_CLEANUP_INFO) && \
	defined(FILE_CLEANUP_FILE_DELETED)
			Irp->IoStatus.Information =
			    FILE_CLEANUP_FILE_DELETED;
#endif
		}
	}

	if (need_notify_cleanup) {
		FsRtlNotifyCleanup(fzmo->NotifySync, &fzmo->DirNotifyList,
		    zccb);
	}

	FileObject->Flags |= FO_CLEANUP_COMPLETE;

	/*
	 * If delete_entry() consumed the vnode, do not log vp/zp here.
	 */
	if (!deleted && vp && zp) {
		dprintf(
		    "%s: '%s' iocount %u usecount %u unlink %u Status 0x%x\n",
		    __func__,
		    zccb && zccb->z_name_cache ? zccb->z_name_cache : "",
		    vp->v_iocount, vp->v_usecount, vnode_unlink(vp), Status);
	}

out:
	if (locked)
		ExReleaseResourceLite(vp->FileHeader.Resource);

	Irp->IoStatus.Status = Status;
	return (Status);
}

/*
 * IRP_MJ_CLOSE - sent when Windows is done with FileObject, and we can
 * free memory.
 */
int
zfs_fileobject_close(PDEVICE_OBJECT DeviceObject, PIRP Irp,
    PIO_STACK_LOCATION IrpSp, vnode_t **hold_vp)
{
	int Status = STATUS_SUCCESS;

	ASSERT(hold_vp != NULL);

	mount_t *zmo = DeviceObject->DeviceExtension;

	if (IrpSp->FileObject) {
		if (IrpSp->FileObject->FsContext) {
			vnode_t *vp = IrpSp->FileObject->FsContext;
			zfs_ccb_t *zccb = IrpSp->FileObject->FsContext2;

			// int isdir = vnode_isdir(vp);
			zfs_decouplefileobject(vp, IrpSp->FileObject);
			zccb = NULL;

			if (!vnode_isvroot(vp)) {
				Status = STATUS_SUCCESS;

				vp = NULL; // Paranoia, signal it is gone.

			} else { /* root node */
				Status = STATUS_SUCCESS;
			}
		}
		return (Status);
	}
	return (Status);
}

/*
 * We received a long-lived ioctl, so lets setup a taskq to handle it,
 * and return pending
 * This code was proof-of-concept, and is NOT used.
 */
void
zfsdev_async_thread(void *arg)
{
	NTSTATUS Status;
	PIRP Irp;
	Irp = (PIRP)arg;

	dprintf("%s: starting ioctl\n", __func__);

	/* Use FKIOCTL to make sure it calls memcpy instead */
	Status = zfsdev_ioctl(NULL, Irp, FKIOCTL);

	dprintf("%s: finished ioctl %ld\n", __func__, Status);

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

	IoCompleteRequest(Irp,
	    Status == STATUS_SUCCESS ? IO_DISK_INCREMENT : IO_NO_INCREMENT);
}

/* Not used */
NTSTATUS
zfsdev_async(PDEVICE_OBJECT DeviceObject, PIRP Irp)
{
	int error;
	PMDL mdl = NULL;
	PIO_STACK_LOCATION IrpSp;
	zfs_cmd_t *zc;
	void *fp = NULL;

	IrpSp = IoGetCurrentIrpStackLocation(Irp);

	IoMarkIrpPending(Irp);

	/*
	 * A separate thread to the one that called us may not access the
	 * buffer from userland, So we have to map the in/out buffer,
	 * and put that address in its place.
	 */
	error = ddi_copysetup(
	    IrpSp->Parameters.DeviceIoControl.Type3InputBuffer,
	    sizeof (zfs_cmd_t),
	    &IrpSp->Parameters.DeviceIoControl.Type3InputBuffer, &mdl);
	if (error)
		return (error);

	/* Save the MDL so we can free it once done */
	Irp->Tail.Overlay.DriverContext[0] = mdl;

	/*
	 * We would also need to handle zc->zc_nvlist_src and zc->zc_nvlist_dst
	 * which is tricker, since they are unpacked into nvlists deep
	 * in zfsdev_ioctl
	 * The same problem happens for the filedescriptor from userland,
	 * also needs to be kernelMode
	 */
	zc = IrpSp->Parameters.DeviceIoControl.Type3InputBuffer;

	if (zc->zc_cookie) {
		error = ObReferenceObjectByHandle((HANDLE)zc->zc_cookie, 0, 0,
		    KernelMode, &fp, 0);
		if (error != STATUS_SUCCESS)
			goto out;
		Irp->Tail.Overlay.DriverContext[1] = fp;

		HANDLE h = NULL;
		error = ObOpenObjectByPointer(fp,
		    OBJ_FORCE_ACCESS_CHECK | OBJ_KERNEL_HANDLE, NULL,
		    GENERIC_READ|GENERIC_WRITE, *IoFileObjectType,
		    KernelMode, &h);
		if (error != STATUS_SUCCESS)
			goto out;
		dprintf("mapped filed is 0x%p\n", h);
		zc->zc_cookie = (uint64_t)h;
		Irp->Tail.Overlay.DriverContext[2] = h;
	}

	taskq_dispatch(system_taskq, zfsdev_async_thread, (void*)Irp, TQ_SLEEP);
	return (STATUS_PENDING);

out:
	if (mdl) {
		UnlockAndFreeMdl(mdl);
	}
	if (fp) {
		ObDereferenceObject(fp);
	}
	return (error);
}

/*
 * dispatcher for the "bus", so we can add pnp devices for mounting
 */
_Function_class_(DRIVER_DISPATCH)
    static NTSTATUS
    busDispatcher(_In_ PDEVICE_OBJECT DeviceObject, _Inout_ PIRP *PIrp,
    PIO_STACK_LOCATION IrpSp)
{
	NTSTATUS Status;
	PIRP Irp = *PIrp;
	ZFS_DRIVER_EXTENSION(WIN_DriverObject, DriverExtension);

	dprintf("  %s: enter: major %d: minor %d: %s busDeviceObject\n",
	    __func__, IrpSp->MajorFunction, IrpSp->MinorFunction,
	    major2str(IrpSp->MajorFunction, IrpSp->MinorFunction));

	Status = STATUS_INVALID_DEVICE_REQUEST;
	mount_t *zmo = DeviceObject->DeviceExtension;

	switch (IrpSp->MajorFunction) {

	case IRP_MJ_PNP:
		switch (IrpSp->MinorFunction) {
		case IRP_MN_START_DEVICE:
			dprintf("IRP_MN_START_DEVICE\n");

			Status = STATUS_SUCCESS;
			if (zmo->AttachedDevice) {
				IoSkipCurrentIrpStackLocation(Irp);
				Status = IoCallDriver(zmo->AttachedDevice, Irp);
				*PIrp = NULL; // Stop completion of IRP below
			}
			break;
		case IRP_MN_CANCEL_REMOVE_DEVICE:
			dprintf("IRP_MN_CANCEL_REMOVE_DEVICE\n");
			Status = STATUS_SUCCESS;
			break;
		case IRP_MN_SURPRISE_REMOVAL:
			dprintf("IRP_MN_SURPRISE_REMOVAL\n");
			Status = STATUS_SUCCESS;
			break;
		case IRP_MN_REMOVE_DEVICE:
			dprintf("IRP_MN_REMOVE_DEVICE\n");
			VERIFY(zmo->type == MOUNT_TYPE_BUS);

			if (DriverExtension->Unload_Module == B_TRUE) {
		// DriverExtension->LowerDeviceObject == zmo_bus->AttachedDevice
				IoDetachDevice(zmo->AttachedDevice);

				// Forward the IRP down stack before deleting
				IoSkipCurrentIrpStackLocation(Irp);
				Status = IoCallDriver(zmo->AttachedDevice, Irp);

				zfs_unload_stage_1();

				*PIrp = NULL; // Stop completion of IRP below
				Status = STATUS_SUCCESS;
				break;
			}
			Status = STATUS_UNSUCCESSFUL;
			break;
		case IRP_MN_QUERY_REMOVE_DEVICE:
			dprintf("IRP_MN_QUERY_REMOVE_DEVICE\n");

			if (DriverExtension->Unload_Module == B_TRUE) {
				IoSkipCurrentIrpStackLocation(Irp);
				Status = IoCallDriver(zmo->AttachedDevice, Irp);
				*PIrp = NULL; // Stop completion of IRP below
				if (NT_SUCCESS(Status)) {
					Status = STATUS_SUCCESS;
					dprintf("IRP_MN_QUERY_REMOVE_DEVICE:"
					    " success\n");
				} else {
					Status = STATUS_UNSUCCESSFUL;
					dprintf("IRP_MN_QUERY_REMOVE_DEVICE:"
					    " unsuccessful\n");
				}
			} else {
				Status = STATUS_UNSUCCESSFUL;
			}
			break;
		case IRP_MN_QUERY_DEVICE_RELATIONS:
			Status = QueryDeviceRelations(DeviceObject, PIrp,
			    IrpSp);
			break;
		case IRP_MN_QUERY_CAPABILITIES:
			Status = QueryCapabilities(DeviceObject, Irp, IrpSp);
			break;
		case IRP_MN_QUERY_PNP_DEVICE_STATE:
			Status = pnp_device_state(DeviceObject, Irp, IrpSp);
			break;
		case IRP_MN_QUERY_ID:
			Status = pnp_query_id(DeviceObject, Irp, IrpSp);
			break;

			// maybes
		case IRP_MN_QUERY_INTERFACE:
			Status = pnp_query_di(DeviceObject, Irp, IrpSp);
			break;
		case IRP_MN_QUERY_BUS_INFORMATION:
			dprintf("IRP_MN_QUERY_BUS_INFORMATION\n");
			Status = pnp_query_bus_information(DeviceObject, Irp,
			    IrpSp);
			break;

		// these are not handled in btrfs, pass down
		case IRP_MN_DEVICE_ENUMERATED:
			dprintf("IRP_MN_DEVICE_ENUMERATED\n");
			Status = STATUS_SUCCESS;
			break;
		case IRP_MN_FILTER_RESOURCE_REQUIREMENTS:
			dprintf("IRP_MN_FILTER_RESOURCE_REQUIREMENTS\n");
			Status = STATUS_SUCCESS;
			break;
		case 0x18:
			dprintf("IRP_MN_QUERY_LEGACY_BUS_INFORMATION\n");
			break;
		default:
			dprintf("**** unknown pnp IRP_MJ_PNP: 0x%hhx\n",
			    IrpSp->MinorFunction); // 0x0b
			break;
		}
		break;

	case IRP_MJ_DEVICE_CONTROL:
		dprintf("**** unknown pnp IRP_MJ_DEVICE_CONTROL: 0x%hhx\n",
		    IrpSp->MinorFunction);
		break;

		/* Allow device \OpenZFS to be opened in WinObj */
	case IRP_MJ_CREATE:
	case IRP_MJ_CLEANUP:
	case IRP_MJ_CLOSE:
		Status = STATUS_SUCCESS;
		break;

		/* No mount expected on the bus */
#if 1
	case IRP_MJ_FILE_SYSTEM_CONTROL:
		switch (IrpSp->MinorFunction) {
		case IRP_MN_MOUNT_VOLUME:
			dprintf("IRP_MN_MOUNT_VOLUME ioctl\n");
			Status = zfs_vnop_mount(DeviceObject, Irp, IrpSp);
		}
		break;
#endif
	default:
		dprintf("**** unknown pnp IRP_MJ_: 0x%lx\n",
		    IrpSp->MajorFunction); // 0x0e
	}

	// If diskDispatcher() dont handle it, send it down somewhere
	if (Status == STATUS_INVALID_DEVICE_REQUEST &&
	    zmo->AttachedDevice && *PIrp != NULL) {
		*PIrp = NULL;
		dprintf("%s Passing down\n", __func__);
		IoSkipCurrentIrpStackLocation(Irp);
		return (IoCallDriver(zmo->AttachedDevice, Irp));
	}

	return (Status);
}

/*
 * This is the ioctl handler for ioctl done directly on /dev/zfs node.
 * This means all the internal ZFS ioctls, like ZFS_IOC_SEND etc.
 * But, we will also get general Windows ioctls, not specific to
 * volumes, or filesystems.
 * Incidentally, cstyle is confused about the function_class
 */
_Function_class_(DRIVER_DISPATCH)
    static NTSTATUS
    ioctlDispatcher(_In_ PDEVICE_OBJECT DeviceObject, _Inout_ PIRP *PIrp,
    PIO_STACK_LOCATION IrpSp)
{
	NTSTATUS Status;
	PIRP Irp = *PIrp;
	ZFS_DRIVER_EXTENSION(WIN_DriverObject, DriverExtension);

	dprintf("  %s: enter: major %d: minor %d: %s ioctlDeviceObject\n",
	    __func__, IrpSp->MajorFunction, IrpSp->MinorFunction,
	    major2str(IrpSp->MajorFunction, IrpSp->MinorFunction));

	Status = STATUS_NOT_IMPLEMENTED;

	switch (IrpSp->MajorFunction) {

	case IRP_MJ_CREATE:
		dprintf("IRP_MJ_CREATE: zfsdev FileObject %p name '%wZ' "
		    "length %u flags 0x%x\n",
		    IrpSp->FileObject, &IrpSp->FileObject->FileName,
		    IrpSp->FileObject->FileName.Length, IrpSp->Flags);
		Status = zfsdev_open((dev_t)IrpSp->FileObject, Irp);
		break;
	case IRP_MJ_CLOSE:
		Status = zfsdev_release((dev_t)IrpSp->FileObject, Irp);
		break;
	case IRP_MJ_DEVICE_CONTROL:
		{
			/* Is it a ZFS ioctl? */
			ulong_t cmd =
			    IrpSp->Parameters.DeviceIoControl.IoControlCode;

			if ((DEVICE_TYPE_FROM_CTL_CODE(cmd) == ZFSIOCTL_TYPE)) {
				ulong_t cmd2;

				cmd2 = DEVICE_FUNCTION_FROM_CTL_CODE(cmd);
				if ((cmd2 >= ZFSIOCTL_BASE + ZFS_IOC_FIRST &&
				    cmd2 < ZFSIOCTL_BASE + ZFS_IOC_LAST)) {

					cmd2 -= ZFSIOCTL_BASE;

/*
 * Some IOCTL are very long-living, so we will put them in the
 * background and return PENDING. Possibly we should always do
 * this logic, but some ioctls are really short lived.
 */
					switch (cmd2) {
					case ZFS_IOC_UNREGISTER_FS:
// We abuse returnedBytes to send back busy
						Irp->IoStatus.Information =
						    zfs_ioc_unregister_fs();
						Status = STATUS_SUCCESS;
						break;
/*
 * So to do ioctl in async mode is a hassle, we have to do the copyin/copyout
 * MDL work in *this* thread, as the thread we spawn does not have access.
 * This would also include zc->zc_nvlist_src / zc->zc_nvlist_dst, so
 * zfsdev_ioctl() would need to be changed quite a bit. The file-descriptor
 * passed in (zfs send/recv) also needs to be opened for kernel mode. This
 * code is left here as an example on how it can be done
 * (without zc->zc_nvlist_*) but we currently do not use it.
 * Everything is handled synchronously.
 *
 * case ZFS_IOC_SEND:
 *	Status = zfsdev_async(DeviceObject, Irp);
 *	break;
 *
 */
					default:
						Status = zfsdev_ioctl(
						    DeviceObject, Irp, 0);
					} // switch cmd for async
					break;
				}
			}
			/* Not ZFS ioctl, handle Windows ones */
			switch (cmd) {
			case IOCTL_VOLUME_GET_GPT_ATTRIBUTES:
				dprintf("IOCTL_VOLUME_GET_GPT_ATTRIBUTES\n");
				Status = 0;
				break;
			case IOCTL_MOUNTDEV_QUERY_DEVICE_NAME:
				dprintf("IOCTL_MOUNTDEV_QUERY_DEVICE_NAME\n");
				Status = ioctl_query_device_name(DeviceObject,
				    Irp, IrpSp);
				break;
			case IOCTL_MOUNTDEV_QUERY_UNIQUE_ID:
				dprintf("IOCTL_MOUNTDEV_QUERY_UNIQUE_ID\n");
				Status = ioctl_query_unique_id(DeviceObject,
				    Irp, IrpSp);
				break;
			case IOCTL_MOUNTDEV_QUERY_STABLE_GUID:
				dprintf("IOCTL_MOUNTDEV_QUERY_STABLE_GUID\n");
				Status = ioctl_query_stable_guid(DeviceObject,
				    Irp, IrpSp);
				break;
			case IOCTL_MOUNTMGR_VOLUME_MOUNT_POINT_CREATED:
				dprintf("IOCTL_MOUNTMGR_VOLUME_MOUNT_POINT_"
				    "CREATED\n");
				Status = STATUS_SUCCESS;
				break;
			case IOCTL_MOUNTMGR_VOLUME_MOUNT_POINT_DELETED:
				dprintf("IOCTL_MOUNTMGR_VOLUME_MOUNT_POINT_"
				    "DELETED\n");
				Status = STATUS_SUCCESS;
				break;
			case IOCTL_MOUNTDEV_QUERY_SUGGESTED_LINK_NAME:
				dprintf("IOCTL_MOUNTDEV_QUERY_SUGGESTED_LINK_"
				    "NAME\n");
				break;
			case IOCTL_VOLUME_ONLINE:
				dprintf("IOCTL_VOLUME_ONLINE\n");
				Status = STATUS_SUCCESS;
				break;
			case IOCTL_DISK_IS_WRITABLE:
				dprintf("IOCTL_DISK_IS_WRITABLE\n");
				mount_t *zmo = DeviceObject->DeviceExtension;
				VERIFY(zmo->type == MOUNT_TYPE_VCB);
				zfsvfs_t *zfsvfs = vfs_fsprivate(zmo);
				if (zfsvfs != NULL && zfsvfs->z_rdonly)
					Status = STATUS_MEDIA_WRITE_PROTECTED;
				else
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
				Status = spl_kstat_chain_id(DeviceObject, Irp,
				    IrpSp);
				break;
			case KSTAT_IOC_READ:
				dprintf("KSTAT_IOC_READ\n");
				Status = spl_kstat_read(DeviceObject, Irp,
				    IrpSp);
				break;
			case KSTAT_IOC_WRITE:
				dprintf("KSTAT_IOC_WRITE\n");
				Status = spl_kstat_write(DeviceObject, Irp,
				    IrpSp);
				break;
			default:
				/*
				 * Catch VOLSNAP IOCTLs arriving at ioctlDevice.
				 * Return STATUS_NOT_SUPPORTED (not
				 * STATUS_NOT_IMPLEMENTED) so swprv's ichannel
				 * sees a hard error and skips this
				 * volume rather than treating a 0-byte
				 * STATUS_SUCCESS
				 * response from the lower device as valid data.
				 */
				if ((cmd >> 16) == 0x53) {
					dprintf("**** ioctl: unhandled VOLSNAP"
					    " 0x%lx -> NOT_SUPPORTED\n", cmd);
					Irp->IoStatus.Information = 0;
					Status = STATUS_NOT_SUPPORTED;
				} else {
					dprintf("**** unknown Windows IOCTL:"
					    " 0x%lx\n", cmd);
				}
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
			dprintf("IRP_MJ_FILE_SYSTEM_CONTROL unknown case!\n");
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
#if 0
			PVPB vpb = DeviceObject->Vpb;
			KIRQL OldIrql;
			IoAcquireVpbSpinLock(&OldIrql);
			vpb->ReferenceCount--;
			IoReleaseVpbSpinLock(&OldIrql);
#endif
			Status = STATUS_SUCCESS;
			break;
		case IRP_MN_START_DEVICE:
			dprintf("IRP_MN_START_DEVICE\n");
			Status = STATUS_SUCCESS;
			break;
		case IRP_MN_CANCEL_REMOVE_DEVICE:
			dprintf("IRP_MN_CANCEL_REMOVE_DEVICE\n");
			Status = STATUS_SUCCESS;
			break;
		case IRP_MN_QUERY_INTERFACE:
			Status = pnp_query_di(DeviceObject, Irp, IrpSp);
			break;
		default:
			dprintf("Unknown IRP_MJ_PNP(ioctl): 0x%x\n",
			    IrpSp->MinorFunction);
			break;
		}
		break;

	}

	return (Status);
}

/*
 * Return ZFS snapshots as a VOLSNAP_NAMES multi-string so that the
 * Windows Previous Versions shell extension (vssui!CVSSShellExt::AddPages)
 * finds our snapshots.  AddPages sends IOCTL_VOLSNAP_QUERY_NAMES_OF_SNAPSHOTS
 * to the live volume before contacting VSS; an empty response causes it to
 * exit in 0ms without ever calling our VSS provider's Query() method.
 *
 * Layout: VOLSNAP_NAMES { ULONG MultiSzLength; WCHAR Names[1]; }
 *   MultiSzLength = byte count of Names[] including the double-NUL
 *   Names[]       = "\Device\ZfsSnapshot<hex16>\0" × N, then L'\0' L'\0'
 *
 * Note: swprv (MS System Provider) also sends this IOCTL and will try to
 * open "<nt_device>@GMT-..." paths when it receives a non-empty list.
 * Those opens fail because ZFS live volumes don't have timewarp support yet.
 * swprv can only purge its own (MS Software Provider) snapshots, not ours,
 * so the failed opens are harmless — our VSS_VOLSNAP_ATTR_NO_AUTO_RELEASE
 * snapshots survive and remain visible via our provider's Query().
 */
static NTSTATUS
zfs_volsnap_query_names_of_snapshots(PDEVICE_OBJECT DeviceObject,
    PIRP Irp, PIO_STACK_LOCATION IrpSp)
{
	mount_t *zmo = DeviceObject->DeviceExtension;
	zfsvfs_t *zfsvfs = vfs_fsprivate(zmo);
	void *buf = Irp->AssociatedIrp.SystemBuffer;
	ULONG outlen = IrpSp->Parameters.DeviceIoControl.OutputBufferLength;

	if (zfsvfs == NULL || zfsvfs->z_os == NULL) {
		ULONG esz = sizeof (ULONG) + sizeof (WCHAR);
		Irp->IoStatus.Information = esz;
		if (outlen < esz)
			return (STATUS_BUFFER_TOO_SMALL);
		RtlZeroMemory(buf, esz);
		return (STATUS_SUCCESS);
	}

	/* Pass 1: count snapshots that have live kernel device objects */
	dsl_pool_t *vqn_dp = dmu_objset_pool(zfsvfs->z_os);
	ULONG nsnaps = 0;

	dsl_pool_config_enter(vqn_dp, FTAG);
	{
		uint64_t vqn_pos = 0;
		char vqn_snapname[MAXNAMELEN];
		uint64_t vqn_id;
		boolean_t vqn_cc;
		while (dmu_snapshot_list_next(zfsvfs->z_os,
		    sizeof (vqn_snapname), vqn_snapname,
		    &vqn_id, &vqn_pos, &vqn_cc) == 0) {
			dsl_dataset_t *vqn_ds;
			if (dsl_dataset_hold_obj(vqn_dp, vqn_id,
			    FTAG, &vqn_ds) == 0) {
				uint64_t g =
				    dsl_dataset_phys(vqn_ds)->ds_guid;
				dsl_dataset_rele(vqn_ds, FTAG);
				if (zfs_vss_has_device(g))
					nsnaps++;
			}
		}
	}
	dsl_pool_config_exit(vqn_dp, FTAG);

	/*
	 * Each entry: "\Device\ZfsSnapshot" (19 WCHARs) + 16 hex digits
	 * + NUL = 36 WCHARs.  The path must NOT end with "\" — srv2.sys
	 * checks [Buffer+Length*2] == '\' and if true jumps past the
	 * Smb2ShareAddSnapShot call, so a trailing '\' prevents the
	 * snapshot from ever entering the srv2 internal list.
	 * Plus 2 WCHARs for the multi-sz double-NUL terminator.
	 *
	 * srv2.sys (Smb2SnapCheckAppInfoForTimeWarp) passes each entry
	 * directly to NtOpenFile as an absolute NT device path, then
	 * issues IOCTL_VOLSNAP_QUERY_APPLICATION_INFO to get the @GMT
	 * timestamp.  We must return device paths, NOT @GMT strings.
	 */
	ULONG msz = (nsnaps * 36 + 2) * sizeof (WCHAR);
	ULONG needed = sizeof (ULONG) + msz;
	Irp->IoStatus.Information = needed;

	if (outlen < sizeof (ULONG))
		return (STATUS_BUFFER_TOO_SMALL);

	*(ULONG *)buf = msz;

	if (nsnaps == 0) {
		if (outlen < needed)
			return (STATUS_BUFFER_TOO_SMALL);
		WCHAR *wz = (WCHAR *)((UCHAR *)buf + sizeof (ULONG));
		wz[0] = L'\0';
		wz[1] = L'\0';
		dprintf("IOCTL_VOLSNAP_QUERY_NAMES_OF_SNAPSHOTS: 0 snaps\n");
		return (STATUS_SUCCESS);
	}
	if (outlen < needed)
		return (STATUS_BUFFER_OVERFLOW);

	/* Pass 2: fill "\Device\ZfsSnapshot<hex16>\0" entries */
	static const WCHAR vqn_pfx[] = L"\\Device\\ZfsSnapshot";
	static const WCHAR vqn_hex[] = L"0123456789abcdef";
	WCHAR *wp = (WCHAR *)((UCHAR *)buf + sizeof (ULONG));

	dsl_pool_config_enter(vqn_dp, FTAG);
	{
		uint64_t vqn_pos = 0;
		char vqn_snapname[MAXNAMELEN];
		uint64_t vqn_id;
		boolean_t vqn_cc;

		while (dmu_snapshot_list_next(zfsvfs->z_os,
		    sizeof (vqn_snapname), vqn_snapname,
		    &vqn_id, &vqn_pos, &vqn_cc) == 0) {
			uint64_t vqn_guid = 0;
			dsl_dataset_t *vqn_ds;
			if (dsl_dataset_hold_obj(vqn_dp, vqn_id,
			    FTAG, &vqn_ds) == 0) {
				vqn_guid = dsl_dataset_phys(vqn_ds)->ds_guid;
				dsl_dataset_rele(vqn_ds, FTAG);
			}

			/* Skip if the kernel device was removed */
			if (!zfs_vss_has_device(vqn_guid))
				continue;

			/* "\Device\ZfsSnapshot" prefix (19 WCHARs) */
			for (int vqn_j = 0; vqn_j < 19; vqn_j++)
				wp[vqn_j] = vqn_pfx[vqn_j];
			/* 16 hex digits of GUID */
			for (int vqn_h = 0; vqn_h < 16; vqn_h++)
				wp[19 + vqn_h] = vqn_hex[
				    (vqn_guid >> (60 - vqn_h * 4)) & 0xf];
			wp[35] = L'\0';

			dprintf("IOCTL_VOLSNAP_QUERY_NAMES_OF_SNAPSHOTS:"
			    " %S (snap %s)\n", wp, vqn_snapname);

			wp += 36;
		}
	}
	dsl_pool_config_exit(vqn_dp, FTAG);

	*wp++ = L'\0'; /* MULTI_SZ first terminator */
	*wp   = L'\0'; /* MULTI_SZ second terminator (double-NUL) */

	return (STATUS_SUCCESS);
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
    _Inout_ PIRP *PIrp,
    PIO_STACK_LOCATION IrpSp)
{
	NTSTATUS Status;
	PIRP Irp = *PIrp;
	mount_t *zmo = DeviceObject->DeviceExtension;
	VERIFY(zmo->type == MOUNT_TYPE_DCB);

	dprintf("  %s: enter: major %d: minor %d: %s diskDeviceObject\n",
	    __func__, IrpSp->MajorFunction, IrpSp->MinorFunction,
	    major2str(IrpSp->MajorFunction, IrpSp->MinorFunction));

	Status = STATUS_INVALID_DEVICE_REQUEST;

	switch (IrpSp->MajorFunction) {

	case IRP_MJ_CREATE:
		dprintf("IRP_MJ_CREATE: volume FileObject %p related %p "
		    "name '%wZ' flags 0x%x\n",
		    IrpSp->FileObject,
		    IrpSp->FileObject ?
		    IrpSp->FileObject->RelatedFileObject : NULL,
		    &IrpSp->FileObject->FileName, IrpSp->Flags);

		if (zmo->dcb_del_pending) {
			dprintf("IRP_MJ_CREATE rejected (dcb_del_pending)\n");
			Status = STATUS_DEVICE_REMOVED;
			break;
		}
		Status = volume_create(DeviceObject, IrpSp->FileObject,
		    IrpSp->Parameters.Create.ShareAccess,
		    Irp->Overlay.AllocationSize.QuadPart,
		    IrpSp->Parameters.Create.SecurityContext->DesiredAccess);
		if (NT_SUCCESS(Status))
			Irp->IoStatus.Information = FILE_OPENED;
		break;
	case IRP_MJ_CLOSE:
		Status = volume_close(DeviceObject, IrpSp->FileObject);
		break;
	case IRP_MJ_DEVICE_CONTROL:
	{
		ulong_t cmd = IrpSp->Parameters.DeviceIoControl.IoControlCode;
		/* Not ZFS ioctl, handle Windows ones */
		switch (cmd) {
		case IOCTL_VOLUME_GET_GPT_ATTRIBUTES:
			dprintf("IOCTL_VOLUME_GET_GPT_ATTRIBUTES\n");
			Status = ioctl_get_gpt_attributes(DeviceObject, Irp,
			    IrpSp);
			break;
		case IOCTL_MOUNTDEV_QUERY_DEVICE_NAME:
			dprintf("IOCTL_MOUNTDEV_QUERY_DEVICE_NAME\n");
			Status = ioctl_query_device_name(DeviceObject, Irp,
			    IrpSp);
			break;
		case IOCTL_MOUNTDEV_QUERY_UNIQUE_ID:
			dprintf("IOCTL_MOUNTDEV_QUERY_UNIQUE_ID\n");
			Status = ioctl_query_unique_id(DeviceObject, Irp,
			    IrpSp);
			break;
		case IOCTL_MOUNTDEV_QUERY_STABLE_GUID:
			dprintf("IOCTL_MOUNTDEV_QUERY_STABLE_GUID\n");
			Status = ioctl_mountdev_query_stable_guid(DeviceObject,
			    Irp, IrpSp);
			break;
		case IOCTL_MOUNTDEV_QUERY_SUGGESTED_LINK_NAME:
			dprintf("IOCTL_MOUNTDEV_QUERY_SUGGESTED_LINK_NAME\n");
			Status = ioctl_mountdev_query_suggested_link_name(
			    DeviceObject, Irp, IrpSp);
			break;
		case IOCTL_MOUNTMGR_VOLUME_MOUNT_POINT_CREATED:
			dprintf("IOCTL_MOUNTMGR_VOLUME_MOUNT_POINT_CREATED\n");
			Status = STATUS_SUCCESS;
			break;
		case IOCTL_MOUNTMGR_VOLUME_MOUNT_POINT_DELETED:
			dprintf("IOCTL_MOUNTMGR_VOLUME_MOUNT_POINT_DELETED\n");
			Status = STATUS_SUCCESS;
			break;
		case IOCTL_MOUNTMGR_DELETE_POINTS:
			dprintf("IOCTL_MOUNTMGR_DELETE_POINTS\n");
			Status = STATUS_SUCCESS;
			break;
		case IOCTL_VOLUME_ONLINE:
			dprintf("IOCTL_VOLUME_ONLINE\n");
			if (vfs_isunmount(DeviceObject->DeviceExtension))
				Status = STATUS_VOLUME_DISMOUNTED;
			else
				Status = STATUS_SUCCESS;
			break;
		case IOCTL_VOLUME_OFFLINE:
		case IOCTL_VOLUME_IS_OFFLINE:
			dprintf("IOCTL_VOLUME_OFFLINE\n");
			Status = STATUS_VOLUME_MOUNTED;
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
			Status = ioctl_storage_get_hotplug_info(DeviceObject,
			    Irp, IrpSp);
			break;
		case IOCTL_STORAGE_QUERY_PROPERTY:
			dprintf("IOCTL_STORAGE_QUERY_PROPERTY\n");
			Status = ioctl_storage_query_property(DeviceObject, Irp,
			    IrpSp);
			break;
		case 0x002D281C: /* unknown storage IOCTL (fn 0xA07) */
			dprintf("unknown storage IOCTL 0x002D281C"
			    " -> NOT_SUPPORTED\n");
			Irp->IoStatus.Information = 0;
			Status = STATUS_NOT_SUPPORTED;
			break;
		case IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS:
			dprintf("IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS\n");
			Status = ioctl_volume_get_volume_disk_extents(
			    DeviceObject, Irp, IrpSp);
			break;
		case IOCTL_STORAGE_GET_DEVICE_NUMBER:
			dprintf("IOCTL_STORAGE_GET_DEVICE_NUMBER\n");
			Status = ioctl_storage_get_device_number(DeviceObject,
			    Irp, IrpSp);
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
		case 0x4d0010:
// Same as IOCTL_MOUNTDEV_LINK_CREATED but bit 14,15 are 0 (access permissions)
			dprintf("IOCTL_MOUNTDEV_LINK_CREATED v2\n");
			Status = STATUS_SUCCESS;
			break;
		case IOCTL_MOUNTDEV_LINK_DELETED:
			dprintf("IOCTL_MOUNTDEV_LINK_DELETED\n");
			Status = STATUS_SUCCESS;
			break;
		case 0x4d0014:
// Same as IOCTL_MOUNTDEV_LINK_DELETED but bit 14,15 are 0 (access permissions)
			dprintf("IOCTL_MOUNTDEV_LINK_DELETED v2\n");
			Status = STATUS_SUCCESS;
			break;
		case IOCTL_DISK_GET_PARTITION_INFO_EX:
			dprintf("IOCTL_DISK_GET_PARTITION_INFO_EX\n");
			Status = ioctl_disk_get_partition_info_ex(DeviceObject,
			    Irp, IrpSp);
			break;
		case IOCTL_DISK_GET_DRIVE_GEOMETRY:
			dprintf("IOCTL_DISK_GET_DRIVE_GEOMETRY\n");
			Status = ioctl_disk_get_drive_geometry(DeviceObject,
			    Irp, IrpSp);
			break;
		case IOCTL_DISK_GET_DRIVE_GEOMETRY_EX:
			dprintf("IOCTL_DISK_GET_DRIVE_GEOMETRY_EX\n");
			Status = ioctl_disk_get_drive_geometry_ex(DeviceObject,
			    Irp, IrpSp);
			break;
		case IOCTL_STORAGE_CHECK_VERIFY:
			dprintf("IOCTL_STORAGE_CHECK_VERIFY\n");
			Status = STATUS_SUCCESS;
			break;
		case IOCTL_DISK_GET_PARTITION_INFO:
			dprintf("IOCTL_DISK_GET_PARTITION_INFO\n");
			Status = ioctl_disk_get_partition_info(DeviceObject,
			    Irp, IrpSp);
			break;
		case IOCTL_VOLUME_IS_IO_CAPABLE:
			dprintf("IOCTL_VOLUME_IS_IO_CAPABLE\n");
			Status = ioctl_volume_is_io_capable(DeviceObject, Irp,
			    IrpSp);
			break;
		case IOCTL_DISK_GET_LENGTH_INFO:
			dprintf("IOCTL_DISK_GET_LENGTH_INFO\n");
			Status = ioctl_disk_get_length_info(DeviceObject, Irp,
			    IrpSp);
			break;
		case FSCTL_GET_VOLUME_BITMAP: // VSS
			Status = STATUS_INVALID_DEVICE_REQUEST;
			break;
		case FSCTL_GET_RETRIEVAL_POINTERS: // VSS
			dprintf("FSCTL_GET_RETRIEVAL_POINTERS\n");
			Status = fsctl_get_retrieval_pointers(DeviceObject, Irp,
			    IrpSp);
			break;
		case ZFS_IOC_GET_MOUNT: // fsctl was too unreliable
			dprintf("ZFS_IOC_GET_MOUNT\n");
			Status = fsctl_zfs_volume_mountpoint(DeviceObject, Irp,
			    IrpSp);
			break;
		case IOCTL_VOLUME_IS_CLUSTERED: /* 0x560030 */
			/*
			 * srv2.sys sends this before enumerating shadow copies.
			 * STATUS_SUCCESS means "is clustered".
			 * STATUS_UNSUCCESSFUL means "not clustered" (normal).
			 * STATUS_INVALID_DEVICE_REQUEST means "invalid disk
			 * type" (e.g. dynamic) and causes srv2.sys to skip
			 * enumeration entirely.  Must return
			 * STATUS_UNSUCCESSFUL
			 * so srv2.sys proceeds to enumerate our ZFS snapshots.
			 */
			dprintf("disk: IOCTL_VOLUME_IS_CLUSTERED"
			    " -> not clustered\n");
			Irp->IoStatus.Information = 0;
			Status = STATUS_UNSUCCESSFUL;
			break;

		case IOCTL_VOLSNAP_FLUSH_AND_HOLD_WRITES:
		case 0x0053C004: /* IOCTL_VOLSNAP_RELEASE_WRITES */
			dprintf("disk: VOLSNAP flush/hold/release no-op"
			    " 0x%lx\n", cmd);
			Irp->IoStatus.Information = 0;
			Status = STATUS_SUCCESS;
			break;

		case 0x530018: /* IOCTL_VOLSNAP_QUERY_NAMES_OF_SNAPSHOTS */
		case 0x534014: /* IOCTL_VOLSNAP_QUERY_NAMES_OF_SNAPSHOTS */
			dprintf("disk: IOCTL_VOLSNAP_QUERY_NAMES_OF"
			    "_SNAPSHOTS\n");
			Status = zfs_volsnap_query_names_of_snapshots(
			    DeviceObject, Irp, IrpSp);
			break;

		case 0x530024: /* IOCTL_VOLSNAP_QUERY_DIFF_AREA */
		case 0x534058: /* IOCTL_VOLSNAP_QUERY_DIFF_AREA_MINIMUM_SIZE */
		case 0x53406C: /* IOCTL_VOLSNAP_QUERY_DIFF_AREA_INFORMATION */
		case 0x530050: /* IOCTL_VOLSNAP function 0x14 QUERY_EPIC */
		case 0x53001C: /* IOCTL_VOLSNAP function 0x7 */
			/*
			 * ZFS uses copy-on-write native snapshots, not a VSS
			 * diff-area.  Return STATUS_NOT_SUPPORTED for all
			 * diff-area IOCTLs so ichannel does not attempt to
			 * Unpack a fixed-size struct from our response (which
			 * caused "IOCTL Unpack overflow" / catastrophic failure
			 * when we returned STATUS_SUCCESS + sizeof(ULONG) bytes
			 * but ichannel expected sizeof(VOLSNAP_DIFF_AREA_TABLE)
			 * which includes an ANYSIZE_ARRAY slot).
			 */
			dprintf("disk: VOLSNAP diff-area ioctl 0x%lx"
			    " -> NOT_SUPPORTED\n", cmd);
			Irp->IoStatus.Information = 0;
			Status = STATUS_NOT_SUPPORTED;
			break;

		case 0x534070: /* IOCTL_VOLSNAP_QUERY_APPLICATION_FLAGS */
		{
			/*
			 * Returns ApplicationInformation (68 bytes):
			 *   ULONG  length          (4)
			 *   GUID   guid            (16)
			 *   GUID   shadowCopyGuid  (16)
			 *   GUID   shadowCopySetGuid (16)
			 *   ULONG  snapshotContext (4)
			 *   ULONG  unknown1        (4)
			 *   LONG   attributes      (4)
			 *   ULONG  unknown2        (4)
			 * Return all zeros: no VSS application has registered
			 * flags for this volume.
			 */
			const ULONG appsz = 4 + 16 + 16 + 16 + 4 + 4 + 4 + 4;
			dprintf("disk: IOCTL_VOLSNAP_QUERY_APPLICATION_FLAGS"
			    " -> 68 zero bytes\n");
			if (IrpSp->Parameters.DeviceIoControl.OutputBufferLength
			    < appsz) {
				Irp->IoStatus.Information = appsz;
				Status = STATUS_BUFFER_TOO_SMALL;
			} else {
				RtlZeroMemory(
				    Irp->AssociatedIrp.SystemBuffer, appsz);
				Irp->IoStatus.Information = appsz;
				Status = STATUS_SUCCESS;
			}
			break;
		}

		default:
			/*
			 * For VOLSNAP IOCTLs (device type 0x53) that we do not
			 * recognise, return STATUS_NOT_SUPPORTED explicitly so
			 * the IRP is NOT passed to the lower device.  The lower
			 * device may return STATUS_SUCCESS + 0 bytes for
			 * unknown IOCTLs, causing swprv's ichannel to attempt
			 * an Unpack from an empty buffer (overflow).
			 */
			if ((cmd >> 16) == 0x53) {
				dprintf("disk: unhandled VOLSNAP IOCTL 0x%lx"
				    " -> NOT_SUPPORTED\n", cmd);
				Irp->IoStatus.Information = 0;
				Status = STATUS_NOT_SUPPORTED;
			} else {
				dprintf("**** unknown disk Windows IOCTL:"
				    " 0x%lx\n", cmd);
			}
	//		DbgBreakPoint();
		}

	}
	break;

	case IRP_MJ_CLEANUP:
		Status = STATUS_SUCCESS;
		break;

	// Technically we don't really let them read from the virtual
	// devices that hold the ZFS filesystem, so we just return all zeros.
	case IRP_MJ_READ:
		Status = volume_read(DeviceObject, Irp, IrpSp);
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
			dprintf("IRP_MN_USER_FS_REQUEST: FsControlCode 0x%lx\n",
			    IrpSp->Parameters.FileSystemControl.FsControlCode);
			Status = user_fs_request(DeviceObject, PIrp, IrpSp);
			break;
		case IRP_MN_KERNEL_CALL:
			/*
			 * srv2.sys may issue FSCTL_SRV_ENUMERATE_SNAPSHOTS (and
			 * other FSCTLs) as IRP_MN_KERNEL_CALL on the
			 * disk device object.  Pass through to user_fs_request
			 * so that our FSCTL_SRV_ENUMERATE_SNAPSHOTS handler
			 * is reached.
			 */
			dprintf("IRP_MN_KERNEL_CALL: FsControlCode 0x%lx\n",
			    IrpSp->Parameters.FileSystemControl.FsControlCode);
			Status = user_fs_request(DeviceObject, PIrp, IrpSp);
			break;
		default:
			dprintf("IRP_MN_unknown: 0x%x\n", IrpSp->MinorFunction);
			break;
		}
		break;

	case IRP_MJ_QUERY_INFORMATION:
		dprintf("volume calling query_information warning\n");
		Status = query_information(DeviceObject, Irp, IrpSp);
		break;

	case IRP_MJ_QUERY_VOLUME_INFORMATION:
		Status = query_volume_information(DeviceObject, Irp, IrpSp);
		break;

	case IRP_MJ_PNP:
		switch (IrpSp->MinorFunction) {
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
			/*
			 * Block new IRP_MJ_CREATE opens so that
			 * IopInvalidateVolumesForDevice (called later in
			 * IopRemoveDevice on the same thread) cannot open a
			 * file object on this device after PnP has internally
			 * deleted the device node.  If it could, the subsequent
			 * close would fire IopCompleteUnloadOrDelete ->
			 * ObDereferenceSecurityDescriptor on an SD with
			 * already 0, BSODing (INVALID_REFERENCE_COUNT, 0x139).
			 */
			zmo->dcb_del_pending = B_TRUE;

			/*
			 * dcb_del_pending only blocks NEW opens; a FILE_OBJECT
			 * opened on this device before this point (e.g. by
			 * IopInvalidateVolumesForDevice, or Windows' own PnP
			 * worker thread doing housekeeping) may still be open
			 * or in the process of closing.  If DeviceObject's
			 * ReferenceCount is still > 0 when PnP's own
			 * device-node deletion proceeds (IopRemoveDevice /
			 * PnpDeleteLockedDeviceNode), that close races the
			 * device's own teardown and hits the exact same
			 * ObDereferenceSecurityDescriptor / 0x139 crash on an
			 * already-0 refcount.  Since dcb_del_pending now blocks
			 * new opens, the count can only decrease; yield briefly
			 * so any in-flight close can complete and drain it to 0
			 * before we hand control back to PnP.  Same pattern as
			 * zfs_vss_snapshot_remove() in zfs_vss.c, which fixed
			 * the identical race for VSS snapshot devices.  1 ms
			 * per tick, up to 100 ticks (100 ms total).
			 */
			if (DeviceObject->ReferenceCount > 0) {
				LARGE_INTEGER surprise_delay;
				int surprise_poll;

				surprise_delay.QuadPart = -10000LL; // 1ms
				dprintf("IRP_MN_SURPRISE_REMOVAL: "
				    "ReferenceCount > 0, waiting for PnP to "
				    "drain\n");
				for (surprise_poll = 0;
				    surprise_poll < 100 &&
				    DeviceObject->ReferenceCount > 0;
				    surprise_poll++) {
					KeDelayExecutionThread(KernelMode,
					    FALSE, &surprise_delay);
				}
				dprintf("IRP_MN_SURPRISE_REMOVAL: waited "
				    "%d ms, ReferenceCount now %d\n",
				    surprise_poll,
				    (int)DeviceObject->ReferenceCount);
			}
			Status = STATUS_SUCCESS;
			break;
		case IRP_MN_START_DEVICE:
			dprintf("IRP_MN_START_DEVICE\n");
			Status = STATUS_SUCCESS;
			break;
		case IRP_MN_CANCEL_REMOVE_DEVICE:
			dprintf("IRP_MN_CANCEL_REMOVE_DEVICE\n");
			Status = STATUS_SUCCESS;
			break;
		case IRP_MN_REMOVE_DEVICE:
			dprintf("IRP_MN_REMOVE_DEVICE\n");
			// Status = STATUS_UNSUCCESSFUL;
			Status = STATUS_SUCCESS;
			break;
		case IRP_MN_QUERY_DEVICE_RELATIONS:
			Status = QueryDeviceRelations(DeviceObject, PIrp,
			    IrpSp);
			break;
		case IRP_MN_DEVICE_USAGE_NOTIFICATION:
			dprintf("IRP_MN_DEVICE_USAGE_NOTIFICATION\n");
			Status = STATUS_SUCCESS;
			break;
		// The rest btrfs does not have, pass down
		case IRP_MN_QUERY_CAPABILITIES:
			dprintf("IRP_MN_QUERY_CAPABILITIES\n");
			Status = QueryCapabilities(DeviceObject, Irp, IrpSp);
			break;
		case IRP_MN_QUERY_INTERFACE:
			dprintf("IRP_MN_QUERY_INTERFACE\n");
			Status = pnp_query_di(DeviceObject, Irp, IrpSp);
			break;
		case IRP_MN_QUERY_DEVICE_TEXT:
			dprintf("IRP_MN_QUERY_DEVICE_TEXT\n");
			Status = pnp_query_device_text(DeviceObject, Irp,
			    IrpSp);
			break;
		case IRP_MN_QUERY_RESOURCE_REQUIREMENTS:
			dprintf("IRP_MN_QUERY_RESOURCE_REQUIREMENTS\n");
			break;
		case IRP_MN_QUERY_RESOURCES:
			dprintf("IRP_MN_QUERY_RESOURCES\n");
			break;
		case IRP_MN_QUERY_BUS_INFORMATION:
			dprintf("IRP_MN_QUERY_BUS_INFORMATION\n");
			/*
			 * not in btrfs Status =
			 * pnp_query_bus_information(DeviceObject, Irp, IrpSp);
			 */
			Status = pnp_query_bus_information(DeviceObject, Irp,
			    IrpSp);
			break;
		case IRP_MN_DEVICE_ENUMERATED:
			dprintf("IRP_MN_DEVICE_ENUMERATED\n");
			Status = STATUS_SUCCESS;
			break;
		default:
			dprintf("Unknown IRP_MJ_PNP(disk): 0x%x\n",
			    IrpSp->MinorFunction); // 0x18 0x0d

			break;
		}
		break;

	}

	// If diskDispatcher() dont handle it, send it down somewhere
	if (Status == STATUS_INVALID_DEVICE_REQUEST &&
	    zmo->AttachedDevice && *PIrp != NULL) {
		dprintf("%s Passing down\n", __func__);
		*PIrp = NULL;
		IoSkipCurrentIrpStackLocation(Irp);
		return (IoCallDriver(zmo->AttachedDevice, Irp));
	}

	return (Status);
}

/*
 * NTFS mangles filenames when redirecting a CREATE through one of its own
 * on-disk IO_REPARSE_TAG_MOUNT_POINT reparse points -- which is what
 * happens whenever a pool is mounted at a folder path rather than a drive
 * letter (see zfs_vnops_windows_mount.c, justDriveLetter == B_FALSE).
 * NtfsFindStartingNode uppercases the whole path to compute a lookup hash,
 * then reverts the portion before the reparse point back to its original
 * case -- but leaves the portion after the reparse point (the part
 * actually forwarded to us as the new IRP's FileName) uppercased.  For a
 * case-sensitive dataset this makes every lookup through a folder mount
 * fail; for a case-insensitive/mixed dataset it still resolves but
 * returns/creates the wrong case.
 *
 * Windows 7+ attaches an undocumented Extra Create Parameter (ECP) of type
 * IopSymlinkECPGuid to the reparsed IRP, which carries the original-case
 * path from before NTFS mangled it.  This recovers the original-case tail
 * from that ECP and overwrites FileObject->FileName's tail in place before
 * we ever use it for lookup.  It is a no-op (returns FALSE) whenever the
 * ECP or the MountPoint flag isn't present, so it never affects
 * drive-letter mounts or ordinary (non-reparsed) opens.
 *
 * IopSymlinkECPGuid and SYMLINK_ECP_CONTEXT are undocumented by Microsoft;
 * both are reverse-engineered, sourced from a WinBtrfs contributor's
 * write-up of this exact NTFS behaviour.
 */
static const GUID IopSymlinkECPGuid = {
	0x73d5118a, 0x88ba, 0x439f,
	{ 0x92, 0xf4, 0x46, 0xd3, 0x89, 0x52, 0xd2, 0x50 }
};

typedef struct _SYMLINK_ECP_CONTEXT {
	USHORT UnparsedNameLength;
	union {
		USHORT Flags;
		struct {
			USHORT MountPoint : 1;
		};
	};
	USHORT DeviceNameLength;
	USHORT Zero;
	struct _SYMLINK_ECP_CONTEXT *Reparsed;
	UNICODE_STRING Name;
} SYMLINK_ECP_CONTEXT;

static BOOLEAN
zfs_revert_reparsed_case(PIRP Irp)
{
	PECP_LIST EcpList;
	SYMLINK_ECP_CONTEXT *EcpContext;
	PUNICODE_STRING FileName;
	USHORT UnparsedNameLength, FileNameLength, NameLength;
	UNICODE_STRING us1, us2;

	if (!NT_SUCCESS(FsRtlGetEcpListFromIrp(Irp, &EcpList)) ||
	    EcpList == NULL)
		return (FALSE);

	if (!NT_SUCCESS(FsRtlFindExtraCreateParameter(EcpList,
	    &IopSymlinkECPGuid, (void **)&EcpContext, NULL)))
		return (FALSE);

	if (FsRtlIsEcpFromUserMode(EcpContext) || !EcpContext->MountPoint)
		return (FALSE);

	UnparsedNameLength = EcpContext->UnparsedNameLength;
	if (UnparsedNameLength == 0)
		return (FALSE);

	FileName = &IoGetCurrentIrpStackLocation(Irp)->FileObject->FileName;
	FileNameLength = FileName->Length;
	NameLength = EcpContext->Name.Length;

	if (UnparsedNameLength > NameLength ||
	    UnparsedNameLength > FileNameLength)
		return (FALSE);

	us1.Length = us1.MaximumLength = UnparsedNameLength;
	us1.Buffer = (PWSTR)RtlOffsetToPointer(FileName->Buffer,
	    FileNameLength - UnparsedNameLength);

	us2.Length = us2.MaximumLength = UnparsedNameLength;
	us2.Buffer = (PWSTR)RtlOffsetToPointer(EcpContext->Name.Buffer,
	    NameLength - UnparsedNameLength);

	if (!RtlEqualUnicodeString(&us1, &us2, TRUE))
		return (FALSE);

	memcpy(us1.Buffer, us2.Buffer, UnparsedNameLength);
	return (TRUE);
}

/*
 * This is the main FileSystem IOCTL handler. This is where the filesystem
 * vnops happen and we handle everything with files and directories in ZFS.
 */
_Function_class_(DRIVER_DISPATCH)
    NTSTATUS
    fsDispatcher(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP *PIrp,
    PIO_STACK_LOCATION IrpSp)
{
	NTSTATUS Status = STATUS_INVALID_DEVICE_REQUEST;
	struct vnode *hold_vp = NULL;
	PIRP Irp = *PIrp;
	ULONG len = 0;

	mount_t *zmo = DeviceObject->DeviceExtension;
	VERIFY(zmo->type == MOUNT_TYPE_VCB || zmo->type == MOUNT_TYPE_VSS);

	dprintf("  %s: enter: major %d: minor %d: %s fsDO fo %p Vpb %lu\n",
	    __func__, IrpSp->MajorFunction, IrpSp->MinorFunction,
	    major2str(IrpSp->MajorFunction, IrpSp->MinorFunction),
	    IrpSp->FileObject,
	    DeviceObject->Vpb ? DeviceObject->Vpb->ReferenceCount : -1);

#ifdef DEBUG_IOCOUNT
	int skiplock = 0;
/*
 * Watch out for re-entrant calls! MJ_READ, can call CCMGR, which calls
 * MJ_READ!
 * This could be a bigger issue, in that any mutex calls in the
 * zfs_read()/zfs_write stack could die - including rangelocks,
 * rwlocks. Investigate. (+zfs_freesp/zfs_trunc)
 */
	if (mutex_owned(&GIANT_SERIAL_LOCK))
		skiplock = 1;
	else
		mutex_enter(&GIANT_SERIAL_LOCK);

	zfsvfs_t *zfsvfs = NULL;
#endif

/*
 * Like VFS layer in upstream, we hold the "vp" here before calling into
 * the VNOP handlers.
 * There is one special case, IRP_MJ_CREATE / zfs_vnop_lookup, which has
 * no vp to start,
 * and assigns the vp on success (held).
 * We also pass "hold_vp" down to delete_entry, so it can release the
 * last hold to delete
 */

	if (IrpSp->FileObject && IrpSp->FileObject->FsContext) {
		hold_vp = IrpSp->FileObject->FsContext;
		if (VN_HOLD(hold_vp) != 0) {

			// If we were given a vp, but can't hold the vp,
			// we should fail this OP.
			Irp->IoStatus.Information = 0;
			hold_vp = NULL;
			// return (STATUS_INVALID_PARAMETER);

		} else {

			// Add FO to vp, if this is the first we've heard of it.
			// Except if it is IRP_MJ_CLOSE, since they are removed
			// in CLEANUP, and CLOSE comes after.
			if (IrpSp->MajorFunction != IRP_MJ_CLOSE &&
			    vnode_fileobject_member(
			    IrpSp->FileObject->FsContext,
			    IrpSp->FileObject) == 0) {
				dprintf("Adding fo %p to vp %p\n",
				    IrpSp->FileObject,
				    IrpSp->FileObject->FsContext);

				vnode_fileobject_add(
				    IrpSp->FileObject->FsContext,
				    IrpSp->FileObject);
			}

			// This is useful if you have iocount leaks, and do
			// only single-threaded operations
#ifdef DEBUG_IOCOUNT
			if (!vnode_isvroot(hold_vp) && vnode_isdir(hold_vp))
				ASSERT(hold_vp->v_iocount == 1);
			zfsvfs = VTOZ(hold_vp)->z_zfsvfs;
#endif
		}
	}
/*
 * Inside VNOP handlers, we no longer need to call VN_HOLD() on *this* vp
 * (but might for dvp etc) and eventually that code will be removed, if this
 * style works out.
 */

	switch (IrpSp->MajorFunction) {

	case IRP_MJ_CREATE:
		/*
		 * Undo NTFS's case-mangling of the unparsed tail when this
		 * CREATE was redirected to us via one of our own folder
		 * mount-point reparse points.  See zfs_revert_reparsed_case()
		 * above.  No-op for drive-letter mounts and ordinary opens.
		 */
		zfs_revert_reparsed_case(Irp);

		if (IrpSp->Parameters.Create.Options & FILE_OPEN_BY_FILE_ID) {
			dprintf("IRP_MJ_CREATE: FileObject %p related %p "
			    "FileID 0x%llx flags 0x%x sharing 0x%x options "
			    "0x%lx\n",
			    IrpSp->FileObject,
			    IrpSp->FileObject ?
			    IrpSp->FileObject->RelatedFileObject :
			    NULL,
			    *((uint64_t *)IrpSp->FileObject->FileName.Buffer),
			    IrpSp->Flags, IrpSp->Parameters.Create.ShareAccess,
			    IrpSp->Parameters.Create.Options);
		} else {
			dprintf("IRP_MJ_CREATE: FileObject %p related %p "
			    "name '%wZ' flags 0x%x sharing 0x%x options "
			    "%s attr 0x%x DesAcc 0x%lx\n",
			    IrpSp->FileObject,
			    IrpSp->FileObject ?
			    IrpSp->FileObject->RelatedFileObject :
			    NULL,
			    &IrpSp->FileObject->FileName, IrpSp->Flags,
			    IrpSp->Parameters.Create.ShareAccess,
			    create_options(IrpSp->Parameters.Create.Options),
			    IrpSp->Parameters.Create.FileAttributes,
			    IrpSp->Parameters.Create.SecurityContext->
			    DesiredAccess);
		}
		Irp->IoStatus.Information = FILE_OPENED;
		Status = STATUS_SUCCESS;

#if 0
		// Disallow autorun.inf for now
		if (IrpSp && IrpSp->FileObject &&
		    IrpSp->FileObject->FileName.Buffer &&
		    _wcsicmp(IrpSp->FileObject->FileName.Buffer,
		    L"\\autorun.inf") == 0) {
			Irp->IoStatus.Information = FILE_DOES_NOT_EXIST;
			Status = STATUS_OBJECT_NAME_NOT_FOUND;
			break;
		}
#endif

		//
		//  Check if we are opening the volume and not a file/directory.
		//  We are opening the volume if the name is empty and there
		//  isn't a related file object.  If there is a related
		// file object then it is the Vcb itself.
		//

		// We have a name, so we are looking for something specific
		// Attempt to find the requested object
		if (IrpSp && IrpSp->FileObject &&
		    /* IrpSp->FileObject->FileName.Buffer && */
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
		Status = zfs_fileobject_cleanup(DeviceObject, Irp, IrpSp,
		    &hold_vp);
		break;

	case IRP_MJ_CLOSE:
		Status = zfs_fileobject_close(DeviceObject, Irp, IrpSp,
		    &hold_vp);
		break;

	case IRP_MJ_DEVICE_CONTROL:
	{
		ulong_t cmd = IrpSp->Parameters.DeviceIoControl.IoControlCode;
		/* Not ZFS ioctl, handle Windows ones */
		switch (cmd) {

		case IOCTL_MOUNTDEV_QUERY_STABLE_GUID:
			dprintf("IOCTL_MOUNTDEV_QUERY_STABLE_GUID\n");
			Status = ioctl_query_stable_guid(DeviceObject, Irp,
			    IrpSp);
			break;
		case IOCTL_DISK_IS_WRITABLE:
			dprintf("IOCTL_DISK_IS_WRITABLE\n");
			Status = STATUS_SUCCESS;
			break;
		case IOCTL_MOUNTDEV_QUERY_DEVICE_NAME:
			/*
			 * twext.dll (Previous Versions shell extension) opens
			 * an ordinary file handle on the volume and issues this
			 * IOCTL.  NTFS returns STATUS_INVALID_PARAMETER for
			 * IOCTL_MOUNTDEV_QUERY_DEVICE_NAME when it arrives on a
			 * file object (only the disk device object answers it).
			 * Returning SUCCESS here caused twext.dll to take a
			 * different code path that never fires the SMB loopback
			 * needed to enumerate snapshots.  Match NTFS behaviour.
			 */
			dprintf("IOCTL_MOUNTDEV_QUERY_DEVICE_NAME (fs): "
			    "returning INVALID_PARAMETER (like NTFS)\n");
			Status = STATUS_INVALID_PARAMETER;
			break;
		case IOCTL_VOLUME_GET_GPT_ATTRIBUTES:
			dprintf("IOCTL_VOLUME_GET_GPT_ATTRIBUTES\n");
			Status = ioctl_get_gpt_attributes(DeviceObject, Irp,
			    IrpSp);
			break;
		case IOCTL_MOUNTDEV_QUERY_UNIQUE_ID:
			dprintf("IOCTL_MOUNTDEV_QUERY_UNIQUE_ID\n");
			Status = ioctl_query_unique_id(DeviceObject, Irp,
			    IrpSp);
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
		case IOCTL_DISK_CHECK_VERIFY:
		case IOCTL_STORAGE_CHECK_VERIFY:
			dprintf("IOCTL_STORAGE_CHECK_VERIFY\n");
			Status = STATUS_SUCCESS;
			break;
		case IOCTL_DISK_GET_DRIVE_GEOMETRY:
			dprintf("IOCTL_DISK_GET_DRIVE_GEOMETRY\n");
			Status = ioctl_disk_get_drive_geometry(DeviceObject,
			    Irp, IrpSp);
			break;
		case IOCTL_DISK_GET_DRIVE_GEOMETRY_EX:
			dprintf("IOCTL_DISK_GET_DRIVE_GEOMETRY_EX\n");
			Status = ioctl_disk_get_drive_geometry_ex(DeviceObject,
			    Irp, IrpSp);
			break;
		case IOCTL_DISK_GET_PARTITION_INFO:
			dprintf("IOCTL_DISK_GET_PARTITION_INFO\n");
			Status = ioctl_disk_get_partition_info(DeviceObject,
			    Irp, IrpSp);
			break;
		case IOCTL_DISK_GET_PARTITION_INFO_EX:
			dprintf("IOCTL_DISK_GET_PARTITION_INFO_EX\n");
			Status = ioctl_disk_get_partition_info_ex(DeviceObject,
			    Irp, IrpSp);
			break;
		case IOCTL_VOLUME_IS_IO_CAPABLE:
			dprintf("IOCTL_VOLUME_IS_IO_CAPABLE\n");
			Status = ioctl_volume_is_io_capable(DeviceObject, Irp,
			    IrpSp);
			break;
		case IOCTL_STORAGE_GET_HOTPLUG_INFO:
			dprintf("IOCTL_STORAGE_GET_HOTPLUG_INFO\n");
			Status = ioctl_storage_get_hotplug_info(DeviceObject,
			    Irp, IrpSp);
			break;
		case IOCTL_DISK_GET_LENGTH_INFO:
			dprintf("IOCTL_DISK_GET_LENGTH_INFO\n");
			Status = ioctl_disk_get_length_info(DeviceObject, Irp,
			    IrpSp);
			break;
		case IOCTL_STORAGE_GET_DEVICE_NUMBER:
			dprintf("IOCTL_STORAGE_GET_DEVICE_NUMBER\n");
			Status = ioctl_storage_get_device_number(DeviceObject,
			    Irp, IrpSp);
			break;
		case IOCTL_STORAGE_QUERY_PROPERTY:
			dprintf("IOCTL_STORAGE_QUERY_PROPERTY\n");
			Status = ioctl_storage_query_property(DeviceObject, Irp,
			    IrpSp);
			break;
		case 0x002D148C: /* unknown storage IOCTL (fn 0x523) */
			dprintf("unknown storage IOCTL 0x002D148C"
			    " -> NOT_SUPPORTED\n");
			Irp->IoStatus.Information = 0;
			Status = STATUS_NOT_SUPPORTED;
			break;
		case 0x002D281C: /* unknown storage IOCTL (fn 0xA07) */
			dprintf("unknown storage IOCTL 0x002D281C"
			    " -> NOT_SUPPORTED\n");
			Irp->IoStatus.Information = 0;
			Status = STATUS_NOT_SUPPORTED;
			break;
		case IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS: // VSS
			dprintf("IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS\n");
			Status = ioctl_volume_get_volume_disk_extents(
			    DeviceObject, Irp, IrpSp);
			break;
		case FSCTL_DISMOUNT_VOLUME:
			dprintf("FSCTL_DISMOUNT_VOLUME\n");
			Status = STATUS_SUCCESS;
			break;
		case FSCTL_LOCK_VOLUME:
			dprintf("FSCTL_LOCK_VOLUME\n");
			Status = STATUS_SUCCESS;
			break;
		case IOCTL_MOUNTDEV_LINK_DELETED:
			dprintf("IOCTL_MOUNTDEV_LINK_DELETED\n");
			Status = STATUS_SUCCESS;
			break;
		case 0x4d0014:
// Same as IOCTL_MOUNTDEV_LINK_DELETED but bit 14,15 are 0 (access permissions)
			dprintf("IOCTL_MOUNTDEV_LINK_DELETED v2\n");
			Status = STATUS_SUCCESS;
			break;

		case IOCTL_VOLSNAP_FLUSH_AND_HOLD_WRITES:
		case 0x0053C004: /* IOCTL_VOLSNAP_RELEASE_WRITES */
			/*
			 * ZFS is copy-on-write; no write freeze or release
			 * is needed.
			 */
			dprintf("IOCTL_VOLSNAP_FLUSH/HOLD/RELEASE: no-op\n");
			Status = STATUS_SUCCESS;
			break;

		case 0x530018: /* IOCTL_VOLSNAP_QUERY_NAMES_OF_SNAPSHOTS */
		case 0x534014: /* IOCTL_VOLSNAP_QUERY_NAMES_OF_SNAPSHOTS */
			dprintf("IOCTL_VOLSNAP_QUERY_NAMES_OF_SNAPSHOTS\n");
			Status = zfs_volsnap_query_names_of_snapshots(
			    DeviceObject, Irp, IrpSp);
			break;

		case 0x534058: /* IOCTL_VOLSNAP_QUERY_DIFF_AREA_MINIMUM_SIZE */
			/*
			 * Returning STATUS_SUCCESS here told the System
			 * Provider this volume can host a diff-area,
			 * causing it to probe ZFS volumes as candidates.
			 * The "add volume to diff-area" IOCTL sequence
			 * returned output sizes we did not match,
			 * causing IOCTL Unpack overflow.  ZFS uses CoW;
			 * return NOT_SUPPORTED so the System Provider
			 * skips ZFS and picks an NTFS diff-area volume.
			 */
			dprintf("IOCTL_VOLSNAP_QUERY_DIFF_AREA_MINIMUM_SIZE"
			    " -> NOT_SUPPORTED (volume)\n");
			Irp->IoStatus.Information = 0;
			Status = STATUS_NOT_SUPPORTED;
			break;

		case 0x53406C: /* IOCTL_VOLSNAP_QUERY_DIFF_AREA_INFORMATION */
			/*
			 * As with QUERY_DIFF_AREA_MINIMUM_SIZE: ZFS
			 * cannot host a diff-area; decline rather than
			 * returning partial data that would overflow.
			 */
			dprintf("IOCTL_VOLSNAP_QUERY_DIFF_AREA_INFORMATION"
			    " -> NOT_SUPPORTED (volume)\n");
			Irp->IoStatus.Information = 0;
			Status = STATUS_NOT_SUPPORTED;
			break;

		case 0x530024: /* IOCTL_VOLSNAP_QUERY_DIFF_AREA */
			/*
			 * ZFS uses CoW native snapshots, not a diff-area.
			 * Return STATUS_NOT_SUPPORTED so ichannel does not
			 * attempt to Unpack VOLSNAP_DIFF_AREA_TABLE from the
			 * response (which caused "IOCTL Unpack overflow" when
			 * we returned only sizeof(ULONG) bytes).
			 */
			dprintf("IOCTL_VOLSNAP_QUERY_DIFF_AREA"
			    " -> NOT_SUPPORTED\n");
			Irp->IoStatus.Information = 0;
			Status = STATUS_NOT_SUPPORTED;
			break;

		case 0x534070: /* IOCTL_VOLSNAP_QUERY_APPLICATION_FLAGS */
		{
			/*
			 * Returns ApplicationInformation (68 bytes):
			 *   ULONG  length, GUID×3, ULONG×3, LONG attributes,
			 *   ULONG  unknown2.
			 * All zeros = no VSS application has set flags.
			 */
			const ULONG appsz = 4 + 16 + 16 + 16 + 4 + 4 + 4 + 4;
			dprintf("IOCTL_VOLSNAP_QUERY_APPLICATION_FLAGS"
			    " (volume) -> 68 zero bytes\n");
			if (IrpSp->Parameters.DeviceIoControl.OutputBufferLength
			    < appsz) {
				Irp->IoStatus.Information = appsz;
				Status = STATUS_BUFFER_TOO_SMALL;
			} else {
				RtlZeroMemory(
				    Irp->AssociatedIrp.SystemBuffer, appsz);
				Irp->IoStatus.Information = appsz;
				Status = STATUS_SUCCESS;
			}
			break;
		}

		case 0x530050: /* IOCTL_VOLSNAP QUERY_EPIC - snapshot count */
		{
			/*
			 * srv2.sys reads 4 bytes from EPIC and
			 * compares with a cached share-object value.
			 * It calls QUERY_NAMES_OF_SNAPSHOTS only when
			 * the value changes (i.e. snapshot set changed).
			 * Return the ZFS snapshot count so srv2.sys
			 * detects new snapshots on the first call
			 * (cache starts at 0) and triggers a full
			 * QUERY_NAMES refresh.
			 */
			ULONG epic_nsnaps = 0;
			mount_t *epic_zmo = DeviceObject->DeviceExtension;
			zfsvfs_t *epic_zfsvfs = vfs_fsprivate(epic_zmo);
			if (epic_zfsvfs != NULL && epic_zfsvfs->z_os != NULL) {
				dsl_pool_t *epic_dp =
				    dmu_objset_pool(epic_zfsvfs->z_os);
				uint64_t epic_pos = 0;
				char epic_sn[MAXNAMELEN];
				uint64_t epic_id;
				boolean_t epic_cc;
				dsl_pool_config_enter(epic_dp, FTAG);
				while (dmu_snapshot_list_next(epic_zfsvfs->z_os,
				    sizeof (epic_sn), epic_sn,
				    &epic_id, &epic_pos, &epic_cc) == 0)
					epic_nsnaps++;
				dsl_pool_config_exit(epic_dp, FTAG);
			}
			if (IrpSp->Parameters.DeviceIoControl.
			    OutputBufferLength >= sizeof (ULONG)) {
				*(ULONG *)Irp->AssociatedIrp.SystemBuffer =
				    epic_nsnaps;
				Irp->IoStatus.Information = sizeof (ULONG);
			} else {
				Irp->IoStatus.Information = 0;
			}
			dprintf("IOCTL_VOLSNAP_EPIC: %lu snap(s)\n",
			    epic_nsnaps);
			Status = STATUS_SUCCESS;
			break;
		}
		case 0x53001C: /* IOCTL_VOLSNAP function 0x7 - no output */
			dprintf("IOCTL_VOLSNAP 0x53001C no-op (volume)\n");
			Irp->IoStatus.Information = 0;
			Status = STATUS_SUCCESS;
			break;

		default:
			/*
			 * For VOLSNAP IOCTLs (device type 0x53) that we
			 * do not handle, return STATUS_NOT_SUPPORTED
			 * explicitly.  Do NOT let these fall through to
			 * the pass-down path: if the lower device
			 * returns STATUS_SUCCESS with 0 bytes, the VSS
			 * ichannel will Unpack a fixed-size struct from
			 * zero bytes ("IOCTL Unpack overflow").
			 */
			if ((cmd >> 16) == 0x53) {
				dprintf("**** unhandled VOLSNAP IOCTL: 0x%lx"
				    " - returning NOT_SUPPORTED\n", cmd);
				Irp->IoStatus.Information = 0;
				Status = STATUS_NOT_SUPPORTED;
			} else {
				dprintf("**** unknown fsWindows IOCTL: 0x%lx\n",
				    cmd);
			}
			break;
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
			Status = user_fs_request(DeviceObject, PIrp, IrpSp);
			break;
		case IRP_MN_VERIFY_VOLUME:
			Status = STATUS_SUCCESS;
			break;
			// FSCTL_QUERY_VOLUME_CONTAINER_STATE 0x90930
		case IRP_MN_KERNEL_CALL:
			dprintf("IRP_MN_KERNEL_CALL: FsControlCode 0x%lx\n",
			    IrpSp->Parameters.FileSystemControl.FsControlCode);
			Status = user_fs_request(DeviceObject, PIrp, IrpSp);
			break;
		default:
			dprintf("IRP_MJ_FILE_SYSTEM_CONTROL: unknown 0x%x\n",
			    IrpSp->MinorFunction);
			Status = STATUS_INVALID_DEVICE_REQUEST;
		}
		break;
	case IRP_MJ_PNP:
		switch (IrpSp->MinorFunction) {
		case IRP_MN_QUERY_DEVICE_RELATIONS:
			Status = QueryDeviceRelations(DeviceObject, PIrp,
			    IrpSp);
			dprintf("DeviceRelations.Type 0x%x\n",
			    IrpSp->Parameters.QueryDeviceRelations.Type);
			break;
		case IRP_MN_QUERY_ID:
			Status = pnp_query_id(DeviceObject, Irp, IrpSp);
			break;
		// These are not implemented in btrfs, so pass down.
		case IRP_MN_QUERY_INTERFACE:
			dprintf("IRP_MN_QUERY_DEVICE_TEXT\n");
			break;
		case IRP_MN_QUERY_DEVICE_TEXT:
			dprintf("IRP_MN_QUERY_DEVICE_TEXT\n");
			break;
		case IRP_MN_QUERY_BUS_INFORMATION:
			dprintf("IRP_MN_QUERY_BUS_INFORMATION\n");
			break;
		case IRP_MN_QUERY_RESOURCE_REQUIREMENTS:
			dprintf("IRP_MN_QUERY_RESOURCE_REQUIREMENTS\n");
			break;
		case IRP_MN_QUERY_RESOURCES:
			dprintf("IRP_MN_QUERY_RESOURCES\n");
			break;
		case IRP_MN_QUERY_CAPABILITIES:
			dprintf("IRP_MN_QUERY_CAPABILITIES\n");
			break;
		case IRP_MN_DEVICE_ENUMERATED:
			dprintf("IRP_MN_DEVICE_ENUMERATED\n");
			Status = STATUS_SUCCESS;
			break;
#if 0
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
		case IRP_MN_CANCEL_REMOVE_DEVICE:
			dprintf("IRP_MN_CANCEL_REMOVE_DEVICE\n");
			Status = STATUS_SUCCESS;
			break;
		case IRP_MN_DEVICE_USAGE_NOTIFICATION:
			dprintf("IRP_MN_DEVICE_USAGE_NOTIFICATION\n");
			Status = pnp_device_usage_notification(DeviceObject,
			    Irp, IrpSp);
			break;
#endif
		default:
			dprintf("Unknown IRP_MJ_PNP(fs): 0x%x\n",
			    IrpSp->MinorFunction);
			break;
		}
		break;
#if 1
	case IRP_MJ_QUERY_VOLUME_INFORMATION:
		Status = query_volume_information(DeviceObject, Irp, IrpSp);
		break;
#endif
	case IRP_MJ_LOCK_CONTROL:
		Status = lock_control(DeviceObject, PIrp, IrpSp);
		break;

	case IRP_MJ_QUERY_INFORMATION:
		Status = query_information(DeviceObject, Irp, IrpSp);
		break;

	case IRP_MJ_DIRECTORY_CONTROL:
		switch (IrpSp->MinorFunction) {
		case IRP_MN_NOTIFY_CHANGE_DIRECTORY:
			Status = notify_change_directory(DeviceObject, Irp,
			    IrpSp);
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
		len = IrpSp->Parameters.Read.Length;
		Status = fs_read(DeviceObject, Irp, IrpSp);
		if (Status == STATUS_PENDING) {
			IoMarkIrpPending(Irp);
			if (add_thread_job(DeviceObject, Irp,
			    len, IoWriteAccess))
				*PIrp = NULL;
			else
				Status = do_read_job(DeviceObject, Irp);
		}
		break;
	case IRP_MJ_WRITE:
		len = IrpSp->Parameters.Write.Length;
		Status = fs_write(DeviceObject, Irp, IrpSp);
		if (Status == STATUS_PENDING) {
			IoMarkIrpPending(Irp);
			if (add_thread_job(DeviceObject, Irp,
			    len, IoReadAccess))
				*PIrp = NULL;
			else
				Status = do_write_job(DeviceObject, Irp);
		}
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
	default:
		dprintf("**** unknown fsWindows IOCTL: 0x%hhx\n",
		    IrpSp->MajorFunction);
		break;
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
		if (zfsvfs)
			taskq_wait(dsl_pool_zrele_taskq(dmu_objset_pool(
			    zfsvfs->z_os)));
		vnode_check_iocount();
		mutex_exit(&GIANT_SERIAL_LOCK);
	}
#endif

	// If fsDispatcher() dont handle it, send it to diskDispatcher
	if (Status == STATUS_INVALID_DEVICE_REQUEST &&
	    zmo->AttachedDevice && *PIrp != NULL) {
		dprintf("%s Passing down\n", __func__);
		*PIrp = NULL;
		IoSkipCurrentIrpStackLocation(Irp);
		return (IoCallDriver(zmo->AttachedDevice, Irp));
	}

	return (Status);
}

void
call_dispatcher(_In_ PVOID Context)
{
	PIRP Irp = (PIRP) Context;
	PIO_STACK_LOCATION IrpSp = IoGetCurrentIrpStackLocation(Irp);

	dispatcher(IrpSp->DeviceObject, Irp);
}

/*
 * ALL ioctl requests come in here, and we do the Windows specific
 * work to handle IRPs then we sort out the type of request
 * (ioctl, volume, filesystem) and call each respective handler.
 * Update
 * Our fsDispatcher() (VDO) can get a request it would normally not
 * handle, so it should pass it down using
 * IoCallDriver(DeviceObject->AttachedDevice, Irp);
 * This is probably our diskDispatcher() (PDO), but that is the
 * standard Windows driver model.
 * diskDispatcher() should also do the same.
 * We use the ntstatus STATUS_INVALID_DEVICE_REQUEST to indicate
 * we have not handled the request at this level.
 */
_Function_class_(DRIVER_DISPATCH)
    NTSTATUS
    dispatcher(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
	BOOLEAN TopLevel = FALSE;
	BOOLEAN AtIrqlPassiveLevel = FALSE;
	PIO_STACK_LOCATION IrpSp;
	NTSTATUS Status = STATUS_NOT_IMPLEMENTED;
	mount_t *zmo = NULL;

	// dprintf("%s: enter\n", __func__);

	//  If we were called with our file system device object instead of a
	//  volume device object, just complete this request with STATUS_SUCCESS
#if 0
	if (vnop_deviceObject == VolumeDeviceObject) {
		dprintf("%s: own object\n", __func__);
		Irp->IoStatus.Status = STATUS_SUCCESS;
		Irp->IoStatus.Information = FILE_OPENED;
		IoCompleteRequest(Irp, IO_DISK_INCREMENT);
		return (STATUS_SUCCESS);
	}
#endif
	IrpSp = IoGetCurrentIrpStackLocation(Irp);

	dprintf("%s: enter: major %d: minor %d: %s: type 0x%x: fo %p\n",
	    __func__, IrpSp->MajorFunction, IrpSp->MinorFunction,
	    major2str(IrpSp->MajorFunction, IrpSp->MinorFunction),
	    Irp->Type, IrpSp->FileObject);


	/*
	 * Mount can re-enter and cause hassles, so let's detect
	 * that here, and spawn off a WorkItem instead.
	 */
	if (zfs_mount_reentry &&
	    tsd_get(zfs_mount_reentry_tsd) == (void *)1) {
		// dprintf("Re-entry detected\n");
		// xprintf("Re-entry detected\n");

		IoMarkIrpPending(Irp);
		if (taskq_dispatch(system_taskq, call_dispatcher, Irp,
		    TQ_SLEEP))
			return (STATUS_PENDING);
	}

	KIRQL saveIRQL;
	saveIRQL = KeGetCurrentIrql();

	ZFS_DRIVER_EXTENSION(WIN_DriverObject, DriverExtension);

	AtIrqlPassiveLevel = (KeGetCurrentIrql() == PASSIVE_LEVEL);
	if (AtIrqlPassiveLevel) {
		FsRtlEnterFileSystem();
	}
	if (IoGetTopLevelIrp() == NULL) {
		IoSetTopLevelIrp(Irp);
		TopLevel = TRUE;
	}

	if (DeviceObject == DriverExtension->ioctlDeviceObject)
		Status = ioctlDispatcher(DeviceObject, &Irp, IrpSp);
	else {
		zmo = DeviceObject->DeviceExtension;

		if (zmo && zmo->type == MOUNT_TYPE_BUS)
			Status = busDispatcher(DeviceObject, &Irp, IrpSp);
		else if (zmo && zmo->type == MOUNT_TYPE_DGL)
			Status = busDispatcher(DeviceObject, &Irp, IrpSp);
		else if (zmo && zmo->type == MOUNT_TYPE_DCB)
			Status = diskDispatcher(DeviceObject, &Irp, IrpSp);
		else if (zmo && zmo->type == MOUNT_TYPE_VCB)
			Status = fsDispatcher(DeviceObject, &Irp, IrpSp);
		else if (zmo && zmo->type == MOUNT_TYPE_VSS)
			Status = zfs_vss_dispatcher(DeviceObject, &Irp, IrpSp);
		else {
			Status = STATUS_INVALID_DEVICE_REQUEST;
			Irp->IoStatus.Information = 0;
			zmo = NULL;
		}
	}

	if (AtIrqlPassiveLevel) {
		FsRtlExitFileSystem();
	}
	if (TopLevel) {
		IoSetTopLevelIrp(NULL);
	}

	switch (Status) {
	case STATUS_INVALID_DEVICE_REQUEST:
		break;
	case STATUS_SUCCESS:
	case STATUS_BUFFER_OVERFLOW:
		break;
	case STATUS_PENDING:
		break;
	default:
		if (Irp != NULL) {
			dprintf("%s: exit: 0x%lx %s Information 0x%llx : %s\n",
			    __func__, Status,
			    common_status_str(Status),
			    Irp ? Irp->IoStatus.Information : 0,
			    major2str(IrpSp->MajorFunction,
			    IrpSp->MinorFunction));
		}
	}

	// Complete the request if it isn't pending (ie, we
	// called zfsdev_async())
#if 0
	if ((Status == STATUS_INVALID_DEVICE_REQUEST) && Irp &&
	    zmo != NULL &&
	    zmo->AttachedDevice != NULL &&
	    DriverExtension->ioctlDeviceObject) {
		dprintf("Passing request %s down\n",
		    major2str(IrpSp->MajorFunction, IrpSp->MinorFunction));

		IoSkipCurrentIrpStackLocation(Irp);
		Status = IoCallDriver(zmo->AttachedDevice, Irp);
		dprintf("Lower Device said 0x%0x %s\n", Status,
		    common_status_str(Status));

	} else
#endif

	if ((Status == STATUS_INVALID_DEVICE_REQUEST) && Irp &&
	    zmo != NULL &&
	    DriverExtension->LowerDeviceObject != NULL &&
	    DriverExtension->ioctlDeviceObject) {
#if 1
		dprintf("Passing request %s down bus\n",
		    major2str(IrpSp->MajorFunction, IrpSp->MinorFunction));

		PDEVICE_OBJECT Attached;
		Attached = DriverExtension->LowerDeviceObject;
		while (Attached->DriverObject == WIN_DriverObject)
			Attached = Attached->AttachedDevice;
		if (Attached) {
			IoSkipCurrentIrpStackLocation(Irp);
			Status = IoCallDriver(
			    DriverExtension->LowerDeviceObject,
			    Irp);
			dprintf("Lower Bus Device said 0x%0lx %s\n", Status,
			    common_status_str(Status));
		}
#endif
	} else if ((Status == STATUS_INVALID_DEVICE_REQUEST) && Irp &&
	    zmo != NULL && // vcb->parent_device = dcb
	    zmo->parent_device != NULL &&
	    DriverExtension->ioctlDeviceObject) {
		dprintf("Passing request %s down bus\n",
		    major2str(IrpSp->MajorFunction, IrpSp->MinorFunction));

		zmo = zmo->parent_device; // We are now dcb
		Status = diskDispatcher(zmo->FunctionalDeviceObject, &Irp,
		    IrpSp);
		dprintf("Direct DDCB said 0x%0lx %s\n", Status,
		    common_status_str(Status));

	} else if (Status != STATUS_PENDING && Irp != NULL) {
		// IOCTL_STORAGE_GET_HOTPLUG_INFO
		// IOCTL_DISK_CHECK_VERIFY
		// IOCTL_STORAGE_QUERY_PROPERTY

		Irp->IoStatus.Status = Status;

		IoCompleteRequest(Irp,
		    Status == STATUS_SUCCESS ? IO_DISK_INCREMENT :
		    IO_NO_INCREMENT);

	} else if (Status == STATUS_PENDING && Irp == NULL) {
		// If Irp is NULL, we are not to IoComplete the IRP
		// as we are to wait, see FSCTL_REQUEST_OPLOCK.

	} else {
		// DbgBreakPoint();
	}

	VERIFY3U(saveIRQL, ==, KeGetCurrentIrql());

	return (Status);
}

NTSTATUS
ZFSCallbackAcquireForCreateSection(
	IN PFS_FILTER_CALLBACK_DATA CallbackData,
	OUT PVOID *CompletionContext)
{
	ASSERT(CallbackData->SizeOfFsFilterCallbackData ==
	    sizeof (FS_FILTER_CALLBACK_DATA));

	dprintf("%s: Operation 0x%x \n", __func__,
	    CallbackData->Operation);

	struct vnode *vp;
	vp = CallbackData->FileObject->FsContext;

	ASSERT(vp != NULL);
	if (vp == NULL)
		return (STATUS_INVALID_PARAMETER);

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
		return (STATUS_INVALID_PARAMETER);
	}
#ifdef DEBUG_IOCOUNT
	if (!nolock)
		mutex_exit(&GIANT_SERIAL_LOCK);
#endif

	if (CallbackData->
	    Parameters.AcquireForSectionSynchronization.SyncType !=
	    SyncTypeCreateSection) {
		return (STATUS_FSFILTER_OP_COMPLETED_SUCCESSFULLY);
	} else if (vp->share_access.Writers == 0) {
		return (STATUS_FILE_LOCKED_WITH_ONLY_READERS);
	} else {
		return (STATUS_FILE_LOCKED_WITH_WRITERS);
	}

}

NTSTATUS
ZFSCallbackReleaseForCreateSection(
	IN PFS_FILTER_CALLBACK_DATA CallbackData,
	OUT PVOID *CompletionContext)
{
	struct vnode *vp;
	vp = CallbackData->FileObject->FsContext;

	dprintf("%s: vp %p\n", __func__, vp);

	ASSERT(vp != NULL);
	if (vp == NULL)
		return (STATUS_INVALID_PARAMETER);

	dprintf("%s: unlocked: %p\n",
	    __func__, vp->FileHeader.Resource);
	if (VN_HOLD(vp) == 0) {
		ExReleaseResourceLite(vp->FileHeader.Resource);
		vnode_rele(vp);
		VN_RELE(vp);
		return (STATUS_FSFILTER_OP_COMPLETED_SUCCESSFULLY);
	}

	dprintf("%s WARNING FAILED\n", __func__);
	return (STATUS_FSFILTER_OP_COMPLETED_SUCCESSFULLY);
}

void
zfs_windows_vnops_callback(PDEVICE_OBJECT deviceObject)
{

}

int
zfs_vfsops_init(void)
{

#ifdef DEBUG_IOCOUNT
	mutex_init(&GIANT_SERIAL_LOCK, NULL, MUTEX_DEFAULT, NULL);
#endif
	return (0);
}

int
zfs_vfsops_fini(void)
{

#ifdef DEBUG_IOCOUNT
	mutex_destroy(&GIANT_SERIAL_LOCK);
#endif
	return (0);
}

NTSTATUS
pnp_query_di(PDEVICE_OBJECT DeviceObject, PIRP Irp, PIO_STACK_LOCATION IrpSp)
{
#if 0
	NTSTATUS status;
	if (IsEqualGUID(IrpSp->Parameters.QueryInterface.InterfaceType,
	    &ZFSZVOLDI_GUID)) {
		if (IrpSp->Parameters.QueryInterface.Version < 1)
			status = STATUS_NOT_SUPPORTED;
		else if (IrpSp->Parameters.QueryInterface.Size <
		    sizeof (zfsdizvol_t))
			status = STATUS_BUFFER_TOO_SMALL;
		else if ((IrpSp->
		    Parameters.QueryInterface.InterfaceSpecificData ==
		    NULL) || strlen(IrpSp->
		    Parameters.QueryInterface.InterfaceSpecificData) <= 8)
			status = STATUS_INVALID_PARAMETER;
		else {
			PVOID zv; // zvol_state_t*, opaque here
			uint32_t openCount;
			extern PVOID zvol_name2zvolState(const char *name,
			    uint32_t *openCount);
			PCHAR vendorUniqueId = (PCHAR)IrpSp->
			    Parameters.QueryInterface.InterfaceSpecificData;
			zv = zvol_name2zvolState(&vendorUniqueId[8],
			    &openCount);
			dprintf("what is this pnp\n");
			// check that the minor number is non-zero: that
			// signifies the zvol has fully completed its
			// bringup phase.
			if (zv && openCount) {
				extern void IncZvolRef(PVOID Context);
				extern void DecZvolRef(PVOID Context);
				extern NTSTATUS ZvolDiRead(PVOID Context,
				    zfsiodesc_t *pIo);
				extern NTSTATUS ZvolDiWrite(PVOID Context,
				    zfsiodesc_t *pIo);
				// lock in an extra reference on the zvol
				IncZvolRef(zv);
				zfsdizvol_t *pDI = (zfsdizvol_t *)IrpSp->
				    Parameters.QueryInterface.Interface;
				pDI->header.Size = sizeof (zfsdizvol_t);
				pDI->header.Version = ZFSZVOLDI_VERSION;
				pDI->header.Context = zv;
				pDI->header.InterfaceReference = IncZvolRef;
				pDI->header.InterfaceDereference = DecZvolRef;
				pDI->Read = ZvolDiRead;
				pDI->Write = ZvolDiWrite;
				Irp->IoStatus.Information = 0;
				status = STATUS_SUCCESS;
			}
			else
				status = STATUS_NOT_FOUND;
		}
	} else
#endif

	// Make it a no-op.
	Irp->IoStatus.Status = STATUS_SUCCESS;
	Irp->IoStatus.Information = 0;

	return (STATUS_SUCCESS);
}

DEFINE_GUID(GUID_BUS_TYPE_PCI,
    0xc8ebdfb0, 0xb510, 0x11d0, 0x80, 0xe5, 0x00, 0xa0,
    0xc9, 0x25, 0x42, 0xe3);

NTSTATUS
pnp_query_bus_information(PDEVICE_OBJECT DeviceObject, PIRP Irp,
    PIO_STACK_LOCATION IrpSp)
{
	PPNP_BUS_INFORMATION busInfo;

	// Allocate memory for the bus information structure
	busInfo = (PPNP_BUS_INFORMATION)
	    ExAllocatePoolWithTag(PagedPool,
	    sizeof (PNP_BUS_INFORMATION),
	    'nIbQ');

	if (busInfo == NULL)
		return (STATUS_INSUFFICIENT_RESOURCES);

	// Fill in the bus information structure
	busInfo->BusTypeGuid = GUID_BUS_TYPE_PCI;  // Change as appropriate
	busInfo->LegacyBusType = PNPBus;
	busInfo->BusNumber = 0;  // Change as appropriate

	// Set the bus information in the IRP
	Irp->IoStatus.Information = (ULONG_PTR)busInfo;

	return (STATUS_SUCCESS);
}

NTSTATUS
pnp_device_usage_notification(PDEVICE_OBJECT DeviceObject, PIRP Irp,
    PIO_STACK_LOCATION IrpSp)
{
	char *type;

	switch (IrpSp->Parameters.UsageNotification.Type) {
	case DeviceUsageTypePaging:
		type = "Paging";
		break;
	case DeviceUsageTypeDumpFile:
		type = "DumpFile";
		break;
	case DeviceUsageTypeBoot:
		type = "Boot";
		break;
	case DeviceUsageTypeGuestAssigned:
		type = "GuestAssigned";
		break;
	case DeviceUsageTypeHibernation:
		type = "Hibernation";
		break;
	case DeviceUsageTypePostDisplay:
		type = "PostDisplay";
		break;
	default:
		type = "Unknown";
		break;
	}

	dprintf("NT wants to %s a %s file.\n",
	    IrpSp->Parameters.UsageNotification.InPath ?
	    "create" : "remove",
	    type);

	/*
	 * I believe we should in fact send the IRP lower down
	 * and that is true of a few IRPs.
	 */

	return (STATUS_SUCCESS);
}


/* FastIO support */


#ifdef ZFS_HAVE_FASTIO

static BOOLEAN __stdcall
fastio_check_if_possible(PFILE_OBJECT FileObject,
    PLARGE_INTEGER FileOffset, ULONG Length, BOOLEAN Wait,
    ULONG LockKey, BOOLEAN CheckForReadOperation, PIO_STATUS_BLOCK IoStatus,
    PDEVICE_OBJECT DeviceObject)
{
	vnode_t *vp = FileObject->FsContext;
	LARGE_INTEGER quadlen;

	quadlen.QuadPart = Length;

	if (CheckForReadOperation) {
		if (FsRtlFastCheckLockForRead(&vp->lock, FileOffset,
		    &quadlen, LockKey, FileObject, PsGetCurrentProcess()))
			return (TRUE);
	} else {
		/*
		 * FILE_WRITE_TO_END_OF_FILE (-1) is an unresolved append
		 * offset: only zfs_write_wrap's z_eof_pending CAS loop knows
		 * how to resolve it against concurrent appenders.  Treated
		 * as a literal byte offset, (uint64_t)-1 + Length wraps
		 * around to a small value that slips under FileHeader.
		 * FileSize, making an unresolved append look like a safe
		 * in-bounds write to the check below.  See fastio_write().
		 *
		 * Compare LowPart/HighPart, not QuadPart: FILE_WRITE_TO_END_
		 * OF_FILE is an untyped 0xffffffff, i.e. plain (32-bit)
		 * unsigned int in C.  QuadPart is a signed 64-bit LONGLONG,
		 * so QuadPart == FILE_WRITE_TO_END_OF_FILE zero-extends the
		 * 32-bit constant to 0x00000000ffffffff instead of sign-
		 * extending it to -1 -- silently never matching a genuine
		 * -1 offset.  Confirmed live: this exact comparison let a
		 * second MEMORY.DMP reproduce subtype 0x103087 right through
		 * this unfired check.
		 */
		if (FileOffset->LowPart == FILE_WRITE_TO_END_OF_FILE &&
		    FileOffset->HighPart == -1)
			return (FALSE);

		/*
		 * FsRtlCopyWrite may only copy into the range already
		 * registered with the Cache Manager via CcSetFileSizes.
		 * A write that extends the file must take the slow (IRP)
		 * path so ZFS can grow FileHeader.FileSize/AllocationSize
		 * and call CcSetFileSizes before any data is copied.  See
		 * fastio_write().
		 */
		if (FileObject->PrivateCacheMap == NULL ||
		    (uint64_t)FileOffset->QuadPart + Length >
		    (uint64_t)vp->FileHeader.FileSize.QuadPart)
			return (FALSE);

		if (/*!vp->Vcb->readonly &&
		    !is_subvol_readonly(vp->subvol, NULL) && */
		    FsRtlFastCheckLockForWrite(&vp->lock, FileOffset,
		    &quadlen, LockKey, FileObject, PsGetCurrentProcess()))
			return (TRUE);
	}

	return (FALSE);
}

static BOOLEAN __stdcall
fastio_write(PFILE_OBJECT FileObject, PLARGE_INTEGER FileOffset,
    ULONG Length, BOOLEAN Wait, ULONG LockKey, PVOID Buffer,
    PIO_STATUS_BLOCK IoStatus, PDEVICE_OBJECT DeviceObject)
{
	BOOLEAN ret;
	vnode_t *vp;
	znode_t *zp;

	if (FileObject == NULL || FileObject->FsContext == NULL ||
	    FileObject->PrivateCacheMap == NULL)
		return (FALSE);

	vp = (vnode_t *)FileObject->FsContext;

	/*
	 * FILE_WRITE_TO_END_OF_FILE (-1) is an unresolved append offset;
	 * only zfs_write_wrap's z_eof_pending CAS loop knows how to resolve
	 * it against concurrent appenders under proper serialisation.  Left
	 * unhandled here, (uint64_t)-1 + Length wraps around to a small
	 * value that slips under FileHeader.FileSize in the bound check
	 * below, so the unresolved append is treated as a safe in-bounds
	 * write and the literal -1 offset is passed straight into
	 * FsRtlCopyWrite/CcCopyWrite -- crashing MmMapViewInSystemCache
	 * (BSOD MEMORY_MANAGEMENT, subtype 0x103087).  Confirmed live via a
	 * MEMORY.DMP where FileOffset->QuadPart was exactly -1 at this
	 * frame (Length 0x1085, FileHeader.FileSize ~45GB, wrapped bound
	 * check value 0x1084).  Must bail to the slow path unconditionally.
	 *
	 * Compare LowPart/HighPart, not QuadPart: FILE_WRITE_TO_END_OF_FILE
	 * is an untyped 0xffffffff, i.e. plain (32-bit) unsigned int in C.
	 * QuadPart is a signed 64-bit LONGLONG, so QuadPart ==
	 * FILE_WRITE_TO_END_OF_FILE zero-extends the 32-bit constant to
	 * 0x00000000ffffffff instead of sign-extending it to -1 -- this
	 * comparison silently never matches and let a second MEMORY.DMP
	 * reproduce this exact crash right through the first attempt at
	 * this fix.
	 */
	if (FileOffset->LowPart == FILE_WRITE_TO_END_OF_FILE &&
	    FileOffset->HighPart == -1)
		return (FALSE);

	/*
	 * Hold PagingIoResource shared for the duration of the copy, the
	 * same resource the slow path takes exclusively while it grows
	 * FileSize/AllocationSize and calls CcSetFileSizes (see the
	 * "prevent truncates" acquisitions in zfs_write_wrap), so this
	 * keeps FsRtlCopyWrite from racing a concurrent size change
	 * (extension or truncation) that would otherwise corrupt the
	 * Cache Manager's view of the section (BSOD MEMORY_MANAGEMENT in
	 * MmMapViewInSystemCache).
	 *
	 * Deliberately NOT FileHeader.Resource: FsRtlCopyWrite may need to
	 * acquire that resource itself internally (e.g. ValidDataLength
	 * bookkeeping or a recursive cache-miss fault-in), and every
	 * reference driver (FastFAT, btrfs, NTFS) avoids holding it across
	 * this call for that reason.  NTFS specifically uses
	 * Header->PagingIoResource here for the same "prevent truncates"
	 * purpose.  Holding FileHeader.Resource instead self-deadlocks:
	 * this thread already owns it shared when FsRtlCopyWrite tries to
	 * acquire it again.
	 */
	if (!ExAcquireResourceSharedLite(vp->FileHeader.PagingIoResource,
	    Wait))
		return (FALSE);

	/*
	 * Re-check under the lock: an extending write must go through the
	 * slow (IRP) path, which grows FileSize/AllocationSize and calls
	 * CcSetFileSizes before copying any data into the cache.
	 */
	if ((uint64_t)FileOffset->QuadPart + Length >
	    (uint64_t)vp->FileHeader.FileSize.QuadPart) {
		ExReleaseResourceLite(vp->FileHeader.PagingIoResource);
		return (FALSE);
	}

	/*
	 * fastio_write otherwise has no logging at all, making any future
	 * FsRtlCopyWrite/CcCopyWrite failure here hard to correlate against
	 * the ring buffer.
	 */
	dprintf("fastio_write: vp=%p fo=%p off=%llx len=%lx wait=%d "
	    "AllocationSize=%llx FileSize=%llx ValidDataLength=%llx\n",
	    vp, FileObject, (unsigned long long)FileOffset->QuadPart,
	    Length, (int)Wait,
	    (unsigned long long)vp->FileHeader.AllocationSize.QuadPart,
	    (unsigned long long)vp->FileHeader.FileSize.QuadPart,
	    (unsigned long long)vp->FileHeader.ValidDataLength.QuadPart);

	ret = FsRtlCopyWrite(FileObject, FileOffset, Length,
	    Wait, LockKey, Buffer, IoStatus, DeviceObject);

	if (ret) {
		zp = VTOZ(vp);
		if (zp) {
			uint64_t new_end =
			    (uint64_t)FileOffset->QuadPart + Length;
			if (new_end > zp->z_size)
				zp->z_size = new_end;
		}
	}

	ExReleaseResourceLite(vp->FileHeader.PagingIoResource);

	return (ret);
}

// It would perhaps be re-use the query_basic_info() call above, if some of the
// IrpSp is moved out of it.
static BOOLEAN __stdcall
fastio_query_basic_info(PFILE_OBJECT FileObject, BOOLEAN wait,
    PFILE_BASIC_INFORMATION fbi, PIO_STATUS_BLOCK IoStatus,
    PDEVICE_OBJECT DeviceObject)
{
	vnode_t *vp;

	dprintf("%s: \n", __func__);

	if (!FileObject || !FileObject->FsContext ||
	    !FileObject->FsContext2) {
		return (FALSE);
	}

	FsRtlEnterFileSystem();

	vp = FileObject->FsContext;

	if (VN_HOLD(vp) != 0) {
		FsRtlExitFileSystem();
		return (FALSE);
	}

	if (!ExAcquireResourceSharedLite(vp->FileHeader.Resource, wait)) {
		VN_RELE(vp);
		FsRtlExitFileSystem();
		return (FALSE);
	}

	file_basic_information_impl(DeviceObject, FileObject, fbi, IoStatus);

	ExReleaseResourceLite(vp->FileHeader.Resource);
	VN_RELE(vp);
	FsRtlExitFileSystem();

	/* Return TRUE to say IoStatus was filled out */
	return (TRUE);
}

static BOOLEAN __stdcall
fastio_query_standard_info(PFILE_OBJECT FileObject,
    BOOLEAN wait, PFILE_STANDARD_INFORMATION fsi, PIO_STATUS_BLOCK IoStatus,
    PDEVICE_OBJECT DeviceObject)
{
	vnode_t *vp;

	dprintf("%s: \n", __func__);

	if (!FileObject || !FileObject->FsContext ||
	    !FileObject->FsContext2) {
		return (FALSE);
	}

	FsRtlEnterFileSystem();

	vp = FileObject->FsContext;

	if (VN_HOLD(vp) != 0) {
		FsRtlExitFileSystem();
		return (FALSE);
	}

	if (!ExAcquireResourceSharedLite(vp->FileHeader.Resource, wait)) {
		VN_RELE(vp);
		FsRtlExitFileSystem();
		return (FALSE);
	}

	file_standard_information_impl(DeviceObject, FileObject, fsi,
	    sizeof (FILE_STANDARD_INFORMATION), IoStatus);

	ExReleaseResourceLite(vp->FileHeader.Resource);
	VN_RELE(vp);
	FsRtlExitFileSystem();

	/* Return TRUE to say IoStatus was filled out */
	return (TRUE);
}

#define	fastio_possible(vp) (!FsRtlAreThereCurrentFileLocks(&vp->lock) \
	/* && !fcb->Vcb->readonly */ ? FastIoIsPossible : FastIoIsQuestionable)

static BOOLEAN __stdcall
fastio_lock(PFILE_OBJECT FileObject, PLARGE_INTEGER FileOffset,
    PLARGE_INTEGER Length, PEPROCESS ProcessId,	ULONG Key,
    BOOLEAN FailImmediately, BOOLEAN ExclusiveLock, PIO_STATUS_BLOCK IoStatus,
    PDEVICE_OBJECT DeviceObject)
{
	BOOLEAN ret;
	vnode_t *vp = FileObject->FsContext;

	dprintf("%s: \n", __func__);

	if (!vnode_isreg(vp)) {
		dprintf("%s: can only lock files\n", __func__);
		IoStatus->Status = STATUS_INVALID_PARAMETER;
		IoStatus->Information = 0;
		return (TRUE);
	}

	FsRtlEnterFileSystem();
	ExAcquireResourceSharedLite(vp->FileHeader.Resource, TRUE);

	ret = FsRtlFastLock(&vp->lock, FileObject, FileOffset, Length,
	    ProcessId, Key, FailImmediately, ExclusiveLock, IoStatus,
	    NULL, FALSE);

	if (ret)
		vp->FileHeader.IsFastIoPossible = fastio_possible(vp);

	ExReleaseResourceLite(vp->FileHeader.Resource);
	FsRtlExitFileSystem();

	return (ret);
}

static BOOLEAN __stdcall
fastio_unlock_single(PFILE_OBJECT FileObject, PLARGE_INTEGER FileOffset,
    PLARGE_INTEGER Length, PEPROCESS ProcessId,	ULONG Key,
    PIO_STATUS_BLOCK IoStatus, PDEVICE_OBJECT DeviceObject)
{
	vnode_t *vp = FileObject->FsContext;

	dprintf("%s: \n", __func__);

	IoStatus->Information = 0;

	if (!vnode_isreg(vp)) {
		dprintf("%s: can only lock files\n", __func__);
		IoStatus->Status = STATUS_INVALID_PARAMETER;
		return (TRUE);
	}

	FsRtlEnterFileSystem();

	IoStatus->Status = FsRtlFastUnlockSingle(&vp->lock, FileObject,
	    FileOffset, Length, ProcessId, Key, NULL, FALSE);

	vp->FileHeader.IsFastIoPossible = fastio_possible(vp);

	FsRtlExitFileSystem();

	return (TRUE);
}

static BOOLEAN __stdcall
fastio_unlock_all(PFILE_OBJECT FileObject, PEPROCESS ProcessId,
    PIO_STATUS_BLOCK IoStatus, PDEVICE_OBJECT DeviceObject)
{
	vnode_t *vp = FileObject->FsContext;

	dprintf("%s: \n", __func__);

	IoStatus->Information = 0;

	if (!vnode_isreg(vp)) {
		dprintf("%s: can only lock files\n", __func__);
		IoStatus->Status = STATUS_INVALID_PARAMETER;
		return (TRUE);
	}

	FsRtlEnterFileSystem();

	ExAcquireResourceSharedLite(vp->FileHeader.Resource, TRUE);

	IoStatus->Status = FsRtlFastUnlockAll(&vp->lock, FileObject,
	    ProcessId, NULL);

	vp->FileHeader.IsFastIoPossible = fastio_possible(vp);

	ExReleaseResourceLite(vp->FileHeader.Resource);

	FsRtlExitFileSystem();

	return (TRUE);
}

static BOOLEAN __stdcall
fastio_unlock_all_by_key(PFILE_OBJECT FileObject, PVOID ProcessId,
    ULONG Key, PIO_STATUS_BLOCK IoStatus, PDEVICE_OBJECT DeviceObject)
{
	vnode_t *vp = FileObject->FsContext;

	dprintf("%s: \n", __func__);

	IoStatus->Information = 0;

	if (!vnode_isreg(vp)) {
		dprintf("%s: can only lock files\n", __func__);
		IoStatus->Status = STATUS_INVALID_PARAMETER;
		return (TRUE);
	}

	FsRtlEnterFileSystem();

	ExAcquireResourceSharedLite(vp->FileHeader.Resource, TRUE);

	IoStatus->Status = FsRtlFastUnlockAllByKey(&vp->lock, FileObject,
	    ProcessId, Key, NULL);

	vp->FileHeader.IsFastIoPossible = fastio_possible(vp);

	ExReleaseResourceLite(vp->FileHeader.Resource);

	FsRtlExitFileSystem();

	return (TRUE);
}

static BOOLEAN
fastio_device_control(
    IN PFILE_OBJECT FileObject,
    IN BOOLEAN Wait,
    IN PVOID InputBuffer OPTIONAL,
    IN ULONG InputBufferLength,
    OUT PVOID OutputBuffer OPTIONAL,
    IN ULONG OutputBufferLength,
    IN ULONG IoControlCode,
    OUT PIO_STATUS_BLOCK IoStatus,
    IN PDEVICE_OBJECT DeviceObject)
{
	dprintf("%s:\n", __func__);
	mount_t *zmo = DeviceObject->DeviceExtension;

	dprintf("vpb %s %lu\n", __func__,
	    zmo && zmo->vpb ? zmo->vpb->ReferenceCount : -1);

	return (FALSE);
}

static void
fastio_acquire_file_for_ntsection(
    IN PFILE_OBJECT FileObject)
{
	vnode_t *vp;

	dprintf("%s: \n", __func__);

	if (!FileObject)
		return;

	vp = FileObject->FsContext;

	if (!vp || VN_HOLD(vp) != 0)
		return;

	FsRtlEnterFileSystem();
	ExAcquireResourceExclusiveLite(vp->FileHeader.Resource, TRUE);
	vnode_ref(vp);
	VN_RELE(vp);
	FsRtlExitFileSystem();
}

static void
fastio_release_file_for_ntsection(
    IN PFILE_OBJECT FileObject)
{
	vnode_t *vp;

	dprintf("%s: \n", __func__);

	if (!FileObject)
		return;

	vp = FileObject->FsContext;

	if (!vp || VN_HOLD(vp) != 0)
		return;

	FsRtlEnterFileSystem();
	ExReleaseResourceLite(vp->FileHeader.Resource);
	vnode_rele(vp);
	VN_RELE(vp);
	FsRtlExitFileSystem();
}

static void
fastio_detach_device(
    IN PDEVICE_OBJECT SourceDevice,
    IN PDEVICE_OBJECT TargetDevice)
{
	dprintf("%s:\n", __func__);
}

static BOOLEAN __stdcall
fastio_query_network_open_info(PFILE_OBJECT FileObject,
    BOOLEAN Wait, FILE_NETWORK_OPEN_INFORMATION *fnoi,
    PIO_STATUS_BLOCK IoStatus, PDEVICE_OBJECT DeviceObject)
{
	vnode_t *vp;

	dprintf("%s: \n", __func__);

	if (!FileObject || !FileObject->FsContext ||
	    !FileObject->FsContext2) {
		return (FALSE);
	}

	FsRtlEnterFileSystem();

	vp = FileObject->FsContext;

	if (VN_HOLD(vp) != 0) {
		FsRtlExitFileSystem();
		return (FALSE);
	}

	if (!ExAcquireResourceSharedLite(vp->FileHeader.Resource, Wait)) {
		VN_RELE(vp);
		FsRtlExitFileSystem();
		return (FALSE);
	}

	file_network_open_information_impl(DeviceObject, FileObject, vp,
	    fnoi,
	    IoStatus);

	ExReleaseResourceLite(vp->FileHeader.Resource);
	VN_RELE(vp);
	FsRtlExitFileSystem();

	/* Return TRUE to say IoStatus was filled out */
	return (TRUE);
}

static NTSTATUS __stdcall
fastio_acquire_for_mod_write(PFILE_OBJECT FileObject,
    PLARGE_INTEGER EndingOffset, struct _ERESOURCE **ResourceToRelease,
    PDEVICE_OBJECT DeviceObject)
{
	vnode_t *vp = NULL;
	mount_t *zmo = DeviceObject->DeviceExtension;
	zfsvfs_t *zfsvfs = vfs_fsprivate(zmo);
	NTSTATUS Status = STATUS_INVALID_PARAMETER;
	dprintf("%s: \n", __func__);

	if (unlikely(zfsvfs == NULL)) {
		dprintf("%s: fo %p already freed zfsvfs\n", __func__,
		    FileObject);
		return (STATUS_INVALID_PARAMETER);
	}

	FsRtlEnterFileSystem();

	if (vfs_busy(zfsvfs->z_vfs, 0) != 0) {
		FsRtlExitFileSystem();
		return (STATUS_INVALID_PARAMETER);
	}

	if (zfsvfs->z_unmounted ||
	    zfs_enter(zfsvfs, FTAG) != 0) {
		vfs_unbusy(zfsvfs->z_vfs);
		FsRtlExitFileSystem();
		return (STATUS_INVALID_PARAMETER);
	}

	vfs_unbusy(zfsvfs->z_vfs);

	vp = FileObject->FsContext;

	if (vp == NULL ||
	    VTOZ(vp) == NULL ||
	    VN_HOLD(vp) != 0) {
		zfs_exit(zfsvfs, FTAG);
		FsRtlExitFileSystem();
		return (STATUS_INVALID_PARAMETER);
	}
	zfs_exit(zfsvfs, FTAG);

	if (!ExAcquireResourceExclusiveLite(vp->FileHeader.Resource, FALSE)) {
		dprintf("%s: returning STATUS_CANT_WAIT\n", __func__);
		Status = STATUS_CANT_WAIT;
		goto out;
	}

	*ResourceToRelease = vp->FileHeader.Resource;
	vnode_ref(vp);
	Status = STATUS_SUCCESS;

out:
	VN_RELE(vp);

	// No zfs_exit(zfsvfs, FTAG) until below

	dprintf("%s: returning STATUS_SUCCESS\n", __func__);
	FsRtlExitFileSystem();

	return (Status);
}

static NTSTATUS __stdcall
fastio_release_for_mod_write(PFILE_OBJECT FileObject,
    struct _ERESOURCE *ResourceToRelease, PDEVICE_OBJECT DeviceObject)
{
	mount_t *zmo = DeviceObject->DeviceExtension;
	vnode_t *vp;

	dprintf("%s:\n", __func__);

	FsRtlEnterFileSystem();

	ExReleaseResourceLite(ResourceToRelease);

	vp = FileObject->FsContext;
	if (vp && VN_HOLD(vp) == 0) {

		VERIFY3P(ResourceToRelease, ==, vp->FileHeader.Resource);
		vnode_rele(vp);
		VN_RELE(vp);

		FsRtlExitFileSystem();
		return (STATUS_SUCCESS);
	}

	dprintf("%s WARNING FAILED\n", __func__);
	FsRtlExitFileSystem();
	return (STATUS_SUCCESS);
}

static BOOLEAN
fastio_read_compressed(
    IN PFILE_OBJECT FileObject,
    IN PLARGE_INTEGER FileOffset,
    IN ULONG Length,
    IN ULONG LockKey,
    IN PVOID Buffer,
    OUT PMDL *MdlChain,
    OUT PIO_STATUS_BLOCK IoStatus,
    IN PCOMPRESSED_DATA_INFO CompressedDataInfo,
    IN ULONG CompressedDataInfoLength,
    IN PDEVICE_OBJECT DeviceObject)
{
	dprintf("%s:\n", __func__);
	return (FALSE);
}

static BOOLEAN
fastio_write_compressed(
    IN PFILE_OBJECT FileObject,
    IN PLARGE_INTEGER FileOffset,
    IN ULONG Length,
    IN ULONG LockKey,
    IN PVOID Buffer,
    OUT PMDL *MdlChain,
    OUT PIO_STATUS_BLOCK IoStatus,
    IN PCOMPRESSED_DATA_INFO CompressedDataInfo,
    IN ULONG CompressedDataInfoLength,
    IN PDEVICE_OBJECT DeviceObject)
{
	dprintf("%s:\n", __func__);
	return (FALSE);
}

static BOOLEAN
fastio_read_complete_compressed(
    IN PFILE_OBJECT FileObject,
    IN PMDL MdlChain,
    IN PDEVICE_OBJECT DeviceObject)
{
	dprintf("%s:\n", __func__);
	return (FALSE);
}

static BOOLEAN
fastio_write_complete_compressed(
    IN PFILE_OBJECT FileObject,
    IN PLARGE_INTEGER FileOffset,
    IN PMDL MdlChain,
    IN PDEVICE_OBJECT DeviceObject)
{
	dprintf("%s:\n", __func__);
	return (FALSE);
}

static BOOLEAN
fastio_query_open(PIRP Irp,
    OUT PFILE_NETWORK_OPEN_INFORMATION NetworkInformation,
    IN PDEVICE_OBJECT DeviceObject)
{
	/* Attempt to open Irp->FileObject->Filename and return stat() */
	char *filename, *lastname = NULL;
	int error = STATUS_INVALID_PARAMETER;
	ULONG outlen;
	mount_t *zmo = DeviceObject->DeviceExtension;
	zfsvfs_t *zfsvfs = vfs_fsprivate(zmo);
	struct vnode *vp = NULL, *dvp = NULL;
	cred_t cr_buf;

	PIO_STACK_LOCATION IrpSp = IoGetCurrentIrpStackLocation(Irp);
#if 0
	// If it has never been open, make it do that through full
	// first, so vp is set.
	if (IrpSp->FileObject->FsContext == NULL)
		return (FALSE);
#endif
	dprintf("%s:\n", __func__);

	FsRtlEnterFileSystem();

	if (IrpSp->FileObject->FileName.Buffer != NULL &&
	    IrpSp->FileObject->FileName.Length > 0) {

		filename = kmem_alloc(PATH_MAX, KM_SLEEP);

		// Convert incoming filename to utf8
		error = RtlUnicodeToUTF8N(filename, PATH_MAX - 1, &outlen,
		    IrpSp->FileObject->FileName.Buffer,
		    IrpSp->FileObject->FileName.Length);

		if (error != STATUS_SUCCESS &&
		    error != STATUS_SOME_NOT_MAPPED) {
			dprintf("RtlUnicodeToUTF8N returned 0x%x "
			    "input len %d\n",
			    error, IrpSp->FileObject->FileName.Length);
			kmem_free(filename, PATH_MAX);
			FsRtlExitFileSystem();
			Irp->IoStatus.Status = STATUS_OBJECT_NAME_INVALID;
			Irp->IoStatus.Information = 0;
			return (FALSE);
		}

		filename[outlen] = 0;

		spl_fill_cred_from_irp(&cr_buf, Irp);
		error = zfs_find_dvp_vp(zfsvfs, filename, 0, 0,
		    &lastname, &dvp, &vp, 0, 0, &cr_buf);

// Handle reparse, or return FALSE/FAIL?
//		if (error == STATUS_REPARSE)
//			allocate_reparse(vp, lastname, Irp);
// But NTFS returns FALSE, so let's do same, so traces match.

		kmem_free(filename, PATH_MAX);

		if (dvp)
			VN_RELE(dvp);

		if (error == 0) {
			/* call sets the IoStatus */
			dprintf("%s: open OK stat()ing.\n", __func__);

			file_network_open_information_impl(DeviceObject,
			    IrpSp->FileObject, vp ? vp : dvp,
			    NetworkInformation,
			    &Irp->IoStatus);
			if (vp)
				VN_RELE(vp);

			FsRtlExitFileSystem();
			return (TRUE);
		}

		if (vp)
			VN_RELE(vp);

	}

	FsRtlExitFileSystem();

	/* Probably can skip setting these, we return FALSE */
	Irp->IoStatus.Status = error;
//	if (error == STATUS_REPARSE)
//		return (TRUE);

	Irp->IoStatus.Information = 0;

	return (FALSE);
}

static NTSTATUS __stdcall
fastio_acquire_for_ccflush(PFILE_OBJECT FileObject,
    PDEVICE_OBJECT DeviceObject)
{
	dprintf("%s:\n", __func__);
	IoSetTopLevelIrp((PIRP)FSRTL_CACHE_TOP_LEVEL_IRP);
	return (STATUS_SUCCESS);
}

static NTSTATUS __stdcall
fastio_release_for_ccflush(PFILE_OBJECT FileObject,
    PDEVICE_OBJECT DeviceObject)
{
	dprintf("%s:\n", __func__);
	if (IoGetTopLevelIrp() == (PIRP)FSRTL_CACHE_TOP_LEVEL_IRP)
		IoSetTopLevelIrp(NULL);
	return (STATUS_SUCCESS);
}

static FAST_IO_DISPATCH FastIoDispatch;

#endif // ZFS_HAVE_FASTIO

void
fastio_init(FAST_IO_DISPATCH **fast)
{
#ifdef ZFS_HAVE_FASTIO
	RtlZeroMemory(&FastIoDispatch, sizeof (FastIoDispatch));
	FastIoDispatch.SizeOfFastIoDispatch = sizeof (FAST_IO_DISPATCH);

	FastIoDispatch.FastIoCheckIfPossible = fastio_check_if_possible;
	FastIoDispatch.FastIoRead = FsRtlCopyRead;
	FastIoDispatch.FastIoWrite = fastio_write;
	FastIoDispatch.FastIoQueryBasicInfo = fastio_query_basic_info;
	FastIoDispatch.FastIoQueryStandardInfo = fastio_query_standard_info;
	FastIoDispatch.FastIoLock = fastio_lock;
	FastIoDispatch.FastIoUnlockSingle = fastio_unlock_single;
	FastIoDispatch.FastIoUnlockAll = fastio_unlock_all;
	FastIoDispatch.FastIoUnlockAllByKey = fastio_unlock_all_by_key;
	FastIoDispatch.FastIoDeviceControl = fastio_device_control;
	FastIoDispatch.AcquireFileForNtCreateSection =
	    fastio_acquire_file_for_ntsection;
	FastIoDispatch.ReleaseFileForNtCreateSection =
	    fastio_release_file_for_ntsection;
	FastIoDispatch.FastIoDetachDevice =
	    (PFAST_IO_DETACH_DEVICE) fastio_detach_device;
//	FastIoDispatch.FastIoQueryNetworkOpenInfo =
//	    fastio_query_network_open_info;
	FastIoDispatch.AcquireForModWrite = fastio_acquire_for_mod_write;
	FastIoDispatch.MdlRead = FsRtlMdlReadDev;
	FastIoDispatch.MdlReadComplete = FsRtlMdlReadCompleteDev;
	FastIoDispatch.PrepareMdlWrite = FsRtlPrepareMdlWriteDev;
	FastIoDispatch.MdlWriteComplete = FsRtlMdlWriteCompleteDev;
	FastIoDispatch.FastIoReadCompressed =
	    (PFAST_IO_READ_COMPRESSED) fastio_read_compressed;
	FastIoDispatch.FastIoWriteCompressed =
	    (PFAST_IO_WRITE_COMPRESSED) fastio_write_compressed;
	FastIoDispatch.MdlReadCompleteCompressed =
	    fastio_read_complete_compressed;
	FastIoDispatch.MdlWriteCompleteCompressed =
	    fastio_write_complete_compressed;
//	FastIoDispatch.FastIoQueryOpen = fastio_query_open;
	FastIoDispatch.ReleaseForModWrite = fastio_release_for_mod_write;
	FastIoDispatch.AcquireForCcFlush = fastio_acquire_for_ccflush;
	FastIoDispatch.ReleaseForCcFlush = fastio_release_for_ccflush;

	*fast = &FastIoDispatch;
//	dprintf("Using FASTIO\n");
#endif // ZFS_HAVE_FASTIO

}
