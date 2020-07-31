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
 * Copyright (c) 2019 Jorgen Lundman <lundman@lundman.net>
 */

#include <sys/debug.h>
#include <ntddk.h>
#include <storport.h>
#include <wmistr.h>
#include <hbapiwmi.h>
#include <wdf.h>
#include <sys/zfs_context.h>
#include <sys/wzvol.h>

extern PDRIVER_OBJECT WIN_DriverObject;
static pHW_HBA_EXT STOR_HBAExt = NULL;


// Verbose

int
zvol_start(PDRIVER_OBJECT  DriverObject, PUNICODE_STRING pRegistryPath)
{
	pwzvolDriverInfo pwzvolDrvInfo;
	NTSTATUS status;
	VIRTUAL_HW_INITIALIZATION_DATA hwInitData;

	RtlZeroMemory(&STOR_wzvolDriverInfo, sizeof (STOR_wzvolDriverInfo));
	pwzvolDrvInfo = &STOR_wzvolDriverInfo;

	RtlZeroMemory(pwzvolDrvInfo, sizeof (wzvolDriverInfo));
	pwzvolDrvInfo->pDriverObj = DriverObject;

	KeInitializeSpinLock(&pwzvolDrvInfo->DrvInfoLock);
	KeInitializeSpinLock(&pwzvolDrvInfo->MPIOExtLock);
	KeInitializeSpinLock(&pwzvolDrvInfo->SrbExtLock);

	InitializeListHead(&pwzvolDrvInfo->ListMPHBAObj);
	InitializeListHead(&pwzvolDrvInfo->ListMPIOExt);
	InitializeListHead(&pwzvolDrvInfo->ListSrbExt);

	pwzvolDrvInfo->wzvolRegInfo.BreakOnEntry = DEFAULT_BREAK_ON_ENTRY;
	pwzvolDrvInfo->wzvolRegInfo.DebugLevel = DEFAULT_DEBUG_LEVEL;
	pwzvolDrvInfo->wzvolRegInfo.InitiatorID = DEFAULT_INITIATOR_ID;
	pwzvolDrvInfo->wzvolRegInfo.PhysicalDiskSize =
	    DEFAULT_PHYSICAL_DISK_SIZE;
	pwzvolDrvInfo->wzvolRegInfo.VirtualDiskSize = DEFAULT_VIRTUAL_DISK_SIZE;
	pwzvolDrvInfo->wzvolRegInfo.NbrVirtDisks = DEFAULT_NbrVirtDisks;

	pwzvolDrvInfo->wzvolRegInfo.NbrLUNsperHBA = DEFAULT_NbrLUNsperHBA;
	pwzvolDrvInfo->wzvolRegInfo.NbrLUNsperTarget = DEFAULT_NbrLUNsperTarget;
	pwzvolDrvInfo->wzvolRegInfo.bCombineVirtDisks =
	    DEFAULT_bCombineVirtDisks;

	RtlInitUnicodeString(&pwzvolDrvInfo->wzvolRegInfo.VendorId, VENDOR_ID);
	RtlInitUnicodeString(&pwzvolDrvInfo->wzvolRegInfo.ProductId,
	    PRODUCT_ID);
	RtlInitUnicodeString(&pwzvolDrvInfo->wzvolRegInfo.ProductRevision,
	    PRODUCT_REV);

	// Calculate the combination of busses, targets and Luns to fit
	// the NbrLUNsperHBA requirement. We privilege the maximum amount
	// of targets vs. luns so TARGET RESETs don't affect a bunch of LUNs
	//
	if ((pwzvolDrvInfo->wzvolRegInfo.NbrLUNsperHBA /
	    pwzvolDrvInfo->wzvolRegInfo.NbrLUNsperTarget) >
	    SCSI_MAXIMUM_TARGETS_PER_BUS)
		pwzvolDrvInfo->MaximumNumberOfTargets =
		    SCSI_MAXIMUM_TARGETS_PER_BUS;
	else
		pwzvolDrvInfo->MaximumNumberOfTargets =
		    (pwzvolDrvInfo->wzvolRegInfo.NbrLUNsperHBA /
		    pwzvolDrvInfo->wzvolRegInfo.NbrLUNsperTarget) +
		    (pwzvolDrvInfo->wzvolRegInfo.NbrLUNsperHBA %
		    pwzvolDrvInfo->wzvolRegInfo.NbrLUNsperTarget ? 1 : 0);

	// pwzvolDrvInfo->MaximumNumberOfTargets =
	// (pwzvolDrvInfo->wzvolRegInfo.NbrLUNsperHBA <=
	// SCSI_MAXIMUM_TARGETS_PER_BUS ?
	// pwzvolDrvInfo->wzvolRegInfo.NbrLUNsperHBA :
	// SCSI_MAXIMUM_TARGETS_PER_BUS);
	pwzvolDrvInfo->MaximumNumberOfLogicalUnits =
	    (pwzvolDrvInfo->wzvolRegInfo.NbrLUNsperHBA /
	    pwzvolDrvInfo->MaximumNumberOfTargets) + 1;
	// supporting more would mean bigger changes in the zv_targets
	// array. now we can go up to 32,640 zvols.
	pwzvolDrvInfo->NumberOfBuses = 1;
	pwzvolDrvInfo->zvContextArray =
	    (wzvolContext*)ExAllocatePoolWithTag(NonPagedPoolNx,
	    ((SIZE_T)pwzvolDrvInfo->MaximumNumberOfTargets *
	    pwzvolDrvInfo->MaximumNumberOfLogicalUnits *
	    sizeof (wzvolContext)), MP_TAG_GENERAL);
	if (pwzvolDrvInfo->zvContextArray == NULL)
		return (STATUS_NO_MEMORY);

	RtlZeroMemory(pwzvolDrvInfo->zvContextArray,
	    ((SIZE_T)pwzvolDrvInfo->MaximumNumberOfTargets *
	    pwzvolDrvInfo->MaximumNumberOfLogicalUnits *
	    (sizeof (wzvolContext))));

	RtlZeroMemory(&hwInitData, sizeof (VIRTUAL_HW_INITIALIZATION_DATA));

	hwInitData.HwInitializationDataSize =
	    sizeof (VIRTUAL_HW_INITIALIZATION_DATA);

	hwInitData.HwInitialize = wzvol_HwInitialize;
	hwInitData.HwStartIo = wzvol_HwStartIo;
	hwInitData.HwFindAdapter = wzvol_HwFindAdapter;
	hwInitData.HwResetBus = wzvol_HwResetBus;
	hwInitData.HwAdapterControl = wzvol_HwAdapterControl;
	hwInitData.HwFreeAdapterResources = wzvol_HwFreeAdapterResources;
	hwInitData.HwInitializeTracing = wzvol_TracingInit;
	hwInitData.HwCleanupTracing = wzvol_TracingCleanup;
	hwInitData.HwProcessServiceRequest = wzvol_ProcServReq;
	hwInitData.HwCompleteServiceIrp = wzvol_CompServReq;

	hwInitData.AdapterInterfaceType = Internal;

	hwInitData.DeviceExtensionSize = sizeof (HW_HBA_EXT);
	hwInitData.SpecificLuExtensionSize = sizeof (HW_LU_EXTENSION);
	hwInitData.SrbExtensionSize =
	    sizeof (HW_SRB_EXTENSION) + IoSizeofWorkItem();

	hwInitData.TaggedQueuing = TRUE;
	hwInitData.AutoRequestSense = TRUE;
	hwInitData.MultipleRequestPerLu = TRUE;
	hwInitData.ReceiveEvent = TRUE;

	status = StorPortInitialize(
	    DriverObject,
	    pRegistryPath,
	    (PHW_INITIALIZATION_DATA)&hwInitData,
	    NULL);

	return (status);
}

