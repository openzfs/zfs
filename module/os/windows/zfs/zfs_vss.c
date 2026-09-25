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
 * fields enclosed by brackets "[]" replaced with your own applying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */
/*
 * Copyright (c) 2025 Jorgen Lundman <lundman@lundman.net>
 */

/*
 * VSS (Volume Shadow Copy Service) snapshot device support.
 *
 * -----------------------------------------------------------------------
 * DEVICE NAMING
 * -----------------------------------------------------------------------
 *
 * Each ZFS snapshot gets a kernel device object named:
 *
 *   \Device\ZfsSnapshot<hex16>
 *
 * where <hex16> is the 16-char lowercase hex of dsl_dataset_phys_t::ds_guid.
 * The name is deterministic and bidirectional: given the device name the
 * GUID is recoverable, and given the GUID the snapshot is findable via
 * the MOS.  No extra ZAP attribute or registry entry is needed.
 *
 * -----------------------------------------------------------------------
 * LIFECYCLE
 * -----------------------------------------------------------------------
 *
 *   zfs_vss_snapshot_add()    - called after snapshot creation
 *   zfs_vss_snapshot_remove() - called before snapshot destruction
 *   zfs_vss_pool_add()        - called after pool import; walks all
 *                               snapshots and calls snapshot_add()
 *   zfs_vss_pool_remove()     - called before pool export
 *   zfs_vss_init() / fini()   - module lifetime
 *
 * The userland VSS Provider COM service (cmd/os/windows/zfs_vss_provider/)
 * handles the VSS requestor protocol (BeginPrepareSnapshot / CommitSnapshots
 * etc.), calls "zfs snapshot"/"zfs destroy" via libzfs, maintains the
 * VSS-GUID -> ZFS-GUID mapping in the registry, and returns the
 * \Device\ZfsSnapshot<hex16> path as the shadow copy device object.
 *
 * -----------------------------------------------------------------------
 * HOW WINDOWS "PREVIOUS VERSIONS" WORKS  (the path we spent weeks on)
 * -----------------------------------------------------------------------
 *
 * The Previous Versions tab in Explorer is populated over SMB2.  srv2.sys
 * on the server drives a multi-step VOLSNAP IOCTL chain to discover and
 * validate each snapshot before building the @GMT timestamp list returned
 * to the client.  The flow below is what we verified by disassembling
 * srv2!Smb2ShareCheckForAndCreateSnapShot and watching it in WinDbg.
 *
 * STEP 1 — QUERY_NAMES_OF_SNAPSHOTS (0x530018) on the LIVE volume (DCB)
 *
 *   srv2 sends this to the live ZFS volume.  We return a VOLSNAP_NAMES
 *   structure: a ULONG MultiSzLength followed by a MULTI_SZ of device
 *   names, one per snapshot:
 *
 *     \Device\ZfsSnapshot<hex16>\0\Device\ZfsSnapshot<hex16>\0\0
 *
 *   CRITICAL — no trailing backslash on each entry.  srv2 appends one
 *   internally when it stores the entry in its SHARE_SNAPSHOT list.
 *   Inside Smb2ShareCheckForAndCreateSnapShot there is a
 *   "cmp [buf+35*2], '\\'  /  je skip_add" guard: if the entry already
 *   ends in '\' srv2 jumps PAST SrvSnapAddShare, so the snapshot is never
 *   added to the list.  The entries must be exactly 35 WCHARs
 *   (L"\\Device\\ZfsSnapshot" = 19 + 16 hex digits) with no terminating
 *   backslash.
 *
 * STEP 2 — srv2 opens each \Device\ZfsSnapshot<hex> device
 *
 *   IRP_MJ_CREATE arrives with an empty FileName or FileName == L"\\.
 *   This is a pure device-level open; no filesystem mount is required.
 *   We complete it immediately with FILE_OPENED.
 *
 * STEP 3 — APPLICATION_INFO (0x53019C) on the snapshot device
 *
 *   srv2 probes with a tiny buffer (we return STATUS_BUFFER_OVERFLOW
 *   with the required size at buf[0]), then retries with the full buffer.
 *
 *   The exact layout srv2!SrvSnapCheckAppInfoForTimeWarp validates
 *   (confirmed by WinDbg disassembly):
 *
 *     +0   ULONG InformationLength   total bytes after this field (56)
 *     +4   GUID  AppInfoLayoutId     MUST be
 *                                    VOLSNAP_APPINFO_GUID_CLIENT_ACCESSIBLE
 *                                    {e5de7d45-49f2-40a4-817c-7dc82b72587f}
 *     +20  GUID  SnapshotId          derived from ZFS ds_guid
 *     +36  GUID  SnapshotSetId       derived from ZFS ds_guid
 *     +52  ULONG SnapshotContext     VSS context flags
 *                                    MUST be 0x1D (see below)
 *     +56  ULONG SnapshotCount       1
 *
 *   srv2 checks [buf+0] > 0x10 (size sanity) and compares [buf+4] against
 *   the CLIENT_ACCESSIBLE GUID.  If both pass, it clears the internal
 *   SRV_SNAP_SHARE_NOT_FOUND flag on the SHARE_SNAPSHOT entry; that flag
 *   controls whether the snapshot appears in the Previous Versions list.
 *
 *   SnapshotContext MUST include VSS_VOLSNAP_ATTR_PERSISTENT (0x01).
 *   0x1D = PERSISTENT | CLIENT_ACCESSIBLE | NO_AUTO_RELEASE | NO_WRITERS
 *   is the canonical VSS_CTX_CLIENT_ACCESSIBLE.  Using 0x1C (the value
 *   without PERSISTENT) causes the Windows System Provider (swprv,
 *   {b5946137-7b9f-4925-af80-51abd60b20d5}) to log "Invalid context 0x1c"
 *   and return E_UNEXPECTED from its own EndPrepareSnapshots whenever it
 *   next creates an NTFS snapshot, because it iterates all registered
 *   shadow copies during its prepare phase and rejects unknown contexts.
 *   Result: NTFS shadow creation fails with VSS_E_UNEXPECTED_PROVIDER_ERROR
 *   whenever any ZFS snapshots exist.
 *
 * STEP 4 — REVERT_UID (0x5301E0)
 *
 *   Return 16 zero bytes.  A zero GUID is acceptable.
 *
 * STEP 5 — ORIGINAL_VOLUME_NAME (0x530190)
 *
 *   Must return the NT device name of the FILESYSTEM device (VCB), not
 *   the disk device (DCB).  srv2 compares this against the device object
 *   that IoGetRelatedDeviceObject returns for a file on the share.  For
 *   ZFS that is \Device\ZFS{uuid} (= dcb->fs_name, which was used as the
 *   name when the VCB device was created with IoCreateDeviceSecure).
 *
 *   Layout:  { USHORT NameLen; WCHAR Name[NameLen/2]; }
 *
 * STEP 6 — CONFIG_INFO / SNAPSHOT_TIMESTAMP (0x530194)
 *
 *   Despite being called SNAPSHOT_TIMESTAMP in many IOCTL tables, this
 *   IOCTL returns a VOLSNAP_CONFIG_INFO structure, NOT a bare LARGE_INTEGER:
 *
 *     typedef struct {
 *         ULONG         Attributes;            // VSS_VOLSNAP_ATTR_* flags
 *         ULONG         Reserved;
 *         LARGE_INTEGER SnapshotCreationTime;  // Windows FILETIME
 *     } VOLSNAP_CONFIG_INFO;
 *
 *   srv2 uses SnapshotCreationTime to synthesise the @GMT-YYYY.MM.DD-HH.MM.SS
 *   string that appears in the Previous Versions popup.  ZFS itself never
 *   produces @GMT-style names; srv2 constructs them entirely from this
 *   FILETIME.  Returning a bare 8-byte LARGE_INTEGER (skipping the 8-byte
 *   Attributes+Reserved header) causes every entry to display as
 *   "@GMT-1601.01.01-00.00.00" (Windows epoch = 0) and the popup appears
 *   empty even though the entry count is correct.
 *
 * STEP 7 — srv2 builds the FSCTL_SRV_ENUMERATE_SNAPSHOTS (0x144064) reply
 *
 *   srv2 does NOT forward FSCTL_SRV_ENUMERATE_SNAPSHOTS to our filesystem
 *   driver.  Instead it re-issues QUERY_NAMES_OF_SNAPSHOTS (step 1) and
 *   runs the full IOCTL chain for each device, storing the FILETIME in its
 *   internal SHARE_SNAPSHOT list.  The @GMT MULTI_SZ returned to the SMB
 *   client is built from that list.  Our own FSCTL_SRV_ENUMERATE_SNAPSHOTS
 *   handler in fsDispatcher is never called for the Previous Versions path.
 *
 * -----------------------------------------------------------------------
 * DEVICE TYPE AND VPB RULES
 * -----------------------------------------------------------------------
 *
 * FILE_DEVICE_DISK_FILE_SYSTEM is required (not FILE_DEVICE_DISK or
 * FILE_DEVICE_VIRTUAL_DISK).  Disk-style types cause the I/O Manager to
 * reject IRP_MJ_CREATE with a path component (e.g. \dir\file) with
 * ERROR_INVALID_NAME before a single IRP is dispatched, because the I/O
 * Manager needs either a filesystem device type or a mounted VPB to route
 * path-bearing opens.
 *
 * We do NOT install a VPB on the snapshot device.  A self-referential VPB
 * (where DeviceObject == RealDevice == our device) confuses the I/O Manager:
 * for FILE_DIRECTORY_FILE opens with a non-root FileName it attempts a
 * namespace sub-device lookup, fails to find one, and the IRP never reaches
 * our dispatcher.  Without a VPB, FILE_DEVICE_DISK_FILE_SYSTEM devices
 * receive all IRPs directly.  We substitute vfs_fsprivate(zmo) != NULL for
 * the VPB_MOUNTED flag to test whether the snapshot is mounted.
 *
 * -----------------------------------------------------------------------
 * LAZY MOUNT
 * -----------------------------------------------------------------------
 *
 * Snapshot devices start unmounted.  The first IRP_MJ_CREATE with a real
 * file path triggers a lazy mount via IoQueueWorkItem — necessary because
 * zfs_vfs_mount cannot run safely in the IRP dispatch context.  Pure
 * device opens (FileName == L"" or L"\") complete immediately without a
 * mount, so the srv2 VOLSNAP IOCTL chain (steps 2-6 above) never pays the
 * cost of a full ZFS mount.
 *
 * -----------------------------------------------------------------------
 * INTERACTION WITH NTFS SNAPSHOT CREATION
 * -----------------------------------------------------------------------
 *
 * The Windows System Provider iterates ALL registered shadow copies at the
 * start of every VSS operation to clean up auto-release copies.  If any
 * ZFS shadow copy advertises an invalid SnapshotContext (e.g. 0x1C),
 * swprv fails its own EndPrepareSnapshots with E_UNEXPECTED and NTFS
 * snapshot creation fails with VSS_E_UNEXPECTED_PROVIDER_ERROR.  The
 * symptom is order-dependent: ZFS-then-NTFS fails; NTFS-then-ZFS works
 * (because swprv scans existing shadows at prepare time, not at create time).
 * Fix: SnapshotContext = 0x1D in APPLICATION_INFO (step 3 above).
 *
 * -----------------------------------------------------------------------
 * CREATING VSS SNAPSHOTS — WHY "set context clientaccessible" FAILS
 * -----------------------------------------------------------------------
 *
 * It is tempting to create Previous-Versions-visible snapshots with:
 *
 *   DISKSHADOW> set context clientaccessible
 *   DISKSHADOW> add volume E:
 *   DISKSHADOW> create
 *
 * This fails for ZFS with VSS_E_VOLUME_NOT_SUPPORTED.  The reason: the
 * Windows System Provider (swprv) has a hard-coded blocklist that rejects
 * any filesystem it does not recognise for "timewarp/persistent" contexts.
 * swprv calls IsVolumeSupported on every registered provider; for the
 * clientaccessible context it vetoes ZFS volumes before our provider is
 * even consulted.
 *
 * The workaround is to create with a context swprv does not block:
 *
 *   DISKSHADOW> set context persistent
 *   DISKSHADOW> add volume E:
 *   DISKSHADOW> create
 *
 * Our VSS Provider's CommitSnapshots() unconditionally ORs the stored
 * snapshot attributes with:
 *
 *   VSS_VOLSNAP_ATTR_PERSISTENT | VSS_VOLSNAP_ATTR_NO_AUTO_RELEASE |
 *   VSS_VOLSNAP_ATTR_CLIENT_ACCESSIBLE | VSS_VOLSNAP_ATTR_DIFFERENTIAL
 *
 * regardless of the requested context.  The snapshot therefore appears in
 * Explorer's Previous Versions tab even though it was created with a
 * non-clientaccessible context.  The APPLICATION_INFO returned by this
 * kernel driver (step 3 above) always advertises 0x1D so srv2 treats the
 * snapshot as client-accessible on the network side as well.
 */

#include <Ntifs.h>
#include <ntdddisk.h>
#include <ntddstor.h>
#include <ntddvol.h>
#include <mountmgr.h>
#include <Mountdev.h>
#include <sys/dmu.h>
#include <sys/dmu_objset.h>
#include <sys/dsl_dataset.h>
#include <sys/dsl_pool.h>
#include <sys/spa.h>
#include <sys/spa_impl.h>
#include <sys/zfs_context.h>
#include <sys/zfs_znode.h>
#include <sys/vnode.h>
#include <sys/mount.h>

