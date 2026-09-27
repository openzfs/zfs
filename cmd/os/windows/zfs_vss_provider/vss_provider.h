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
 * Copyright (c) 2025 Jorgen Lundman <lundman@lundman.net>
 */

#ifndef _ZFS_VSS_PROVIDER_H
#define	_ZFS_VSS_PROVIDER_H

#include <windows.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Provider identity strings and GUIDs.
 *
 * CLSID_ZfsVssProvider = {89300202-3CAE-4584-B38F-E72F2B3A2FBF}
 * This GUID is used both as the COM CLSID and as the VSS ProviderID.
 */
#define	ZFS_VSS_PROVIDER_NAME		L"OpenZFS VSS Provider"
#define	ZFS_VSS_PROVIDER_VERSION	L"1.0"
#define	ZFS_VSS_PROVIDER_GUID_STR	\
    L"{89300202-3CAE-4584-B38F-E72F2B3A2FBF}"
	/* Registry subtree for per-snapshot metadata */

#define	ZFS_VSS_PROVIDER_GUID_BINARY 0x89300202, 0x3CAE, 0x4584, \
	{ 0xB3, 0x8F, 0xE7, 0x2F, 0x2B, 0x3A, 0x2F, 0xBF }


#define	ZFS_VSS_REG_ROOT	\
	L"SOFTWARE\\OpenZFS\\VSS\\Snapshots"

/* Per-snapshot value names */
#define	ZFS_VSS_REG_ZFSNAME	L"ZfsName"	/* REG_SZ pool/ds@snap */
#define	ZFS_VSS_REG_ZFSGUID	L"ZfsGuid"	/* REG_QWORD ds_guid */
#define	ZFS_VSS_REG_VOLUME	L"VolumeName"	/* REG_SZ \\?\Volume{} */
#define	ZFS_VSS_REG_SETID	L"SnapshotSetId"	/* REG_SZ {guid} */
#define	ZFS_VSS_REG_TIMESTAMP	L"Timestamp"	/* REG_QWORD FILETIME */
#define	ZFS_VSS_REG_ATTRS	L"Attributes"	/* REG_DWORD */
#define	ZFS_VSS_REG_EXPNAME	L"ExposedName"	/* REG_SZ e.g. "S:\" */
#define	ZFS_VSS_REG_EXPPATH	L"ExposedPath"	/* REG_SZ subdir or "" */
#define	ZFS_VSS_REG_STATUS	L"Status"	/* REG_DWORD VSS_SS_* */

/*
 * Registry helpers (vss_registry.c)
 */
int vss_reg_store(const GUID *sid, const GUID *setid,
    const wchar_t *volume, const char *zfsname, uint64_t zfsguid,
    LONGLONG timestamp, LONG attrs);

int vss_reg_load(const GUID *sid,
    wchar_t *volume, int volchars,
    char *zfsname, int znchars,
    uint64_t *zfsguid, GUID *setid,
    LONGLONG *timestamp, LONG *attrs);

int vss_reg_delete(const GUID *sid);

/* Returns number of GUIDs found, up to maxids. */
int vss_reg_enum(GUID *ids, int maxids);

/* Update individual fields on an existing snapshot entry. */
int vss_reg_update_attrs(const GUID *sid, LONG attrs);
int vss_reg_update_status(const GUID *sid, LONG status);
int vss_reg_update_string(const GUID *sid, const wchar_t *valuename,
    const wchar_t *value);

/*
 * LocalServer32 EXE entry points (implemented in vss_provider.cpp,
 * called by vss_service.c to register/revoke the COM class factory).
 */
HRESULT vss_provider_register_server(void);
HRESULT vss_provider_revoke_server(void);

#ifdef __cplusplus
}
#endif

#endif /* _ZFS_VSS_PROVIDER_H */