BOOLEAN
wzvol_HwInitialize(__in pHW_HBA_EXT pHBAExt)
{
	UNREFERENCED_PARAMETER(pHBAExt);

	dprintf("%s: entry\n", __func__);
	return (TRUE);
}

ULONG
wzvol_HwFindAdapter(
    __in pHW_HBA_EXT pHBAExt,
	__in PVOID pHwContext,
	__in PVOID pBusInformation,
	__in PVOID pLowerDO,
	__in PCHAR pArgumentString,
	__in __out PPORT_CONFIGURATION_INFORMATION pConfigInfo,
	__in PBOOLEAN pBAgain)
{
	ULONG i, len, status = SP_RETURN_FOUND;
	PCHAR pChar;

	dprintf("%s: entry\n", __func__);

#if defined(_AMD64_)

	KLOCK_QUEUE_HANDLE LockHandle;

#else

	KIRQL SaveIrql;

#endif

	UNREFERENCED_PARAMETER(pHwContext);
	UNREFERENCED_PARAMETER(pBusInformation);
	UNREFERENCED_PARAMETER(pLowerDO);
	UNREFERENCED_PARAMETER(pArgumentString);

	dprintf("%s: pHBAExt = 0x%p, pConfigInfo = 0x%p\n", __func__,
	    pHBAExt, pConfigInfo);

	// Copy master object from static variable.
	pHBAExt->pwzvolDrvObj = &STOR_wzvolDriverInfo;

	if (STOR_HBAExt == NULL) {
		// We save the first adapter only to announce.
		STOR_HBAExt = pHBAExt; // So we can announce
		pHBAExt->bDontReport = FALSE;
	} else {
		// If MPIO support is not requested we won't present
		// the LUNs through other found adapters.
		pHBAExt->bDontReport =
		    !STOR_wzvolDriverInfo.wzvolRegInfo.bCombineVirtDisks;
	}

	InitializeListHead(&pHBAExt->MPIOLunList);
	InitializeListHead(&pHBAExt->LUList);

	KeInitializeSpinLock(&pHBAExt->WkItemsLock);
	KeInitializeSpinLock(&pHBAExt->WkRoutinesLock);
	KeInitializeSpinLock(&pHBAExt->MPHBAObjLock);
	KeInitializeSpinLock(&pHBAExt->LUListLock);

	pHBAExt->HostTargetId =
	    (UCHAR)pHBAExt->pwzvolDrvObj->wzvolRegInfo.InitiatorID;

	pHBAExt->pDrvObj = pHBAExt->pwzvolDrvObj->pDriverObj;

	pHBAExt->NbrLUNsperHBA =
	    pHBAExt->pwzvolDrvObj->wzvolRegInfo.NbrLUNsperHBA;

	pConfigInfo->VirtualDevice = TRUE;
	pConfigInfo->WmiDataProvider = TRUE;
	pConfigInfo->MaximumTransferLength = SP_UNINITIALIZED_VALUE;
	pConfigInfo->NumberOfPhysicalBreaks = 0x21; // 128K IO size
	pConfigInfo->AlignmentMask = 0x3;
	pConfigInfo->CachesData = FALSE;
	pConfigInfo->ScatterGather = TRUE;
	pConfigInfo->MapBuffers = STOR_MAP_ALL_BUFFERS_INCLUDING_READ_WRITE;
	pConfigInfo->SynchronizationModel = StorSynchronizeFullDuplex;
	pConfigInfo->MaximumNumberOfLogicalUnits =
	    pHBAExt->pwzvolDrvObj->MaximumNumberOfLogicalUnits;
	pConfigInfo->MaximumNumberOfTargets =
	    pHBAExt->pwzvolDrvObj->MaximumNumberOfTargets;
	pConfigInfo->NumberOfBuses = pHBAExt->pwzvolDrvObj->NumberOfBuses;

	dprintf("%s: pHBAExt = 0x%p, NbBuses/MaxTargets/MaxLuns=%d/%d/%d.\n",
	    __func__,
	    pHBAExt, pConfigInfo->NumberOfBuses,
	    pConfigInfo->MaximumNumberOfTargets,
	    pConfigInfo->MaximumNumberOfLogicalUnits);

	// Save Vendor Id, Product Id, Revision in device extension.

	pChar = (PCHAR)pHBAExt->pwzvolDrvObj->wzvolRegInfo.VendorId.Buffer;
	len = min(8, (pHBAExt->pwzvolDrvObj->wzvolRegInfo.VendorId.Length / 2));
	for (i = 0; i < len; i++, pChar += 2)
		pHBAExt->VendorId[i] = *pChar;

	pChar = (PCHAR)pHBAExt->pwzvolDrvObj->wzvolRegInfo.ProductId.Buffer;
	len = min(16,
	    (pHBAExt->pwzvolDrvObj->wzvolRegInfo.ProductId.Length / 2));
	for (i = 0; i < len; i++, pChar += 2)
		pHBAExt->ProductId[i] = *pChar;

	pChar = (PCHAR)pHBAExt->pwzvolDrvObj->
	    wzvolRegInfo.ProductRevision.Buffer;
	len = min(4,
	    (pHBAExt->pwzvolDrvObj->wzvolRegInfo.ProductRevision.Length / 2));
	for (i = 0; i < len; i++, pChar += 2)
		pHBAExt->ProductRevision[i] = *pChar;

	// Add HBA extension to master driver object's linked list.

#if defined(_AMD64_)
	KeAcquireInStackQueuedSpinLock(&pHBAExt->pwzvolDrvObj->DrvInfoLock,
	    &LockHandle);
#else
	KeAcquireSpinLock(&pHBAExt->pwzvolDrvObj->DrvInfoLock, &SaveIrql);
#endif
	InsertTailList(&pHBAExt->pwzvolDrvObj->ListMPHBAObj, &pHBAExt->List);
	pHBAExt->pwzvolDrvObj->DrvInfoNbrMPHBAObj++;
#if defined(_AMD64_)
	KeReleaseInStackQueuedSpinLock(&LockHandle);
#else
	KeReleaseSpinLock(&pHBAExt->pwzvolDrvObj->DrvInfoLock, SaveIrql);
#endif

	InitializeWmiContext(pHBAExt);

	// *pBAgain = FALSE;  // should not touch this.

	return (status);
}