#include <sys/zfs_vss.h>
#include <sys/zfs_vfsops.h>
#include <sys/zfs_windows.h>
#include <sys/zfs_mount.h>

/* Prefix for all snapshot device objects. */
#define	ZFS_VSS_DEVICE_PREFIX	"\\Device\\ZfsSnapshot"
#define	ZFS_VSS_DEVICE_PREFIXW	L"\\Device\\ZfsSnapshot"

/*
 * Per-snapshot device tracking entry.
 * Stored in a global AVL tree keyed by ZFS dataset GUID.
 */
typedef struct zfs_vss_snap {
	avl_node_t	zvs_node;
	uint64_t	zvs_guid;	/* ZFS dataset GUID (key) */
	uint64_t	zvs_creation;	/* snapshot creation (Unix seconds) */
	PDEVICE_OBJECT	zvs_devobj;	/* \Device\ZfsSnapshot<hex> */
} zfs_vss_snap_t;

static avl_tree_t	zfs_vss_snaps;
static kmutex_t		zfs_vss_lock;

static int
zfs_vss_snap_compare(const void *a, const void *b)
{
	const zfs_vss_snap_t *sa = a;
	const zfs_vss_snap_t *sb = b;
	return (TREE_CMP(sa->zvs_guid, sb->zvs_guid));
}

extern PDRIVER_OBJECT WIN_DriverObject;

/*
 * Return B_TRUE if a kernel device object exists for this snapshot GUID.
 * Used by QUERY_NAMES_OF_SNAPSHOTS to skip stale ZFS snapshots whose
 * device was removed (e.g. between remove_by_name and destroy).
 */
boolean_t
zfs_vss_has_device(uint64_t guid)
{
	zfs_vss_snap_t key = { .zvs_guid = guid };
	mutex_enter(&zfs_vss_lock);
	boolean_t found = (avl_find(&zfs_vss_snaps, &key, NULL) != NULL);
	mutex_exit(&zfs_vss_lock);
	return (found);
}

/*
 * Create \Device\ZfsSnapshot<hex16> for the given ZFS dataset GUID.
 * poolname is stored in the device extension so IOCTL handlers can
 * open the SPA and look up the snapshot for size queries.
 */
int
zfs_vss_snapshot_add(uint64_t guid, const char *snapname, uint64_t creation)
{
	NTSTATUS status;
	char devname[64];
	ANSI_STRING astr;
	UNICODE_STRING ustr;
	PDEVICE_OBJECT devobj = NULL;

	mutex_enter(&zfs_vss_lock);

	/* Already tracked? */
	zfs_vss_snap_t key = { .zvs_guid = guid };
	if (avl_find(&zfs_vss_snaps, &key, NULL) != NULL) {
		mutex_exit(&zfs_vss_lock);
		return (0);
	}

	mutex_exit(&zfs_vss_lock);

	snprintf(devname, sizeof (devname),
	    ZFS_VSS_DEVICE_PREFIX "%016llx", (unsigned long long)guid);

	astr.Buffer = devname;
	astr.Length = (USHORT)strlen(devname);
	astr.MaximumLength = sizeof (devname);

	status = RtlAnsiStringToUnicodeString(&ustr, &astr, TRUE);
	if (!NT_SUCCESS(status)) {
		dprintf("%s: RtlAnsiStringToUnicodeString failed 0x%x\n",
		    __func__, status);
		return (EIO);
	}

	/*
	 * FILE_DEVICE_DISK_FILE_SYSTEM: required so that the IO Manager routes
	 * IRP_MJ_CREATE with a path component to this device.  With any disk-
	 * style device type (FILE_DEVICE_VIRTUAL_DISK, FILE_DEVICE_DISK, …)
	 * the IO Manager rejects "\\?\GLOBALROOT\Device\ZfsSnapshot...\path"
	 * with ERROR_INVALID_NAME before sending a single IRP — it requires
	 * either a filesystem device type or a VPB to locate the FS.
	 * FILE_READ_ONLY_DEVICE: snapshot is always read-only.
	 * Use sizeof(mount_t) so the device extension IS the mount object;
	 * fsDispatcher can handle file IRPs once the snapshot is
	 * lazily mounted.
	 */
	/*
	 * Do NOT use FILE_DEVICE_SECURE_OPEN: that flag makes the IO Manager
	 * enforce the device-object security descriptor for every
	 * file/directory
	 * open within the filesystem, not just raw device opens.  The default
	 * device SD (created by IoCreateDevice) grants access only to SYSTEM
	 * and Administrators, so Explorer (running with the user's
	 * limited token)
	 * would get STATUS_ACCESS_DENIED on every path open.
	 * Standard filesystem
	 * volumes (NTFS, ZFS live mounts) omit this flag and let the filesystem
	 * driver handle per-file security via its own ACL checks.
	 * FILE_READ_ONLY_DEVICE is still set to prevent write IRPs at
	 * the device
	 * level — snapshots are always read-only.
	 */
	status = IoCreateDevice(WIN_DriverObject,
	    sizeof (mount_t),
	    &ustr,
	    FILE_DEVICE_DISK_FILE_SYSTEM,
	    FILE_READ_ONLY_DEVICE,
	    FALSE,
	    &devobj);

	RtlFreeUnicodeString(&ustr);

	if (!NT_SUCCESS(status)) {
		dprintf("%s: IoCreateDevice '%s' failed 0x%x\n",
		    __func__, devname, status);
		return (EIO);
	}

	mount_t *zmo = devobj->DeviceExtension;
	RtlZeroMemory(zmo, sizeof (mount_t));
	zmo->type = MOUNT_TYPE_VSS;
	zmo->size = sizeof (mount_t);
	zmo->vss_guid = guid;
	zmo->vss_creation = creation;
	strlcpy(zmo->vss_snapname, snapname ? snapname : "",
	    sizeof (zmo->vss_snapname));
	/* ascii_name points into vss_snapname ("pool/ds@snap") */
	zmo->ascii_name = (const unsigned char *)zmo->vss_snapname;
	zmo->mountflags = MNT_RDONLY;
	mutex_init(&zmo->vss_mount_lock, NULL, MUTEX_DEFAULT, NULL);
	InitializeListHead(&zmo->DirNotifyList);
	FsRtlNotifyInitializeSync(&zmo->NotifySync);
	devobj->SectorSize = 512;

	/*
	 * Clear DO_DEVICE_INITIALIZING so the I/O Manager will accept IRPs
	 * sent to this device.  Must be done before notifying the
	 * Mount Manager,
	 * otherwise its probe IRPs are silently rejected.
	 */
	devobj->Flags &= ~DO_DEVICE_INITIALIZING;

	zfs_vss_snap_t *zvs = kmem_alloc(sizeof (*zvs), KM_SLEEP);
	zvs->zvs_guid = guid;
	zvs->zvs_creation = creation;
	zvs->zvs_devobj = devobj;

	mutex_enter(&zfs_vss_lock);
	avl_add(&zfs_vss_snaps, zvs);
	mutex_exit(&zfs_vss_lock);

	dprintf("%s: created %s\n", __func__, devname);

	return (0);
}

/*
 * Remove the device object for the given GUID.
 */
void
zfs_vss_snapshot_remove(uint64_t guid)
{
	zfs_vss_snap_t key = { .zvs_guid = guid };

	mutex_enter(&zfs_vss_lock);
	zfs_vss_snap_t *zvs = avl_find(&zfs_vss_snaps, &key, NULL);
	if (zvs == NULL) {
		mutex_exit(&zfs_vss_lock);
		return;
	}
	avl_remove(&zfs_vss_snaps, zvs);
	mutex_exit(&zfs_vss_lock);

	dprintf("%s: removing " ZFS_VSS_DEVICE_PREFIX "%016llx\n",
	    __func__, (unsigned long long)guid);

	mount_t *zmo = zvs->zvs_devobj->DeviceExtension;

	/*
	 * Unmount the snapshot filesystem if it was lazily mounted.
	 * zfs_vss_remove_by_prefix() may have already cleared fsprivate
	 * (pre-flight unmount during pool export), in which case we skip
	 * the unmount but still remove from the mount list if present.
	 */
	if (vfs_fsprivate(zmo) != NULL) {
		dprintf("%s: unmounting snapshot %016llx\n",
		    __func__, (unsigned long long)guid);
		vfs_setfsprivate(zmo, NULL);
		(void) zfs_windows_unmount_impl(zmo->vss_snapname);
	}
	if (vfs_mount_member(zmo))
		vfs_mount_remove(zmo);
	FsRtlNotifyUninitializeSync(&zmo->NotifySync);

	/*
	 * Signal IRP_MJ_CREATE to reject new opens before we call
	 * IoDeleteDevice.  After IoDeleteDevice the device is removed from
	 * the driver's linked list; any PnP open that arrives afterward
	 * (via a cached PDEVICE_OBJECT pointer) and is then closed would
	 * trigger IopCompleteUnloadOrDelete -> IopInsertRemoveDevice a
	 * second time on an already-removed device, BSODing at
	 * IopInsertRemoveDevice+0x5d (NULL->NextDevice).
	 */
	mutex_enter(&zmo->vss_mount_lock);
	zmo->vss_del_pending = B_TRUE;
	mutex_exit(&zmo->vss_mount_lock);
	mutex_destroy(&zmo->vss_mount_lock);

	/*
	 * Windows's IopInvalidateVolumesForDevice (called from
	 * IopRemoveDevice when the parent disk is removed) may have opened
	 * a file on our VSS device before vss_del_pending blocked new
	 * opens.  If DeviceObject->ReferenceCount is still > 0,
	 * IoDeleteDevice would remove the device from the driver's linked
	 * list immediately (Windows does NOT defer this even when
	 * ReferenceCount > 0, despite what MSDN implies).  Then when PnP
	 * closes its handle, IopDecrementDeviceObjectRef fires
	 * IopCompleteUnloadOrDelete -> IopInsertRemoveDevice a second time
	 * on a device already gone from the list, crashing at
	 * NULL->NextDevice.
	 *
	 * Since del_pending now prevents new opens, the count can only
	 * decrease.  Yield briefly so the PnP worker thread can complete
	 * its NtClose and let IopDecrementDeviceObjectRef drain the count
	 * to 0.  1 ms per tick, up to 100 ticks (100 ms total).
	 */
	if (zvs->zvs_devobj->ReferenceCount > 0) {
		LARGE_INTEGER delay;
		int poll;

		/* 1 ms (relative, 100-ns units) */
		delay.QuadPart = -10000LL;
		dprintf("%s: ReferenceCount > 0, waiting for PnP to drain\n",
		    __func__);
		for (poll = 0;
		    poll < 100 && zvs->zvs_devobj->ReferenceCount > 0;
		    poll++) {
			KeDelayExecutionThread(KernelMode, FALSE, &delay);
		}
		dprintf("%s: waited %d ms, ReferenceCount now %d\n", __func__,
		    poll, (int)zvs->zvs_devobj->ReferenceCount);
	}

	IoDeleteDevice(zvs->zvs_devobj);
	kmem_free(zvs, sizeof (*zvs));
}

/*
 * Resolve a snapshot dataset name to its GUID, then create the device.
 * Called after snapshot creation completes.
 */
void
zfs_vss_snapshot_add_by_name(const char *snapname)
{
	spa_t *spa;
	dsl_pool_t *dp;
	dsl_dataset_t *ds;

	if (spa_open(snapname, &spa, FTAG) != 0)
		return;

	dp = spa_get_dsl(spa);
	if (dp == NULL) {
		spa_close(spa, FTAG);
		return;
	}

	dsl_pool_config_enter(dp, FTAG);
	if (dsl_dataset_hold(dp, snapname, FTAG, &ds) == 0) {
		if (dsl_dataset_is_snapshot(ds))
			(void) zfs_vss_snapshot_add(
			    dsl_dataset_phys(ds)->ds_guid,
			    snapname, dsl_get_creation(ds));
		dsl_dataset_rele(ds, FTAG);
	}
	dsl_pool_config_exit(dp, FTAG);

	spa_close(spa, FTAG);
}

/*
 * Resolve a snapshot dataset name to its GUID, then remove the device.
 * Called before snapshot destruction.
 */
void
zfs_vss_snapshot_remove_by_name(const char *snapname)
{
	spa_t *spa;
	dsl_pool_t *dp;
	dsl_dataset_t *ds;

	if (spa_open(snapname, &spa, FTAG) != 0)
		return;

	dp = spa_get_dsl(spa);
	if (dp == NULL) {
		spa_close(spa, FTAG);
		return;
	}

	dsl_pool_config_enter(dp, FTAG);
	if (dsl_dataset_hold(dp, snapname, FTAG, &ds) == 0) {
		if (dsl_dataset_is_snapshot(ds))
			zfs_vss_snapshot_remove(dsl_dataset_phys(ds)->ds_guid);
		dsl_dataset_rele(ds, FTAG);
	}
	dsl_pool_config_exit(dp, FTAG);

	spa_close(spa, FTAG);
}

/*
 * Called after a pool is imported.  Walk every snapshot in every
 * mounted dataset and create the corresponding device object.
 */
