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
 * vss_provider.cpp - ZFS VSS software snapshot provider.
 *
 * Implements the three COM interfaces required by Windows VSS for a
 * software snapshot provider:
 *
 *   IVssSoftwareSnapshotProvider  - snapshot CRUD + volume queries
 *   IVssProviderCreateSnapshotSet - Prepare/Commit/Abort lifecycle
 *   IVssProviderNotifications     - load/unload hooks
 *
 * Snapshot lifecycle (VSS Coordinator calls in order):
 *   BeginPrepareSnapshot  - record (SnapshotId, VolumeName) pair
 *   EndPrepareSnapshots   - no-op
 *   PreCommitSnapshots    - no-op (ZFS needs no IO freeze)
 *   CommitSnapshots       - zfs_snapshot() via libzfs; store mapping
 *   PostCommitSnapshots   - no-op
 *   PostFinalCommitSnapshots - no-op
 *
 * GetSnapshotProperties fills m_pwszSnapshotDeviceObject with:
 *   "\\?\GLOBALROOT\Device\ZfsSnapshot<hex16>"
 * where hex16 is the ZFS ds_guid, matching the kernel device created
 * by zfs_vss.c in response to the snapshot creation event.
 */

#define	_CRT_SECURE_NO_WARNINGS
#define	WIN32_LEAN_AND_MEAN

#define MNTTAB_NO_DIRENT_H
extern "C" {
#include <libzfs.h>
}

#include <windows.h>
#include <wincrypt.h>
#include <objbase.h>
#include <vss.h>
#include <vsprov.h>
#include <new>
#include <stdio.h>
#include <stdint.h>


#include "vss_provider.h"




/* ------------------------------------------------------------------ */
/* Diagnostic logging to C:\Windows\Temp\ZfsVssProvider.log            */
/* ------------------------------------------------------------------ */

static void
vss_log(const char *fmt, ...)
{
	static const wchar_t *path =
	    L"C:\\Windows\\Temp\\ZfsVssProvider.log";
	HANDLE h = CreateFileW(path, FILE_APPEND_DATA,
	    FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
	    OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (h == INVALID_HANDLE_VALUE)
		return;

	char buf[8192];
	int hdr = _snprintf(buf, sizeof (buf), "[%lu] ", GetCurrentProcessId());
	if (hdr < 0)
		hdr = 0;

	va_list ap;
	va_start(ap, fmt);
	int len = _vsnprintf(buf + hdr, sizeof (buf) - hdr, fmt, ap);
	va_end(ap);
	if (len < 0)
		len = 0;

	DWORD written;
	WriteFile(h, buf, (DWORD)(hdr + len), &written, NULL);
	CloseHandle(h);
}

/*
 * CLSID_ZfsVssProvider = {89300202-3CAE-4584-B38F-E72F2B3A2FBF}
 * Used as both the COM CLSID and the VSS ProviderID.
 */
static const GUID CLSID_ZfsVssProvider = {
		ZFS_VSS_PROVIDER_GUID_BINARY
};

static LONG g_server_locks = 0;
static LONG g_obj_count    = 0;

/* ------------------------------------------------------------------ */
/* Pending snapshot state (filled by BeginPrepareSnapshot)             */
/* ------------------------------------------------------------------ */

#define	MAX_PENDING	32

struct PendingSnap {
	VSS_ID   SnapshotId;
	VSS_ID   SnapshotSetId;
	wchar_t  VolumeName[MAX_PATH];
	LONG     Context;
	BOOL     used;
};

/* ------------------------------------------------------------------ */
/* IVssEnumObject - simple array-backed enumerator                     */
/* ------------------------------------------------------------------ */
class ZfsVssEnumObject : public IVssEnumObject {
public:
	ZfsVssEnumObject() : m_ref(1), m_pUnkFTM(NULL), m_pos(0), m_count(0), m_capacity(0), m_props(NULL) {
		InterlockedIncrement(&g_obj_count);
	}

	~ZfsVssEnumObject() {
		if (m_pUnkFTM) m_pUnkFTM->Release();
		for (ULONG i = 0; i < m_count; i++) {
			if (m_props[i].Type != VSS_OBJECT_SNAPSHOT)
				continue;
			VSS_SNAPSHOT_PROP &s = m_props[i].Obj.Snap;
			CoTaskMemFree(s.m_pwszSnapshotDeviceObject);
			CoTaskMemFree(s.m_pwszOriginalVolumeName);
			CoTaskMemFree(s.m_pwszOriginatingMachine);
			CoTaskMemFree(s.m_pwszServiceMachine);
			CoTaskMemFree(s.m_pwszExposedName);
			CoTaskMemFree(s.m_pwszExposedPath);
		}
		delete[] m_props;
		InterlockedDecrement(&g_obj_count);
	}

	BOOL Init(ULONG n) {
		/* Always allocate at least 4096 slots to cover all ZFS snaps */
		m_capacity = n > 4096 ? n : 4096;
		m_props = new(std::nothrow) VSS_OBJECT_PROP[m_capacity];
		if (!m_props)
			return (FALSE);
		ZeroMemory(m_props, m_capacity * sizeof (VSS_OBJECT_PROP));
		m_count = 0;
		CoCreateFreeThreadedMarshaler(
			static_cast<IUnknown *>(static_cast<IVssEnumObject *>(this)),
			&m_pUnkFTM
		);
		return (TRUE);
	}

	void Add(const VSS_OBJECT_PROP &p) {
		if (m_count < m_capacity)
			m_props[m_count++] = p;
	}

	STDMETHOD_(ULONG, AddRef)() { return (InterlockedIncrement(&m_ref)); }
	STDMETHOD_(ULONG, Release)() {
		LONG r = InterlockedDecrement(&m_ref);
		if (r == 0) delete this;
		return (r);
	}
	STDMETHOD(QueryInterface)(REFIID riid, void **ppv) {
		if (riid == IID_IUnknown || riid == IID_IVssEnumObject) {
			*ppv = static_cast<IVssEnumObject *>(this);
			AddRef();
			return (S_OK);
		} else if (riid == IID_IMarshal && m_pUnkFTM != NULL) {
			return m_pUnkFTM->QueryInterface(riid, ppv);
		} else {
			*ppv = NULL;
			return (E_NOINTERFACE);
		}
	}
	STDMETHOD(Next)(ULONG celt, VSS_OBJECT_PROP *rgelt,
	    ULONG *pceltFetched) {
		ULONG f = 0;
		while (f < celt && m_pos < m_count) {
			rgelt[f] = m_props[m_pos];
			/*
			 * VSS takes ownership of the CoTaskMem strings in the
			 * returned VSS_SNAPSHOT_PROP.  NULL them out in our
			 * copy so the destructor does not double-free them.
			 */
			if (m_props[m_pos].Type == VSS_OBJECT_SNAPSHOT) {
				VSS_SNAPSHOT_PROP &s =
				    m_props[m_pos].Obj.Snap;
				vss_log("magic number 123456 and count %ld\n",
					s.m_lSnapshotsCount);
				vss_log("Next[%lu]: device=%ls vol=%ls"
				    " machine=%ls svc=%ls"
				    " expname=%ls exppath=%ls"
				    " attrs=0x%lx status=%d count=%ld\n",
				    m_pos,
				    s.m_pwszSnapshotDeviceObject
				    ? s.m_pwszSnapshotDeviceObject : L"(null)",
				    s.m_pwszOriginalVolumeName
				    ? s.m_pwszOriginalVolumeName : L"(null)",
				    s.m_pwszOriginatingMachine
				    ? s.m_pwszOriginatingMachine : L"(null)",
				    s.m_pwszServiceMachine
				    ? s.m_pwszServiceMachine : L"(null)",
				    s.m_pwszExposedName
				    ? s.m_pwszExposedName : L"(null)",
				    s.m_pwszExposedPath
				    ? s.m_pwszExposedPath : L"(null)",
				    (ULONG)s.m_lSnapshotAttributes,
				    (int)s.m_eStatus,
				    s.m_lSnapshotsCount);
				s.m_pwszSnapshotDeviceObject = NULL;
				s.m_pwszOriginalVolumeName   = NULL;
				s.m_pwszOriginatingMachine   = NULL;
				s.m_pwszServiceMachine       = NULL;
				s.m_pwszExposedName          = NULL;
				s.m_pwszExposedPath          = NULL;
			}
			m_pos++;
			f++;
		}
		if (pceltFetched) *pceltFetched = f;
		vss_log("Next: celt=%lu fetched=%lu pos=%lu/%lu\n",
		    celt, f, m_pos, m_count);
		return (f == celt ? S_OK : S_FALSE);
	}
	STDMETHOD(Skip)(ULONG celt) {
		m_pos = m_pos + celt < m_count ? m_pos + celt : m_count;
		return (S_OK);
	}
	STDMETHOD(Reset)() { m_pos = 0; return (S_OK); }
	STDMETHOD(Clone)(IVssEnumObject **ppEnum) {
		vss_log("Clone: called, count=%lu pos=%lu\n", m_count, m_pos);
		if (!ppEnum)
			return (E_INVALIDARG);
		/*
		 * The VSS coordinator calls Clone() to snapshot the enumerator
		 * state before aggregating results from multiple providers.
		 * Create a new object with the same props and position.
		 */
		ZfsVssEnumObject *p = new(std::nothrow) ZfsVssEnumObject();
		if (!p)
			return (E_OUTOFMEMORY);
		if (!p->Init(m_count)) {
			p->Release();
			return (E_OUTOFMEMORY);
		}
		for (ULONG i = 0; i < m_count; i++) {
			VSS_OBJECT_PROP op = m_props[i];
			if (op.Type == VSS_OBJECT_SNAPSHOT) {
				VSS_SNAPSHOT_PROP &s = op.Obj.Snap;
				/* Deep-copy all CoTaskMem strings */
#define	DUPWSZ(f) \
	if (s.f) { \
		size_t _n = (wcslen(s.f) + 1) * sizeof(wchar_t); \
		s.f = (VSS_PWSZ)CoTaskMemAlloc(_n); \
		if (s.f) memcpy(s.f, m_props[i].Obj.Snap.f, _n); \
	}
				DUPWSZ(m_pwszSnapshotDeviceObject)
				DUPWSZ(m_pwszOriginalVolumeName)
				DUPWSZ(m_pwszOriginatingMachine)
				DUPWSZ(m_pwszServiceMachine)
				DUPWSZ(m_pwszExposedName)
				DUPWSZ(m_pwszExposedPath)
#undef DUPWSZ
			}
			p->m_props[i] = op;
			p->m_count++;
		}
		p->m_pos = m_pos;
		*ppEnum = p;
		return (S_OK);
	}

	/* Allow Clone() to access internals of another instance */
	friend class ZfsVssEnumObject;

private:
	LONG             m_ref;
	ULONG            m_pos;
	ULONG            m_count;
	ULONG            m_capacity;
	VSS_OBJECT_PROP *m_props;
	IUnknown *m_pUnkFTM;
};

/* ------------------------------------------------------------------ */
/* Helper: fill VSS_SNAPSHOT_PROP from the registry                    */
/* ------------------------------------------------------------------ */

static HRESULT
load_snap_prop(const VSS_ID &sid, VSS_SNAPSHOT_PROP *p)
{
	wchar_t volume[MAX_PATH];
	char    zfsname[256];
	uint64_t zfsguid;
	GUID    setid;
	LONGLONG ts;
	LONG    attrs;

	if (vss_reg_load(&sid, volume, MAX_PATH, zfsname, 256,
	    &zfsguid, &setid, &ts, &attrs) != 0)
		return (VSS_E_OBJECT_NOT_FOUND);

	ZeroMemory(p, sizeof (*p));
	p->m_SnapshotId         = sid;
	p->m_SnapshotSetId      = setid;
	p->m_lSnapshotsCount    = 1;
	p->m_ProviderId         = CLSID_ZfsVssProvider;
	p->m_tsCreationTimestamp = ts;
	p->m_eStatus            = VSS_SS_CREATED;
	p->m_lSnapshotAttributes = attrs |
	    VSS_VOLSNAP_ATTR_PERSISTENT |
	    VSS_VOLSNAP_ATTR_NO_AUTO_RELEASE |
	    VSS_VOLSNAP_ATTR_CLIENT_ACCESSIBLE |
	    VSS_VOLSNAP_ATTR_NO_WRITERS |
	    VSS_VOLSNAP_ATTR_DIFFERENTIAL;

	/* Device path that exposes the frozen ZFS snapshot */
	wchar_t devname[128];
#if 1
	_snwprintf(devname, 128,
	    L"\\\\?\\GLOBALROOT\\Device\\ZfsSnapshot%016I64x",
	    (unsigned long long)zfsguid);
#else
	_snwprintf(devname, 128,
		L"\\Device\\ZfsSnapshot%016I64x",
		(unsigned long long)zfsguid);
#endif

	p->m_pwszSnapshotDeviceObject =
	    (VSS_PWSZ)CoTaskMemAlloc(
	    (wcslen(devname) + 1) * sizeof (wchar_t));
	if (!p->m_pwszSnapshotDeviceObject)
		return (E_OUTOFMEMORY);
	wcscpy(p->m_pwszSnapshotDeviceObject, devname);

	p->m_pwszOriginalVolumeName =
	    (VSS_PWSZ)CoTaskMemAlloc(
	    (wcslen(volume) + 1) * sizeof (wchar_t));
	if (!p->m_pwszOriginalVolumeName) {
		CoTaskMemFree(p->m_pwszSnapshotDeviceObject);
		return (E_OUTOFMEMORY);
	}
	wcscpy(p->m_pwszOriginalVolumeName, volume);

	wchar_t machine[MAX_COMPUTERNAME_LENGTH + 1];
	DWORD mlen = MAX_COMPUTERNAME_LENGTH + 1;
	GetComputerNameW(machine, &mlen);

	p->m_pwszOriginatingMachine =
	    (VSS_PWSZ)CoTaskMemAlloc((mlen + 1) * sizeof (wchar_t));
	if (p->m_pwszOriginatingMachine)
		wcscpy(p->m_pwszOriginatingMachine, machine);

	p->m_pwszServiceMachine =
	    (VSS_PWSZ)CoTaskMemAlloc((mlen + 1) * sizeof (wchar_t));
	if (p->m_pwszServiceMachine)
		wcscpy(p->m_pwszServiceMachine, machine);

	/*
	 * Exposed name/path — read back from the registry if the coordinator
	 * has previously called SetSnapshotProperty to record them.  Fall back
	 * to empty strings so the fields are never NULL.
	 */
	{
		HKEY hk = NULL;
		wchar_t keyname[256];
		wchar_t sub[64];
		/* reuse the same key layout as vss_registry.c */
		_snwprintf(sub, 64,
		    L"%08X-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X",
		    sid.Data1, sid.Data2, sid.Data3,
		    sid.Data4[0], sid.Data4[1],
		    sid.Data4[2], sid.Data4[3], sid.Data4[4],
		    sid.Data4[5], sid.Data4[6], sid.Data4[7]);
		_snwprintf(keyname, 256, L"%s\\%s",
		    ZFS_VSS_REG_ROOT, sub);
		wchar_t expname[MAX_PATH] = L"";
		wchar_t exppath[MAX_PATH] = L"";
		if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, keyname, 0,
		    KEY_READ, &hk) == ERROR_SUCCESS) {
			DWORD sz = (DWORD)(MAX_PATH * sizeof (wchar_t));
			RegQueryValueExW(hk, ZFS_VSS_REG_EXPNAME,
			    NULL, NULL, (BYTE *)expname, &sz);
			sz = (DWORD)(MAX_PATH * sizeof (wchar_t));
			RegQueryValueExW(hk, ZFS_VSS_REG_EXPPATH,
			    NULL, NULL, (BYTE *)exppath, &sz);
			RegCloseKey(hk);
		}
		if (wcslen(expname) > 0) {
			p->m_pwszExposedName = (VSS_PWSZ)CoTaskMemAlloc(
			    (wcslen(expname) + 1) * sizeof (wchar_t));
			if (p->m_pwszExposedName)
				wcscpy(p->m_pwszExposedName, expname);
		} else {
			p->m_pwszExposedName = NULL;
		}
		if (wcslen(exppath) > 0) {
			p->m_pwszExposedPath = (VSS_PWSZ)CoTaskMemAlloc(
			    (wcslen(exppath) + 1) * sizeof (wchar_t));
			if (p->m_pwszExposedPath)
				wcscpy(p->m_pwszExposedPath, exppath);
		} else {
			p->m_pwszExposedPath = NULL;
		}
	}

	return (S_OK);
}