// Maximum WMIEvent size StorPort will support.
#define	StorPortMaxWMIEventSize 0x80
#define	InstName L"ZVOL"

void
wzvol_HwReportAdapter(__in pHW_HBA_EXT pHBAExt)
{
	dprintf("%s: entry\n", __func__);

#if 1
	NTSTATUS status;
	PWNODE_SINGLE_INSTANCE pWnode;
	ULONG WnodeSize,
	    WnodeSizeInstanceName,
	    WnodeSizeDataBlock,
	    length,
	    size;
	GUID lclGuid = MSFC_AdapterEvent_GUID;
	UNICODE_STRING lclInstanceName;
	UCHAR  myPortWWN[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };
	PMSFC_AdapterEvent pAdapterArr;

	// With the instance name used here and with the rounding-up
	// to 4-byte alignment of the data portion used here,
	// 0x34 (52) bytes are available for the actual data of the
	// WMI event.  (The 0x34 bytes result from the fact that StorPort
	// at present (August 2008) allows 0x80 bytes for the entire
	// WMIEvent (header, instance name and data); the header is 0x40
	// bytes; the instance name used here results in 0xA bytes,
	// and the rounding up consumes 2 bytes; in other words,
	// 0x80 - (0x40 + 0x0A + 0x02)).

	RtlInitUnicodeString(&lclInstanceName, InstName);

	WnodeSize = sizeof (WNODE_SINGLE_INSTANCE);

	// Because the first field in the data block, EventType, is a ULONG,
	// ensure that the data block begins on a
	// 4-byte boundary (as will be calculated in DataBlockOffset).

	// Size of USHORT at beginning plus
	WnodeSizeInstanceName = sizeof (USHORT) +
	    lclInstanceName.Length;   //   size of instance name.
	// Round length up to multiple of 4 (if needed).
	WnodeSizeInstanceName =
	    (ULONG)WDF_ALIGN_SIZE_UP(WnodeSizeInstanceName, sizeof (ULONG));

	WnodeSizeDataBlock = MSFC_AdapterEvent_SIZE;   // Size of data block.

	size = WnodeSize +  // Size of WMIEvent.
	    WnodeSizeInstanceName +
	    WnodeSizeDataBlock;

	pWnode = ExAllocatePoolWithTag(NonPagedPoolNx, size, MP_TAG_GENERAL);

	if (NULL != pWnode) {
		RtlZeroMemory(pWnode, size);

		// Fill out most of header. StorPort will set the
		// ProviderId and TimeStamp in the header.

		pWnode->WnodeHeader.BufferSize = size;
		pWnode->WnodeHeader.Version = 1;
		RtlCopyMemory(&pWnode->WnodeHeader.Guid, &lclGuid,
		    sizeof (lclGuid));
		pWnode->WnodeHeader.Flags = WNODE_FLAG_EVENT_ITEM |
		    WNODE_FLAG_SINGLE_INSTANCE;

		// Say where to find instance name and the data block
		// and what is the data block's size.

		pWnode->OffsetInstanceName = WnodeSize;
		pWnode->DataBlockOffset = WnodeSize + WnodeSizeInstanceName;
		pWnode->SizeDataBlock = WnodeSizeDataBlock;

		// Copy the instance name.

		// Length remaining and available.
		size -= WnodeSize;
		// Copy WCHAR string, preceded by its size.
		status = WDF_WMI_BUFFER_APPEND_STRING(
		    WDF_PTR_ADD_OFFSET(pWnode, pWnode->OffsetInstanceName),
		    size,
		    &lclInstanceName,
		    &length);

		if (STATUS_SUCCESS != status) {
			ASSERT(FALSE);
		}

		pAdapterArr =
		    WDF_PTR_ADD_OFFSET(pWnode, pWnode->DataBlockOffset);

		// Copy event code and WWN.

		pAdapterArr->EventType = HBA_EVENT_ADAPTER_ADD;

		RtlCopyMemory(pAdapterArr->PortWWN, myPortWWN,
		    sizeof (myPortWWN));

		// Ask StorPort to announce the event.

		StorPortNotification(WMIEvent,
		    pHBAExt,
		    pWnode,
		    0xFF); // Notification pertains to an HBA.

		ExFreePoolWithTag(pWnode, MP_TAG_GENERAL);
	} else {
	}
#endif
}

