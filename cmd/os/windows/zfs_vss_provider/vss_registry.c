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
 * CDDL HEADER END
 */
/*
 * Copyright (c) 2025 Jorgen Lundman <lundman@lundman.net>
 */

/*
 * vss_registry.c - persistent storage for VSS GUID -> ZFS snapshot mappings.
 *
 * Each VSS shadow copy gets its own subkey under ZFS_VSS_REG_ROOT:
 *   HKLM\SOFTWARE\OpenZFS\VSS\Snapshots\{VSS-GUID}
 *
 * Values stored per snapshot:
 *   ZfsName     REG_SZ    "pool/dataset@vss-..."
 *   ZfsGuid     REG_QWORD ZFS dataset GUID (ds_guid)
 *   VolumeName  REG_SZ    "\\?\Volume{...}\"
 *   SnapshotSetId REG_SZ  "{...}"
 *   Timestamp   REG_QWORD creation time as FILETIME
 *   Attributes  REG_DWORD VSS_VOLUME_SNAPSHOT_ATTRIBUTES flags
 */

#define	_CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <objbase.h>
#include <stdio.h>
#include <stdint.h>

#include "vss_provider.h"

/* Format a GUID as the registry subkey name (no braces). */
static void
guid_to_key(const GUID *g, wchar_t *buf, int nchars)
{
	_snwprintf(buf, nchars,
	    L"%08X-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X",
	    g->Data1, g->Data2, g->Data3,
	    g->Data4[0], g->Data4[1],
	    g->Data4[2], g->Data4[3], g->Data4[4],
	    g->Data4[5], g->Data4[6], g->Data4[7]);
}

/* Parse a GUID string (with or without braces) into a GUID struct. */
static int
key_to_guid(const wchar_t *str, GUID *g)
{
	wchar_t buf[64];

	/* CLSIDFromString requires braces; add them if missing */
	if (str[0] != L'{') {
		_snwprintf(buf, 64, L"{%s}", str);
		str = buf;
	}
	return (CLSIDFromString((LPCOLESTR)str, g) == S_OK ? 0 : -1);
}

/*
 * Open (or create) the per-snapshot subkey.
 * Caller must call RegCloseKey on the returned handle.
 */
static HKEY
open_snap_key(const GUID *sid, BOOL create)
{
	wchar_t keyname[256];
	wchar_t subkey[64];
	HKEY hk = NULL;
	LONG r;

	guid_to_key(sid, subkey, 64);
	_snwprintf(keyname, 256, L"%s\\%s", ZFS_VSS_REG_ROOT, subkey);

	if (create) {
		/*
		 * RegCreateKeyExW creates only the final component; ensure
		 * the root path exists first by creating it (no-op if it
		 * already exists).
		 */
		HKEY hroot;
		RegCreateKeyExW(HKEY_LOCAL_MACHINE, ZFS_VSS_REG_ROOT,
		    0, NULL, REG_OPTION_NON_VOLATILE,
		    KEY_WRITE, NULL, &hroot, NULL);
		if (hroot)
			RegCloseKey(hroot);

		r = RegCreateKeyExW(HKEY_LOCAL_MACHINE, keyname,
		    0, NULL, REG_OPTION_NON_VOLATILE,
		    KEY_READ | KEY_WRITE, NULL, &hk, NULL);
	} else {
		r = RegOpenKeyExW(HKEY_LOCAL_MACHINE, keyname,
		    0, KEY_READ, &hk);
	}

	return (r == ERROR_SUCCESS ? hk : NULL);
}

/*
 * vss_reg_store - write all metadata for a new snapshot entry.
 * Returns 0 on success, -1 on error.
 */
