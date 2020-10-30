#ifndef _mpwmi_h_
#define _mpwmi_h_

// MSFC_HBAPortStatistics - MSFC_HBAPortStatistics


//***************************************************************************
//
//  hbapiwmi.h
// 
//  Module: WDM classes to expose HBA api data from drivers
//
//  Purpose: Contains WDM classes that specify the HBA data to be exposed 
//           via the HBA api set.
//
//  NOTE: This file contains information that is based upon:
//        SM-HBA Version 1.0 and FC-HBA 2.18 specification.
//
//        Please specify which WMI interfaces the provider will implement by
//        defining MS_SM_HBA_API or MSFC_HBA_API before including this file.
//        That is:
//
//        #define MS_SM_HBA_API
//        #include <hbapiwmi.h>
//
//        - or -
//
//        #define MSFC_HBA_API
//        #include <hbapiwmi.h>
//
//
//  Copyright (c) 2001 Microsoft Corporation
//
//***************************************************************************


#define MSFC_HBAPortStatisticsGuid \
    { 0x3ce7904f,0x459f,0x480d, { 0x9a,0x3c,0x01,0x3e,0xde,0x3b,0xdd,0xe8 } }

#if ! (defined(MIDL_PASS))
DEFINE_GUID(MSFC_HBAPortStatistics_GUID, \
            0x3ce7904f,0x459f,0x480d,0x9a,0x3c,0x01,0x3e,0xde,0x3b,0xdd,0xe8);
#endif


typedef struct _MSFC_HBAPortStatistics
{
    // 
    LONGLONG SecondsSinceLastReset;
    #define MSFC_HBAPortStatistics_SecondsSinceLastReset_SIZE sizeof(LONGLONG)
    #define MSFC_HBAPortStatistics_SecondsSinceLastReset_ID 1

    // 
    LONGLONG TxFrames;
    #define MSFC_HBAPortStatistics_TxFrames_SIZE sizeof(LONGLONG)
    #define MSFC_HBAPortStatistics_TxFrames_ID 2

    // 
    LONGLONG TxWords;
    #define MSFC_HBAPortStatistics_TxWords_SIZE sizeof(LONGLONG)
    #define MSFC_HBAPortStatistics_TxWords_ID 3

    // 
    LONGLONG RxFrames;
    #define MSFC_HBAPortStatistics_RxFrames_SIZE sizeof(LONGLONG)
    #define MSFC_HBAPortStatistics_RxFrames_ID 4

    // 
    LONGLONG RxWords;
    #define MSFC_HBAPortStatistics_RxWords_SIZE sizeof(LONGLONG)
    #define MSFC_HBAPortStatistics_RxWords_ID 5

    // 
    LONGLONG LIPCount;
    #define MSFC_HBAPortStatistics_LIPCount_SIZE sizeof(LONGLONG)
    #define MSFC_HBAPortStatistics_LIPCount_ID 6

    // 
    LONGLONG NOSCount;
    #define MSFC_HBAPortStatistics_NOSCount_SIZE sizeof(LONGLONG)
    #define MSFC_HBAPortStatistics_NOSCount_ID 7

    // 
    LONGLONG ErrorFrames;
    #define MSFC_HBAPortStatistics_ErrorFrames_SIZE sizeof(LONGLONG)
    #define MSFC_HBAPortStatistics_ErrorFrames_ID 8

    // 
    LONGLONG DumpedFrames;
    #define MSFC_HBAPortStatistics_DumpedFrames_SIZE sizeof(LONGLONG)
    #define MSFC_HBAPortStatistics_DumpedFrames_ID 9

    // 
    LONGLONG LinkFailureCount;
    #define MSFC_HBAPortStatistics_LinkFailureCount_SIZE sizeof(LONGLONG)
    #define MSFC_HBAPortStatistics_LinkFailureCount_ID 10

    // 
    LONGLONG LossOfSyncCount;
    #define MSFC_HBAPortStatistics_LossOfSyncCount_SIZE sizeof(LONGLONG)
    #define MSFC_HBAPortStatistics_LossOfSyncCount_ID 11

    // 
    LONGLONG LossOfSignalCount;
    #define MSFC_HBAPortStatistics_LossOfSignalCount_SIZE sizeof(LONGLONG)
    #define MSFC_HBAPortStatistics_LossOfSignalCount_ID 12

    // 
    LONGLONG PrimitiveSeqProtocolErrCount;
    #define MSFC_HBAPortStatistics_PrimitiveSeqProtocolErrCount_SIZE sizeof(LONGLONG)
    #define MSFC_HBAPortStatistics_PrimitiveSeqProtocolErrCount_ID 13

    // 
    LONGLONG InvalidTxWordCount;
    #define MSFC_HBAPortStatistics_InvalidTxWordCount_SIZE sizeof(LONGLONG)
    #define MSFC_HBAPortStatistics_InvalidTxWordCount_ID 14

    // 
    LONGLONG InvalidCRCCount;
    #define MSFC_HBAPortStatistics_InvalidCRCCount_SIZE sizeof(LONGLONG)
    #define MSFC_HBAPortStatistics_InvalidCRCCount_ID 15

} MSFC_HBAPortStatistics, *PMSFC_HBAPortStatistics;

#define MSFC_HBAPortStatistics_SIZE (FIELD_OFFSET(MSFC_HBAPortStatistics, InvalidCRCCount) + MSFC_HBAPortStatistics_InvalidCRCCount_SIZE)

// HBAFC3MgmtInfo - HBAFC3MgmtInfo
#define HBAFC3MgmtInfoGuid \
    { 0x5966a24f,0x6aa5,0x418e, { 0xb7,0x5c,0x2f,0x21,0x4d,0xfb,0x4b,0x18 } }

#if ! (defined(MIDL_PASS))
DEFINE_GUID(HBAFC3MgmtInfo_GUID, \
            0x5966a24f,0x6aa5,0x418e,0xb7,0x5c,0x2f,0x21,0x4d,0xfb,0x4b,0x18);
#endif


typedef struct _HBAFC3MgmtInfo
{
    // 
    ULONGLONG UniqueAdapterId;
    #define HBAFC3MgmtInfo_UniqueAdapterId_SIZE sizeof(ULONGLONG)
    #define HBAFC3MgmtInfo_UniqueAdapterId_ID 1

    // 
    UCHAR wwn[8];
    #define HBAFC3MgmtInfo_wwn_SIZE sizeof(UCHAR[8])
    #define HBAFC3MgmtInfo_wwn_ID 2

    // 
    ULONG unittype;
    #define HBAFC3MgmtInfo_unittype_SIZE sizeof(ULONG)
    #define HBAFC3MgmtInfo_unittype_ID 3

    // 
    ULONG PortId;
    #define HBAFC3MgmtInfo_PortId_SIZE sizeof(ULONG)
    #define HBAFC3MgmtInfo_PortId_ID 4

    // 
    ULONG NumberOfAttachedNodes;
    #define HBAFC3MgmtInfo_NumberOfAttachedNodes_SIZE sizeof(ULONG)
    #define HBAFC3MgmtInfo_NumberOfAttachedNodes_ID 5

    // 
    USHORT IPVersion;
    #define HBAFC3MgmtInfo_IPVersion_SIZE sizeof(USHORT)
    #define HBAFC3MgmtInfo_IPVersion_ID 6

    // 
    USHORT UDPPort;
    #define HBAFC3MgmtInfo_UDPPort_SIZE sizeof(USHORT)
    #define HBAFC3MgmtInfo_UDPPort_ID 7

    // 
    UCHAR IPAddress[16];
    #define HBAFC3MgmtInfo_IPAddress_SIZE sizeof(UCHAR[16])
    #define HBAFC3MgmtInfo_IPAddress_ID 8

    // 
    USHORT reserved;
    #define HBAFC3MgmtInfo_reserved_SIZE sizeof(USHORT)
    #define HBAFC3MgmtInfo_reserved_ID 9

    // 
    USHORT TopologyDiscoveryFlags;
    #define HBAFC3MgmtInfo_TopologyDiscoveryFlags_SIZE sizeof(USHORT)
    #define HBAFC3MgmtInfo_TopologyDiscoveryFlags_ID 10

    // 
    ULONG reserved1;
    #define HBAFC3MgmtInfo_reserved1_SIZE sizeof(ULONG)
    #define HBAFC3MgmtInfo_reserved1_ID 11

} HBAFC3MgmtInfo, *PHBAFC3MgmtInfo;

#define HBAFC3MgmtInfo_SIZE (FIELD_OFFSET(HBAFC3MgmtInfo, reserved1) + HBAFC3MgmtInfo_reserved1_SIZE)

// HBAScsiID - HBAScsiID
#define HBAScsiIDGuid \
    { 0xa76f5058,0xb1f0,0x4622, { 0x9e,0x88,0x5c,0xc4,0x1e,0x34,0x45,0x4a } }

#if ! (defined(MIDL_PASS))
DEFINE_GUID(HBAScsiID_GUID, \
            0xa76f5058,0xb1f0,0x4622,0x9e,0x88,0x5c,0xc4,0x1e,0x34,0x45,0x4a);
#endif


typedef struct _HBAScsiID
{
    // 
    ULONG ScsiBusNumber;
    #define HBAScsiID_ScsiBusNumber_SIZE sizeof(ULONG)
    #define HBAScsiID_ScsiBusNumber_ID 1

    // 
    ULONG ScsiTargetNumber;
    #define HBAScsiID_ScsiTargetNumber_SIZE sizeof(ULONG)
    #define HBAScsiID_ScsiTargetNumber_ID 2

    // 
    ULONG ScsiOSLun;
    #define HBAScsiID_ScsiOSLun_SIZE sizeof(ULONG)
    #define HBAScsiID_ScsiOSLun_ID 3



   //******************************************************************
   //
   //  This used to be a string type, but we made this a fixed length
   //  array so the WmiSizeIs() will work correctly for structs that 
   //  contain this type.
   //  Please note that this should still be treated as a string.
   //  The first uint16 must hold the length of string (in bytes).
   //
   //******************************************************************


    // 
    USHORT OSDeviceName[257];
    #define HBAScsiID_OSDeviceName_SIZE sizeof(USHORT[257])
    #define HBAScsiID_OSDeviceName_ID 4

} HBAScsiID, *PHBAScsiID;

#define HBAScsiID_SIZE (FIELD_OFFSET(HBAScsiID, OSDeviceName) + HBAScsiID_OSDeviceName_SIZE)

// MSFC_LinkEvent - MSFC_LinkEvent




//
// Event types.
//
// These match the definitions in hbaapi.h and must be kept in sync.
//
   /* Adapter Level Events */
#define HBA_EVENT_ADAPTER_UNKNOWN       0x100
#define HBA_EVENT_ADAPTER_ADD           0x101
#define HBA_EVENT_ADAPTER_REMOVE        0x102
#define HBA_EVENT_ADAPTER_CHANGE        0x103

   /* Port Level Events */
#define HBA_EVENT_PORT_UNKNOWN          0x200
#define HBA_EVENT_PORT_OFFLINE          0x201
#define HBA_EVENT_PORT_ONLINE           0x202
#define HBA_EVENT_PORT_NEW_TARGETS      0x203
#define HBA_EVENT_PORT_FABRIC           0x204
#define HBA_EVENT_PORT_BROADCAST_CHANGE 0x205
#define HBA_EVENT_PORT_BROADCAST_D24_0  0x206
#define HBA_EVENT_PORT_BROADCAST_D27_4  0x207
#define HBA_EVENT_PORT_BROADCAST_SES    0x208
#define HBA_EVENT_PORT_BROADCAST_D01_4  0x209
#define HBA_EVENT_PORT_BROADCAST_D04_7  0x20a
#define HBA_EVENT_PORT_BROADCAST_D16_7  0x20b
#define HBA_EVENT_PORT_BROADCAST_D29_7  0x20c
#define HBA_EVENT_PORT_ALL              0x2ff
   
   /* Port Statistics Events */
#define HBA_EVENT_PORT_STAT_THRESHOLD   0x301
#define HBA_EVENT_PORT_STAT_GROWTH      0x302

/* Phy Statistics Events */
#define HBA_EVENT_PHY_STAT_THRESHOLD    0x351
#define HBA_EVENT_PHY_STAT_GROWTH       0x352

   /* Target Level Events */
#define HBA_EVENT_TARGET_UNKNOWN        0x400
#define HBA_EVENT_TARGET_OFFLINE        0x401
#define HBA_EVENT_TARGET_ONLINE         0x402
#define HBA_EVENT_TARGET_REMOVED        0x403

   /* Fabric Link  Events */
#define HBA_EVENT_LINK_UNKNOWN          0x500
#define HBA_EVENT_LINK_INCIDENT         0x501

#define MSFC_LinkEventGuid \
    { 0xc66015ee,0x014b,0x498a, { 0x94,0x51,0x99,0xfe,0xad,0x0a,0xb4,0x51 } }

#if ! (defined(MIDL_PASS))
DEFINE_GUID(MSFC_LinkEvent_GUID, \
            0xc66015ee,0x014b,0x498a,0x94,0x51,0x99,0xfe,0xad,0x0a,0xb4,0x51);
#endif


typedef struct _MSFC_LinkEvent
{
    // 
    ULONG EventType;
    #define MSFC_LinkEvent_EventType_SIZE sizeof(ULONG)
    #define MSFC_LinkEvent_EventType_ID 1

    // 
    UCHAR AdapterWWN[8];
    #define MSFC_LinkEvent_AdapterWWN_SIZE sizeof(UCHAR[8])
    #define MSFC_LinkEvent_AdapterWWN_ID 2

    // 
    ULONG RLIRBufferSize;
    #define MSFC_LinkEvent_RLIRBufferSize_SIZE sizeof(ULONG)
    #define MSFC_LinkEvent_RLIRBufferSize_ID 3

    // 
    UCHAR RLIRBuffer[1];
    #define MSFC_LinkEvent_RLIRBuffer_ID 4

} MSFC_LinkEvent, *PMSFC_LinkEvent;

// MSFC_FCAdapterHBAAttributes - MSFC_FCAdapterHBAAttributes


#ifndef MS_SM_HBA_API
#ifndef MSFC_HBA_API
//
// if neither defined then default to MSFC
//
#define MSFC_HBA_API
#endif
#endif


#ifdef MSFC_HBA_API

#define MSFC_FCAdapterHBAAttributesGuid \
    { 0xf8f3ea26,0xab2c,0x4593, { 0x8b,0x84,0xc5,0x64,0x28,0xe6,0xbe,0xdb } }

#if ! (defined(MIDL_PASS))
DEFINE_GUID(MSFC_FCAdapterHBAAttributes_GUID, \
            0xf8f3ea26,0xab2c,0x4593,0x8b,0x84,0xc5,0x64,0x28,0xe6,0xbe,0xdb);
#endif


typedef struct _MSFC_FCAdapterHBAAttributes
{
    // 
    ULONGLONG UniqueAdapterId;
    #define MSFC_FCAdapterHBAAttributes_UniqueAdapterId_SIZE sizeof(ULONGLONG)
    #define MSFC_FCAdapterHBAAttributes_UniqueAdapterId_ID 1

    // 
    ULONG HBAStatus;
    #define MSFC_FCAdapterHBAAttributes_HBAStatus_SIZE sizeof(ULONG)
    #define MSFC_FCAdapterHBAAttributes_HBAStatus_ID 2

    // 
    UCHAR NodeWWN[8];
    #define MSFC_FCAdapterHBAAttributes_NodeWWN_SIZE sizeof(UCHAR[8])
    #define MSFC_FCAdapterHBAAttributes_NodeWWN_ID 3

    // 
    ULONG VendorSpecificID;
    #define MSFC_FCAdapterHBAAttributes_VendorSpecificID_SIZE sizeof(ULONG)
    #define MSFC_FCAdapterHBAAttributes_VendorSpecificID_ID 4

    // 
    ULONG NumberOfPorts;
    #define MSFC_FCAdapterHBAAttributes_NumberOfPorts_SIZE sizeof(ULONG)
    #define MSFC_FCAdapterHBAAttributes_NumberOfPorts_ID 5



   //******************************************************************
   //
   //  The string type is variable length (up to MaxLen).              
   //  Each string starts with a ushort that holds the strings length  
   //  (in bytes) followed by the WCHARs that make up the string.      
   //
   //******************************************************************


    // 
    WCHAR Manufacturer[64 + 1];
    #define MSFC_FCAdapterHBAAttributes_Manufacturer_ID 6

    // 
    WCHAR SerialNumber[64 + 1];
    #define MSFC_FCAdapterHBAAttributes_SerialNumber_ID 7

    // 
    WCHAR Model[256 + 1];
    #define MSFC_FCAdapterHBAAttributes_Model_ID 8

    // 
    WCHAR ModelDescription[256 + 1];
    #define MSFC_FCAdapterHBAAttributes_ModelDescription_ID 9

    // 
    WCHAR NodeSymbolicName[256 + 1];
    #define MSFC_FCAdapterHBAAttributes_NodeSymbolicName_ID 10

    // 
    WCHAR HardwareVersion[256 + 1];
    #define MSFC_FCAdapterHBAAttributes_HardwareVersion_ID 11

    // 
    WCHAR DriverVersion[256 + 1];
    #define MSFC_FCAdapterHBAAttributes_DriverVersion_ID 12

    // 
    WCHAR OptionROMVersion[256 + 1];
    #define MSFC_FCAdapterHBAAttributes_OptionROMVersion_ID 13

    // 
    WCHAR FirmwareVersion[256 + 1];
    #define MSFC_FCAdapterHBAAttributes_FirmwareVersion_ID 14

    // 
    WCHAR DriverName[256 + 1];
    #define MSFC_FCAdapterHBAAttributes_DriverName_ID 15

    // 
    WCHAR MfgDomain[256 + 1];
    #define MSFC_FCAdapterHBAAttributes_MfgDomain_ID 16

} MSFC_FCAdapterHBAAttributes, *PMSFC_FCAdapterHBAAttributes;

// MSFC_HBAPortAttributesResults - MSFC_HBAPortAttributesResults
#define MSFC_HBAPortAttributesResultsGuid \
    { 0xa76bd4e3,0x9961,0x4d9b, { 0xb6,0xbe,0x86,0xe6,0x98,0x26,0x0f,0x68 } }

#if ! (defined(MIDL_PASS))
DEFINE_GUID(MSFC_HBAPortAttributesResults_GUID, \
            0xa76bd4e3,0x9961,0x4d9b,0xb6,0xbe,0x86,0xe6,0x98,0x26,0x0f,0x68);
#endif


typedef struct _MSFC_HBAPortAttributesResults
{
    // 
    UCHAR NodeWWN[8];
    #define MSFC_HBAPortAttributesResults_NodeWWN_SIZE sizeof(UCHAR[8])
    #define MSFC_HBAPortAttributesResults_NodeWWN_ID 1

    // 
    UCHAR PortWWN[8];
    #define MSFC_HBAPortAttributesResults_PortWWN_SIZE sizeof(UCHAR[8])
    #define MSFC_HBAPortAttributesResults_PortWWN_ID 2

    // 
    ULONG PortFcId;
    #define MSFC_HBAPortAttributesResults_PortFcId_SIZE sizeof(ULONG)
    #define MSFC_HBAPortAttributesResults_PortFcId_ID 3

    // 
    ULONG PortType;
    #define MSFC_HBAPortAttributesResults_PortType_SIZE sizeof(ULONG)
    #define MSFC_HBAPortAttributesResults_PortType_ID 4

    // 
    ULONG PortState;
    #define MSFC_HBAPortAttributesResults_PortState_SIZE sizeof(ULONG)
    #define MSFC_HBAPortAttributesResults_PortState_ID 5

    // 
    ULONG PortSupportedClassofService;
    #define MSFC_HBAPortAttributesResults_PortSupportedClassofService_SIZE sizeof(ULONG)
    #define MSFC_HBAPortAttributesResults_PortSupportedClassofService_ID 6

    // 
    UCHAR PortSupportedFc4Types[32];
    #define MSFC_HBAPortAttributesResults_PortSupportedFc4Types_SIZE sizeof(UCHAR[32])
    #define MSFC_HBAPortAttributesResults_PortSupportedFc4Types_ID 7

    // 
    UCHAR PortActiveFc4Types[32];
    #define MSFC_HBAPortAttributesResults_PortActiveFc4Types_SIZE sizeof(UCHAR[32])
    #define MSFC_HBAPortAttributesResults_PortActiveFc4Types_ID 8

    // 
    ULONG PortSupportedSpeed;
    #define MSFC_HBAPortAttributesResults_PortSupportedSpeed_SIZE sizeof(ULONG)
    #define MSFC_HBAPortAttributesResults_PortSupportedSpeed_ID 9

    // 
    ULONG PortSpeed;
    #define MSFC_HBAPortAttributesResults_PortSpeed_SIZE sizeof(ULONG)
    #define MSFC_HBAPortAttributesResults_PortSpeed_ID 10

    // 
    ULONG PortMaxFrameSize;
    #define MSFC_HBAPortAttributesResults_PortMaxFrameSize_SIZE sizeof(ULONG)
    #define MSFC_HBAPortAttributesResults_PortMaxFrameSize_ID 11

    // 
    UCHAR FabricName[8];
    #define MSFC_HBAPortAttributesResults_FabricName_SIZE sizeof(UCHAR[8])
    #define MSFC_HBAPortAttributesResults_FabricName_ID 12

    // 
    ULONG NumberofDiscoveredPorts;
    #define MSFC_HBAPortAttributesResults_NumberofDiscoveredPorts_SIZE sizeof(ULONG)
    #define MSFC_HBAPortAttributesResults_NumberofDiscoveredPorts_ID 13

} MSFC_HBAPortAttributesResults, *PMSFC_HBAPortAttributesResults;

#define MSFC_HBAPortAttributesResults_SIZE (FIELD_OFFSET(MSFC_HBAPortAttributesResults, NumberofDiscoveredPorts) + MSFC_HBAPortAttributesResults_NumberofDiscoveredPorts_SIZE)

// MSFC_FibrePortHBAAttributes - MSFC_FibrePortHBAAttributes
#define MSFC_FibrePortHBAAttributesGuid \
    { 0x61b397fd,0xf5ae,0x4950, { 0x97,0x58,0x0e,0xe5,0x98,0xe3,0xc6,0xe6 } }

#if ! (defined(MIDL_PASS))
DEFINE_GUID(MSFC_FibrePortHBAAttributes_GUID, \
            0x61b397fd,0xf5ae,0x4950,0x97,0x58,0x0e,0xe5,0x98,0xe3,0xc6,0xe6);
#endif


typedef struct _MSFC_FibrePortHBAAttributes
{
    // 
    ULONGLONG UniquePortId;
    #define MSFC_FibrePortHBAAttributes_UniquePortId_SIZE sizeof(ULONGLONG)
    #define MSFC_FibrePortHBAAttributes_UniquePortId_ID 1

    // 
    ULONG HBAStatus;
    #define MSFC_FibrePortHBAAttributes_HBAStatus_SIZE sizeof(ULONG)
    #define MSFC_FibrePortHBAAttributes_HBAStatus_ID 2

    // 
    MSFC_HBAPortAttributesResults Attributes;
    #define MSFC_FibrePortHBAAttributes_Attributes_SIZE sizeof(MSFC_HBAPortAttributesResults)
    #define MSFC_FibrePortHBAAttributes_Attributes_ID 3

} MSFC_FibrePortHBAAttributes, *PMSFC_FibrePortHBAAttributes;

#define MSFC_FibrePortHBAAttributes_SIZE (FIELD_OFFSET(MSFC_FibrePortHBAAttributes, Attributes) + MSFC_FibrePortHBAAttributes_Attributes_SIZE)

// MSFC_FibrePortHBAStatistics - MSFC_FibrePortHBAStatistics
#define MSFC_FibrePortHBAStatisticsGuid \
    { 0x27efaba4,0x362a,0x4f20, { 0x92,0x0b,0xed,0x66,0xe2,0x80,0xfc,0xf5 } }

#if ! (defined(MIDL_PASS))
DEFINE_GUID(MSFC_FibrePortHBAStatistics_GUID, \
            0x27efaba4,0x362a,0x4f20,0x92,0x0b,0xed,0x66,0xe2,0x80,0xfc,0xf5);
#endif


typedef struct _MSFC_FibrePortHBAStatistics
{
    // 
    ULONGLONG UniquePortId;
    #define MSFC_FibrePortHBAStatistics_UniquePortId_SIZE sizeof(ULONGLONG)
    #define MSFC_FibrePortHBAStatistics_UniquePortId_ID 1

    // 
    ULONG HBAStatus;
    #define MSFC_FibrePortHBAStatistics_HBAStatus_SIZE sizeof(ULONG)
    #define MSFC_FibrePortHBAStatistics_HBAStatus_ID 2

    // 
    MSFC_HBAPortStatistics Statistics;
    #define MSFC_FibrePortHBAStatistics_Statistics_SIZE sizeof(MSFC_HBAPortStatistics)
    #define MSFC_FibrePortHBAStatistics_Statistics_ID 3

} MSFC_FibrePortHBAStatistics, *PMSFC_FibrePortHBAStatistics;

#define MSFC_FibrePortHBAStatistics_SIZE (FIELD_OFFSET(MSFC_FibrePortHBAStatistics, Statistics) + MSFC_FibrePortHBAStatistics_Statistics_SIZE)

// MSFC_FibrePortHBAMethods - MSFC_FibrePortHBAMethods
#define MSFC_FibrePortHBAMethodsGuid \
    { 0xe693553e,0xedf6,0x4d57, { 0xbf,0x08,0xef,0xca,0xae,0x1a,0x2e,0x1c } }

#if ! (defined(MIDL_PASS))
DEFINE_GUID(MSFC_FibrePortHBAMethods_GUID, \
            0xe693553e,0xedf6,0x4d57,0xbf,0x08,0xef,0xca,0xae,0x1a,0x2e,0x1c);
#endif