/* ------------------------------------------------------------------ */
/* Helper: build VSS_SNAPSHOT_PROP from a ZFS snapshot handle          */
/* ------------------------------------------------------------------ */

/*
 * Map a ZFS ds_guid (uint64_t) to a stable VSS_ID (GUID) so VSS can
 * refer back to the snapshot via GetSnapshotProperties.
 * Layout: { (DWORD)guid_lo32, (WORD)guid_hi16, 0x4000,
 *           { 0x80, 'Z','F','S', b4,b5,b6,b7 } }
 * This is deterministic and reversible: given a VSS_ID we can recover
 * the original ds_guid by reading Data1 | ((uint64_t)Data2 << 32).
 */
static void
zfsguid_to_vssid(uint64_t zg, VSS_ID *out)
{
	ZeroMemory(out, sizeof (*out));
	out->Data1 = (DWORD)(zg & 0xffffffff);
	out->Data2 = (WORD)((zg >> 32) & 0xffff);
	out->Data3 = (WORD)(0x4000 | ((zg >> 48) & 0x0fff));
	out->Data4[0] = 0x80;
	out->Data4[1] = 'Z';
	out->Data4[2] = 'F';
	out->Data4[3] = 'S';
	/* remaining 4 bytes from upper guid bits */
	out->Data4[4] = (BYTE)((zg >> 32) & 0xff);
	out->Data4[5] = (BYTE)((zg >> 40) & 0xff);
	out->Data4[6] = (BYTE)((zg >> 48) & 0xff);
	out->Data4[7] = (BYTE)((zg >> 56) & 0xff);
}

/*
 * Fill a VSS_SNAPSHOT_PROP from a live zfs_handle_t snapshot, without
 * touching the registry.  The SnapshotId is derived from the ZFS ds_guid
 * so it is stable across provider restarts.
 *
 * volname must be the canonical "\\?\Volume{...}\" string for the parent
 * dataset (caller supplies; may be empty if not known — VSS uses it only
 * for display).
 */
static HRESULT
snap_prop_from_zfs(zfs_handle_t *zhp, const wchar_t *volname,
    VSS_SNAPSHOT_PROP *p)
{
	uint64_t zg = zfs_prop_get_int(zhp, ZFS_PROP_GUID);
	uint64_t ctime = zfs_prop_get_int(zhp, ZFS_PROP_CREATION);

	ZeroMemory(p, sizeof (*p));

	zfsguid_to_vssid(zg, &p->m_SnapshotId);
	/* SnapshotSetId: same derivation with a different marker byte */
	zfsguid_to_vssid(zg ^ 0x5a5a5a5a5a5a5a5aULL, &p->m_SnapshotSetId);

	p->m_lSnapshotsCount     = 1;
	p->m_ProviderId          = CLSID_ZfsVssProvider;
	/* VSS timestamps are in 100-ns intervals since 1601-01-01 */
	p->m_tsCreationTimestamp = ((LONGLONG)ctime + 11644473600LL) * 10000000LL;
	p->m_eStatus             = VSS_SS_CREATED;
	p->m_lSnapshotAttributes =
	    VSS_VOLSNAP_ATTR_PERSISTENT |
	    VSS_VOLSNAP_ATTR_NO_AUTO_RELEASE |
	    VSS_VOLSNAP_ATTR_CLIENT_ACCESSIBLE |
	    VSS_VOLSNAP_ATTR_NO_WRITERS |
	    VSS_VOLSNAP_ATTR_DIFFERENTIAL;

	wchar_t devname[128];
#if 1
	_snwprintf(devname, 128,
	    L"\\\\?\\GLOBALROOT\\Device\\ZfsSnapshot%016I64x",
	    (unsigned long long)zg);
#else
	_snwprintf(devname, 128,
		L"\\Device\\ZfsSnapshot%016I64x",
		(unsigned long long)zg);
#endif

	p->m_pwszSnapshotDeviceObject =
	    (VSS_PWSZ)CoTaskMemAlloc(
	    (wcslen(devname) + 1) * sizeof (wchar_t));
	if (!p->m_pwszSnapshotDeviceObject)
		return (E_OUTOFMEMORY);
	wcscpy(p->m_pwszSnapshotDeviceObject, devname);

	p->m_pwszOriginalVolumeName =
	    (VSS_PWSZ)CoTaskMemAlloc(
	    (wcslen(volname) + 1) * sizeof (wchar_t));
	if (!p->m_pwszOriginalVolumeName) {
		CoTaskMemFree(p->m_pwszSnapshotDeviceObject);
		return (E_OUTOFMEMORY);
	}
	wcscpy(p->m_pwszOriginalVolumeName, volname);

	wchar_t machine[MAX_COMPUTERNAME_LENGTH + 1];
	DWORD mlen = MAX_COMPUTERNAME_LENGTH + 1;
	GetComputerNameW(machine, &mlen);

	p->m_pwszOriginatingMachine =
	    (VSS_PWSZ)CoTaskMemAlloc((mlen + 1) * sizeof (wchar_t));
	if (p->m_pwszOriginatingMachine)
		wcscpy(p->m_pwszOriginatingMachine, machine);

	p->m_pwszServiceMachine =
	    (VSS_PWSZ)CoTaskMemAlloc((mlen + 1) * sizeof (wchar_t));
	if (p->m_pwszServiceMachine)
		wcscpy(p->m_pwszServiceMachine, machine);

	p->m_pwszExposedName = NULL;
	p->m_pwszExposedPath = NULL;

	return (S_OK);
}

/* ------------------------------------------------------------------ */
/* Helper: find ZFS dataset for a given volume name                    */
/* ------------------------------------------------------------------ */

/*
 * Replicate the kernel's zfs_vfs_uuid_gen / zfs_vfs_uuid_unparse to
 * compute the deterministic Windows volume GUID for a ZFS dataset name.
 * The GUID is a version-3 (MD5) UUID with namespace
 * {50670853-FBD2-4EC3-9802-73D847BF7E62}.
 *
 * Returns the GUID as "\\?\Volume{xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx}\".
 */
