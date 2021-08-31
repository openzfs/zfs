/*
 * Module Name:  scsi.c
 * Project:      CppWDKStorPortVirtualMiniport
 *
 * Copyright (c) Microsoft Corporation.
 *
 * a.       ScsiExecuteMain()
 * Handles SCSI SRBs with opcodes needed to support file system operations by
 * calling subroutines. Fails SRBs with other opcodes.
 * Note: In a real-world virtual miniport, it may be necessary to handle
 * other opcodes.
 *
 * b.      ScsiOpInquiry()
 * Handles Inquiry, including creating a new LUN as needed.
 *
 * c.       ScsiOpVPD()
 * Handles Vital Product Data.
 *
 * d.      ScsiOpRead()
 * Beginning of a SCSI Read operation.
 *
 * e.      ScsiOpWrite()
 * Beginning of a SCSI Write operation.
 *
 * f.        ScsiReadWriteSetup()
 * Sets up a work element for SCSI Read or Write and enqueues the element.
 *
 * g.       ScsiOpReportLuns()
 * Handles Report LUNs.
 *
 *
 * This source is subject to the Microsoft Public License.
 * See http://www.microsoft.com/opensource/licenses.mspx#Ms-PL.
 * All other rights reserved.
 *
 * THIS CODE AND INFORMATION IS PROVIDED "AS IS" WITHOUT WARRANTY OF ANY KIND,
 * EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A PARTICULAR PURPOSE.
 */

#include <sys/debug.h>
#include <ntddk.h>
#include <storport.h>

#include <sys/zfs_context.h>
#include <sys/wzvol.h>

#pragma warning(push)
#pragma warning(disable : 4204)
#include <stortrce.h>
#pragma warning(pop)

#include <sys/spa.h>
#include <sys/zfs_rlock.h>
#include <sys/dataset_kstats.h>
#include <sys/zil.h>
#include <sys/zvol.h>
#include <sys/zvol_impl.h>
#include <sys/zvol_os.h>

/*
 * We have a list of ZVOLs, and we receive incoming (Target, Lun)
 * requests that needs to be mapped to the correct "zv" ptr.
 *
 * ssv-18807: fixed the race condition in the zvol destroy processing
 * by adding remove lock logic to ensure no new I/O can be processed
 * from the front end (StorPort) and all outstanding host I/Os have
 * left the pipeline.
 *
 * The zv control block starts to get protected in wzvol_assign_targetid()
 * and this until wzvol_clear_targetid() is called.
 *
 * Once wzvol_find_target(t,l) returns a valid pointer to the zv,
 * that zv is protected via an extra reference on its remove lock so
 * it can't be freed unless all references on it are cleared.
 * It is the caller's responsibility to clear the extra reference it got
 * by calling wzvol_unlock_target(zv).
 *
 * wzvol_find_target(t,l) will take an extra reference each time its
 * called so each of those will need their wzvol_unlock_target(zv)
 * counterpart call.
 *
 * The wzvol_lock_target(zv) call is commented out because not used yet
 * but its purpose is for when nested extra references need to be taken
 * on the zv after wzvol_find_target(t,l) was called.  That can be useful
 * for when asynchronous processing (queueing) involving the zv control
 * block need to make sure that zv stays allocated.
 *
 * When the zvol is destroyed the wzvol_clear_targetid(t,l,zv) will
 * actively wait for all references to be released and no new one can
 * be taken.
 *
 * programming notes: the remove lock must be dynamically allocated
 * because it cannot be reinitialized. An interlocked refcnt variable
 * is also necessary to protect the remove lock control block's allocation.
 * when the refcnt reaches 0 it is safe to free the remove lock cb.
 */
extern wzvolDriverInfo STOR_wzvolDriverInfo;
extern taskq_t *zvol_taskq;

inline int
resolveArrayIndex(int t, int l, int nbL)
{
	return ((t * nbL) + l);
}

static inline void
wzvol_decref_target(wzvolContext* zvc)
{
	if (atomic_dec_64_nv(&zvc->refCnt) == 0) {
		PIO_REMOVE_LOCK pIoRemLock = zvc->pIoRemLock;
		ASSERT(pIoRemLock != NULL);
		// when refCnt is 0 we can free the remove lock block.
		// All IoReleaseRemoveLock have been called.
		atomic_cas_ptr(&zvc->pIoRemLock, pIoRemLock, NULL);
		kmem_free(pIoRemLock, sizeof (*pIoRemLock));
	}
}

/*
 * not used now but left for completeness in case we need to have
 * an extra reference after calling find_targetid
 */
static inline BOOLEAN
wzvol_lock_target(zvol_state_t *zv)
{
	wzvolContext *zvc = (pwzvolContext)zv->zv_zso->zso_target_context;
	if (zvc) {
		if (atomic_inc_64_nv(&zvc->refCnt) > 1) {
			// safe to access the remove lock.
			// Make sure we are on the same zv.
			if (zvc->zv == zv) {
				if (STATUS_SUCCESS ==
				    IoAcquireRemoveLock(zvc->pIoRemLock, zv))
					return (TRUE);
				else
					wzvol_decref_target(zvc);
			} else
				wzvol_decref_target(zvc);
		} else
			atomic_dec_64_nv(&zvc->refCnt);
	}
	return (FALSE);
}

static inline void
wzvol_unlock_target(zvol_state_t *zv)
{
	wzvolContext* zvc = (pwzvolContext)zv->zv_zso->zso_target_context;
	IoReleaseRemoveLock(zvc->pIoRemLock, zv);
	wzvol_decref_target(zvc);
}

