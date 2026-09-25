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
 * Copyright (c) 2025 Jorgen Lundman <lundman@lundman.net>.
 */

// zed_service.c � shared service/foreground runner
#define	_CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <sddl.h>
#include <stdio.h>
#include <stdint.h>

#include <libzfs.h>

#include "ops_status.h"
#include "ops_import.h"
#include "ops_export.h"
#include "ops_mount.h"
#include "ops_crypto.h"
#include "ops_common.h"

// #include "rpc_dispatch.h" // your pipe dispatch function prototypes

// Include after libzfs for dprintf
#include "pipe_rpc.h"

/* Global  libzfs handle provided by the service */
libzfs_handle_t *g_lzh = NULL;

// ---- service name
static const wchar_t kServiceName[] = L"OpenZFS_tray";

// ---- globals
static SERVICE_STATUS_HANDLE g_ScmHandle = NULL;
static SERVICE_STATUS g_SvcStatus = { 0 };
static HANDLE g_StopEvent = NULL;   // signaled to stop accept loop

static DWORD ClientWorker(HANDLE client, HANDLE event);

// ---- SCM status helper
static void
ReportSvcStatus(DWORD state, DWORD win32Exit, DWORD waitHint)
{
	if (!g_ScmHandle)
		return;
	static DWORD checkPoint = 1;
	g_SvcStatus.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
	g_SvcStatus.dwCurrentState = state;
	g_SvcStatus.dwWin32ExitCode = win32Exit;
	g_SvcStatus.dwWaitHint = waitHint;
	g_SvcStatus.dwControlsAccepted = (state == SERVICE_START_PENDING) ? 0 :
	    SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN;
	g_SvcStatus.dwCheckPoint =
	    ((state == SERVICE_RUNNING) || (state == SERVICE_STOPPED)) ?
	    0 : checkPoint++;
	SetServiceStatus(g_ScmHandle, &g_SvcStatus);
}

// ---- SCM control handler
static DWORD WINAPI
SvcCtrlHandler(DWORD ctrl, DWORD dwEventType, LPVOID data, LPVOID context)
{
	(void) dwEventType;
	(void) data;
	(void) context;

	switch (ctrl) {
	case SERVICE_CONTROL_STOP:
	case SERVICE_CONTROL_SHUTDOWN:
		ReportSvcStatus(SERVICE_STOP_PENDING, NO_ERROR, 3000);
		if (g_StopEvent)
			SetEvent(g_StopEvent);
		return (NO_ERROR);
	}
	return (ERROR_CALL_NOT_IMPLEMENTED);
}

static BOOL
IsCallerAdmin(HANDLE hPipe)
{
	BOOL isAdmin = FALSE;

	if (!ImpersonateNamedPipeClient(hPipe))
		return (FALSE);

	HANDLE hTok = NULL;
	if (!OpenThreadToken(GetCurrentThread(), TOKEN_QUERY, TRUE, &hTok)) {
		RevertToSelf();
		return (FALSE);
	}

	// Build builtin Administrators SID
	BYTE sidBuf[SECURITY_MAX_SID_SIZE];
	DWORD sidLen = sizeof (sidBuf);
	PSID adminSid = (PSID)sidBuf;
	if (!CreateWellKnownSid(WinBuiltinAdministratorsSid, NULL, adminSid,
	    &sidLen)) {
		CloseHandle(hTok);
		RevertToSelf();
		return (FALSE);
	}

	// First try direct membership (works for elevated tokens)
	if (!CheckTokenMembership(hTok, adminSid, &isAdmin))
		isAdmin = FALSE;

	if (!isAdmin) {
		// Handle UAC: check the linked (elevated) token if present
		TOKEN_ELEVATION_TYPE et; DWORD cb = 0;
		if (GetTokenInformation(hTok, TokenElevationType, &et,
		    sizeof (et), &cb) && et == TokenElevationTypeLimited) {
			HANDLE hLinked = NULL;

			if (GetTokenInformation(hTok, TokenLinkedToken,
			    &hLinked, sizeof (hLinked), &cb) && hLinked) {
				BOOL isAdminLinked = FALSE;
				CheckTokenMembership(hLinked, adminSid,
				    &isAdminLinked);
				CloseHandle(hLinked);
				if (isAdminLinked)
					isAdmin = TRUE;
			}
		}
	}

	CloseHandle(hTok);
	RevertToSelf();
	return (isAdmin);
}