static void
ds_to_vol_guid(const char *dsname, wchar_t *out, int outchars)
{
	/* Same namespace as the kernel */
	static const unsigned char ns[16] = {
	    0x50, 0x67, 0x08, 0x53,
	    0xfb, 0xd2, 0x4e, 0xc3,
	    0x98, 0x02,
	    0x73, 0xd8, 0x47, 0xbf, 0x7e, 0x62
	};

	/* MD5 via Windows CryptAPI */
	HCRYPTPROV hprov = 0;
	HCRYPTHASH hhash = 0;
	unsigned char uuid[16] = {0};
	DWORD hashlen = 16;

	if (CryptAcquireContextW(&hprov, NULL, NULL, PROV_RSA_FULL,
	    CRYPT_VERIFYCONTEXT)) {
		if (CryptCreateHash(hprov, CALG_MD5, 0, 0, &hhash)) {
			CryptHashData(hhash, ns, 16, 0);
			CryptHashData(hhash, (const BYTE *)dsname,
			    (DWORD)strlen(dsname), 0);
			CryptGetHashParam(hhash, HP_HASHVAL, uuid,
			    &hashlen, 0);
			CryptDestroyHash(hhash);
		}
		CryptReleaseContext(hprov, 0);
	}

	/* Set UUID version 3 bits */
	uuid[6] = (uuid[6] & 0x0F) | 0x30;
	uuid[8] = (uuid[8] & 0x3F) | 0x80;

	/*
	 * Build the ZFS device volume name from the UUID.
	 * Then ask MountMgr for the canonical GUID it assigned when it
	 * mounted this device — that is what VSS passes us as volname.
	 */
	wchar_t zfs_vol[MAX_PATH];
	_snwprintf(zfs_vol, MAX_PATH,
	    L"\\\\?\\Volume{%02x%02x%02x%02x-%02x%02x-%02x%02x-"
	    L"%02x%02x-%02x%02x%02x%02x%02x%02x}\\",
	    uuid[0], uuid[1], uuid[2], uuid[3],
	    uuid[4], uuid[5], uuid[6], uuid[7],
	    uuid[8], uuid[9],
	    uuid[10], uuid[11], uuid[12], uuid[13], uuid[14], uuid[15]);

	/*
	 * GetVolumeNameForVolumeMountPointW translates our ZFS device
	 * volume name to the MountMgr-assigned canonical GUID, which is
	 * the GUID that VSS uses.
	 */
	if (!GetVolumeNameForVolumeMountPointW(zfs_vol, out, outchars))
		wcsncpy(out, zfs_vol, outchars);
}

struct FindCtx {
	const wchar_t *vol_guid;	/* canonical "\\?\Volume{...}\" */
	char found_ds[256];
};

static int
find_dataset_cb(zfs_handle_t *zhp, void *arg)
{
	struct FindCtx *ctx = (struct FindCtx *)arg;
	char mp[MAXPATHLEN];
	int rc;

	if (zfs_prop_get(zhp, ZFS_PROP_MOUNTPOINT, mp, sizeof (mp),
	    NULL, NULL, 0, B_FALSE) == 0) {
		/*
		 * Compute the deterministic Windows volume GUID for this
		 * dataset name (same MD5/UUID-v3 algorithm as the kernel's
		 * zfs_vfs_uuid_gen) and compare against the target GUID.
		 */
		wchar_t ds_guid[MAX_PATH];
		ds_to_vol_guid(zfs_get_name(zhp), ds_guid, MAX_PATH);

		vss_log("find_dataset_cb: ds=%s guid=%ls vs %ls\n",
		    zfs_get_name(zhp), ds_guid, ctx->vol_guid);

		if (_wcsicmp(ds_guid, ctx->vol_guid) == 0) {
			strncpy(ctx->found_ds, zfs_get_name(zhp), 256);
			zfs_close(zhp);
			return (1);
		}

		/*
		 * Fallback 1: the ZFS mountpoint property (mp) may be a
		 * Windows drive path.  Ask Windows for the canonical volume
		 * GUID at that mount point.
		 */
		{
			wchar_t mp_w[MAX_PATH];
			if (MultiByteToWideChar(CP_UTF8, 0, mp, -1,
			    mp_w, MAX_PATH) > 0) {
				/* Convert forward slashes to backslashes */
				for (wchar_t *p = mp_w; *p; p++)
					if (*p == L'/') *p = L'\\';
				int mplen = (int)wcslen(mp_w);
				if (mplen > 0 && mp_w[mplen - 1] != L'\\' &&
				    mplen + 2 < MAX_PATH) {
					mp_w[mplen]   = L'\\';
					mp_w[mplen+1] = L'\0';
				}
				wchar_t mp_guid[MAX_PATH];
				if (GetVolumeNameForVolumeMountPointW(mp_w,
				    mp_guid, MAX_PATH)) {
					vss_log("find_dataset_cb: mp=%ls "
					    "mp_guid=%ls vs %ls\n",
					    mp_w, mp_guid, ctx->vol_guid);
					if (_wcsicmp(mp_guid,
					    ctx->vol_guid) == 0) {
						strncpy(ctx->found_ds,
						    zfs_get_name(zhp), 256);
						zfs_close(zhp);
						return (1);
					}
				}
			}
		}

		/*
		 * Fallback 2: on Windows, ZFS datasets use drive letters.
		 * The ZFS mountpoint property stores "/" (Unix style) while the
		 * actual mount is tracked via MountMgr.  Query the actual mount
		 * path via zfs_is_mounted(), which returns the Windows path
		 * (e.g. "E:/") derived from GetVolumePathNamesForVolumeName.
		 */
		{
			char *actual = NULL;
			if (zfs_is_mounted(zhp, &actual) && actual != NULL) {
				wchar_t tmp_w[MAX_PATH];
				if (MultiByteToWideChar(CP_UTF8, 0, actual, -1,
				    tmp_w, MAX_PATH) > 0) {
					/* libspl uses '/' separators */
					for (wchar_t *p = tmp_w; *p; p++)
						if (*p == L'/') *p = L'\\';
					int tmplen = (int)wcslen(tmp_w);
					if (tmplen > 0 &&
					    tmp_w[tmplen-1] != L'\\' &&
					    tmplen + 2 < MAX_PATH) {
						tmp_w[tmplen]   = L'\\';
						tmp_w[tmplen+1] = L'\0';
					}
					wchar_t tmp_guid[MAX_PATH];
					if (GetVolumeNameForVolumeMountPointW(
					    tmp_w, tmp_guid, MAX_PATH)) {
						vss_log("find_dataset_cb:"
						    " is_mounted mp=%ls"
						    " guid=%ls vs %ls\n",
						    tmp_w, tmp_guid,
						    ctx->vol_guid);
						if (_wcsicmp(tmp_guid,
						    ctx->vol_guid) == 0) {
							strncpy(ctx->found_ds,
							    zfs_get_name(zhp),
							    256);
							free(actual);
							zfs_close(zhp);
							return (1);
						}
					}
				}
				free(actual);
			}
		}
	}

	rc = zfs_iter_filesystems(zhp, find_dataset_cb, ctx);
	zfs_close(zhp);
	return (rc);
}

/*
 * Resolve a VSS volume name ("\\?\Volume{guid}\") to the ZFS dataset
 * mounted there.  Returns 0 on success, -1 if not found or not ZFS.
 */
/*
 * Resolve a VSS volume name ("\\?\Volume{guid}\") to the ZFS dataset
 * mounted there by comparing the volume GUID against the deterministic
 * GUID derived from each dataset name.  Returns 0 on success.
 */
static int
volume_to_zfs_dataset(libzfs_handle_t *lzh, const wchar_t *volname,
    char *ds_out, int ds_len)
{
	vss_log("volume_to_zfs_dataset: looking for %ls\n", volname);

	struct FindCtx ctx;
	ctx.vol_guid = volname;	/* already in canonical \\?\Volume{...}\ form */
	ctx.found_ds[0] = '\0';

	zfs_iter_root(lzh, find_dataset_cb, &ctx);

	if (ctx.found_ds[0] == '\0') {
		vss_log("volume_to_zfs_dataset: no matching dataset\n");
		return (-1);
	}

	strncpy(ds_out, ctx.found_ds, ds_len);
	return (0);
}

/* ------------------------------------------------------------------ */
/* Helper: iterate snapshots of a dataset and add to enum              */
/* ------------------------------------------------------------------ */

struct SnapEnumCtx {
	ZfsVssEnumObject *pEnum;
	const wchar_t    *volname;
	const GUID       *reg_ids;
	int               reg_count;
	int               added;
};

static int
snap_enum_cb(zfs_handle_t *zhp, void *arg)
{
	struct SnapEnumCtx *ctx = (struct SnapEnumCtx *)arg;

	uint64_t zg = zfs_prop_get_int(zhp, ZFS_PROP_GUID);

	/* Skip if already returned from the registry pass */
	VSS_ID sid;
	zfsguid_to_vssid(zg, &sid);
	for (int i = 0; i < ctx->reg_count; i++) {
		if (memcmp(&ctx->reg_ids[i], &sid, sizeof (VSS_ID)) == 0) {
			zfs_close(zhp);
			return (0);
		}
	}

	VSS_OBJECT_PROP op;
	op.Type = VSS_OBJECT_SNAPSHOT;
	HRESULT hr = snap_prop_from_zfs(zhp, ctx->volname, &op.Obj.Snap);
	if (SUCCEEDED(hr)) {
		ctx->pEnum->Add(op);
		ctx->added++;
		vss_log("snap_enum_cb: added snap %s guid=%016I64x\n",
		    zfs_get_name(zhp), (unsigned long long)zg);
	}

	zfs_close(zhp);
	return (0);
}

/* ------------------------------------------------------------------ */
/* Helper: find a snapshot by ds_guid across all pools                 */
/* ------------------------------------------------------------------ */

struct FindByGuidCtx {
	uint64_t target_guid;
	char     found_name[256];
};

struct AllSnapCtx {
	struct SnapEnumCtx *sc;
	libzfs_handle_t    *lzh;
};

static int
all_snap_child_cb(zfs_handle_t *zhp, void *arg)
{
	struct AllSnapCtx *a = (struct AllSnapCtx *)arg;
	wchar_t vg[MAX_PATH];
	ds_to_vol_guid(zfs_get_name(zhp), vg, MAX_PATH);
	a->sc->volname = vg;
	zfs_iter_snapshots(zhp, B_FALSE, snap_enum_cb, a->sc, 0, 0);
	zfs_close(zhp);
	return (0);
}

static int
all_snap_root_cb(zfs_handle_t *zhp, void *arg)
{
	struct AllSnapCtx *a = (struct AllSnapCtx *)arg;
	wchar_t vg[MAX_PATH];
	ds_to_vol_guid(zfs_get_name(zhp), vg, MAX_PATH);
	vss_log("all_snap_root_cb: ds=%s vol=%ls\n", zfs_get_name(zhp), vg);
	a->sc->volname = vg;
	zfs_iter_snapshots(zhp, B_FALSE, snap_enum_cb, a->sc, 0, 0);
	zfs_iter_filesystems(zhp, all_snap_child_cb, a);
	zfs_close(zhp);
	return (0);
}