int
vss_reg_store(const GUID *sid, const GUID *setid,
    const wchar_t *volume, const char *zfsname, uint64_t zfsguid,
    LONGLONG timestamp, LONG attrs)
{
	HKEY hk;
	wchar_t setid_str[64];
	wchar_t zfsname_w[256];
	int ret = -1;

	hk = open_snap_key(sid, TRUE);
	if (hk == NULL)
		return (-1);

	/* ZfsName (REG_SZ) - convert from char to wchar */
	MultiByteToWideChar(CP_UTF8, 0, zfsname, -1,
	    zfsname_w, 256);
	if (RegSetValueExW(hk, ZFS_VSS_REG_ZFSNAME, 0, REG_SZ,
	    (const BYTE *)zfsname_w,
	    (DWORD)((wcslen(zfsname_w) + 1) * sizeof (wchar_t)))
	    != ERROR_SUCCESS)
		goto out;

	/* ZfsGuid (REG_QWORD) */
	if (RegSetValueExW(hk, ZFS_VSS_REG_ZFSGUID, 0, REG_QWORD,
	    (const BYTE *)&zfsguid, sizeof (zfsguid)) != ERROR_SUCCESS)
		goto out;

	/* VolumeName (REG_SZ) */
	if (RegSetValueExW(hk, ZFS_VSS_REG_VOLUME, 0, REG_SZ,
	    (const BYTE *)volume,
	    (DWORD)((wcslen(volume) + 1) * sizeof (wchar_t)))
	    != ERROR_SUCCESS)
		goto out;

	/* SnapshotSetId (REG_SZ) */
	guid_to_key(setid, setid_str, 64);
	if (RegSetValueExW(hk, ZFS_VSS_REG_SETID, 0, REG_SZ,
	    (const BYTE *)setid_str,
	    (DWORD)((wcslen(setid_str) + 1) * sizeof (wchar_t)))
	    != ERROR_SUCCESS)
		goto out;

	/* Timestamp (REG_QWORD) */
	if (RegSetValueExW(hk, ZFS_VSS_REG_TIMESTAMP, 0, REG_QWORD,
	    (const BYTE *)&timestamp, sizeof (timestamp)) != ERROR_SUCCESS)
		goto out;

	/* Attributes (REG_DWORD) */
	if (RegSetValueExW(hk, ZFS_VSS_REG_ATTRS, 0, REG_DWORD,
	    (const BYTE *)&attrs, sizeof (attrs)) != ERROR_SUCCESS)
		goto out;

	ret = 0;
out:
	RegCloseKey(hk);
	return (ret);
}

/*
 * vss_reg_load - read metadata for an existing snapshot entry.
 * Returns 0 on success, -1 if not found or error.
 */
int
vss_reg_load(const GUID *sid,
    wchar_t *volume, int volchars,
    char *zfsname, int znchars,
    uint64_t *zfsguid, GUID *setid,
    LONGLONG *timestamp, LONG *attrs)
{
	HKEY hk;
	DWORD type, sz;
	wchar_t setid_str[64];
	wchar_t zfsname_w[256];
	int ret = -1;

	hk = open_snap_key(sid, FALSE);
	if (hk == NULL)
		return (-1);

	/* ZfsName */
	sz = sizeof (zfsname_w);
	if (RegQueryValueExW(hk, ZFS_VSS_REG_ZFSNAME, NULL, &type,
	    (BYTE *)zfsname_w, &sz) != ERROR_SUCCESS)
		goto out;
	WideCharToMultiByte(CP_UTF8, 0, zfsname_w, -1,
	    zfsname, znchars, NULL, NULL);

	/* ZfsGuid */
	sz = sizeof (*zfsguid);
	if (RegQueryValueExW(hk, ZFS_VSS_REG_ZFSGUID, NULL, &type,
	    (BYTE *)zfsguid, &sz) != ERROR_SUCCESS)
		goto out;

	/* VolumeName */
	sz = (DWORD)(volchars * sizeof (wchar_t));
	if (RegQueryValueExW(hk, ZFS_VSS_REG_VOLUME, NULL, &type,
	    (BYTE *)volume, &sz) != ERROR_SUCCESS)
		goto out;

	/* SnapshotSetId */
	sz = sizeof (setid_str);
	if (RegQueryValueExW(hk, ZFS_VSS_REG_SETID, NULL, &type,
	    (BYTE *)setid_str, &sz) != ERROR_SUCCESS)
		goto out;
	key_to_guid(setid_str, setid);

	/* Timestamp */
	sz = sizeof (*timestamp);
	if (RegQueryValueExW(hk, ZFS_VSS_REG_TIMESTAMP, NULL, &type,
	    (BYTE *)timestamp, &sz) != ERROR_SUCCESS)
		goto out;

	/* Attributes */
	sz = sizeof (*attrs);
	if (RegQueryValueExW(hk, ZFS_VSS_REG_ATTRS, NULL, &type,
	    (BYTE *)attrs, &sz) != ERROR_SUCCESS)
		goto out;

	ret = 0;
out:
	RegCloseKey(hk);
	return (ret);
}