void
zfs_vss_pool_add(spa_t *spa)
{
	dsl_pool_t *dp = spa_get_dsl(spa);
	if (dp == NULL)
		return;

	dsl_pool_config_enter(dp, FTAG);

	/*
	 * Iterate all datasets in the pool looking for snapshots.
	 * dmu_objset_find_dp visits every objset; we filter for snapshots.
	 */
	dmu_objset_find_dp(dp, dp->dp_root_dir_obj,
	    zfs_vss_pool_add_cb, NULL, DS_FIND_SNAPSHOTS | DS_FIND_CHILDREN);

	dsl_pool_config_exit(dp, FTAG);
}

/* Callback for dmu_objset_find_dp - create device for each snapshot. */
int
zfs_vss_pool_add_cb(dsl_pool_t *dp, dsl_dataset_t *ds, void *arg)
{
	(void) arg;

	if (!dsl_dataset_is_snapshot(ds))
		return (0);

	char dsname[ZFS_MAX_DATASET_NAME_LEN];
	dsl_dataset_name(ds, dsname);
	uint64_t guid = dsl_dataset_phys(ds)->ds_guid;
	uint64_t ctime = dsl_get_creation(ds);
	(void) zfs_vss_snapshot_add(guid, dsname, ctime);
	return (0);
}

/*
 * Unmount the ZFS filesystem from each VSS snapshot device whose
 * vss_snapname begins with "<prefix>@" or "<prefix>/", WITHOUT deleting
 * the device objects.  This is the pre-flight step called from
 * zfs_unmount_snap_children: it clears fsprivate before the parent
 * dataset teardown so that IRP_MN_SURPRISE_REMOVAL cannot race with a
 * live VCB inside the VSS device.
 *
 * The device objects remain alive (still in the AVL tree and driver device
 * list).  zfs_vss_pool_remove() → zfs_vss_snapshot_remove() is called
 * later from spa_export_os() to do the full cleanup including IoDeleteDevice,
 * at which point the pool teardown is complete and it is safe to do so.
 */
void
zfs_vss_remove_by_prefix(const char *prefix)
{
	size_t plen = strlen(prefix);
	uint_t alloc, nsnaps = 0;
	uint64_t *guids;
	zfs_vss_snap_t *zvs;

	mutex_enter(&zfs_vss_lock);
	alloc = (uint_t)avl_numnodes(&zfs_vss_snaps);
	mutex_exit(&zfs_vss_lock);

	if (alloc == 0)
		return;

	guids = kmem_alloc(alloc * sizeof (uint64_t), KM_SLEEP);

	mutex_enter(&zfs_vss_lock);
	for (zvs = avl_first(&zfs_vss_snaps); zvs != NULL && nsnaps < alloc;
	    zvs = AVL_NEXT(&zfs_vss_snaps, zvs)) {
		mount_t *zmo = zvs->zvs_devobj->DeviceExtension;
		const char *sname = zmo->vss_snapname;
		if (strncmp(sname, prefix, plen) == 0 &&
		    (sname[plen] == '@' || sname[plen] == '/'))
			guids[nsnaps++] = zvs->zvs_guid;
	}
	mutex_exit(&zfs_vss_lock);

	for (uint_t i = 0; i < nsnaps; i++) {
		zfs_vss_snap_t key = { .zvs_guid = guids[i] };
		mount_t *zmo;

		mutex_enter(&zfs_vss_lock);
		zvs = avl_find(&zfs_vss_snaps, &key, NULL);
		zmo = (zvs != NULL) ? zvs->zvs_devobj->DeviceExtension : NULL;
		mutex_exit(&zfs_vss_lock);

		if (zmo == NULL)
			continue;

		if (vfs_fsprivate(zmo) != NULL) {
			dprintf("%s: suspending VSS snap guid %016llx "
			    "(prefix '%s')\n", __func__,
			    (unsigned long long)guids[i], prefix);
			vfs_setfsprivate(zmo, NULL);
			(void) zfs_windows_unmount_impl(zmo->vss_snapname);
		}
	}

	kmem_free(guids, alloc * sizeof (uint64_t));
}

/*
 * Before zfs_windows_unmount_impl() tears down a snapshot VCB, clear
 * fsprivate on any VSS stub device that still points at that zfsvfs.
 * Without this, the next IRP on the VSS stub dereferences the destroyed
 * zfsvfs and panics with MUTEX_DESTROYED.
 */
void
zfs_vss_clear_fsprivate(const char *snapname)
{
	zfs_vss_snap_t *zvs;

	mutex_enter(&zfs_vss_lock);
	for (zvs = avl_first(&zfs_vss_snaps); zvs != NULL;
	    zvs = AVL_NEXT(&zfs_vss_snaps, zvs)) {
		mount_t *zmo = zvs->zvs_devobj->DeviceExtension;
		if (strcmp(zmo->vss_snapname, snapname) != 0)
			continue;
		mutex_enter(&zmo->vss_mount_lock);
		if (vfs_fsprivate(zmo) != NULL) {
			dprintf("%s: clearing fsprivate for VSS stub '%s'\n",
			    __func__, snapname);
			vfs_setfsprivate(zmo, NULL);
		}
		mutex_exit(&zmo->vss_mount_lock);
	}
	mutex_exit(&zfs_vss_lock);
}

/*
 * Called before a pool is exported.  Remove all snapshot devices
 * belonging to this pool.
 *
 * We walk the AVL tree and delete any entry whose device object
 * name matches the pool's snapshots by re-checking via the dataset GUID.
 * For simplicity we remove everything and let re-import recreate them;
 * if multiple pools are active, only the exported pool's snapshots are
 * affected because GUIDs are pool-unique.
 */
void
zfs_vss_pool_remove(spa_t *spa)
{
	/*
	 * Collect GUIDs to remove (can't remove while iterating AVL, and
	 * can't sleep while holding the mutex).  Pre-allocate based on the
	 * current tree count, then fill under the lock.
	 */
	uint_t alloc = 0;
	mutex_enter(&zfs_vss_lock);
	alloc = (uint_t)avl_numnodes(&zfs_vss_snaps);
	mutex_exit(&zfs_vss_lock);

	if (alloc == 0)
		return;

	uint64_t *guids = kmem_alloc(alloc * sizeof (uint64_t), KM_SLEEP);
	uint_t nsnaps = 0;

	/*
	 * Collect GUIDs for snapshots belonging to this pool.
	 * Match by snapshot name prefix: "poolname@" or "poolname/".
	 * Note: zvs_guid is the ZFS dataset GUID (a random 64-bit value),
	 * NOT the DMU object number, so dsl_dataset_hold_obj() cannot be
	 * used to filter — it would always fail.
	 */
	const char *poolname = spa_name(spa);
	size_t poolname_len = strlen(poolname);

	mutex_enter(&zfs_vss_lock);
	zfs_vss_snap_t *zvs;
	for (zvs = avl_first(&zfs_vss_snaps); zvs != NULL &&
	    nsnaps < alloc; zvs = AVL_NEXT(&zfs_vss_snaps, zvs)) {
		mount_t *zmo = zvs->zvs_devobj->DeviceExtension;
		const char *sname = zmo->vss_snapname;
		if (strncmp(sname, poolname, poolname_len) == 0 &&
		    (sname[poolname_len] == '@' || sname[poolname_len] == '/'))
			guids[nsnaps++] = zvs->zvs_guid;
	}
	mutex_exit(&zfs_vss_lock);

	for (uint_t i = 0; i < nsnaps; i++)
		zfs_vss_snapshot_remove(guids[i]);

	if (guids != NULL)
		kmem_free(guids, alloc * sizeof (uint64_t));
}

/*
 * Find the GUID of the snapshot of 'dataset' whose creation time is
 * the largest value that is <= unix_time.  Used by the @GMT time-warp
 * path handler: given a UTC timestamp from a Previous Versions path
 * (\@GMT-YYYY.MM.DD-HH.MM.SS\...), find the ZFS snapshot to open.
 *
 * Returns the dataset GUID, or 0 if no matching snapshot was found.
 *
 * 'dataset' is the live dataset name (e.g. "pool/data"); only snapshots
 * of the form "pool/data@..." are considered.
 */
uint64_t
zfs_vss_find_by_time(uint64_t unix_time, const char *dataset)
{
	size_t dslen = dataset ? strlen(dataset) : 0;
	uint64_t best_guid = 0;
	uint64_t best_ctime = 0;
	zfs_vss_snap_t *zvs;

	mutex_enter(&zfs_vss_lock);
	for (zvs = avl_first(&zfs_vss_snaps);
	    zvs != NULL;
	    zvs = AVL_NEXT(&zfs_vss_snaps, zvs)) {
		/* Filter by dataset: snapname must be "<dataset>@..." */
		if (dslen > 0) {
			mount_t *snzmo =
			    (mount_t *)zvs->zvs_devobj->DeviceExtension;
			const char *sn = snzmo->vss_snapname;
			if (strncmp(sn, dataset, dslen) != 0 ||
			    sn[dslen] != '@')
				continue;
		}
		if (zvs->zvs_creation <= unix_time &&
		    zvs->zvs_creation > best_ctime) {
			best_ctime = zvs->zvs_creation;
			best_guid  = zvs->zvs_guid;
		}
	}
	mutex_exit(&zfs_vss_lock);
	return (best_guid);
}

void
zfs_vss_init(void)
{
	mutex_init(&zfs_vss_lock, NULL, MUTEX_DEFAULT, NULL);
	avl_create(&zfs_vss_snaps, zfs_vss_snap_compare,
	    sizeof (zfs_vss_snap_t), offsetof(zfs_vss_snap_t, zvs_node));
	dprintf("%s: VSS snapshot device support initialised\n", __func__);
}

void
zfs_vss_fini(void)
{
	/* Remove any remaining snapshot devices. */
	mutex_enter(&zfs_vss_lock);
	zfs_vss_snap_t *zvs;
	void *cookie = NULL;
	while ((zvs = avl_destroy_nodes(&zfs_vss_snaps, &cookie)) != NULL) {
		mount_t *zmo_fini = zvs->zvs_devobj->DeviceExtension;
		mutex_enter(&zmo_fini->vss_mount_lock);
		zmo_fini->vss_del_pending = B_TRUE;
		mutex_exit(&zmo_fini->vss_mount_lock);
		mutex_destroy(&zmo_fini->vss_mount_lock);
		IoDeleteDevice(zvs->zvs_devobj);
		kmem_free(zvs, sizeof (*zvs));
	}
	avl_destroy(&zfs_vss_snaps);
	mutex_exit(&zfs_vss_lock);
	mutex_destroy(&zfs_vss_lock);
}

/*
 * Get the number of bytes referenced by the snapshot (its logical size).
 * Returns 0 if the lookup fails.
 */
static uint64_t
zfs_vss_snap_refbytes(mount_t *zmo)
{
	spa_t *spa;
	dsl_pool_t *dp;
	dsl_dataset_t *ds;
	uint64_t refbytes = 0;

	/* Extract pool name from "pool/ds@snap" or "pool" */
	char poolname[256];
	const char *snapname = zmo->vss_snapname;
	const char *sep = strpbrk(snapname, "/@");
	size_t plen = sep ? (size_t)(sep - snapname) : strlen(snapname);
	if (plen == 0 || plen >= sizeof (poolname))
		return (0);
	strlcpy(poolname, snapname, plen + 1);

	if (spa_open(poolname, &spa, FTAG) != 0)
		return (0);

	dp = spa_get_dsl(spa);
	if (dp == NULL) {
		spa_close(spa, FTAG);
		return (0);
	}

	dsl_pool_config_enter(dp, FTAG);
	if (dsl_dataset_hold_obj(dp, zmo->vss_guid, FTAG, &ds) == 0) {
		refbytes = dsl_dataset_phys(ds)->ds_referenced_bytes;
		dsl_dataset_rele(ds, FTAG);
	}
	dsl_pool_config_exit(dp, FTAG);
	spa_close(spa, FTAG);

	return (refbytes);
}

/*
 * Return the VCB device object for a VSS stub that has been fully mounted.
 * The stub's fsprivate is borrowed from the VCB after zfs_vss_do_mount();
 * zfsvfs->z_vfs is the VCB mount_t.  Returns NULL if not yet mounted.
 */
static PDEVICE_OBJECT
zfs_vss_vcb_devobj(mount_t *zmo)
{
	zfsvfs_t *zfsvfs = vfs_fsprivate(zmo);
	if (zfsvfs == NULL)
		return (NULL);
	mount_t *vcb = zfsvfs->z_vfs;
	return (vcb ? vcb->VolumeDeviceObject : NULL);
}

/*
 * Mount snapshot fullname ("pool/data@snap") onto the standard ctldir path
 * $parent_mountpoint\.zfs\snapshot\$snap_component.  After this returns 0 a
 * DCB+VCB pair exists for the snapshot and file IRPs are routed to the VCB.
 *
 * Called from both the VSS lazy-mount path (zfs_vss_do_mount) and the ctldir
 * auto-mount path (zfsctl_root_lookup) so the logic lives in one place.
 */
