/*
 * Module Name:  wmi.c
 * Project:      CppWDKStorPortVirtualMiniport
 *
 * Copyright (c) Microsoft Corporation.
 *
 * a.       HandleWmiSrb()
 * Handles WMI SRBs, possibly by calling a subroutine.
 *
 * b.      QueryWmiDataBlock()
 * Supports WMI Query Data Block.
 *
 * c.       ExecuteWmiMethod()
 * Supports WMI Execute Method.
 *
 * This source is subject to the Microsoft Public License.
 * See http://www.microsoft.com/opensource/licenses.mspx#Ms-PL.
 * All other rights reserved.
 *
 * THIS CODE AND INFORMATION IS PROVIDED "AS IS" WITHOUT WARRANTY OF ANY KIND,
 * EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A PARTICULAR PURPOSE.
 */
#include <sys/zfs_context.h>
#include <ntddk.h>
#include <storport.h>
#include <scsiwmi.h>
#include <initguid.h>
#include <wmistr.h>
#include <wdf.h>
#include <hbaapi.h>
#include <sys/wzvol.h>
#include <sys/wzvolwmi.h>

// #include "trace.h"
// #include "wmi.tmh"

// Paper about FC WMI classes, including C++ and VBS examples:
//
//  http:/ /download.microsoft.com/download/9/c/5/
//   9c5b2167-8017-4bae-9fde-d599bac8184a/FCTopology.doc
//

#define	IdxGmDemoDriver_GUID			0
#define	IdxGmDemoDriver2_GUID			1
#define	IdxGmDemoDriverSrbActivity_GUID		2
#define	IdxGmDrvDrvMethodGuid			3
#define	IdxMSFC_AdapterEvent_GUID		4
#define	IdxMSFC_LinkEvent_GUID			5
#define	IdxMSFC_FibrePortHBAStatistics_GUID	6
#define	IdxMSFC_FibrePortHBAAttributes_GUID	7
#define	IdxMSFC_FCAdapterHBAAttributes_GUID	8
#define	IdxMSFC_HBAFCPInfo_GUID			9
#define	IdxMSFC_FibrePortHBAMethods_GUID	10
#define	IdxMSFC_HBAAdapterMethods_GUID		11
#define	IdxMSFC_HBAPortStatistics_GUID		12

#define	NUMBEROFPORTS	1

SCSIWMIGUIDREGINFO WmiGuidList[] = // GUIDs supported.
{
	{&GmDemoDriver_GUID,			1, WMIREG_FLAG_INSTANCE_PDO},
	{&GmDemoDriver2_GUID,			1, WMIREG_FLAG_INSTANCE_PDO},
	{&GmDemoDriverSrbActivity_GUID,		1, WMIREG_FLAG_INSTANCE_PDO},
	{&GmDrvDrvMethod_GUID,			1, 0x0},
	{&MSFC_AdapterEvent_GUID,		NUMBEROFPORTS, 0x0},
	{&MSFC_LinkEvent_GUID,			NUMBEROFPORTS, 0x0},
	{&MSFC_FibrePortHBAStatistics_GUID, 	NUMBEROFPORTS, 0x0},
	{&MSFC_FibrePortHBAAttributes_GUID, 	NUMBEROFPORTS, 0x0},
	{&MSFC_FCAdapterHBAAttributes_GUID, 	NUMBEROFPORTS, 0x0},
	{&MSFC_HBAFCPInfo_GUID,			NUMBEROFPORTS, 0x0},
	{&MSFC_FibrePortHBAMethods_GUID,	NUMBEROFPORTS, 0x0},
	{&MSFC_HBAAdapterMethods_GUID,		NUMBEROFPORTS, 0x0},
	{&MSFC_HBAPortStatistics_GUID,		NUMBEROFPORTS, 0x0},
};

#define	WmiGuidCount (sizeof (WmiGuidList) / sizeof (SCSIWMIGUIDREGINFO))

UCHAR
QueryWmiRegInfo(IN PVOID pContext,
    IN PSCSIWMI_REQUEST_CONTEXT pRequestContext,
    OUT PWCHAR *pMofResourceName);

BOOLEAN
QueryWmiDataBlock(
	IN PVOID pContext,
	IN PSCSIWMI_REQUEST_CONTEXT pDispatchContext,
	IN ULONG GuidIndex,
	IN ULONG InstanceIndex,
	IN ULONG InstanceCount,
	IN OUT PULONG pInstanceLengthArray,
	IN ULONG BufferAvail,
	OUT PUCHAR);

UCHAR
SetWmiDataBlock(
	IN PVOID pContext,
	IN PSCSIWMI_REQUEST_CONTEXT pDispatchContext,
	IN ULONG GuidIndex,
	IN ULONG InstanceIndex,
	IN ULONG BufferSize,
	IN PUCHAR pBuffer);

UCHAR
ExecuteWmiMethod(
	IN PVOID pContext,
	IN PSCSIWMI_REQUEST_CONTEXT pDispatchContext,
	IN ULONG GuidIndex,
	IN ULONG InstanceIndex,
	IN ULONG MethodId,
	IN ULONG InBufferSize,
	IN ULONG OutBufferSize,
	IN OUT PUCHAR pBuffer);

void
SpUpdateWmiRequest(
	pHW_HBA_EXT pHbaExtension,
	PSCSI_WMI_REQUEST_BLOCK  pSrb,
	PSCSIWMI_REQUEST_CONTEXT pDispatchContext,
	UCHAR Status,
	ULONG SizeNeeded);

