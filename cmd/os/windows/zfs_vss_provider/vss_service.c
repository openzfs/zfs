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
 * vss_service.c - Windows Service shell for the ZFS VSS Provider.
 *
 * The provider runs as a Windows Service named "ZfsVssProvider".
 * VSS Coordinator instantiates our COM class via CoCreateInstance;
 * we expose it as an out-of-process (LocalServer32) COM server.
 *
 * Registry layout for COM / VSS registration (set up by the installer):
 *
 *   HKLM\SOFTWARE\Classes\CLSID\{89300202-3CAE-4584-B38F-E72F2B3A2FBF}
 *     (Default) = "OpenZFS VSS Provider"
 *     LocalServer32\(Default) = "<path>\zfs_vss_provider.exe"
 *
 *   HKLM\SYSTEM\CurrentControlSet\Services\VSS\Providers\
 *       {89300202-3CAE-4584-B38F-E72F2B3A2FBF}
 *     (Default) = "OpenZFS VSS Provider"
 *     Type      = REG_DWORD 1  (VSS_PROV_SOFTWARE)
 *     Version   = "1.0"
 *     CLSID     = "{89300202-3CAE-4584-B38F-E72F2B3A2FBF}"
 *
 * The installer must create these keys.  This file implements the
 * service lifecycle; COM registration is in vss_provider.cpp.
 *
 * Command-line usage:
 *   zfs_vss_provider.exe /service   -- started by SCM
 *   zfs_vss_provider.exe /install   -- register COM + VSS + service
 *   zfs_vss_provider.exe /uninstall -- remove the above
 *   zfs_vss_provider.exe /run       -- run in foreground (debug)
 */

#define	_CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <sddl.h>
#include <objbase.h>
#include <stdio.h>

#include "vss_provider.h"

#define	SERVICE_NAME		L"ZfsVssProvider"
#define	SERVICE_DISPLAY		L"OpenZFS VSS Provider"
#define	SERVICE_DESC		L"Exposes ZFS snapshots to Windows VSS " \
				    L"(Volume Shadow Copy Service)."

#define	PROVIDER_GUID_STR	ZFS_VSS_PROVIDER_GUID_STR
#define	PROVIDER_NAME		ZFS_VSS_PROVIDER_NAME
#define	PROVIDER_VERSION	ZFS_VSS_PROVIDER_VERSION

/* VSS provider type: software = 2 (1 = system, 3 = hardware) */
#define	VSS_PROV_SOFTWARE_TYPE	2

static SERVICE_STATUS_HANDLE g_scm = NULL;
static SERVICE_STATUS g_status = { 0 };
static HANDLE g_stop_event = NULL;

/* ------------------------------------------------------------------ */
/* SCM helpers */
/* ------------------------------------------------------------------ */

static void
set_service_status(DWORD state, DWORD exit_code, DWORD wait_hint)
{
	static DWORD checkpoint = 1;

	g_status.dwCurrentState  = state;
	g_status.dwWin32ExitCode = exit_code;
	g_status.dwWaitHint = wait_hint;
	g_status.dwCheckPoint    =
	    (state == SERVICE_RUNNING || state == SERVICE_STOPPED)
	    ? 0 : checkpoint++;

	SetServiceStatus(g_scm, &g_status);
}

static void WINAPI
service_ctrl(DWORD ctrl)
{
	switch (ctrl) {
	case SERVICE_CONTROL_STOP:
	case SERVICE_CONTROL_SHUTDOWN:
		set_service_status(SERVICE_STOP_PENDING, NO_ERROR, 5000);
		SetEvent(g_stop_event);
		break;
	default:
		break;
	}
}

/* ------------------------------------------------------------------ */
/* COM server run loop (shared by /service and /run modes) */
/* ------------------------------------------------------------------ */

static int
run_com_server(void)
{
	HRESULT hr;

	hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
	if (FAILED(hr)) {
		fwprintf(stderr, L"CoInitializeEx failed: 0x%lx\n", hr);
		return (1);
	}

	hr = vss_provider_register_server();
	if (FAILED(hr)) {
		fwprintf(stderr,
		    L"CoRegisterClassObject failed: 0x%lx\n", hr);
		CoUninitialize();
		return (1);
	}

	/* Wait for stop signal */
	WaitForSingleObject(g_stop_event, INFINITE);

	vss_provider_revoke_server();
	CoUninitialize();
	return (0);
}

/* ------------------------------------------------------------------ */
/* ServiceMain */
/* ------------------------------------------------------------------ */