int
zfs_vss_snapshot_mount(const char *parent_name, const char *fullname,
    const char *snap_component)
{
	char parent_mpt[PATH_MAX];
	char value[PATH_MAX];
	int err;

	/*
	 * Find the parent's Windows mountpoint via its VCB (getzfsvfs acquires
	 * a reader lock).  The DCB carries the \??\ mountpoint string.
	 * Release the busy lock before calling zfs_windows_mount_impl.
	 */
	zfsvfs_t *parent_zfsvfs;
	if (getzfsvfs(parent_name, &parent_zfsvfs) != 0) {
		dprintf("%s: parent dataset '%s' not mounted\n",
		    __func__, parent_name);
		return (ENOENT);
	}
	mount_t *parent_vcb = parent_zfsvfs->z_vfs;
	mount_t *parent_dcb = parent_vcb->parent_device;

	ANSI_STRING ansi;
	ansi.Buffer = parent_mpt;
	ansi.MaximumLength = sizeof (parent_mpt) - 1;
	NTSTATUS ns = RtlUnicodeStringToAnsiString(&ansi,
	    &parent_dcb->mountpoint, FALSE);
	vfs_unbusy(parent_vcb);

	if (!NT_SUCCESS(ns)) {
		dprintf("%s: mountpoint conversion failed 0x%lx\n",
		    __func__, ns);
		return (EINVAL);
	}
	parent_mpt[ansi.Length] = '\0';

	/* Strip trailing backslash */
	size_t mptlen = strlen(parent_mpt);
	if (mptlen > 0 && parent_mpt[mptlen - 1] == '\\')
		parent_mpt[--mptlen] = '\0';

	/* Normalize \DosDevices\ prefix to \??\ which mount_impl expects */
	if (strncmp(parent_mpt, "\\DosDevices\\", 12) == 0) {
		char tmp[PATH_MAX];
		snprintf(tmp, sizeof (tmp), "\\??\\%s", parent_mpt + 12);
		strlcpy(parent_mpt, tmp, sizeof (parent_mpt));
		mptlen = strlen(parent_mpt);
	}

	/* Construct \??\X:\.zfs\snapshot\<snap_component> */
	snprintf(value, sizeof (value), "%s\\.zfs\\snapshot\\%s",
	    parent_mpt, snap_component);

	dprintf("%s: mounting '%s' at '%s'\n", __func__, fullname, value);

	err = zfs_windows_mount_impl(fullname, value, sizeof (value),
	    MNT_RDONLY);
	if (err != 0)
		dprintf("%s: mount failed %d\n", __func__, err);
	return (err);
}

/*
 * VSS stub lazy-mount: mount the snapshot and wire up the stub's fsprivate
 * so the VSS dispatcher can forward IRPs to the real VCB.
 */
static int
zfs_vss_do_mount(PDEVICE_OBJECT DeviceObject, mount_t *zmo)
{
	ASSERT(vfs_fsprivate(zmo) == NULL);

	/* Split "pool/data@snap" into parent name and snap component */
	const char *at = strchr(zmo->vss_snapname, '@');
	if (at == NULL)
		return (EINVAL);
	size_t plen = (size_t)(at - zmo->vss_snapname);
	char parent_name[ZFS_MAX_DATASET_NAME_LEN];
	if (plen == 0 || plen >= sizeof (parent_name))
		return (EINVAL);
	strlcpy(parent_name, zmo->vss_snapname, plen + 1);

	/*
	 * Borrow the VCB's zfsvfs into the stub's fsprivate.  This lets all
	 * the existing vfs_fsprivate(zmo) != NULL guards in the dispatcher
	 * work unchanged, and lets zfs_vss_vcb_devobj() find the VCB device
	 * for IRP forwarding.  We do NOT call zfs_vfs_mount() on the stub.
	 *
	 * Check if the snapshot is already mounted (e.g. via 'zfs mount' or
	 * ctldir auto-mount) before trying to create a new device, which
	 * would fail with STATUS_OBJECT_NAME_COLLISION (c0000035).
	 */
	zfsvfs_t *zfsvfs;
	if (getzfsvfs(zmo->vss_snapname, &zfsvfs) == 0) {
		zfsvfs->z_mimic = ZFS_MIMIC_NTFS;
		vfs_setfsprivate(zmo, zfsvfs);
		vfs_unbusy(zfsvfs->z_vfs);
		dprintf("%s: snapshot '%s' already mounted\n",
		    __func__, zmo->vss_snapname);
		return (0);
	}

	int err = zfs_vss_snapshot_mount(parent_name, zmo->vss_snapname,
	    at + 1);
	if (err != 0)
		return (err);

	if (getzfsvfs(zmo->vss_snapname, &zfsvfs) == 0) {
		zfsvfs->z_mimic = ZFS_MIMIC_NTFS;
		vfs_setfsprivate(zmo, zfsvfs);
		vfs_unbusy(zfsvfs->z_vfs);
	}

	dprintf("%s: snapshot '%s' mounted\n", __func__, zmo->vss_snapname);
	return (0);
}

/*
 * Work item context for lazy snapshot mount.
 * Carries the pending IRP so it can be completed after the mount finishes.
 */
typedef struct {
	PIO_WORKITEM	wi_item;
	PDEVICE_OBJECT	wi_devobj;
	PIRP		wi_irp;
} zfs_vss_mount_wi_t;

static IO_WORKITEM_ROUTINE zfs_vss_mount_workitem;
static void
zfs_vss_mount_workitem(PDEVICE_OBJECT DeviceObject, PVOID Context)
{
	zfs_vss_mount_wi_t *wi = Context;
	PIRP Irp = wi->wi_irp;
	PIO_STACK_LOCATION IrpSp = IoGetCurrentIrpStackLocation(Irp);
	mount_t *zmo = DeviceObject->DeviceExtension;

	/*
	 * Serialise: only one thread mounts; others wait on the mutex and
	 * then fall through to fsDispatcher with the snapshot already live.
	 */
	mutex_enter(&zmo->vss_mount_lock);

	if (vfs_fsprivate(zmo) == NULL)
		(void) zfs_vss_do_mount(DeviceObject, zmo);

	mutex_exit(&zmo->vss_mount_lock);

	if (vfs_fsprivate(zmo) == NULL) {
		/* Mount failed; reject the create. */
		Irp->IoStatus.Status = STATUS_UNRECOGNIZED_VOLUME;
		Irp->IoStatus.Information = 0;
		IoCompleteRequest(Irp, IO_NO_INCREMENT);
	} else {
		/*
		 * Snapshot is live; let fsDispatcher handle the create.
		 * zfs_vss_do_mount releases vfs_busy before returning, which
		 * opens a window where zfsctl_unmount_thread could tear down
		 * the snapshot before we reach fsDispatcher.  Re-acquire a
		 * READER reference here so the snapshot cannot be unmounted
		 * while fsDispatcher uses the zfsvfs.
		 */
		zfsvfs_t *snap_zfsvfs;
		boolean_t have_busy = (getzfsvfs(zmo->vss_snapname,
		    &snap_zfsvfs) == 0);
		if (!have_busy) {
			/* Snapshot was unmounted between do_mount and here. */
			Irp->IoStatus.Status = STATUS_UNRECOGNIZED_VOLUME;
			Irp->IoStatus.Information = 0;
			IoCompleteRequest(Irp, IO_NO_INCREMENT);
		} else {
			PDEVICE_OBJECT vcb_devobj = zfs_vss_vcb_devobj(zmo);
			NTSTATUS st = fsDispatcher(
			    vcb_devobj ? vcb_devobj : DeviceObject, &Irp,
			    IrpSp);
			vfs_unbusy(snap_zfsvfs->z_vfs);
			if (Irp != NULL) {
				Irp->IoStatus.Status = st;
				IoCompleteRequest(Irp, IO_NO_INCREMENT);
			}
		}
	}

	IoFreeWorkItem(wi->wi_item);
	kmem_free(wi, sizeof (*wi));
}

/*
 * Helper for IOCTL_VOLSNAP_QUERY_ORIGINAL_VOLUME_NAME.
 * Walks the mount list for the DCB whose ascii_name matches the parent
 * dataset (vss_snapname with the "@snapshot" suffix stripped).
 */
typedef struct {
	const char	*dataset;
	USHORT		namelen;	/* bytes copied into namebuf */
	WCHAR		namebuf[PATH_MAX];
	boolean_t	found;
} vqovn_ctx_t;

static int
vqovn_find_cb(void *mp, void *arg)
{
	mount_t *m = mp;
	vqovn_ctx_t *ctx = arg;

	if (m->type != MOUNT_TYPE_DCB ||
	    m->ascii_name == NULL ||
	    strcmp(m->ascii_name, ctx->dataset) != 0)
		return (0);

	/*
	 * srv2.sys compares ORIGINAL_VOLUME_NAME against the filesystem
	 * volume device (the VCB), not the disk device.  The DCB's fs_name
	 * (\Device\ZFS{uuid}) is the name IoCreateDevice used for the VCB,
	 * so it is what IoGetRelatedDeviceObject returns for a file on the
	 * share.  Return fs_name; fall back to device_name if fs_name is
	 * unset (should not happen for a mounted volume).
	 */
	const UNICODE_STRING *src = (m->fs_name.Length > 0 &&
	    m->fs_name.Buffer != NULL) ? &m->fs_name : &m->device_name;
	if (src->Length == 0 || src->Buffer == NULL)
		return (0);

	USHORT copy_len = min(src->Length,
	    (USHORT)(sizeof (ctx->namebuf) - sizeof (WCHAR)));
	RtlCopyMemory(ctx->namebuf, src->Buffer, copy_len);
	ctx->namelen = copy_len;
	ctx->found = B_TRUE;
	dprintf("%s: returning '%.*S' for dataset '%s'\n",
	    __func__, (int)(copy_len / sizeof (WCHAR)), ctx->namebuf,
	    ctx->dataset);
	return (1);
}

/*
 * IRP_MJ_DEVICE_CONTROL handler for VSS snapshot devices.
 *
 * Handles the storage/volume/mountdev IOCTLs that VSS and the
 * Mount Manager issue when discovering and characterising shadow copies.
 *
 * From WireShark:
 *  {0x530018, "IOCTL_VOLSNAP_QUERY_NAMES_OF_SNAPSHOTS" },
 *  {0x530050, "IOCTL_VOLSNAP_QUERY_EPIC" },
 *  {0x530190, "IOCTL_VOLSNAP_QUERY_ORIGINAL_VOLUME_NAME" },
 *  {0x530194, "IOCTL_VOLSNAP_QUERY_SNAPSHOT_TIMESTAMP" },
 *  {0x53019c, "IOCTL_VOLSNAP_QUERY_APPLICATION_INFO" },
 *  {0x5301e0, "IOCTL_VOLSNAP_QUERY_REVERT_UID" },
 *  {0x534058, "IOCTL_VOLSNAP_QUERY_DIFF_AREA_MINIMUM_SIZE" },
 *  {0x534070, "IOCTL_VOLSNAP_QUERY_APPLICATION_FLAGS" },
 *  {0x534194, "IOCTL_VOLSNAP_QUERY_SNAPSHOT_TIMESTAMP_RO" },
 *  {0x53419C, "IOCTL_VOLSNAP_QUERY_APPLICATION_INFO" }, //  (FILE_READ_ACCESS)
 *  {0x53c004, "IOCTL_VOLSNAP_RELEASE_WRITES" },
 *  {0x53c198, "IOCTL_VOLSNAP_SET_APPLICATION_INFO" },
 *
 *  {0x530024, "IOCTL_VOLSNAP_QUERY_DIFF_AREA" },
 */

#ifndef VOLSNAP_DEVICE_TYPE
#define	VOLSNAP_DEVICE_TYPE	0x00000053
#endif

#ifndef IOCTL_VOLSNAP_QUERY_NAMES_OF_SNAPSHOTS
#define	IOCTL_VOLSNAP_QUERY_NAMES_OF_SNAPSHOTS	\
	CTL_CODE(VOLSNAP_DEVICE_TYPE, 6, METHOD_BUFFERED, FILE_ANY_ACCESS)
#endif

#ifndef IOCTL_VOLSNAP_QUERY_EPIC
#define	IOCTL_VOLSNAP_QUERY_EPIC	\
	CTL_CODE(VOLSNAP_DEVICE_TYPE, 20, METHOD_BUFFERED, FILE_ANY_ACCESS)
#endif

#ifndef IOCTL_VOLSNAP_QUERY_DIFF_AREA
#define	IOCTL_VOLSNAP_QUERY_DIFF_AREA	\
	CTL_CODE(VOLSNAP_DEVICE_TYPE, 9, METHOD_BUFFERED, FILE_ANY_ACCESS)
#endif

#ifndef IOCTL_VOLSNAP_QUERY_ORIGINAL_VOLUME_NAME
#define	IOCTL_VOLSNAP_QUERY_ORIGINAL_VOLUME_NAME	\
	CTL_CODE(VOLSNAP_DEVICE_TYPE, 100, METHOD_BUFFERED, FILE_ANY_ACCESS)
#endif

/* IOCTL_VOLSNAP_QUERY_SNAPSHOT_TIMESTAMP */
#ifndef IOCTL_VOLSNAP_QUERY_CONFIG_INFO
#define	IOCTL_VOLSNAP_QUERY_CONFIG_INFO	\
	CTL_CODE(VOLSNAP_DEVICE_TYPE, 101, METHOD_BUFFERED, FILE_ANY_ACCESS)
#endif

#ifndef IOCTL_VOLSNAP_QUERY_CONFIG_INFO_RO
#define	IOCTL_VOLSNAP_QUERY_CONFIG_INFO_RO	\
	CTL_CODE(VOLSNAP_DEVICE_TYPE, 101, METHOD_BUFFERED, FILE_READ_ACCESS)