//
// Method id definitions for MSFC_FibrePortHBAMethods
#define ResetStatistics     1

// MSFC_FC4STATISTICS - MSFC_FC4STATISTICS
#define MSFC_FC4STATISTICSGuid \
    { 0xca8e7fe6,0xb85e,0x497f, { 0x88,0x58,0x9b,0x5d,0x93,0xa6,0x6f,0xe1 } }

#if ! (defined(MIDL_PASS))
DEFINE_GUID(MSFC_FC4STATISTICS_GUID, \
            0xca8e7fe6,0xb85e,0x497f,0x88,0x58,0x9b,0x5d,0x93,0xa6,0x6f,0xe1);
#endif


typedef struct _MSFC_FC4STATISTICS
{
    // 
    ULONGLONG InputRequests;
    #define MSFC_FC4STATISTICS_InputRequests_SIZE sizeof(ULONGLONG)
    #define MSFC_FC4STATISTICS_InputRequests_ID 1

    // 
    ULONGLONG OutputRequests;
    #define MSFC_FC4STATISTICS_OutputRequests_SIZE sizeof(ULONGLONG)
    #define MSFC_FC4STATISTICS_OutputRequests_ID 2

    // 
    ULONGLONG ControlRequests;
    #define MSFC_FC4STATISTICS_ControlRequests_SIZE sizeof(ULONGLONG)
    #define MSFC_FC4STATISTICS_ControlRequests_ID 3

    // 
    ULONGLONG InputMegabytes;
    #define MSFC_FC4STATISTICS_InputMegabytes_SIZE sizeof(ULONGLONG)
    #define MSFC_FC4STATISTICS_InputMegabytes_ID 4

    // 
    ULONGLONG OutputMegabytes;
    #define MSFC_FC4STATISTICS_OutputMegabytes_SIZE sizeof(ULONGLONG)
    #define MSFC_FC4STATISTICS_OutputMegabytes_ID 5

} MSFC_FC4STATISTICS, *PMSFC_FC4STATISTICS;

#define MSFC_FC4STATISTICS_SIZE (FIELD_OFFSET(MSFC_FC4STATISTICS, OutputMegabytes) + MSFC_FC4STATISTICS_OutputMegabytes_SIZE)

// MSFC_EventBuffer - MSFC_EventBuffer
#define MSFC_EventBufferGuid \
    { 0x623f4588,0xcf01,0x4f0e, { 0xb1,0x97,0xab,0xbe,0xe5,0xe0,0xcf,0xf3 } }

#if ! (defined(MIDL_PASS))
DEFINE_GUID(MSFC_EventBuffer_GUID, \
            0x623f4588,0xcf01,0x4f0e,0xb1,0x97,0xab,0xbe,0xe5,0xe0,0xcf,0xf3);
#endif


typedef struct _MSFC_EventBuffer
{
    // 
    ULONG EventType;
    #define MSFC_EventBuffer_EventType_SIZE sizeof(ULONG)
    #define MSFC_EventBuffer_EventType_ID 1

    // 
    ULONG EventInfo[4];
    #define MSFC_EventBuffer_EventInfo_SIZE sizeof(ULONG[4])
    #define MSFC_EventBuffer_EventInfo_ID 2

} MSFC_EventBuffer, *PMSFC_EventBuffer;

#define MSFC_EventBuffer_SIZE (FIELD_OFFSET(MSFC_EventBuffer, EventInfo) + MSFC_EventBuffer_EventInfo_SIZE)

// MSFC_HBAAdapterMethods - MSFC_HBAAdapterMethods
#define MSFC_HBAAdapterMethodsGuid \
    { 0xdf87d4ed,0x4612,0x4d12, { 0x85,0xfb,0x83,0x57,0x4e,0xc3,0x4b,0x7c } }

#if ! (defined(MIDL_PASS))
DEFINE_GUID(MSFC_HBAAdapterMethods_GUID, \
            0xdf87d4ed,0x4612,0x4d12,0x85,0xfb,0x83,0x57,0x4e,0xc3,0x4b,0x7c);
#endif

//
// Method id definitions for MSFC_HBAAdapterMethods
#define GetDiscoveredPortAttributes     1
typedef struct _GetDiscoveredPortAttributes_IN
{
    // 
    ULONG PortIndex;
    #define GetDiscoveredPortAttributes_IN_PortIndex_SIZE sizeof(ULONG)
    #define GetDiscoveredPortAttributes_IN_PortIndex_ID 1

    // 
    ULONG DiscoveredPortIndex;
    #define GetDiscoveredPortAttributes_IN_DiscoveredPortIndex_SIZE sizeof(ULONG)
    #define GetDiscoveredPortAttributes_IN_DiscoveredPortIndex_ID 2

} GetDiscoveredPortAttributes_IN, *PGetDiscoveredPortAttributes_IN;

#define GetDiscoveredPortAttributes_IN_SIZE (FIELD_OFFSET(GetDiscoveredPortAttributes_IN, DiscoveredPortIndex) + GetDiscoveredPortAttributes_IN_DiscoveredPortIndex_SIZE)

typedef struct _GetDiscoveredPortAttributes_OUT
{
    // 
    ULONG HBAStatus;
    #define GetDiscoveredPortAttributes_OUT_HBAStatus_SIZE sizeof(ULONG)
    #define GetDiscoveredPortAttributes_OUT_HBAStatus_ID 3

    // 
    MSFC_HBAPortAttributesResults PortAttributes;
    #define GetDiscoveredPortAttributes_OUT_PortAttributes_SIZE sizeof(MSFC_HBAPortAttributesResults)
    #define GetDiscoveredPortAttributes_OUT_PortAttributes_ID 4

} GetDiscoveredPortAttributes_OUT, *PGetDiscoveredPortAttributes_OUT;

#define GetDiscoveredPortAttributes_OUT_SIZE (FIELD_OFFSET(GetDiscoveredPortAttributes_OUT, PortAttributes) + GetDiscoveredPortAttributes_OUT_PortAttributes_SIZE)

#define GetPortAttributesByWWN     2
typedef struct _GetPortAttributesByWWN_IN
{
    // 
    UCHAR wwn[8];
    #define GetPortAttributesByWWN_IN_wwn_SIZE sizeof(UCHAR[8])
    #define GetPortAttributesByWWN_IN_wwn_ID 1

} GetPortAttributesByWWN_IN, *PGetPortAttributesByWWN_IN;

#define GetPortAttributesByWWN_IN_SIZE (FIELD_OFFSET(GetPortAttributesByWWN_IN, wwn) + GetPortAttributesByWWN_IN_wwn_SIZE)

typedef struct _GetPortAttributesByWWN_OUT
{
    // 
    ULONG HBAStatus;
    #define GetPortAttributesByWWN_OUT_HBAStatus_SIZE sizeof(ULONG)
    #define GetPortAttributesByWWN_OUT_HBAStatus_ID 2

    // 
    MSFC_HBAPortAttributesResults PortAttributes;
    #define GetPortAttributesByWWN_OUT_PortAttributes_SIZE sizeof(MSFC_HBAPortAttributesResults)
    #define GetPortAttributesByWWN_OUT_PortAttributes_ID 3

} GetPortAttributesByWWN_OUT, *PGetPortAttributesByWWN_OUT;

#define GetPortAttributesByWWN_OUT_SIZE (FIELD_OFFSET(GetPortAttributesByWWN_OUT, PortAttributes) + GetPortAttributesByWWN_OUT_PortAttributes_SIZE)

#define RefreshInformation     3
#define SendCTPassThru     4
typedef struct _SendCTPassThru_IN
{
    // 
    UCHAR PortWWN[8];
    #define SendCTPassThru_IN_PortWWN_SIZE sizeof(UCHAR[8])
    #define SendCTPassThru_IN_PortWWN_ID 1

    // 
    ULONG RequestBufferCount;
    #define SendCTPassThru_IN_RequestBufferCount_SIZE sizeof(ULONG)
    #define SendCTPassThru_IN_RequestBufferCount_ID 2

    // 
    UCHAR RequestBuffer[1];
    #define SendCTPassThru_IN_RequestBuffer_ID 3

} SendCTPassThru_IN, *PSendCTPassThru_IN;

typedef struct _SendCTPassThru_OUT
{
    // 
    ULONG HBAStatus;
    #define SendCTPassThru_OUT_HBAStatus_SIZE sizeof(ULONG)
    #define SendCTPassThru_OUT_HBAStatus_ID 4

    // 
    ULONG TotalResponseBufferCount;
    #define SendCTPassThru_OUT_TotalResponseBufferCount_SIZE sizeof(ULONG)
    #define SendCTPassThru_OUT_TotalResponseBufferCount_ID 5

    // 
    ULONG ActualResponseBufferCount;
    #define SendCTPassThru_OUT_ActualResponseBufferCount_SIZE sizeof(ULONG)
    #define SendCTPassThru_OUT_ActualResponseBufferCount_ID 6


#define SendCTPassThru_OUT_ResponseBuffer_SIZE_HINT 768

    // 
    UCHAR ResponseBuffer[1];
    #define SendCTPassThru_OUT_ResponseBuffer_ID 7

} SendCTPassThru_OUT, *PSendCTPassThru_OUT;

#define SendRNID     5
typedef struct _SendRNID_IN
{
    // 
    UCHAR wwn[8];
    #define SendRNID_IN_wwn_SIZE sizeof(UCHAR[8])
    #define SendRNID_IN_wwn_ID 1

    // 
    ULONG wwntype;
    #define SendRNID_IN_wwntype_SIZE sizeof(ULONG)
    #define SendRNID_IN_wwntype_ID 2

} SendRNID_IN, *PSendRNID_IN;

#define SendRNID_IN_SIZE (FIELD_OFFSET(SendRNID_IN, wwntype) + SendRNID_IN_wwntype_SIZE)

typedef struct _SendRNID_OUT
{
    // 
    ULONG HBAStatus;
    #define SendRNID_OUT_HBAStatus_SIZE sizeof(ULONG)
    #define SendRNID_OUT_HBAStatus_ID 3

    // 
    ULONG ResponseBufferCount;
    #define SendRNID_OUT_ResponseBufferCount_SIZE sizeof(ULONG)
    #define SendRNID_OUT_ResponseBufferCount_ID 4


#define SendRNID_OUT_ResponseBuffer_SIZE_HINT 76

    // 
    UCHAR ResponseBuffer[1];
    #define SendRNID_OUT_ResponseBuffer_ID 5

} SendRNID_OUT, *PSendRNID_OUT;

#define SendRNIDV2     6
typedef struct _SendRNIDV2_IN
{
    // 
    UCHAR PortWWN[8];
    #define SendRNIDV2_IN_PortWWN_SIZE sizeof(UCHAR[8])
    #define SendRNIDV2_IN_PortWWN_ID 1

    // 
    UCHAR DestWWN[8];
    #define SendRNIDV2_IN_DestWWN_SIZE sizeof(UCHAR[8])
    #define SendRNIDV2_IN_DestWWN_ID 2

    // 
    ULONG DestFCID;
    #define SendRNIDV2_IN_DestFCID_SIZE sizeof(ULONG)
    #define SendRNIDV2_IN_DestFCID_ID 3

    // 
    ULONG NodeIdDataFormat;
    #define SendRNIDV2_IN_NodeIdDataFormat_SIZE sizeof(ULONG)
    #define SendRNIDV2_IN_NodeIdDataFormat_ID 4

} SendRNIDV2_IN, *PSendRNIDV2_IN;

#define SendRNIDV2_IN_SIZE (FIELD_OFFSET(SendRNIDV2_IN, NodeIdDataFormat) + SendRNIDV2_IN_NodeIdDataFormat_SIZE)

typedef struct _SendRNIDV2_OUT
{
    // 
    ULONG HBAStatus;
    #define SendRNIDV2_OUT_HBAStatus_SIZE sizeof(ULONG)
    #define SendRNIDV2_OUT_HBAStatus_ID 5

    // 
    ULONG TotalRspBufferSize;
    #define SendRNIDV2_OUT_TotalRspBufferSize_SIZE sizeof(ULONG)
    #define SendRNIDV2_OUT_TotalRspBufferSize_ID 6

    // 
    ULONG ActualRspBufferSize;
    #define SendRNIDV2_OUT_ActualRspBufferSize_SIZE sizeof(ULONG)
    #define SendRNIDV2_OUT_ActualRspBufferSize_ID 7


#define SendRNIDV2_OUT_RspBuffer_SIZE_HINT 76

    // 
    UCHAR RspBuffer[1];
    #define SendRNIDV2_OUT_RspBuffer_ID 8

} SendRNIDV2_OUT, *PSendRNIDV2_OUT;

#define GetFC3MgmtInfo     7
typedef struct _GetFC3MgmtInfo_OUT
{
    // 
    ULONG HBAStatus;
    #define GetFC3MgmtInfo_OUT_HBAStatus_SIZE sizeof(ULONG)
    #define GetFC3MgmtInfo_OUT_HBAStatus_ID 1

    // 
    HBAFC3MgmtInfo MgmtInfo;
    #define GetFC3MgmtInfo_OUT_MgmtInfo_SIZE sizeof(HBAFC3MgmtInfo)
    #define GetFC3MgmtInfo_OUT_MgmtInfo_ID 2

} GetFC3MgmtInfo_OUT, *PGetFC3MgmtInfo_OUT;

#define GetFC3MgmtInfo_OUT_SIZE (FIELD_OFFSET(GetFC3MgmtInfo_OUT, MgmtInfo) + GetFC3MgmtInfo_OUT_MgmtInfo_SIZE)

#define SetFC3MgmtInfo     8
typedef struct _SetFC3MgmtInfo_IN
{
    // 
    HBAFC3MgmtInfo MgmtInfo;
    #define SetFC3MgmtInfo_IN_MgmtInfo_SIZE sizeof(HBAFC3MgmtInfo)
    #define SetFC3MgmtInfo_IN_MgmtInfo_ID 1

} SetFC3MgmtInfo_IN, *PSetFC3MgmtInfo_IN;

#define SetFC3MgmtInfo_IN_SIZE (FIELD_OFFSET(SetFC3MgmtInfo_IN, MgmtInfo) + SetFC3MgmtInfo_IN_MgmtInfo_SIZE)

typedef struct _SetFC3MgmtInfo_OUT
{
    // 
    ULONG HBAStatus;
    #define SetFC3MgmtInfo_OUT_HBAStatus_SIZE sizeof(ULONG)
    #define SetFC3MgmtInfo_OUT_HBAStatus_ID 2

} SetFC3MgmtInfo_OUT, *PSetFC3MgmtInfo_OUT;

#define SetFC3MgmtInfo_OUT_SIZE (FIELD_OFFSET(SetFC3MgmtInfo_OUT, HBAStatus) + SetFC3MgmtInfo_OUT_HBAStatus_SIZE)

#define SendRPL     9
typedef struct _SendRPL_IN
{
    // 
    UCHAR PortWWN[8];
    #define SendRPL_IN_PortWWN_SIZE sizeof(UCHAR[8])
    #define SendRPL_IN_PortWWN_ID 1

    // 
    UCHAR AgentWWN[8];
    #define SendRPL_IN_AgentWWN_SIZE sizeof(UCHAR[8])
    #define SendRPL_IN_AgentWWN_ID 2

    // 
    ULONG agent_domain;
    #define SendRPL_IN_agent_domain_SIZE sizeof(ULONG)
    #define SendRPL_IN_agent_domain_ID 3

    // 
    ULONG portIndex;
    #define SendRPL_IN_portIndex_SIZE sizeof(ULONG)
    #define SendRPL_IN_portIndex_ID 4

} SendRPL_IN, *PSendRPL_IN;

#define SendRPL_IN_SIZE (FIELD_OFFSET(SendRPL_IN, portIndex) + SendRPL_IN_portIndex_SIZE)

typedef struct _SendRPL_OUT
{
    // 
    ULONG HBAStatus;
    #define SendRPL_OUT_HBAStatus_SIZE sizeof(ULONG)
    #define SendRPL_OUT_HBAStatus_ID 5

    // 
    ULONG TotalRspBufferSize;
    #define SendRPL_OUT_TotalRspBufferSize_SIZE sizeof(ULONG)
    #define SendRPL_OUT_TotalRspBufferSize_ID 6

    // 
    ULONG ActualRspBufferSize;
    #define SendRPL_OUT_ActualRspBufferSize_SIZE sizeof(ULONG)
    #define SendRPL_OUT_ActualRspBufferSize_ID 7


#define SendRPL_OUT_RspBuffer_SIZE_HINT 28 // 12+16*n

    // 
    UCHAR RspBuffer[1];
    #define SendRPL_OUT_RspBuffer_ID 8

} SendRPL_OUT, *PSendRPL_OUT;

#define SendRPS     10
typedef struct _SendRPS_IN
{
    // 
    UCHAR PortWWN[8];
    #define SendRPS_IN_PortWWN_SIZE sizeof(UCHAR[8])
    #define SendRPS_IN_PortWWN_ID 1

    // 
    UCHAR AgentWWN[8];
    #define SendRPS_IN_AgentWWN_SIZE sizeof(UCHAR[8])
    #define SendRPS_IN_AgentWWN_ID 2

    // 
    UCHAR ObjectWWN[8];
    #define SendRPS_IN_ObjectWWN_SIZE sizeof(UCHAR[8])
    #define SendRPS_IN_ObjectWWN_ID 3

    // 
    ULONG AgentDomain;
    #define SendRPS_IN_AgentDomain_SIZE sizeof(ULONG)
    #define SendRPS_IN_AgentDomain_ID 4

    // 
    ULONG ObjectPortNumber;
    #define SendRPS_IN_ObjectPortNumber_SIZE sizeof(ULONG)
    #define SendRPS_IN_ObjectPortNumber_ID 5

} SendRPS_IN, *PSendRPS_IN;

#define SendRPS_IN_SIZE (FIELD_OFFSET(SendRPS_IN, ObjectPortNumber) + SendRPS_IN_ObjectPortNumber_SIZE)

typedef struct _SendRPS_OUT
{
    // 
    ULONG HBAStatus;
    #define SendRPS_OUT_HBAStatus_SIZE sizeof(ULONG)
    #define SendRPS_OUT_HBAStatus_ID 6

    // 
    ULONG TotalRspBufferSize;
    #define SendRPS_OUT_TotalRspBufferSize_SIZE sizeof(ULONG)
    #define SendRPS_OUT_TotalRspBufferSize_ID 7

    // 
    ULONG ActualRspBufferSize;
    #define SendRPS_OUT_ActualRspBufferSize_SIZE sizeof(ULONG)
    #define SendRPS_OUT_ActualRspBufferSize_ID 8


#define SendRPS_OUT_RspBuffer_SIZE_HINT 64

    // 
    UCHAR RspBuffer[1];
    #define SendRPS_OUT_RspBuffer_ID 9

} SendRPS_OUT, *PSendRPS_OUT;

#define SendSRL     11
typedef struct _SendSRL_IN
{
    // 
    UCHAR PortWWN[8];
    #define SendSRL_IN_PortWWN_SIZE sizeof(UCHAR[8])
    #define SendSRL_IN_PortWWN_ID 1

    // 
    UCHAR WWN[8];
    #define SendSRL_IN_WWN_SIZE sizeof(UCHAR[8])
    #define SendSRL_IN_WWN_ID 2

    // 
    ULONG Domain;
    #define SendSRL_IN_Domain_SIZE sizeof(ULONG)
    #define SendSRL_IN_Domain_ID 3

} SendSRL_IN, *PSendSRL_IN;

#define SendSRL_IN_SIZE (FIELD_OFFSET(SendSRL_IN, Domain) + SendSRL_IN_Domain_SIZE)

typedef struct _SendSRL_OUT
{
    // 
    ULONG HBAStatus;
    #define SendSRL_OUT_HBAStatus_SIZE sizeof(ULONG)
    #define SendSRL_OUT_HBAStatus_ID 4

    // 
    ULONG TotalRspBufferSize;
    #define SendSRL_OUT_TotalRspBufferSize_SIZE sizeof(ULONG)
    #define SendSRL_OUT_TotalRspBufferSize_ID 5

    // 
    ULONG ActualRspBufferSize;
    #define SendSRL_OUT_ActualRspBufferSize_SIZE sizeof(ULONG)
    #define SendSRL_OUT_ActualRspBufferSize_ID 6


#define SendSRL_OUT_RspBuffer_SIZE_HINT 8

    // 
    UCHAR RspBuffer[1];
    #define SendSRL_OUT_RspBuffer_ID 7

} SendSRL_OUT, *PSendSRL_OUT;

#define SendLIRR     12
typedef struct _SendLIRR_IN
{
    // 
    UCHAR SourceWWN[8];
    #define SendLIRR_IN_SourceWWN_SIZE sizeof(UCHAR[8])
    #define SendLIRR_IN_SourceWWN_ID 1

    // 
    UCHAR DestWWN[8];
    #define SendLIRR_IN_DestWWN_SIZE sizeof(UCHAR[8])
    #define SendLIRR_IN_DestWWN_ID 2

    // 
    UCHAR Function;
    #define SendLIRR_IN_Function_SIZE sizeof(UCHAR)
    #define SendLIRR_IN_Function_ID 3

    // 
    UCHAR Type;
    #define SendLIRR_IN_Type_SIZE sizeof(UCHAR)
    #define SendLIRR_IN_Type_ID 4

} SendLIRR_IN, *PSendLIRR_IN;

#define SendLIRR_IN_SIZE (FIELD_OFFSET(SendLIRR_IN, Type) + SendLIRR_IN_Type_SIZE)

typedef struct _SendLIRR_OUT
{
    // 
    ULONG HBAStatus;
    #define SendLIRR_OUT_HBAStatus_SIZE sizeof(ULONG)
    #define SendLIRR_OUT_HBAStatus_ID 5

    // 
    ULONG TotalRspBufferSize;
    #define SendLIRR_OUT_TotalRspBufferSize_SIZE sizeof(ULONG)
    #define SendLIRR_OUT_TotalRspBufferSize_ID 6

    // 
    ULONG ActualRspBufferSize;
    #define SendLIRR_OUT_ActualRspBufferSize_SIZE sizeof(ULONG)
    #define SendLIRR_OUT_ActualRspBufferSize_ID 7


#define SendLIRR_OUT_RspBuffer_SIZE_HINT 8

    // 
    UCHAR RspBuffer[1];
    #define SendLIRR_OUT_RspBuffer_ID 8

} SendLIRR_OUT, *PSendLIRR_OUT;

#define GetFC4Statistics     13
typedef struct _GetFC4Statistics_IN
{
    // 
    UCHAR PortWWN[8];
    #define GetFC4Statistics_IN_PortWWN_SIZE sizeof(UCHAR[8])
    #define GetFC4Statistics_IN_PortWWN_ID 1

    // 
    UCHAR FC4Type;
    #define GetFC4Statistics_IN_FC4Type_SIZE sizeof(UCHAR)
    #define GetFC4Statistics_IN_FC4Type_ID 2

} GetFC4Statistics_IN, *PGetFC4Statistics_IN;

#define GetFC4Statistics_IN_SIZE (FIELD_OFFSET(GetFC4Statistics_IN, FC4Type) + GetFC4Statistics_IN_FC4Type_SIZE)

typedef struct _GetFC4Statistics_OUT
{
    // 
    ULONG HBAStatus;
    #define GetFC4Statistics_OUT_HBAStatus_SIZE sizeof(ULONG)
    #define GetFC4Statistics_OUT_HBAStatus_ID 3

    // 
    MSFC_FC4STATISTICS FC4Statistics;
    #define GetFC4Statistics_OUT_FC4Statistics_SIZE sizeof(MSFC_FC4STATISTICS)
    #define GetFC4Statistics_OUT_FC4Statistics_ID 4

} GetFC4Statistics_OUT, *PGetFC4Statistics_OUT;

#define GetFC4Statistics_OUT_SIZE (FIELD_OFFSET(GetFC4Statistics_OUT, FC4Statistics) + GetFC4Statistics_OUT_FC4Statistics_SIZE)

#define GetFCPStatistics     14
typedef struct _GetFCPStatistics_IN
{
    // 
    HBAScsiID ScsiId;
    #define GetFCPStatistics_IN_ScsiId_SIZE sizeof(HBAScsiID)
    #define GetFCPStatistics_IN_ScsiId_ID 1

} GetFCPStatistics_IN, *PGetFCPStatistics_IN;

#define GetFCPStatistics_IN_SIZE (FIELD_OFFSET(GetFCPStatistics_IN, ScsiId) + GetFCPStatistics_IN_ScsiId_SIZE)

typedef struct _GetFCPStatistics_OUT
{
    // 
    ULONG HBAStatus;
    #define GetFCPStatistics_OUT_HBAStatus_SIZE sizeof(ULONG)
    #define GetFCPStatistics_OUT_HBAStatus_ID 2

    // 
    MSFC_FC4STATISTICS FC4Statistics;
    #define GetFCPStatistics_OUT_FC4Statistics_SIZE sizeof(MSFC_FC4STATISTICS)
    #define GetFCPStatistics_OUT_FC4Statistics_ID 3

} GetFCPStatistics_OUT, *PGetFCPStatistics_OUT;

#define GetFCPStatistics_OUT_SIZE (FIELD_OFFSET(GetFCPStatistics_OUT, FC4Statistics) + GetFCPStatistics_OUT_FC4Statistics_SIZE)

#define ScsiInquiry     15
typedef struct _ScsiInquiry_IN
{
    // 
    UCHAR Cdb[6];
    #define ScsiInquiry_IN_Cdb_SIZE sizeof(UCHAR[6])
    #define ScsiInquiry_IN_Cdb_ID 1

    // 
    UCHAR HbaPortWWN[8];
    #define ScsiInquiry_IN_HbaPortWWN_SIZE sizeof(UCHAR[8])
    #define ScsiInquiry_IN_HbaPortWWN_ID 2

    // 
    UCHAR DiscoveredPortWWN[8];
    #define ScsiInquiry_IN_DiscoveredPortWWN_SIZE sizeof(UCHAR[8])
    #define ScsiInquiry_IN_DiscoveredPortWWN_ID 3

    // 
    ULONGLONG FcLun;
    #define ScsiInquiry_IN_FcLun_SIZE sizeof(ULONGLONG)
    #define ScsiInquiry_IN_FcLun_ID 4

} ScsiInquiry_IN, *PScsiInquiry_IN;