int
wzvol_assign_targetid(zvol_state_t *zv)
{
	wzvolContext* zv_targets = STOR_wzvolDriverInfo.zvContextArray;
	ASSERT(zv->zv_zso->zso_target_context == NULL);
	PIO_REMOVE_LOCK pIoRemLock = kmem_zalloc(sizeof (*pIoRemLock),
	    KM_SLEEP);
	if (!pIoRemLock) {
		dprintf("ZFS: Unable to assign targetid - out of memory.\n");
		ASSERT("Unable to assign targetid - out of memory.");
		return (0);
	}

	IoInitializeRemoveLock(pIoRemLock, 'KLRZ', 0, 0);

	if (STATUS_SUCCESS != IoAcquireRemoveLock(pIoRemLock, zv)) {
		dprintf("ZFS: Unable to assign targetid - can't acquire "
		    "the remlock.\n");
		ASSERT("Unable to assign targetid - can't acquire remlock.");
	} else {
		for (uint8_t l = 0;
		    l < STOR_wzvolDriverInfo.MaximumNumberOfLogicalUnits;
		    l++) {
			for (uint8_t t = 0;
			    t < STOR_wzvolDriverInfo.MaximumNumberOfTargets;
			    t++) {
				int zvidx = resolveArrayIndex(t, l,
				    STOR_wzvolDriverInfo.
				    MaximumNumberOfLogicalUnits);

				if (zv_targets[zvidx].zv == NULL &&
				    zv_targets[zvidx].pIoRemLock == NULL) {
					if (atomic_inc_64_nv(
					    &zv_targets[zvidx].refCnt) == 1) {
						// brand new entry - got it.
						ASSERT(
						    zv_targets[zvidx].pIoRemLock
						    == NULL);
						zv->zv_zso->zso_target_id = t;
						zv->zv_zso->zso_lun_id = l;
						zv->zv_zso->zso_target_context =
						    &zv_targets[zvidx];
						zv_targets[zvidx].pIoRemLock =
						    pIoRemLock;
						atomic_cas_ptr(
						    &zv_targets[zvidx].zv,
						    NULL, zv);
						// zv is now searchable
						return (1);
					} else {
						// assign_targetid collision
						// (very rare)
						wzvol_decref_target(
						    &zv_targets[zvidx]);
					}
				}
			}
		}
		IoReleaseRemoveLock(pIoRemLock, zv);
	}

	kmem_free(pIoRemLock, sizeof (*pIoRemLock));
	dprintf("ZFS: Unable to assign targetid - out of room.\n");
	ASSERT("Unable to assign targetid - out of room.");
	return (0);
}

/*
 * note: find_target will lock the zv's remove lock. caller
 * is responsible to unlock_target if a non-NULL zv pointer is returned
 */
static inline zvol_state_t *
wzvol_find_target(uint8_t targetid, uint8_t lun)
{
	wzvolContext *zv_targets = STOR_wzvolDriverInfo.zvContextArray;
	ASSERT(targetid < STOR_wzvolDriverInfo.MaximumNumberOfTargets);
	ASSERT(lun < STOR_wzvolDriverInfo.MaximumNumberOfLogicalUnits);
	if (targetid < STOR_wzvolDriverInfo.MaximumNumberOfTargets &&
	    lun < STOR_wzvolDriverInfo.MaximumNumberOfLogicalUnits) {
		int zvidx = resolveArrayIndex(targetid, lun,
		    STOR_wzvolDriverInfo.MaximumNumberOfLogicalUnits);
		zvol_state_t *zv = zv_targets[zvidx].zv;
		if (zv) {
			if (atomic_inc_64_nv(&zv_targets[zvidx].refCnt) > 1) {
				// safe to access the remove lock
				if (STATUS_SUCCESS ==
				    IoAcquireRemoveLock(
				    zv_targets[zvidx].pIoRemLock,
				    zv))
					return ((zvol_state_t *)
					    zv_targets[zvidx].zv);
				else
					wzvol_decref_target(&zv_targets[zvidx]);
			} else
				atomic_dec_64_nv(&zv_targets[zvidx].refCnt);
		} // nothing in that t-l
	}
	return (NULL);
}


void
wzvol_clear_targetid(uint8_t targetid, uint8_t lun, zvol_state_t *zv)
{
	wzvolContext *zvc = (pwzvolContext)zv->zv_zso->zso_target_context;

	ASSERT(KeGetCurrentIrql() < DISPATCH_LEVEL);
	ASSERT(targetid < STOR_wzvolDriverInfo.MaximumNumberOfTargets);
	ASSERT(lun < STOR_wzvolDriverInfo.MaximumNumberOfLogicalUnits);
	if (targetid < STOR_wzvolDriverInfo.MaximumNumberOfTargets &&
	    lun < STOR_wzvolDriverInfo.MaximumNumberOfLogicalUnits) {
		/*
		 * make sure no new I/O can enter the front-end + all
		 * outstanding I/Os are completed (ssv-18807).
		 */
		if (atomic_cas_ptr(
		    &STOR_wzvolDriverInfo.zvContextArray[resolveArrayIndex(
		    targetid, lun,
		    STOR_wzvolDriverInfo.MaximumNumberOfLogicalUnits)].zv,
		    zv, NULL) == zv) {
			IoReleaseRemoveLockAndWait(zvc->pIoRemLock, zv);
			wzvol_decref_target(zvc);
		}
	}
}

