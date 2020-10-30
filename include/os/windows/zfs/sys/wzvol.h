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
 * Copyright (c) 2018 by Jorgen Lundman <lundman@lundman.net>.
 */

#ifndef _SPL_WZVOL_H
#define _SPL_WZVOL_H

#include <ntdef.h>  
#include <storport.h>  
#include <devioctl.h>
#include <ntddscsi.h>
#include <scsiwmi.h>



#define VENDOR_ID                   L"OpenZFS "
#define VENDOR_ID_ascii             "OpenZFS "
#define PRODUCT_ID                  L"WinZVOL         "
#define PRODUCT_ID_ascii            "WinZVOL         "
#define PRODUCT_REV                 L"1.00"
#define PRODUCT_REV_ascii           "1.00"
#define MP_TAG_GENERAL              'LOVZ'

//#define WZOL_MAX_TARGETS            SCSI_MAXIMUM_TARGETS  // 8! A bit low
#define WZOL_MAX_TARGETS            16  // 8! A bit low

#define MAX_TARGETS                 WZOL_MAX_TARGETS
#define MAX_LUNS                    24
#define MP_MAX_TRANSFER_SIZE        (32 * 1024)
#define TIME_INTERVAL               (1 * 1000 * 1000) //1 second.
#define DEVLIST_BUFFER_SIZE         1024
#define DEVICE_NOT_FOUND            0xFF
#define SECTOR_NOT_FOUND            0xFFFF

#define MINIMUM_DISK_SIZE           (1540 * 1024)    // Minimum size required for Disk Manager
#define MAXIMUM_MAP_DISK_SIZE       (256 * 1024)

#define MP_BLOCK_SIZE                  (512)
#define BUF_SIZE                    (1540 * 1024)
#define MAX_BLOCKS                  (BUF_SIZE / MP_BLOCK_SIZE)

#define DEFAULT_BREAK_ON_ENTRY      0                // No break
#define DEFAULT_DEBUG_LEVEL         2               
#define DEFAULT_INITIATOR_ID        7
#define DEFAULT_VIRTUAL_DISK_SIZE   (8 * 1024 * 1024)  // 8 MB.  JAntogni, 03.12.2005.
#define DEFAULT_PHYSICAL_DISK_SIZE  DEFAULT_VIRTUAL_DISK_SIZE
#define DEFAULT_USE_LBA_LIST        0
#define DEFAULT_NUMBER_OF_BUSES     1
#define DEFAULT_NbrVirtDisks        1
#define DEFAULT_NbrLUNsperHBA       400
#define DEFAULT_NbrLUNsperTarget    32
#define DEFAULT_bCombineVirtDisks   FALSE

#define GET_FLAG(Flags, Bit)        ((Flags) & (Bit))
#define SET_FLAG(Flags, Bit)        ((Flags) |= (Bit))
#define CLEAR_FLAG(Flags, Bit)      ((Flags) &= ~(Bit))

typedef struct _DEVICE_LIST          DEVICE_LIST, *pDEVICE_LIST;
typedef struct _wzvolDriverInfo         wzvolDriverInfo, *pwzvolDriverInfo;
typedef struct _MP_REG_INFO          MP_REG_INFO, *pMP_REG_INFO;
typedef struct _HW_LU_EXTENSION      HW_LU_EXTENSION, *pHW_LU_EXTENSION;
typedef struct _HW_LU_EXTENSION_MPIO HW_LU_EXTENSION_MPIO, *pHW_LU_EXTENSION_MPIO;
typedef struct _LBA_LIST             LBA_LIST, *PLBA_LIST;

extern
wzvolDriverInfo STOR_wzvolDriverInfo;

typedef struct _MP_REG_INFO {
	UNICODE_STRING   VendorId;
	UNICODE_STRING   ProductId;
	UNICODE_STRING   ProductRevision;
	ULONG            BreakOnEntry;       // Break into debugger
	ULONG            DebugLevel;         // Debug log level
	ULONG            InitiatorID;        // Adapter's target ID
	ULONG            VirtualDiskSize;    // Disk size to be reported
	ULONG            PhysicalDiskSize;   // Disk size to be allocated
	ULONG            NbrVirtDisks;       // Number of virtual disks.
	ULONG            NbrLUNsperHBA;      // Number of LUNs per HBA : really is the amount of zvols we can present throiugh StorPort
	ULONG            NbrLUNsperTarget;   // Number of LUNs per Target.
	ULONG            bCombineVirtDisks;  // 0 => do not combine virtual disks a la MPIO.
} WZVOL_REG_INFO, *pWZVOL_REG_INFO;