static void WINAPI
ServiceMain(DWORD argc, LPWSTR *argv)
{
	(void) argc;
	(void) argv;

	g_status.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
	g_status.dwControlsAccepted = SERVICE_ACCEPT_STOP |
	    SERVICE_ACCEPT_SHUTDOWN;

	g_scm = RegisterServiceCtrlHandlerW(SERVICE_NAME, service_ctrl);
	if (!g_scm)
		return;

	set_service_status(SERVICE_START_PENDING, NO_ERROR, 3000);

	g_stop_event = CreateEventW(NULL, TRUE, FALSE, NULL);
	if (!g_stop_event) {
		set_service_status(SERVICE_STOPPED, GetLastError(), 0);
		return;
	}

	set_service_status(SERVICE_RUNNING, NO_ERROR, 0);

	run_com_server();

	CloseHandle(g_stop_event);
	g_stop_event = NULL;

	set_service_status(SERVICE_STOPPED, NO_ERROR, 0);
}

/* ------------------------------------------------------------------ */
/* COM and VSS registration helpers (/install, /uninstall) */
/* ------------------------------------------------------------------ */

/*
 * Register COM keys for our out-of-process server.
 *
 * We register both LocalServer32 (for /run foreground mode) and an
 * AppID with LocalService so that COM routes activation requests to
 * the Windows service (which runs as SYSTEM, matching the VSS
 * coordinator's security context).
 */
static int
reg_com_server(const wchar_t *exe_path)
{
	wchar_t key[256];
	HKEY hk;
	REGSAM flags = KEY_WRITE | KEY_WOW64_64KEY;

	/* CLSID key */
	_snwprintf(key, 256,
	    L"SOFTWARE\\Classes\\CLSID\\%s", PROVIDER_GUID_STR);
	if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, key, 0, NULL,
	    REG_OPTION_NON_VOLATILE, flags, NULL, &hk, NULL)
	    != ERROR_SUCCESS) {
		fwprintf(stderr, L"Cannot create CLSID key\n");
		return (-1);
	}
	RegSetValueExW(hk, NULL, 0, REG_SZ,
	    (const BYTE *)PROVIDER_NAME,
	    (DWORD)((wcslen(PROVIDER_NAME) + 1) * sizeof (wchar_t)));
	/* Link CLSID to AppID for service-based activation */
	RegSetValueExW(hk, L"AppID", 0, REG_SZ,
	    (const BYTE *)PROVIDER_GUID_STR,
	    (DWORD)((wcslen(PROVIDER_GUID_STR) + 1) * sizeof (wchar_t)));
	RegCloseKey(hk);

	/* LocalServer32 */
	_snwprintf(key, 256,
	    L"SOFTWARE\\Classes\\CLSID\\%s\\LocalServer32",
	    PROVIDER_GUID_STR);
	if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, key, 0, NULL,
	    REG_OPTION_NON_VOLATILE, flags, NULL, &hk, NULL)
	    != ERROR_SUCCESS) {
		fwprintf(stderr, L"Cannot create LocalServer32 key\n");
		return (-1);
	}
	RegSetValueExW(hk, NULL, 0, REG_SZ,
	    (const BYTE *)exe_path,
	    (DWORD)((wcslen(exe_path) + 1) * sizeof (wchar_t)));
	RegCloseKey(hk);

	/*
	 * AppID/LocalService: COM uses the SCM to start our service on
	 * demand.  The coordinator then uses the VSS LRPC channel (not
	 * generic DCOM) for all provider calls, which handles
	 * VSS_OBJECT_PROP serialization (copySnapshot) correctly.
	 */
	_snwprintf(key, 256,
	    L"SOFTWARE\\Classes\\AppID\\%s", PROVIDER_GUID_STR);
	if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, key, 0, NULL,
	    REG_OPTION_NON_VOLATILE, flags, NULL, &hk, NULL)
	    != ERROR_SUCCESS) {
		fwprintf(stderr, L"Cannot create AppID key\n");
		return (-1);
	}
	RegSetValueExW(hk, NULL, 0, REG_SZ,
	    (const BYTE *)PROVIDER_NAME,
	    (DWORD)((wcslen(PROVIDER_NAME) + 1) * sizeof (wchar_t)));
	RegSetValueExW(hk, L"LocalService", 0, REG_SZ,
	    (const BYTE *)SERVICE_NAME,
	    (DWORD)((wcslen(SERVICE_NAME) + 1) * sizeof (wchar_t)));
	RegCloseKey(hk);

	return (0);
}

/*
 * Version GUID for our provider - identifies this specific build/version.
 * {00000001-0000-0000-0001-000000000001} is an arbitrary unique value.
 */
#define	PROVIDER_VERSION_ID	L"{00000001-0000-0000-0001-000000000001}"