static int
get_names_cb(zpool_handle_t *zhp, void *cookie)
{
	(void) cookie;
	const char *name = zpool_get_name(zhp);
	if (name) printf("%s\n", name);
	zpool_close(zhp);
	return (0);
}

// Console runner for debugging: build JSON and print to stdout
static int
run_console(int argc, wchar_t **argv)
{
	// default: status-json
	int list_names = 0;
	for (int i = 1; i < argc; ++i) {
		if (wcscmp(argv[i], L"--list-names") == 0) list_names = 1;
		if (wcscmp(argv[i], L"--status-json") == 0) list_names = 0;
	}

	if (list_names) {
		// minimal libzfs iteration to print names only
		libzfs_handle_t *g = libzfs_init();
		if (!g) {
			fputs("libzfs_init failed\n", stderr);
			return (2);
		}
		zpool_iter(g, get_names_cb, NULL);
		libzfs_fini(g);
		return (0);
	} else {
		size_t jlen = 0;
		char *json = zed_status_json_build(&jlen);
		if (!json) {
			fputs("zed_status_json_build failed\n", stderr);
			return (3);
		}
		fwrite(json, 1, jlen, stdout);
		fputc('\n', stdout);
		HeapFree(GetProcessHeap(), 0, json);
		return (0);
	}
}

// ---- security attrs for the pipe: Users R/W, Admins & System full
static BOOL
MakePipeSA(SECURITY_ATTRIBUTES *sa, PSECURITY_DESCRIPTOR *psdOut)
{
	static const wchar_t *sddl =
	    L"D:P"
	    L"(A;;GA;;;SY)"	// System: full
	    L"(A;;GA;;;BA)"	// Administrators: full
	    L"(A;;GRGW;;;BU)";	// Users: read/write
	PSECURITY_DESCRIPTOR sd = NULL;
	if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl,
	    SDDL_REVISION_1, &sd, NULL))
		return (FALSE);
	sa->nLength = sizeof (*sa);
	sa->bInheritHandle = FALSE;
	sa->lpSecurityDescriptor = sd;
	*psdOut = sd;
	return (TRUE);
}

// ---- accept loop shared by service/foreground
static DWORD
RunPipeServerLoopX(void)
{
	const wchar_t *pipeName = L"\\\\.\\pipe\\openzfs_zed";
	SECURITY_ATTRIBUTES sa;
	PSECURITY_DESCRIPTOR sd = NULL;
	if (!MakePipeSA(&sa, &sd))
		return (ERROR_ACCESS_DENIED);

	DWORD err = NO_ERROR;

	for (;;) {
		if (WaitForSingleObject(g_StopEvent, 0) == WAIT_OBJECT_0)
			break;

		HANDLE hPipe = CreateNamedPipeW(
		    pipeName,
		    // first instance ok; if you want multi, drop FIRST
		    PIPE_ACCESS_DUPLEX | FILE_FLAG_FIRST_PIPE_INSTANCE,
		    PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
		    1, // max instances; bump if you want concurrency
		    64 * 1024, 64 * 1024, // out/in buffer
		    5000, // default timeout
		    &sa);
		if (hPipe == INVALID_HANDLE_VALUE) {
			err = GetLastError();
			dprintf("CreateNamedPipe failed: %lu\n", err);
			break;
		}

		dprintf("Waiting for client...\n");
		BOOL ok = ConnectNamedPipe(hPipe, NULL) ? TRUE :
		    (GetLastError() == ERROR_PIPE_CONNECTED);
		if (!ok) {
			err = GetLastError();
			CloseHandle(hPipe);
			if (err == ERROR_OPERATION_ABORTED)
				break;
			dprintf("ConnectNamedPipe failed: %lu\n", err);
			continue;
		}

		dprintf("Client connected\n");
		ClientWorker(hPipe, g_StopEvent);
		FlushFileBuffers(hPipe);
		DisconnectNamedPipe(hPipe);
		CloseHandle(hPipe);
		dprintf("Client disconnected\n");
	}

	if (sd)
		LocalFree(sd);
	return (err);
}