UCHAR
ScsiExecuteMain(
	__in pHW_HBA_EXT pHBAExt,
	__in PSCSI_REQUEST_BLOCK pSrb,
	__in PUCHAR pResult)
{
	UCHAR status = SRB_STATUS_INVALID_REQUEST;

	TraceEvent(TRACE_VERBOSE, "%s:%d: ScsiExecute: pSrb = 0x%p, CDB = 0x%x"
	    " Path: %x TID: %x Lun: %x\n", __func__, __LINE__, pSrb,
	    pSrb->Cdb[0], pSrb->PathId, pSrb->TargetId, pSrb->Lun);
	*pResult = ResultDone;

	// Verify that the B/T/L is not out of bound.
	if (pSrb->PathId > 0) {
		status = SRB_STATUS_INVALID_PATH_ID;
		goto Done;
	} else if (pSrb->TargetId >=
	    STOR_wzvolDriverInfo.MaximumNumberOfTargets) {
		status = SRB_STATUS_INVALID_TARGET_ID;
		goto Done;
	} else if (pSrb->Lun >=
	    STOR_wzvolDriverInfo.MaximumNumberOfLogicalUnits) {
		status = SRB_STATUS_INVALID_LUN;
		goto Done;
	}

	// Handle sufficient opcodes to support a LUN suitable
	// for a file system. Other opcodes are failed.
	switch (pSrb->Cdb[0]) {

		case SCSIOP_TEST_UNIT_READY:
		case SCSIOP_SYNCHRONIZE_CACHE:
		case SCSIOP_START_STOP_UNIT:
		case SCSIOP_VERIFY:
			status = SRB_STATUS_SUCCESS;
			break;

		case SCSIOP_INQUIRY:
			status = ScsiOpInquiry(pHBAExt, pSrb);
			break;

		case SCSIOP_READ_CAPACITY:
			status = ScsiOpReadCapacity(pHBAExt, pSrb);
			break;

		case SCSIOP_READ_CAPACITY16:
			status = ScsiOpReadCapacity16(pHBAExt, pSrb);
			break;

		case SCSIOP_READ:
		case SCSIOP_READ16:
			status = ScsiOpRead(pHBAExt, pSrb, pResult);
			break;

		case SCSIOP_WRITE:
		case SCSIOP_WRITE16:
			status = ScsiOpWrite(pHBAExt, pSrb, pResult);
			break;

		case SCSIOP_MODE_SENSE:
			status = ScsiOpModeSense(pHBAExt, pSrb);
			break;

		case SCSIOP_REPORT_LUNS:
			status = ScsiOpReportLuns(pHBAExt, pSrb);
			break;

		case SCSIOP_UNMAP:
			status = ScsiOpUnmap(pHBAExt, pSrb, pResult);
			break;

		default:
			status = SRB_STATUS_INVALID_REQUEST;
			break;

	} // switch (pSrb->Cdb[0])

Done:
	return (status);
}

pHW_LU_EXTENSION_MPIO
ScsiGetMPIOExt(
	__in pHW_HBA_EXT pHBAExt,
	__in pHW_LU_EXTENSION pLUExt,
	__in PSCSI_REQUEST_BLOCK pSrb)
{
	pHW_LU_EXTENSION_MPIO pLUMPIOExt = NULL;
#if defined(_AMD64_)
	KLOCK_QUEUE_HANDLE LockHandle,
	    LockHandle2;
#else
	KIRQL SaveIrql,
	    SaveIrql2;
#endif
	PLIST_ENTRY pNextEntry;

#if defined(_AMD64_)
	KeAcquireInStackQueuedSpinLock(&pHBAExt->pwzvolDrvObj->MPIOExtLock,
	    &LockHandle);
#else
	KeAcquireSpinLock(&pHBAExt->pwzvolDrvObj->MPIOExtLock, &SaveIrql);
#endif

	for (pNextEntry = pHBAExt->pwzvolDrvObj->ListMPIOExt.Flink;
	    pNextEntry != &pHBAExt->pwzvolDrvObj->ListMPIOExt;
	    pNextEntry = pNextEntry->Flink) {
		pLUMPIOExt = CONTAINING_RECORD(pNextEntry,
		    HW_LU_EXTENSION_MPIO, List);

		if (pSrb->PathId == pLUMPIOExt->ScsiAddr.PathId &&
		    pSrb->TargetId == pLUMPIOExt->ScsiAddr.TargetId &&
		    pSrb->Lun == pLUMPIOExt->ScsiAddr.Lun) {
			break;
		}
	}

	if (pNextEntry == &pHBAExt->pwzvolDrvObj->ListMPIOExt) {
		pLUMPIOExt = ExAllocatePoolWithTag(NonPagedPoolNx,
		    sizeof (HW_LU_EXTENSION_MPIO), MP_TAG_GENERAL);

		if (!pLUMPIOExt) {
			dprintf("Failed to allocate HW_LU_EXTENSION_MPIO\n");
			goto Done;
		}

		RtlZeroMemory(pLUMPIOExt, sizeof (HW_LU_EXTENSION_MPIO));

		pLUMPIOExt->ScsiAddr.PathId = pSrb->PathId;
		pLUMPIOExt->ScsiAddr.TargetId = pSrb->TargetId;
		pLUMPIOExt->ScsiAddr.Lun = pSrb->Lun;

		KeInitializeSpinLock(&pLUMPIOExt->LUExtMPIOLock);

		InitializeListHead(&pLUMPIOExt->LUExtList);

		// ScsiAllocDiskBuf(pHBAExt,
		// &pLUMPIOExt->pDiskBuf, &pLUExt->MaxBlocks);

		if (!pLUMPIOExt->pDiskBuf) {
			dprintf("Failed to allocate DiskBuf\n");
			ExFreePoolWithTag(pLUMPIOExt, MP_TAG_GENERAL);
			pLUMPIOExt = NULL;

			goto Done;
		}

		InsertTailList(&pHBAExt->pwzvolDrvObj->ListMPIOExt,
		    &pLUMPIOExt->List);

		pHBAExt->pwzvolDrvObj->DrvInfoNbrMPIOExtObj++;
	} else {
		pLUExt->MaxBlocks =
		    (USHORT)(pHBAExt->pwzvolDrvObj->
		    wzvolRegInfo.PhysicalDiskSize / MP_BLOCK_SIZE);
	}

Done:
	if (pLUMPIOExt) {

#if defined(_AMD64_)
		KeAcquireInStackQueuedSpinLock(&pLUMPIOExt->LUExtMPIOLock,
		    &LockHandle2);
#else
		KeAcquireSpinLock(&pLUMPIOExt->LUExtMPIOLock, &SaveIrql2);
#endif

		pLUExt->pLUMPIOExt = pLUMPIOExt;
		pLUExt->pDiskBuf = pLUMPIOExt->pDiskBuf;

		InsertTailList(&pLUMPIOExt->LUExtList, &pLUExt->MPIOList);
		pLUMPIOExt->NbrRealLUNs++;

#if defined(_AMD64_)
		KeReleaseInStackQueuedSpinLock(&LockHandle2);
#else
		KeReleaseSpinLock(&pLUMPIOExt->LUExtMPIOLock, SaveIrql2);
#endif
	}

#if defined(_AMD64_)
	KeReleaseInStackQueuedSpinLock(&LockHandle);
#else
	KeReleaseSpinLock(&pHBAExt->pwzvolDrvObj->MPIOExtLock, SaveIrql);
#endif

	return (pLUMPIOExt);
}