typedef struct _wzvolContext {
	PVOID	zv;
	PIO_REMOVE_LOCK pIoRemLock;
	volatile LONGLONG refCnt;
} wzvolContext, * pwzvolContext;

typedef struct _wzvolDriverInfo {                        // The master miniport object. In effect, an extension of the driver object for the miniport.
	WZVOL_REG_INFO                    wzvolRegInfo;
	KSPIN_LOCK                     DrvInfoLock;
	KSPIN_LOCK                     MPIOExtLock;       // Lock for ListMPIOExt, header of list of HW_LU_EXTENSION_MPIO objects, 
	KSPIN_LOCK                     SrbExtLock;        // Lock for ListSrbExt
	LIST_ENTRY                     ListMPHBAObj;      // Header of list of HW_HBA_EXT objects.
	LIST_ENTRY                     ListMPIOExt;       // Header of list of HW_LU_EXTENSION_MPIO objects.
	LIST_ENTRY					   ListSrbExt;		  // Heade rof List of HW_SRB_EXTENSION
	PDRIVER_OBJECT                 pDriverObj;
	wzvolContext				*zvContextArray;
	ULONG                          DrvInfoNbrMPHBAObj;// Count of items in ListMPHBAObj.
	ULONG                          DrvInfoNbrMPIOExtObj; // Count of items in ListMPIOExt.
	UCHAR						MaximumNumberOfLogicalUnits;
	UCHAR						MaximumNumberOfTargets;
	UCHAR						NumberOfBuses;
} wzvolDriverInfo, *pwzvolDriverInfo;

typedef struct _LUNInfo {
	UCHAR     bReportLUNsDontUse;
	UCHAR     bIODontUse;
} LUNInfo, *pLUNInfo;


#define DISK_DEVICE         0x00

typedef struct _MP_DEVICE_INFO {
	UCHAR    DeviceType;
	UCHAR    TargetID;
	UCHAR    LunID;
} MP_DEVICE_INFO, *pMP_DEVICE_INFO;

typedef struct _MP_DEVICE_LIST {
	ULONG          DeviceCount;
	MP_DEVICE_INFO DeviceInfo[1];
} MP_DEVICE_LIST, *pMP_DEVICE_LIST;

#define LUNInfoMax 8

typedef struct _HW_HBA_EXT {                          // Adapter device-object extension allocated by StorPort.
	LIST_ENTRY                     List;              // Pointers to next and previous HW_HBA_EXT objects.
	LIST_ENTRY                     LUList;            // Pointers to HW_LU_EXTENSION objects.
	LIST_ENTRY                     MPIOLunList;
	pwzvolDriverInfo               pwzvolDrvObj;
	PDRIVER_OBJECT                 pDrvObj;
	SCSI_WMILIB_CONTEXT            WmiLibContext;
	PIRP                           pReverseCallIrp;
	KSPIN_LOCK                     WkItemsLock;
	KSPIN_LOCK                     WkRoutinesLock;
	KSPIN_LOCK                     MPHBAObjLock;
	KSPIN_LOCK                     LUListLock;
	ULONG                          SRBsSeen;
	ULONG                          WMISRBsSeen;
	ULONG                          NbrMPIOLuns;
	ULONG                          NbrLUNsperHBA;
	ULONG                          Test;
	UCHAR                          HostTargetId;
	UCHAR                          AdapterState;
	UCHAR                          VendorId[9];
	UCHAR                          ProductId[17];
	UCHAR                          ProductRevision[5];


	BOOLEAN                        bDontReport;       // TRUE => no Report LUNs.
	BOOLEAN                        bReportAdapterDone;
	LUNInfo                        LUNInfoArray[LUNInfoMax]; // To be set only by a kernel debugger.
} HW_HBA_EXT, *pHW_HBA_EXT;