#define ScsiInquiry_IN_SIZE (FIELD_OFFSET(ScsiInquiry_IN, FcLun) + ScsiInquiry_IN_FcLun_SIZE)

typedef struct _ScsiInquiry_OUT
{
    // 
    ULONG HBAStatus;
    #define ScsiInquiry_OUT_HBAStatus_SIZE sizeof(ULONG)
    #define ScsiInquiry_OUT_HBAStatus_ID 5

    // 
    ULONG ResponseBufferSize;
    #define ScsiInquiry_OUT_ResponseBufferSize_SIZE sizeof(ULONG)
    #define ScsiInquiry_OUT_ResponseBufferSize_ID 6

    // 
    ULONG SenseBufferSize;
    #define ScsiInquiry_OUT_SenseBufferSize_SIZE sizeof(ULONG)
    #define ScsiInquiry_OUT_SenseBufferSize_ID 7

    // 
    UCHAR ScsiStatus;
    #define ScsiInquiry_OUT_ScsiStatus_SIZE sizeof(UCHAR)
    #define ScsiInquiry_OUT_ScsiStatus_ID 8


#define ScsiInquiry_OUT_ResponseBuffer_SIZE_HINT 96

    // 
    UCHAR ResponseBuffer[1];
    #define ScsiInquiry_OUT_ResponseBuffer_ID 9

    // 
//  UCHAR SenseBuffer[1];
    #define ScsiInquiry_OUT_SenseBuffer_ID 10

} ScsiInquiry_OUT, *PScsiInquiry_OUT;

#define ScsiReadCapacity     16
typedef struct _ScsiReadCapacity_IN
{
    // 
    UCHAR Cdb[10];
    #define ScsiReadCapacity_IN_Cdb_SIZE sizeof(UCHAR[10])
    #define ScsiReadCapacity_IN_Cdb_ID 1

    // 
    UCHAR HbaPortWWN[8];
    #define ScsiReadCapacity_IN_HbaPortWWN_SIZE sizeof(UCHAR[8])
    #define ScsiReadCapacity_IN_HbaPortWWN_ID 2

    // 
    UCHAR DiscoveredPortWWN[8];
    #define ScsiReadCapacity_IN_DiscoveredPortWWN_SIZE sizeof(UCHAR[8])
    #define ScsiReadCapacity_IN_DiscoveredPortWWN_ID 3

    // 
    ULONGLONG FcLun;
    #define ScsiReadCapacity_IN_FcLun_SIZE sizeof(ULONGLONG)
    #define ScsiReadCapacity_IN_FcLun_ID 4

} ScsiReadCapacity_IN, *PScsiReadCapacity_IN;

#define ScsiReadCapacity_IN_SIZE (FIELD_OFFSET(ScsiReadCapacity_IN, FcLun) + ScsiReadCapacity_IN_FcLun_SIZE)

typedef struct _ScsiReadCapacity_OUT
{
    // 
    ULONG HBAStatus;
    #define ScsiReadCapacity_OUT_HBAStatus_SIZE sizeof(ULONG)
    #define ScsiReadCapacity_OUT_HBAStatus_ID 5

    // 
    ULONG ResponseBufferSize;
    #define ScsiReadCapacity_OUT_ResponseBufferSize_SIZE sizeof(ULONG)
    #define ScsiReadCapacity_OUT_ResponseBufferSize_ID 6

    // 
    ULONG SenseBufferSize;
    #define ScsiReadCapacity_OUT_SenseBufferSize_SIZE sizeof(ULONG)
    #define ScsiReadCapacity_OUT_SenseBufferSize_ID 7

    // 
    UCHAR ScsiStatus;
    #define ScsiReadCapacity_OUT_ScsiStatus_SIZE sizeof(UCHAR)
    #define ScsiReadCapacity_OUT_ScsiStatus_ID 8


#define ScsiReadCapacity_OUT_ResponseBuffer_SIZE_HINT 16

    // 
    UCHAR ResponseBuffer[1];
    #define ScsiReadCapacity_OUT_ResponseBuffer_ID 9

    // 
//  UCHAR SenseBuffer[1];
    #define ScsiReadCapacity_OUT_SenseBuffer_ID 10

} ScsiReadCapacity_OUT, *PScsiReadCapacity_OUT;

#define ScsiReportLuns     17
typedef struct _ScsiReportLuns_IN
{
    // 
    UCHAR Cdb[12];
    #define ScsiReportLuns_IN_Cdb_SIZE sizeof(UCHAR[12])
    #define ScsiReportLuns_IN_Cdb_ID 1

    // 
    UCHAR HbaPortWWN[8];
    #define ScsiReportLuns_IN_HbaPortWWN_SIZE sizeof(UCHAR[8])
    #define ScsiReportLuns_IN_HbaPortWWN_ID 2

    // 
    UCHAR DiscoveredPortWWN[8];
    #define ScsiReportLuns_IN_DiscoveredPortWWN_SIZE sizeof(UCHAR[8])
    #define ScsiReportLuns_IN_DiscoveredPortWWN_ID 3

} ScsiReportLuns_IN, *PScsiReportLuns_IN;

#define ScsiReportLuns_IN_SIZE (FIELD_OFFSET(ScsiReportLuns_IN, DiscoveredPortWWN) + ScsiReportLuns_IN_DiscoveredPortWWN_SIZE)

typedef struct _ScsiReportLuns_OUT
{
    // 
    ULONG HBAStatus;
    #define ScsiReportLuns_OUT_HBAStatus_SIZE sizeof(ULONG)
    #define ScsiReportLuns_OUT_HBAStatus_ID 4

    // 
    ULONG ResponseBufferSize;
    #define ScsiReportLuns_OUT_ResponseBufferSize_SIZE sizeof(ULONG)
    #define ScsiReportLuns_OUT_ResponseBufferSize_ID 5

    // 
    ULONG SenseBufferSize;
    #define ScsiReportLuns_OUT_SenseBufferSize_SIZE sizeof(ULONG)
    #define ScsiReportLuns_OUT_SenseBufferSize_ID 6

    // 
    UCHAR ScsiStatus;
    #define ScsiReportLuns_OUT_ScsiStatus_SIZE sizeof(UCHAR)
    #define ScsiReportLuns_OUT_ScsiStatus_ID 7


#define ScsiReportLuns_OUT_ResponseBuffer_SIZE_HINT 16 // 8+8*number_of_luns

    // 
    UCHAR ResponseBuffer[1];
    #define ScsiReportLuns_OUT_ResponseBuffer_ID 8

    // 
//  UCHAR SenseBuffer[1];
    #define ScsiReportLuns_OUT_SenseBuffer_ID 9

} ScsiReportLuns_OUT, *PScsiReportLuns_OUT;

#define GetEventBuffer     18
typedef struct _GetEventBuffer_OUT
{
    // 
    ULONG HBAStatus;
    #define GetEventBuffer_OUT_HBAStatus_SIZE sizeof(ULONG)
    #define GetEventBuffer_OUT_HBAStatus_ID 1

    // 
    ULONG EventCount;
    #define GetEventBuffer_OUT_EventCount_SIZE sizeof(ULONG)
    #define GetEventBuffer_OUT_EventCount_ID 2

    // 
    MSFC_EventBuffer Events[1];
    #define GetEventBuffer_OUT_Events_ID 3

} GetEventBuffer_OUT, *PGetEventBuffer_OUT;

#define SendRLS     19
typedef struct _SendRLS_IN
{
    // 
    UCHAR PortWWN[8];
    #define SendRLS_IN_PortWWN_SIZE sizeof(UCHAR[8])
    #define SendRLS_IN_PortWWN_ID 1

    // 
    UCHAR DestWWN[8];
    #define SendRLS_IN_DestWWN_SIZE sizeof(UCHAR[8])
    #define SendRLS_IN_DestWWN_ID 2

} SendRLS_IN, *PSendRLS_IN;

#define SendRLS_IN_SIZE (FIELD_OFFSET(SendRLS_IN, DestWWN) + SendRLS_IN_DestWWN_SIZE)

typedef struct _SendRLS_OUT
{
    // 
    ULONG HBAStatus;
    #define SendRLS_OUT_HBAStatus_SIZE sizeof(ULONG)
    #define SendRLS_OUT_HBAStatus_ID 3

    // 
    ULONG TotalRspBufferSize;
    #define SendRLS_OUT_TotalRspBufferSize_SIZE sizeof(ULONG)
    #define SendRLS_OUT_TotalRspBufferSize_ID 4

    // 
    ULONG ActualRspBufferSize;
    #define SendRLS_OUT_ActualRspBufferSize_SIZE sizeof(ULONG)
    #define SendRLS_OUT_ActualRspBufferSize_ID 5


#define SendRLS_OUT_RspBuffer_SIZE_HINT 28

    // 
    UCHAR RspBuffer[1];
    #define SendRLS_OUT_RspBuffer_ID 6

} SendRLS_OUT, *PSendRLS_OUT;


// HBAFCPID - HBAFCPID
#define HBAFCPIDGuid \
    { 0xff02bc96,0x7fb0,0x4bac, { 0x8f,0x97,0xc7,0x1e,0x49,0x5f,0xa6,0x98 } }

#if ! (defined(MIDL_PASS))
DEFINE_GUID(HBAFCPID_GUID, \
            0xff02bc96,0x7fb0,0x4bac,0x8f,0x97,0xc7,0x1e,0x49,0x5f,0xa6,0x98);
#endif


typedef struct _HBAFCPID
{
    // 
    ULONG Fcid;
    #define HBAFCPID_Fcid_SIZE sizeof(ULONG)
    #define HBAFCPID_Fcid_ID 1

    // 
    UCHAR NodeWWN[8];
    #define HBAFCPID_NodeWWN_SIZE sizeof(UCHAR[8])
    #define HBAFCPID_NodeWWN_ID 2

    // 
    UCHAR PortWWN[8];
    #define HBAFCPID_PortWWN_SIZE sizeof(UCHAR[8])
    #define HBAFCPID_PortWWN_ID 3

    // 
    ULONGLONG FcpLun;
    #define HBAFCPID_FcpLun_SIZE sizeof(ULONGLONG)
    #define HBAFCPID_FcpLun_ID 4

} HBAFCPID, *PHBAFCPID;

#define HBAFCPID_SIZE (FIELD_OFFSET(HBAFCPID, FcpLun) + HBAFCPID_FcpLun_SIZE)

// HBAFCPScsiEntry - HBAFCPScsiEntry
#define HBAFCPScsiEntryGuid \
    { 0x77ca1248,0x1505,0x4221, { 0x8e,0xb6,0xbb,0xb6,0xec,0x77,0x1a,0x87 } }

#if ! (defined(MIDL_PASS))
DEFINE_GUID(HBAFCPScsiEntry_GUID, \
            0x77ca1248,0x1505,0x4221,0x8e,0xb6,0xbb,0xb6,0xec,0x77,0x1a,0x87);
#endif


typedef struct _HBAFCPScsiEntry
{
    // 
    HBAFCPID FCPId;
    #define HBAFCPScsiEntry_FCPId_SIZE sizeof(HBAFCPID)
    #define HBAFCPScsiEntry_FCPId_ID 1

    // 
    UCHAR Luid[256];
    #define HBAFCPScsiEntry_Luid_SIZE sizeof(UCHAR[256])
    #define HBAFCPScsiEntry_Luid_ID 2

    // 
    HBAScsiID ScsiId;
    #define HBAFCPScsiEntry_ScsiId_SIZE sizeof(HBAScsiID)
    #define HBAFCPScsiEntry_ScsiId_ID 3

} HBAFCPScsiEntry, *PHBAFCPScsiEntry;

#define HBAFCPScsiEntry_SIZE (FIELD_OFFSET(HBAFCPScsiEntry, ScsiId) + HBAFCPScsiEntry_ScsiId_SIZE)

// HBAFCPBindingEntry - HBAFCPBindingEntry
#define HBAFCPBindingEntryGuid \
    { 0xfceff8b7,0x9d6b,0x4115, { 0x84,0x22,0x05,0x99,0x24,0x51,0xa6,0x29 } }

#if ! (defined(MIDL_PASS))
DEFINE_GUID(HBAFCPBindingEntry_GUID, \
            0xfceff8b7,0x9d6b,0x4115,0x84,0x22,0x05,0x99,0x24,0x51,0xa6,0x29);
#endif


typedef struct _HBAFCPBindingEntry
{
    // 
    ULONG Type;
    #define HBAFCPBindingEntry_Type_SIZE sizeof(ULONG)
    #define HBAFCPBindingEntry_Type_ID 1

    // 
    HBAFCPID FCPId;
    #define HBAFCPBindingEntry_FCPId_SIZE sizeof(HBAFCPID)
    #define HBAFCPBindingEntry_FCPId_ID 2

    // 
    HBAScsiID ScsiId;
    #define HBAFCPBindingEntry_ScsiId_SIZE sizeof(HBAScsiID)
    #define HBAFCPBindingEntry_ScsiId_ID 3

} HBAFCPBindingEntry, *PHBAFCPBindingEntry;

#define HBAFCPBindingEntry_SIZE (FIELD_OFFSET(HBAFCPBindingEntry, ScsiId) + HBAFCPBindingEntry_ScsiId_SIZE)

// HBAFCPBindingEntry2 - HBAFCPBindingEntry2
#define HBAFCPBindingEntry2Guid \
    { 0x3a1e7679,0x4b1f,0x4f31, { 0xa8,0xae,0xfe,0x92,0x78,0x73,0x09,0x24 } }

#if ! (defined(MIDL_PASS))
DEFINE_GUID(HBAFCPBindingEntry2_GUID, \
            0x3a1e7679,0x4b1f,0x4f31,0xa8,0xae,0xfe,0x92,0x78,0x73,0x09,0x24);
#endif


typedef struct _HBAFCPBindingEntry2
{
    // 
    ULONG Type;
    #define HBAFCPBindingEntry2_Type_SIZE sizeof(ULONG)
    #define HBAFCPBindingEntry2_Type_ID 1

    // 
    HBAFCPID FCPId;
    #define HBAFCPBindingEntry2_FCPId_SIZE sizeof(HBAFCPID)
    #define HBAFCPBindingEntry2_FCPId_ID 2

    // 
    UCHAR Luid[256];
    #define HBAFCPBindingEntry2_Luid_SIZE sizeof(UCHAR[256])
    #define HBAFCPBindingEntry2_Luid_ID 3

    // 
    HBAScsiID ScsiId;
    #define HBAFCPBindingEntry2_ScsiId_SIZE sizeof(HBAScsiID)
    #define HBAFCPBindingEntry2_ScsiId_ID 4

} HBAFCPBindingEntry2, *PHBAFCPBindingEntry2;

#define HBAFCPBindingEntry2_SIZE (FIELD_OFFSET(HBAFCPBindingEntry2, ScsiId) + HBAFCPBindingEntry2_ScsiId_SIZE)

// MSFC_HBAFCPInfo - MSFC_HBAFCPInfo
#define MSFC_HBAFCPInfoGuid \
    { 0x7a1fc391,0x5b23,0x4c19, { 0xb0,0xeb,0xb1,0xae,0xf5,0x90,0x50,0xc3 } }

#if ! (defined(MIDL_PASS))
DEFINE_GUID(MSFC_HBAFCPInfo_GUID, \
            0x7a1fc391,0x5b23,0x4c19,0xb0,0xeb,0xb1,0xae,0xf5,0x90,0x50,0xc3);
#endif

//
// Method id definitions for MSFC_HBAFCPInfo
#define GetFcpTargetMapping     1
typedef struct _GetFcpTargetMapping_IN
{
    // 
    UCHAR HbaPortWWN[8];
    #define GetFcpTargetMapping_IN_HbaPortWWN_SIZE sizeof(UCHAR[8])
    #define GetFcpTargetMapping_IN_HbaPortWWN_ID 1

    // 
    ULONG InEntryCount;
    #define GetFcpTargetMapping_IN_InEntryCount_SIZE sizeof(ULONG)
    #define GetFcpTargetMapping_IN_InEntryCount_ID 2

} GetFcpTargetMapping_IN, *PGetFcpTargetMapping_IN;

#define GetFcpTargetMapping_IN_SIZE (FIELD_OFFSET(GetFcpTargetMapping_IN, InEntryCount) + GetFcpTargetMapping_IN_InEntryCount_SIZE)

typedef struct _GetFcpTargetMapping_OUT
{
    // 
    ULONG HBAStatus;
    #define GetFcpTargetMapping_OUT_HBAStatus_SIZE sizeof(ULONG)
    #define GetFcpTargetMapping_OUT_HBAStatus_ID 3

    // 
    ULONG TotalEntryCount;
    #define GetFcpTargetMapping_OUT_TotalEntryCount_SIZE sizeof(ULONG)
    #define GetFcpTargetMapping_OUT_TotalEntryCount_ID 4

    // 
    ULONG OutEntryCount;
    #define GetFcpTargetMapping_OUT_OutEntryCount_SIZE sizeof(ULONG)
    #define GetFcpTargetMapping_OUT_OutEntryCount_ID 5

    // 
    HBAFCPScsiEntry Entry[1];
    #define GetFcpTargetMapping_OUT_Entry_ID 6

} GetFcpTargetMapping_OUT, *PGetFcpTargetMapping_OUT;

#define GetFcpPersistentBinding     2
typedef struct _GetFcpPersistentBinding_IN
{
    // 
    ULONG InEntryCount;
    #define GetFcpPersistentBinding_IN_InEntryCount_SIZE sizeof(ULONG)
    #define GetFcpPersistentBinding_IN_InEntryCount_ID 1

} GetFcpPersistentBinding_IN, *PGetFcpPersistentBinding_IN;

#define GetFcpPersistentBinding_IN_SIZE (FIELD_OFFSET(GetFcpPersistentBinding_IN, InEntryCount) + GetFcpPersistentBinding_IN_InEntryCount_SIZE)

typedef struct _GetFcpPersistentBinding_OUT
{
    // 
    ULONG HBAStatus;
    #define GetFcpPersistentBinding_OUT_HBAStatus_SIZE sizeof(ULONG)
    #define GetFcpPersistentBinding_OUT_HBAStatus_ID 2

    // 
    ULONG TotalEntryCount;
    #define GetFcpPersistentBinding_OUT_TotalEntryCount_SIZE sizeof(ULONG)
    #define GetFcpPersistentBinding_OUT_TotalEntryCount_ID 3

    // 
    ULONG OutEntryCount;
    #define GetFcpPersistentBinding_OUT_OutEntryCount_SIZE sizeof(ULONG)
    #define GetFcpPersistentBinding_OUT_OutEntryCount_ID 4

    // 
    HBAFCPBindingEntry Entry[1];
    #define GetFcpPersistentBinding_OUT_Entry_ID 5

} GetFcpPersistentBinding_OUT, *PGetFcpPersistentBinding_OUT;

#define GetBindingCapability     3
typedef struct _GetBindingCapability_IN
{
    // 
    UCHAR PortWWN[8];
    #define GetBindingCapability_IN_PortWWN_SIZE sizeof(UCHAR[8])
    #define GetBindingCapability_IN_PortWWN_ID 1

} GetBindingCapability_IN, *PGetBindingCapability_IN;

#define GetBindingCapability_IN_SIZE (FIELD_OFFSET(GetBindingCapability_IN, PortWWN) + GetBindingCapability_IN_PortWWN_SIZE)

typedef struct _GetBindingCapability_OUT
{
    // 
    ULONG HBAStatus;
    #define GetBindingCapability_OUT_HBAStatus_SIZE sizeof(ULONG)
    #define GetBindingCapability_OUT_HBAStatus_ID 2

    // 
    ULONG BindType;
    #define GetBindingCapability_OUT_BindType_SIZE sizeof(ULONG)
    #define GetBindingCapability_OUT_BindType_ID 3

} GetBindingCapability_OUT, *PGetBindingCapability_OUT;

#define GetBindingCapability_OUT_SIZE (FIELD_OFFSET(GetBindingCapability_OUT, BindType) + GetBindingCapability_OUT_BindType_SIZE)

#define GetBindingSupport     4
typedef struct _GetBindingSupport_IN
{
    // 
    UCHAR PortWWN[8];
    #define GetBindingSupport_IN_PortWWN_SIZE sizeof(UCHAR[8])
    #define GetBindingSupport_IN_PortWWN_ID 1

} GetBindingSupport_IN, *PGetBindingSupport_IN;

#define GetBindingSupport_IN_SIZE (FIELD_OFFSET(GetBindingSupport_IN, PortWWN) + GetBindingSupport_IN_PortWWN_SIZE)

typedef struct _GetBindingSupport_OUT
{
    // 
    ULONG HBAStatus;
    #define GetBindingSupport_OUT_HBAStatus_SIZE sizeof(ULONG)
    #define GetBindingSupport_OUT_HBAStatus_ID 2

    // 
    ULONG BindType;
    #define GetBindingSupport_OUT_BindType_SIZE sizeof(ULONG)
    #define GetBindingSupport_OUT_BindType_ID 3

} GetBindingSupport_OUT, *PGetBindingSupport_OUT;

#define GetBindingSupport_OUT_SIZE (FIELD_OFFSET(GetBindingSupport_OUT, BindType) + GetBindingSupport_OUT_BindType_SIZE)

#define SetBindingSupport     5
typedef struct _SetBindingSupport_IN
{
    // 
    UCHAR PortWWN[8];
    #define SetBindingSupport_IN_PortWWN_SIZE sizeof(UCHAR[8])
    #define SetBindingSupport_IN_PortWWN_ID 1

    // 
    ULONG BindType;
    #define SetBindingSupport_IN_BindType_SIZE sizeof(ULONG)
    #define SetBindingSupport_IN_BindType_ID 2

} SetBindingSupport_IN, *PSetBindingSupport_IN;

#define SetBindingSupport_IN_SIZE (FIELD_OFFSET(SetBindingSupport_IN, BindType) + SetBindingSupport_IN_BindType_SIZE)

typedef struct _SetBindingSupport_OUT
{
    // 
    ULONG HBAStatus;
    #define SetBindingSupport_OUT_HBAStatus_SIZE sizeof(ULONG)
    #define SetBindingSupport_OUT_HBAStatus_ID 3

} SetBindingSupport_OUT, *PSetBindingSupport_OUT;

#define SetBindingSupport_OUT_SIZE (FIELD_OFFSET(SetBindingSupport_OUT, HBAStatus) + SetBindingSupport_OUT_HBAStatus_SIZE)

#define GetPersistentBinding2     6
typedef struct _GetPersistentBinding2_IN
{
    // 
    UCHAR PortWWN[8];
    #define GetPersistentBinding2_IN_PortWWN_SIZE sizeof(UCHAR[8])
    #define GetPersistentBinding2_IN_PortWWN_ID 1

    // 
    ULONG InEntryCount;
    #define GetPersistentBinding2_IN_InEntryCount_SIZE sizeof(ULONG)
    #define GetPersistentBinding2_IN_InEntryCount_ID 2

} GetPersistentBinding2_IN, *PGetPersistentBinding2_IN;

#define GetPersistentBinding2_IN_SIZE (FIELD_OFFSET(GetPersistentBinding2_IN, InEntryCount) + GetPersistentBinding2_IN_InEntryCount_SIZE)

typedef struct _GetPersistentBinding2_OUT
{
    // 
    ULONG HBAStatus;
    #define GetPersistentBinding2_OUT_HBAStatus_SIZE sizeof(ULONG)
    #define GetPersistentBinding2_OUT_HBAStatus_ID 3

    // 
    ULONG TotalEntryCount;
    #define GetPersistentBinding2_OUT_TotalEntryCount_SIZE sizeof(ULONG)
    #define GetPersistentBinding2_OUT_TotalEntryCount_ID 4

    // 
    ULONG OutEntryCount;
    #define GetPersistentBinding2_OUT_OutEntryCount_SIZE sizeof(ULONG)
    #define GetPersistentBinding2_OUT_OutEntryCount_ID 5

    // 
    HBAFCPBindingEntry2 Bindings[1];
    #define GetPersistentBinding2_OUT_Bindings_ID 6

} GetPersistentBinding2_OUT, *PGetPersistentBinding2_OUT;



//*********************************************************************
//
//  A call to HBA_SetPersistentBindingV2 will call SetPersistentEntry
//  once for each binding entry.
//  Each binding entry that SetPersistentEntry accepts will be stored
//  in the registry.
//
//  Persistent bindings are stored in the registry under:
//
//     System\CurrentControlSet\Control\Storage\FC\<PortWWN>
//
//         under the REG_BINARY key Bindings  is the struct:
//
//             typedef struct {
//                 ULONG            Version;
//                 HBA_FCPBINDING2  Bindings;
//             } HBAP_PERSISTENT_BINDINGS, *PHBAP_PERSISTENT_BINDINGS;
//
//  This is done so that storport capable drivers may have access to
//  this information during boot
//
//********************************************************************

#define HBA_REGISTRY_BINDING_VERSION        (1)
#define HBA_REGISTRY_BINDING_RELATIVE_PATH  L"System\\CurrentControlSet\\Control\\Storage\\FC"
#define HBA_REGISTRY_BINDING_KEY            L"Bindings"


#define SetPersistentEntry     7
typedef struct _SetPersistentEntry_IN
{
    // 
    UCHAR PortWWN[8];
    #define SetPersistentEntry_IN_PortWWN_SIZE sizeof(UCHAR[8])
    #define SetPersistentEntry_IN_PortWWN_ID 1

    // 
    HBAFCPBindingEntry2 Binding;
    #define SetPersistentEntry_IN_Binding_SIZE sizeof(HBAFCPBindingEntry2)
    #define SetPersistentEntry_IN_Binding_ID 2

} SetPersistentEntry_IN, *PSetPersistentEntry_IN;