/* Stops at first snapshot found; used by IsVolumeSnapshotted */
static int
has_snap_cb(zfs_handle_t *zhp, void *arg)
{
	zfs_close(zhp);
	*(int *)arg = 1;
	return (1); /* stop iteration */
}

static int
find_snap_by_guid_cb(zfs_handle_t *zhp, void *arg)
{
	struct FindByGuidCtx *f = (struct FindByGuidCtx *)arg;
	uint64_t g = zfs_prop_get_int(zhp, ZFS_PROP_GUID);
	if (g == f->target_guid) {
		strncpy(f->found_name, zfs_get_name(zhp), 255);
		zfs_close(zhp);
		return (1);
	}
	zfs_close(zhp);
	return (0);
}

static int
find_guid_in_fs_cb(zfs_handle_t *zhp, void *arg)
{
	struct FindByGuidCtx *f = (struct FindByGuidCtx *)arg;
	if (f->found_name[0]) {
		zfs_close(zhp);
		return (1);
	}
	/* check snapshots */
	zfs_iter_snapshots(zhp, B_FALSE, find_snap_by_guid_cb, f, 0, 0);
	/* recurse into children */
	if (!f->found_name[0])
		zfs_iter_filesystems(zhp, find_guid_in_fs_cb, f);
	zfs_close(zhp);
	return (f->found_name[0] ? 1 : 0);
}

/* ------------------------------------------------------------------ */
/* Undocumented VSS expose interfaces                                   */
/* ------------------------------------------------------------------ */
/*
 * IID {AF86E2E0-B12D-4C6A-9C5A-D7AA65101E90}: discovered by logging
 * unknown QueryInterface requests during DISKSHADOW "expose" testing.
 * VSS queries this interface FIRST during expose; returning E_NOINTERFACE
 * causes VSS_E_UNEXPECTED_PROVIDER_ERROR without even reaching the
 * ExposeSnapshotVolume interface ({B076ED90}).
 *
 * The method list and signatures are unknown.  We expose stubs that log
 * their vtable ordinal and return S_OK so we can discover at run-time
 * which method(s) VSS actually calls.  On x64 Windows all calling
 * conventions are caller-cleanup so void-parameter stubs are safe
 * regardless of the real argument count.
 */
static const IID IID_IVssExposeSnapshotMgmt = {
    0xAF86E2E0, 0xB12D, 0x4C6A,
    { 0x9C, 0x5A, 0xD7, 0xAA, 0x65, 0x10, 0x1E, 0x90 }
};

class IVssExposeSnapshotMgmt : public IUnknown {
public:
    virtual HRESULT STDMETHODCALLTYPE Method1(void) = 0;
    /*
     * Method2 (vtable[4]): called by VSS during expose.
     * On x64, VSS_ID (16 bytes) is passed by pointer in RDX.
     * Signature confirmed by logging raw register values:
     *   RDX = VSS_ID*  R8 = LONG lAttributes  R9 = VSS_PWSZ wszExpose
     *   [rsp+28h] = VSS_PWSZ* ppwszExposed
     * No wszPathFromRoot parameter exists in this interface.
     */
    virtual HRESULT STDMETHODCALLTYPE Method2(
        const VSS_ID *pSnapshotId,
        LONG lAttributes,
        VSS_PWSZ wszExpose,
        VSS_PWSZ *ppwszExposed) = 0;
    virtual HRESULT STDMETHODCALLTYPE Method3(void) = 0;
    virtual HRESULT STDMETHODCALLTYPE Method4(void) = 0;
    virtual HRESULT STDMETHODCALLTYPE Method5(void) = 0;
};

/*
 * IID {B076ED90-8BDE-4E40-AAA0-892FD3C83947}: queried after the above
 * interface is satisfied.  VSS calls ExposeSnapshotVolume on this; we
 * acknowledge with S_OK (coordinator does the actual drive-letter
 * assignment via its own SYSTEM-privilege path).
 */
static const IID IID_IVssExposeSnapshot = {
    0xB076ED90, 0x8BDE, 0x4E40,
    { 0xAA, 0xA0, 0x89, 0x2F, 0xD3, 0xC8, 0x39, 0x47 }
};

class IVssExposeSnapshot : public IUnknown {
public:
    virtual HRESULT STDMETHODCALLTYPE ExposeSnapshotVolume(
        VSS_ID SnapshotId,
        LONG lAttributes,
        VSS_PWSZ wszExposed,
        VSS_PWSZ wszPathFromRoot
    ) = 0;
};

/* ------------------------------------------------------------------ */
/* ZfsVssProvider                                                       */
/* ------------------------------------------------------------------ */