static int
reg_vss_provider(void)
{
	wchar_t key[256];
	HKEY hk, hk_clsid;
	DWORD type_val = VSS_PROV_SOFTWARE_TYPE;
	DWORD ctx_val = 0xFFFFFFFF;

	_snwprintf(key, 256,
	    L"SYSTEM\\CurrentControlSet\\Services\\VSS\\Providers\\%s",
	    PROVIDER_GUID_STR);

	if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, key, 0, NULL,
	    REG_OPTION_NON_VOLATILE, KEY_WRITE | KEY_WOW64_64KEY,
	    NULL, &hk, NULL) != ERROR_SUCCESS) {
		fwprintf(stderr, L"Cannot create VSS provider key\n");
		return (-1);
	}

	RegSetValueExW(hk, NULL, 0, REG_SZ,
	    (const BYTE *)PROVIDER_NAME,
	    (DWORD)((wcslen(PROVIDER_NAME) + 1) * sizeof (wchar_t)));
	RegSetValueExW(hk, L"Type", 0, REG_DWORD,
	    (const BYTE *)&type_val, sizeof (type_val));
	RegSetValueExW(hk, L"Version", 0, REG_SZ,
	    (const BYTE *)PROVIDER_VERSION,
	    (DWORD)((wcslen(PROVIDER_VERSION) + 1) * sizeof (wchar_t)));
	RegSetValueExW(hk, L"VersionId", 0, REG_SZ,
	    (const BYTE *)PROVIDER_VERSION_ID,
	    (DWORD)((wcslen(PROVIDER_VERSION_ID) + 1) * sizeof (wchar_t)));
	RegSetValueExW(hk, L"SupportedContexts", 0, REG_DWORD,
	    (const BYTE *)&ctx_val, sizeof (ctx_val));

	if (RegCreateKeyExW(hk, L"CLSID", 0, NULL,
	    REG_OPTION_NON_VOLATILE, KEY_WRITE | KEY_WOW64_64KEY,
	    NULL, &hk_clsid, NULL) == ERROR_SUCCESS) {
		RegSetValueExW(hk_clsid, NULL, 0, REG_SZ,
		    (const BYTE *)PROVIDER_GUID_STR,
		    (DWORD)((wcslen(PROVIDER_GUID_STR) + 1) *
		    sizeof (wchar_t)));
		RegCloseKey(hk_clsid);
	}

	RegCloseKey(hk);
	return (0);
}

/*
 * Create HKLM\SOFTWARE\OpenZFS\VSS\Snapshots and grant SYSTEM and
 * Administrators full key access for per-snapshot metadata storage.
 */
static int
reg_snapshot_store(void)
{
	PSECURITY_DESCRIPTOR psd = NULL;
	ULONG sd_size = 0;
	SECURITY_ATTRIBUTES sa;
	HKEY hk = NULL;
	LSTATUS r;

	if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
	    L"D:(A;OICI;KA;;;SY)(A;OICI;KA;;;BA)",
	    SDDL_REVISION_1, &psd, &sd_size))
		return (-1);

	ZeroMemory(&sa, sizeof (sa));
	sa.nLength = sizeof (sa);
	sa.lpSecurityDescriptor = psd;

	r = RegCreateKeyExW(HKEY_LOCAL_MACHINE,
	    L"SOFTWARE\\OpenZFS\\VSS\\Snapshots",
	    0, NULL, REG_OPTION_NON_VOLATILE,
	    KEY_WRITE | KEY_WOW64_64KEY, &sa, &hk, NULL);
	LocalFree(psd);
	if (r != ERROR_SUCCESS)
		return (-1);

	RegCloseKey(hk);
	return (0);
}

static int
install_service(const wchar_t *exe_path)
{
	SC_HANDLE scm, svc;
	wchar_t cmd[MAX_PATH + 16];

	/* Register COM keys */
	if (reg_com_server(exe_path) != 0)
		return (-1);
	if (reg_vss_provider() != 0)
		return (-1);
	if (reg_snapshot_store() != 0)
		return (-1);

	/* Install Windows service */
	scm = OpenSCManagerW(NULL, NULL, SC_MANAGER_CREATE_SERVICE);
	if (!scm) {
		fwprintf(stderr, L"OpenSCManager failed: %lu\n",
		    GetLastError());
		return (-1);
	}

	_snwprintf(cmd, MAX_PATH + 16, L"\"%s\" /service", exe_path);
	svc = CreateServiceW(scm, SERVICE_NAME, SERVICE_DISPLAY,
	    SERVICE_ALL_ACCESS, SERVICE_WIN32_OWN_PROCESS,
	    SERVICE_DEMAND_START, SERVICE_ERROR_NORMAL,
	    cmd, NULL, NULL, NULL, NULL, NULL);

	if (!svc) {
		DWORD err = GetLastError();
		if (err != ERROR_SERVICE_EXISTS) {
			fwprintf(stderr,
			    L"CreateService failed: %lu\n", err);
			CloseServiceHandle(scm);
			return (-1);
		}
		fwprintf(stdout, L"Service already exists.\n");
	} else {
		SERVICE_DESCRIPTIONW sd = { (LPWSTR)SERVICE_DESC };
		ChangeServiceConfig2W(svc,
		    SERVICE_CONFIG_DESCRIPTION, &sd);
		CloseServiceHandle(svc);
		fwprintf(stdout, L"Service installed.\n");
	}

	CloseServiceHandle(scm);
	fwprintf(stdout, L"ZFS VSS Provider installed successfully.\n");
	return (0);
}