#define SetPersistentEntry_IN_SIZE (FIELD_OFFSET(SetPersistentEntry_IN, Binding) + SetPersistentEntry_IN_Binding_SIZE)

typedef struct _SetPersistentEntry_OUT
{
    // 
    ULONG HBAStatus;
    #define SetPersistentEntry_OUT_HBAStatus_SIZE sizeof(ULONG)
    #define SetPersistentEntry_OUT_HBAStatus_ID 3

} SetPersistentEntry_OUT, *PSetPersistentEntry_OUT;

#define SetPersistentEntry_OUT_SIZE (FIELD_OFFSET(SetPersistentEntry_OUT, HBAStatus) + SetPersistentEntry_OUT_HBAStatus_SIZE)

#define RemovePersistentEntry     8
typedef struct _RemovePersistentEntry_IN
{
    // 
    UCHAR PortWWN[8];
    #define RemovePersistentEntry_IN_PortWWN_SIZE sizeof(UCHAR[8])
    #define RemovePersistentEntry_IN_PortWWN_ID 1

    // 
    HBAFCPBindingEntry2 Binding;
    #define RemovePersistentEntry_IN_Binding_SIZE sizeof(HBAFCPBindingEntry2)
    #define RemovePersistentEntry_IN_Binding_ID 2

} RemovePersistentEntry_IN, *PRemovePersistentEntry_IN;

#define RemovePersistentEntry_IN_SIZE (FIELD_OFFSET(RemovePersistentEntry_IN, Binding) + RemovePersistentEntry_IN_Binding_SIZE)

typedef struct _RemovePersistentEntry_OUT
{
    // 
    ULONG HBAStatus;
    #define RemovePersistentEntry_OUT_HBAStatus_SIZE sizeof(ULONG)
    #define RemovePersistentEntry_OUT_HBAStatus_ID 3

} RemovePersistentEntry_OUT, *PRemovePersistentEntry_OUT;

#define RemovePersistentEntry_OUT_SIZE (FIELD_OFFSET(RemovePersistentEntry_OUT, HBAStatus) + RemovePersistentEntry_OUT_HBAStatus_SIZE)


// MSFC_AdapterEvent - MSFC_AdapterEvent
#define MSFC_AdapterEventGuid \
    { 0xe9e47403,0xd1d7,0x43f8, { 0x8e,0xe3,0x53,0xcd,0xbf,0xff,0x56,0x46 } }

#if ! (defined(MIDL_PASS))
DEFINE_GUID(MSFC_AdapterEvent_GUID, \
            0xe9e47403,0xd1d7,0x43f8,0x8e,0xe3,0x53,0xcd,0xbf,0xff,0x56,0x46);
#endif


typedef struct _MSFC_AdapterEvent
{
    // 
    ULONG EventType;
    #define MSFC_AdapterEvent_EventType_SIZE sizeof(ULONG)
    #define MSFC_AdapterEvent_EventType_ID 1

    // 
    UCHAR PortWWN[8];
    #define MSFC_AdapterEvent_PortWWN_SIZE sizeof(UCHAR[8])
    #define MSFC_AdapterEvent_PortWWN_ID 2

} MSFC_AdapterEvent, *PMSFC_AdapterEvent;

#define MSFC_AdapterEvent_SIZE (FIELD_OFFSET(MSFC_AdapterEvent, PortWWN) + MSFC_AdapterEvent_PortWWN_SIZE)

// MSFC_PortEvent - MSFC_PortEvent
#define MSFC_PortEventGuid \
    { 0x095fbe97,0x3876,0x48ef, { 0x8a,0x04,0x1c,0x55,0x93,0x5d,0x0d,0xf5 } }

#if ! (defined(MIDL_PASS))
DEFINE_GUID(MSFC_PortEvent_GUID, \
            0x095fbe97,0x3876,0x48ef,0x8a,0x04,0x1c,0x55,0x93,0x5d,0x0d,0xf5);
#endif


typedef struct _MSFC_PortEvent
{
    // 
    ULONG EventType;
    #define MSFC_PortEvent_EventType_SIZE sizeof(ULONG)
    #define MSFC_PortEvent_EventType_ID 1

    // 
    ULONG FabricPortId;
    #define MSFC_PortEvent_FabricPortId_SIZE sizeof(ULONG)
    #define MSFC_PortEvent_FabricPortId_ID 2

    // 
    UCHAR PortWWN[8];
    #define MSFC_PortEvent_PortWWN_SIZE sizeof(UCHAR[8])
    #define MSFC_PortEvent_PortWWN_ID 3

} MSFC_PortEvent, *PMSFC_PortEvent;

#define MSFC_PortEvent_SIZE (FIELD_OFFSET(MSFC_PortEvent, PortWWN) + MSFC_PortEvent_PortWWN_SIZE)

// MSFC_TargetEvent - MSFC_TargetEvent
#define MSFC_TargetEventGuid \
    { 0xcfa6ef26,0x8675,0x4e27, { 0x9a,0x0b,0xb4,0xa8,0x60,0xdd,0xd0,0xf3 } }

#if ! (defined(MIDL_PASS))
DEFINE_GUID(MSFC_TargetEvent_GUID, \
            0xcfa6ef26,0x8675,0x4e27,0x9a,0x0b,0xb4,0xa8,0x60,0xdd,0xd0,0xf3);
#endif


typedef struct _MSFC_TargetEvent
{
    // 
    ULONG EventType;
    #define MSFC_TargetEvent_EventType_SIZE sizeof(ULONG)
    #define MSFC_TargetEvent_EventType_ID 1

    // 
    UCHAR PortWWN[8];
    #define MSFC_TargetEvent_PortWWN_SIZE sizeof(UCHAR[8])
    #define MSFC_TargetEvent_PortWWN_ID 2

    // 
    UCHAR DiscoveredPortWWN[8];
    #define MSFC_TargetEvent_DiscoveredPortWWN_SIZE sizeof(UCHAR[8])
    #define MSFC_TargetEvent_DiscoveredPortWWN_ID 3

} MSFC_TargetEvent, *PMSFC_TargetEvent;

#define MSFC_TargetEvent_SIZE (FIELD_OFFSET(MSFC_TargetEvent, DiscoveredPortWWN) + MSFC_TargetEvent_DiscoveredPortWWN_SIZE)

// MSFC_EventControl - MSFC_EventControl
#define MSFC_EventControlGuid \
    { 0xa251ccb3,0x5ab0,0x411b, { 0x87,0x71,0x54,0x30,0xef,0x53,0xa2,0x6c } }

#if ! (defined(MIDL_PASS))
DEFINE_GUID(MSFC_EventControl_GUID, \
            0xa251ccb3,0x5ab0,0x411b,0x87,0x71,0x54,0x30,0xef,0x53,0xa2,0x6c);
#endif

//
// Method id definitions for MSFC_EventControl
#define AddTarget     10
typedef struct _AddTarget_IN
{
    // 
    UCHAR HbaPortWWN[8];
    #define AddTarget_IN_HbaPortWWN_SIZE sizeof(UCHAR[8])
    #define AddTarget_IN_HbaPortWWN_ID 1

    // 
    UCHAR DiscoveredPortWWN[8];
    #define AddTarget_IN_DiscoveredPortWWN_SIZE sizeof(UCHAR[8])
    #define AddTarget_IN_DiscoveredPortWWN_ID 2

    // 
    ULONG AllTargets;
    #define AddTarget_IN_AllTargets_SIZE sizeof(ULONG)
    #define AddTarget_IN_AllTargets_ID 3

} AddTarget_IN, *PAddTarget_IN;

#define AddTarget_IN_SIZE (FIELD_OFFSET(AddTarget_IN, AllTargets) + AddTarget_IN_AllTargets_SIZE)

typedef struct _AddTarget_OUT
{
    // 
    ULONG HBAStatus;
    #define AddTarget_OUT_HBAStatus_SIZE sizeof(ULONG)
    #define AddTarget_OUT_HBAStatus_ID 4

} AddTarget_OUT, *PAddTarget_OUT;

#define AddTarget_OUT_SIZE (FIELD_OFFSET(AddTarget_OUT, HBAStatus) + AddTarget_OUT_HBAStatus_SIZE)

#define RemoveTarget     11
typedef struct _RemoveTarget_IN
{
    // 
    UCHAR HbaPortWWN[8];
    #define RemoveTarget_IN_HbaPortWWN_SIZE sizeof(UCHAR[8])
    #define RemoveTarget_IN_HbaPortWWN_ID 1

    // 
    UCHAR DiscoveredPortWWN[8];
    #define RemoveTarget_IN_DiscoveredPortWWN_SIZE sizeof(UCHAR[8])
    #define RemoveTarget_IN_DiscoveredPortWWN_ID 2

    // 
    ULONG AllTargets;
    #define RemoveTarget_IN_AllTargets_SIZE sizeof(ULONG)
    #define RemoveTarget_IN_AllTargets_ID 3

} RemoveTarget_IN, *PRemoveTarget_IN;

#define RemoveTarget_IN_SIZE (FIELD_OFFSET(RemoveTarget_IN, AllTargets) + RemoveTarget_IN_AllTargets_SIZE)

typedef struct _RemoveTarget_OUT
{
    // 
    ULONG HBAStatus;
    #define RemoveTarget_OUT_HBAStatus_SIZE sizeof(ULONG)
    #define RemoveTarget_OUT_HBAStatus_ID 4

} RemoveTarget_OUT, *PRemoveTarget_OUT;

#define RemoveTarget_OUT_SIZE (FIELD_OFFSET(RemoveTarget_OUT, HBAStatus) + RemoveTarget_OUT_HBAStatus_SIZE)

#define AddPort     20
typedef struct _AddPort_IN
{
    // 
    UCHAR PortWWN[8];
    #define AddPort_IN_PortWWN_SIZE sizeof(UCHAR[8])
    #define AddPort_IN_PortWWN_ID 1

} AddPort_IN, *PAddPort_IN;

#define AddPort_IN_SIZE (FIELD_OFFSET(AddPort_IN, PortWWN) + AddPort_IN_PortWWN_SIZE)

typedef struct _AddPort_OUT
{
    // 
    ULONG HBAStatus;
    #define AddPort_OUT_HBAStatus_SIZE sizeof(ULONG)
    #define AddPort_OUT_HBAStatus_ID 2

} AddPort_OUT, *PAddPort_OUT;

#define AddPort_OUT_SIZE (FIELD_OFFSET(AddPort_OUT, HBAStatus) + AddPort_OUT_HBAStatus_SIZE)

#define RemovePort     21
typedef struct _RemovePort_IN
{
    // 
    UCHAR PortWWN[8];
    #define RemovePort_IN_PortWWN_SIZE sizeof(UCHAR[8])
    #define RemovePort_IN_PortWWN_ID 1

} RemovePort_IN, *PRemovePort_IN;

#define RemovePort_IN_SIZE (FIELD_OFFSET(RemovePort_IN, PortWWN) + RemovePort_IN_PortWWN_SIZE)

typedef struct _RemovePort_OUT
{
    // 
    ULONG HBAStatus;
    #define RemovePort_OUT_HBAStatus_SIZE sizeof(ULONG)
    #define RemovePort_OUT_HBAStatus_ID 2

} RemovePort_OUT, *PRemovePort_OUT;

#define RemovePort_OUT_SIZE (FIELD_OFFSET(RemovePort_OUT, HBAStatus) + RemovePort_OUT_HBAStatus_SIZE)

#define AddLink     30
typedef struct _AddLink_OUT
{
    // 
    ULONG HBAStatus;
    #define AddLink_OUT_HBAStatus_SIZE sizeof(ULONG)
    #define AddLink_OUT_HBAStatus_ID 1

} AddLink_OUT, *PAddLink_OUT;

#define AddLink_OUT_SIZE (FIELD_OFFSET(AddLink_OUT, HBAStatus) + AddLink_OUT_HBAStatus_SIZE)

#define RemoveLink     31
typedef struct _RemoveLink_OUT
{
    // 
    ULONG HBAStatus;
    #define RemoveLink_OUT_HBAStatus_SIZE sizeof(ULONG)
    #define RemoveLink_OUT_HBAStatus_ID 1

} RemoveLink_OUT, *PRemoveLink_OUT;

#define RemoveLink_OUT_SIZE (FIELD_OFFSET(RemoveLink_OUT, HBAStatus) + RemoveLink_OUT_HBAStatus_SIZE)


// MS_SM_AdapterInformationQuery - MS_SM_AdapterInformationQuery


#endif // MSFC_HBA_API

#ifdef MS_SM_HBA_API

#define MS_SM_AdapterInformationQueryGuid \
    { 0xbdc67efa,0xe5e7,0x4777, { 0xb1,0x3c,0x62,0x14,0x59,0x65,0x70,0x99 } }

#if ! (defined(MIDL_PASS))
DEFINE_GUID(MS_SM_AdapterInformationQuery_GUID, \
            0xbdc67efa,0xe5e7,0x4777,0xb1,0x3c,0x62,0x14,0x59,0x65,0x70,0x99);
#endif


typedef struct _MS_SM_AdapterInformationQuery
{
    // 
    ULONGLONG UniqueAdapterId;
    #define MS_SM_AdapterInformationQuery_UniqueAdapterId_SIZE sizeof(ULONGLONG)
    #define MS_SM_AdapterInformationQuery_UniqueAdapterId_ID 1

    // 
    ULONG HBAStatus;
    #define MS_SM_AdapterInformationQuery_HBAStatus_SIZE sizeof(ULONG)
    #define MS_SM_AdapterInformationQuery_HBAStatus_ID 2

    // 
    ULONG NumberOfPorts;
    #define MS_SM_AdapterInformationQuery_NumberOfPorts_SIZE sizeof(ULONG)
    #define MS_SM_AdapterInformationQuery_NumberOfPorts_ID 3

    // 
    ULONG VendorSpecificID;
    #define MS_SM_AdapterInformationQuery_VendorSpecificID_SIZE sizeof(ULONG)
    #define MS_SM_AdapterInformationQuery_VendorSpecificID_ID 4



   //******************************************************************
   //
   //  The string type is variable length (up to MaxLen).              
   //  Each string starts with a ushort that holds the strings length  
   //  (in bytes) followed by the WCHARs that make up the string.      
   //
   //******************************************************************


    // 
    WCHAR Manufacturer[64 + 1];
    #define MS_SM_AdapterInformationQuery_Manufacturer_ID 5

    // 
    WCHAR SerialNumber[64 + 1];
    #define MS_SM_AdapterInformationQuery_SerialNumber_ID 6

    // 
    WCHAR Model[256 + 1];
    #define MS_SM_AdapterInformationQuery_Model_ID 7

    // 
    WCHAR ModelDescription[256 + 1];
    #define MS_SM_AdapterInformationQuery_ModelDescription_ID 8

    // 
    WCHAR HardwareVersion[256 + 1];
    #define MS_SM_AdapterInformationQuery_HardwareVersion_ID 9

    // 
    WCHAR DriverVersion[256 + 1];
    #define MS_SM_AdapterInformationQuery_DriverVersion_ID 10

    // 
    WCHAR OptionROMVersion[256 + 1];
    #define MS_SM_AdapterInformationQuery_OptionROMVersion_ID 11

    // 
    WCHAR FirmwareVersion[256 + 1];
    #define MS_SM_AdapterInformationQuery_FirmwareVersion_ID 12

    // 
    WCHAR DriverName[256 + 1];
    #define MS_SM_AdapterInformationQuery_DriverName_ID 13

    // 
    WCHAR HBASymbolicName[256 + 1];
    #define MS_SM_AdapterInformationQuery_HBASymbolicName_ID 14

    // 
    WCHAR RedundantOptionROMVersion[256 + 1];
    #define MS_SM_AdapterInformationQuery_RedundantOptionROMVersion_ID 15

    // 
    WCHAR RedundantFirmwareVersion[256 + 1];
    #define MS_SM_AdapterInformationQuery_RedundantFirmwareVersion_ID 16

    // 
    WCHAR MfgDomain[256 + 1];
    #define MS_SM_AdapterInformationQuery_MfgDomain_ID 17

} MS_SM_AdapterInformationQuery, *PMS_SM_AdapterInformationQuery;

// MS_SMHBA_FC_Port - MS_SMHBA_FC_Port
#define MS_SMHBA_FC_PortGuid \
    { 0x96b827a7,0x2b4a,0x49c8, { 0x90,0x97,0x07,0x82,0x00,0xc5,0xa5,0xcd } }

#if ! (defined(MIDL_PASS))
DEFINE_GUID(MS_SMHBA_FC_Port_GUID, \
            0x96b827a7,0x2b4a,0x49c8,0x90,0x97,0x07,0x82,0x00,0xc5,0xa5,0xcd);
#endif


typedef struct _MS_SMHBA_FC_Port
{
    // 
    UCHAR NodeWWN[8];
    #define MS_SMHBA_FC_Port_NodeWWN_SIZE sizeof(UCHAR[8])
    #define MS_SMHBA_FC_Port_NodeWWN_ID 1

    // 
    UCHAR PortWWN[8];
    #define MS_SMHBA_FC_Port_PortWWN_SIZE sizeof(UCHAR[8])
    #define MS_SMHBA_FC_Port_PortWWN_ID 2

    // 
    ULONG FcId;
    #define MS_SMHBA_FC_Port_FcId_SIZE sizeof(ULONG)
    #define MS_SMHBA_FC_Port_FcId_ID 3

    // 
    ULONG PortSupportedClassofService;
    #define MS_SMHBA_FC_Port_PortSupportedClassofService_SIZE sizeof(ULONG)
    #define MS_SMHBA_FC_Port_PortSupportedClassofService_ID 4

    // 
    UCHAR PortSupportedFc4Types[32];
    #define MS_SMHBA_FC_Port_PortSupportedFc4Types_SIZE sizeof(UCHAR[32])
    #define MS_SMHBA_FC_Port_PortSupportedFc4Types_ID 5

    // 
    UCHAR PortActiveFc4Types[32];
    #define MS_SMHBA_FC_Port_PortActiveFc4Types_SIZE sizeof(UCHAR[32])
    #define MS_SMHBA_FC_Port_PortActiveFc4Types_ID 6

    // 
    UCHAR FabricName[8];
    #define MS_SMHBA_FC_Port_FabricName_SIZE sizeof(UCHAR[8])
    #define MS_SMHBA_FC_Port_FabricName_ID 7

    // 
    ULONG NumberofDiscoveredPorts;
    #define MS_SMHBA_FC_Port_NumberofDiscoveredPorts_SIZE sizeof(ULONG)
    #define MS_SMHBA_FC_Port_NumberofDiscoveredPorts_ID 8

    // 
    UCHAR NumberofPhys;
    #define MS_SMHBA_FC_Port_NumberofPhys_SIZE sizeof(UCHAR)
    #define MS_SMHBA_FC_Port_NumberofPhys_ID 9

    // 
    WCHAR PortSymbolicName[256 + 1];
    #define MS_SMHBA_FC_Port_PortSymbolicName_ID 10

} MS_SMHBA_FC_Port, *PMS_SMHBA_FC_Port;

// MS_SMHBA_SAS_Port - MS_SMHBA_SAS_Port
#define MS_SMHBA_SAS_PortGuid \
    { 0xb914e34f,0x7b80,0x46b0, { 0x80,0x34,0x6d,0x9b,0x68,0x9e,0x1d,0xdd } }

#if ! (defined(MIDL_PASS))
DEFINE_GUID(MS_SMHBA_SAS_Port_GUID, \
            0xb914e34f,0x7b80,0x46b0,0x80,0x34,0x6d,0x9b,0x68,0x9e,0x1d,0xdd);
#endif


typedef struct _MS_SMHBA_SAS_Port
{
    // 
    ULONG PortProtocol;
    #define MS_SMHBA_SAS_Port_PortProtocol_SIZE sizeof(ULONG)
    #define MS_SMHBA_SAS_Port_PortProtocol_ID 1

    // 
    UCHAR LocalSASAddress[8];
    #define MS_SMHBA_SAS_Port_LocalSASAddress_SIZE sizeof(UCHAR[8])
    #define MS_SMHBA_SAS_Port_LocalSASAddress_ID 2

    // 
    UCHAR AttachedSASAddress[8];
    #define MS_SMHBA_SAS_Port_AttachedSASAddress_SIZE sizeof(UCHAR[8])
    #define MS_SMHBA_SAS_Port_AttachedSASAddress_ID 3

    // 
    ULONG NumberofDiscoveredPorts;
    #define MS_SMHBA_SAS_Port_NumberofDiscoveredPorts_SIZE sizeof(ULONG)
    #define MS_SMHBA_SAS_Port_NumberofDiscoveredPorts_ID 4

    // 
    ULONG NumberofPhys;
    #define MS_SMHBA_SAS_Port_NumberofPhys_SIZE sizeof(ULONG)
    #define MS_SMHBA_SAS_Port_NumberofPhys_ID 5

} MS_SMHBA_SAS_Port, *PMS_SMHBA_SAS_Port;

#define MS_SMHBA_SAS_Port_SIZE (FIELD_OFFSET(MS_SMHBA_SAS_Port, NumberofPhys) + MS_SMHBA_SAS_Port_NumberofPhys_SIZE)

// MS_SMHBA_PORTATTRIBUTES - MS_SMHBA_PORTATTRIBUTES
#define MS_SMHBA_PORTATTRIBUTESGuid \
    { 0x50a97b2d,0x99ad,0x4cf9, { 0x84,0x37,0xb4,0xea,0x0c,0x07,0xbe,0x4c } }

#if ! (defined(MIDL_PASS))
DEFINE_GUID(MS_SMHBA_PORTATTRIBUTES_GUID, \
            0x50a97b2d,0x99ad,0x4cf9,0x84,0x37,0xb4,0xea,0x0c,0x07,0xbe,0x4c);
#endif


typedef struct _MS_SMHBA_PORTATTRIBUTES
{
    // 
    ULONG PortType;
    #define MS_SMHBA_PORTATTRIBUTES_PortType_SIZE sizeof(ULONG)
    #define MS_SMHBA_PORTATTRIBUTES_PortType_ID 1

    // 
    ULONG PortState;
    #define MS_SMHBA_PORTATTRIBUTES_PortState_SIZE sizeof(ULONG)
    #define MS_SMHBA_PORTATTRIBUTES_PortState_ID 2

    // 
    ULONG PortSpecificAttributesSize;
    #define MS_SMHBA_PORTATTRIBUTES_PortSpecificAttributesSize_SIZE sizeof(ULONG)
    #define MS_SMHBA_PORTATTRIBUTES_PortSpecificAttributesSize_ID 3

    // 
    WCHAR OSDeviceName[256 + 1];
    #define MS_SMHBA_PORTATTRIBUTES_OSDeviceName_ID 4

    // 
    ULONGLONG Reserved;
    #define MS_SMHBA_PORTATTRIBUTES_Reserved_SIZE sizeof(ULONGLONG)
    #define MS_SMHBA_PORTATTRIBUTES_Reserved_ID 5

    // 
    UCHAR PortSpecificAttributes[1];
    #define MS_SMHBA_PORTATTRIBUTES_PortSpecificAttributes_ID 6

} MS_SMHBA_PORTATTRIBUTES, *PMS_SMHBA_PORTATTRIBUTES;

// MS_SMHBA_PROTOCOLSTATISTICS - MS_SMHBA_PROTOCOLSTATISTICS
#define MS_SMHBA_PROTOCOLSTATISTICSGuid \
    { 0xb557bd86,0x4128,0x4d5c, { 0xb6,0xe6,0xb6,0x5f,0x9b,0xd6,0x87,0x22 } }

#if ! (defined(MIDL_PASS))
DEFINE_GUID(MS_SMHBA_PROTOCOLSTATISTICS_GUID, \
            0xb557bd86,0x4128,0x4d5c,0xb6,0xe6,0xb6,0x5f,0x9b,0xd6,0x87,0x22);
#endif


typedef struct _MS_SMHBA_PROTOCOLSTATISTICS
{
    // 
    LONGLONG SecondsSinceLastReset;
    #define MS_SMHBA_PROTOCOLSTATISTICS_SecondsSinceLastReset_SIZE sizeof(LONGLONG)
    #define MS_SMHBA_PROTOCOLSTATISTICS_SecondsSinceLastReset_ID 1

    // 
    LONGLONG InputRequests;
    #define MS_SMHBA_PROTOCOLSTATISTICS_InputRequests_SIZE sizeof(LONGLONG)
    #define MS_SMHBA_PROTOCOLSTATISTICS_InputRequests_ID 2

    // 
    LONGLONG OutputRequests;
    #define MS_SMHBA_PROTOCOLSTATISTICS_OutputRequests_SIZE sizeof(LONGLONG)
    #define MS_SMHBA_PROTOCOLSTATISTICS_OutputRequests_ID 3

    // 
    LONGLONG ControlRequests;
    #define MS_SMHBA_PROTOCOLSTATISTICS_ControlRequests_SIZE sizeof(LONGLONG)
    #define MS_SMHBA_PROTOCOLSTATISTICS_ControlRequests_ID 4

    // 
    LONGLONG InputMegabytes;
    #define MS_SMHBA_PROTOCOLSTATISTICS_InputMegabytes_SIZE sizeof(LONGLONG)
    #define MS_SMHBA_PROTOCOLSTATISTICS_InputMegabytes_ID 5

    // 
    LONGLONG OutputMegabytes;
    #define MS_SMHBA_PROTOCOLSTATISTICS_OutputMegabytes_SIZE sizeof(LONGLONG)
    #define MS_SMHBA_PROTOCOLSTATISTICS_OutputMegabytes_ID 6

} MS_SMHBA_PROTOCOLSTATISTICS, *PMS_SMHBA_PROTOCOLSTATISTICS;