void
InitializeWmiContext(__in pHW_HBA_EXT pHbaExtension)
{
	PSCSI_WMILIB_CONTEXT pWmiLibContext =
	    &(pHbaExtension->WmiLibContext);

	RtlZeroMemory(pWmiLibContext,
	    sizeof (SCSI_WMILIB_CONTEXT));

	pWmiLibContext->GuidCount = WmiGuidCount;
	pWmiLibContext->GuidList  = WmiGuidList;

	//
	// Point to WMI callback routines.
	//
	pWmiLibContext->QueryWmiRegInfo    = QueryWmiRegInfo;
	pWmiLibContext->QueryWmiDataBlock  = QueryWmiDataBlock;
	pWmiLibContext->SetWmiDataBlock    = SetWmiDataBlock;
	pWmiLibContext->ExecuteWmiMethod   = ExecuteWmiMethod;
	pWmiLibContext->WmiFunctionControl = NULL;
}

BOOLEAN
HandleWmiSrb(
	__in pHW_HBA_EXT pHbaExtension,
	__in __out PSCSI_WMI_REQUEST_BLOCK pSrb)
{
	PSCSIWMI_REQUEST_CONTEXT pRequestContext;
	PHW_SRB_EXTENSION pSrbExtension;
	BOOLEAN bPending = FALSE;

	//
	// Validate our assumptions.
	//
	ASSERT(pSrb->Function == SRB_FUNCTION_WMI);
	ASSERT(pSrb->Length == sizeof (SCSI_WMI_REQUEST_BLOCK));

	if (!(pSrb->WMIFlags & SRB_WMI_FLAGS_ADAPTER_REQUEST)) {

		//
		// This is targetted to one of the disks, since there is
		// no per-disk WMI information we return an error.
		// Note that if there was per-disk information, then you'd
		// likely have a different WmiLibContext and a different
		// set of GUIDs.
		//
		pSrb->DataTransferLength = 0;
		pSrb->SrbStatus = SRB_STATUS_NO_DEVICE;

	} else {
		if (IRP_MN_ENABLE_EVENTS == pSrb->WMISubFunction) {
		}

		pSrbExtension = (PHW_SRB_EXTENSION)pSrb->SrbExtension;
		pRequestContext = &(pSrbExtension->WmiRequestContext);

		//
		// Save the pointer to the SRB in UserContext of
		// SCSIWMI_REQUEST_CONTEXT
		//
		pRequestContext->UserContext = pSrb;

		//
		// Process the incoming WMI request.
		//

		bPending = ScsiPortWmiDispatchFunction(
		    &pHbaExtension->WmiLibContext,
		    pSrb->WMISubFunction,
		    pHbaExtension,
		    pRequestContext,
		    pSrb->DataPath,
		    pSrb->DataTransferLength,
		    pSrb->DataBuffer);

		// If the request is complete, status and transfer
		// length aren't ever going to be set.

		if (FALSE == bPending) {
			pSrb->DataTransferLength =
			    ScsiPortWmiGetReturnSize(pRequestContext);
			pSrb->SrbStatus =
			    ScsiPortWmiGetReturnStatus(pRequestContext);
		}
	}

	return (TRUE);
}

UCHAR
QueryWmiRegInfo(
	__in  PVOID pContext,
	__in  PSCSIWMI_REQUEST_CONTEXT pRequestContext,
	__out PWCHAR *pMofResourceName)
{
	KIRQL saveIRQL;
	UNREFERENCED_PARAMETER(pContext);
	UNREFERENCED_PARAMETER(pRequestContext);

	saveIRQL = KeGetCurrentIrql();

	*pMofResourceName = L"MofResource";

	return (SRB_STATUS_SUCCESS);
}