UCHAR
ScsiOpInquiry(
	__in pHW_HBA_EXT pHBAExt,
	__in PSCSI_REQUEST_BLOCK pSrb)
{
	UCHAR status = SRB_STATUS_SUCCESS;
	zvol_state_t *zv = NULL;

	if (pHBAExt->bDontReport) {
		status = SRB_STATUS_NO_DEVICE;
		goto out;
	}

	zv = wzvol_find_target(pSrb->TargetId, pSrb->Lun);
	if (NULL == zv) {
		dprintf("Unable to get zv context for device %d:%d:%d\n",
		    pSrb->PathId, pSrb->TargetId, pSrb->Lun);
		status = SRB_STATUS_NO_DEVICE;
		goto out;
	}

	ASSERT(pSrb->DataTransferLength > 0);
	if (0 == pSrb->DataTransferLength) {
		status = SRB_STATUS_DATA_OVERRUN;
		goto out;
	}

	RtlZeroMemory((PUCHAR)pSrb->DataBuffer, pSrb->DataTransferLength);

	if (1 == ((PCDB)pSrb->Cdb)->CDB6INQUIRY3.EnableVitalProductData) {
		status = ScsiOpVPD(pHBAExt, pSrb, zv);
	} else {
		PINQUIRYDATA pInqData = pSrb->DataBuffer;
		// Claim SCSI-3 commands support
		pInqData->DeviceType = DISK_DEVICE;
		pInqData->DeviceTypeQualifier = DEVICE_CONNECTED;
		pInqData->ResponseDataFormat = 2;
		pInqData->Versions = 5;
		pInqData->RemovableMedia = FALSE;
		pInqData->CommandQueue = TRUE;

		RtlMoveMemory(pInqData->VendorId, pHBAExt->VendorId, 8);
		RtlMoveMemory(pInqData->ProductId, pHBAExt->ProductId, 16);
		RtlMoveMemory(pInqData->ProductRevisionLevel,
		    pHBAExt->ProductRevision, 4);
		memset((PCHAR)pInqData->VendorSpecific, ' ',
		    sizeof (pInqData->VendorSpecific));
		sprintf(pInqData->VendorSpecific, "%.04d-%.04d-%.04d",
		    pSrb->PathId, pSrb->TargetId, pSrb->Lun);
		pInqData->VendorSpecific[strlen(pInqData->VendorSpecific)] =
		    ' ';

		pInqData->AdditionalLength = sizeof (*pInqData) - 4;
	}

out:
	if (zv)
		wzvol_unlock_target(zv);
	return (status);
}

UCHAR
ScsiOpVPD(
	__in pHW_HBA_EXT pHBAExt,
	__in PSCSI_REQUEST_BLOCK pSrb,
	__in PVOID zvContext)
{
	UCHAR status = SRB_STATUS_SUCCESS;
	ULONG len = 0;
	zvol_state_t *zv = (zvol_state_t *)zvContext;

	switch (((struct _CDB6INQUIRY3 *)&pSrb->Cdb)->PageCode) {
	case VPD_SUPPORTED_PAGES:
	{
		PVPD_SUPPORTED_PAGES_PAGE pPage = pSrb->DataBuffer;
		// 0x00 + 0x80 + 0x83
		len = sizeof (VPD_SUPPORTED_PAGES_PAGE) + 3;
		if (pSrb->DataTransferLength < len) {
			status = SRB_STATUS_DATA_OVERRUN;
			goto ScsiOpVPD_done;
		}

		pPage->DeviceType = DIRECT_ACCESS_DEVICE;
		pPage->DeviceTypeQualifier = DEVICE_CONNECTED;
		pPage->PageCode = VPD_SUPPORTED_PAGES;
		pPage->PageLength = 3;
		pPage->SupportedPageList[0] = VPD_SUPPORTED_PAGES;
		pPage->SupportedPageList[1] = VPD_SERIAL_NUMBER;
		pPage->SupportedPageList[2] = VPD_DEVICE_IDENTIFIERS;
	}
	break;
	case VPD_SERIAL_NUMBER:
	{
		PVPD_SERIAL_NUMBER_PAGE pPage = pSrb->DataBuffer;
		len = sizeof (VPD_SERIAL_NUMBER_PAGE) + strlen(zv->zv_name);
		if (pSrb->DataTransferLength < len) {
			status = SRB_STATUS_DATA_OVERRUN;
			goto ScsiOpVPD_done;
		}

		pPage->DeviceType = DIRECT_ACCESS_DEVICE;
		pPage->DeviceTypeQualifier = DEVICE_CONNECTED;
		pPage->PageCode = VPD_SERIAL_NUMBER;
		pPage->PageLength = strlen(zv->zv_name);
		memcpy(&pPage->SerialNumber[0], zv->zv_name,
		    strlen(zv->zv_name));

		dprintf("ScsiOpVPD:  VPD Page: %d Serial No.: %s",
		    pPage->PageCode, (const char *)pPage->SerialNumber);
	}
	break;
	case VPD_DEVICE_IDENTIFIERS:
	{
		PVPD_IDENTIFICATION_PAGE pPage = pSrb->DataBuffer;
		PVPD_IDENTIFICATION_DESCRIPTOR pDesc =
		    (PVPD_IDENTIFICATION_DESCRIPTOR)&pPage->Descriptors[0];

		len = sizeof (VPD_IDENTIFICATION_PAGE) +
		    sizeof (VPD_IDENTIFICATION_DESCRIPTOR) +
		    strlen(VENDOR_ID_ascii) + strlen(zv->zv_name);
		if (pSrb->DataTransferLength < len) {
			status = SRB_STATUS_DATA_OVERRUN;
			goto ScsiOpVPD_done;
		}

		pPage->PageCode = VPD_DEVICE_IDENTIFIERS;
		// Only descriptor is the vendor T10 for now:
		// VendorId:Poolname/Zvolname
		// NAA can't be done as OpenZFS is not IEEE registered for NAA.
		pDesc->CodeSet = VpdCodeSetAscii;
		pDesc->IdentifierType = VpdIdentifierTypeVendorId;
		pDesc->Association = VpdAssocDevice;
		pDesc->IdentifierLength = strlen(VENDOR_ID_ascii) +
		    strlen(zv->zv_name);
		memcpy(&pDesc->Identifier[0], VENDOR_ID_ascii,
		    strlen(VENDOR_ID_ascii));
		memcpy(&pDesc->Identifier[strlen(VENDOR_ID_ascii)],
		    zv->zv_name, strlen(zv->zv_name));
		pPage->PageLength =
		    FIELD_OFFSET(VPD_IDENTIFICATION_DESCRIPTOR,
		    Identifier) + pDesc->IdentifierLength;
	}
	break;
	default:
		status = SRB_STATUS_INVALID_REQUEST;
		len = 0;
		break;
	} // endswitch

ScsiOpVPD_done:
	pSrb->DataTransferLength = len;
	return (status);
}