#endif

#ifndef IOCTL_VOLSNAP_QUERY_DIFF_AREA_MINIMUM_SIZE
#define	IOCTL_VOLSNAP_QUERY_DIFF_AREA_MINIMUM_SIZE	\
	CTL_CODE(VOLSNAP_DEVICE_TYPE, 22, METHOD_BUFFERED, FILE_READ_ACCESS)
#endif

#ifndef IOCTL_VOLSNAP_QUERY_APPLICATION_FLAGS
#define	IOCTL_VOLSNAP_QUERY_APPLICATION_FLAGS	\
	CTL_CODE(VOLSNAP_DEVICE_TYPE, 28, METHOD_BUFFERED, FILE_READ_ACCESS)
#endif

/* Function 103 with ANY access (0x53019C) */
#ifndef IOCTL_VOLSNAP_QUERY_APPLICATION_INFO
#define	IOCTL_VOLSNAP_QUERY_APPLICATION_INFO	\
	CTL_CODE(VOLSNAP_DEVICE_TYPE, 103, METHOD_BUFFERED, FILE_ANY_ACCESS)
#endif

/* Function 103 with READ access (0x53419C) */
#ifndef IOCTL_VOLSNAP_QUERY_APPLICATION_INFO_RO
#define	IOCTL_VOLSNAP_QUERY_APPLICATION_INFO_RO	\
	CTL_CODE(VOLSNAP_DEVICE_TYPE, 103, METHOD_BUFFERED, FILE_READ_ACCESS)
#endif

#ifndef IOCTL_VOLSNAP_RELEASE_WRITES
#define	IOCTL_VOLSNAP_RELEASE_WRITES	\
	CTL_CODE(VOLSNAP_DEVICE_TYPE, 1, METHOD_BUFFERED, FILE_WRITE_ACCESS)
#endif

#ifndef IOCTL_VOLSNAP_SET_APPLICATION_INFO
#define	IOCTL_VOLSNAP_SET_APPLICATION_INFO	\
	CTL_CODE(VOLSNAP_DEVICE_TYPE, 102, METHOD_BUFFERED, FILE_WRITE_ACCESS)
#endif

#ifndef IOCTL_VOLSNAP_QUERY_REVERT_UID
#define	IOCTL_VOLSNAP_QUERY_REVERT_UID	\
	CTL_CODE(VOLSNAP_DEVICE_TYPE, 120, METHOD_BUFFERED, FILE_ANY_ACCESS)
#endif

#ifndef IOCTL_VOLSNAP_QUERY_APPLICATION_INFO_PROPERTIES
#define	IOCTL_VOLSNAP_QUERY_APPLICATION_INFO_PROPERTIES	\
	CTL_CODE(VOLSNAP_DEVICE_TYPE, 27, METHOD_BUFFERED, FILE_READ_ACCESS)
#endif

#ifndef IOCTL_VOLSNAP_QUERY_NAMES_OF_SNAPSHOTS_RO
#define	IOCTL_VOLSNAP_QUERY_NAMES_OF_SNAPSHOTS_RO	\
	CTL_CODE(VOLSNAP_DEVICE_TYPE, 5, METHOD_BUFFERED, FILE_READ_ACCESS)
#endif

#ifndef VSS_VOLSNAP_ATTR_PERSISTENT
#define	VSS_VOLSNAP_ATTR_PERSISTENT	0x00000001
#endif
#ifndef VSS_VOLSNAP_ATTR_NO_AUTORECOVERY
#define	VSS_VOLSNAP_ATTR_NO_AUTORECOVERY	0x00000002
#endif
#ifndef VSS_VOLSNAP_ATTR_CLIENT_ACCESSIBLE
#define	VSS_VOLSNAP_ATTR_CLIENT_ACCESSIBLE	0x00000004
#endif
#ifndef VSS_VOLSNAP_ATTR_NO_AUTO_RELEASE
#define	VSS_VOLSNAP_ATTR_NO_AUTO_RELEASE	0x00000008
#endif
#ifndef VSS_VOLSNAP_ATTR_NO_WRITERS
#define	VSS_VOLSNAP_ATTR_NO_WRITERS	0x00000010
#endif