BOOLEAN
QueryWmiDataBlock(
	__in PVOID pContext,
	__in PSCSIWMI_REQUEST_CONTEXT pDispatchContext,
	__in ULONG GuidIndex,
	__in ULONG InstanceIndex,
	__in ULONG InstanceCount,
	__in __out PULONG pInstanceLenArr,
	__in ULONG BufferAvail,
	__out PUCHAR pBuffer)
{
	pHW_HBA_EXT pHbaExtension = (pHW_HBA_EXT)pContext;
	PSCSI_WMI_REQUEST_BLOCK pSrb =
	    (PSCSI_WMI_REQUEST_BLOCK)pDispatchContext->UserContext;
	ULONG sizeNeeded = 0,
	    i,
	    IdxLim = InstanceIndex + InstanceCount,
	    ulBfrUsed = 0,
	    LastIndex,
	    InstanceSize;
	UCHAR status = SRB_STATUS_SUCCESS;
	PWCHAR pBfrX;

	switch (GuidIndex) {
		case IdxGmDemoDriver_GUID:

			// Demo index.

			sizeNeeded = GmDemoDriver_SIZE;

			if (BufferAvail < sizeNeeded) {
				status = SRB_STATUS_DATA_OVERRUN;
				break;
			}

			for (i = InstanceIndex; i < IdxLim; i++) {
				PGmDemoDriver pOut =
				    (PGmDemoDriver)
				    ((PUCHAR)pBuffer + ulBfrUsed);

				pOut->TheAnswer = 22;
				pOut->TheNextAnswer = 23;
				pOut->SRBsSeen = pHbaExtension->SRBsSeen;
				pOut->WMISRBsSeen = pHbaExtension->WMISRBsSeen;

				pInstanceLenArr[i] = sizeNeeded;
				ulBfrUsed += pInstanceLenArr[i];
			}

			break;

		case IdxGmDemoDriver2_GUID:
			sizeNeeded = sizeof (ULONG);

			if (BufferAvail < sizeNeeded) {
				status = SRB_STATUS_DATA_OVERRUN;
				break;
			}

			status = SRB_STATUS_INVALID_REQUEST;
			break;

		case IdxGmDemoDriverSrbActivity_GUID:
			sizeNeeded = 0;
			break;

		case IdxGmDrvDrvMethodGuid:
			sizeNeeded = sizeof (ULONG);

			if (BufferAvail < sizeNeeded) {
				status = SRB_STATUS_DATA_OVERRUN;
				break;
			}
			break;

		case IdxMSFC_FibrePortHBAStatistics_GUID: {
			PMSFC_FibrePortHBAStatistics pPortStats;
			PMSFC_HBAPortStatistics pHBAPortStats;

			// Verify there is enough room in the output buffer
			// to return all data requested.
			// Calculate size, rounding up to a multiple of
			// 8 if needed.
			InstanceSize =
			    (sizeof (MSFC_FibrePortHBAStatistics)+7) & ~7;
			sizeNeeded = InstanceCount * InstanceSize;

			if (BufferAvail >= sizeNeeded) {
				LastIndex = InstanceIndex + InstanceCount;

				for (i = InstanceIndex,
				    pPortStats =
				    (PMSFC_FibrePortHBAStatistics)pBuffer;
				    i < LastIndex;
				    i++, pPortStats++) {
					// Set a unique value for the port.

					pPortStats->UniquePortId =
					    (ULONGLONG)pHbaExtension + i;

					pPortStats->HBAStatus = HBA_STATUS_OK;

					pHBAPortStats =
					    &pPortStats->Statistics;

					pHBAPortStats->SecondsSinceLastReset =
					    10;
					pHBAPortStats->TxFrames = 11;
					pHBAPortStats->TxWords = 12;
					pHBAPortStats->RxFrames = 13;
					pHBAPortStats->RxWords = 14;
					pHBAPortStats->LIPCount = 15;
					pHBAPortStats->NOSCount = 16;
					pHBAPortStats->ErrorFrames = 17;
					pHBAPortStats->DumpedFrames = 18;
					pHBAPortStats->LinkFailureCount = 19;
					pHBAPortStats->LossOfSyncCount = 20;
					pHBAPortStats->LossOfSignalCount = 21;
					pHBAPortStats->
					    PrimitiveSeqProtocolErrCount = 22;
					pHBAPortStats->InvalidTxWordCount = 23;
					pHBAPortStats->InvalidCRCCount = 24;

				*pInstanceLenArr++ =
				    sizeof (MSFC_FibrePortHBAStatistics);
				}
			} else {
				status = SRB_STATUS_DATA_OVERRUN;
			}

			break;
		}

		case IdxMSFC_HBAPortStatistics_GUID: {
			PMSFC_HBAPortStatistics pHBAPortStats;
			PUCHAR pBuffer2 = pBuffer;

			InstanceSize = (sizeof (MSFC_HBAPortStatistics)+7) & ~7;
			sizeNeeded = InstanceCount * InstanceSize;

			if (BufferAvail >= sizeNeeded) {
				LastIndex = InstanceIndex + InstanceCount;

				for (i = InstanceIndex; i < LastIndex; i++) {
					pHBAPortStats =
					    (PMSFC_HBAPortStatistics)pBuffer2;

					memset(pBuffer2, 0, InstanceSize);

					pHBAPortStats->SecondsSinceLastReset =
					    0;
					pHBAPortStats->TxFrames = 1;
					pHBAPortStats->TxWords = 2;
					pHBAPortStats->RxFrames = 3;
					pHBAPortStats->RxWords = 4;
					pHBAPortStats->LIPCount = 5;
					pHBAPortStats->NOSCount = 6;
					pHBAPortStats->ErrorFrames = 7;
					pHBAPortStats->DumpedFrames = 8;
					pHBAPortStats->LinkFailureCount = 9;
					pHBAPortStats->LossOfSyncCount = 10;
					pHBAPortStats->LossOfSignalCount = 11;
					pHBAPortStats->
					    PrimitiveSeqProtocolErrCount = 12;
					pHBAPortStats->InvalidTxWordCount = 13;
					pHBAPortStats->InvalidCRCCount = 14;

					pBuffer2 += InstanceSize;
					*pInstanceLenArr++ =
					    sizeof (MSFC_HBAPortStatistics);
				}
			} else {
				status = SRB_STATUS_DATA_OVERRUN;
			}

			break;
		}

		case IdxMSFC_FibrePortHBAAttributes_GUID: {
			PUCHAR pBuffer2 = pBuffer;

#define	FibrePortHBAAttributesNODEWWN			"VM123456"
/* Will appear as 56:4D:32:33:34:35:36:37 */
#define	FibrePortHBAAttributesPortWWN			"VM234567"
#define	FibrePortHBAAttributesPortType			0x99
#define	FibrePortHBAAttributesPortSupportedFc4Types	"VM345678"
#define	FibrePortHBAAttributesPortActiveFc4Types	"VM456789"
#define	FibrePortHBAAttributesFabricName		"VM56789A"

			InstanceSize =
			    (sizeof (MSFC_FibrePortHBAAttributes)+7)&~7;
			sizeNeeded = InstanceCount * InstanceSize;

			if (BufferAvail >= sizeNeeded) {
				LastIndex = InstanceIndex + InstanceCount;

				for (i = InstanceIndex; i < LastIndex; i++) {
					PMSFC_FibrePortHBAAttributes
					    pFibrePortHBAAttributes =
					    (PMSFC_FibrePortHBAAttributes)
					    pBuffer2;

					memset(pBuffer2, 0, InstanceSize);

					pFibrePortHBAAttributes->UniquePortId =
					    ((ULONGLONG)pHbaExtension) + i;
					pFibrePortHBAAttributes->HBAStatus =
					    HBA_STATUS_OK;

		memcpy(pFibrePortHBAAttributes->Attributes.NodeWWN,
		    FibrePortHBAAttributesNODEWWN,
		    sizeof (pFibrePortHBAAttributes->Attributes.NodeWWN));

		memcpy(pFibrePortHBAAttributes->Attributes.PortWWN,
		    FibrePortHBAAttributesPortWWN,
		    sizeof (pFibrePortHBAAttributes->Attributes.PortWWN));

					pFibrePortHBAAttributes->Attributes.
					    PortFcId = i + 0x100;
					pFibrePortHBAAttributes->Attributes.
					    PortType =
					    FibrePortHBAAttributesPortType + i;
					pFibrePortHBAAttributes->Attributes.
					    PortState = i;
					pFibrePortHBAAttributes->Attributes.
					    PortSupportedClassofService = i;

	memcpy(pFibrePortHBAAttributes->Attributes.PortSupportedFc4Types,
	    FibrePortHBAAttributesPortSupportedFc4Types,
	    sizeof (pFibrePortHBAAttributes->Attributes.PortSupportedFc4Types));

	memcpy(pFibrePortHBAAttributes->Attributes.PortActiveFc4Types,
	    FibrePortHBAAttributesPortActiveFc4Types,
	    sizeof (pFibrePortHBAAttributes->Attributes.PortActiveFc4Types));

					pFibrePortHBAAttributes->Attributes.
					    PortSupportedSpeed = i * 2;
					pFibrePortHBAAttributes->Attributes.
					    PortSpeed = i;
					pFibrePortHBAAttributes->Attributes.
					    PortMaxFrameSize = i * 4;

	memcpy(pFibrePortHBAAttributes->Attributes.FabricName,
	    FibrePortHBAAttributesFabricName,
	    sizeof (pFibrePortHBAAttributes->Attributes.FabricName));

					pFibrePortHBAAttributes->Attributes.
					    NumberofDiscoveredPorts = 1;

					pBuffer2 += InstanceSize;
				*pInstanceLenArr++ =
				    sizeof (MSFC_FibrePortHBAAttributes);
				}
			} else {
				status = SRB_STATUS_DATA_OVERRUN;
			}
			break;
		}

#define	CopyWMIString(_pDest, _pSrc, _maxlength) \
	{ \
		PUSHORT _pDestTemp = _pDest; \
		USHORT  _length = _maxlength - sizeof (USHORT); \
		*_pDestTemp++ = _length;\
		_length = (USHORT)min(wcslen(_pSrc)*sizeof (WCHAR), _length); \
		memcpy(_pDestTemp, _pSrc, _length);\
	}

		case IdxMSFC_FCAdapterHBAAttributes_GUID:
		{
			PMSFC_FCAdapterHBAAttributes pFCAdapterHBAAttributes;

			//
			// First thing to do is verify if there is enough
			// room in the output buffer to return all data
			// requested
			//
			sizeNeeded = (sizeof (MSFC_FCAdapterHBAAttributes));

			if (BufferAvail >= sizeNeeded) {
#define	FCAdapterHBAAttributesNODEWWN	"12345678"
#define	VENDORID			0x1234
#define	PRODUCTID			0x5678
#define	MANUFACTURER			L"OpenZFS"
#define	SERIALNUMBER			L"ZVOL SerialNumber"
#define	MODEL				L"ZVOL Model"
#define	MODELDESCRIPTION		L"ZVOL ModelDescription"
#define	NODESYMBOLICNAME		L"ZVOL NodeSymbolicName"
#define	HARDWAREVERSION			L"ZVOL HardwareVersion"
#define	DRIVERVERSION			L"ZVOL DriverVersion"
#define	OPTIONROMVERSION		L"ZVOL OptionROMVersion"
#define	DRIVERNAME			L"ZVOL DriverName"
#define	FIRMWAREVERSION			L"ZVOL FirmwareVersion"
#define	MFRDOMAIN			L"ZVOL MfrDomain"

				//
				// We know there is always only 1 instance
				// for this guid
				//
				pFCAdapterHBAAttributes =
				    (PMSFC_FCAdapterHBAAttributes)pBuffer;
				memset(pBuffer, 0, sizeNeeded);
				pFCAdapterHBAAttributes->UniqueAdapterId =
				    (ULONGLONG)pHbaExtension;

				pFCAdapterHBAAttributes->HBAStatus =
				    HBA_STATUS_OK;

				memcpy(pFCAdapterHBAAttributes->NodeWWN,
				    FCAdapterHBAAttributesNODEWWN,
				    sizeof (pFCAdapterHBAAttributes->NodeWWN));

				pFCAdapterHBAAttributes->VendorSpecificID =
				    VENDORID | (PRODUCTID<<16);

				pFCAdapterHBAAttributes->NumberOfPorts =
				    NUMBEROFPORTS;

				pBfrX = pFCAdapterHBAAttributes->Manufacturer;

	CopyWMIString(pFCAdapterHBAAttributes->Manufacturer,
	    MANUFACTURER,
	    sizeof (pFCAdapterHBAAttributes->Manufacturer));

	CopyWMIString(pFCAdapterHBAAttributes->SerialNumber,
	    SERIALNUMBER,
	    sizeof (pFCAdapterHBAAttributes->SerialNumber));

	CopyWMIString(pFCAdapterHBAAttributes->Model,
	    MODEL,
	    sizeof (pFCAdapterHBAAttributes->Model));

	CopyWMIString(pFCAdapterHBAAttributes->ModelDescription,
	    MODELDESCRIPTION,
	    sizeof (pFCAdapterHBAAttributes->ModelDescription));

	CopyWMIString(pFCAdapterHBAAttributes->NodeSymbolicName,
	    NODESYMBOLICNAME,
	    sizeof (pFCAdapterHBAAttributes->NodeSymbolicName));

	CopyWMIString(pFCAdapterHBAAttributes->HardwareVersion,
	    HARDWAREVERSION,
	    sizeof (pFCAdapterHBAAttributes->HardwareVersion));

	CopyWMIString(pFCAdapterHBAAttributes->DriverVersion,
	    DRIVERVERSION,
	    sizeof (pFCAdapterHBAAttributes->DriverVersion));

	CopyWMIString(pFCAdapterHBAAttributes->OptionROMVersion,
	    OPTIONROMVERSION,
	    sizeof (pFCAdapterHBAAttributes->OptionROMVersion));

	CopyWMIString(pFCAdapterHBAAttributes->FirmwareVersion,
	    FIRMWAREVERSION,
	    sizeof (pFCAdapterHBAAttributes->FirmwareVersion));

	CopyWMIString(pFCAdapterHBAAttributes->DriverName,
	    DRIVERNAME,
	    sizeof (pFCAdapterHBAAttributes->DriverName));

	CopyWMIString(pFCAdapterHBAAttributes->MfgDomain,
	    MFRDOMAIN,
	    sizeof (pFCAdapterHBAAttributes->MfgDomain));

				pInstanceLenArr[0] =
				    sizeof (MSFC_FCAdapterHBAAttributes);

			} else {
				status = SRB_STATUS_DATA_OVERRUN;
			}
			break;
		}

		case IdxMSFC_HBAFCPInfo_GUID:
		case IdxMSFC_FibrePortHBAMethods_GUID:
		case IdxMSFC_HBAAdapterMethods_GUID:
		{
			//
			// Methods don't return data per se, but must respond to
			// queries with an empty data block. We know that all of
			// these method guids only have one instance
			//
			sizeNeeded = sizeof (ULONG);

			if (BufferAvail >= sizeNeeded) {
				pInstanceLenArr[0] = sizeNeeded;
			} else {
				status = SRB_STATUS_DATA_OVERRUN;
			}
			break;
		}

		default:
			status = SRB_STATUS_ERROR;
			break;
	}

	SpUpdateWmiRequest(pHbaExtension, pSrb, pDispatchContext,
	    status, sizeNeeded);

	return (SRB_STATUS_PENDING);
}