class ZfsVssProvider
    : public IVssSoftwareSnapshotProvider
    , public IVssProviderCreateSnapshotSet
    , public IVssProviderNotifications
    , public IVssExposeSnapshotMgmt
    , public IVssExposeSnapshot
{
public:
	ZfsVssProvider() : m_ref(1), m_lzh(NULL), m_ctx(VSS_CTX_BACKUP) {
		ZeroMemory(m_pending, sizeof (m_pending));
		CoCreateFreeThreadedMarshaler(
			static_cast<IUnknown *>(static_cast<IVssSoftwareSnapshotProvider *>(this)),
			&m_pUnkFTM
		);
		InterlockedIncrement(&g_obj_count);
	}

	~ZfsVssProvider() {
		if (m_lzh)
			libzfs_fini(m_lzh);
		if (m_pUnkFTM) m_pUnkFTM->Release();
		InterlockedDecrement(&g_obj_count);
	}

	/* ---- IUnknown ---- */
	STDMETHOD_(ULONG, AddRef)() {
		return (InterlockedIncrement(&m_ref));
	}
	STDMETHOD_(ULONG, Release)() {
		LONG r = InterlockedDecrement(&m_ref);
		if (r == 0) delete this;
		return (r);
	}
	STDMETHOD(QueryInterface)(REFIID riid, void **ppv) {
		if (riid == IID_IUnknown) {
			*ppv = static_cast<IVssSoftwareSnapshotProvider *>(this);
		} else if (riid == IID_IVssSoftwareSnapshotProvider) {
			*ppv = static_cast<IVssSoftwareSnapshotProvider *>(this);
		} else if (riid == IID_IVssProviderCreateSnapshotSet) {
			*ppv = static_cast<IVssProviderCreateSnapshotSet *>(this);
		} else if (riid == IID_IVssProviderNotifications) {
			*ppv = static_cast<IVssProviderNotifications *>(this);
		} else if (IsEqualGUID(riid, IID_IVssExposeSnapshotMgmt)) {
			/*
			 * Returning S_OK here deadlocks: the LRPC layer tries to
			 * marshal our IVssExposeSnapshotMgmt pointer back to the
			 * coordinator using the system-registered PSFactoryBuffer
			 * ({00000320}), which blocks indefinitely waiting for type
			 * library info that does not exist for this interface.
			 * B076ED90 works because the coordinator handles it
			 * internally (no system ProxyStubClsid32 registered for it).
			 * TODO: provide a working in-process proxy/stub DLL for
			 * AF86E2E0 so the coordinator can marshal it cleanly.
			 */
			vss_log("QueryInterface: IVssExposeSnapshotMgmt"
			    " {AF86E2E0} -> E_NOINTERFACE (PSFactoryBuffer deadlock)\n");
			*ppv = NULL;
			return (E_NOINTERFACE);
		} else if (IsEqualGUID(riid, IID_IVssExposeSnapshot)) {
			vss_log("QueryInterface: IVssExposeSnapshot"
			    " {B076ED90} -> S_OK\n");
			*ppv = static_cast<IVssExposeSnapshot *>(this);
		} else if (riid == IID_IMarshal && m_pUnkFTM != NULL) {
			return m_pUnkFTM->QueryInterface(riid, ppv);
		} else {
			wchar_t guidstr[64];
			StringFromGUID2(riid, guidstr, 64);
			vss_log("QueryInterface: unknown IID %ls -> E_NOINTERFACE\n",
			    guidstr);
			*ppv = NULL;
			return (E_NOINTERFACE);
		}
		AddRef();
		return (S_OK);
	}

	/* ---- IVssSoftwareSnapshotProvider ---- */

	STDMETHOD(SetContext)(LONG lContext) {
		vss_log("SetContext: lContext=0x%lx\n", (ULONG)lContext);
		m_ctx = lContext;
		return (S_OK);
	}

	/*
	 * IsVolumeSupported - return TRUE if the volume has a ZFS dataset
	 * mounted on it, determined via libzfs iteration.  We do not rely
	 * on GetVolumeInformation filesystem type because the ZFS driver
	 * may report a compatibility alias instead of "ZFS".
	 */
	STDMETHOD(IsVolumeSupported)(VSS_PWSZ pwszVolumeName,
	    BOOL *pbSupportedByThisProvider)
	{
		vss_log("IsVolumeSupported: %ls\n", pwszVolumeName);
		if (!pbSupportedByThisProvider)
			return (E_INVALIDARG);
		*pbSupportedByThisProvider = FALSE;

		if (!m_lzh) {
			m_lzh = libzfs_init();
			if (!m_lzh) {
				vss_log("IsVolumeSupported: libzfs_init failed\n");
				return (S_OK);
			}
		}

		char ds[256];
		if (volume_to_zfs_dataset(m_lzh, pwszVolumeName,
		    ds, sizeof (ds)) == 0) {
			*pbSupportedByThisProvider = TRUE;
			vss_log("IsVolumeSupported: TRUE (ds=%s)\n", ds);
		} else {
			vss_log("IsVolumeSupported: FALSE (no ZFS dataset)\n");
		}

		return (S_OK);
	}

	/*
	 * IsVolumeSnapshotted - report whether any of our snapshots cover
	 * this volume.
	 */
	STDMETHOD(IsVolumeSnapshotted)(VSS_PWSZ pwszVolumeName,
	    BOOL *pbSnapshotsPresent, LONG *plSnapshotCompatibility)
	{
		vss_log("IsVolumeSnapshotted: %ls\n", pwszVolumeName);
		if (!pbSnapshotsPresent || !plSnapshotCompatibility)
			return (E_INVALIDARG);

		*pbSnapshotsPresent = FALSE;
		*plSnapshotCompatibility = 0;

		/* Check registry (VSS-created snapshots) */
		GUID ids[256];
		int n = vss_reg_enum(ids, 256);
		for (int i = 0; i < n; i++) {
			wchar_t vol[MAX_PATH];
			char    zn[256];
			uint64_t zg;
			GUID    setid;
			LONGLONG ts;
			LONG    attrs;

			if (vss_reg_load(&ids[i], vol, MAX_PATH,
			    zn, 256, &zg, &setid, &ts, &attrs) != 0)
				continue;
			if (_wcsicmp(vol, pwszVolumeName) == 0) {
				*pbSnapshotsPresent = TRUE;
				vss_log("IsVolumeSnapshotted: TRUE (registry)\n");
				return (S_OK);
			}
		}

		/*
		 * Also check ZFS native snapshots: find the dataset for this
		 * volume and check if it has any snapshots.  VSS calls this
		 * before Query(); if we return FALSE, Query() is never called
		 * and Previous Versions shows nothing even though we have
		 * ZFS snapshots.
		 */
		if (!m_lzh)
			m_lzh = libzfs_init();
		if (m_lzh) {
			char ds[256] = {0};
			if (volume_to_zfs_dataset(m_lzh, pwszVolumeName,
			    ds, sizeof (ds)) == 0) {
				zfs_handle_t *zhp = zfs_open(m_lzh, ds,
				    ZFS_TYPE_FILESYSTEM);
				if (zhp) {
					/*
					 * Check if any snapshot exists by trying
					 * to iterate — snap_count_cb stops at
					 * the first snapshot found.
					 */
					int found = 0;
					zfs_iter_snapshots(zhp, B_FALSE,
					    has_snap_cb, &found, 0, 0);
					zfs_close(zhp);
					if (found) {
						*pbSnapshotsPresent = TRUE;
						/* 0 = no special restrictions */
						vss_log("IsVolumeSnapshotted:"
						    " TRUE (ZFS native, ds=%s)"
						    "\n", ds);
						return (S_OK);
					}
				}
			}
		}

		vss_log("IsVolumeSnapshotted: FALSE\n");
		return (S_OK);
	}

	/*
	 * BeginPrepareSnapshot - record (SnapshotId, VolumeName) for later
	 * use by CommitSnapshots.
	 */
	STDMETHOD(BeginPrepareSnapshot)(VSS_ID SnapshotSetId,
	    VSS_ID SnapshotId, VSS_PWSZ pwszVolumeName, LONG lNewContext)
	{
		wchar_t snapguid[64], setguid[64];
		StringFromGUID2(SnapshotId, snapguid, 64);
		StringFromGUID2(SnapshotSetId, setguid, 64);
		vss_log("BeginPrepareSnapshot: vol=%ls ctx=0x%lx"
		    " snapid=%ls setid=%ls\n",
		    pwszVolumeName, (ULONG)lNewContext, snapguid, setguid);
		for (int i = 0; i < MAX_PENDING; i++) {
			if (m_pending[i].used)
				continue;
			m_pending[i].SnapshotId    = SnapshotId;
			m_pending[i].SnapshotSetId = SnapshotSetId;
			wcsncpy(m_pending[i].VolumeName, pwszVolumeName,
			    MAX_PATH);
			m_pending[i].Context = lNewContext;
			m_pending[i].used = TRUE;
			return (S_OK);
		}
		return (VSS_E_MAXIMUM_NUMBER_OF_SNAPSHOTS_REACHED);
	}

	/*
	 * GetSnapshotProperties - fill VSS_SNAPSHOT_PROP from registry.
	 * Strings must be allocated with CoTaskMemAlloc; VSS frees them.
	 */
	STDMETHOD(GetSnapshotProperties)(VSS_ID SnapshotId,
	    VSS_SNAPSHOT_PROP *pProp)
	{
		wchar_t guidstr[64];
		StringFromGUID2(SnapshotId, guidstr, 64);
		vss_log("GetSnapshotProperties: id=%ls\n", guidstr);
		if (!pProp)
			return (E_INVALIDARG);

		/* Try registry first (VSS-created snapshots) */
		HRESULT hr = load_snap_prop(SnapshotId, pProp);
		if (SUCCEEDED(hr)) {
			vss_log("GetSnapshotProperties: registry hit"
			    " dev=%ls vol=%ls attrs=0x%lx status=%d"
			    " count=%ld\n",
			    pProp->m_pwszSnapshotDeviceObject
			    ? pProp->m_pwszSnapshotDeviceObject : L"(null)",
			    pProp->m_pwszOriginalVolumeName
			    ? pProp->m_pwszOriginalVolumeName : L"(null)",
			    (ULONG)pProp->m_lSnapshotAttributes,
			    (int)pProp->m_eStatus,
			    pProp->m_lSnapshotsCount);
			return (hr);
		}
		vss_log("GetSnapshotProperties: not in registry"
		    " (hr=0x%lx), trying libzfs path\n", (ULONG)hr);

		/*
		 * Not in the registry — the SnapshotId may be one we
		 * synthesised from a ZFS ds_guid in Query().  Recover
		 * the guid from the ID layout used in zfsguid_to_vssid():
		 *   guid = Data1 | ((uint64_t)Data2 << 32)
		 *          | ((uint64_t)(Data3 & 0x0fff) << 48)
		 * and look it up via libzfs.
		 */
		if (!m_lzh)
			m_lzh = libzfs_init();
		if (!m_lzh) {
			vss_log("GetSnapshotProperties: libzfs_init failed\n");
			return (VSS_E_OBJECT_NOT_FOUND);
		}

		uint64_t zg =
		    (uint64_t)SnapshotId.Data1 |
		    ((uint64_t)SnapshotId.Data2 << 32) |
		    ((uint64_t)(SnapshotId.Data3 & 0x0fff) << 48);

		/* Verify the marker bytes to avoid false positives */
		if (SnapshotId.Data4[0] != 0x80 ||
		    SnapshotId.Data4[1] != 'Z' ||
		    SnapshotId.Data4[2] != 'F' ||
		    SnapshotId.Data4[3] != 'S') {
			vss_log("GetSnapshotProperties: not a ZFS synthetic ID\n");
			return (VSS_E_OBJECT_NOT_FOUND);
		}

		/*
		 * Find the snapshot by GUID via a root iteration using
		 * the static find_guid_in_fs_cb / find_snap_by_guid_cb
		 * helpers defined above the class.
		 */
		struct FindByGuidCtx fbg;
		fbg.target_guid = zg;
		fbg.found_name[0] = '\0';

		zfs_iter_root(m_lzh, find_guid_in_fs_cb, &fbg);

		if (fbg.found_name[0] == '\0') {
			vss_log("GetSnapshotProperties: guid %016I64x not found\n",
			    (unsigned long long)zg);
			return (VSS_E_OBJECT_NOT_FOUND);
		}

		zfs_handle_t *zhp = zfs_open(m_lzh, fbg.found_name,
		    ZFS_TYPE_SNAPSHOT);
		if (!zhp) {
			vss_log("GetSnapshotProperties: zfs_open(%s) failed\n",
			    fbg.found_name);
			return (VSS_E_OBJECT_NOT_FOUND);
		}

		/* Get the volume GUID for the parent dataset */
		char parent_ds[256];
		strncpy(parent_ds, fbg.found_name, sizeof (parent_ds));
		char *at = strchr(parent_ds, '@');
		if (at) *at = '\0';
		wchar_t volname[MAX_PATH];
		ds_to_vol_guid(parent_ds, volname, MAX_PATH);

		hr = snap_prop_from_zfs(zhp, volname, pProp);
		zfs_close(zhp);
		if (SUCCEEDED(hr)) {
			vss_log("GetSnapshotProperties: libzfs hit"
			    " snap=%s dev=%ls vol=%ls attrs=0x%lx\n",
			    fbg.found_name,
			    pProp->m_pwszSnapshotDeviceObject
			    ? pProp->m_pwszSnapshotDeviceObject : L"(null)",
			    pProp->m_pwszOriginalVolumeName
			    ? pProp->m_pwszOriginalVolumeName : L"(null)",
			    (ULONG)pProp->m_lSnapshotAttributes);
		} else {
			vss_log("GetSnapshotProperties: libzfs path"
			    " FAILED hr=0x%lx snap=%s\n",
			    (ULONG)hr, fbg.found_name);
		}
		return (hr);
	}

	/*
	 * Query - enumerate snapshot objects, optionally filtered by
	 * snapshot set ID.
	 */
	STDMETHOD(Query)(VSS_ID QueriedObjectId,
	    VSS_OBJECT_TYPE eQueriedObjectType,
	    VSS_OBJECT_TYPE eReturnedObjectsType,
	    IVssEnumObject **ppEnum)
	{
		vss_log("Query: eQueried=%d eReturned=%d\n",
		    (int)eQueriedObjectType, (int)eReturnedObjectsType);
		if (!ppEnum)
			return (E_INVALIDARG);
		*ppEnum = NULL;

		if (eReturnedObjectsType != VSS_OBJECT_SNAPSHOT) {
			vss_log("Query: unsupported returned type %d\n",
			    (int)eReturnedObjectsType);
			return (E_INVALIDARG);
		}

		if (!m_lzh) {
			m_lzh = libzfs_init();
			if (!m_lzh)
				vss_log("Query: libzfs_init failed"
				    " (snapshot validation disabled)\n");
		}

		GUID ids[256];
		int n = vss_reg_enum(ids, 256);
		vss_log("Query: vss_reg_enum returned %d entries\n", n);

		ZfsVssEnumObject *pEnum =
		    new(std::nothrow) ZfsVssEnumObject();
		if (!pEnum)
			return (E_OUTOFMEMORY);

		if (!pEnum->Init(n)) {
			pEnum->Release();
			return (E_OUTOFMEMORY);
		}

		int added = 0;
		for (int i = 0; i < n; i++) {
			wchar_t vol[MAX_PATH];
			char    zn[256];
			uint64_t zg;
			GUID    setid;
			LONGLONG ts;
			LONG    attrs;

			if (vss_reg_load(&ids[i], vol, MAX_PATH,
			    zn, 256, &zg, &setid, &ts, &attrs) != 0) {
				vss_log("Query: reg_load[%d] failed, skipping\n",
				    i);
				continue;
			}

			/* Filter by set ID if the query is for a set */
			if (eQueriedObjectType == VSS_OBJECT_SNAPSHOT_SET &&
			    memcmp(&setid, &QueriedObjectId,
			    sizeof (GUID)) != 0)
				continue;

			/* Validate the ZFS snapshot still exists */
			if (m_lzh) {
				zfs_handle_t *zhp = zfs_open(m_lzh, zn,
				    ZFS_TYPE_SNAPSHOT);
				if (!zhp) {
					vss_log("Query: snapshot %s gone,"
					    " cleaning registry\n", zn);
					vss_reg_delete(&ids[i]);
					continue;
				}
				zfs_close(zhp);
			}

			VSS_OBJECT_PROP op;
			op.Type = VSS_OBJECT_SNAPSHOT;
			HRESULT hr = load_snap_prop(ids[i], &op.Obj.Snap);
			vss_log("Query: load_snap_prop[%d] hr=0x%lx zfs=%s\n",
			    i, hr, zn);
			if (SUCCEEDED(hr)) {
				pEnum->Add(op);
				added++;
			}
		}

		vss_log("Query: registry pass: %d valid entries (of %d)\n",
		    added, n);

		/*
		 * Second pass: enumerate all ZFS snapshots via libzfs and add
		 * any that were not already returned from the registry.  This
		 * makes pre-existing snapshots (created by 'zfs snapshot' or
		 * imported pools) visible in Explorer's Previous Versions.
		 *
		 * When the query is for a specific volume (VSS_OBJECT_SNAPSHOT)
		 * look up the matching dataset; for a global query iterate all.
		 */
		if (m_lzh) {
			/*
			 * Build the list of VSS_IDs we already returned so
			 * snap_enum_cb can skip duplicates.
			 */
			VSS_ID reg_vss_ids[256];
			int reg_vss_n = 0;
			for (int i = 0; i < n && reg_vss_n < 256; i++) {
				wchar_t vol[MAX_PATH];
				char    zn[256];
				uint64_t zg;
				GUID    setid;
				LONGLONG ts;
				LONG    attrs;
				if (vss_reg_load(&ids[i], vol, MAX_PATH,
				    zn, 256, &zg, &setid, &ts, &attrs) == 0) {
					VSS_ID sid;
					zfsguid_to_vssid(zg, &sid);
					reg_vss_ids[reg_vss_n++] = sid;
				}
			}

			/*
			 * Determine the volume name for the query so we can
			 * find the matching ZFS dataset.  For VSS_OBJECT_NONE
			 * (global query) we iterate all datasets.
			 */
			wchar_t qvol[MAX_PATH] = L"";
			char    qds[256] = "";

			if (eQueriedObjectType == VSS_OBJECT_SNAPSHOT) {
				/*
				 * QueriedObjectId is a SnapshotId — we don't
				 * know the volume from it alone; iterate all.
				 */
			} else if (eQueriedObjectType == VSS_OBJECT_NONE) {
				/* global query — iterate all datasets */
			}
			/* (snapshot-set queries are already handled above) */

			if (qds[0] != '\0') {
				/* Single dataset */
				zfs_handle_t *zhp = zfs_open(m_lzh, qds,
				    ZFS_TYPE_FILESYSTEM);
				if (zhp) {
					struct SnapEnumCtx sc;
					sc.pEnum      = pEnum;
					sc.volname    = qvol;
					sc.reg_ids    = reg_vss_ids;
					sc.reg_count  = reg_vss_n;
					sc.added      = 0;
					zfs_iter_snapshots(zhp, B_FALSE,
					    snap_enum_cb, &sc, 0, 0);
					added += sc.added;
					zfs_close(zhp);
				}
			} else {
				/*
				 * Global or set query: walk every imported pool
				 * and every filesystem in it.
				 */
				struct SnapEnumCtx sc;
				sc.pEnum      = pEnum;
				sc.volname    = L"";
				sc.reg_ids    = reg_vss_ids;
				sc.reg_count  = reg_vss_n;
				sc.added      = 0;

				/*
				 * For each filesystem, fill in the canonical
				 * volume GUID before iterating its snapshots.
				 */
				struct AllSnapCtx asc = { &sc, m_lzh };

				zfs_iter_root(m_lzh, all_snap_root_cb, &asc);
				added += sc.added;
			}
			vss_log("Query: libzfs pass added %d snapshot(s)\n",
			    added);
		}

		vss_log("Query: returning S_OK with %d total entries\n", added);
		*ppEnum = pEnum;
		return (S_OK);
	}

	/*
	 * DeleteSnapshots - destroy ZFS snapshots and remove registry
	 * entries.
	 */
	STDMETHOD(DeleteSnapshots)(VSS_ID SourceObjectId,
	    VSS_OBJECT_TYPE eSourceObjectType, BOOL bForceDelete,
	    LONG *plDeletedSnapshots, VSS_ID *pNondeletedSnapshotID)
	{
		(void)bForceDelete;
		if (!plDeletedSnapshots || !pNondeletedSnapshotID)
			return (E_INVALIDARG);

		*plDeletedSnapshots = 0;
		ZeroMemory(pNondeletedSnapshotID, sizeof (VSS_ID));

		if (!m_lzh) {
			m_lzh = libzfs_init();
			if (!m_lzh)
				return (E_FAIL);
		}

		GUID ids[256];
		int n;

		if (eSourceObjectType == VSS_OBJECT_SNAPSHOT) {
			ids[0] = SourceObjectId;
			n = 1;
		} else if (eSourceObjectType == VSS_OBJECT_SNAPSHOT_SET) {
			GUID all[256];
			int total = vss_reg_enum(all, 256);
			n = 0;
			for (int i = 0; i < total && n < 256; i++) {
				wchar_t vol[MAX_PATH];
				char    zn[256];
				uint64_t zg;
				GUID    setid;
				LONGLONG ts;
				LONG    attrs;
				if (vss_reg_load(&all[i], vol, MAX_PATH,
				    zn, 256, &zg, &setid, &ts, &attrs) == 0 &&
				    memcmp(&setid, &SourceObjectId,
				    sizeof (GUID)) == 0)
					ids[n++] = all[i];
			}
		} else {
			return (E_INVALIDARG);
		}

		for (int i = 0; i < n; i++) {
			wchar_t vol[MAX_PATH];
			char    zname[256];
			uint64_t zg;
			GUID    setid;
			LONGLONG ts;
			LONG    attrs;

			if (vss_reg_load(&ids[i], vol, MAX_PATH,
			    zname, 256, &zg, &setid, &ts, &attrs) != 0) {
				/*
				 * Not in our registry — unknown to us.
				 * When forced, count as gone so VSS can
				 * clear the catalog entry.
				 */
				if (bForceDelete)
					(*plDeletedSnapshots)++;
				else
					*pNondeletedSnapshotID = ids[i];
				continue;
			}

			zfs_handle_t *zhp = zfs_open(m_lzh, zname,
			    ZFS_TYPE_SNAPSHOT);
			if (!zhp) {
				/*
				 * ZFS snapshot already gone — clean up
				 * our registry and count as deleted so
				 * VSS removes the catalog entry.
				 */
				vss_reg_delete(&ids[i]);
				(*plDeletedSnapshots)++;
				continue;
			}
			int rc = zfs_destroy(zhp, B_FALSE);
			zfs_close(zhp);
			if (rc != 0) {
				*pNondeletedSnapshotID = ids[i];
				continue;
			}

			vss_reg_delete(&ids[i]);
			(*plDeletedSnapshots)++;
		}

		return (*plDeletedSnapshots == n ?
		    S_OK : VSS_E_OBJECT_NOT_FOUND);
	}

	/*
	 * SetSnapshotProperty - called by the coordinator after it exposes a
	 * snapshot (local drive letter or mount point) to record the exposed
	 * name / path / updated attributes in our persistent store.
	 * Returning E_NOTIMPL causes VSS_E_UNEXPECTED_PROVIDER_ERROR and the
	 * coordinator rolls back the drive-letter assignment, so we must
	 * accept all property IDs even those we do not persist.
	 */
	STDMETHOD(SetSnapshotProperty)(VSS_ID SnapshotId,
	    VSS_SNAPSHOT_PROPERTY_ID eSnapshotPropertyId,
	    VARIANT vProperty)
	{
		wchar_t guidstr[64];
		StringFromGUID2(SnapshotId, guidstr, 64);
		vss_log("SetSnapshotProperty: id=%ls propId=%d vt=%d\n",
		    guidstr, (int)eSnapshotPropertyId, (int)vProperty.vt);

		switch (eSnapshotPropertyId) {
		case VSS_SPROPID_SNAPSHOT_ATTRIBUTES:
			if (vProperty.vt == VT_I4 || vProperty.vt == VT_UI4) {
				vss_log("SetSnapshotProperty: attrs=0x%lx\n",
				    (ULONG)vProperty.lVal);
				vss_reg_update_attrs(&SnapshotId, vProperty.lVal);
			}
			break;
		case VSS_SPROPID_EXPOSED_NAME:
			if (vProperty.vt == VT_BSTR && vProperty.bstrVal) {
				vss_log("SetSnapshotProperty:"
				    " ExposedName=%ls\n", vProperty.bstrVal);
				vss_reg_update_string(&SnapshotId,
				    ZFS_VSS_REG_EXPNAME, vProperty.bstrVal);
			}
			break;
		case VSS_SPROPID_EXPOSED_PATH:
			if (vProperty.vt == VT_BSTR && vProperty.bstrVal) {
				vss_log("SetSnapshotProperty:"
				    " ExposedPath=%ls\n", vProperty.bstrVal);
				vss_reg_update_string(&SnapshotId,
				    ZFS_VSS_REG_EXPPATH, vProperty.bstrVal);
			}
			break;
		case VSS_SPROPID_STATUS:
			if (vProperty.vt == VT_I4 || vProperty.vt == VT_UI4) {
				vss_log("SetSnapshotProperty: status=%d\n",
				    (int)vProperty.lVal);
				vss_reg_update_status(&SnapshotId,
				    vProperty.lVal);
			}
			break;
		default:
			vss_log("SetSnapshotProperty: propId=%d ignored\n",
			    (int)eSnapshotPropertyId);
			break;
		}
		return (S_OK);
	}
	STDMETHOD(RevertToSnapshot)(VSS_ID) { return (E_NOTIMPL); }
	STDMETHOD(QueryRevertStatus)(VSS_PWSZ, IVssAsync **)
	    { return (E_NOTIMPL); }

	/* ---- IVssProviderCreateSnapshotSet ---- */

	STDMETHOD(EndPrepareSnapshots)(VSS_ID SnapshotSetId) {
		vss_log("EndPrepareSnapshots: called\n");
		(void)SnapshotSetId;
		return (S_OK);
	}

	/* PreCommitSnapshots - no IO freeze needed for ZFS */
	STDMETHOD(PreCommitSnapshots)(VSS_ID SnapshotSetId) {
		vss_log("PreCommitSnapshots: called\n");
		(void)SnapshotSetId;
		return (S_OK);
	}

	/*
	 * CommitSnapshots - create the actual ZFS snapshot for each pending
	 * entry that belongs to this snapshot set.
	 */
	STDMETHOD(CommitSnapshots)(VSS_ID SnapshotSetId)
	{
		vss_log("CommitSnapshots called\n");
		if (!m_lzh) {
			m_lzh = libzfs_init();
			if (!m_lzh)
				return (E_FAIL);
		}

		SYSTEMTIME st;
		GetSystemTime(&st);
		char ts_str[32];
		_snprintf(ts_str, sizeof (ts_str),
		    "%04d%02d%02dT%02d%02d%02d",
		    st.wYear, st.wMonth, st.wDay,
		    st.wHour, st.wMinute, st.wSecond);

		FILETIME ft;
		SystemTimeToFileTime(&st, &ft);
		LONGLONG timestamp =
		    ((LONGLONG)ft.dwHighDateTime << 32) | ft.dwLowDateTime;

		for (int i = 0; i < MAX_PENDING; i++) {
			if (!m_pending[i].used)
				continue;
			if (memcmp(&m_pending[i].SnapshotSetId,
			    &SnapshotSetId, sizeof (VSS_ID)) != 0)
				continue;

			char ds[256];
			if (volume_to_zfs_dataset(m_lzh,
			    m_pending[i].VolumeName, ds, sizeof (ds)) != 0)
				return (VSS_E_VOLUME_NOT_SUPPORTED);

			char snapname[512];
			_snprintf(snapname, sizeof (snapname),
			    "%s@vss-%s", ds, ts_str);

			vss_log("CommitSnapshots: creating snapshot %s\n",
			    snapname);
			int rc = zfs_snapshot(m_lzh, snapname, B_FALSE, NULL);
			vss_log("CommitSnapshots: zfs_snapshot returned %d"
			    " (err=%s)\n", rc,
			    rc ? libzfs_error_description(m_lzh) : "ok");
			if (rc != 0)
				return (E_FAIL);

			zfs_handle_t *zhp = zfs_open(m_lzh, snapname,
			    ZFS_TYPE_SNAPSHOT);
			vss_log("CommitSnapshots: zfs_open %s\n",
			    zhp ? "ok" : "FAILED");
			if (!zhp)
				return (E_FAIL);

			char guidbuf[32] = { 0 };
			uint64_t zfsguid = 0;
			if (zfs_prop_get(zhp, ZFS_PROP_GUID, guidbuf,
			    sizeof (guidbuf), NULL, NULL, 0, B_FALSE) == 0)
				zfsguid = strtoull(guidbuf, NULL, 10);
			zfs_close(zhp);

			/*
			 * Use the requestor's context augmented by the minimum
			 * flags ZFS always satisfies.
			 *
			 * VSS_VOLSNAP_ATTR_CLIENT_ACCESSIBLE (0x04): always set.
			 * Previous Versions (Explorer right-click) filters on this
			 * flag in Query() results.  set context clientaccessible
			 * (context=0x1D) cannot be used directly because the
			 * Windows System Provider (swprv) has a hard-coded blocklist
			 * that rejects ZFS volumes for timewarp/persistent snapshots,
			 * vetoing IsVolumeSupported before our provider is consulted.
			 * Setting the flag in stored attrs makes snapshots created
			 * with any working context (backup, app_rollback, etc.)
			 * visible in Previous Versions via our Query() path.
			 *
			 * VSS_VOLSNAP_ATTR_DIFFERENTIAL (0x00020000): the coordinator
			 * requires at least one type bit before writing the shadow set
			 * to its persistent catalog.  ZFS copy-on-write is
			 * semantically differential.
			 */
			LONG attrs = m_pending[i].Context;
			attrs |= VSS_VOLSNAP_ATTR_PERSISTENT |
			    VSS_VOLSNAP_ATTR_NO_AUTO_RELEASE |
			    VSS_VOLSNAP_ATTR_CLIENT_ACCESSIBLE |
			    VSS_VOLSNAP_ATTR_NO_WRITERS |
			    VSS_VOLSNAP_ATTR_DIFFERENTIAL;

			vss_log("CommitSnapshots: storing attrs=0x%lx"
			    " ctx=0x%lx guid=%016I64x\n",
			    (ULONG)attrs, (ULONG)m_pending[i].Context,
			    (unsigned long long)zfsguid);

			if (vss_reg_store(&m_pending[i].SnapshotId,
			    &SnapshotSetId,
			    m_pending[i].VolumeName,
			    snapname, zfsguid, timestamp, attrs) != 0) {
				vss_log("CommitSnapshots: vss_reg_store FAILED"
				    " (no write access to registry?)\n");
				return (E_FAIL);
			}

			vss_log("CommitSnapshots: registry stored OK\n");

			/*
			 * Eagerly mount the snapshot device so that the VSS
			 * coordinator's post-commit device probe does not
			 * encounter a pending lazy-mount IRP.  Failure is
			 * non-fatal: the lazy mount still works on first access.
			 */
			wchar_t devpath[128];
			_snwprintf(devpath, 128,
			    L"\\\\?\\GLOBALROOT\\Device\\"
			    L"ZfsSnapshot%016I64x",
			    (unsigned long long)zfsguid);
			vss_log("CommitSnapshots: eager-mount %ls\n", devpath);
			HANDLE hdev = CreateFileW(devpath,
			    FILE_READ_ATTRIBUTES,
			    FILE_SHARE_READ | FILE_SHARE_WRITE |
			    FILE_SHARE_DELETE,
			    NULL, OPEN_EXISTING,
			    FILE_FLAG_BACKUP_SEMANTICS, NULL);
			if (hdev != INVALID_HANDLE_VALUE) {
				vss_log("CommitSnapshots: eager-mount OK"
				    " (handle open)\n");
				CloseHandle(hdev);
			} else {
				vss_log("CommitSnapshots: eager-mount failed"
				    " err=%lu (lazy mount will be used)\n",
				    GetLastError());
			}

			m_pending[i].used = FALSE;
		}
		vss_log("CommitSnapshots: returning S_OK\n");
		return (S_OK);
	}

	STDMETHOD(PostCommitSnapshots)(VSS_ID SnapshotSetId,
	    LONG lSnapshotsCount) {
		vss_log("PostCommitSnapshots: count=%ld"
		    " (next: PreFinalCommitSnapshots)\n", lSnapshotsCount);
		(void)SnapshotSetId;
		(void)lSnapshotsCount;
		return (S_OK);
	}

	STDMETHOD(PreFinalCommitSnapshots)(VSS_ID SnapshotSetId) {
		vss_log("PreFinalCommitSnapshots: called"
		    " (next: PostFinalCommitSnapshots)\n");
		(void)SnapshotSetId;
		return (S_OK);
	}

	STDMETHOD(PostFinalCommitSnapshots)(VSS_ID SnapshotSetId) {
		vss_log("PostFinalCommitSnapshots: called"
		    " (next: coordinator should call GetSnapshotProperties)\n");
		(void)SnapshotSetId;
		return (S_OK);
	}

	STDMETHOD(AbortSnapshots)(VSS_ID SnapshotSetId) {
		/* Clear all pending entries for this set */
		for (int i = 0; i < MAX_PENDING; i++) {
			if (m_pending[i].used &&
			    memcmp(&m_pending[i].SnapshotSetId,
			    &SnapshotSetId, sizeof (VSS_ID)) == 0)
				m_pending[i].used = FALSE;
		}
		return (S_OK);
	}

	/* ---- IVssProviderNotifications ---- */

	STDMETHOD(OnLoad)(IUnknown *pCallback) {
		vss_log("OnLoad: called\n");
		(void)pCallback;
		return (S_OK);
	}
	STDMETHOD(OnUnload)(BOOL bForceUnload) {
		vss_log("OnUnload: force=%d\n", (int)bForceUnload);
		return (S_OK);
	}

	/* ---- IVssExposeSnapshotMgmt ({AF86E2E0}) ---- */

	/*
	 * Method signatures unknown; stubs log their vtable ordinal so we
	 * can identify which method VSS calls and reverse its signature.
	 * All return S_OK — safe because on x64 the caller cleans up args.
	 */
	STDMETHOD(Method1)(void) {
		vss_log("IVssExposeSnapshotMgmt::Method1 (vtable[3]) called\n");
		return (S_OK);
	}
	STDMETHOD(Method2)(
	    const VSS_ID *pSnapshotId,
	    LONG lAttributes,
	    VSS_PWSZ wszExpose,
	    VSS_PWSZ *ppwszExposed)
	{
		/*
		 * Raw WriteFile before any parameter access so we can tell
		 * whether Method2 is called even if a later crash prevents
		 * vss_log from writing.
		 */
		{
			HANDLE _h = CreateFileW(
			    L"C:\\Windows\\Temp\\ZfsVssProvider.log",
			    FILE_APPEND_DATA,
			    FILE_SHARE_READ | FILE_SHARE_WRITE,
			    NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
			if (_h != INVALID_HANDLE_VALUE) {
				DWORD _w;
				char _hdr[32];
				int _n = _snprintf(_hdr, sizeof (_hdr),
				    "[%lu] Method2:ENTRY\n",
				    GetCurrentProcessId());
				WriteFile(_h, _hdr, (DWORD)_n, &_w, NULL);
				CloseHandle(_h);
			}
		}
		__try {
		wchar_t sidstr[64] = L"(null)";
		if (pSnapshotId)
			StringFromGUID2(*pSnapshotId, sidstr, 64);
		vss_log("IVssExposeSnapshotMgmt::Method2: id=%ls"
		    " attrs=0x%lx expose=[%.64ls]"
		    " ppwszExposed=%p\n",
		    sidstr,
		    (ULONG)lAttributes,
		    wszExpose ? wszExpose : L"(null)",
		    (void *)ppwszExposed);

		if (ppwszExposed)
			*ppwszExposed = NULL;

		if (!wszExpose || wszExpose[0] == L'\0')
			return (S_OK);

		/*
		 * Software providers are responsible for creating the drive
		 * letter (or mount point) mapping themselves via DefineDosDevice.
		 * The VSS coordinator passes the desired drive letter in wszExpose
		 * and expects the provider to:
		 *   1. Map the letter to the snapshot device (DefineDosDevice).
		 *   2. Return the canonical exposed path in *ppwszExposed.
		 *
		 * Look up the ZFS dataset GUID from the registry so we can
		 * construct the NT device name \Device\ZfsSnapshot<hex>.
		 */
		wchar_t nt_dev[128] = L"";
		if (pSnapshotId) {
			wchar_t vol[MAX_PATH];
			char zn[256];
			uint64_t zfsguid = 0;
			GUID setid;
			LONGLONG ts;
			LONG reg_attrs;
			if (vss_reg_load(pSnapshotId, vol, MAX_PATH, zn, 256,
			    &zfsguid, &setid, &ts, &reg_attrs) == 0) {
				_snwprintf(nt_dev, 128,
				    L"\\Device\\ZfsSnapshot%016I64x",
				    (unsigned long long)zfsguid);
			}
		}

		/*
		 * Normalise the expose target to a "drive:" form for
		 * DefineDosDevice, and a "drive:\" form for the returned path.
		 * "X" → "X:\"  |  "X:" → "X:\"  |  mount point → trailing "\"
		 */
		wchar_t drive[8] = L"";
		wchar_t exposed[MAX_PATH];
		wcsncpy(exposed, wszExpose, MAX_PATH - 2);
		exposed[MAX_PATH - 2] = L'\0';
		int explen = (int)wcslen(exposed);

		if (explen == 1) {
			/* "X" → drive "X:", exposed "X:\" */
			drive[0] = exposed[0]; drive[1] = L':'; drive[2] = L'\0';
			exposed[1] = L':'; exposed[2] = L'\\'; exposed[3] = L'\0';
		} else if (explen == 2 && exposed[1] == L':') {
			/* "X:" → drive "X:", exposed "X:\" */
			drive[0] = exposed[0]; drive[1] = L':'; drive[2] = L'\0';
			exposed[2] = L'\\'; exposed[3] = L'\0';
		} else if (explen >= 3 && exposed[1] == L':') {
			/* "X:\..." → drive "X:", exposed as-is with trailing "\" */
			drive[0] = exposed[0]; drive[1] = L':'; drive[2] = L'\0';
			if (exposed[explen - 1] != L'\\' &&
			    explen + 1 < MAX_PATH) {
				exposed[explen]   = L'\\';
				exposed[explen+1] = L'\0';
			}
		} else {
			/* Mount-point path: use as-is, ensure trailing "\" */
			wcsncpy(drive, exposed, 7);
			if (explen > 0 && exposed[explen - 1] != L'\\' &&
			    explen + 1 < MAX_PATH) {
				exposed[explen]   = L'\\';
				exposed[explen+1] = L'\0';
			}
		}

		/*
		 * Create the DOS device alias.  DefineDosDevice with
		 * DDD_RAW_TARGET_PATH maps the NT object-namespace path
		 * directly.  Called from Session 0 (NetworkService) this
		 * creates an entry in \GLOBAL?? that is visible system-wide.
		 */
		if (nt_dev[0] != L'\0' && drive[0] != L'\0') {
			vss_log("Method2: DefineDosDevice(%ls -> %ls)\n",
			    drive, nt_dev);
			/*
			 * Remove any stale mapping first (e.g. from a prior
			 * expose that was not cleaned up), then add the new one.
			 * Failure of the remove is expected when no prior mapping
			 * exists; ignore it.
			 */
			DefineDosDeviceW(DDD_RAW_TARGET_PATH |
			    DDD_REMOVE_DEFINITION | DDD_EXACT_MATCH_ON_REMOVE,
			    drive, nt_dev);
			BOOL ok = DefineDosDeviceW(DDD_RAW_TARGET_PATH,
			    drive, nt_dev);
			vss_log("Method2: DefineDosDevice %s (err=%lu)\n",
			    ok ? "OK" : "FAILED", ok ? 0UL : GetLastError());
		} else {
			vss_log("Method2: no NT device path available"
			    " (nt_dev=%ls drive=%ls)\n",
			    nt_dev[0] ? nt_dev : L"(empty)",
			    drive[0] ? drive : L"(empty)");
		}

		if (ppwszExposed) {
			*ppwszExposed = (VSS_PWSZ)CoTaskMemAlloc(
			    (wcslen(exposed) + 1) * sizeof (wchar_t));
			if (*ppwszExposed) {
				wcscpy(*ppwszExposed, exposed);
				vss_log("Method2: returning exposed=%ls\n",
				    *ppwszExposed);
			}
		}

		return (S_OK);
		} __except (EXCEPTION_EXECUTE_HANDLER) {
			vss_log("Method2: EXCEPTION 0x%08lx\n",
			    (ULONG)GetExceptionCode());
			return (E_FAIL);
		}
	}
	STDMETHOD(Method3)(void) {
		vss_log("IVssExposeSnapshotMgmt::Method3 (vtable[5]) called\n");
		return (S_OK);
	}
	STDMETHOD(Method4)(void) {
		vss_log("IVssExposeSnapshotMgmt::Method4 (vtable[6]) called\n");
		return (S_OK);
	}
	STDMETHOD(Method5)(void) {
		vss_log("IVssExposeSnapshotMgmt::Method5 (vtable[7]) called\n");
		return (S_OK);
	}

	/* ---- IVssExposeSnapshot ({B076ED90}) ---- */

	/*
	 * Software providers are responsible for mapping the drive letter.
	 * DefineDosDevice with DDD_RAW_TARGET_PATH maps the NT device path
	 * to the requested drive letter.  The coordinator passes the desired
	 * drive letter (or mount point) in wszExposed; we normalise it and
	 * create the DOS device alias.
	 *
	 * Note: AF86E2E0 (IVssExposeSnapshotMgmt) has no COM proxy/stub and
	 * cannot be returned from QI in an out-of-process provider without
	 * deadlocking.  All expose work is done here instead.
	 */
	STDMETHOD(ExposeSnapshotVolume)(
	    VSS_ID SnapshotId,
	    LONG lAttributes,
	    VSS_PWSZ wszExposed,
	    VSS_PWSZ wszPathFromRoot)
	{
		/* Raw entry marker before any parameter use */
		{
			HANDLE _h = CreateFileW(
			    L"C:\\Windows\\Temp\\ZfsVssProvider.log",
			    FILE_APPEND_DATA,
			    FILE_SHARE_READ | FILE_SHARE_WRITE,
			    NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
			if (_h != INVALID_HANDLE_VALUE) {
				DWORD _w;
				char _hdr[32];
				int _n = _snprintf(_hdr, sizeof (_hdr),
				    "[%lu] ESV:ENTRY\n",
				    GetCurrentProcessId());
				WriteFile(_h, _hdr, (DWORD)_n, &_w, NULL);
				CloseHandle(_h);
			}
		}
		__try {
		wchar_t sidstr[64];
		StringFromGUID2(SnapshotId, sidstr, 64);
		vss_log("ExposeSnapshotVolume: id=%ls attrs=0x%lx"
		    " exposed=[%.64ls] path=[%.256ls]\n",
		    sidstr, (ULONG)lAttributes,
		    wszExposed ? wszExposed : L"(null)",
		    wszPathFromRoot ? wszPathFromRoot : L"(null)");

		if (!wszExposed || wszExposed[0] == L'\0')
			return (S_OK);

		/* Look up the ZFS dataset GUID from the registry. */
		wchar_t nt_dev[128] = L"";
		{
			wchar_t vol[MAX_PATH];
			char zn[256];
			uint64_t zfsguid = 0;
			GUID setid;
			LONGLONG ts;
			LONG reg_attrs;
			if (vss_reg_load(&SnapshotId, vol, MAX_PATH, zn, 256,
			    &zfsguid, &setid, &ts, &reg_attrs) == 0) {
				_snwprintf(nt_dev, 128,
				    L"\\Device\\ZfsSnapshot%016I64x",
				    (unsigned long long)zfsguid);
			}
		}

		/*
		 * Normalise the expose target to "X:" (for DefineDosDevice)
		 * and "X:\" (returned to caller).
		 */
		wchar_t drive[8] = L"";
		int explen = wszExposed ? (int)wcslen(wszExposed) : 0;

		if (explen == 1) {
			drive[0] = wszExposed[0];
			drive[1] = L':'; drive[2] = L'\0';
		} else if (explen >= 2 && wszExposed[1] == L':') {
			drive[0] = wszExposed[0];
			drive[1] = L':'; drive[2] = L'\0';
		} else if (explen > 0) {
			wcsncpy(drive, wszExposed, 7);
			drive[7] = L'\0';
		}

		if (nt_dev[0] != L'\0' && drive[0] != L'\0') {
			vss_log("ExposeSnapshotVolume: DefineDosDevice(%ls -> %ls)\n",
			    drive, nt_dev);
			DefineDosDeviceW(DDD_RAW_TARGET_PATH |
			    DDD_REMOVE_DEFINITION | DDD_EXACT_MATCH_ON_REMOVE,
			    drive, nt_dev);
			BOOL ok = DefineDosDeviceW(DDD_RAW_TARGET_PATH,
			    drive, nt_dev);
			vss_log("ExposeSnapshotVolume: DefineDosDevice %s"
			    " (err=%lu)\n",
			    ok ? "OK" : "FAILED",
			    ok ? 0UL : GetLastError());
		} else {
			vss_log("ExposeSnapshotVolume: no NT device path"
			    " (nt_dev=%ls drive=%ls)\n",
			    nt_dev[0] ? nt_dev : L"(empty)",
			    drive[0] ? drive : L"(empty)");
		}

		return (S_OK);
		} __except (EXCEPTION_EXECUTE_HANDLER) {
			vss_log("ExposeSnapshotVolume: EXCEPTION 0x%08lx\n",
			    (ULONG)GetExceptionCode());
			return (E_FAIL);
		}
	}

private:
	LONG             m_ref;
	libzfs_handle_t *m_lzh;
	LONG             m_ctx;
	PendingSnap      m_pending[MAX_PENDING];
	IUnknown *m_pUnkFTM;
};

/* ------------------------------------------------------------------ */
/* Class factory                                                        */
/* ------------------------------------------------------------------ */

class ZfsVssClassFactory : public IClassFactory {
public:
	ZfsVssClassFactory() : m_ref(1) {
		InterlockedIncrement(&g_obj_count);
	}
	~ZfsVssClassFactory() { InterlockedDecrement(&g_obj_count); }

	STDMETHOD_(ULONG, AddRef)() { return (InterlockedIncrement(&m_ref)); }
	STDMETHOD_(ULONG, Release)() {
		LONG r = InterlockedDecrement(&m_ref);
		if (r == 0) delete this;
		return (r);
	}
	STDMETHOD(QueryInterface)(REFIID riid, void **ppv) {
		if (riid == IID_IUnknown || riid == IID_IClassFactory) {
			*ppv = static_cast<IClassFactory *>(this);
		} else {
			*ppv = NULL;
			return (E_NOINTERFACE);
		}
		AddRef();
		return (S_OK);
	}
	STDMETHOD(CreateInstance)(IUnknown *pUnkOuter, REFIID riid, void **ppv)
	{
		vss_log("CreateInstance called\n");
		if (pUnkOuter)
			return (CLASS_E_NOAGGREGATION);
		ZfsVssProvider *p = new(std::nothrow) ZfsVssProvider();
		if (!p)
			return (E_OUTOFMEMORY);
		HRESULT hr = p->QueryInterface(riid, ppv);
		p->Release();
		return (hr);
	}

	STDMETHOD(LockServer)(BOOL fLock) {
		if (fLock)
			InterlockedIncrement(&g_server_locks);
		else
			InterlockedDecrement(&g_server_locks);
		return (S_OK);
	}

private:
	LONG m_ref;
};

/* ------------------------------------------------------------------ */
/* COM InprocServer32 DLL exports                                       */
/*                                                                      */
/* VSS activates software providers (Type=2) via CLSCTX_INPROC_SERVER  */
/* which loads our DLL into vssvc.exe's process.  We export the         */
/* standard COM DLL entry points; vssvc calls DllGetClassObject to      */
/* obtain a ZfsVssClassFactory and from it creates ZfsVssProvider.      */
/* ------------------------------------------------------------------ */

BOOL WINAPI
DllMain(HINSTANCE hInstDll, DWORD dwReason, LPVOID lpvReserved)
{
	(void)hInstDll;
	(void)lpvReserved;
	if (dwReason == DLL_PROCESS_ATTACH)
		vss_log("DllMain: loaded into pid %lu\n", GetCurrentProcessId());
	else if (dwReason == DLL_PROCESS_DETACH)
		vss_log("DllMain: unloaded from pid %lu\n", GetCurrentProcessId());
	return (TRUE);
}

extern "C" __declspec(dllexport) HRESULT STDAPICALLTYPE
DllGetClassObject(REFCLSID rclsid, REFIID riid, void **ppv)
{
	vss_log("DllGetClassObject called\n");
	if (rclsid != CLSID_ZfsVssProvider) {
		*ppv = NULL;
		return (CLASS_E_CLASSNOTAVAILABLE);
	}
	ZfsVssClassFactory *pFactory = new(std::nothrow) ZfsVssClassFactory();
	if (!pFactory)
		return (E_OUTOFMEMORY);
	HRESULT hr = pFactory->QueryInterface(riid, ppv);
	pFactory->Release();
	return (hr);
}

extern "C" __declspec(dllexport) HRESULT STDAPICALLTYPE
DllCanUnloadNow(void)
{
	HRESULT hr = (g_obj_count == 0 && g_server_locks == 0)
	    ? S_OK : S_FALSE;
	vss_log("DllCanUnloadNow: obj=%ld locks=%ld -> %s\n",
	    g_obj_count, g_server_locks,
	    hr == S_OK ? "S_OK (may unload)" : "S_FALSE (keep loaded)");
	return (hr);
}

/* ------------------------------------------------------------------ */
/* LocalServer32 EXE registration (called from vss_service.c)         */
/* ------------------------------------------------------------------ */

static DWORD g_regtoken = 0;

extern "C" HRESULT
vss_provider_register_server(void)
{
	ZfsVssClassFactory *pCF = new (std::nothrow) ZfsVssClassFactory();
	if (!pCF)
		return (E_OUTOFMEMORY);
	HRESULT hr = CoRegisterClassObject(CLSID_ZfsVssProvider, pCF,
	    CLSCTX_LOCAL_SERVER, REGCLS_MULTIPLEUSE, &g_regtoken);
	pCF->Release();
	vss_log("register_server: hr=0x%lx token=%lu\n", hr, g_regtoken);
	return (hr);
}

extern "C" HRESULT
vss_provider_revoke_server(void)
{
	HRESULT hr = CoRevokeClassObject(g_regtoken);
	g_regtoken = 0;
	vss_log("revoke_server: hr=0x%lx\n", hr);
	return (hr);
}