#define MS_SMHBA_PROTOCOLSTATISTICS_SIZE (FIELD_OFFSET(MS_SMHBA_PROTOCOLSTATISTICS, OutputMegabytes) + MS_SMHBA_PROTOCOLSTATISTICS_OutputMegabytes_SIZE)

// MS_SMHBA_SASPHYSTATISTICS - MS_SMHBA_SASPHYSTATISTICS
#define MS_SMHBA_SASPHYSTATISTICSGuid \
    { 0xbd458e7d,0xc40a,0x4401, { 0xa1,0x79,0x11,0x91,0x9c,0xbc,0xc5,0xc6 } }

#if ! (defined(MIDL_PASS))
DEFINE_GUID(MS_SMHBA_SASPHYSTATISTICS_GUID, \
            0xbd458e7d,0xc40a,0x4401,0xa1,0x79,0x11,0x91,0x9c,0xbc,0xc5,0xc6);
#endif


typedef struct _MS_SMHBA_SASPHYSTATISTICS
{
    // 
    LONGLONG SecondsSinceLastReset;
    #define MS_SMHBA_SASPHYSTATISTICS_SecondsSinceLastReset_SIZE sizeof(LONGLONG)
    #define MS_SMHBA_SASPHYSTATISTICS_SecondsSinceLastReset_ID 1

    // 
    LONGLONG TxFrames;
    #define MS_SMHBA_SASPHYSTATISTICS_TxFrames_SIZE sizeof(LONGLONG)
    #define MS_SMHBA_SASPHYSTATISTICS_TxFrames_ID 2

    // 
    LONGLONG TxWords;
    #define MS_SMHBA_SASPHYSTATISTICS_TxWords_SIZE sizeof(LONGLONG)
    #define MS_SMHBA_SASPHYSTATISTICS_TxWords_ID 3

    // 
    LONGLONG RxFrames;
    #define MS_SMHBA_SASPHYSTATISTICS_RxFrames_SIZE sizeof(LONGLONG)
    #define MS_SMHBA_SASPHYSTATISTICS_RxFrames_ID 4

    // 
    LONGLONG RxWords;
    #define MS_SMHBA_SASPHYSTATISTICS_RxWords_SIZE sizeof(LONGLONG)
    #define MS_SMHBA_SASPHYSTATISTICS_RxWords_ID 5

    // 
    LONGLONG InvalidDwordCount;
    #define MS_SMHBA_SASPHYSTATISTICS_InvalidDwordCount_SIZE sizeof(LONGLONG)
    #define MS_SMHBA_SASPHYSTATISTICS_InvalidDwordCount_ID 6

    // 
    LONGLONG RunningDisparityErrorCount;
    #define MS_SMHBA_SASPHYSTATISTICS_RunningDisparityErrorCount_SIZE sizeof(LONGLONG)
    #define MS_SMHBA_SASPHYSTATISTICS_RunningDisparityErrorCount_ID 7

    // 
    LONGLONG LossofDwordSyncCount;
    #define MS_SMHBA_SASPHYSTATISTICS_LossofDwordSyncCount_SIZE sizeof(LONGLONG)
    #define MS_SMHBA_SASPHYSTATISTICS_LossofDwordSyncCount_ID 8

    // 
    LONGLONG PhyResetProblemCount;
    #define MS_SMHBA_SASPHYSTATISTICS_PhyResetProblemCount_SIZE sizeof(LONGLONG)
    #define MS_SMHBA_SASPHYSTATISTICS_PhyResetProblemCount_ID 9

} MS_SMHBA_SASPHYSTATISTICS, *PMS_SMHBA_SASPHYSTATISTICS;

#define MS_SMHBA_SASPHYSTATISTICS_SIZE (FIELD_OFFSET(MS_SMHBA_SASPHYSTATISTICS, PhyResetProblemCount) + MS_SMHBA_SASPHYSTATISTICS_PhyResetProblemCount_SIZE)

// MS_SMHBA_FC_PHY - MS_SMHBA_FC_PHY
#define MS_SMHBA_FC_PHYGuid \
    { 0xfb66c8fe,0x1da0,0x48a2, { 0x92,0xdb,0x02,0xc3,0x41,0x14,0x3c,0x46 } }

#if ! (defined(MIDL_PASS))
DEFINE_GUID(MS_SMHBA_FC_PHY_GUID, \
            0xfb66c8fe,0x1da0,0x48a2,0x92,0xdb,0x02,0xc3,0x41,0x14,0x3c,0x46);
#endif


typedef struct _MS_SMHBA_FC_PHY
{
    // 
    ULONG PhySupportSpeed;
    #define MS_SMHBA_FC_PHY_PhySupportSpeed_SIZE sizeof(ULONG)
    #define MS_SMHBA_FC_PHY_PhySupportSpeed_ID 1

    // 
    ULONG PhySpeed;
    #define MS_SMHBA_FC_PHY_PhySpeed_SIZE sizeof(ULONG)
    #define MS_SMHBA_FC_PHY_PhySpeed_ID 2

    // 
    UCHAR PhyType;
    #define MS_SMHBA_FC_PHY_PhyType_SIZE sizeof(UCHAR)
    #define MS_SMHBA_FC_PHY_PhyType_ID 3

    // 
    ULONG MaxFrameSize;
    #define MS_SMHBA_FC_PHY_MaxFrameSize_SIZE sizeof(ULONG)
    #define MS_SMHBA_FC_PHY_MaxFrameSize_ID 4

} MS_SMHBA_FC_PHY, *PMS_SMHBA_FC_PHY;

#define MS_SMHBA_FC_PHY_SIZE (FIELD_OFFSET(MS_SMHBA_FC_PHY, MaxFrameSize) + MS_SMHBA_FC_PHY_MaxFrameSize_SIZE)

// MS_SMHBA_SAS_PHY - MS_SMHBA_SAS_PHY
#define MS_SMHBA_SAS_PHYGuid \
    { 0xdde0a090,0x96bc,0x452b, { 0x9a,0x64,0x6f,0xbb,0x6a,0x19,0xc4,0x7d } }

#if ! (defined(MIDL_PASS))
DEFINE_GUID(MS_SMHBA_SAS_PHY_GUID, \
            0xdde0a090,0x96bc,0x452b,0x9a,0x64,0x6f,0xbb,0x6a,0x19,0xc4,0x7d);
#endif


typedef struct _MS_SMHBA_SAS_PHY
{
    // 
    UCHAR PhyIdentifier;
    #define MS_SMHBA_SAS_PHY_PhyIdentifier_SIZE sizeof(UCHAR)
    #define MS_SMHBA_SAS_PHY_PhyIdentifier_ID 1

    // 
    ULONG NegotiatedLinkRate;
    #define MS_SMHBA_SAS_PHY_NegotiatedLinkRate_SIZE sizeof(ULONG)
    #define MS_SMHBA_SAS_PHY_NegotiatedLinkRate_ID 2

    // 
    ULONG ProgrammedMinLinkRate;
    #define MS_SMHBA_SAS_PHY_ProgrammedMinLinkRate_SIZE sizeof(ULONG)
    #define MS_SMHBA_SAS_PHY_ProgrammedMinLinkRate_ID 3

    // 
    ULONG HardwareMinLinkRate;
    #define MS_SMHBA_SAS_PHY_HardwareMinLinkRate_SIZE sizeof(ULONG)
    #define MS_SMHBA_SAS_PHY_HardwareMinLinkRate_ID 4

    // 
    ULONG ProgrammedMaxLinkRate;
    #define MS_SMHBA_SAS_PHY_ProgrammedMaxLinkRate_SIZE sizeof(ULONG)
    #define MS_SMHBA_SAS_PHY_ProgrammedMaxLinkRate_ID 5

    // 
    ULONG HardwareMaxLinkRate;
    #define MS_SMHBA_SAS_PHY_HardwareMaxLinkRate_SIZE sizeof(ULONG)
    #define MS_SMHBA_SAS_PHY_HardwareMaxLinkRate_ID 6

    // 
    UCHAR domainPortWWN[8];
    #define MS_SMHBA_SAS_PHY_domainPortWWN_SIZE sizeof(UCHAR[8])
    #define MS_SMHBA_SAS_PHY_domainPortWWN_ID 7

} MS_SMHBA_SAS_PHY, *PMS_SMHBA_SAS_PHY;

#define MS_SMHBA_SAS_PHY_SIZE (FIELD_OFFSET(MS_SMHBA_SAS_PHY, domainPortWWN) + MS_SMHBA_SAS_PHY_domainPortWWN_SIZE)

// MS_SM_PortInformationMethods - MS_SM_PortInformationMethods
#define MS_SM_PortInformationMethodsGuid \
    { 0x5b6a8b86,0x708d,0x4ec6, { 0x82,0xa6,0x39,0xad,0xcf,0x6f,0x64,0x33 } }

#if ! (defined(MIDL_PASS))
DEFINE_GUID(MS_SM_PortInformationMethods_GUID, \
            0x5b6a8b86,0x708d,0x4ec6,0x82,0xa6,0x39,0xad,0xcf,0x6f,0x64,0x33);
#endif

//
// Method id definitions for MS_SM_PortInformationMethods
#define SM_GetPortType     1
typedef struct _SM_GetPortType_IN
{
    // 
    ULONG PortIndex;
    #define SM_GetPortType_IN_PortIndex_SIZE sizeof(ULONG)
    #define SM_GetPortType_IN_PortIndex_ID 1

} SM_GetPortType_IN, *PSM_GetPortType_IN;

#define SM_GetPortType_IN_SIZE (FIELD_OFFSET(SM_GetPortType_IN, PortIndex) + SM_GetPortType_IN_PortIndex_SIZE)

typedef struct _SM_GetPortType_OUT
{
    // 
    ULONG HBAStatus;
    #define SM_GetPortType_OUT_HBAStatus_SIZE sizeof(ULONG)
    #define SM_GetPortType_OUT_HBAStatus_ID 2

    // 
    ULONG PortType;
    #define SM_GetPortType_OUT_PortType_SIZE sizeof(ULONG)
    #define SM_GetPortType_OUT_PortType_ID 3

} SM_GetPortType_OUT, *PSM_GetPortType_OUT;

#define SM_GetPortType_OUT_SIZE (FIELD_OFFSET(SM_GetPortType_OUT, PortType) + SM_GetPortType_OUT_PortType_SIZE)

#define SM_GetAdapterPortAttributes     2
typedef struct _SM_GetAdapterPortAttributes_IN
{
    // 
    ULONG PortIndex;
    #define SM_GetAdapterPortAttributes_IN_PortIndex_SIZE sizeof(ULONG)
    #define SM_GetAdapterPortAttributes_IN_PortIndex_ID 1


#define SM_PORT_SPECIFIC_ATTRIBUTES_MAXSIZE  max(sizeof(MS_SMHBA_FC_Port),  sizeof(MS_SMHBA_SAS_Port))
    // 
    ULONG PortSpecificAttributesMaxSize;
    #define SM_GetAdapterPortAttributes_IN_PortSpecificAttributesMaxSize_SIZE sizeof(ULONG)
    #define SM_GetAdapterPortAttributes_IN_PortSpecificAttributesMaxSize_ID 2

} SM_GetAdapterPortAttributes_IN, *PSM_GetAdapterPortAttributes_IN;

#define SM_GetAdapterPortAttributes_IN_SIZE (FIELD_OFFSET(SM_GetAdapterPortAttributes_IN, PortSpecificAttributesMaxSize) + SM_GetAdapterPortAttributes_IN_PortSpecificAttributesMaxSize_SIZE)

typedef struct _SM_GetAdapterPortAttributes_OUT
{
    // 
    ULONG HBAStatus;
    #define SM_GetAdapterPortAttributes_OUT_HBAStatus_SIZE sizeof(ULONG)
    #define SM_GetAdapterPortAttributes_OUT_HBAStatus_ID 3

    // 
    MS_SMHBA_PORTATTRIBUTES PortAttributes;
    #define SM_GetAdapterPortAttributes_OUT_PortAttributes_SIZE sizeof(MS_SMHBA_PORTATTRIBUTES)
    #define SM_GetAdapterPortAttributes_OUT_PortAttributes_ID 4

} SM_GetAdapterPortAttributes_OUT, *PSM_GetAdapterPortAttributes_OUT;

#define SM_GetAdapterPortAttributes_OUT_SIZE (FIELD_OFFSET(SM_GetAdapterPortAttributes_OUT, PortAttributes) + SM_GetAdapterPortAttributes_OUT_PortAttributes_SIZE)

#define SM_GetDiscoveredPortAttributes     3
typedef struct _SM_GetDiscoveredPortAttributes_IN
{
    // 
    ULONG PortIndex;
    #define SM_GetDiscoveredPortAttributes_IN_PortIndex_SIZE sizeof(ULONG)
    #define SM_GetDiscoveredPortAttributes_IN_PortIndex_ID 1

    // 
    ULONG DiscoveredPortIndex;
    #define SM_GetDiscoveredPortAttributes_IN_DiscoveredPortIndex_SIZE sizeof(ULONG)
    #define SM_GetDiscoveredPortAttributes_IN_DiscoveredPortIndex_ID 2

    // 
    ULONG PortSpecificAttributesMaxSize;
    #define SM_GetDiscoveredPortAttributes_IN_PortSpecificAttributesMaxSize_SIZE sizeof(ULONG)
    #define SM_GetDiscoveredPortAttributes_IN_PortSpecificAttributesMaxSize_ID 3

} SM_GetDiscoveredPortAttributes_IN, *PSM_GetDiscoveredPortAttributes_IN;

#define SM_GetDiscoveredPortAttributes_IN_SIZE (FIELD_OFFSET(SM_GetDiscoveredPortAttributes_IN, PortSpecificAttributesMaxSize) + SM_GetDiscoveredPortAttributes_IN_PortSpecificAttributesMaxSize_SIZE)

typedef struct _SM_GetDiscoveredPortAttributes_OUT
{
    // 
    ULONG HBAStatus;
    #define SM_GetDiscoveredPortAttributes_OUT_HBAStatus_SIZE sizeof(ULONG)
    #define SM_GetDiscoveredPortAttributes_OUT_HBAStatus_ID 4

    // 
    MS_SMHBA_PORTATTRIBUTES PortAttributes;
    #define SM_GetDiscoveredPortAttributes_OUT_PortAttributes_SIZE sizeof(MS_SMHBA_PORTATTRIBUTES)
    #define SM_GetDiscoveredPortAttributes_OUT_PortAttributes_ID 5

} SM_GetDiscoveredPortAttributes_OUT, *PSM_GetDiscoveredPortAttributes_OUT;

#define SM_GetDiscoveredPortAttributes_OUT_SIZE (FIELD_OFFSET(SM_GetDiscoveredPortAttributes_OUT, PortAttributes) + SM_GetDiscoveredPortAttributes_OUT_PortAttributes_SIZE)

#define SM_GetPortAttributesByWWN     4
typedef struct _SM_GetPortAttributesByWWN_IN
{
    // 
    UCHAR PortWWN[8];
    #define SM_GetPortAttributesByWWN_IN_PortWWN_SIZE sizeof(UCHAR[8])
    #define SM_GetPortAttributesByWWN_IN_PortWWN_ID 1

    // 
    UCHAR DomainPortWWN[8];
    #define SM_GetPortAttributesByWWN_IN_DomainPortWWN_SIZE sizeof(UCHAR[8])
    #define SM_GetPortAttributesByWWN_IN_DomainPortWWN_ID 2

    // 
    ULONG PortSpecificAttributesMaxSize;
    #define SM_GetPortAttributesByWWN_IN_PortSpecificAttributesMaxSize_SIZE sizeof(ULONG)
    #define SM_GetPortAttributesByWWN_IN_PortSpecificAttributesMaxSize_ID 3

} SM_GetPortAttributesByWWN_IN, *PSM_GetPortAttributesByWWN_IN;

#define SM_GetPortAttributesByWWN_IN_SIZE (FIELD_OFFSET(SM_GetPortAttributesByWWN_IN, PortSpecificAttributesMaxSize) + SM_GetPortAttributesByWWN_IN_PortSpecificAttributesMaxSize_SIZE)

typedef struct _SM_GetPortAttributesByWWN_OUT
{
    // 
    ULONG HBAStatus;
    #define SM_GetPortAttributesByWWN_OUT_HBAStatus_SIZE sizeof(ULONG)
    #define SM_GetPortAttributesByWWN_OUT_HBAStatus_ID 4

    // 
    MS_SMHBA_PORTATTRIBUTES PortAttributes;
    #define SM_GetPortAttributesByWWN_OUT_PortAttributes_SIZE sizeof(MS_SMHBA_PORTATTRIBUTES)
    #define SM_GetPortAttributesByWWN_OUT_PortAttributes_ID 5

} SM_GetPortAttributesByWWN_OUT, *PSM_GetPortAttributesByWWN_OUT;

#define SM_GetPortAttributesByWWN_OUT_SIZE (FIELD_OFFSET(SM_GetPortAttributesByWWN_OUT, PortAttributes) + SM_GetPortAttributesByWWN_OUT_PortAttributes_SIZE)

#define SM_GetProtocolStatistics     5
typedef struct _SM_GetProtocolStatistics_IN
{
    // 
    ULONG PortIndex;
    #define SM_GetProtocolStatistics_IN_PortIndex_SIZE sizeof(ULONG)
    #define SM_GetProtocolStatistics_IN_PortIndex_ID 1

    // 
    ULONG ProtocolType;
    #define SM_GetProtocolStatistics_IN_ProtocolType_SIZE sizeof(ULONG)
    #define SM_GetProtocolStatistics_IN_ProtocolType_ID 2

} SM_GetProtocolStatistics_IN, *PSM_GetProtocolStatistics_IN;

#define SM_GetProtocolStatistics_IN_SIZE (FIELD_OFFSET(SM_GetProtocolStatistics_IN, ProtocolType) + SM_GetProtocolStatistics_IN_ProtocolType_SIZE)

typedef struct _SM_GetProtocolStatistics_OUT
{
    // 
    ULONG HBAStatus;
    #define SM_GetProtocolStatistics_OUT_HBAStatus_SIZE sizeof(ULONG)
    #define SM_GetProtocolStatistics_OUT_HBAStatus_ID 3

    // 
    MS_SMHBA_PROTOCOLSTATISTICS ProtocolStatistics;
    #define SM_GetProtocolStatistics_OUT_ProtocolStatistics_SIZE sizeof(MS_SMHBA_PROTOCOLSTATISTICS)
    #define SM_GetProtocolStatistics_OUT_ProtocolStatistics_ID 4

} SM_GetProtocolStatistics_OUT, *PSM_GetProtocolStatistics_OUT;

#define SM_GetProtocolStatistics_OUT_SIZE (FIELD_OFFSET(SM_GetProtocolStatistics_OUT, ProtocolStatistics) + SM_GetProtocolStatistics_OUT_ProtocolStatistics_SIZE)

#define SM_GetPhyStatistics     6
typedef struct _SM_GetPhyStatistics_IN
{
    // 
    ULONG PortIndex;
    #define SM_GetPhyStatistics_IN_PortIndex_SIZE sizeof(ULONG)
    #define SM_GetPhyStatistics_IN_PortIndex_ID 1

    // 
    ULONG PhyIndex;
    #define SM_GetPhyStatistics_IN_PhyIndex_SIZE sizeof(ULONG)
    #define SM_GetPhyStatistics_IN_PhyIndex_ID 2

    // 
    ULONG InNumOfPhyCounters;
    #define SM_GetPhyStatistics_IN_InNumOfPhyCounters_SIZE sizeof(ULONG)
    #define SM_GetPhyStatistics_IN_InNumOfPhyCounters_ID 3

} SM_GetPhyStatistics_IN, *PSM_GetPhyStatistics_IN;

#define SM_GetPhyStatistics_IN_SIZE (FIELD_OFFSET(SM_GetPhyStatistics_IN, InNumOfPhyCounters) + SM_GetPhyStatistics_IN_InNumOfPhyCounters_SIZE)

typedef struct _SM_GetPhyStatistics_OUT
{
    // 
    ULONG HBAStatus;
    #define SM_GetPhyStatistics_OUT_HBAStatus_SIZE sizeof(ULONG)
    #define SM_GetPhyStatistics_OUT_HBAStatus_ID 4

    // 
    ULONG TotalNumOfPhyCounters;
    #define SM_GetPhyStatistics_OUT_TotalNumOfPhyCounters_SIZE sizeof(ULONG)
    #define SM_GetPhyStatistics_OUT_TotalNumOfPhyCounters_ID 5

    // 
    ULONG OutNumOfPhyCounters;
    #define SM_GetPhyStatistics_OUT_OutNumOfPhyCounters_SIZE sizeof(ULONG)
    #define SM_GetPhyStatistics_OUT_OutNumOfPhyCounters_ID 6

    // 
    LONGLONG PhyCounter[1];
    #define SM_GetPhyStatistics_OUT_PhyCounter_ID 7

} SM_GetPhyStatistics_OUT, *PSM_GetPhyStatistics_OUT;

#define SM_GetFCPhyAttributes     7
typedef struct _SM_GetFCPhyAttributes_IN
{
    // 
    ULONG PortIndex;
    #define SM_GetFCPhyAttributes_IN_PortIndex_SIZE sizeof(ULONG)
    #define SM_GetFCPhyAttributes_IN_PortIndex_ID 1

    // 
    ULONG PhyIndex;
    #define SM_GetFCPhyAttributes_IN_PhyIndex_SIZE sizeof(ULONG)
    #define SM_GetFCPhyAttributes_IN_PhyIndex_ID 2

} SM_GetFCPhyAttributes_IN, *PSM_GetFCPhyAttributes_IN;

#define SM_GetFCPhyAttributes_IN_SIZE (FIELD_OFFSET(SM_GetFCPhyAttributes_IN, PhyIndex) + SM_GetFCPhyAttributes_IN_PhyIndex_SIZE)

typedef struct _SM_GetFCPhyAttributes_OUT
{
    // 
    ULONG HBAStatus;
    #define SM_GetFCPhyAttributes_OUT_HBAStatus_SIZE sizeof(ULONG)
    #define SM_GetFCPhyAttributes_OUT_HBAStatus_ID 3

    // 
    MS_SMHBA_FC_PHY PhyType;
    #define SM_GetFCPhyAttributes_OUT_PhyType_SIZE sizeof(MS_SMHBA_FC_PHY)
    #define SM_GetFCPhyAttributes_OUT_PhyType_ID 4

} SM_GetFCPhyAttributes_OUT, *PSM_GetFCPhyAttributes_OUT;

#define SM_GetFCPhyAttributes_OUT_SIZE (FIELD_OFFSET(SM_GetFCPhyAttributes_OUT, PhyType) + SM_GetFCPhyAttributes_OUT_PhyType_SIZE)

#define SM_GetSASPhyAttributes     8
typedef struct _SM_GetSASPhyAttributes_IN
{
    // 
    ULONG PortIndex;
    #define SM_GetSASPhyAttributes_IN_PortIndex_SIZE sizeof(ULONG)
    #define SM_GetSASPhyAttributes_IN_PortIndex_ID 1

    // 
    ULONG PhyIndex;
    #define SM_GetSASPhyAttributes_IN_PhyIndex_SIZE sizeof(ULONG)
    #define SM_GetSASPhyAttributes_IN_PhyIndex_ID 2

} SM_GetSASPhyAttributes_IN, *PSM_GetSASPhyAttributes_IN;

#define SM_GetSASPhyAttributes_IN_SIZE (FIELD_OFFSET(SM_GetSASPhyAttributes_IN, PhyIndex) + SM_GetSASPhyAttributes_IN_PhyIndex_SIZE)

typedef struct _SM_GetSASPhyAttributes_OUT
{
    // 
    ULONG HBAStatus;
    #define SM_GetSASPhyAttributes_OUT_HBAStatus_SIZE sizeof(ULONG)
    #define SM_GetSASPhyAttributes_OUT_HBAStatus_ID 3

    // 
    MS_SMHBA_SAS_PHY PhyType;
    #define SM_GetSASPhyAttributes_OUT_PhyType_SIZE sizeof(MS_SMHBA_SAS_PHY)
    #define SM_GetSASPhyAttributes_OUT_PhyType_ID 4

} SM_GetSASPhyAttributes_OUT, *PSM_GetSASPhyAttributes_OUT;

#define SM_GetSASPhyAttributes_OUT_SIZE (FIELD_OFFSET(SM_GetSASPhyAttributes_OUT, PhyType) + SM_GetSASPhyAttributes_OUT_PhyType_SIZE)

#define SM_RefreshInformation     10

// MS_SMHBA_PORTLUN - MS_SMHBA_PORTLUN
#define MS_SMHBA_PORTLUNGuid \
    { 0x0669d100,0x066e,0x4e49, { 0xa6,0x8c,0xe0,0x51,0x99,0x59,0x61,0x32 } }

#if ! (defined(MIDL_PASS))
DEFINE_GUID(MS_SMHBA_PORTLUN_GUID, \
            0x0669d100,0x066e,0x4e49,0xa6,0x8c,0xe0,0x51,0x99,0x59,0x61,0x32);
#endif


typedef struct _MS_SMHBA_PORTLUN
{
    // 
    UCHAR PortWWN[8];
    #define MS_SMHBA_PORTLUN_PortWWN_SIZE sizeof(UCHAR[8])
    #define MS_SMHBA_PORTLUN_PortWWN_ID 1

    // 
    UCHAR domainPortWWN[8];
    #define MS_SMHBA_PORTLUN_domainPortWWN_SIZE sizeof(UCHAR[8])
    #define MS_SMHBA_PORTLUN_domainPortWWN_ID 2

    // 
    ULONGLONG TargetLun;
    #define MS_SMHBA_PORTLUN_TargetLun_SIZE sizeof(ULONGLONG)
    #define MS_SMHBA_PORTLUN_TargetLun_ID 3

} MS_SMHBA_PORTLUN, *PMS_SMHBA_PORTLUN;