void
wzvol_HwReportLink(__in pHW_HBA_EXT pHBAExt)
{
	dprintf("%s: entry\n", __func__);

#if 1
	NTSTATUS status;
	PWNODE_SINGLE_INSTANCE pWnode;
	PMSFC_LinkEvent pLinkEvent;
	ULONG WnodeSize,
	    WnodeSizeInstanceName,
	    WnodeSizeDataBlock,
	    length,
	    size;
	GUID lclGuid = MSFC_LinkEvent_GUID;
	UNICODE_STRING lclInstanceName;

	// Define 16 entries in MSFC_LinkEvent.RLIRBuffer[].
#define	RLIRBufferArraySize 0x10

	UCHAR myAdapterWWN[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };
	UCHAR myRLIRBuffer[RLIRBufferArraySize] =
	    { 10, 11, 12, 13, 14, 15, 16, 17, 20, 21, 22, 23,
		    24, 25, 26, 0xFF };

	RtlInitUnicodeString(&lclInstanceName, InstName);

	WnodeSize = sizeof (WNODE_SINGLE_INSTANCE);
	WnodeSizeInstanceName = sizeof (USHORT) +
	    lclInstanceName.Length;
	WnodeSizeInstanceName =
	    (ULONG)WDF_ALIGN_SIZE_UP(WnodeSizeInstanceName, sizeof (ULONG));
	WnodeSizeDataBlock =
	    FIELD_OFFSET(MSFC_LinkEvent, RLIRBuffer) +
	    sizeof (myRLIRBuffer);

	size = WnodeSize +
	    WnodeSizeInstanceName +
	    WnodeSizeDataBlock;

	pWnode = ExAllocatePoolWithTag(NonPagedPoolNx, size, MP_TAG_GENERAL);

	if (NULL != pWnode) {
		RtlZeroMemory(pWnode, size);

		// Fill out most of header. StorPort will set the
		// ProviderId and TimeStamp in the header.

		pWnode->WnodeHeader.BufferSize = size;
		pWnode->WnodeHeader.Version = 1;
		RtlCopyMemory(&pWnode->WnodeHeader.Guid, &lclGuid,
		    sizeof (lclGuid));
		pWnode->WnodeHeader.Flags = WNODE_FLAG_EVENT_ITEM |
		    WNODE_FLAG_SINGLE_INSTANCE;

		// Say where to find instance name and the data block
		// and what is the data block's size.

		pWnode->OffsetInstanceName = WnodeSize;
		pWnode->DataBlockOffset = WnodeSize + WnodeSizeInstanceName;
		pWnode->SizeDataBlock = WnodeSizeDataBlock;

		// Copy the instance name.

		size -= WnodeSize;
		status = WDF_WMI_BUFFER_APPEND_STRING(
		    WDF_PTR_ADD_OFFSET(pWnode, pWnode->OffsetInstanceName),
		    size,
		    &lclInstanceName,
		    &length);

		if (STATUS_SUCCESS != status) {
			ASSERT(FALSE);
		}

		pLinkEvent =
		    WDF_PTR_ADD_OFFSET(pWnode, pWnode->DataBlockOffset);

		// Copy event code, WWN, buffer size and buffer contents.

		pLinkEvent->EventType = HBA_EVENT_LINK_INCIDENT;

		RtlCopyMemory(pLinkEvent->AdapterWWN, myAdapterWWN,
		    sizeof (myAdapterWWN));

		pLinkEvent->RLIRBufferSize = sizeof (myRLIRBuffer);

		RtlCopyMemory(pLinkEvent->RLIRBuffer, myRLIRBuffer,
		    sizeof (myRLIRBuffer));

		StorPortNotification(WMIEvent,
		    pHBAExt,
		    pWnode,
		    0xFF);

		ExFreePoolWithTag(pWnode, MP_TAG_GENERAL);
	} else {
	}
#endif
}