UCHAR
ScsiOpReadCapacity(
	__in pHW_HBA_EXT pHBAExt,
	__in PSCSI_REQUEST_BLOCK  pSrb)
{
	UNREFERENCED_PARAMETER(pHBAExt);
	PREAD_CAPACITY_DATA  readCapacity = pSrb->DataBuffer;
	ULONG maxBlocks,
	    blockSize;
	zvol_state_t *zv = wzvol_find_target(pSrb->TargetId, pSrb->Lun);
	if (NULL == zv) {
		dprintf("Unable to get zv context for device %d:%d:%d\n",
		    pSrb->PathId, pSrb->TargetId, pSrb->Lun);
		pSrb->DataTransferLength = 0;
		return (SRB_STATUS_NO_DEVICE);
	}

	RtlZeroMemory((PUCHAR)pSrb->DataBuffer, pSrb->DataTransferLength);

	/*
	 * fake maxBlocks to ULONG_MAX so that Windows calls with
	 * SCSIOP_READ_CAPACITY16.
	 * This would help specify non-zero LogicalPerPhysicalExponent
	 * that makes logical and physical sector size of a zvol different,
	 * kind of 512e disk!
	 */
	maxBlocks = ULONG_MAX;
	blockSize = MP_BLOCK_SIZE;

	dprintf("Block Size: 0x%x Total Blocks: 0x%x\n", blockSize, maxBlocks);
	REVERSE_BYTES(&readCapacity->BytesPerBlock, &blockSize);
	REVERSE_BYTES(&readCapacity->LogicalBlockAddress, &maxBlocks);

	wzvol_unlock_target(zv);
	return (SRB_STATUS_SUCCESS);
}

UCHAR
ScsiOpReadCapacity16(
	__in pHW_HBA_EXT pHBAExt,
	__in PSCSI_REQUEST_BLOCK pSrb)
{
	PREAD_CAPACITY16_DATA  readCapacity = pSrb->DataBuffer;
	ULONGLONG maxBlocks = 0;
	ULONG blockSize = 0;
	UCHAR lppExponent = 0;
	ULONG lppFactor;
	UNREFERENCED_PARAMETER(pHBAExt);

	zvol_state_t *zv = wzvol_find_target(pSrb->TargetId, pSrb->Lun);
	if (NULL == zv) {
		dprintf("Unable to get zv context for device %d:%d:%d\n",
		    pSrb->PathId, pSrb->TargetId, pSrb->Lun);
		pSrb->DataTransferLength = 0;
		return (SRB_STATUS_NO_DEVICE);
	}
	RtlZeroMemory((PUCHAR)pSrb->DataBuffer, pSrb->DataTransferLength);
	blockSize = MP_BLOCK_SIZE;
	maxBlocks = (zv->zv_volsize / blockSize) - 1;

	dprintf("%s:%d Block Size: 0x%x Total Blocks: 0x%llx targetid:%d "
	    "lun:%d, volname:%s, zv_volsize=%llu\n", __func__, __LINE__,
	    blockSize, maxBlocks, pSrb->TargetId, pSrb->Lun, zv->zv_name,
	    zv->zv_volsize);
	REVERSE_BYTES(&readCapacity->BytesPerBlock, &blockSize);
	REVERSE_BYTES_QUAD(&readCapacity->LogicalBlockAddress.QuadPart,
	    &maxBlocks);
	lppFactor = zv->zv_volblocksize / MP_BLOCK_SIZE;
	ASSERT((lppFactor & (lppFactor - 1)) == 0);
	while (lppFactor >>= 1)
		lppExponent++;
	readCapacity->LogicalPerPhysicalExponent = lppExponent;
	wzvol_unlock_target(zv);
	return (SRB_STATUS_SUCCESS);
}

UCHAR
ScsiOpRead(
	__in pHW_HBA_EXT pHBAExt,
	__in PSCSI_REQUEST_BLOCK pSrb,
	__in PUCHAR pResult)
{
	UCHAR status;

	status = ScsiReadWriteSetup(pHBAExt, pSrb, ActionRead, pResult);

	return (status);
}