#define MS_SMHBA_PORTLUN_SIZE (FIELD_OFFSET(MS_SMHBA_PORTLUN, TargetLun) + MS_SMHBA_PORTLUN_TargetLun_SIZE)

// MS_SMHBA_SCSIENTRY - MS_SMHBA_SCSIENTRY
#define MS_SMHBA_SCSIENTRYGuid \
    { 0x125d41bc,0x7643,0x4155, { 0xb8,0x1c,0xe2,0xf1,0x28,0xad,0x1f,0xb4 } }

#if ! (defined(MIDL_PASS))
DEFINE_GUID(MS_SMHBA_SCSIENTRY_GUID, \
            0x125d41bc,0x7643,0x4155,0xb8,0x1c,0xe2,0xf1,0x28,0xad,0x1f,0xb4);
#endif


typedef struct _MS_SMHBA_SCSIENTRY
{
    // 
    MS_SMHBA_PORTLUN PortLun;
    #define MS_SMHBA_SCSIENTRY_PortLun_SIZE sizeof(MS_SMHBA_PORTLUN)
    #define MS_SMHBA_SCSIENTRY_PortLun_ID 1

    // 
    UCHAR LUID[256];
    #define MS_SMHBA_SCSIENTRY_LUID_SIZE sizeof(UCHAR[256])
    #define MS_SMHBA_SCSIENTRY_LUID_ID 2

    // 
    HBAScsiID ScsiId;
    #define MS_SMHBA_SCSIENTRY_ScsiId_SIZE sizeof(HBAScsiID)
    #define MS_SMHBA_SCSIENTRY_ScsiId_ID 3

} MS_SMHBA_SCSIENTRY, *PMS_SMHBA_SCSIENTRY;

#define MS_SMHBA_SCSIENTRY_SIZE (FIELD_OFFSET(MS_SMHBA_SCSIENTRY, ScsiId) + MS_SMHBA_SCSIENTRY_ScsiId_SIZE)

// MS_SMHBA_BINDINGENTRY - MS_SMHBA_BINDINGENTRY
#define MS_SMHBA_BINDINGENTRYGuid \
    { 0x65bfb548,0xd00a,0x4d4c, { 0xa3,0x57,0x7d,0xaa,0x23,0xbc,0x2e,0x3d } }

#if ! (defined(MIDL_PASS))
DEFINE_GUID(MS_SMHBA_BINDINGENTRY_GUID, \
            0x65bfb548,0xd00a,0x4d4c,0xa3,0x57,0x7d,0xaa,0x23,0xbc,0x2e,0x3d);
#endif


typedef struct _MS_SMHBA_BINDINGENTRY
{
    // 
    ULONG type;
    #define MS_SMHBA_BINDINGENTRY_type_SIZE sizeof(ULONG)
    #define MS_SMHBA_BINDINGENTRY_type_ID 1

    // 
    MS_SMHBA_PORTLUN PortLun;
    #define MS_SMHBA_BINDINGENTRY_PortLun_SIZE sizeof(MS_SMHBA_PORTLUN)
    #define MS_SMHBA_BINDINGENTRY_PortLun_ID 2

    // 
    UCHAR LUID[256];
    #define MS_SMHBA_BINDINGENTRY_LUID_SIZE sizeof(UCHAR[256])
    #define MS_SMHBA_BINDINGENTRY_LUID_ID 3

    // 
    ULONG Status;
    #define MS_SMHBA_BINDINGENTRY_Status_SIZE sizeof(ULONG)
    #define MS_SMHBA_BINDINGENTRY_Status_ID 4

    // 
    HBAScsiID ScsiId;
    #define MS_SMHBA_BINDINGENTRY_ScsiId_SIZE sizeof(HBAScsiID)
    #define MS_SMHBA_BINDINGENTRY_ScsiId_ID 5

} MS_SMHBA_BINDINGENTRY, *PMS_SMHBA_BINDINGENTRY;

#define MS_SMHBA_BINDINGENTRY_SIZE (FIELD_OFFSET(MS_SMHBA_BINDINGENTRY, ScsiId) + MS_SMHBA_BINDINGENTRY_ScsiId_SIZE)

// MS_SM_TargetInformationMethods - MS_SM_TargetInformationMethods
#define MS_SM_TargetInformationMethodsGuid \
    { 0x93545055,0xab4c,0x4e80, { 0x84,0xae,0x6a,0x86,0xa2,0xdc,0x4b,0x84 } }

#if ! (defined(MIDL_PASS))
DEFINE_GUID(MS_SM_TargetInformationMethods_GUID, \
            0x93545055,0xab4c,0x4e80,0x84,0xae,0x6a,0x86,0xa2,0xdc,0x4b,0x84);
#endif

//
// Method id definitions for MS_SM_TargetInformationMethods
#define SM_GetTargetMapping     1
typedef struct _SM_GetTargetMapping_IN
{
    // 
    UCHAR HbaPortWWN[8];
    #define SM_GetTargetMapping_IN_HbaPortWWN_SIZE sizeof(UCHAR[8])
    #define SM_GetTargetMapping_IN_HbaPortWWN_ID 1

    // 
    UCHAR DomainPortWWN[8];
    #define SM_GetTargetMapping_IN_DomainPortWWN_SIZE sizeof(UCHAR[8])
    #define SM_GetTargetMapping_IN_DomainPortWWN_ID 2

    // 
    ULONG InEntryCount;
    #define SM_GetTargetMapping_IN_InEntryCount_SIZE sizeof(ULONG)
    #define SM_GetTargetMapping_IN_InEntryCount_ID 3

} SM_GetTargetMapping_IN, *PSM_GetTargetMapping_IN;

#define SM_GetTargetMapping_IN_SIZE (FIELD_OFFSET(SM_GetTargetMapping_IN, InEntryCount) + SM_GetTargetMapping_IN_InEntryCount_SIZE)

typedef struct _SM_GetTargetMapping_OUT
{
    // 
    ULONG HBAStatus;
    #define SM_GetTargetMapping_OUT_HBAStatus_SIZE sizeof(ULONG)
    #define SM_GetTargetMapping_OUT_HBAStatus_ID 4

    // 
    ULONG TotalEntryCount;
    #define SM_GetTargetMapping_OUT_TotalEntryCount_SIZE sizeof(ULONG)
    #define SM_GetTargetMapping_OUT_TotalEntryCount_ID 5

    // 
    ULONG OutEntryCount;
    #define SM_GetTargetMapping_OUT_OutEntryCount_SIZE sizeof(ULONG)
    #define SM_GetTargetMapping_OUT_OutEntryCount_ID 6

    // 
    MS_SMHBA_SCSIENTRY Entry[1];
    #define SM_GetTargetMapping_OUT_Entry_ID 7

} SM_GetTargetMapping_OUT, *PSM_GetTargetMapping_OUT;

#define SM_GetBindingCapability     2
typedef struct _SM_GetBindingCapability_IN
{
    // 
    UCHAR HbaPortWWN[8];
    #define SM_GetBindingCapability_IN_HbaPortWWN_SIZE sizeof(UCHAR[8])
    #define SM_GetBindingCapability_IN_HbaPortWWN_ID 1

    // 
    UCHAR DomainPortWWN[8];
    #define SM_GetBindingCapability_IN_DomainPortWWN_SIZE sizeof(UCHAR[8])
    #define SM_GetBindingCapability_IN_DomainPortWWN_ID 2

} SM_GetBindingCapability_IN, *PSM_GetBindingCapability_IN;

#define SM_GetBindingCapability_IN_SIZE (FIELD_OFFSET(SM_GetBindingCapability_IN, DomainPortWWN) + SM_GetBindingCapability_IN_DomainPortWWN_SIZE)

typedef struct _SM_GetBindingCapability_OUT
{
    // 
    ULONG HBAStatus;
    #define SM_GetBindingCapability_OUT_HBAStatus_SIZE sizeof(ULONG)
    #define SM_GetBindingCapability_OUT_HBAStatus_ID 3

    // 
    ULONG Flags;
    #define SM_GetBindingCapability_OUT_Flags_SIZE sizeof(ULONG)
    #define SM_GetBindingCapability_OUT_Flags_ID 4

} SM_GetBindingCapability_OUT, *PSM_GetBindingCapability_OUT;

#define SM_GetBindingCapability_OUT_SIZE (FIELD_OFFSET(SM_GetBindingCapability_OUT, Flags) + SM_GetBindingCapability_OUT_Flags_SIZE)

#define SM_GetBindingSupport     3
typedef struct _SM_GetBindingSupport_IN
{
    // 
    UCHAR HbaPortWWN[8];
    #define SM_GetBindingSupport_IN_HbaPortWWN_SIZE sizeof(UCHAR[8])
    #define SM_GetBindingSupport_IN_HbaPortWWN_ID 1

    // 
    UCHAR DomainPortWWN[8];
    #define SM_GetBindingSupport_IN_DomainPortWWN_SIZE sizeof(UCHAR[8])
    #define SM_GetBindingSupport_IN_DomainPortWWN_ID 2

} SM_GetBindingSupport_IN, *PSM_GetBindingSupport_IN;

#define SM_GetBindingSupport_IN_SIZE (FIELD_OFFSET(SM_GetBindingSupport_IN, DomainPortWWN) + SM_GetBindingSupport_IN_DomainPortWWN_SIZE)

typedef struct _SM_GetBindingSupport_OUT
{
    // 
    ULONG HBAStatus;
    #define SM_GetBindingSupport_OUT_HBAStatus_SIZE sizeof(ULONG)
    #define SM_GetBindingSupport_OUT_HBAStatus_ID 3

    // 
    ULONG Flags;
    #define SM_GetBindingSupport_OUT_Flags_SIZE sizeof(ULONG)
    #define SM_GetBindingSupport_OUT_Flags_ID 4

} SM_GetBindingSupport_OUT, *PSM_GetBindingSupport_OUT;

#define SM_GetBindingSupport_OUT_SIZE (FIELD_OFFSET(SM_GetBindingSupport_OUT, Flags) + SM_GetBindingSupport_OUT_Flags_SIZE)

#define SM_SetBindingSupport     4
typedef struct _SM_SetBindingSupport_IN
{
    // 
    UCHAR HbaPortWWN[8];
    #define SM_SetBindingSupport_IN_HbaPortWWN_SIZE sizeof(UCHAR[8])
    #define SM_SetBindingSupport_IN_HbaPortWWN_ID 1

    // 
    UCHAR DomainPortWWN[8];
    #define SM_SetBindingSupport_IN_DomainPortWWN_SIZE sizeof(UCHAR[8])
    #define SM_SetBindingSupport_IN_DomainPortWWN_ID 2

    // 
    ULONG Flags;
    #define SM_SetBindingSupport_IN_Flags_SIZE sizeof(ULONG)
    #define SM_SetBindingSupport_IN_Flags_ID 3

} SM_SetBindingSupport_IN, *PSM_SetBindingSupport_IN;

#define SM_SetBindingSupport_IN_SIZE (FIELD_OFFSET(SM_SetBindingSupport_IN, Flags) + SM_SetBindingSupport_IN_Flags_SIZE)

typedef struct _SM_SetBindingSupport_OUT
{
    // 
    ULONG HBAStatus;
    #define SM_SetBindingSupport_OUT_HBAStatus_SIZE sizeof(ULONG)
    #define SM_SetBindingSupport_OUT_HBAStatus_ID 4

} SM_SetBindingSupport_OUT, *PSM_SetBindingSupport_OUT;

#define SM_SetBindingSupport_OUT_SIZE (FIELD_OFFSET(SM_SetBindingSupport_OUT, HBAStatus) + SM_SetBindingSupport_OUT_HBAStatus_SIZE)

#define SM_GetPersistentBinding     5
typedef struct _SM_GetPersistentBinding_IN
{
    // 
    UCHAR HbaPortWWN[8];
    #define SM_GetPersistentBinding_IN_HbaPortWWN_SIZE sizeof(UCHAR[8])
    #define SM_GetPersistentBinding_IN_HbaPortWWN_ID 1

    // 
    UCHAR DomainPortWWN[8];
    #define SM_GetPersistentBinding_IN_DomainPortWWN_SIZE sizeof(UCHAR[8])
    #define SM_GetPersistentBinding_IN_DomainPortWWN_ID 2

    // 
    ULONG InEntryCount;
    #define SM_GetPersistentBinding_IN_InEntryCount_SIZE sizeof(ULONG)
    #define SM_GetPersistentBinding_IN_InEntryCount_ID 3

} SM_GetPersistentBinding_IN, *PSM_GetPersistentBinding_IN;

#define SM_GetPersistentBinding_IN_SIZE (FIELD_OFFSET(SM_GetPersistentBinding_IN, InEntryCount) + SM_GetPersistentBinding_IN_InEntryCount_SIZE)

typedef struct _SM_GetPersistentBinding_OUT
{
    // 
    ULONG HBAStatus;
    #define SM_GetPersistentBinding_OUT_HBAStatus_SIZE sizeof(ULONG)
    #define SM_GetPersistentBinding_OUT_HBAStatus_ID 4

    // 
    ULONG TotalEntryCount;
    #define SM_GetPersistentBinding_OUT_TotalEntryCount_SIZE sizeof(ULONG)
    #define SM_GetPersistentBinding_OUT_TotalEntryCount_ID 5

    // 
    ULONG OutEntryCount;
    #define SM_GetPersistentBinding_OUT_OutEntryCount_SIZE sizeof(ULONG)
    #define SM_GetPersistentBinding_OUT_OutEntryCount_ID 6

    // 
    MS_SMHBA_BINDINGENTRY Entry[1];
    #define SM_GetPersistentBinding_OUT_Entry_ID 7

} SM_GetPersistentBinding_OUT, *PSM_GetPersistentBinding_OUT;

#define SM_SetPersistentBinding     6
typedef struct _SM_SetPersistentBinding_IN
{
    // 
    UCHAR HbaPortWWN[8];
    #define SM_SetPersistentBinding_IN_HbaPortWWN_SIZE sizeof(UCHAR[8])
    #define SM_SetPersistentBinding_IN_HbaPortWWN_ID 1

    // 
    UCHAR DomainPortWWN[8];
    #define SM_SetPersistentBinding_IN_DomainPortWWN_SIZE sizeof(UCHAR[8])
    #define SM_SetPersistentBinding_IN_DomainPortWWN_ID 2

    // 
    ULONG InEntryCount;
    #define SM_SetPersistentBinding_IN_InEntryCount_SIZE sizeof(ULONG)
    #define SM_SetPersistentBinding_IN_InEntryCount_ID 3

    // 
    MS_SMHBA_BINDINGENTRY Entry[1];
    #define SM_SetPersistentBinding_IN_Entry_ID 4

} SM_SetPersistentBinding_IN, *PSM_SetPersistentBinding_IN;

typedef struct _SM_SetPersistentBinding_OUT
{
    // 
    ULONG HBAStatus;
    #define SM_SetPersistentBinding_OUT_HBAStatus_SIZE sizeof(ULONG)
    #define SM_SetPersistentBinding_OUT_HBAStatus_ID 5

    // 
    ULONG OutStatusCount;
    #define SM_SetPersistentBinding_OUT_OutStatusCount_SIZE sizeof(ULONG)
    #define SM_SetPersistentBinding_OUT_OutStatusCount_ID 6

    // 
    ULONG EntryStatus[1];
    #define SM_SetPersistentBinding_OUT_EntryStatus_ID 7

} SM_SetPersistentBinding_OUT, *PSM_SetPersistentBinding_OUT;

#define SM_RemovePersistentBinding     7
typedef struct _SM_RemovePersistentBinding_IN
{
    // 
    UCHAR HbaPortWWN[8];
    #define SM_RemovePersistentBinding_IN_HbaPortWWN_SIZE sizeof(UCHAR[8])
    #define SM_RemovePersistentBinding_IN_HbaPortWWN_ID 1

    // 
    UCHAR DomainPortWWN[8];
    #define SM_RemovePersistentBinding_IN_DomainPortWWN_SIZE sizeof(UCHAR[8])
    #define SM_RemovePersistentBinding_IN_DomainPortWWN_ID 2

    // 
    ULONG EntryCount;
    #define SM_RemovePersistentBinding_IN_EntryCount_SIZE sizeof(ULONG)
    #define SM_RemovePersistentBinding_IN_EntryCount_ID 3

    // 
    MS_SMHBA_BINDINGENTRY Entry[1];
    #define SM_RemovePersistentBinding_IN_Entry_ID 4

} SM_RemovePersistentBinding_IN, *PSM_RemovePersistentBinding_IN;

typedef struct _SM_RemovePersistentBinding_OUT
{
    // 
    ULONG HBAStatus;
    #define SM_RemovePersistentBinding_OUT_HBAStatus_SIZE sizeof(ULONG)
    #define SM_RemovePersistentBinding_OUT_HBAStatus_ID 5

} SM_RemovePersistentBinding_OUT, *PSM_RemovePersistentBinding_OUT;

#define SM_RemovePersistentBinding_OUT_SIZE (FIELD_OFFSET(SM_RemovePersistentBinding_OUT, HBAStatus) + SM_RemovePersistentBinding_OUT_HBAStatus_SIZE)

#define SM_GetLUNStatistics     8
typedef struct _SM_GetLUNStatistics_IN
{
    // 
    HBAScsiID Lunit;
    #define SM_GetLUNStatistics_IN_Lunit_SIZE sizeof(HBAScsiID)
    #define SM_GetLUNStatistics_IN_Lunit_ID 1

} SM_GetLUNStatistics_IN, *PSM_GetLUNStatistics_IN;

#define SM_GetLUNStatistics_IN_SIZE (FIELD_OFFSET(SM_GetLUNStatistics_IN, Lunit) + SM_GetLUNStatistics_IN_Lunit_SIZE)

typedef struct _SM_GetLUNStatistics_OUT
{
    // 
    ULONG HBAStatus;
    #define SM_GetLUNStatistics_OUT_HBAStatus_SIZE sizeof(ULONG)
    #define SM_GetLUNStatistics_OUT_HBAStatus_ID 2

    // 
    MS_SMHBA_PROTOCOLSTATISTICS ProtocolStatistics;
    #define SM_GetLUNStatistics_OUT_ProtocolStatistics_SIZE sizeof(MS_SMHBA_PROTOCOLSTATISTICS)
    #define SM_GetLUNStatistics_OUT_ProtocolStatistics_ID 3

} SM_GetLUNStatistics_OUT, *PSM_GetLUNStatistics_OUT;

#define SM_GetLUNStatistics_OUT_SIZE (FIELD_OFFSET(SM_GetLUNStatistics_OUT, ProtocolStatistics) + SM_GetLUNStatistics_OUT_ProtocolStatistics_SIZE)


// MS_SM_ScsiInformationMethods - MS_SM_ScsiInformationMethods
#define MS_SM_ScsiInformationMethodsGuid \
    { 0xb6661e6f,0x075e,0x4209, { 0xae,0x20,0xfe,0x81,0xdb,0x03,0xd9,0x79 } }

#if ! (defined(MIDL_PASS))
DEFINE_GUID(MS_SM_ScsiInformationMethods_GUID, \
            0xb6661e6f,0x075e,0x4209,0xae,0x20,0xfe,0x81,0xdb,0x03,0xd9,0x79);
#endif

//
// Method id definitions for MS_SM_ScsiInformationMethods
#define SM_ScsiInquiry     1
typedef struct _SM_ScsiInquiry_IN
{
    // 
    UCHAR HbaPortWWN[8];
    #define SM_ScsiInquiry_IN_HbaPortWWN_SIZE sizeof(UCHAR[8])
    #define SM_ScsiInquiry_IN_HbaPortWWN_ID 1

    // 
    UCHAR DiscoveredPortWWN[8];
    #define SM_ScsiInquiry_IN_DiscoveredPortWWN_SIZE sizeof(UCHAR[8])
    #define SM_ScsiInquiry_IN_DiscoveredPortWWN_ID 2

    // 
    UCHAR DomainPortWWN[8];
    #define SM_ScsiInquiry_IN_DomainPortWWN_SIZE sizeof(UCHAR[8])
    #define SM_ScsiInquiry_IN_DomainPortWWN_ID 3

    // 
    ULONGLONG SmhbaLUN;
    #define SM_ScsiInquiry_IN_SmhbaLUN_SIZE sizeof(ULONGLONG)
    #define SM_ScsiInquiry_IN_SmhbaLUN_ID 4

    // 
    UCHAR Cdb[6];
    #define SM_ScsiInquiry_IN_Cdb_SIZE sizeof(UCHAR[6])
    #define SM_ScsiInquiry_IN_Cdb_ID 5

    // 
    ULONG InRespBufferMaxSize;
    #define SM_ScsiInquiry_IN_InRespBufferMaxSize_SIZE sizeof(ULONG)
    #define SM_ScsiInquiry_IN_InRespBufferMaxSize_ID 6

    // 
    ULONG InSenseBufferMaxSize;
    #define SM_ScsiInquiry_IN_InSenseBufferMaxSize_SIZE sizeof(ULONG)
    #define SM_ScsiInquiry_IN_InSenseBufferMaxSize_ID 7

} SM_ScsiInquiry_IN, *PSM_ScsiInquiry_IN;

#define SM_ScsiInquiry_IN_SIZE (FIELD_OFFSET(SM_ScsiInquiry_IN, InSenseBufferMaxSize) + SM_ScsiInquiry_IN_InSenseBufferMaxSize_SIZE)

typedef struct _SM_ScsiInquiry_OUT
{
    // 
    ULONG HBAStatus;
    #define SM_ScsiInquiry_OUT_HBAStatus_SIZE sizeof(ULONG)
    #define SM_ScsiInquiry_OUT_HBAStatus_ID 8

    // 
    UCHAR ScsiStatus;
    #define SM_ScsiInquiry_OUT_ScsiStatus_SIZE sizeof(UCHAR)
    #define SM_ScsiInquiry_OUT_ScsiStatus_ID 9

    // 
    ULONG OutRespBufferSize;
    #define SM_ScsiInquiry_OUT_OutRespBufferSize_SIZE sizeof(ULONG)
    #define SM_ScsiInquiry_OUT_OutRespBufferSize_ID 10

    // 
    ULONG OutSenseBufferSize;
    #define SM_ScsiInquiry_OUT_OutSenseBufferSize_SIZE sizeof(ULONG)
    #define SM_ScsiInquiry_OUT_OutSenseBufferSize_ID 11

    // 
    UCHAR RespBuffer[1];
    #define SM_ScsiInquiry_OUT_RespBuffer_ID 12

    // 
//  UCHAR SenseBuffer[1];
    #define SM_ScsiInquiry_OUT_SenseBuffer_ID 13

} SM_ScsiInquiry_OUT, *PSM_ScsiInquiry_OUT;

#define SM_ScsiReportLuns     2
typedef struct _SM_ScsiReportLuns_IN
{
    // 
    UCHAR HbaPortWWN[8];
    #define SM_ScsiReportLuns_IN_HbaPortWWN_SIZE sizeof(UCHAR[8])
    #define SM_ScsiReportLuns_IN_HbaPortWWN_ID 1

    // 
    UCHAR DiscoveredPortWWN[8];
    #define SM_ScsiReportLuns_IN_DiscoveredPortWWN_SIZE sizeof(UCHAR[8])
    #define SM_ScsiReportLuns_IN_DiscoveredPortWWN_ID 2

    // 
    UCHAR DomainPortWWN[8];
    #define SM_ScsiReportLuns_IN_DomainPortWWN_SIZE sizeof(UCHAR[8])
    #define SM_ScsiReportLuns_IN_DomainPortWWN_ID 3

    // 
    UCHAR Cdb[12];
    #define SM_ScsiReportLuns_IN_Cdb_SIZE sizeof(UCHAR[12])
    #define SM_ScsiReportLuns_IN_Cdb_ID 4

    // 
    ULONG InRespBufferMaxSize;
    #define SM_ScsiReportLuns_IN_InRespBufferMaxSize_SIZE sizeof(ULONG)
    #define SM_ScsiReportLuns_IN_InRespBufferMaxSize_ID 5

    // 
    ULONG InSenseBufferMaxSize;
    #define SM_ScsiReportLuns_IN_InSenseBufferMaxSize_SIZE sizeof(ULONG)
    #define SM_ScsiReportLuns_IN_InSenseBufferMaxSize_ID 6

} SM_ScsiReportLuns_IN, *PSM_ScsiReportLuns_IN;

#define SM_ScsiReportLuns_IN_SIZE (FIELD_OFFSET(SM_ScsiReportLuns_IN, InSenseBufferMaxSize) + SM_ScsiReportLuns_IN_InSenseBufferMaxSize_SIZE)

typedef struct _SM_ScsiReportLuns_OUT
{
    // 
    ULONG HBAStatus;
    #define SM_ScsiReportLuns_OUT_HBAStatus_SIZE sizeof(ULONG)
    #define SM_ScsiReportLuns_OUT_HBAStatus_ID 7

    // 
    UCHAR ScsiStatus;
    #define SM_ScsiReportLuns_OUT_ScsiStatus_SIZE sizeof(UCHAR)
    #define SM_ScsiReportLuns_OUT_ScsiStatus_ID 8

    // 
    ULONG TotalRespBufferSize;
    #define SM_ScsiReportLuns_OUT_TotalRespBufferSize_SIZE sizeof(ULONG)
    #define SM_ScsiReportLuns_OUT_TotalRespBufferSize_ID 9

    // 
    ULONG OutRespBufferSize;
    #define SM_ScsiReportLuns_OUT_OutRespBufferSize_SIZE sizeof(ULONG)
    #define SM_ScsiReportLuns_OUT_OutRespBufferSize_ID 10

    // 
    ULONG OutSenseBufferSize;
    #define SM_ScsiReportLuns_OUT_OutSenseBufferSize_SIZE sizeof(ULONG)
    #define SM_ScsiReportLuns_OUT_OutSenseBufferSize_ID 11

    // 
    UCHAR RespBuffer[1];
    #define SM_ScsiReportLuns_OUT_RespBuffer_ID 12

    // 
//  UCHAR SenseBuffer[1];
    #define SM_ScsiReportLuns_OUT_SenseBuffer_ID 13

} SM_ScsiReportLuns_OUT, *PSM_ScsiReportLuns_OUT;