static DWORD WINAPI
RunPipeServerLoop(void)
{
	const wchar_t *pipeName = L"\\\\.\\pipe\\openzfs_zed";
	SECURITY_ATTRIBUTES sa;
	PSECURITY_DESCRIPTOR sd = NULL;

	if (!MakePipeSA(&sa, &sd))
		return (ERROR_ACCESS_DENIED);

	for (;;) {
		if (WaitForSingleObject(g_StopEvent, 0) == WAIT_OBJECT_0)
			break;

		HANDLE hPipe = CreateNamedPipeW(
		    pipeName,
		    PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
		    PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
		    PIPE_UNLIMITED_INSTANCES, 64 * 1024, 64 * 1024, 0, &sa);

		if (hPipe == INVALID_HANDLE_VALUE)
			break;

		OVERLAPPED ov = { 0 };
		ov.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
		BOOL connected = ConnectNamedPipe(hPipe, &ov);
		if (!connected) {
			DWORD err = GetLastError();
			if (err == ERROR_IO_PENDING) {
				HANDLE waitOn[2] = { ov.hEvent, g_StopEvent };
				DWORD wr = WaitForMultipleObjects(2, waitOn,
				    FALSE, INFINITE);
				if (wr == WAIT_OBJECT_0) {
					DWORD bytes = 0;
					if (!GetOverlappedResult(hPipe, &ov,
					    &bytes, FALSE)) {
						// fall through to cleanup
					} else {
						connected = TRUE;
					}
				} else {
					// stop requested
					CancelIoEx(hPipe, &ov);
				}
			} else if (err == ERROR_PIPE_CONNECTED) {
				connected = TRUE;
			}
		}

		if (connected) {
			// ServeOneClient should also be non-infinite blocking:
			// use overlapped ReadFile/WriteFile OR small timeouts +
			// poll g_stopEvent between requests
			ClientWorker(hPipe, g_StopEvent);
		}

		if (ov.hEvent)
			CloseHandle(ov.hEvent);
		FlushFileBuffers(hPipe);
		DisconnectNamedPipe(hPipe);
		CloseHandle(hPipe);

		if (WaitForSingleObject(g_StopEvent, 0) == WAIT_OBJECT_0)
			break;
	}
	return (0);
}


static BOOL
signal_handler(DWORD sig)
{
	if (sig == CTRL_C_EVENT || sig == CTRL_BREAK_EVENT ||
	    sig == CTRL_CLOSE_EVENT || sig == CTRL_LOGOFF_EVENT ||
	    sig == CTRL_SHUTDOWN_EVENT) {
		if (g_StopEvent)
			SetEvent(g_StopEvent);
		return (TRUE);
	}
	return (FALSE);
}

// ---- shared main (service or foreground)
static DWORD
ServiceMain_impl(BOOL is_service)
{
	DWORD rc = NO_ERROR;

	g_lzh = libzfs_init();
	if (!g_lzh)
		goto out;

	// stop event always exists
	g_StopEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
	if (!g_StopEvent)
		return (GetLastError());

	if (is_service) {
		ReportSvcStatus(SERVICE_START_PENDING, NO_ERROR, 3000);
	} else {
		// Console: Ctrl+C triggers stop event
		SetConsoleCtrlHandler(signal_handler, TRUE);
	}

	if (is_service)
		ReportSvcStatus(SERVICE_RUNNING, NO_ERROR, 0);

	rc = RunPipeServerLoop();

out:

	// shutdown
	if (g_StopEvent) {
		CloseHandle(g_StopEvent);
		g_StopEvent = NULL;
	}

	if (is_service)
		ReportSvcStatus(SERVICE_STOPPED, rc, 0);

	if (g_lzh) {
		libzfs_fini(g_lzh);
		g_lzh = NULL;
	}

	return (rc);
}

// ---- SCM entry point
static void WINAPI
ServiceMain(DWORD, LPWSTR *)
{
	g_ScmHandle = RegisterServiceCtrlHandlerExW(kServiceName,
	    SvcCtrlHandler, NULL);
	if (!g_ScmHandle)
		return;
	ZeroMemory(&g_SvcStatus, sizeof (g_SvcStatus));
	ReportSvcStatus(SERVICE_START_PENDING, NO_ERROR, 3000);
	(void) ServiceMain_impl(TRUE);
}

// ---- foreground runner (no SCM)
int
ServiceMainImpl_Foreground(void)
{
	dprintf("Running foreground server (Ctrl+C to stop)\n");
	return ((int)ServiceMain_impl(FALSE));
}

// ---- console helpers you already have (optional)
static int run_console(int argc, wchar_t **argv);