UCHAR
ScsiOpWrite(
	__in pHW_HBA_EXT pHBAExt,
	__in PSCSI_REQUEST_BLOCK pSrb,
	__in PUCHAR pResult)
{
	UCHAR status;

	status = ScsiReadWriteSetup(pHBAExt, pSrb, ActionWrite, pResult);

	return (status);
}

UCHAR
ScsiOpUnmap(
	__in pHW_HBA_EXT pHBAExt,
	__in PSCSI_REQUEST_BLOCK pSrb,
	__in PUCHAR pResult)
{
	UCHAR status;

	status = ScsiReadWriteSetup(pHBAExt, pSrb, ActionUnmap, pResult);

	return (status);
}

UCHAR
ScsiOpUnmap_impl(
	__in pHW_HBA_EXT pHBAExt,
	__in PSCSI_REQUEST_BLOCK pSrb,
	__in zvol_state_t *zv)
{
	PUNMAP_LIST_HEADER DataBuffer = pSrb->DataBuffer;
	ULONG DataTransferLength = pSrb->DataTransferLength;
	ULONG DataLength;
	int ret;

	if (0 == DataBuffer ||
	    DataTransferLength < sizeof (UNMAP_LIST_HEADER) ||
	    DataTransferLength < sizeof (UNMAP_LIST_HEADER) + (DataLength =
	    ((ULONG)DataBuffer->BlockDescrDataLength[0] << 8) |
	    (ULONG)DataBuffer->BlockDescrDataLength[1]))
		return (SRB_STATUS_INTERNAL_ERROR);

	// Test for DataLength > MaxTransferLength ?

	// Fasttrack 0 length
	if (DataLength == 0)
		return (SRB_STATUS_SUCCESS);

	// Loop all the unmap ranges
	for (ULONG i = 0, n = DataLength / sizeof (UNMAP_BLOCK_DESCRIPTOR);
	    n > i; i++)	{
		PUNMAP_BLOCK_DESCRIPTOR Src = &DataBuffer->Descriptors[i];
		UINT64 BlockAddress =
		    ((UINT64)Src->StartingLba[0] << 56) |
		    ((UINT64)Src->StartingLba[1] << 48) |
		    ((UINT64)Src->StartingLba[2] << 40) |
		    ((UINT64)Src->StartingLba[3] << 32) |
		    ((UINT64)Src->StartingLba[4] << 24) |
		    ((UINT64)Src->StartingLba[5] << 16) |
		    ((UINT64)Src->StartingLba[6] << 8) |
		    ((UINT64)Src->StartingLba[7]);
		UINT32 BlockCount =
		    ((UINT32)Src->LbaCount[0] << 24) |
		    ((UINT32)Src->LbaCount[1] << 16) |
		    ((UINT32)Src->LbaCount[2] << 8) |
		    ((UINT32)Src->LbaCount[3]);

		ret = zvol_os_unmap(zv,
		    BlockAddress * zv->zv_volblocksize,
		    BlockCount * zv->zv_volblocksize);
		if (ret != 0)
			break;
	}

	if (ret != 0)
		return (SCSI_SENSE_ILLEGAL_REQUEST);

	return (SRB_STATUS_SUCCESS);
}

UCHAR
ScsiReadWriteSetup(
	__in pHW_HBA_EXT pHBAExt,
	__in PSCSI_REQUEST_BLOCK pSrb,
	__in MpWkRtnAction WkRtnAction,
	__in PUCHAR pResult)
{
	PHW_SRB_EXTENSION pSrbExt = pSrb->SrbExtension;
	pMP_WorkRtnParms pWkRtnParms = &pSrbExt->WkRtnParms;

	ASSERT(pSrb->DataBuffer != NULL);

	*pResult = ResultDone;

	RtlZeroMemory(pWkRtnParms, sizeof (MP_WorkRtnParms));

	pWkRtnParms->pHBAExt = pHBAExt;
	pWkRtnParms->pSrb = pSrb;
	pWkRtnParms->Action = WkRtnAction;

	IoInitializeWorkItem((PDEVICE_OBJECT)pHBAExt->pDrvObj,
	    (PIO_WORKITEM)pWkRtnParms->pQueueWorkItem);

	// Save the SRB in a list allowing cancellation via
	// SRB_FUNCTION_RESET_xxx
	pSrbExt->pSrbBackPtr = pSrb;
	pSrbExt->Cancelled = 0;
	KIRQL oldIrql;
	KeAcquireSpinLock(&pHBAExt->pwzvolDrvObj->SrbExtLock, &oldIrql);
	InsertTailList(&pHBAExt->pwzvolDrvObj->ListSrbExt,
	    &pSrbExt->QueuedForProcessing);
	KeReleaseSpinLock(&pHBAExt->pwzvolDrvObj->SrbExtLock, oldIrql);

	// Queue work item, which will run in the System process.

	IoQueueWorkItem((PIO_WORKITEM)pWkRtnParms->pQueueWorkItem,
	    wzvol_GeneralWkRtn, DelayedWorkQueue, pWkRtnParms);

	*pResult = ResultQueued;
	return (SRB_STATUS_SUCCESS);
}

UCHAR
ScsiOpModeSense(
	__in pHW_HBA_EXT pHBAExt,
	__in PSCSI_REQUEST_BLOCK pSrb)
{
	UNREFERENCED_PARAMETER(pHBAExt);

	RtlZeroMemory((PUCHAR)pSrb->DataBuffer, pSrb->DataTransferLength);

	return (SRB_STATUS_SUCCESS);
}