void
wzvol_HwReportLog(__in pHW_HBA_EXT pHBAExt)
{
	dprintf("%s: entry\n", __func__);

#if 1
	NTSTATUS status;
	PWNODE_SINGLE_INSTANCE pWnode;
	ULONG WnodeSize,
	    WnodeSizeInstanceName,
	    WnodeSizeDataBlock,
	    length,
	    size;
	UNICODE_STRING lclInstanceName;
	PIO_ERROR_LOG_PACKET pLogError;

	RtlInitUnicodeString(&lclInstanceName, InstName);

	WnodeSize = sizeof (WNODE_SINGLE_INSTANCE);
	WnodeSizeInstanceName = sizeof (USHORT) +
	    lclInstanceName.Length;
	WnodeSizeInstanceName =
	    (ULONG)WDF_ALIGN_SIZE_UP(WnodeSizeInstanceName, sizeof (ULONG));
	WnodeSizeDataBlock = sizeof (IO_ERROR_LOG_PACKET);

	size = WnodeSize +
	    WnodeSizeInstanceName +
	    WnodeSizeDataBlock;

	pWnode = ExAllocatePoolWithTag(NonPagedPoolNx, size, MP_TAG_GENERAL);

	if (NULL != pWnode) {
		RtlZeroMemory(pWnode, size);

		// Fill out most of header. StorPort will set the
		// ProviderId and TimeStamp in the header.

		pWnode->WnodeHeader.BufferSize = size;
		pWnode->WnodeHeader.Version = 1;
		pWnode->WnodeHeader.Flags = WNODE_FLAG_EVENT_ITEM |
		    WNODE_FLAG_LOG_WNODE;

		pWnode->WnodeHeader.HistoricalContext = 9;

		// Say where to find instance name and the data
		// block and what is the data block's size.

		pWnode->OffsetInstanceName = WnodeSize;
		pWnode->DataBlockOffset = WnodeSize + WnodeSizeInstanceName;
		pWnode->SizeDataBlock = WnodeSizeDataBlock;

		// Copy the instance name.

		size -= WnodeSize;
		status = WDF_WMI_BUFFER_APPEND_STRING(
		    WDF_PTR_ADD_OFFSET(pWnode, pWnode->OffsetInstanceName),
		    size,
		    &lclInstanceName,
		    &length);

		if (STATUS_SUCCESS != status) {
			ASSERT(FALSE);
		}

		pLogError =
		    WDF_PTR_ADD_OFFSET(pWnode, pWnode->DataBlockOffset);

		pLogError->UniqueErrorValue = 0x40;
		pLogError->FinalStatus = 0x41;
		pLogError->ErrorCode = 0x42;

		StorPortNotification(WMIEvent,
		    pHBAExt,
		    pWnode,
		    0xFF);

		ExFreePoolWithTag(pWnode, MP_TAG_GENERAL);
	} else {
	}
#endif
}

BOOLEAN
wzvol_HwResetBus(
    __in pHW_HBA_EXT pHBAExt,
    __in ULONG BusId)
{
	UNREFERENCED_PARAMETER(pHBAExt);
	UNREFERENCED_PARAMETER(BusId);

	// To do: At some future point, it may be worthwhile to ensure
	// that any SRBs being handled be completed at once. Practically
	// speaking, however, it seems that the only SRBs that would not
	// be completed very quickly would be those handled by the worker
	// thread. In the future, therefore, there might be a global flag
	// set here to instruct the thread to complete outstanding I/Os
	// as they appear; but a period for that happening would have to
	// be devised (such completion shouldn't be unbounded).

	return (TRUE);
}

NTSTATUS
wzvol_HandleRemoveDevice(
	__in pHW_HBA_EXT pHBAExt,
	__in PSCSI_PNP_REQUEST_BLOCK pSrb)
{
	UNREFERENCED_PARAMETER(pHBAExt);

	pSrb->SrbStatus = SRB_STATUS_BAD_FUNCTION;

	return (STATUS_UNSUCCESSFUL);
}