static NTSTATUS
zfs_vss_device_control(PDEVICE_OBJECT DeviceObject, PIRP Irp,
    PIO_STACK_LOCATION IrpSp)
{
	mount_t *zmo = DeviceObject->DeviceExtension;
	ulong_t cmd = IrpSp->Parameters.DeviceIoControl.IoControlCode;
	ulong_t outlen = IrpSp->Parameters.DeviceIoControl.OutputBufferLength;
	void *buf = Irp->AssociatedIrp.SystemBuffer;
	NTSTATUS status = STATUS_INVALID_DEVICE_REQUEST;

	switch (cmd) {

	case IOCTL_VOLUME_IS_CLUSTERED: /* 0x560030 */
		/*
		 * srv2.sys sends this to every volume device while scanning for
		 * shadow copies.  STATUS_UNSUCCESSFUL = not clustered (correct
		 * for snapshot devices).  STATUS_INVALID_DEVICE_REQUEST would
		 * cause srv2.sys to skip this device as an invalid disk type.
		 */
		dprintf("VSS %s: IOCTL_VOLUME_IS_CLUSTERED -> not clustered\n",
		    __func__);
		Irp->IoStatus.Information = 0;
		status = STATUS_UNSUCCESSFUL;
		break;

	case IOCTL_DISK_IS_WRITABLE:
		/* Snapshots are always read-only */
		dprintf("VSS %s: IOCTL_DISK_IS_WRITABLE\n", __func__);
		Irp->IoStatus.Information = 0;
		status = STATUS_MEDIA_WRITE_PROTECTED;
		break;

	case IOCTL_DISK_CHECK_VERIFY:
	case IOCTL_STORAGE_CHECK_VERIFY:
	case IOCTL_STORAGE_CHECK_VERIFY2:
	case IOCTL_DISK_MEDIA_REMOVAL:
	case IOCTL_STORAGE_MEDIA_REMOVAL:
	case IOCTL_VOLUME_ONLINE:
#ifdef IOCTL_VOLUME_POST_ONLINE
	case IOCTL_VOLUME_POST_ONLINE:
#endif
	case IOCTL_MOUNTMGR_VOLUME_MOUNT_POINT_CREATED:
	case IOCTL_MOUNTMGR_VOLUME_MOUNT_POINT_DELETED:
	case IOCTL_MOUNTDEV_LINK_CREATED:
	case IOCTL_MOUNTDEV_LINK_DELETED:
		dprintf("VSS %s: simple-ok ioctl 0x%lx\n", __func__, cmd);
		Irp->IoStatus.Information = 0;
		status = STATUS_SUCCESS;
		break;

	case IOCTL_MOUNTDEV_QUERY_SUGGESTED_LINK_NAME:
		/*
		 * Tell the Mount Manager we have no suggested link name so it
		 * does not auto-assign a drive letter to snapshot devices.
		 */
		dprintf("VSS %s: IOCTL_MOUNTDEV_QUERY_SUGGESTED_LINK_NAME"
		    " (suppressed)\n", __func__);
		Irp->IoStatus.Information = 0;
		status = STATUS_NOT_FOUND;
		break;

	case IOCTL_STORAGE_GET_DEVICE_NUMBER:
	{
		dprintf("VSS %s: IOCTL_STORAGE_GET_DEVICE_NUMBER\n", __func__);
		if (outlen < sizeof (STORAGE_DEVICE_NUMBER)) {
			Irp->IoStatus.Information =
			    sizeof (STORAGE_DEVICE_NUMBER);
			status = STATUS_BUFFER_TOO_SMALL;
			break;
		}
		STORAGE_DEVICE_NUMBER *sdn = buf;
		sdn->DeviceType    = FILE_DEVICE_VIRTUAL_DISK;
		sdn->DeviceNumber  = (ULONG)(zmo->vss_guid & 0xffffffff);
		sdn->PartitionNumber = (ULONG)-1;
		Irp->IoStatus.Information = sizeof (STORAGE_DEVICE_NUMBER);
		status = STATUS_SUCCESS;
		break;
	}

	case IOCTL_DISK_GET_LENGTH_INFO:
	{
		dprintf("VSS %s: IOCTL_DISK_GET_LENGTH_INFO\n", __func__);
		if (outlen < sizeof (GET_LENGTH_INFORMATION)) {
			Irp->IoStatus.Information =
			    sizeof (GET_LENGTH_INFORMATION);
			status = STATUS_BUFFER_TOO_SMALL;
			break;
		}
		GET_LENGTH_INFORMATION *gli = buf;
		gli->Length.QuadPart = (int64_t)zfs_vss_snap_refbytes(zmo);
		Irp->IoStatus.Information = sizeof (GET_LENGTH_INFORMATION);
		status = STATUS_SUCCESS;
		break;
	}

	case IOCTL_DISK_GET_DRIVE_GEOMETRY:
	{
		dprintf("VSS %s: IOCTL_DISK_GET_DRIVE_GEOMETRY\n", __func__);
		if (outlen < sizeof (DISK_GEOMETRY)) {
			Irp->IoStatus.Information = sizeof (DISK_GEOMETRY);
			status = STATUS_BUFFER_TOO_SMALL;
			break;
		}
		uint64_t total = zfs_vss_snap_refbytes(zmo);
		const ULONG bps = 512;
		const ULONG spt = 63;
		const ULONG tpc = 255;
		ULONG spc = spt * tpc;
		LONGLONG cyls = (total / bps) / spc;
		if (cyls < 1)
			cyls = 1;
		DISK_GEOMETRY *dg = buf;
		dg->Cylinders.QuadPart = cyls;
		dg->TracksPerCylinder  = tpc;
		dg->SectorsPerTrack    = spt;
		dg->BytesPerSector = bps;
		dg->MediaType = FixedMedia;
		Irp->IoStatus.Information = sizeof (DISK_GEOMETRY);
		status = STATUS_SUCCESS;
		break;
	}

	case IOCTL_MOUNTDEV_QUERY_DEVICE_NAME:
	{
		dprintf("VSS %s: IOCTL_MOUNTDEV_QUERY_DEVICE_NAME\n",
		    __func__);
		/* Build wide device name from the prefix + hex GUID */
		WCHAR wname[64];
		int nch = _snwprintf(wname, sizeof (wname) / sizeof (wname[0]),
		    ZFS_VSS_DEVICE_PREFIXW L"%016llx",
		    (unsigned long long)zmo->vss_guid);
		USHORT namelen = (USHORT)(nch * sizeof (WCHAR));
		ULONG needed = FIELD_OFFSET(MOUNTDEV_NAME, Name) + namelen;
		if (outlen < sizeof (MOUNTDEV_NAME)) {
			Irp->IoStatus.Information = needed;
			status = STATUS_BUFFER_TOO_SMALL;
			break;
		}
		MOUNTDEV_NAME *mdn = buf;
		mdn->NameLength = namelen;
		if (outlen < needed) {
			Irp->IoStatus.Information = needed;
			status = STATUS_BUFFER_OVERFLOW;
			break;
		}
		RtlCopyMemory(mdn->Name, wname, namelen);
		Irp->IoStatus.Information = needed;
		status = STATUS_SUCCESS;
		break;
	}

	case IOCTL_MOUNTDEV_QUERY_UNIQUE_ID:
	{
		dprintf("VSS %s: IOCTL_MOUNTDEV_QUERY_UNIQUE_ID\n", __func__);
		/* Use the hex GUID string as the unique ID (ASCII bytes) */
		char uid[32];
		int uidlen = snprintf(uid, sizeof (uid),
		    "%016llx", (unsigned long long)zmo->vss_guid);
		ULONG needed = FIELD_OFFSET(MOUNTDEV_UNIQUE_ID, UniqueId) +
		    uidlen;
		if (outlen < sizeof (MOUNTDEV_UNIQUE_ID)) {
			Irp->IoStatus.Information = needed;
			status = STATUS_BUFFER_TOO_SMALL;
			break;
		}
		MOUNTDEV_UNIQUE_ID *uid_out = buf;
		uid_out->UniqueIdLength = (USHORT)uidlen;
		if (outlen < needed) {
			Irp->IoStatus.Information = needed;
			status = STATUS_BUFFER_OVERFLOW;
			break;
		}
		RtlCopyMemory(uid_out->UniqueId, uid, uidlen);
		Irp->IoStatus.Information = needed;
		status = STATUS_SUCCESS;
		/*
		 * MountMgr's arrival probe ends with QUERY_UNIQUE_ID.
		 * Mark the device as past the probe phase so that subsequent
		 * FILE_DIRECTORY_FILE opens (from srv2.sys or twext.dll) are
		 * allowed to trigger the lazy snapshot mount.
		 */
		zmo->vss_mountmgr_probed = B_TRUE;
		break;
	}

	case IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS:
	{
		dprintf("VSS %s: IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS\n",
		    __func__);
		if (outlen < sizeof (VOLUME_DISK_EXTENTS)) {
			Irp->IoStatus.Information =
			    sizeof (VOLUME_DISK_EXTENTS);
			status = STATUS_BUFFER_TOO_SMALL;
			break;
		}
		VOLUME_DISK_EXTENTS *vde = buf;
		vde->NumberOfDiskExtents = 1;
		vde->Extents[0].DiskNumber =
		    (ULONG)(zmo->vss_guid & 0xffffffff);
		vde->Extents[0].StartingOffset.QuadPart = 0;
		vde->Extents[0].ExtentLength.QuadPart =
		    (int64_t)zfs_vss_snap_refbytes(zmo);
		Irp->IoStatus.Information = sizeof (VOLUME_DISK_EXTENTS);
		status = STATUS_SUCCESS;
		break;
	}

	case IOCTL_VOLUME_GET_GPT_ATTRIBUTES:
	{
		dprintf("VSS %s: IOCTL_VOLUME_GET_GPT_ATTRIBUTES\n", __func__);
		if (outlen < sizeof (VOLUME_GET_GPT_ATTRIBUTES_INFORMATION)) {
			Irp->IoStatus.Information =
			    sizeof (VOLUME_GET_GPT_ATTRIBUTES_INFORMATION);
			status = STATUS_BUFFER_TOO_SMALL;
			break;
		}
		VOLUME_GET_GPT_ATTRIBUTES_INFORMATION *ga = buf;
		/* GPT_ATTRIBUTE_PLATFORM_REQUIRED | no-automount */
		ga->GptAttributes = 0x8000000000000001ULL;
		Irp->IoStatus.Information =
		    sizeof (VOLUME_GET_GPT_ATTRIBUTES_INFORMATION);
		status = STATUS_SUCCESS;
		break;
	}

	case IOCTL_VOLSNAP_QUERY_DIFF_AREA_MINIMUM_SIZE:
		/*
		 * Snapshot devices cannot host a volsnap diff-area.
		 * Decline so the System Provider does not try to use
		 * ZFS snapshot devices as diff-area storage targets.
		 */
		dprintf("VSS %s: IOCTL_VOLSNAP_QUERY_DIFF_AREA_MINIMUM_SIZE"
		    " -> NOT_SUPPORTED\n", __func__);
		Irp->IoStatus.Information = 0;
		status = STATUS_NOT_SUPPORTED;
		break;

	case IOCTL_VOLSNAP_FLUSH_AND_HOLD_WRITES:
	case IOCTL_VOLSNAP_RELEASE_WRITES:
		/*
		 * Snapshot devices are read-only; no write hold/release needed.
		 */
		dprintf("VSS %s: VOLSNAP flush/hold/release no-op 0x%lx\n",
		    __func__, cmd);
		Irp->IoStatus.Information = 0;
		status = STATUS_SUCCESS;
		break;

	case IOCTL_VOLSNAP_QUERY_APPLICATION_FLAGS:
	{
		/*
		 * Returns ApplicationInformation (68 bytes):
		 *   ULONG length, GUID×3, ULONG×3, LONG attributes, ULONG.
		 * All zeros = no VSS application has set flags.
		 */
		const ULONG appsz = 4 + 16 + 16 + 16 + 4 + 4 + 4 + 4;
		dprintf("VSS %s: IOCTL_VOLSNAP_QUERY_APPLICATION_FLAGS"
		    " -> 68 zero bytes\n", __func__);
		if (outlen < appsz) {
			Irp->IoStatus.Information = appsz;
			status = STATUS_BUFFER_TOO_SMALL;
			break;
		}
		RtlZeroMemory(buf, appsz);
		Irp->IoStatus.Information = appsz;
		status = STATUS_SUCCESS;
		break;
	}

	case IOCTL_VOLSNAP_QUERY_APPLICATION_INFO_PROPERTIES:
		/*
		 * Snapshot devices cannot host a diff-area; decline.
		 */
		dprintf("VSS %s: "
		    "IOCTL_VOLSNAP_QUERY_APPLICATION_INFO_PROPERTIES"
		    " -> NOT_SUPPORTED\n", __func__);
		Irp->IoStatus.Information = 0;
		status = STATUS_NOT_SUPPORTED;
		break;

	case IOCTL_VOLSNAP_QUERY_NAMES_OF_SNAPSHOTS:
	case IOCTL_VOLSNAP_QUERY_NAMES_OF_SNAPSHOTS_RO:
	{
		/*
		 * The snapshot device itself has no child snapshots.
		 * Return an empty VOLSNAP_NAMES: MultiSzLength=0, Names[1]=0.
		 *
		 * VOLSNAP_NAMES: { ULONG MultiSzLength; WCHAR Names[1]; }
		 * sizeof() = 8 (ULONG=4, WCHAR=2, 2-byte pad to ULONG align).
		 * ichannel Unpack<VOLSNAP_NAMES> requires at least 8 bytes;
		 * returning only 4 bytes causes "IOCTL Unpack overflow".
		 */
		const ULONG sz = sizeof (ULONG) + sizeof (WCHAR) +
		    2 * sizeof (BYTE); /* = 8, matching sizeof(VOLSNAP_NAMES) */
		dprintf("VSS %s: IOCTL_VOLSNAP_QUERY_NAMES_OF_SNAPSHOTS\n",
		    __func__);
		if (outlen < sz) {
			Irp->IoStatus.Information = sz;
			status = STATUS_BUFFER_TOO_SMALL;
			break;
		}
		RtlZeroMemory(buf, sz); /* MultiSzLength=0, Names=L"" */
		Irp->IoStatus.Information = sz;
		status = STATUS_SUCCESS;
		break;
	}

	case IOCTL_VOLSNAP_QUERY_DIFF_AREA:
	{
		/*
		 * Return an empty diff-area entry.
		 * struct: ULONG DiffAreaVolumeNameLength + WCHAR ZeroTerminator
		 */
		typedef struct _MOCK_DIFF_INFO {
			ULONG	DiffAreaVolumeNameLength;
			WCHAR	ZeroTerminator;
		} MOCK_DIFF_INFO;

		ULONG need = sizeof (MOCK_DIFF_INFO);

		dprintf("VSS %s: IOCTL_VOLSNAP_QUERY_DIFF_AREA"
		    " (outlen=%lu)\n", __func__, outlen);
		if (outlen < need) {
			Irp->IoStatus.Information = need;
			status = STATUS_BUFFER_OVERFLOW;
			break;
		}
		MOCK_DIFF_INFO *diff_info = (MOCK_DIFF_INFO *)buf;
		RtlZeroMemory(diff_info, outlen);
		diff_info->DiffAreaVolumeNameLength = 0;
		diff_info->ZeroTerminator = L'\0';
		Irp->IoStatus.Information = need;
		status = STATUS_SUCCESS;
		break;
	}

	case IOCTL_VOLSNAP_QUERY_REVERT_UID:
		/*
		 * srv2.sys calls this after QUERY_APPLICATION_INFO.
		 * Returns a 16-byte UID (GUID-sized) identifying this snapshot
		 * for revert purposes.  A zero GUID is acceptable here.
		 */
		dprintf("VSS %s: IOCTL_VOLSNAP_QUERY_REVERT_UID\n", __func__);
		if (outlen >= 16)
			RtlZeroMemory(buf, 16);
		Irp->IoStatus.Information = (outlen >= 16) ? 16 : 0;
		status = STATUS_SUCCESS;
		break;

	case IOCTL_VOLSNAP_QUERY_CONFIG_INFO:
	case IOCTL_VOLSNAP_QUERY_CONFIG_INFO_RO:
	{
		typedef struct _VOLSNAP_CONFIG_INFO {
			ULONG		Attributes;
			ULONG		Reserved;
			LARGE_INTEGER	SnapshotCreationTime;
		} VOLSNAP_CONFIG_INFO, *PVOLSNAP_CONFIG_INFO;

		ULONG need = sizeof (VOLSNAP_CONFIG_INFO);

		dprintf("VSS %s: IOCTL_VOLSNAP_QUERY_CONFIG_INFO\n",
		    __func__);
		if (outlen < need) {
			Irp->IoStatus.Information = need;
			status = STATUS_BUFFER_OVERFLOW;
			break;
		}
		VOLSNAP_CONFIG_INFO *config = (VOLSNAP_CONFIG_INFO *)buf;
		RtlZeroMemory(config, sizeof (*config));
		config->Attributes =
		    VSS_VOLSNAP_ATTR_PERSISTENT |
		    VSS_VOLSNAP_ATTR_CLIENT_ACCESSIBLE |
		    VSS_VOLSNAP_ATTR_NO_AUTO_RELEASE;
		/*
		 * SnapshotCreationTime must be a Windows FILETIME
		 * (100-ns intervals since January 1, 1601).
		 */
		LARGE_INTEGER ct;
		TIME_UNIX_TO_WINDOWS_EX(zmo->vss_creation, 0,
		    ct.QuadPart);
		config->SnapshotCreationTime.QuadPart = ct.QuadPart;
		Irp->IoStatus.Information = sizeof (VOLSNAP_CONFIG_INFO);
		status = STATUS_SUCCESS;
		break;
	}

	case IOCTL_VOLSNAP_QUERY_ORIGINAL_VOLUME_NAME:
	{
		char vqovn_ds[256];
		strlcpy(vqovn_ds, zmo->vss_snapname, sizeof (vqovn_ds));
		char *vqovn_at = strchr(vqovn_ds, '@');
		if (vqovn_at)
			*vqovn_at = '\0';

		vqovn_ctx_t vqovn_ctx = { .dataset = vqovn_ds };
		vfs_mount_iterate(vqovn_find_cb, &vqovn_ctx);

		if (!vqovn_ctx.found) {
			status = STATUS_NOT_FOUND;
			break;
		}
		/* Volume name: USHORT length prefix + WCHAR array */
		ULONG string_need =
		    sizeof (USHORT) + vqovn_ctx.namelen;
		if (outlen < string_need) {
			Irp->IoStatus.Information = string_need;
			status = STATUS_BUFFER_OVERFLOW;
			break;
		}
		USHORT *out_len_prefix = (USHORT *)buf;
		*out_len_prefix = vqovn_ctx.namelen;
		RtlCopyMemory(out_len_prefix + 1,
		    vqovn_ctx.namebuf, vqovn_ctx.namelen);
		Irp->IoStatus.Information = string_need;
		status = STATUS_SUCCESS;
		break;
	}

	case IOCTL_VOLSNAP_QUERY_APPLICATION_INFO:
	/* IOCTL_VOLSNAP_QUERY_APPLICATION_INFO_RO: 0x53419C */
	/* same payload; read-only access variant used by srv2 */
	case IOCTL_VOLSNAP_QUERY_APPLICATION_INFO_RO:
	{
		/*
		 * srv2.sys probes with a small buffer (BUFFER_OVERFLOW),
		 * reads [probe_buf+0] as DWORD, adds 8, allocates, retries.
		 * After the real call it checks:
		 *   [rdi+0] DWORD > 0x10  (total size sanity)
		 *   [rdi+4] == VOLSNAP_APPINFO_GUID_CLIENT_ACCESSIBLE (16 B)
		 *
		 * Wire format (inferred from srv2.sys disassembly):
		 *   ULONG  cb;    +0: total struct size, must be > 16
		 *   GUID   guid;  +4: VOLSNAP_APPINFO_GUID_CLIENT_ACCESSIBLE
		 *   GUID   sid;   +20: snapshot ID
		 *   GUID   setid; +36: snapshot set ID
		 *   ULONG  ctx;   +52: VSS context flags
		 *   ULONG  cnt;   +56: snapshot count
		 */
#pragma pack(push, 4)
		typedef struct _VSS_APPLICATION_INFO_PAYLOAD {
			ULONG	InformationLength;
			GUID	AppInfoLayoutId;
			GUID	SnapshotId;
			GUID	SnapshotSetId;
			ULONG	SnapshotContext;
			ULONG	SnapshotCount;
		} VSS_APPLICATION_INFO_PAYLOAD;
#pragma pack(pop)

		ULONG need = sizeof (VSS_APPLICATION_INFO_PAYLOAD);

		dprintf("VSS %s: IOCTL_VOLSNAP_QUERY_APPLICATION_INFO"
		    " (outlen=%lu)\n", __func__, outlen);
		if (outlen < need) {
			if (outlen >= sizeof (ULONG))
				*(ULONG *)buf = need;
			Irp->IoStatus.Information = sizeof (ULONG);
			status = STATUS_BUFFER_OVERFLOW;
			break;
		}
		VSS_APPLICATION_INFO_PAYLOAD *app_info =
		    (VSS_APPLICATION_INFO_PAYLOAD *)buf;
		RtlZeroMemory(app_info, need);
		app_info->InformationLength = need - sizeof (ULONG);

		static const GUID client_accessible_guid = {
			0xe5de7d45, 0x49f2, 0x40a4,
			{0x81, 0x7c, 0x7d, 0xc8, 0x2b, 0x72, 0x58, 0x7f}
		};
		app_info->AppInfoLayoutId = client_accessible_guid;

		GUID *g = (GUID *)&(app_info->SnapshotId);
		g->Data1 = (ULONG)(zmo->vss_guid & 0xffffffff);
		g->Data2 = (USHORT)((zmo->vss_guid >> 32) & 0xffff);
		g->Data3 = (USHORT)((zmo->vss_guid >> 48) & 0xffff);

		g = (GUID *)&(app_info->SnapshotSetId);
		g->Data1 = (ULONG)(zmo->vss_guid & 0xffffffff);
		g->Data2 = (USHORT)((zmo->vss_guid >> 32) & 0xffff);
		g->Data3 = (USHORT)((zmo->vss_guid >> 48) & 0xffff);

		/* 0x1D = VSS_CTX_CLIENT_ACCESSIBLE */
		app_info->SnapshotContext =
		    VSS_VOLSNAP_ATTR_PERSISTENT |
		    VSS_VOLSNAP_ATTR_CLIENT_ACCESSIBLE |
		    VSS_VOLSNAP_ATTR_NO_AUTO_RELEASE |
		    VSS_VOLSNAP_ATTR_NO_WRITERS;
		app_info->SnapshotCount = 1;

		Irp->IoStatus.Information = need;
		status = STATUS_SUCCESS;
		break;
	}

	case IOCTL_VOLSNAP_QUERY_EPIC: /* IOCTL_VOLSNAP function 0x14 */
	{
		/*
		 * VSS sends this to the snapshot device with a 0-byte (or tiny)
		 * output buffer; it is a fire-and-forget control IOCTL with no
		 * data returned.  STATUS_SUCCESS + Information=0 is correct.
		 */
		typedef struct _VOLSNAP_EPIC_INFO {
			ULONG	EpicNumber;
		} VOLSNAP_EPIC_INFO, *PVOLSNAP_EPIC_INFO;

		dprintf("VSS %s: IOCTL_VOLSNAP_QUERY_EPIC no-op\n", __func__);

		if (outlen >= sizeof (ULONG)) {
			*(ULONG *)buf = 0;
		}

		Irp->IoStatus.Information = 0;
		status = STATUS_SUCCESS;
		break;
	}

	case IOCTL_STORAGE_QUERY_PROPERTY:
	{
		/*
		 * Return a minimal STORAGE_DEVICE_DESCRIPTOR so that VSS and
		 * storage subsystems can identify this as a virtual disk
		 * device.
		 */
		dprintf("VSS %s: IOCTL_STORAGE_QUERY_PROPERTY\n", __func__);

		if (IrpSp->Parameters.DeviceIoControl.InputBufferLength <
		    sizeof (STORAGE_PROPERTY_QUERY)) {
			status = STATUS_INVALID_PARAMETER;
			break;
		}

		STORAGE_PROPERTY_QUERY *q = (STORAGE_PROPERTY_QUERY *)buf;
		STORAGE_PROPERTY_ID propid = q->PropertyId;
		STORAGE_QUERY_TYPE qtype = q->QueryType;

		if (qtype == PropertyExistsQuery) {
			/* Exists query: confirm StorageDeviceProperty exists */
			Irp->IoStatus.Information = 0;
			status = (propid == StorageDeviceProperty) ?
			    STATUS_SUCCESS : STATUS_NOT_SUPPORTED;
			break;
		}

		if (propid != StorageDeviceProperty) {
			status = STATUS_NOT_SUPPORTED;
			break;
		}

		/* PropertyStandardQuery for StorageDeviceProperty */
		ULONG needed = sizeof (STORAGE_DEVICE_DESCRIPTOR);
		if (outlen < sizeof (STORAGE_DESCRIPTOR_HEADER)) {
			Irp->IoStatus.Information = needed;
			status = STATUS_BUFFER_TOO_SMALL;
			break;
		}
		if (outlen < needed) {
			/* Return just the header */
			STORAGE_DESCRIPTOR_HEADER *h =
			    (STORAGE_DESCRIPTOR_HEADER *)buf;
			h->Version = sizeof (STORAGE_DEVICE_DESCRIPTOR);
			h->Size    = needed;
			Irp->IoStatus.Information =
			    sizeof (STORAGE_DESCRIPTOR_HEADER);
			status = STATUS_SUCCESS;
			break;
		}
		STORAGE_DEVICE_DESCRIPTOR *d = (STORAGE_DEVICE_DESCRIPTOR *)buf;
		RtlZeroMemory(d, needed);
		d->Version = sizeof (STORAGE_DEVICE_DESCRIPTOR);
		d->Size = needed;
		d->DeviceType = FILE_DEVICE_VIRTUAL_DISK;
		d->DeviceTypeModifier = 0;
		d->RemovableMedia = FALSE;
		d->CommandQueueing = FALSE;
		d->VendorIdOffset = 0;
		d->ProductIdOffset = 0;
		d->ProductRevisionOffset = 0;
		d->SerialNumberOffset = 0;
		/* BusTypeVirtual = 0x12 (Windows 8+) */
		d->BusType = (STORAGE_BUS_TYPE)0x12;
		d->RawPropertiesLength = 0;
		Irp->IoStatus.Information = needed;
		status = STATUS_SUCCESS;
		break;
	}

	case IOCTL_DISK_GET_PARTITION_INFO_EX: /* 0x70048 */
	{
		/*
		 * Snapshot devices are raw (no partition table).
		 * Return PARTITION_STYLE_RAW with the snapshot's logical size.
		 */
		dprintf("VSS %s: IOCTL_DISK_GET_PARTITION_INFO_EX\n", __func__);
		if (outlen < sizeof (PARTITION_INFORMATION_EX)) {
			Irp->IoStatus.Information =
			    sizeof (PARTITION_INFORMATION_EX);
			status = STATUS_BUFFER_TOO_SMALL;
			break;
		}
		PARTITION_INFORMATION_EX *pi = (PARTITION_INFORMATION_EX *)buf;
		RtlZeroMemory(pi, sizeof (*pi));
		pi->PartitionStyle = PARTITION_STYLE_RAW;
		pi->StartingOffset.QuadPart = 0;
		pi->PartitionLength.QuadPart =
		    (LONGLONG)zfs_vss_snap_refbytes(zmo);
		pi->PartitionNumber = 0;
		Irp->IoStatus.Information = sizeof (PARTITION_INFORMATION_EX);
		status = STATUS_SUCCESS;
		break;
	}

	case 0x2d1084: /* IOCTL_STORAGE_GET_DEVICE_NUMBER_EX (Win10 1709+) */
	{
		/*
		 * STORAGE_DEVICE_NUMBER_EX:
		 *   ULONG Version, Size, Flags, DeviceType, DeviceNumber;
		 *   GUID  DeviceGuid;
		 *   ULONG PartitionNumber;
		 * Total: 6 * ULONG + GUID = 24 + 16 = 40 bytes.
		 */
		dprintf("VSS %s: IOCTL_STORAGE_GET_DEVICE_NUMBER_EX\n",
		    __func__);
		const ULONG sz = 6 * sizeof (ULONG) + sizeof (GUID);
		if (outlen < sz) {
			Irp->IoStatus.Information = sz;
			status = STATUS_BUFFER_TOO_SMALL;
			break;
		}
		ULONG *p = (ULONG *)buf;
		RtlZeroMemory(p, sz);
		p[0] = sz;	/* Version */
		p[1] = sz;	/* Size */
		p[2] = 0;	/* Flags */
		p[3] = FILE_DEVICE_VIRTUAL_DISK; /* DeviceType */
		p[4] = (ULONG)(zmo->vss_guid & 0xffffffff); /* DeviceNumber */
		/* DeviceGuid: derive from vss_guid */
		GUID *g = (GUID *)(p + 5);
		g->Data1 = (ULONG)(zmo->vss_guid & 0xffffffff);
		g->Data2 = (USHORT)((zmo->vss_guid >> 32) & 0xffff);
		g->Data3 = (USHORT)((zmo->vss_guid >> 48) & 0xffff);
		/* Data4 already zeroed */
		p[9] = (ULONG)-1;	/* PartitionNumber */
		Irp->IoStatus.Information = sz;
		status = STATUS_SUCCESS;
		break;
	}

	case 0x2d0c14: /* IOCTL_STORAGE_GET_HOTPLUG_INFO (function 0x305) */
	{
		/*
		 * STORAGE_HOTPLUG_INFO: { ULONG Size; BOOLEAN x4; }
		 * Snapshot devices are fixed, non-removable virtual disks.
		 */
		dprintf("VSS %s: IOCTL_STORAGE_GET_HOTPLUG_INFO\n", __func__);
		const ULONG sz = sizeof (ULONG) + 4 * sizeof (BOOLEAN);
		if (outlen < sz) {
			Irp->IoStatus.Information = sz;
			status = STATUS_BUFFER_TOO_SMALL;
			break;
		}
		ULONG *shi = (ULONG *)buf;
		RtlZeroMemory(shi, sz);
		shi[0] = sz; /* Size field */
		/* MediaRemovable, MediaHotplug, DeviceHotplug = FALSE */
		Irp->IoStatus.Information = sz;
		status = STATUS_SUCCESS;
		break;
	}

	case 0x56c064: /* VOLUME function 0x19 r/w - not applicable to snaps */
	case 0x2d118c: /* STORAGE function 0x463 - not applicable to snaps */
	case 0x2d1190: /* STORAGE function 0x464 - not applicable to snaps */
		dprintf("VSS %s: optional ioctl 0x%lx not supported\n",
		    __func__, cmd);
		Irp->IoStatus.Information = 0;
		status = STATUS_NOT_SUPPORTED;
		break;

	case 0x4d0010: /* VOLMGR function 4 - not applicable to snapshots */
	case 0x4d0018: /* VOLMGR function 6 - not applicable to snapshots */
		dprintf("VSS %s: volmgr 0x%lx not supported\n", __func__, cmd);
		Irp->IoStatus.Information = 0;
		status = STATUS_NOT_SUPPORTED;
		break;

	default:
		dprintf("VSS %s: unhandled ioctl 0x%lx\n", __func__, cmd);
		Irp->IoStatus.Information = 0;
		status = STATUS_NOT_SUPPORTED;
		break;
	}

	dprintf("VSS %s: ioctl 0x%lx -> ntstatus 0x%lx info %lu\n",
	    __func__, cmd, (ULONG)status,
	    (ULONG)Irp->IoStatus.Information);

	return (status);
}