typedef struct _HW_LU_EXTENSION_MPIO {                // Collector for LUNs that are represented by MPIO as 1 pseudo-LUN.
	LIST_ENTRY            List;                       // Pointers to next and previous HW_LU_EXTENSION_MPIO objects.
	LIST_ENTRY            LUExtList;                  // Header of list of HW_LU_EXTENSION objects.
	KSPIN_LOCK            LUExtMPIOLock;
	ULONG                 NbrRealLUNs;
	SCSI_ADDRESS          ScsiAddr;
	PUCHAR                pDiskBuf;
	USHORT                MaxBlocks;
	BOOLEAN               bIsMissingOnAnyPath;        // At present, this is set only by a kernel debugger, for testing.
} HW_LU_EXTENSION_MPIO, *pHW_LU_EXTENSION_MPIO;

// Flag definitions for LUFlags.

#define LU_DEVICE_INITIALIZED   0x0001
#define LU_MPIO_MAPPED          0x0004

typedef struct _HW_LU_EXTENSION {                     // LUN extension allocated by StorPort.
	LIST_ENTRY            List;                       // Pointers to next and previous HW_LU_EXTENSION objects, used in HW_HBA_EXT.
	LIST_ENTRY            MPIOList;                   // Pointers to next and previous HW_LU_EXTENSION objects, used in HW_LU_EXTENSION_MPIO.
	pHW_LU_EXTENSION_MPIO pLUMPIOExt;
	PUCHAR                pDiskBuf;
	ULONG                 LUFlags;
	USHORT                MaxBlocks;
	USHORT                BlocksUsed;
	BOOLEAN               bIsMissing;                 // At present, this is set only by a kernel debugger, for testing.
	UCHAR                 DeviceType;
	UCHAR                 TargetId;
	UCHAR                 Lun;
} HW_LU_EXTENSION, *pHW_LU_EXTENSION;


typedef enum {
	ActionRead,
	ActionWrite
} MpWkRtnAction;

typedef struct _MP_WorkRtnParms {
	pHW_HBA_EXT          pHBAExt;
	PSCSI_REQUEST_BLOCK  pSrb;
	PEPROCESS            pReqProcess;
	MpWkRtnAction        Action;
	ULONG                SecondsToDelay;
	CHAR				 pQueueWorkItem[1]; // IO_WORKITEM structure: keep at the end of this block (dynamically allocated).
} MP_WorkRtnParms, *pMP_WorkRtnParms;

typedef struct _HW_SRB_EXTENSION {
	SCSIWMI_REQUEST_CONTEXT WmiRequestContext;
	LIST_ENTRY  QueuedForProcessing;
	volatile ULONG Cancelled;
	PSCSI_REQUEST_BLOCK pSrbBackPtr;
	MP_WorkRtnParms WkRtnParms; // keep at the end of this block (pQueueWorkItem dynamically allocated). 
} HW_SRB_EXTENSION, *PHW_SRB_EXTENSION;

enum ResultType {
	ResultDone,
	ResultQueued
};

#define RegWkBfrSz  0x1000

typedef struct _RegWorkBuffer {
	pHW_HBA_EXT          pAdapterExt;
	UCHAR                Work[256];
} RegWorkBuffer, *pRegWorkBuffer;


ULONG
wzvol_HwFindAdapter(
	__in       pHW_HBA_EXT DevExt,
	__in       PVOID HwContext,
	__in       PVOID BusInfo,
	__in       PVOID LowerDevice,
	__in       PCHAR ArgumentString,
	__in __out PPORT_CONFIGURATION_INFORMATION ConfigInfo,
	__out PBOOLEAN Again
);

VOID
wzvol_HwTimer(
	__in pHW_HBA_EXT DevExt
);

BOOLEAN
wzvol_HwInitialize(
	__in pHW_HBA_EXT
);

void
wzvol_HwReportAdapter(
	__in pHW_HBA_EXT
);

void
wzvol_HwReportLink(
	__in pHW_HBA_EXT
);

void
wzvol_HwReportLog(__in pHW_HBA_EXT);

VOID
wzvol_HwFreeAdapterResources(
	__in pHW_HBA_EXT
);

BOOLEAN
wzvol_HwStartIo(
	__in pHW_HBA_EXT,
	__in PSCSI_REQUEST_BLOCK
);

BOOLEAN
wzvol_HwResetBus(
	__in pHW_HBA_EXT,
	__in ULONG
);