UCHAR
ScsiOpReportLuns(
	__in __out pHW_HBA_EXT pHBAExt,
	__in PSCSI_REQUEST_BLOCK pSrb)
{
	UCHAR status = SRB_STATUS_SUCCESS;
	PLUN_LIST pLunList = (PLUN_LIST)pSrb->DataBuffer; // Point to LUN list.
	uint8_t GoodLunIdx = 0;
	uint8_t totalLun = 0;
	zvol_state_t *zv;

	if (FALSE == pHBAExt->bReportAdapterDone) {
		wzvol_HwReportAdapter(pHBAExt);
		wzvol_HwReportLink(pHBAExt);
		wzvol_HwReportLog(pHBAExt);
		pHBAExt->bReportAdapterDone = TRUE;
	}

	RtlZeroMemory((PUCHAR)pSrb->DataBuffer, pSrb->DataTransferLength);
	if (!pHBAExt->bDontReport) {
		for (uint8_t i = 0;
		    i < STOR_wzvolDriverInfo.MaximumNumberOfLogicalUnits;
		    i++) {
			// make sure we have the space for 1 more LUN each time.
			if ((zv = wzvol_find_target(pSrb->TargetId, i)) !=
			    NULL) {
				totalLun++;
				if (pSrb->DataTransferLength >=
				    FIELD_OFFSET(LUN_LIST, Lun) +
				    (GoodLunIdx * sizeof (pLunList->Lun[0])) +
				    sizeof (pLunList->Lun[0])) {
					pLunList->Lun[GoodLunIdx][1] = (UCHAR)i;
					GoodLunIdx++;
				}
				wzvol_unlock_target(zv);
			}
		}
	}

	*((ULONG*)&pLunList->LunListLength) =
	    RtlUlongByteSwap(totalLun * sizeof (pLunList->Lun[0]));
	pSrb->DataTransferLength = FIELD_OFFSET(LUN_LIST, Lun) +
	    (GoodLunIdx * sizeof (pLunList->Lun[0]));

	return (status);
}

void
wzvol_WkRtn(__in PVOID pWkParms)
{
	pMP_WorkRtnParms pWkRtnParms = (pMP_WorkRtnParms)pWkParms;
	pHW_HBA_EXT pHBAExt = pWkRtnParms->pHBAExt;
	PSCSI_REQUEST_BLOCK pSrb = pWkRtnParms->pSrb;
	PCDB pCdb = (PCDB)pSrb->Cdb;
	PHW_SRB_EXTENSION pSrbExt = (PHW_SRB_EXTENSION)pSrb->SrbExtension;
	ULONGLONG startingSector = 0ULL, sectorOffset = 0ULL;
	UCHAR status;
	int flags = 0;
	zvol_state_t *zv = NULL;

	// Find out if that SRB has been cancelled and busy it back if it was.
	KIRQL oldIrql;
	KeAcquireSpinLock(&pHBAExt->pwzvolDrvObj->SrbExtLock, &oldIrql);
	RemoveEntryList(&pSrbExt->QueuedForProcessing);
	KeReleaseSpinLock(&pHBAExt->pwzvolDrvObj->SrbExtLock, oldIrql);
	if (pSrbExt->Cancelled) {
		status = SRB_STATUS_BUSY;
		goto Done;
	}
	ASSERT(pSrb->DataBuffer != NULL);

	zv = wzvol_find_target(pSrb->TargetId, pSrb->Lun);
	if (zv == NULL) {
		status = SRB_STATUS_NO_DEVICE;
		goto Done;
	}

	if (ActionUnmap == pWkRtnParms->Action) {
		status = ScsiOpUnmap_impl(pHBAExt, pSrb, zv);
		goto Done;
	}

	if (pSrb->CdbLength == 10) {
		startingSector = (ULONG)pCdb->CDB10.LogicalBlockByte3 |
		    pCdb->CDB10.LogicalBlockByte2 << 8 |
		    pCdb->CDB10.LogicalBlockByte1 << 16 |
		    pCdb->CDB10.LogicalBlockByte0 << 24;
		if (pCdb->CDB10.ForceUnitAccess)
			flags |= ZVOL_WRITE_SYNC;
	} else if (pSrb->CdbLength == 16) {
		REVERSE_BYTES_QUAD(&startingSector, pCdb->CDB16.LogicalBlock);
		if (pCdb->CDB16.ForceUnitAccess)
			flags |= ZVOL_WRITE_SYNC;
	} else {
		status = SRB_STATUS_ERROR;
		goto Done;
	}

	sectorOffset = startingSector * MP_BLOCK_SIZE;

	TraceEvent(TRACE_VERBOSE, "%s:%d: MpWkRtn Action: %X, starting sector:"
	    " 0x%llX, sector offset: 0x%llX\n", __func__, __LINE__,
	    pWkRtnParms->Action, startingSector, sectorOffset);
	TraceEvent(TRACE_VERBOSE, "%s:%d: MpWkRtn pSrb: 0x%p, pSrb->DataBuffer"
	    ": 0x%p\n", __func__, __LINE__, pSrb, pSrb->DataBuffer);

	if (sectorOffset >= zv->zv_volsize) {
		dprintf("%s:%d invalid starting sector: %d for zvol:%s, "
		    "volsize=%llu\n", __func__, __LINE__, startingSector,
		    zv->zv_name, zv->zv_volsize);
		status = SRB_STATUS_INVALID_REQUEST;
		goto Done;
	}

	// Create an uio for the IO. If we can possibly embed
	// the uio in some Extension to this IO, we could
	// save the allocation here.
	struct iovec iov;
	iov.iov_base = (void *)pSrb->DataBuffer;
	iov.iov_len = pSrb->DataTransferLength;

	zfs_uio_t uio;
	zfs_uio_iovec_init(&uio, &iov, 1, sectorOffset, UIO_SYSSPACE,
	    pSrb->DataTransferLength, 0);
	//    ActionRead == pWkRtnParms->Action ? UIO_READ : UIO_WRITE);

	/* Call ZFS to read/write data */
	if (ActionRead == pWkRtnParms->Action) {
		status = zvol_os_read_zv(zv, &uio, flags);
	} else {
		status = zvol_os_write_zv(zv, &uio, flags);
	}

	if (status == 0)
		status = SRB_STATUS_SUCCESS;

Done:
	if (zv)
		wzvol_unlock_target(zv);

	pSrb->SrbStatus = status;

	// Tell StorPort this action has been completed.
	StorPortNotification(RequestComplete, pHBAExt, pSrb);
}