NTSTATUS
wzvol_HandleQueryCapabilities(
    __in pHW_HBA_EXT pHBAExt,
    __in PSCSI_PNP_REQUEST_BLOCK pSrb)
{
	NTSTATUS status = STATUS_SUCCESS;
	PSTOR_DEVICE_CAPABILITIES pStorageCapabilities =
	    (PSTOR_DEVICE_CAPABILITIES)pSrb->DataBuffer;

	UNREFERENCED_PARAMETER(pHBAExt);

	dprintf("%s: entry\n", __func__);

	RtlZeroMemory(pStorageCapabilities, pSrb->DataTransferLength);

	pStorageCapabilities->Removable = FALSE;
	pStorageCapabilities->SurpriseRemovalOK = FALSE;

	pSrb->SrbStatus = SRB_STATUS_SUCCESS;

	return (status);
}

NTSTATUS
wzvol_HwHandlePnP(
	__in pHW_HBA_EXT pHBAExt,
	__in PSCSI_PNP_REQUEST_BLOCK pSrb)
{
	NTSTATUS status = STATUS_SUCCESS;
	dprintf("%s: entry\n", __func__);

#if 1
	switch (pSrb->PnPAction) {

	case StorRemoveDevice:
		status = wzvol_HandleRemoveDevice(pHBAExt, pSrb);
		break;

	case StorQueryCapabilities:
		status = wzvol_HandleQueryCapabilities(pHBAExt, pSrb);
		break;

	default:
		pSrb->SrbStatus = SRB_STATUS_SUCCESS;
	}

	if (STATUS_SUCCESS != status) {
	}
#endif
	return (status);
}

BOOLEAN
wzvol_HwStartIo(
	__in pHW_HBA_EXT pHBAExt,
	__in __out PSCSI_REQUEST_BLOCK pSrb)
{

/*
 * This function cal be called as DPC, so we can not call into ZFS
 * (mutex etc) and kmem, including dprintf
 */

	UCHAR srbStatus = SRB_STATUS_INVALID_REQUEST;
	BOOLEAN bFlag;
	NTSTATUS status;
	UCHAR Result = ResultDone;


	_InterlockedExchangeAdd((volatile LONG *)&pHBAExt->SRBsSeen, 1);

	// Next, if true, will cause StorPort to remove the associated LUNs
	// if, for example, devmgmt.msc is asked "scan for hardware changes."


	switch (pSrb->Function) {

	case SRB_FUNCTION_EXECUTE_SCSI:
		srbStatus = ScsiExecuteMain(pHBAExt, pSrb, &Result);
		break;

	case SRB_FUNCTION_WMI:
		_InterlockedExchangeAdd((volatile LONG *)&pHBAExt->WMISRBsSeen,
		    1);
		bFlag = HandleWmiSrb(pHBAExt, (PSCSI_WMI_REQUEST_BLOCK)pSrb);
		srbStatus = TRUE == bFlag ? SRB_STATUS_SUCCESS :
		    SRB_STATUS_INVALID_REQUEST;
		break;

	case SRB_FUNCTION_RESET_BUS:
	case SRB_FUNCTION_RESET_DEVICE:
	case SRB_FUNCTION_RESET_LOGICAL_UNIT:
	{
		// Set as cancelled all queued SRBs that match the criteria.
		KIRQL oldIrql;
		PLIST_ENTRY pNextEntry;
		PHW_SRB_EXTENSION pSrbExt;
		KeAcquireSpinLock(&pHBAExt->pwzvolDrvObj->SrbExtLock, &oldIrql);
		for (pNextEntry = pHBAExt->pwzvolDrvObj->ListSrbExt.Flink;
		    pNextEntry != &pHBAExt->pwzvolDrvObj->ListSrbExt;
		    pNextEntry = pNextEntry->Flink)	{
			pSrbExt = CONTAINING_RECORD(pNextEntry,
			    HW_SRB_EXTENSION, QueuedForProcessing);
			if ((pSrbExt->pSrbBackPtr->PathId == pSrb->PathId) &&
			    (pSrb->Function == SRB_FUNCTION_RESET_BUS ? TRUE :
			    pSrbExt->pSrbBackPtr->TargetId == pSrb->TargetId) &&
			    (pSrb->Function == SRB_FUNCTION_RESET_BUS ||
			    pSrb->Function == SRB_FUNCTION_RESET_DEVICE ? TRUE :
			    pSrbExt->pSrbBackPtr->Lun == pSrb->Lun))
				pSrbExt->Cancelled = 1;
		}
		KeReleaseSpinLock(&pHBAExt->pwzvolDrvObj->SrbExtLock, oldIrql);
		srbStatus = SRB_STATUS_SUCCESS;
	}
		break;
	case SRB_FUNCTION_PNP:
		status = wzvol_HwHandlePnP(pHBAExt,
		    (PSCSI_PNP_REQUEST_BLOCK)pSrb);
		srbStatus = pSrb->SrbStatus;

		break;

	case SRB_FUNCTION_POWER:
		// Do nothing.
		srbStatus = SRB_STATUS_SUCCESS;

		break;

	case SRB_FUNCTION_SHUTDOWN:
		// Do nothing.
		srbStatus = SRB_STATUS_SUCCESS;

		break;

	default:
		srbStatus = SRB_STATUS_INVALID_REQUEST;
		break;

	} // switch (pSrb->Function)

	if (ResultDone == Result) {
		pSrb->SrbStatus = srbStatus;

		// Note:  A miniport with real hardware would not always
		// be calling RequestComplete from HwStorStartIo.  Rather,
		// the miniport would typically be doing real I/O and would
		// call RequestComplete only at the end of that
		// real I/O, in its HwStorInterrupt or in a DPC routine.

		StorPortNotification(RequestComplete, pHBAExt, pSrb);
	}

	return (TRUE);
}