/*
 * IRP dispatcher for \Device\ZfsSnapshot<hex> devices.
 */
NTSTATUS
zfs_vss_dispatcher(PDEVICE_OBJECT DeviceObject, PIRP *pIrp,
    PIO_STACK_LOCATION IrpSp)
{
	PIRP Irp = *pIrp;
	NTSTATUS status;

	mount_t *zmo = DeviceObject->DeviceExtension;
	/*
	 * vcb_devobj: the real VCB device object after lazy mount.
	 * May be NULL if the snapshot has not been mounted yet.
	 * Use it in fsDispatcher calls so that it sees a fully-initialised
	 * MOUNT_TYPE_VCB mount_t; fall back to DeviceObject (MOUNT_TYPE_VSS)
	 * if not yet available.
	 */
	PDEVICE_OBJECT vcb_devobj = zfs_vss_vcb_devobj(zmo);

	switch (IrpSp->MajorFunction) {

	case IRP_MJ_CREATE:
	{
		/*
		 * Read del_pending and fsprivate together under
		 * vss_mount_lock.
		 *
		 * vss_del_pending: set by zfs_vss_snapshot_remove before
		 * IoDeleteDevice.  We must reject new opens here to avoid the
		 * IopDecrementDeviceObjectRef -> IopCompleteUnloadOrDelete ->
		 * IopInsertRemoveDevice double-removal BSOD: IoDeleteDevice
		 * removes the device from the driver list immediately when
		 * DeviceObject->ReferenceCount is 0; if PnP then opens a
		 * file via a cached PDEVICE_OBJECT pointer and closes it,
		 * the ref decrement to 0 fires IopInsertRemoveDevice a
		 * second time on a device no longer in the list, crashing
		 * at NULL->NextDevice.
		 *
		 * fsprivate: zfs_domount sets it early while holding the
		 * lock, so reading without the lock could race with a
		 * half-initialised zfsvfs and create znodes that outlive
		 * the failed mount.
		 */
		mutex_enter(&zmo->vss_mount_lock);
		boolean_t del_pending = zmo->vss_del_pending;
		boolean_t vss_mounted = (vfs_fsprivate(zmo) != NULL);
		mutex_exit(&zmo->vss_mount_lock);
		if (del_pending) {
			dprintf("VSS %s: IRP_MJ_CREATE rejected"
			    " (delete pending)\n", __func__);
			Irp->IoStatus.Information = 0;
			status = STATUS_DELETE_PENDING;
			break;
		}
		/*
		 * Kernel-mode opens (MountMgr, PnP, filter drivers) must never
		 * trigger a lazy snapshot mount.  User-mode opens (twext.dll /
		 * Previous Versions) need a real filesystem open so
		 * that QUERY_SECURITY / QUERY_INFORMATION succeed.
		 *
		 * An empty FileName or bare "\" without FILE_DIRECTORY_FILE is
		 * also treated as a device open: srv2.sys uses that pattern to
		 * send IOCTL_VOLSNAP_* -- forwarding to fsDispatcher would set
		 * FsContext = vnode, causing IOCTLs to miss
		 * zfs_vss_device_control.
		 */
		{
			PUNICODE_STRING fn = &IrpSp->FileObject->FileName;
			boolean_t is_dir = (IrpSp->Parameters.Create.Options &
			    FILE_DIRECTORY_FILE) != 0;
			boolean_t is_dev_open = (fn->Length == 0 ||
			    (fn->Length == sizeof (WCHAR) &&
			    fn->Buffer[0] == L'\\' && !is_dir) ||
			    Irp->RequestorMode == KernelMode);
			if (is_dev_open) {
				dprintf("VSS %s: IRP_MJ_CREATE device open"
				    " (no mount)\n", __func__);
				Irp->IoStatus.Status = STATUS_SUCCESS;
				Irp->IoStatus.Information = FILE_OPENED;
				IoCompleteRequest(Irp, IO_NO_INCREMENT);
				*pIrp = NULL;
				return (STATUS_SUCCESS);
			}
		}

		if (vss_mounted) {
			/* Real filesystem open; forward to VCB device. */
			PDEVICE_OBJECT fwd = zfs_vss_vcb_devobj(zmo);
			dprintf("VSS %s: IRP_MJ_CREATE -> fsDispatcher"
			    " (mounted)\n", __func__);
			return (fsDispatcher(fwd ? fwd : DeviceObject,
			    pIrp, IrpSp));
		}

		/*
		 * File-path open — pend and schedule a work item to mount,
		 * then replay via fsDispatcher.
		 */
		dprintf("VSS %s: IRP_MJ_CREATE -> lazy mount\n", __func__);
		zfs_vss_mount_wi_t *wi =
		    kmem_alloc(sizeof (*wi), KM_NOSLEEP);
		if (wi == NULL) {
			status = STATUS_INSUFFICIENT_RESOURCES;
			break;
		}
		wi->wi_item = IoAllocateWorkItem(DeviceObject);
		if (wi->wi_item == NULL) {
			kmem_free(wi, sizeof (*wi));
			status = STATUS_INSUFFICIENT_RESOURCES;
			break;
		}
		wi->wi_devobj = DeviceObject;
		wi->wi_irp    = Irp;
		IoMarkIrpPending(Irp);
		IoQueueWorkItem(wi->wi_item, zfs_vss_mount_workitem,
		    DelayedWorkQueue, wi);
		*pIrp = NULL; /* work item owns the IRP now */
		return (STATUS_PENDING);
	}

	case IRP_MJ_CLEANUP:
		if (vfs_fsprivate(zmo) != NULL &&
		    IrpSp->FileObject != NULL &&
		    IrpSp->FileObject->FsContext != NULL) {
			return (fsDispatcher(vcb_devobj ?
			    vcb_devobj : DeviceObject, pIrp, IrpSp));
		}
		dprintf("VSS %s: IRP_MJ_CLEANUP\n", __func__);
		Irp->IoStatus.Information = 0;
		status = STATUS_SUCCESS;
		break;

	case IRP_MJ_CLOSE:
		if (vfs_fsprivate(zmo) != NULL &&
		    IrpSp->FileObject != NULL &&
		    IrpSp->FileObject->FsContext != NULL) {
			return (fsDispatcher(vcb_devobj ?
			    vcb_devobj : DeviceObject, pIrp, IrpSp));
		}
		dprintf("VSS %s: IRP_MJ_CLOSE\n", __func__);
		Irp->IoStatus.Information = 0;
		status = STATUS_SUCCESS;
		break;

	case IRP_MJ_DEVICE_CONTROL:
		status = zfs_vss_device_control(DeviceObject, Irp, IrpSp);
		break;

	case IRP_MJ_READ:
		/*
		 * If a file is open (FsContext set), delegate to fsDispatcher.
		 * Otherwise this is a raw sector read from VSS probing the
		 * device; return zeroed data.
		 */
		if (vfs_fsprivate(zmo) != NULL &&
		    IrpSp->FileObject != NULL &&
		    IrpSp->FileObject->FsContext != NULL) {
			return (fsDispatcher(vcb_devobj ?
			    vcb_devobj : DeviceObject, pIrp, IrpSp));
		}
		{
			ULONG rdlen = IrpSp->Parameters.Read.Length;
			dprintf("VSS %s: IRP_MJ_READ (raw) off=%lld len=%lu\n",
			    __func__,
			    IrpSp->Parameters.Read.ByteOffset.QuadPart, rdlen);
			if (rdlen > 0 && Irp->MdlAddress != NULL) {
				void *kbuf = MmGetSystemAddressForMdlSafe(
				    Irp->MdlAddress, NormalPagePriority);
				if (kbuf != NULL) {
					RtlZeroMemory(kbuf, rdlen);
					Irp->IoStatus.Information = rdlen;
					status = STATUS_SUCCESS;
					break;
				}
			}
			Irp->IoStatus.Information = 0;
			status = STATUS_SUCCESS;
		}
		break;

	case IRP_MJ_WRITE:
	case IRP_MJ_SET_INFORMATION:
	case IRP_MJ_QUERY_EA:
	case IRP_MJ_SET_EA:
	case IRP_MJ_QUERY_SECURITY:
	{
		if (vfs_fsprivate(zmo) != NULL) {
			dprintf("VSS %s: QUERY_SECURITY -> fsDispatcher\n",
			    __func__);
			return (fsDispatcher(vcb_devobj ?
			    vcb_devobj : DeviceObject, pIrp, IrpSp));
		}
		if (!vfs_main_lock_write_held()) {
			zfsvfs_t *tmp_zfsvfs;
			if (getzfsvfs(zmo->vss_snapname, &tmp_zfsvfs) == 0) {
				mutex_enter(&zmo->vss_mount_lock);
				if (vfs_fsprivate(zmo) == NULL) {
					tmp_zfsvfs->z_mimic = ZFS_MIMIC_NTFS;
					vfs_setfsprivate(zmo, tmp_zfsvfs);
				}
				mutex_exit(&zmo->vss_mount_lock);
				PDEVICE_OBJECT fwd = zfs_vss_vcb_devobj(zmo);
				NTSTATUS st = fsDispatcher(
				    fwd ? fwd : DeviceObject, pIrp, IrpSp);
				vfs_unbusy(tmp_zfsvfs->z_vfs);
				return (st);
			}
		}
		/*
		 * Snapshot not mounted: return the device object's own security
		 * descriptor.  twext.dll (Previous Versions) checks the SD to
		 * decide whether to show the snapshot entry;
		 * STATUS_INVALID_DEVICE_REQUEST silently drops it.
		 */
		{
			PSECURITY_DESCRIPTOR pDevSD = NULL;
			BOOLEAN memAlloc = FALSE;
			NTSTATUS sd_st = ObGetObjectSecurity(DeviceObject,
			    &pDevSD, &memAlloc);
			if (NT_SUCCESS(sd_st)) {
				ULONG outLen =
				    IrpSp->Parameters.QuerySecurity.Length;
				sd_st = SeQuerySecurityDescriptorInfo(
				    &IrpSp->Parameters.QuerySecurity.
				    SecurityInformation,
				    (PSECURITY_DESCRIPTOR)Irp->UserBuffer,
				    &outLen,
				    &pDevSD);
				Irp->IoStatus.Information = outLen;
				if (memAlloc)
					ObReleaseObjectSecurity(pDevSD, TRUE);
				status = sd_st;
			} else {
				dprintf("VSS %s: QUERY_SECURITY unmounted"
				    " ObGetObjectSecurity failed 0x%x\n",
				    __func__, (unsigned)sd_st);
				Irp->IoStatus.Information = 0;
				status = sd_st;
			}
		}
		break;
	}

	case IRP_MJ_FLUSH_BUFFERS:
	case IRP_MJ_QUERY_VOLUME_INFORMATION:
	case IRP_MJ_SET_VOLUME_INFORMATION:
	case IRP_MJ_DIRECTORY_CONTROL:
	case IRP_MJ_FILE_SYSTEM_CONTROL:
	case IRP_MJ_LOCK_CONTROL:
	case IRP_MJ_SET_SECURITY:
		if (vfs_fsprivate(zmo) != NULL) {
			dprintf("VSS %s: major 0x%x -> fsDispatcher\n",
			    __func__, IrpSp->MajorFunction);
			return (fsDispatcher(vcb_devobj ?
			    vcb_devobj : DeviceObject, pIrp, IrpSp));
		}
		/*
		 * Snapshot not yet mounted via the VSS path -- check
		 * whether it was mounted externally ("zfs mount").
		 * Hold the READER ref from getzfsvfs across fsDispatcher
		 * so the snapshot cannot be torn down.  Releasing before
		 * fsDispatcher would open a window where zfsctl_unmount_thread
		 * acquires the WRITER lock and destroys the mutexes under us.
		 */
		if (!vfs_main_lock_write_held()) {
			zfsvfs_t *tmp_zfsvfs;
			if (getzfsvfs(zmo->vss_snapname, &tmp_zfsvfs) == 0) {
				mutex_enter(&zmo->vss_mount_lock);
				if (vfs_fsprivate(zmo) == NULL) {
					tmp_zfsvfs->z_mimic = ZFS_MIMIC_NTFS;
					vfs_setfsprivate(zmo, tmp_zfsvfs);
				}
				mutex_exit(&zmo->vss_mount_lock);
				PDEVICE_OBJECT fwd = zfs_vss_vcb_devobj(zmo);
				dprintf("VSS %s: major 0x%x -> fsDispatcher"
				    " (external mount)\n",
				    __func__, IrpSp->MajorFunction);
				NTSTATUS st = fsDispatcher(
				    fwd ? fwd : DeviceObject, pIrp, IrpSp);
				vfs_unbusy(tmp_zfsvfs->z_vfs);
				return (st);
			}
		}
		dprintf("VSS %s: major 0x%x unmounted\n",
		    __func__, IrpSp->MajorFunction);
		Irp->IoStatus.Information = 0;
		status = STATUS_INVALID_DEVICE_REQUEST;
		break;

	case IRP_MJ_QUERY_INFORMATION:
		if (vfs_fsprivate(zmo) != NULL) {
			dprintf("VSS %s: IRP_MJ_QUERY_INFORMATION"
			    " -> fsDispatcher\n", __func__);
			return (fsDispatcher(vcb_devobj ?
			    vcb_devobj : DeviceObject, pIrp, IrpSp));
		}
		/*
		 * Unmounted: answer creation-time queries used by srv2.sys
		 * when it probes the snapshot device for AppInfo.
		 */
		{
			ULONG cls =
			    IrpSp->Parameters.QueryFile.FileInformationClass;
			void *ibuf = Irp->AssociatedIrp.SystemBuffer;
			LARGE_INTEGER ct;
			TIME_UNIX_TO_WINDOWS_EX(zmo->vss_creation, 0,
			    ct.QuadPart);

			if (cls == FileBasicInformation) {
				if (IrpSp->Parameters.QueryFile.Length <
				    sizeof (FILE_BASIC_INFORMATION)) {
					Irp->IoStatus.Information =
					    sizeof (FILE_BASIC_INFORMATION);
					status = STATUS_BUFFER_TOO_SMALL;
				} else {
					FILE_BASIC_INFORMATION *fbi = ibuf;
					RtlZeroMemory(fbi, sizeof (*fbi));
					fbi->CreationTime = ct;
					fbi->LastAccessTime = ct;
					fbi->LastWriteTime = ct;
					fbi->ChangeTime = ct;
					fbi->FileAttributes =
					    FILE_ATTRIBUTE_READONLY;
					Irp->IoStatus.Information =
					    sizeof (FILE_BASIC_INFORMATION);
					status = STATUS_SUCCESS;
				}
			} else if (cls == FileNetworkOpenInformation) {
				FILE_NETWORK_OPEN_INFORMATION *fnoi = ibuf;
				if (IrpSp->Parameters.QueryFile.Length <
				    sizeof (*fnoi)) {
					Irp->IoStatus.Information =
					    sizeof (*fnoi);
					status = STATUS_BUFFER_TOO_SMALL;
				} else {
					RtlZeroMemory(fnoi, sizeof (*fnoi));
					fnoi->CreationTime = ct;
					fnoi->LastAccessTime = ct;
					fnoi->LastWriteTime = ct;
					fnoi->ChangeTime = ct;
					fnoi->AllocationSize.QuadPart =
					    (LONGLONG)zfs_vss_snap_refbytes(
					    zmo);
					fnoi->EndOfFile =
					    fnoi->AllocationSize;
					fnoi->FileAttributes =
					    FILE_ATTRIBUTE_READONLY;
					Irp->IoStatus.Information =
					    sizeof (*fnoi);
					status = STATUS_SUCCESS;
				}
			} else if (cls == FileInternalInformation) {
				FILE_INTERNAL_INFORMATION *fii = ibuf;
				if (IrpSp->Parameters.QueryFile.Length <
				    sizeof (*fii)) {
					Irp->IoStatus.Information =
					    sizeof (*fii);
					status = STATUS_BUFFER_TOO_SMALL;
				} else {
					fii->IndexNumber.QuadPart =
					    (LONGLONG)zmo->vss_guid;
					Irp->IoStatus.Information =
					    sizeof (*fii);
					status = STATUS_SUCCESS;
				}
			} else {
				dprintf("VSS %s: QUERY_INFO unmounted"
				    " cls=%lu\n", __func__, cls);
				Irp->IoStatus.Information = 0;
				status = STATUS_INVALID_DEVICE_REQUEST;
			}
		}
		break;

	default:
		dprintf("VSS %s: unhandled major 0x%x\n",
		    __func__, IrpSp->MajorFunction);
		Irp->IoStatus.Information = 0;
		status = STATUS_INVALID_DEVICE_REQUEST;
		break;
	}

	Irp->IoStatus.Status = status;
	IoCompleteRequest(Irp, IO_NO_INCREMENT);
	*pIrp = NULL;
	return (status);
}