SCSI_ADAPTER_CONTROL_STATUS
wzvol_HwAdapterControl(
	__in pHW_HBA_EXT DevExt,
	__in SCSI_ADAPTER_CONTROL_TYPE ControlType,
	__in PVOID Parameters
);

UCHAR
ScsiExecuteMain(
	__in pHW_HBA_EXT DevExt,
	__in PSCSI_REQUEST_BLOCK,
	__in PUCHAR
);

UCHAR
ScsiExecute(
	__in pHW_HBA_EXT DevExt,
	__in PSCSI_REQUEST_BLOCK Srb
);

UCHAR
ScsiOpInquiry(
	__in pHW_HBA_EXT DevExt,
	__in PSCSI_REQUEST_BLOCK Srb
);

UCHAR
ScsiOpReadCapacity(
	IN pHW_HBA_EXT DevExt,
	IN PSCSI_REQUEST_BLOCK Srb
);

UCHAR
ScsiOpReadCapacity16(
	IN pHW_HBA_EXT DevExt,
	IN PSCSI_REQUEST_BLOCK Srb
);

UCHAR
ScsiOpRead(
	IN pHW_HBA_EXT          DevExt,
	IN PSCSI_REQUEST_BLOCK  Srb,
	IN PUCHAR               Action
);

UCHAR
ScsiOpWrite(
	IN pHW_HBA_EXT          DevExt,
	IN PSCSI_REQUEST_BLOCK  Srb,
	IN PUCHAR               Action
);

UCHAR
ScsiOpModeSense(
	IN pHW_HBA_EXT         DevExt,
	IN PSCSI_REQUEST_BLOCK pSrb
);

UCHAR
ScsiOpReportLuns(
	IN pHW_HBA_EXT          DevExt,
	IN PSCSI_REQUEST_BLOCK  Srb
);

VOID
wzvol_QueryRegParameters(
	IN PUNICODE_STRING,
	IN pMP_REG_INFO
);

NTSTATUS
wzvol_CreateDeviceList(
	__in       pHW_HBA_EXT,
	__in       ULONG
);

UCHAR
wzvol_GetDeviceType(
	__in pHW_HBA_EXT DevExt,
	__in UCHAR PathId,
	__in UCHAR TargetId,
	__in UCHAR Lun
);

UCHAR wzvol_FindRemovedDevice(
	__in pHW_HBA_EXT,
	__in PSCSI_REQUEST_BLOCK
);

VOID wzvol_StopAdapter(
	__in pHW_HBA_EXT DevExt
);

VOID
wzvol_TracingInit(
	__in PVOID,
	__in PVOID
);

VOID
wzvol_TracingCleanup(__in PVOID);

VOID
wzvol_ProcServReq(
	__in pHW_HBA_EXT,
	__in PIRP
);

VOID
wzvol_CompServReq(
	__in pHW_HBA_EXT
);

UCHAR
ScsiOpVPD(
	__in pHW_HBA_EXT,
	__in PSCSI_REQUEST_BLOCK,
	__in PVOID
);

void
InitializeWmiContext(__in pHW_HBA_EXT);

BOOLEAN
HandleWmiSrb(
	__in       pHW_HBA_EXT,
	__in __out PSCSI_WMI_REQUEST_BLOCK
);

UCHAR
ScsiReadWriteSetup(
	__in pHW_HBA_EXT          pDevExt,
	__in PSCSI_REQUEST_BLOCK  pSrb,
	__in MpWkRtnAction        WkRtnAction,
	__in PUCHAR               pResult
);

VOID
wzvol_GeneralWkRtn(
	__in PVOID,
	__in PVOID
);
ULONG
wzvol_ThreadWkRtn(__in PVOID);

VOID
wzvol_WkRtn(IN PVOID);

VOID
wzvol_CompleteIrp(
	__in pHW_HBA_EXT,
	__in PIRP
);

VOID
wzvol_QueueServiceIrp(
	__in pHW_HBA_EXT          pDevExt,
	__in PIRP                 pIrp
);

VOID
wzvol_ProcServReq(
	__in pHW_HBA_EXT          pDevExt,
	__in PIRP                 pIrp
);

VOID
wzvol_CompServReq(
	__in pHW_HBA_EXT pDevExt
);

extern int zvol_start(PDRIVER_OBJECT  DriverObject, PUNICODE_STRING pRegistryPath);


#endif
