// SPDX-License-Identifier: CDDL-1.0
/*
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this
 * source.  A copy of the CDDL is also available via the Internet at
 * https://opensource.org/license/CDDL-1.0.
 */

/*
 * Copyright (c) 2025 by Jorgen Lundman <lundman@lundman.net>.
 */

#ifndef SYS_OPENZVOL_H
#define	SYS_OPENZVOL_H

#define	DEVICE_NAME L"\\Device\\OpenZVOL"
#define	SYMBOLIC_LINK_NAME L"\\??\\OpenZVOL"
#define	STOP_UNLOAD_NAME L"\\Device\\StopUnload"
#define	STOP_UNLOAD_LINK_NAME L"\\??\\StopUnload"

void OpenZVOLUnloadRoutine(IN PDRIVER_OBJECT DriverObject);

extern void printBuffer(const char *fmt, ...);
#undef dprintf
#define	dprintf printBuffer

NTSYSCALLAPI NTSTATUS NTAPI ZwQuerySystemInformation(
	ULONG SystemInformationClass,
	PVOID SystemInformation,
	ULONG SystemInformationLength,
	PULONG ReturnLength
);

typedef struct _SYSTEM_MODULE {
	ULONG	Reserved[2];
#ifdef _WIN64
	ULONG Unknown3;
	ULONG Unknown4;
#endif
	PVOID	Base;
	ULONG	Size;
	ULONG	Flags;
	USHORT	Index;
	USHORT	Unknown;
	USHORT	LoadCount;
	USHORT	ModuleNameOffset;
	CHAR	ImageName[256];
} SYSTEM_MODULE, *PSYSTEM_MODULE;

typedef struct _SYSTEM_MODULE_INFORMATION {
	ULONG	ModulesCount;
	SYSTEM_MODULE	Modules[1];
} SYSTEM_MODULE_INFORMATION, *PSYSTEM_MODULE_INFORMATION;

#define	SystemModuleInformation 11

#define	OPENZVOL_REGISTER \
	CTL_CODE(FILE_DEVICE_UNKNOWN, 0x8e0, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define	OPENZVOL_DEREGISTER \
	CTL_CODE(FILE_DEVICE_UNKNOWN, 0x8e1, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define	OPENZVOL_ASSIGN_TARGETID \
	CTL_CODE(FILE_DEVICE_UNKNOWN, 0x8e2, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define	OPENZVOL_CLEAR_TARGETID \
	CTL_CODE(FILE_DEVICE_UNKNOWN, 0x8e3, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define	OPENZVOL_ANNOUNCE_BUSCHANGE \
	CTL_CODE(FILE_DEVICE_UNKNOWN, 0x8e4, METHOD_BUFFERED, FILE_ANY_ACCESS)

typedef struct zvol_state zvol_state_t;

struct zvol_api {
	int (*zvol_os_read_zv)(zvol_state_t *zv, zfs_uio_t *uio, int flags);
	int (*zvol_os_write_zv)(zvol_state_t *zv, zfs_uio_t *uio, int flags);
	int (*zvol_os_unmap)(zvol_state_t *zv, uint64_t off, uint64_t bytes);
};

typedef struct zvol_api zvol_api_t;

extern int (*pzvol_os_read_zv)(zvol_state_t *zv, zfs_uio_t *uio, int flags);
extern int (*pzvol_os_write_zv)(zvol_state_t *zv, zfs_uio_t *uio, int flags);
extern int (*pzvol_os_unmap)(zvol_state_t *zv, uint64_t off, uint64_t bytes);

extern void wzvol_clear_targetid(uint8_t targetid, uint8_t lun,
    zvol_state_t *zv);
extern void wzvol_announce_buschange(void);
extern int wzvol_assign_targetid(zvol_state_t *zv);

extern NTSTATUS openzvol_register_module(void *ptr);

extern int  zvol_os_register_module(void);
extern void zvol_os_deregister_module(void);

inline static boolean_t
FindDriver(char *driver_name)
{
	ULONG bufferLength = 0;
	boolean_t found = FALSE;

	ZwQuerySystemInformation(SystemModuleInformation, NULL, 0,
	    &bufferLength);

	PVOID buffer = ExAllocatePoolWithTag(NonPagedPoolNx, bufferLength,
	    'sysm');

	if (buffer == NULL)
		return (found);

	if (NT_SUCCESS(ZwQuerySystemInformation(SystemModuleInformation,
	    buffer, bufferLength, &bufferLength))) {
		PSYSTEM_MODULE_INFORMATION moduleInformation =
		    (PSYSTEM_MODULE_INFORMATION)buffer;
		for (ULONG i = 0; i < moduleInformation->ModulesCount; ++i) {
			dprintf("Driver: %s\n",
			    moduleInformation->Modules[i].ImageName);

			if (strncmp(driver_name,
			    moduleInformation->Modules[i].ImageName,
			    sizeof (moduleInformation->Modules[i].ImageName))
			    == 0)
				found = TRUE; /* Could break here */
		}
	}

	ExFreePoolWithTag(buffer, 'sysm');

	dprintf("%s: %s driver %s on system\n",
	    __func__, found ? "found" : "did not find",
	    driver_name);

	return (found);
}


#endif /* SYS_OPENZVOL_H */