void
wzvol_GeneralWkRtn(
	__in PVOID pDummy,
	__in PVOID pWkParms)
{
	pMP_WorkRtnParms pWkRtnParms = (pMP_WorkRtnParms)pWkParms;

	UNREFERENCED_PARAMETER(pDummy);
	IoUninitializeWorkItem((PIO_WORKITEM)pWkRtnParms->pQueueWorkItem);

	// If the next starts, it has to be stopped by a kernel debugger.

	while (pWkRtnParms->SecondsToDelay) {
		LARGE_INTEGER delay;

		delay.QuadPart =
		    -10 * 1000 * 1000 * pWkRtnParms->SecondsToDelay;

		KeDelayExecutionThread(KernelMode, TRUE, &delay);
	}

	wzvol_WkRtn(pWkParms);

}

/*
 * ZFS ZVOLDI
 * ZVOL Direct Interface
 */
void
bzvol_ReadWriteTaskRtn(__in PVOID  pWkParms)
{
	NTSTATUS Status;
	pMP_WorkRtnParms pWkRtnParms = (pMP_WorkRtnParms)pWkParms;
	zfsiodesc_t *pIo = &pWkRtnParms->ioDesc;
	int iores;

	struct iovec iov;
	iov.iov_base = (void*)pIo->Buffer;
	iov.iov_len = pIo->Length;

	zfs_uio_t uio;
	zfs_uio_iovec_init(&uio, &iov, 1, pIo->ByteOffset, UIO_SYSSPACE,
	    pIo->Length, 0);

	/* Call ZFS to read/write data */

	if (ActionRead == pWkRtnParms->Action) {
		iores = zvol_os_read_zv(pWkRtnParms->zv, &uio, 0);
	} else {
		/* TODO add flag if FUA */
		iores = zvol_os_write_zv(pWkRtnParms->zv, &uio, 0);
	}

	if (pIo->Cb) {
		pIo->Cb(pIo, iores == 0 ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL,
		    TRUE);
	}

	ExFreePoolWithTag(pWkRtnParms, MP_TAG_GENERAL);
}

void
bzvol_TaskQueuingWkRtn(__in PVOID pDummy, __in PVOID pWkParms)
{
	pMP_WorkRtnParms pWkRtnParms = (pMP_WorkRtnParms)pWkParms;
	zfsiodesc_t *pIo = &pWkRtnParms->ioDesc;
	PIO_WORKITEM pWI = (PIO_WORKITEM)ALIGN_UP_POINTER_BY(
	    pWkRtnParms->pQueueWorkItem, 16);

	UNREFERENCED_PARAMETER(pDummy);
	IoUninitializeWorkItem(pWI);

	if (pIo->Flags & ZFSZVOLFG_AlwaysPend) {
		taskq_init_ent(&pWkRtnParms->ent);
		taskq_dispatch_ent(zvol_taskq, bzvol_ReadWriteTaskRtn,
		    pWkRtnParms, 0, &pWkRtnParms->ent);
	} else {
		// bypass the taskq and do everything under workitem thread
		// context.
		bzvol_ReadWriteTaskRtn(pWkRtnParms);
	}
	// workitem freed inside bzvol_ReadWriteTaskRtn
}

NTSTATUS
DiReadWriteSetup(zvol_state_t *zv, MpWkRtnAction action, zfsiodesc_t *pIo)
{
	// cannot use kmem_alloc with sleep if IRQL dispatch so get straight
	// from NP pool.
	pMP_WorkRtnParms pWkRtnParms = (pMP_WorkRtnParms)ExAllocatePoolWithTag(
	    NonPagedPool, ALIGN_UP_BY(sizeof (MP_WorkRtnParms), 16) +
	    IoSizeofWorkItem(), MP_TAG_GENERAL);
	if (NULL == pWkRtnParms) {
		if (pIo->Cb) {
			pIo->Cb(pIo, STATUS_INSUFFICIENT_RESOURCES, FALSE);
		}
		return (STATUS_INSUFFICIENT_RESOURCES);
	}
	RtlZeroMemory(pWkRtnParms, sizeof (MP_WorkRtnParms));
	pWkRtnParms->ioDesc = *pIo;
	pWkRtnParms->zv = zv;
	pWkRtnParms->Action = action;

	/*
	 * SSV-19147: cannot use taskq queuing at dispatch. must queue a work
	 * item to do it. Since taskq queueing involves a possibly waiting mutex
	 * we do not want to slow down the caller so perform taskq queuing
	 * always in the workitem.
	 */
	extern PDEVICE_OBJECT ioctlDeviceObject;
	PIO_WORKITEM pWI = (PIO_WORKITEM)ALIGN_UP_POINTER_BY(
	    pWkRtnParms->pQueueWorkItem, 16);
	IoInitializeWorkItem(ioctlDeviceObject, pWI);
	IoQueueWorkItem(pWI, bzvol_TaskQueuingWkRtn, DelayedWorkQueue,
	    pWkRtnParms);
	return (STATUS_PENDING); // queued up.
}

NTSTATUS
ZvolDiRead(PVOID Context, zfsiodesc_t *pIo)
{
	return (DiReadWriteSetup((zvol_state_t *)Context, ActionRead, pIo));
}

NTSTATUS
ZvolDiWrite(PVOID Context, zfsiodesc_t *pIo)
{
	return (DiReadWriteSetup((zvol_state_t *)Context, ActionWrite, pIo));
}