SCSI_ADAPTER_CONTROL_STATUS
wzvol_HwAdapterControl(
	__in pHW_HBA_EXT pHBAExt,
	__in SCSI_ADAPTER_CONTROL_TYPE ControlType,
	__in PVOID pParameters)
{
	PSCSI_SUPPORTED_CONTROL_TYPE_LIST pCtlTypList;
	ULONG i;

	dprintf("MpHwAdapterControl:  ControlType = %d\n", ControlType);

	pHBAExt->AdapterState = ControlType;

	switch (ControlType) {
	case ScsiQuerySupportedControlTypes:
		dprintf("MpHwAdapterControl: ScsiQuerySupportedControlTypes\n");

		// Ggt pointer to control type list
		pCtlTypList = (PSCSI_SUPPORTED_CONTROL_TYPE_LIST)pParameters;

		// Cycle through list to set TRUE for each type supported
		// making sure not to go past the MaxControlType
		for (i = 0; i < pCtlTypList->MaxControlType; i++)
			if (i == ScsiQuerySupportedControlTypes ||
			    i == ScsiStopAdapter || i == ScsiRestartAdapter ||
			    i == ScsiSetBootConfig ||
			    i == ScsiSetRunningConfig) {
				pCtlTypList->SupportedTypeList[i] = TRUE;
			}
		break;

	case ScsiStopAdapter:
		dprintf("MpHwAdapterControl:  ScsiStopAdapter\n");

		// Free memory allocated for disk
		wzvol_StopAdapter(pHBAExt);

		break;

	case ScsiRestartAdapter:
		dprintf("MpHwAdapterControl:  ScsiRestartAdapter\n");

		/* To Do: Add some function. */

		break;

	case ScsiSetBootConfig:
		dprintf("MpHwAdapterControl:  ScsiSetBootConfig\n");

		break;

	case ScsiSetRunningConfig:
		dprintf("MpHwAdapterControl:  ScsiSetRunningConfig\n");

		break;

	default:
		dprintf("MpHwAdapterControl:  UNKNOWN\n");

		break;
	}

	dprintf("MpHwAdapterControl - OUT\n");

	return (ScsiAdapterControlSuccess);
}

void
wzvol_StopAdapter(__in pHW_HBA_EXT pHBAExt)
{
	pHW_LU_EXTENSION pLUExt, pLUExt2;
	PLIST_ENTRY pNextEntry, pNextEntry2;
	pwzvolDriverInfo pwzvolDrvInfo = pHBAExt->pwzvolDrvObj;
	pHW_LU_EXTENSION_MPIO pLUMPIOExt = NULL;

#if defined(_AMD64_)
	KLOCK_QUEUE_HANDLE LockHandle;
#else
	KIRQL SaveIrql;
#endif

	dprintf("%s: entry\n", __func__);

	// Clean up the "disk buffers."

	for (pNextEntry = pHBAExt->LUList.Flink;
	    pNextEntry != &pHBAExt->LUList;
	    pNextEntry = pNextEntry->Flink) {
		pLUExt = CONTAINING_RECORD(pNextEntry, HW_LU_EXTENSION, List);

		if (pwzvolDrvInfo->wzvolRegInfo.bCombineVirtDisks) {
			pLUMPIOExt = pLUExt->pLUMPIOExt;

			if (!pLUMPIOExt) {
				break;
			}

#if defined(_AMD64_)
			KeAcquireInStackQueuedSpinLock(
			    &pLUMPIOExt->LUExtMPIOLock,
			    &LockHandle);
#else
			KeAcquireSpinLock(&pLUMPIOExt->LUExtMPIOLock,
			    &SaveIrql);
#endif

			for (pNextEntry2 = pLUMPIOExt->LUExtList.Flink;
			    pNextEntry2 != &pLUMPIOExt->LUExtList;
			    pNextEntry2 = pNextEntry2->Flink) {
				pLUExt2 = CONTAINING_RECORD(pNextEntry2,
				    HW_LU_EXTENSION, MPIOList);

				if (pLUExt2 == pLUExt) {
					break;
				}
			}

			if (pNextEntry2 != &pLUMPIOExt->LUExtList) {
				RemoveEntryList(pNextEntry2);

				pLUMPIOExt->NbrRealLUNs--;

				if (0 == pLUMPIOExt->NbrRealLUNs) {
					ExFreePool(pLUExt->pDiskBuf);
				}
			}

#if defined(_AMD64_)
			KeReleaseInStackQueuedSpinLock(&LockHandle);
#else
			KeReleaseSpinLock(&pLUMPIOExt->LUExtMPIOLock, SaveIrql);
#endif
		} else {
			ExFreePool(pLUExt->pDiskBuf);
		}
	}

	// Clean up the linked list of MPIO collector objects, if needed.

	if (pwzvolDrvInfo->wzvolRegInfo.bCombineVirtDisks) {
#if defined(_AMD64_)
		KeAcquireInStackQueuedSpinLock(
		    &pwzvolDrvInfo->MPIOExtLock, &LockHandle);
#else
		KeAcquireSpinLock(&pwzvolDrvInfo->MPIOExtLock, &SaveIrql);
#endif

		for (pNextEntry = pwzvolDrvInfo->ListMPIOExt.Flink;
		    pNextEntry != &pwzvolDrvInfo->ListMPIOExt;
		    pNextEntry = pNextEntry2) {
			pLUMPIOExt = CONTAINING_RECORD(pNextEntry,
			    HW_LU_EXTENSION_MPIO, List);

			if (!pLUMPIOExt) {
				break;
			}

			pNextEntry2 = pNextEntry->Flink;

			if (0 == pLUMPIOExt->NbrRealLUNs) {
				RemoveEntryList(pNextEntry);
				ExFreePoolWithTag(pLUMPIOExt, MP_TAG_GENERAL);
			}
		}

#if defined(_AMD64_)
		KeReleaseInStackQueuedSpinLock(&LockHandle);
#else
		KeReleaseSpinLock(&pwzvolDrvInfo->MPIOExtLock, SaveIrql);
#endif
	}
}