#define SM_ScsiReadCapacity     3
typedef struct _SM_ScsiReadCapacity_IN
{
    // 
    UCHAR HbaPortWWN[8];
    #define SM_ScsiReadCapacity_IN_HbaPortWWN_SIZE sizeof(UCHAR[8])
    #define SM_ScsiReadCapacity_IN_HbaPortWWN_ID 1

    // 
    UCHAR DiscoveredPortWWN[8];
    #define SM_ScsiReadCapacity_IN_DiscoveredPortWWN_SIZE sizeof(UCHAR[8])
    #define SM_ScsiReadCapacity_IN_DiscoveredPortWWN_ID 2

    // 
    UCHAR DomainPortWWN[8];
    #define SM_ScsiReadCapacity_IN_DomainPortWWN_SIZE sizeof(UCHAR[8])
    #define SM_ScsiReadCapacity_IN_DomainPortWWN_ID 3

    // 
    ULONGLONG SmhbaLUN;
    #define SM_ScsiReadCapacity_IN_SmhbaLUN_SIZE sizeof(ULONGLONG)
    #define SM_ScsiReadCapacity_IN_SmhbaLUN_ID 4

    // 
    UCHAR Cdb[16];
    #define SM_ScsiReadCapacity_IN_Cdb_SIZE sizeof(UCHAR[16])
    #define SM_ScsiReadCapacity_IN_Cdb_ID 5

    // 
    ULONG InRespBufferMaxSize;
    #define SM_ScsiReadCapacity_IN_InRespBufferMaxSize_SIZE sizeof(ULONG)
    #define SM_ScsiReadCapacity_IN_InRespBufferMaxSize_ID 6

    // 
    ULONG InSenseBufferMaxSize;
    #define SM_ScsiReadCapacity_IN_InSenseBufferMaxSize_SIZE sizeof(ULONG)
    #define SM_ScsiReadCapacity_IN_InSenseBufferMaxSize_ID 7

} SM_ScsiReadCapacity_IN, *PSM_ScsiReadCapacity_IN;

#define SM_ScsiReadCapacity_IN_SIZE (FIELD_OFFSET(SM_ScsiReadCapacity_IN, InSenseBufferMaxSize) + SM_ScsiReadCapacity_IN_InSenseBufferMaxSize_SIZE)

typedef struct _SM_ScsiReadCapacity_OUT
{
    // 
    ULONG HBAStatus;
    #define SM_ScsiReadCapacity_OUT_HBAStatus_SIZE sizeof(ULONG)
    #define SM_ScsiReadCapacity_OUT_HBAStatus_ID 8

    // 
    UCHAR ScsiStatus;
    #define SM_ScsiReadCapacity_OUT_ScsiStatus_SIZE sizeof(UCHAR)
    #define SM_ScsiReadCapacity_OUT_ScsiStatus_ID 9

    // 
    ULONG OutRespBufferSize;
    #define SM_ScsiReadCapacity_OUT_OutRespBufferSize_SIZE sizeof(ULONG)
    #define SM_ScsiReadCapacity_OUT_OutRespBufferSize_ID 10

    // 
    ULONG OutSenseBufferSize;
    #define SM_ScsiReadCapacity_OUT_OutSenseBufferSize_SIZE sizeof(ULONG)
    #define SM_ScsiReadCapacity_OUT_OutSenseBufferSize_ID 11

    // 
    UCHAR RespBuffer[1];
    #define SM_ScsiReadCapacity_OUT_RespBuffer_ID 12

    // 
//  UCHAR SenseBuffer[1];
    #define SM_ScsiReadCapacity_OUT_SenseBuffer_ID 13

} SM_ScsiReadCapacity_OUT, *PSM_ScsiReadCapacity_OUT;


// MS_SM_FabricAndDomainManagementMethods - MS_SM_FabricAndDomainManagementMethods
#define MS_SM_FabricAndDomainManagementMethodsGuid \
    { 0x467fea10,0x701b,0x4388, { 0x91,0x7f,0x73,0x06,0x20,0xce,0xa3,0x28 } }

#if ! (defined(MIDL_PASS))
DEFINE_GUID(MS_SM_FabricAndDomainManagementMethods_GUID, \
            0x467fea10,0x701b,0x4388,0x91,0x7f,0x73,0x06,0x20,0xce,0xa3,0x28);
#endif

//
// Method id definitions for MS_SM_FabricAndDomainManagementMethods
#define SM_SendTEST     1
typedef struct _SM_SendTEST_IN
{
    // 
    UCHAR HbaPortWWN[8];
    #define SM_SendTEST_IN_HbaPortWWN_SIZE sizeof(UCHAR[8])
    #define SM_SendTEST_IN_HbaPortWWN_ID 1

    // 
    UCHAR DestWWN[8];
    #define SM_SendTEST_IN_DestWWN_SIZE sizeof(UCHAR[8])
    #define SM_SendTEST_IN_DestWWN_ID 2

    // 
    ULONG DestFCID;
    #define SM_SendTEST_IN_DestFCID_SIZE sizeof(ULONG)
    #define SM_SendTEST_IN_DestFCID_ID 3

    // 
    ULONG ReqBufferSize;
    #define SM_SendTEST_IN_ReqBufferSize_SIZE sizeof(ULONG)
    #define SM_SendTEST_IN_ReqBufferSize_ID 4

    // 
    UCHAR ReqBuffer[1];
    #define SM_SendTEST_IN_ReqBuffer_ID 5

} SM_SendTEST_IN, *PSM_SendTEST_IN;

typedef struct _SM_SendTEST_OUT
{
    // 
    ULONG HBAStatus;
    #define SM_SendTEST_OUT_HBAStatus_SIZE sizeof(ULONG)
    #define SM_SendTEST_OUT_HBAStatus_ID 6

} SM_SendTEST_OUT, *PSM_SendTEST_OUT;

#define SM_SendTEST_OUT_SIZE (FIELD_OFFSET(SM_SendTEST_OUT, HBAStatus) + SM_SendTEST_OUT_HBAStatus_SIZE)

#define SM_SendECHO     2
typedef struct _SM_SendECHO_IN
{
    // 
    UCHAR HbaPortWWN[8];
    #define SM_SendECHO_IN_HbaPortWWN_SIZE sizeof(UCHAR[8])
    #define SM_SendECHO_IN_HbaPortWWN_ID 1

    // 
    UCHAR DestWWN[8];
    #define SM_SendECHO_IN_DestWWN_SIZE sizeof(UCHAR[8])
    #define SM_SendECHO_IN_DestWWN_ID 2

    // 
    ULONG DestFCID;
    #define SM_SendECHO_IN_DestFCID_SIZE sizeof(ULONG)
    #define SM_SendECHO_IN_DestFCID_ID 3

    // 
    ULONG InRespBufferMaxSize;
    #define SM_SendECHO_IN_InRespBufferMaxSize_SIZE sizeof(ULONG)
    #define SM_SendECHO_IN_InRespBufferMaxSize_ID 4

    // 
    ULONG ReqBufferSize;
    #define SM_SendECHO_IN_ReqBufferSize_SIZE sizeof(ULONG)
    #define SM_SendECHO_IN_ReqBufferSize_ID 5

    // 
    UCHAR ReqBuffer[1];
    #define SM_SendECHO_IN_ReqBuffer_ID 6

} SM_SendECHO_IN, *PSM_SendECHO_IN;

typedef struct _SM_SendECHO_OUT
{
    // 
    ULONG HBAStatus;
    #define SM_SendECHO_OUT_HBAStatus_SIZE sizeof(ULONG)
    #define SM_SendECHO_OUT_HBAStatus_ID 7

    // 
    ULONG OutRespBufferSize;
    #define SM_SendECHO_OUT_OutRespBufferSize_SIZE sizeof(ULONG)
    #define SM_SendECHO_OUT_OutRespBufferSize_ID 8

    // 
    UCHAR RespBuffer[1];
    #define SM_SendECHO_OUT_RespBuffer_ID 9

} SM_SendECHO_OUT, *PSM_SendECHO_OUT;

#define SM_SendSMPPassThru     3
typedef struct _SM_SendSMPPassThru_IN
{
    // 
    UCHAR HbaPortWWN[8];
    #define SM_SendSMPPassThru_IN_HbaPortWWN_SIZE sizeof(UCHAR[8])
    #define SM_SendSMPPassThru_IN_HbaPortWWN_ID 1

    // 
    UCHAR DestPortWWN[8];
    #define SM_SendSMPPassThru_IN_DestPortWWN_SIZE sizeof(UCHAR[8])
    #define SM_SendSMPPassThru_IN_DestPortWWN_ID 2

    // 
    UCHAR DomainPortWWN[8];
    #define SM_SendSMPPassThru_IN_DomainPortWWN_SIZE sizeof(UCHAR[8])
    #define SM_SendSMPPassThru_IN_DomainPortWWN_ID 3

    // 
    ULONG InRespBufferMaxSize;
    #define SM_SendSMPPassThru_IN_InRespBufferMaxSize_SIZE sizeof(ULONG)
    #define SM_SendSMPPassThru_IN_InRespBufferMaxSize_ID 4

    // 
    ULONG ReqBufferSize;
    #define SM_SendSMPPassThru_IN_ReqBufferSize_SIZE sizeof(ULONG)
    #define SM_SendSMPPassThru_IN_ReqBufferSize_ID 5

    // 
    UCHAR ReqBuffer[1];
    #define SM_SendSMPPassThru_IN_ReqBuffer_ID 6

} SM_SendSMPPassThru_IN, *PSM_SendSMPPassThru_IN;

typedef struct _SM_SendSMPPassThru_OUT
{
    // 
    ULONG HBAStatus;
    #define SM_SendSMPPassThru_OUT_HBAStatus_SIZE sizeof(ULONG)
    #define SM_SendSMPPassThru_OUT_HBAStatus_ID 7

    // 
    ULONG TotalRespBufferSize;
    #define SM_SendSMPPassThru_OUT_TotalRespBufferSize_SIZE sizeof(ULONG)
    #define SM_SendSMPPassThru_OUT_TotalRespBufferSize_ID 8

    // 
    ULONG OutRespBufferSize;
    #define SM_SendSMPPassThru_OUT_OutRespBufferSize_SIZE sizeof(ULONG)
    #define SM_SendSMPPassThru_OUT_OutRespBufferSize_ID 9

    // 
    UCHAR RespBuffer[1];
    #define SM_SendSMPPassThru_OUT_RespBuffer_ID 10

} SM_SendSMPPassThru_OUT, *PSM_SendSMPPassThru_OUT;

#define SM_SendCTPassThru     10
typedef struct _SM_SendCTPassThru_IN
{
    // 
    UCHAR HbaPortWWN[8];
    #define SM_SendCTPassThru_IN_HbaPortWWN_SIZE sizeof(UCHAR[8])
    #define SM_SendCTPassThru_IN_HbaPortWWN_ID 1

    // 
    ULONG InRespBufferMaxSize;
    #define SM_SendCTPassThru_IN_InRespBufferMaxSize_SIZE sizeof(ULONG)
    #define SM_SendCTPassThru_IN_InRespBufferMaxSize_ID 2

    // 
    ULONG ReqBufferSize;
    #define SM_SendCTPassThru_IN_ReqBufferSize_SIZE sizeof(ULONG)
    #define SM_SendCTPassThru_IN_ReqBufferSize_ID 3

    // 
    UCHAR ReqBuffer[1];
    #define SM_SendCTPassThru_IN_ReqBuffer_ID 4

} SM_SendCTPassThru_IN, *PSM_SendCTPassThru_IN;

typedef struct _SM_SendCTPassThru_OUT
{
    // 
    ULONG HBAStatus;
    #define SM_SendCTPassThru_OUT_HBAStatus_SIZE sizeof(ULONG)
    #define SM_SendCTPassThru_OUT_HBAStatus_ID 5

    // 
    ULONG TotalRespBufferSize;
    #define SM_SendCTPassThru_OUT_TotalRespBufferSize_SIZE sizeof(ULONG)
    #define SM_SendCTPassThru_OUT_TotalRespBufferSize_ID 6

    // 
    ULONG OutRespBufferSize;
    #define SM_SendCTPassThru_OUT_OutRespBufferSize_SIZE sizeof(ULONG)
    #define SM_SendCTPassThru_OUT_OutRespBufferSize_ID 7

    // 
    UCHAR RespBuffer[1];
    #define SM_SendCTPassThru_OUT_RespBuffer_ID 8

} SM_SendCTPassThru_OUT, *PSM_SendCTPassThru_OUT;

#define SM_GetRNIDMgmtInfo     11
typedef struct _SM_GetRNIDMgmtInfo_OUT
{
    // 
    ULONG HBAStatus;
    #define SM_GetRNIDMgmtInfo_OUT_HBAStatus_SIZE sizeof(ULONG)
    #define SM_GetRNIDMgmtInfo_OUT_HBAStatus_ID 1

    // 
    HBAFC3MgmtInfo MgmtInfo;
    #define SM_GetRNIDMgmtInfo_OUT_MgmtInfo_SIZE sizeof(HBAFC3MgmtInfo)
    #define SM_GetRNIDMgmtInfo_OUT_MgmtInfo_ID 2

} SM_GetRNIDMgmtInfo_OUT, *PSM_GetRNIDMgmtInfo_OUT;

#define SM_GetRNIDMgmtInfo_OUT_SIZE (FIELD_OFFSET(SM_GetRNIDMgmtInfo_OUT, MgmtInfo) + SM_GetRNIDMgmtInfo_OUT_MgmtInfo_SIZE)

#define SM_SetRNIDMgmtInfo     12
typedef struct _SM_SetRNIDMgmtInfo_IN
{
    // 
    HBAFC3MgmtInfo MgmtInfo;
    #define SM_SetRNIDMgmtInfo_IN_MgmtInfo_SIZE sizeof(HBAFC3MgmtInfo)
    #define SM_SetRNIDMgmtInfo_IN_MgmtInfo_ID 1

} SM_SetRNIDMgmtInfo_IN, *PSM_SetRNIDMgmtInfo_IN;

#define SM_SetRNIDMgmtInfo_IN_SIZE (FIELD_OFFSET(SM_SetRNIDMgmtInfo_IN, MgmtInfo) + SM_SetRNIDMgmtInfo_IN_MgmtInfo_SIZE)

typedef struct _SM_SetRNIDMgmtInfo_OUT
{
    // 
    ULONG HBAStatus;
    #define SM_SetRNIDMgmtInfo_OUT_HBAStatus_SIZE sizeof(ULONG)
    #define SM_SetRNIDMgmtInfo_OUT_HBAStatus_ID 2

} SM_SetRNIDMgmtInfo_OUT, *PSM_SetRNIDMgmtInfo_OUT;

#define SM_SetRNIDMgmtInfo_OUT_SIZE (FIELD_OFFSET(SM_SetRNIDMgmtInfo_OUT, HBAStatus) + SM_SetRNIDMgmtInfo_OUT_HBAStatus_SIZE)

#define SM_SendRNID     13
typedef struct _SM_SendRNID_IN
{
    // 
    UCHAR HbaPortWWN[8];
    #define SM_SendRNID_IN_HbaPortWWN_SIZE sizeof(UCHAR[8])
    #define SM_SendRNID_IN_HbaPortWWN_ID 1

    // 
    UCHAR DestWWN[8];
    #define SM_SendRNID_IN_DestWWN_SIZE sizeof(UCHAR[8])
    #define SM_SendRNID_IN_DestWWN_ID 2

    // 
    ULONG DestFCID;
    #define SM_SendRNID_IN_DestFCID_SIZE sizeof(ULONG)
    #define SM_SendRNID_IN_DestFCID_ID 3

    // 
    ULONG NodeIdDataFormat;
    #define SM_SendRNID_IN_NodeIdDataFormat_SIZE sizeof(ULONG)
    #define SM_SendRNID_IN_NodeIdDataFormat_ID 4

    // 
    ULONG InRespBufferMaxSize;
    #define SM_SendRNID_IN_InRespBufferMaxSize_SIZE sizeof(ULONG)
    #define SM_SendRNID_IN_InRespBufferMaxSize_ID 5

} SM_SendRNID_IN, *PSM_SendRNID_IN;

#define SM_SendRNID_IN_SIZE (FIELD_OFFSET(SM_SendRNID_IN, InRespBufferMaxSize) + SM_SendRNID_IN_InRespBufferMaxSize_SIZE)

typedef struct _SM_SendRNID_OUT
{
    // 
    ULONG HBAStatus;
    #define SM_SendRNID_OUT_HBAStatus_SIZE sizeof(ULONG)
    #define SM_SendRNID_OUT_HBAStatus_ID 6

    // 
    ULONG TotalRespBufferSize;
    #define SM_SendRNID_OUT_TotalRespBufferSize_SIZE sizeof(ULONG)
    #define SM_SendRNID_OUT_TotalRespBufferSize_ID 7

    // 
    ULONG OutRespBufferSize;
    #define SM_SendRNID_OUT_OutRespBufferSize_SIZE sizeof(ULONG)
    #define SM_SendRNID_OUT_OutRespBufferSize_ID 8

    // 
    UCHAR RespBuffer[1];
    #define SM_SendRNID_OUT_RespBuffer_ID 9

} SM_SendRNID_OUT, *PSM_SendRNID_OUT;

#define SM_SendRPL     14
typedef struct _SM_SendRPL_IN
{
    // 
    UCHAR HbaPortWWN[8];
    #define SM_SendRPL_IN_HbaPortWWN_SIZE sizeof(UCHAR[8])
    #define SM_SendRPL_IN_HbaPortWWN_ID 1

    // 
    UCHAR AgentWWN[8];
    #define SM_SendRPL_IN_AgentWWN_SIZE sizeof(UCHAR[8])
    #define SM_SendRPL_IN_AgentWWN_ID 2

    // 
    ULONG AgentDomain;
    #define SM_SendRPL_IN_AgentDomain_SIZE sizeof(ULONG)
    #define SM_SendRPL_IN_AgentDomain_ID 3

    // 
    ULONG PortIndex;
    #define SM_SendRPL_IN_PortIndex_SIZE sizeof(ULONG)
    #define SM_SendRPL_IN_PortIndex_ID 4

    // 
    ULONG InRespBufferMaxSize;
    #define SM_SendRPL_IN_InRespBufferMaxSize_SIZE sizeof(ULONG)
    #define SM_SendRPL_IN_InRespBufferMaxSize_ID 5

} SM_SendRPL_IN, *PSM_SendRPL_IN;

#define SM_SendRPL_IN_SIZE (FIELD_OFFSET(SM_SendRPL_IN, InRespBufferMaxSize) + SM_SendRPL_IN_InRespBufferMaxSize_SIZE)

typedef struct _SM_SendRPL_OUT
{
    // 
    ULONG HBAStatus;
    #define SM_SendRPL_OUT_HBAStatus_SIZE sizeof(ULONG)
    #define SM_SendRPL_OUT_HBAStatus_ID 6

    // 
    ULONG TotalRespBufferSize;
    #define SM_SendRPL_OUT_TotalRespBufferSize_SIZE sizeof(ULONG)
    #define SM_SendRPL_OUT_TotalRespBufferSize_ID 7

    // 
    ULONG OutRespBufferSize;
    #define SM_SendRPL_OUT_OutRespBufferSize_SIZE sizeof(ULONG)
    #define SM_SendRPL_OUT_OutRespBufferSize_ID 8

    // 
    UCHAR RespBuffer[1];
    #define SM_SendRPL_OUT_RespBuffer_ID 9

} SM_SendRPL_OUT, *PSM_SendRPL_OUT;

#define SM_SendRPS     15
typedef struct _SM_SendRPS_IN
{
    // 
    UCHAR HbaPortWWN[8];
    #define SM_SendRPS_IN_HbaPortWWN_SIZE sizeof(UCHAR[8])
    #define SM_SendRPS_IN_HbaPortWWN_ID 1

    // 
    UCHAR AgentWWN[8];
    #define SM_SendRPS_IN_AgentWWN_SIZE sizeof(UCHAR[8])
    #define SM_SendRPS_IN_AgentWWN_ID 2

    // 
    UCHAR ObjectWWN[8];
    #define SM_SendRPS_IN_ObjectWWN_SIZE sizeof(UCHAR[8])
    #define SM_SendRPS_IN_ObjectWWN_ID 3

    // 
    ULONG AgentDomain;
    #define SM_SendRPS_IN_AgentDomain_SIZE sizeof(ULONG)
    #define SM_SendRPS_IN_AgentDomain_ID 4

    // 
    ULONG ObjectPortNumber;
    #define SM_SendRPS_IN_ObjectPortNumber_SIZE sizeof(ULONG)
    #define SM_SendRPS_IN_ObjectPortNumber_ID 5

    // 
    ULONG InRespBufferMaxSize;
    #define SM_SendRPS_IN_InRespBufferMaxSize_SIZE sizeof(ULONG)
    #define SM_SendRPS_IN_InRespBufferMaxSize_ID 6

} SM_SendRPS_IN, *PSM_SendRPS_IN;

#define SM_SendRPS_IN_SIZE (FIELD_OFFSET(SM_SendRPS_IN, InRespBufferMaxSize) + SM_SendRPS_IN_InRespBufferMaxSize_SIZE)

typedef struct _SM_SendRPS_OUT
{
    // 
    ULONG HBAStatus;
    #define SM_SendRPS_OUT_HBAStatus_SIZE sizeof(ULONG)
    #define SM_SendRPS_OUT_HBAStatus_ID 7

    // 
    ULONG TotalRespBufferSize;
    #define SM_SendRPS_OUT_TotalRespBufferSize_SIZE sizeof(ULONG)
    #define SM_SendRPS_OUT_TotalRespBufferSize_ID 8

    // 
    ULONG OutRespBufferSize;
    #define SM_SendRPS_OUT_OutRespBufferSize_SIZE sizeof(ULONG)
    #define SM_SendRPS_OUT_OutRespBufferSize_ID 9

    // 
    UCHAR RespBuffer[1];
    #define SM_SendRPS_OUT_RespBuffer_ID 10

} SM_SendRPS_OUT, *PSM_SendRPS_OUT;

#define SM_SendSRL     16
typedef struct _SM_SendSRL_IN
{
    // 
    UCHAR HbaPortWWN[8];
    #define SM_SendSRL_IN_HbaPortWWN_SIZE sizeof(UCHAR[8])
    #define SM_SendSRL_IN_HbaPortWWN_ID 1

    // 
    UCHAR WWN[8];
    #define SM_SendSRL_IN_WWN_SIZE sizeof(UCHAR[8])
    #define SM_SendSRL_IN_WWN_ID 2

    // 
    ULONG Domain;
    #define SM_SendSRL_IN_Domain_SIZE sizeof(ULONG)
    #define SM_SendSRL_IN_Domain_ID 3

    // 
    ULONG InRespBufferMaxSize;
    #define SM_SendSRL_IN_InRespBufferMaxSize_SIZE sizeof(ULONG)
    #define SM_SendSRL_IN_InRespBufferMaxSize_ID 4

} SM_SendSRL_IN, *PSM_SendSRL_IN;

#define SM_SendSRL_IN_SIZE (FIELD_OFFSET(SM_SendSRL_IN, InRespBufferMaxSize) + SM_SendSRL_IN_InRespBufferMaxSize_SIZE)

typedef struct _SM_SendSRL_OUT
{
    // 
    ULONG HBAStatus;
    #define SM_SendSRL_OUT_HBAStatus_SIZE sizeof(ULONG)
    #define SM_SendSRL_OUT_HBAStatus_ID 5

    // 
    ULONG TotalRespBufferSize;
    #define SM_SendSRL_OUT_TotalRespBufferSize_SIZE sizeof(ULONG)
    #define SM_SendSRL_OUT_TotalRespBufferSize_ID 6

    // 
    ULONG OutRespBufferSize;
    #define SM_SendSRL_OUT_OutRespBufferSize_SIZE sizeof(ULONG)
    #define SM_SendSRL_OUT_OutRespBufferSize_ID 7

    // 
    UCHAR RespBuffer[1];
    #define SM_SendSRL_OUT_RespBuffer_ID 8

} SM_SendSRL_OUT, *PSM_SendSRL_OUT;

#define SM_SendLIRR     17
typedef struct _SM_SendLIRR_IN
{
    // 
    UCHAR SourceWWN[8];
    #define SM_SendLIRR_IN_SourceWWN_SIZE sizeof(UCHAR[8])
    #define SM_SendLIRR_IN_SourceWWN_ID 1

    // 
    UCHAR DestWWN[8];
    #define SM_SendLIRR_IN_DestWWN_SIZE sizeof(UCHAR[8])
    #define SM_SendLIRR_IN_DestWWN_ID 2

    // 
    UCHAR Function;
    #define SM_SendLIRR_IN_Function_SIZE sizeof(UCHAR)
    #define SM_SendLIRR_IN_Function_ID 3

    // 
    UCHAR Type;
    #define SM_SendLIRR_IN_Type_SIZE sizeof(UCHAR)
    #define SM_SendLIRR_IN_Type_ID 4

    // 
    ULONG InRespBufferMaxSize;
    #define SM_SendLIRR_IN_InRespBufferMaxSize_SIZE sizeof(ULONG)
    #define SM_SendLIRR_IN_InRespBufferMaxSize_ID 5

} SM_SendLIRR_IN, *PSM_SendLIRR_IN;

#define SM_SendLIRR_IN_SIZE (FIELD_OFFSET(SM_SendLIRR_IN, InRespBufferMaxSize) + SM_SendLIRR_IN_InRespBufferMaxSize_SIZE)