// ---- wmain: SCM vs --fg vs one-shot probes
int
wmain(int argc, wchar_t **argv)
{
	for (int i = 1; i < argc; ++i) {
		if (wcscmp(argv[i], L"--fg") == 0) {
			return (ServiceMainImpl_Foreground());
		}
		if (wcscmp(argv[i], L"--status-json") == 0 ||
		    wcscmp(argv[i], L"--list-names") == 0) {
			return (run_console(argc, argv));
		}
	}

	SERVICE_TABLE_ENTRYW table[] = {
	    { (LPWSTR)kServiceName, ServiceMain },
	    { NULL, NULL }
	};
	if (!StartServiceCtrlDispatcherW(table)) {
		if (GetLastError() == ERROR_FAILED_SERVICE_CONTROLLER_CONNECT) {
			// Not launched by SCM -> default to foreground server
			// (nicer than exiting)
			return (ServiceMainImpl_Foreground());
		}
		fprintf(stderr, "StartServiceCtrlDispatcherW failed: %lu\n",
		    GetLastError());
		return (1);
	}
	return (0);
}

static DWORD
ClientWorker(HANDLE client, HANDLE event)
{
	req_hdr_t rh;
	DWORD err;
	err = ReadAll(client, &rh, sizeof (rh));
	if (err)
		return (err);
	if (rh.len > (16*1024*1024))
		return (ERROR_INVALID_DATA); // sanity

	BYTE *payload = NULL;
	if (rh.len) {
		payload = (BYTE*)HeapAlloc(GetProcessHeap(), 0, rh.len);
		if (!payload)
			return (ERROR_OUTOFMEMORY);
		err = ReadAll(client, payload, rh.len);
		if (err) {
			HeapFree(GetProcessHeap(), 0, payload);
			return (err);
		}
	}

	if (!IsCallerAdmin(client)) {
		RESP_ERR(client, ERROR_ACCESS_DENIED);
		return (ERROR_ACCESS_DENIED);
	}

	// Minimal dispatch: only GET_STATUS and SUBSCRIBE_EVENTS
	// are implemented here
	switch (rh.op) {
	case OP_GET_STATUS:
		{
			if (rh.len < sizeof (op_get_status_by_guid_req_t)) {
				RESP_ERR(client, ERROR_GEN_FAILURE);
				break;
			}
			const op_get_status_by_guid_req_t *req =
			    (const void *)payload;
			size_t jlen = 0;
			char *json =
			    zed_status_json_build_by_guid(req->guid,
			    (zfs_status_verbosity_t)req->verbosity, &jlen);
			if (!json) {
				RESP_ERR(client, ERROR_GEN_FAILURE);
			} else {
				RESP_OK_JSON(client, jlen, json);
			}
			err = 0;
		}
		break;
	case  OP_LIST_POOLS:
		{
			dprintf("OP_LIST_POOLS\n");
			size_t jlen = 0;
			char *json = zed_list_json_build(&jlen);
			if (!json) {
				RESP_ERR(client, ERROR_GEN_FAILURE);
			} else {
				RESP_OK_JSON(client, jlen, json);
			}
			return (0);
		}
		break;
	case OP_IMPORT_SCAN:
		{
			size_t jlen = 0;
			char *json = zed_import_scan_json(&jlen);
			RESP_OK_JSON(client, jlen, json);
			break;
		}

	case OP_IMPORT_ONE:
		{
			// body:
			// op_import_one_req_t + new_name(NUL) + altroot(NUL)
			if (rh.len < sizeof (op_import_one_req_t)) {
				RESP_ERR(client, ERROR_INVALID_PARAMETER);
				break;
			}

			const op_import_one_req_t *req = (const void *)payload;
			const char *p = (const char *)payload + sizeof (*req);
			size_t remain = rh.len - sizeof (*req);

			const char *new_name = NULL, *altroot = NULL;

			// parse two NUL-terminated strings from the tail
			// (may be empty)
			if (remain) {
				new_name = p;
				size_t n0 = strnlen(new_name, remain);
				if (n0 < remain) {
					altroot = new_name + n0 + 1;
				}
			}

			size_t jlen = 0;
			char *json = zed_import_one_json(req->flags, req->guid,
			    new_name, altroot, &jlen);
			RESP_OK_JSON(client, jlen, json);
			break;
		}

	case OP_IMPORT_ALL:
		{
			dprintf("OP_IMPORT_ALL\n");

			// body: op_import_all_req_t + optional altroot(NUL)
			op_import_all_req_t req = { 0 };
			const char *altroot = NULL;
			if (rh.len >= sizeof (req)) {
				memcpy(&req, payload, sizeof (req));
				size_t tail = rh.len - sizeof (req);
				if (tail) {
					static char ar[260];
					size_t n = (tail < sizeof (ar) - 1) ?
					    tail : sizeof (ar) - 1;
					memcpy(ar, (const char *)payload +
					    sizeof (req), n);
					ar[n] = 0;
					altroot = ar;
				}
			}
			size_t jlen = 0;
			char *json = zed_import_all_json(req.flags,
			    altroot, &jlen);
			RESP_OK_JSON(client, jlen, json);
			break;
		}

	case OP_EXPORT_ONE:
		{
			dprintf("OP_EXPORT_ONE\n");

			if (rh.len < sizeof (op_export_one_req_t)) {
				RESP_ERR(client, ERROR_INVALID_PARAMETER);
				break;
			}
			const op_export_one_req_t *req = (const void *)payload;
			size_t jlen = 0;
			char *json = zed_export_one_json(req->flags,
			    req->guid, &jlen);
			if (!json) {
				RESP_ERR(client, ERROR_GEN_FAILURE);
				break;
			}
			RESP_OK_JSON(client, jlen, json);
			break;
		}

	case OP_EXPORT_ALL:
		{
			dprintf("OP_EXPORT_ALL\n");

			op_export_all_req_t req = { 0 };
			if (rh.len >= sizeof (req))
				memcpy(&req, payload, sizeof (req));
			size_t jlen = 0;
			char *json = zed_export_all_json(req.flags, &jlen);
			if (!json) {
				RESP_ERR(client, ERROR_GEN_FAILURE);
				break;
			}
			RESP_OK_JSON(client, jlen, json);
			break;
		}

	case OP_MOUNT_POOL:
		{
			dprintf("OP_MOUNT_POOL\n");
			if (rh.len < sizeof (op_mount_req_t)) {
				RESP_ERR(client, ERROR_GEN_FAILURE);
				break;
			}

			const op_mount_req_t *req = (const void *)payload;
			nvlist_t *res = zed_mount_pool_nvl(req->pool_guid,
			    req->pool_name, NULL, req->flags);
			RESP_OK_NVL(client, res);
			break;
		}

	case OP_UNMOUNT_POOL:
		{
			dprintf("OP_UNMOUNT_POOL\n");
			if (rh.len < sizeof (op_unmount_req_t)) {
				RESP_ERR(client, ERROR_GEN_FAILURE);
				break;
			}

			const op_unmount_req_t *req = (const void *)payload;
			nvlist_t *res = zed_unmount_pool_nvl(req->pool_guid,
			    req->pool_name, req->flags);
			RESP_OK_NVL(client, res);
			break;
		}
	case OP_MOUNT_PREFLIGHT:
		{
			dprintf("OP_MOUNT_PREFLIGHT\n");

			if (rh.len < sizeof (op_mount_preflight_req_t)) {
				RESP_ERR(client, ERROR_GEN_FAILURE);
				break;
			}

			const op_mount_preflight_req_t *req =
			    (const void *)payload;

			nvlist_t *res = NULL;
			if (req->dataset[0] != '\0') {
				dprintf("OP_MOUNT_PREFLIGHT: dataset='%s'\n",
				    req->dataset);
				res = zed_mount_preflight_dataset_nvl(g_lzh,
				    req->dataset);
			} else {
				const char *pname =
				    req->pool_name[0] ? req->pool_name : NULL;
				dprintf("OP_MOUNT_PREFLIGHT: pool='%s'\n",
				    pname ? pname : "(null)");
				res = zed_mount_preflight_pool_nvl(g_lzh,
				    pname);
			}

			RESP_OK_NVL(client, res);
			break;
		}

	case OP_LOAD_KEY_ONE:
		{
			dprintf("OP_LOAD_KEY_ONE\n");

			if (rh.len < sizeof (op_load_key_one_req_t)) {
				RESP_ERR(client, ERROR_GEN_FAILURE);
				break;
			}

			const op_load_key_one_req_t *req =
			    (const void *)payload;
			const char *pass =
			    (const char *)payload +
			    sizeof (op_load_key_one_req_t);

			// Always null-terminate?

			nvlist_t *res = zed_load_key_one_nvl(g_lzh,
			    req->dataset,
			    pass,
			    req->passlen);

			SecureZeroMemory(pass, req->passlen);

			RESP_OK_NVL(client, res);
			break;
		}


	case OP_SUBSCRIBE_EVENTS:
		{
			err = 0;
		}
		break;
	default:
		{
			RESP_ERR(client, ERROR_CALL_NOT_IMPLEMENTED);
			err = 0;
		}
		break;
	}

	if (payload)
		HeapFree(GetProcessHeap(), 0, payload);
	return (err);
}