void
wzvol_TracingInit(
	__in PVOID pArg1,
	__in PVOID pArg2)
{
//	WPP_INIT_TRACING(pArg1, pArg2);
}

void
wzvol_TracingCleanup(__in PVOID pArg1)
{
#if 1
	dprintf("MPTracingCleanUp entered\n");

	// WPP_CLEANUP(pArg1);
#endif
}

void
wzvol_HwFreeAdapterResources(__in pHW_HBA_EXT pHBAExt)
{
#if 1
	PLIST_ENTRY pNextEntry;
	pHW_HBA_EXT pLclHBAExt;
#if defined(_AMD64_)
	KLOCK_QUEUE_HANDLE LockHandle;
#else
	KIRQL SaveIrql;
#endif

	dprintf("MpHwFreeAdapterResources entered, pHBAExt = 0x%p\n", pHBAExt);

#if defined(_AMD64_)
	KeAcquireInStackQueuedSpinLock(&pHBAExt->pwzvolDrvObj->DrvInfoLock,
	    &LockHandle);
#else
	KeAcquireSpinLock(&pHBAExt->pwzvolDrvObj->DrvInfoLock, &SaveIrql);
#endif

	for (pNextEntry = pHBAExt->pwzvolDrvObj->ListMPHBAObj.Flink;
	    pNextEntry != &pHBAExt->pwzvolDrvObj->ListMPHBAObj;
	    pNextEntry = pNextEntry->Flink) {
		pLclHBAExt = CONTAINING_RECORD(pNextEntry, HW_HBA_EXT, List);

		if (pLclHBAExt == pHBAExt) {
			RemoveEntryList(pNextEntry);
			pHBAExt->pwzvolDrvObj->DrvInfoNbrMPHBAObj--;
			break;
		}
	}

#if defined(_AMD64_)
	KeReleaseInStackQueuedSpinLock(&LockHandle);
#else
	KeReleaseSpinLock(&pHBAExt->pwzvolDrvObj->DrvInfoLock, SaveIrql);
#endif

#endif

	if (STOR_HBAExt == pHBAExt)
		STOR_HBAExt = NULL;
}

void
wzvol_CompleteIrp(
	__in pHW_HBA_EXT pHBAExt,
	__in PIRP pIrp)
{
	dprintf("MpCompleteIrp entered\n");

	if (NULL != pIrp) {
		NTSTATUS Status;
		PIO_STACK_LOCATION pIrpStack =
		    IoGetCurrentIrpStackLocation(pIrp);

		switch (pIrpStack->Parameters.DeviceIoControl.IoControlCode) {
		case IOCTL_MINIPORT_PROCESS_SERVICE_IRP:
			Status = STATUS_SUCCESS;
			break;
		default:
			Status = STATUS_INVALID_DEVICE_REQUEST;
			break;
		}

		pIrp->IoStatus.Status = Status;
		if (NT_SUCCESS(Status))
			pIrp->IoStatus.Information =
			    pIrpStack->
			    Parameters.DeviceIoControl.OutputBufferLength;
		else
			pIrp->IoStatus.Information = 0;

		StorPortCompleteServiceIrp(pHBAExt, pIrp);
	}
}

void
wzvol_QueueServiceIrp(
	__in pHW_HBA_EXT pHBAExt,
	__in PIRP pIrp)
{

#if 1
	PIRP pOldIrp;

	dprintf("MpQueueServiceIrp entered\n");

	pOldIrp = InterlockedExchangePointer(&pHBAExt->pReverseCallIrp, pIrp);
	if (NULL != pOldIrp) {
		wzvol_CompleteIrp(pHBAExt, pOldIrp);
}
#endif
}

void
wzvol_ProcServReq(
	__in pHW_HBA_EXT pHBAExt,
	__in PIRP pIrp)
{

#if 1
	dprintf("MpProcServReq entered\n");

	wzvol_QueueServiceIrp(pHBAExt, pIrp);
#endif
}

void
wzvol_CompServReq(__in pHW_HBA_EXT pHBAExt)
{

#if 1
	dprintf("MpHwCompServReq entered\n");

	wzvol_QueueServiceIrp(pHBAExt, NULL);
#endif
}

void
wzvol_announce_buschange(void)
{
	dprintf("%s: \n", __func__);
	if (STOR_HBAExt != NULL)
		StorPortNotification(BusChangeDetected, STOR_HBAExt, 0);
}