/*
 * vss_reg_update_attrs - update only the Attributes field.
 */
int
vss_reg_update_attrs(const GUID *sid, LONG attrs)
{
	HKEY hk = open_snap_key(sid, FALSE);
	if (hk == NULL)
		return (-1);
	LONG r = RegSetValueExW(hk, ZFS_VSS_REG_ATTRS, 0, REG_DWORD,
	    (const BYTE *)&attrs, sizeof (attrs));
	RegCloseKey(hk);
	return (r == ERROR_SUCCESS ? 0 : -1);
}

/*
 * vss_reg_update_status - update only the Status field.
 */
int
vss_reg_update_status(const GUID *sid, LONG status)
{
	HKEY hk = open_snap_key(sid, FALSE);
	if (hk == NULL)
		return (-1);
	LONG r = RegSetValueExW(hk, ZFS_VSS_REG_STATUS, 0, REG_DWORD,
	    (const BYTE *)&status, sizeof (status));
	RegCloseKey(hk);
	return (r == ERROR_SUCCESS ? 0 : -1);
}

/*
 * vss_reg_update_string - update any named REG_SZ field.
 */
int
vss_reg_update_string(const GUID *sid, const wchar_t *valuename,
    const wchar_t *value)
{
	HKEY hk = open_snap_key(sid, FALSE);
	if (hk == NULL)
		return (-1);
	LONG r = RegSetValueExW(hk, valuename, 0, REG_SZ,
	    (const BYTE *)value,
	    (DWORD)((wcslen(value) + 1) * sizeof (wchar_t)));
	RegCloseKey(hk);
	return (r == ERROR_SUCCESS ? 0 : -1);
}

/*
 * vss_reg_delete - remove the registry entry for a snapshot.
 */
int
vss_reg_delete(const GUID *sid)
{
	wchar_t keyname[256];
	wchar_t subkey[64];

	guid_to_key(sid, subkey, 64);
	_snwprintf(keyname, 256, L"%s\\%s", ZFS_VSS_REG_ROOT, subkey);

	return (RegDeleteKeyW(HKEY_LOCAL_MACHINE, keyname) == ERROR_SUCCESS
	    ? 0 : -1);
}

/*
 * vss_reg_enum - enumerate all stored snapshot GUIDs.
 * Returns the number of GUIDs found (may exceed maxids if truncated).
 */
int
vss_reg_enum(GUID *ids, int maxids)
{
	HKEY hroot;
	int count = 0;
	DWORD idx = 0;
	wchar_t name[64];
	DWORD namesz;
	LONG r;
	GUID g;

	if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, ZFS_VSS_REG_ROOT,
	    0, KEY_ENUMERATE_SUB_KEYS, &hroot) != ERROR_SUCCESS)
		return (0);

	for (;;) {
		namesz = 64;
		r = RegEnumKeyExW(hroot, idx++, name, &namesz,
		    NULL, NULL, NULL, NULL);
		if (r == ERROR_NO_MORE_ITEMS)
			break;
		if (r != ERROR_SUCCESS)
			continue;
		if (key_to_guid(name, &g) != 0)
			continue;
		if (count < maxids)
			ids[count] = g;
		count++;
	}

	RegCloseKey(hroot);
	return (count);
}