static int
uninstall_service(void)
{
	SC_HANDLE scm, svc;
	SERVICE_STATUS st;
	wchar_t key[256];

	scm = OpenSCManagerW(NULL, NULL, SC_MANAGER_ALL_ACCESS);
	if (scm) {
		svc = OpenServiceW(scm, SERVICE_NAME, SERVICE_ALL_ACCESS);
		if (svc) {
			ControlService(svc, SERVICE_CONTROL_STOP, &st);
			DeleteService(svc);
			CloseServiceHandle(svc);
		}
		CloseServiceHandle(scm);
	}

	/* Remove COM and VSS registry keys */
	_snwprintf(key, 256,
	    L"SOFTWARE\\Classes\\CLSID\\%s\\LocalServer32",
	    PROVIDER_GUID_STR);
	RegDeleteKeyW(HKEY_LOCAL_MACHINE, key);

	_snwprintf(key, 256,
	    L"SOFTWARE\\Classes\\CLSID\\%s", PROVIDER_GUID_STR);
	RegDeleteKeyW(HKEY_LOCAL_MACHINE, key);

	_snwprintf(key, 256,
	    L"SOFTWARE\\Classes\\AppID\\%s", PROVIDER_GUID_STR);
	RegDeleteKeyW(HKEY_LOCAL_MACHINE, key);

	/* Delete CLSID subkey first, then parent */
	_snwprintf(key, 256,
	    L"SYSTEM\\CurrentControlSet\\Services\\VSS\\Providers\\%s\\CLSID",
	    PROVIDER_GUID_STR);
	RegDeleteKeyW(HKEY_LOCAL_MACHINE, key);

	_snwprintf(key, 256,
	    L"SYSTEM\\CurrentControlSet\\Services\\VSS\\Providers\\%s",
	    PROVIDER_GUID_STR);
	RegDeleteKeyW(HKEY_LOCAL_MACHINE, key);

	fwprintf(stdout, L"ZFS VSS Provider uninstalled.\n");
	return (0);
}

/* ------------------------------------------------------------------ */
/* Entry point */
/* ------------------------------------------------------------------ */

int
wmain(int argc, wchar_t *argv[])
{
	if (argc >= 2 && _wcsicmp(argv[1], L"/service") == 0) {
		/* Started by SCM */
		SERVICE_TABLE_ENTRYW table[] = {
			{ (LPWSTR)SERVICE_NAME, ServiceMain },
			{ NULL, NULL }
		};
		StartServiceCtrlDispatcherW(table);
		return (0);
	}

	if (argc >= 2 && _wcsicmp(argv[1], L"/install") == 0) {
		wchar_t exe[MAX_PATH];
		GetModuleFileNameW(NULL, exe, MAX_PATH);
		return (install_service(exe));
	}

	if (argc >= 2 && _wcsicmp(argv[1], L"/uninstall") == 0) {
		return (uninstall_service());
	}

	if (argc >= 2 && _wcsicmp(argv[1], L"/run") == 0) {
		/* Foreground debug mode */
		fwprintf(stdout,
		    L"Running ZFS VSS Provider in foreground. "
		    L"Press Ctrl-C to stop.\n");
		g_stop_event = CreateEventW(NULL, TRUE, FALSE, NULL);
		if (!g_stop_event)
			return (1);
		/* Ctrl-C will terminate the process directly */
		int rc = run_com_server();
		CloseHandle(g_stop_event);
		return (rc);
	}

	/*
	 * COM launches LocalServer32 EXEs with "-Embedding" to request
	 * class factory registration.  Treat it like /run: register the
	 * factory and wait until COM releases all references.
	 */
	if (argc >= 2 && (_wcsicmp(argv[1], L"-Embedding") == 0 ||
	    _wcsicmp(argv[1], L"/Embedding") == 0)) {
		g_stop_event = CreateEventW(NULL, TRUE, FALSE, NULL);
		if (!g_stop_event)
			return (1);
		int rc = run_com_server();
		CloseHandle(g_stop_event);
		return (rc);
	}

	fwprintf(stderr,
	    L"Usage: zfs_vss_provider.exe "
	    L"/install | /uninstall | /service | /run\n");
	return (1);
}