UCHAR
SetWmiDataBlock(
    __in PVOID pContext,
    __in PSCSIWMI_REQUEST_CONTEXT pDispatchContext,
    __in ULONG GuidIndex,
    __in ULONG InstanceIndex,
    __in ULONG BufferSize,
    __in PUCHAR pBuffer)
{
	pHW_HBA_EXT pHbaExtension = (pHW_HBA_EXT)pContext;
	PSCSI_WMI_REQUEST_BLOCK pSrb =
	    (PSCSI_WMI_REQUEST_BLOCK)pDispatchContext->UserContext;
	UCHAR status = SRB_STATUS_SUCCESS;
	ULONG sizeNeeded = 0;

	UNREFERENCED_PARAMETER(InstanceIndex);
	UNREFERENCED_PARAMETER(BufferSize);
	UNREFERENCED_PARAMETER(pBuffer);

	switch (GuidIndex) {

		default:

			status = SRB_STATUS_INVALID_REQUEST;
			break;
	}

	SpUpdateWmiRequest(pHbaExtension, pSrb, pDispatchContext,
	    status, sizeNeeded);

	return (SRB_STATUS_PENDING);
}

UCHAR
ExecuteWmiMethod(
    __in PVOID pContext,
    __in PSCSIWMI_REQUEST_CONTEXT pDispatchContext,
    __in ULONG GuidIndex,
    __in ULONG InstanceIndex,
    __in ULONG MethodId,
    __in ULONG InBufferSize,
    __in ULONG OutBufferSize,
    __in __out PUCHAR pBuffer)
{
	pHW_HBA_EXT pHbaExtension = (pHW_HBA_EXT)pContext;
	PSCSI_WMI_REQUEST_BLOCK pSrb =
	    (PSCSI_WMI_REQUEST_BLOCK)pDispatchContext->UserContext;
	ULONG sizeNeeded = 0,
	    i;
	UCHAR status = SRB_STATUS_SUCCESS;

	UNREFERENCED_PARAMETER(InstanceIndex);

	switch (GuidIndex) {

		case IdxMSFC_HBAFCPInfo_GUID:

			switch (MethodId) {

				// The next is the source of LUN that FCInfo
				// gets (via HBAAPI!HbapGetFcpTargets).

		case GetFcpTargetMapping: {
#define	PORTNAME	L"VirtMini FibrePortName"
#define	OSDEVICENAME	L"VirtMini OsDeviceName"
#define	LuidNAME	"VirtMini Dummy LUID"

#define	FcpIdNODEWWN	"23456789"
#define	FcpIdPORTWWN	"3456789A"

			sizeNeeded = FIELD_OFFSET(
			    GetFcpTargetMapping_OUT,
			    Entry) +
			    (pHbaExtension->NbrLUNsperHBA *
			    sizeof (HBAFCPScsiEntry));

			if (OutBufferSize >= sizeNeeded) {
				PGetFcpTargetMapping_IN  pIn  =
				    (PGetFcpTargetMapping_IN)
				    pBuffer;
				PGetFcpTargetMapping_OUT pOut =
				    (PGetFcpTargetMapping_OUT)
				    pBuffer;

				pIn;

				RtlZeroMemory(pOut, OutBufferSize);

				pOut->HBAStatus = HBA_STATUS_OK;

				pOut->TotalEntryCount =
				    pHbaExtension->NbrLUNsperHBA;

				pOut->OutEntryCount =
				    pHbaExtension->NbrLUNsperHBA;

				for (i = 0; i < pHbaExtension->NbrLUNsperHBA;
				    i++) {
					// Construct the present HBAFCPID.

					pOut->Entry[i].FCPId.Fcid = i;

				memcpy(pOut->Entry[i].FCPId.NodeWWN,
				    FcpIdNODEWWN,
				    sizeof (pOut->Entry[i].FCPId.NodeWWN));

				memcpy(pOut->Entry[i].FCPId.PortWWN,
				    FcpIdPORTWWN,
				    sizeof (pOut->Entry[i].FCPId.PortWWN));

					pOut->Entry[i].FCPId.FcpLun = i;

					// Construct Luid.

					memcpy(pOut->Entry[i].Luid, LuidNAME,
					    min(sizeof (pOut->Entry[i].Luid),
					    sizeof (LuidNAME)-1));

					// Construct the present HBAScsiID.

					pOut->Entry[i].ScsiId.ScsiBusNumber = 0;
					pOut->Entry[i].ScsiId.ScsiTargetNumber =
					    0;
					pOut->Entry[i].ScsiId.ScsiOSLun = i;
				}
			} else {
				status = SRB_STATUS_DATA_OVERRUN;
			}

			break;
		}

				default:

					__try {
						DbgBreakPoint();
					}
					__except(EXCEPTION_EXECUTE_HANDLER) {
					}

					status = SRB_STATUS_INVALID_REQUEST;
					break;
			}

			break;

		case IdxMSFC_HBAAdapterMethods_GUID:

			switch (MethodId) {

				case GetDiscoveredPortAttributes: {
					PGetDiscoveredPortAttributes_OUT pOut =
					    (PGetDiscoveredPortAttributes_OUT)
					    pBuffer;
		sizeNeeded =
		    GetDiscoveredPortAttributes_OUT_SIZE;

					if (OutBufferSize >= sizeNeeded) {
						memset(pOut, 0, sizeNeeded);

						// since this is a virtual
						// driver with no discovered
						// ports,always return an error
		pOut->HBAStatus =
		    HBA_STATUS_ERROR_ILLEGAL_INDEX;
					} else {
						status =
						    SRB_STATUS_DATA_OVERRUN;
					}

					break;
				}

				case RefreshInformation: {
					break;
				}

				case ScsiInquiry: {
					PScsiInquiry_OUT pOut =
					    (PScsiInquiry_OUT)pBuffer;
					PScsiInquiry_IN pIn  =
					    (PScsiInquiry_IN)pBuffer;
					PINQUIRYDATA pInqData =
					    (PINQUIRYDATA)pOut->ResponseBuffer;
					struct _CDB6INQUIRY3 *pCdbInq =
					    (struct _CDB6INQUIRY3 *)pBuffer;

					sizeNeeded =
					    FIELD_OFFSET(ScsiInquiry_OUT,
					    ResponseBuffer) +
					    sizeof (INQUIRYDATA);

					if (OutBufferSize < sizeNeeded) {
						status =
						    SRB_STATUS_DATA_OVERRUN;
// Taken from \\tkcsssrcidx01\windows2008r2-windows7\rtm\drivers\
// oem\src\storage\elxstor\elxstorwmi.c,
//   ElxWmiScsiOperations().
						pIn = NULL;
						pCdbInq = NULL;

						break;
					}

					memset(pOut, 0, OutBufferSize);

					pOut->HBAStatus = HBA_STATUS_OK;
					pOut->ResponseBufferSize =
					    sizeof (INQUIRYDATA);
					pOut->SenseBufferSize = 0;
					pOut->ScsiStatus = 0;

					pInqData->DeviceType = DISK_DEVICE;
					pInqData->RemovableMedia = FALSE;
					pInqData->CommandQueue = TRUE;

			RtlMoveMemory(pInqData->VendorId,
			    VENDOR_ID_ascii,
			    sizeof (pInqData->VendorId));
			RtlMoveMemory(pInqData->ProductId,
			    PRODUCT_ID_ascii,
			    sizeof (pInqData->ProductId));
			RtlMoveMemory(pInqData->ProductRevisionLevel,
			    PRODUCT_REV_ascii,
			    sizeof (pInqData->ProductRevisionLevel));

					break;
				}

		case SendCTPassThru: {

#define	minSizeNeeded 0x1000

//
// Derived from \\tkcsssrcidx01\windows2008r2-windows7\rtm\drivers\
// storage\hbaapi\dll\ctpass.h
//

// Response codes, see 4.3.1.6 FC-GS3. Note they are reversed since
// they are little endian in the packet.

#define	CTREJECT  0x0180
#define	CTACCESPT 0x0280

// CT Passthru definitions. See section 4.3 in FC-GS3

	typedef struct _CTPREAMBLE {
		UCHAR Revision;
		UCHAR IN_ID[3];
		UCHAR GS_Type;
		UCHAR GS_SubType;
		UCHAR Options;
		UCHAR Reserved1;
		USHORT CommandResponse;
		union {
			USHORT MaxResidualSize;
			USHORT MaxSize;
		} _xx;
		UCHAR Reserved2;
		UCHAR Reason;
		UCHAR ReasonExplaination;
		UCHAR VendorSpecific;
	} CTPREAMBLE, *PCTPREAMBLE;

	typedef struct _CTPASSTHRU_GSPN_ID_ACCEPT {
		CTPREAMBLE Preamble;
		UCHAR SymbolicNameLen;
		UCHAR SymbolicName[1];    // symbolic name
	} CTPASSTHRU_GSPN_ID_ACCEPT, *PCTPASSTHRU_GSPN_ID_ACCEPT;

	PSendCTPassThru_IN  pIn;
	PSendCTPassThru_OUT pOut;
	ULONG RequestCount,
	    ResponseCount;

	if (InBufferSize >= sizeof (ULONG)) {
		pIn = (PSendCTPassThru_IN)pBuffer;

		RequestCount = pIn->RequestBufferCount;
		sizeNeeded = FIELD_OFFSET(SendCTPassThru_IN,
		    RequestBuffer) + RequestCount;

		if (InBufferSize >= sizeNeeded) {
#define	RESPONSE_BUFFER_SIZE 0x1000

			ResponseCount = RESPONSE_BUFFER_SIZE;
			sizeNeeded = FIELD_OFFSET(SendCTPassThru_OUT,
			    ResponseBuffer) + ResponseCount;

			if (OutBufferSize >= sizeNeeded) {
				PCTPASSTHRU_GSPN_ID_ACCEPT pRespBfr;
#define	SYMBOLICNAME "VMSymName"

				pOut = (PSendCTPassThru_OUT)pBuffer;
				pOut->HBAStatus = HBA_STATUS_OK;
				pOut->TotalResponseBufferCount =
				    ResponseCount;
				pOut->ActualResponseBufferCount =
				    ResponseCount;

				pRespBfr =
				    (PCTPASSTHRU_GSPN_ID_ACCEPT)
				    pOut->ResponseBuffer;

				memset(pRespBfr, 0, ResponseCount);

				pRespBfr->Preamble.CommandResponse =
				    CTACCESPT;
				pRespBfr->SymbolicNameLen =
				    sizeof (SYMBOLICNAME)-1;
				memcpy(pRespBfr->SymbolicName,
				    SYMBOLICNAME,
				    pRespBfr->SymbolicNameLen);
			} else {
				status = SRB_STATUS_DATA_OVERRUN;
			}
		} else {
			status = SRB_STATUS_ERROR;
		}
	} else {
		sizeNeeded = minSizeNeeded;
		status = SRB_STATUS_ERROR;
	}

	break;
	}

	case ScsiReadCapacity: {
		PScsiReadCapacity_IN  pIn;
		PScsiReadCapacity_OUT pOut;
		PREAD_CAPACITY_DATA   pReadCapData;

		if (InBufferSize >= sizeof (ULONG)) {
			pIn = (PScsiReadCapacity_IN)pBuffer;
			sizeNeeded = sizeof (ScsiReadCapacity_IN);
			if (InBufferSize >= sizeNeeded) {
			sizeNeeded = FIELD_OFFSET(ScsiReadCapacity_OUT,
			    ResponseBuffer) +
			    ScsiReadCapacity_OUT_ResponseBuffer_SIZE_HINT;

				if (OutBufferSize >= sizeNeeded) {
					pOut = (PScsiReadCapacity_OUT)
					    pBuffer;
					pOut->HBAStatus = HBA_STATUS_OK;
					pOut->ResponseBufferSize =
					    sizeNeeded;
					pOut->SenseBufferSize = 0;

					pReadCapData =
					    (PREAD_CAPACITY_DATA)
					    pOut->ResponseBuffer;
					pReadCapData->
					    LogicalBlockAddress = 0;
					pReadCapData->BytesPerBlock =
					    MP_BLOCK_SIZE;
				} else {
					status =
					    SRB_STATUS_DATA_OVERRUN;
				}
			} else {
				status = SRB_STATUS_ERROR;
			}
		} else {
			sizeNeeded = minSizeNeeded;
			status = SRB_STATUS_ERROR;
		}
		break;
	}
	case SendRNID: {
		PSendRNID_IN  pIn;
		PSendRNID_OUT pOut;

		if (InBufferSize >= sizeof (ULONG)) {
			pIn = (PSendRNID_IN)pBuffer;

			sizeNeeded = sizeof (SendRNID_IN);

			if (InBufferSize >= sizeNeeded) {
				sizeNeeded = FIELD_OFFSET(
				    SendRNID_OUT, ResponseBuffer) +
				    SendRNID_OUT_ResponseBuffer_SIZE_HINT;

				if (OutBufferSize >= sizeNeeded) {
					pOut = (PSendRNID_OUT)pBuffer;

				pOut->HBAStatus = HBA_STATUS_OK;
				pOut->ResponseBufferCount =
				    SendRNID_OUT_ResponseBuffer_SIZE_HINT;

					memset(pOut->ResponseBuffer,
					    0xFF,
					    pOut->ResponseBufferCount);
				} else {
					status =
					    SRB_STATUS_DATA_OVERRUN;
				}
			} else {
				status = SRB_STATUS_ERROR;
			}
		} else {
			sizeNeeded = minSizeNeeded;
			status = SRB_STATUS_ERROR;
		}
		break;
	}

	default:
		__try {
			DbgBreakPoint();
		}
		__except(EXCEPTION_EXECUTE_HANDLER) {
		}

		status = SRB_STATUS_INVALID_REQUEST;
		break;
		}
		break;

	case IdxGmDrvDrvMethodGuid:

		switch (MethodId) {

			case GmDrvDemoMethod1: {
				PGmDrvDemoMethod1_IN  pInBfr =
				    (PGmDrvDemoMethod1_IN)pBuffer;
				PGmDrvDemoMethod1_OUT pOutBfr =
				    (PGmDrvDemoMethod1_OUT)pBuffer;

				sizeNeeded = GmDrvDemoMethod1_OUT_SIZE;

				if (OutBufferSize < sizeNeeded) {
					status =
					    SRB_STATUS_DATA_OVERRUN;
					break;
				}

				if (InBufferSize < GmDrvDemoMethod1_IN_SIZE) {
					status = SRB_STATUS_BAD_FUNCTION;
					break;
				}

				pOutBfr->outDatum = pInBfr->inDatum + 1;
				break;

			}

	case GmDrvDemoMethod2: {
		PGmDrvDemoMethod2_IN  pInBfr =
		    (PGmDrvDemoMethod2_IN)pBuffer;
		PGmDrvDemoMethod2_OUT pOutBfr =
		    (PGmDrvDemoMethod2_OUT)pBuffer;

		sizeNeeded = GmDrvDemoMethod2_OUT_SIZE;

		if (OutBufferSize < sizeNeeded) {
			status = SRB_STATUS_DATA_OVERRUN;
			break;
		}

		if (InBufferSize < GmDrvDemoMethod2_IN_SIZE) {
			status = SRB_STATUS_BAD_FUNCTION;
			break;
		}

		pOutBfr->outDatum1 = pInBfr->inDatum1 +
		    pInBfr->inDatum2 + 1;
		break;

	}

	case GmDrvDemoMethod3: {
		ULONG x1, x2;
		PGmDrvDemoMethod3_IN  pInBfr =
		    (PGmDrvDemoMethod3_IN)pBuffer;
		PGmDrvDemoMethod3_OUT pOutBfr =
		    (PGmDrvDemoMethod3_OUT)pBuffer;

		sizeNeeded = GmDrvDemoMethod3_OUT_SIZE;

		if (OutBufferSize < sizeNeeded) {
			status = SRB_STATUS_DATA_OVERRUN;
			break;
		}

		if (InBufferSize < GmDrvDemoMethod3_IN_SIZE) {
			status = SRB_STATUS_BAD_FUNCTION;
			break;
		}

		x1 = pInBfr->inDatum1 + 1;
		x2 = pInBfr->inDatum2 + 1;

		pOutBfr->outDatum1 = x1;
		pOutBfr->outDatum2 = x2;

		break;
	}

	default:
		status = SRB_STATUS_INVALID_REQUEST;
		break;
	}
	break;

	default:
		status = SRB_STATUS_INVALID_REQUEST;
		break;
	}

	SpUpdateWmiRequest(pHbaExtension, pSrb, pDispatchContext,
	    status, sizeNeeded);

	return (SRB_STATUS_PENDING);
}

void
SpUpdateWmiRequest(
    __in pHW_HBA_EXT pHbaExtension,
    __in PSCSI_WMI_REQUEST_BLOCK  pSrb,
    __in PSCSIWMI_REQUEST_CONTEXT pDispatchContext,
    __in UCHAR Status,
    __in ULONG SizeNeeded)
{
	UNREFERENCED_PARAMETER(pHbaExtension);

	// Update the request if the status is NOT pending or NOT
	// already completed within the callback.

	if (SRB_STATUS_PENDING != Status) {

		//
		// Request completed successfully or there was an error.
		//

		ScsiPortWmiPostProcess(pDispatchContext, Status, SizeNeeded);

		pSrb->SrbStatus = ScsiPortWmiGetReturnStatus(pDispatchContext);
		pSrb->DataTransferLength =
		    ScsiPortWmiGetReturnSize(pDispatchContext);
	}
}