typedef struct _SM_SendLIRR_OUT
{
    // 
    ULONG HBAStatus;
    #define SM_SendLIRR_OUT_HBAStatus_SIZE sizeof(ULONG)
    #define SM_SendLIRR_OUT_HBAStatus_ID 6

    // 
    ULONG TotalRespBufferSize;
    #define SM_SendLIRR_OUT_TotalRespBufferSize_SIZE sizeof(ULONG)
    #define SM_SendLIRR_OUT_TotalRespBufferSize_ID 7

    // 
    ULONG OutRespBufferSize;
    #define SM_SendLIRR_OUT_OutRespBufferSize_SIZE sizeof(ULONG)
    #define SM_SendLIRR_OUT_OutRespBufferSize_ID 8

    // 
    UCHAR RespBuffer[1];
    #define SM_SendLIRR_OUT_RespBuffer_ID 9

} SM_SendLIRR_OUT, *PSM_SendLIRR_OUT;

#define SM_SendRLS     18
typedef struct _SM_SendRLS_IN
{
    // 
    UCHAR HbaPortWWN[8];
    #define SM_SendRLS_IN_HbaPortWWN_SIZE sizeof(UCHAR[8])
    #define SM_SendRLS_IN_HbaPortWWN_ID 1

    // 
    UCHAR DestWWN[8];
    #define SM_SendRLS_IN_DestWWN_SIZE sizeof(UCHAR[8])
    #define SM_SendRLS_IN_DestWWN_ID 2

    // 
    ULONG InRespBufferMaxSize;
    #define SM_SendRLS_IN_InRespBufferMaxSize_SIZE sizeof(ULONG)
    #define SM_SendRLS_IN_InRespBufferMaxSize_ID 3

} SM_SendRLS_IN, *PSM_SendRLS_IN;

#define SM_SendRLS_IN_SIZE (FIELD_OFFSET(SM_SendRLS_IN, InRespBufferMaxSize) + SM_SendRLS_IN_InRespBufferMaxSize_SIZE)

typedef struct _SM_SendRLS_OUT
{
    // 
    ULONG HBAStatus;
    #define SM_SendRLS_OUT_HBAStatus_SIZE sizeof(ULONG)
    #define SM_SendRLS_OUT_HBAStatus_ID 4

    // 
    ULONG TotalRespBufferSize;
    #define SM_SendRLS_OUT_TotalRespBufferSize_SIZE sizeof(ULONG)
    #define SM_SendRLS_OUT_TotalRespBufferSize_ID 5

    // 
    ULONG OutRespBufferSize;
    #define SM_SendRLS_OUT_OutRespBufferSize_SIZE sizeof(ULONG)
    #define SM_SendRLS_OUT_OutRespBufferSize_ID 6

    // 
    UCHAR RespBuffer[1];
    #define SM_SendRLS_OUT_RespBuffer_ID 7

} SM_SendRLS_OUT, *PSM_SendRLS_OUT;


// MS_SM_AdapterEvent - MS_SM_AdapterEvent
#define MS_SM_AdapterEventGuid \
    { 0x7944cf67,0x697b,0x4432, { 0x95,0x3e,0x1f,0xda,0xda,0x88,0x43,0x61 } }

#if ! (defined(MIDL_PASS))
DEFINE_GUID(MS_SM_AdapterEvent_GUID, \
            0x7944cf67,0x697b,0x4432,0x95,0x3e,0x1f,0xda,0xda,0x88,0x43,0x61);
#endif


typedef struct _MS_SM_AdapterEvent
{
    // 
    ULONG EventType;
    #define MS_SM_AdapterEvent_EventType_SIZE sizeof(ULONG)
    #define MS_SM_AdapterEvent_EventType_ID 1

    // 
    UCHAR PortWWN[8];
    #define MS_SM_AdapterEvent_PortWWN_SIZE sizeof(UCHAR[8])
    #define MS_SM_AdapterEvent_PortWWN_ID 2

} MS_SM_AdapterEvent, *PMS_SM_AdapterEvent;

#define MS_SM_AdapterEvent_SIZE (FIELD_OFFSET(MS_SM_AdapterEvent, PortWWN) + MS_SM_AdapterEvent_PortWWN_SIZE)

// MS_SM_PortEvent - MS_SM_PortEvent
#define MS_SM_PortEventGuid \
    { 0x0f760256,0x8fc6,0x47ad, { 0x9d,0x2e,0xf0,0xd6,0x98,0x01,0xde,0x7c } }

#if ! (defined(MIDL_PASS))
DEFINE_GUID(MS_SM_PortEvent_GUID, \
            0x0f760256,0x8fc6,0x47ad,0x9d,0x2e,0xf0,0xd6,0x98,0x01,0xde,0x7c);
#endif


typedef struct _MS_SM_PortEvent
{
    // 
    ULONG EventType;
    #define MS_SM_PortEvent_EventType_SIZE sizeof(ULONG)
    #define MS_SM_PortEvent_EventType_ID 1

    // 
    ULONG FabricPortId;
    #define MS_SM_PortEvent_FabricPortId_SIZE sizeof(ULONG)
    #define MS_SM_PortEvent_FabricPortId_ID 2

    // 
    UCHAR PortWWN[8];
    #define MS_SM_PortEvent_PortWWN_SIZE sizeof(UCHAR[8])
    #define MS_SM_PortEvent_PortWWN_ID 3

} MS_SM_PortEvent, *PMS_SM_PortEvent;

#define MS_SM_PortEvent_SIZE (FIELD_OFFSET(MS_SM_PortEvent, PortWWN) + MS_SM_PortEvent_PortWWN_SIZE)

// MS_SM_TargetEvent - MS_SM_TargetEvent
#define MS_SM_TargetEventGuid \
    { 0x6e2d8b73,0xf928,0x4da9, { 0xbd,0xa1,0xae,0x54,0x18,0x9a,0x38,0x25 } }

#if ! (defined(MIDL_PASS))
DEFINE_GUID(MS_SM_TargetEvent_GUID, \
            0x6e2d8b73,0xf928,0x4da9,0xbd,0xa1,0xae,0x54,0x18,0x9a,0x38,0x25);
#endif


typedef struct _MS_SM_TargetEvent
{
    // 
    ULONG EventType;
    #define MS_SM_TargetEvent_EventType_SIZE sizeof(ULONG)
    #define MS_SM_TargetEvent_EventType_ID 1

    // 
    UCHAR PortWWN[8];
    #define MS_SM_TargetEvent_PortWWN_SIZE sizeof(UCHAR[8])
    #define MS_SM_TargetEvent_PortWWN_ID 2

    // 
    UCHAR DiscoveredPortWWN[8];
    #define MS_SM_TargetEvent_DiscoveredPortWWN_SIZE sizeof(UCHAR[8])
    #define MS_SM_TargetEvent_DiscoveredPortWWN_ID 3

    // 
    UCHAR DomainPortWWN[8];
    #define MS_SM_TargetEvent_DomainPortWWN_SIZE sizeof(UCHAR[8])
    #define MS_SM_TargetEvent_DomainPortWWN_ID 4

} MS_SM_TargetEvent, *PMS_SM_TargetEvent;

#define MS_SM_TargetEvent_SIZE (FIELD_OFFSET(MS_SM_TargetEvent, DomainPortWWN) + MS_SM_TargetEvent_DomainPortWWN_SIZE)

// MS_SM_EventControl - MS_SM_EventControl
#define MS_SM_EventControlGuid \
    { 0xd6145693,0x5988,0x457f, { 0x85,0x81,0x9a,0x01,0x57,0xb5,0x86,0x90 } }

#if ! (defined(MIDL_PASS))
DEFINE_GUID(MS_SM_EventControl_GUID, \
            0xd6145693,0x5988,0x457f,0x85,0x81,0x9a,0x01,0x57,0xb5,0x86,0x90);
#endif

//
// Method id definitions for MS_SM_EventControl
#define SM_AddTarget     1
typedef struct _SM_AddTarget_IN
{
    // 
    UCHAR HbaPortWWN[8];
    #define SM_AddTarget_IN_HbaPortWWN_SIZE sizeof(UCHAR[8])
    #define SM_AddTarget_IN_HbaPortWWN_ID 1

    // 
    UCHAR DiscoveredPortWWN[8];
    #define SM_AddTarget_IN_DiscoveredPortWWN_SIZE sizeof(UCHAR[8])
    #define SM_AddTarget_IN_DiscoveredPortWWN_ID 2

    // 
    UCHAR DomainPortWWN[8];
    #define SM_AddTarget_IN_DomainPortWWN_SIZE sizeof(UCHAR[8])
    #define SM_AddTarget_IN_DomainPortWWN_ID 3

    // 
    ULONG AllTargets;
    #define SM_AddTarget_IN_AllTargets_SIZE sizeof(ULONG)
    #define SM_AddTarget_IN_AllTargets_ID 4

} SM_AddTarget_IN, *PSM_AddTarget_IN;

#define SM_AddTarget_IN_SIZE (FIELD_OFFSET(SM_AddTarget_IN, AllTargets) + SM_AddTarget_IN_AllTargets_SIZE)

typedef struct _SM_AddTarget_OUT
{
    // 
    ULONG HBAStatus;
    #define SM_AddTarget_OUT_HBAStatus_SIZE sizeof(ULONG)
    #define SM_AddTarget_OUT_HBAStatus_ID 5

} SM_AddTarget_OUT, *PSM_AddTarget_OUT;

#define SM_AddTarget_OUT_SIZE (FIELD_OFFSET(SM_AddTarget_OUT, HBAStatus) + SM_AddTarget_OUT_HBAStatus_SIZE)

#define SM_RemoveTarget     2
typedef struct _SM_RemoveTarget_IN
{
    // 
    UCHAR HbaPortWWN[8];
    #define SM_RemoveTarget_IN_HbaPortWWN_SIZE sizeof(UCHAR[8])
    #define SM_RemoveTarget_IN_HbaPortWWN_ID 1

    // 
    UCHAR DiscoveredPortWWN[8];
    #define SM_RemoveTarget_IN_DiscoveredPortWWN_SIZE sizeof(UCHAR[8])
    #define SM_RemoveTarget_IN_DiscoveredPortWWN_ID 2

    // 
    UCHAR DomainPortWWN[8];
    #define SM_RemoveTarget_IN_DomainPortWWN_SIZE sizeof(UCHAR[8])
    #define SM_RemoveTarget_IN_DomainPortWWN_ID 3

    // 
    ULONG AllTargets;
    #define SM_RemoveTarget_IN_AllTargets_SIZE sizeof(ULONG)
    #define SM_RemoveTarget_IN_AllTargets_ID 4

} SM_RemoveTarget_IN, *PSM_RemoveTarget_IN;

#define SM_RemoveTarget_IN_SIZE (FIELD_OFFSET(SM_RemoveTarget_IN, AllTargets) + SM_RemoveTarget_IN_AllTargets_SIZE)

typedef struct _SM_RemoveTarget_OUT
{
    // 
    ULONG HBAStatus;
    #define SM_RemoveTarget_OUT_HBAStatus_SIZE sizeof(ULONG)
    #define SM_RemoveTarget_OUT_HBAStatus_ID 5

} SM_RemoveTarget_OUT, *PSM_RemoveTarget_OUT;

#define SM_RemoveTarget_OUT_SIZE (FIELD_OFFSET(SM_RemoveTarget_OUT, HBAStatus) + SM_RemoveTarget_OUT_HBAStatus_SIZE)

#define SM_AddPort     3
typedef struct _SM_AddPort_IN
{
    // 
    UCHAR PortWWN[8];
    #define SM_AddPort_IN_PortWWN_SIZE sizeof(UCHAR[8])
    #define SM_AddPort_IN_PortWWN_ID 1

    // 
    ULONG EventType;
    #define SM_AddPort_IN_EventType_SIZE sizeof(ULONG)
    #define SM_AddPort_IN_EventType_ID 2

} SM_AddPort_IN, *PSM_AddPort_IN;

#define SM_AddPort_IN_SIZE (FIELD_OFFSET(SM_AddPort_IN, EventType) + SM_AddPort_IN_EventType_SIZE)

typedef struct _SM_AddPort_OUT
{
    // 
    ULONG HBAStatus;
    #define SM_AddPort_OUT_HBAStatus_SIZE sizeof(ULONG)
    #define SM_AddPort_OUT_HBAStatus_ID 3

} SM_AddPort_OUT, *PSM_AddPort_OUT;

#define SM_AddPort_OUT_SIZE (FIELD_OFFSET(SM_AddPort_OUT, HBAStatus) + SM_AddPort_OUT_HBAStatus_SIZE)

#define SM_RemovePort     4
typedef struct _SM_RemovePort_IN
{
    // 
    UCHAR PortWWN[8];
    #define SM_RemovePort_IN_PortWWN_SIZE sizeof(UCHAR[8])
    #define SM_RemovePort_IN_PortWWN_ID 1

    // 
    ULONG EventType;
    #define SM_RemovePort_IN_EventType_SIZE sizeof(ULONG)
    #define SM_RemovePort_IN_EventType_ID 2

} SM_RemovePort_IN, *PSM_RemovePort_IN;

#define SM_RemovePort_IN_SIZE (FIELD_OFFSET(SM_RemovePort_IN, EventType) + SM_RemovePort_IN_EventType_SIZE)

typedef struct _SM_RemovePort_OUT
{
    // 
    ULONG HBAStatus;
    #define SM_RemovePort_OUT_HBAStatus_SIZE sizeof(ULONG)
    #define SM_RemovePort_OUT_HBAStatus_ID 3

} SM_RemovePort_OUT, *PSM_RemovePort_OUT;

#define SM_RemovePort_OUT_SIZE (FIELD_OFFSET(SM_RemovePort_OUT, HBAStatus) + SM_RemovePort_OUT_HBAStatus_SIZE)

#define SM_AddLink     10
typedef struct _SM_AddLink_OUT
{
    // 
    ULONG HBAStatus;
    #define SM_AddLink_OUT_HBAStatus_SIZE sizeof(ULONG)
    #define SM_AddLink_OUT_HBAStatus_ID 1

} SM_AddLink_OUT, *PSM_AddLink_OUT;

#define SM_AddLink_OUT_SIZE (FIELD_OFFSET(SM_AddLink_OUT, HBAStatus) + SM_AddLink_OUT_HBAStatus_SIZE)

#define SM_RemoveLink     11
typedef struct _SM_RemoveLink_OUT
{
    // 
    ULONG HBAStatus;
    #define SM_RemoveLink_OUT_HBAStatus_SIZE sizeof(ULONG)
    #define SM_RemoveLink_OUT_HBAStatus_ID 1

} SM_RemoveLink_OUT, *PSM_RemoveLink_OUT;

#define SM_RemoveLink_OUT_SIZE (FIELD_OFFSET(SM_RemoveLink_OUT, HBAStatus) + SM_RemoveLink_OUT_HBAStatus_SIZE)


// MSFC_TM - MSFC_TM

#endif // MS_SM_HBA_API

#define MSFC_TMGuid \
    { 0x8cf4c7eb,0xa286,0x409d, { 0x9e,0xb9,0x29,0xd7,0xe0,0xe9,0xf4,0xfa } }

#if ! (defined(MIDL_PASS))
DEFINE_GUID(MSFC_TM_GUID, \
            0x8cf4c7eb,0xa286,0x409d,0x9e,0xb9,0x29,0xd7,0xe0,0xe9,0xf4,0xfa);
#endif


typedef struct _MSFC_TM
{
    // 
    ULONG tm_sec;
    #define MSFC_TM_tm_sec_SIZE sizeof(ULONG)
    #define MSFC_TM_tm_sec_ID 1

    // 
    ULONG tm_min;
    #define MSFC_TM_tm_min_SIZE sizeof(ULONG)
    #define MSFC_TM_tm_min_ID 2

    // 
    ULONG tm_hour;
    #define MSFC_TM_tm_hour_SIZE sizeof(ULONG)
    #define MSFC_TM_tm_hour_ID 3

    // 
    ULONG tm_mday;
    #define MSFC_TM_tm_mday_SIZE sizeof(ULONG)
    #define MSFC_TM_tm_mday_ID 4

    // 
    ULONG tm_mon;
    #define MSFC_TM_tm_mon_SIZE sizeof(ULONG)
    #define MSFC_TM_tm_mon_ID 5

    // 
    ULONG tm_year;
    #define MSFC_TM_tm_year_SIZE sizeof(ULONG)
    #define MSFC_TM_tm_year_ID 6

    // 
    ULONG tm_wday;
    #define MSFC_TM_tm_wday_SIZE sizeof(ULONG)
    #define MSFC_TM_tm_wday_ID 7

    // 
    ULONG tm_yday;
    #define MSFC_TM_tm_yday_SIZE sizeof(ULONG)
    #define MSFC_TM_tm_yday_ID 8

    // 
    ULONG tm_isdst;
    #define MSFC_TM_tm_isdst_SIZE sizeof(ULONG)
    #define MSFC_TM_tm_isdst_ID 9

} MSFC_TM, *PMSFC_TM;

#define MSFC_TM_SIZE (FIELD_OFFSET(MSFC_TM, tm_isdst) + MSFC_TM_tm_isdst_SIZE)

// GmDemoDriver - GmDemoDriver
// GmDemoDriver Schema
#define GmDemoDriverGuid \
    { 0x33168f61,0x67a8,0x408e, { 0xb2,0x62,0x12,0x40,0xaa,0xc0,0x34,0x47 } }

#if ! (defined(MIDL_PASS))
DEFINE_GUID(GmDemoDriver_GUID, \
            0x33168f61,0x67a8,0x408e,0xb2,0x62,0x12,0x40,0xaa,0xc0,0x34,0x47);
#endif


typedef struct _GmDemoDriver
{
    // The Answer
    ULONG TheAnswer;
    #define GmDemoDriver_TheAnswer_SIZE sizeof(ULONG)
    #define GmDemoDriver_TheAnswer_ID 1

    // The Next Answer
    ULONG TheNextAnswer;
    #define GmDemoDriver_TheNextAnswer_SIZE sizeof(ULONG)
    #define GmDemoDriver_TheNextAnswer_ID 2

    // SRBs seen
    ULONG SRBsSeen;
    #define GmDemoDriver_SRBsSeen_SIZE sizeof(ULONG)
    #define GmDemoDriver_SRBsSeen_ID 3

    // WMI SRBs seen
    ULONG WMISRBsSeen;
    #define GmDemoDriver_WMISRBsSeen_SIZE sizeof(ULONG)
    #define GmDemoDriver_WMISRBsSeen_ID 4

} GmDemoDriver, *PGmDemoDriver;

#define GmDemoDriver_SIZE (FIELD_OFFSET(GmDemoDriver, WMISRBsSeen) + GmDemoDriver_WMISRBsSeen_SIZE)

// GmDemoDriver2 - GmDemoDriver2
// GmDemoDriver Schema2
#define GmDemoDriver2Guid \
    { 0x33168f62,0x67a8,0x408e, { 0xb2,0x62,0x12,0x40,0xaa,0xc0,0x34,0x47 } }

#if ! (defined(MIDL_PASS))
DEFINE_GUID(GmDemoDriver2_GUID, \
            0x33168f62,0x67a8,0x408e,0xb2,0x62,0x12,0x40,0xaa,0xc0,0x34,0x47);
#endif


typedef struct _GmDemoDriver2
{
    // Number of array elements
    ULONG NumberElements;
    #define GmDemoDriver2_NumberElements_SIZE sizeof(ULONG)
    #define GmDemoDriver2_NumberElements_ID 1

    // The array
    ULONG UlongArray[1];
    #define GmDemoDriver2_UlongArray_ID 2

} GmDemoDriver2, *PGmDemoDriver2;

// GmDemoDriverSrbActivity - GmDemoDriverSrbActivity
// Performance counter class that keeps counts of SRBs
#define GmDemoDriverSrbActivityGuid \
    { 0x33168f63,0x67a8,0x408e, { 0xb2,0x62,0x12,0x40,0xaa,0xc0,0x34,0x47 } }

#if ! (defined(MIDL_PASS))
DEFINE_GUID(GmDemoDriverSrbActivity_GUID, \
            0x33168f63,0x67a8,0x408e,0xb2,0x62,0x12,0x40,0xaa,0xc0,0x34,0x47);
#endif


typedef struct _GmDemoDriverSrbActivity
{
    // Count of CREATE SRBs received
    ULONG TotalCreateSRBs;
    #define GmDemoDriverSrbActivity_TotalCreateSRBs_SIZE sizeof(ULONG)
    #define GmDemoDriverSrbActivity_TotalCreateSRBs_ID 1

    // Count of CLOSE SRBs received
    ULONG TotalCloseSRBs;
    #define GmDemoDriverSrbActivity_TotalCloseSRBs_SIZE sizeof(ULONG)
    #define GmDemoDriverSrbActivity_TotalCloseSRBs_ID 2

    // Count of IOCTL SRBs received
    ULONG TotalIoCtlSrbs;
    #define GmDemoDriverSrbActivity_TotalIoCtlSrbs_SIZE sizeof(ULONG)
    #define GmDemoDriverSrbActivity_TotalIoCtlSrbs_ID 3

} GmDemoDriverSrbActivity, *PGmDemoDriverSrbActivity;

#define GmDemoDriverSrbActivity_SIZE (FIELD_OFFSET(GmDemoDriverSrbActivity, TotalIoCtlSrbs) + GmDemoDriverSrbActivity_TotalIoCtlSrbs_SIZE)

// GmDrvDrvMethod - GmDrvDrvMethod
// WMI method
#define GmDrvDrvMethodGuid \
    { 0x33168f64,0x67a8,0x408e, { 0xb2,0x62,0x12,0x40,0xaa,0xc0,0x34,0x47 } }

#if ! (defined(MIDL_PASS))
DEFINE_GUID(GmDrvDrvMethod_GUID, \
            0x33168f64,0x67a8,0x408e,0xb2,0x62,0x12,0x40,0xaa,0xc0,0x34,0x47);
#endif

//
// Method id definitions for GmDrvDrvMethod
#define GmDrvDemoMethod1     1
typedef struct _GmDrvDemoMethod1_IN
{
    // 
    ULONG inDatum;
    #define GmDrvDemoMethod1_IN_inDatum_SIZE sizeof(ULONG)
    #define GmDrvDemoMethod1_IN_inDatum_ID 1

} GmDrvDemoMethod1_IN, *PGmDrvDemoMethod1_IN;

#define GmDrvDemoMethod1_IN_SIZE (FIELD_OFFSET(GmDrvDemoMethod1_IN, inDatum) + GmDrvDemoMethod1_IN_inDatum_SIZE)

typedef struct _GmDrvDemoMethod1_OUT
{
    // 
    ULONG outDatum;
    #define GmDrvDemoMethod1_OUT_outDatum_SIZE sizeof(ULONG)
    #define GmDrvDemoMethod1_OUT_outDatum_ID 2

} GmDrvDemoMethod1_OUT, *PGmDrvDemoMethod1_OUT;

#define GmDrvDemoMethod1_OUT_SIZE (FIELD_OFFSET(GmDrvDemoMethod1_OUT, outDatum) + GmDrvDemoMethod1_OUT_outDatum_SIZE)

#define GmDrvDemoMethod2     2
typedef struct _GmDrvDemoMethod2_IN
{
    // 
    ULONG inDatum1;
    #define GmDrvDemoMethod2_IN_inDatum1_SIZE sizeof(ULONG)
    #define GmDrvDemoMethod2_IN_inDatum1_ID 1

    // 
    ULONG inDatum2;
    #define GmDrvDemoMethod2_IN_inDatum2_SIZE sizeof(ULONG)
    #define GmDrvDemoMethod2_IN_inDatum2_ID 2

} GmDrvDemoMethod2_IN, *PGmDrvDemoMethod2_IN;

#define GmDrvDemoMethod2_IN_SIZE (FIELD_OFFSET(GmDrvDemoMethod2_IN, inDatum2) + GmDrvDemoMethod2_IN_inDatum2_SIZE)

typedef struct _GmDrvDemoMethod2_OUT
{
    // 
    ULONG outDatum1;
    #define GmDrvDemoMethod2_OUT_outDatum1_SIZE sizeof(ULONG)
    #define GmDrvDemoMethod2_OUT_outDatum1_ID 3

} GmDrvDemoMethod2_OUT, *PGmDrvDemoMethod2_OUT;

#define GmDrvDemoMethod2_OUT_SIZE (FIELD_OFFSET(GmDrvDemoMethod2_OUT, outDatum1) + GmDrvDemoMethod2_OUT_outDatum1_SIZE)

#define GmDrvDemoMethod3     3
typedef struct _GmDrvDemoMethod3_IN
{
    // 
    ULONG inDatum1;
    #define GmDrvDemoMethod3_IN_inDatum1_SIZE sizeof(ULONG)
    #define GmDrvDemoMethod3_IN_inDatum1_ID 1

    // 
    ULONG inDatum2;
    #define GmDrvDemoMethod3_IN_inDatum2_SIZE sizeof(ULONG)
    #define GmDrvDemoMethod3_IN_inDatum2_ID 2

} GmDrvDemoMethod3_IN, *PGmDrvDemoMethod3_IN;

#define GmDrvDemoMethod3_IN_SIZE (FIELD_OFFSET(GmDrvDemoMethod3_IN, inDatum2) + GmDrvDemoMethod3_IN_inDatum2_SIZE)

typedef struct _GmDrvDemoMethod3_OUT
{
    // 
    ULONG outDatum1;
    #define GmDrvDemoMethod3_OUT_outDatum1_SIZE sizeof(ULONG)
    #define GmDrvDemoMethod3_OUT_outDatum1_ID 3

    // 
    ULONG outDatum2;
    #define GmDrvDemoMethod3_OUT_outDatum2_SIZE sizeof(ULONG)
    #define GmDrvDemoMethod3_OUT_outDatum2_ID 4

} GmDrvDemoMethod3_OUT, *PGmDrvDemoMethod3_OUT;

#define GmDrvDemoMethod3_OUT_SIZE (FIELD_OFFSET(GmDrvDemoMethod3_OUT, outDatum2) + GmDrvDemoMethod3_OUT_outDatum2_SIZE)


#endif
