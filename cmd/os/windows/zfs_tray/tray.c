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

#define	UNICODE
#define	_UNICODE
#define	_WIN32_WINNT 0x0600   // Vista+ for Task Dialog APIs

#include <windows.h>
#include <shellapi.h>
#include <commctrl.h>
#include <stdio.h>
#include <stdint.h>
#include "pipe_rpc.h"
#include "pool_json.h"

#define	JSMN_STATIC
#include "jsmn.h" // single-header JSON tokenizer
#include "jsmn_utils.h"
#include "rpc_client.h" // zrpc_t + zrpc_init/zrpc_call from earlier

#include "import_window.h"
#include "pass_prompt.h"
#include "dlg_util.h"

#define	strlcpy(dest, src, siz) \
	_snprintf_s(dest, siz, _TRUNCATE, "%s", src)

#pragma comment(lib, "comctl32.lib")

// Ensure v6 common-controls manifest is present
#pragma comment(linker, \
	"/manifestdependency:\"type='win32' " \
	"name='Microsoft.Windows.Common-Controls' " \
	"version='6.0.0.0' processorArchitecture='*' " \
	"publicKeyToken='6595b64144ccf1df' language='*'\"")

#define	WM_TRAYICON	(WM_USER + 1)
#define	ID_TRAY_ICON	1001
#define	IDM_STATUS	2001
#define	IDM_EXIT	2003

#define	IDM_POOLS_BASE	3000 // pool items will be 3000..3099
#define	IDM_POOLS_MAX	100
#define	IDM_POOLS_HEADER 2100 // “Pools” parent (not clickable)

#define	IDM_IMPORT_ALL	2201
#define	IDM_IMPORT_WIN  2202

#define	IDM_EXPORT_ALL 2301

#define	IDM_EXPORT_BASE 3100

static HINSTANCE g_hInst;
static HWND g_hWnd;
static NOTIFYICONDATAW g_nid;
static zrpc_t g_rpc;
static wchar_t g_pool_names[IDM_POOLS_MAX][64];
static uint64_t g_pool_guids[IDM_POOLS_MAX];
static int g_pool_count = 0;

// RPC helpers
static BOOL RpcGetStatus(wchar_t *out, size_t cchOut);
static HANDLE RpcSubscribe();
static DWORD WINAPI EventThread(LPVOID);
static void ShowBalloon(LPCWSTR title, LPCWSTR msg);

#include "resource.h"

int
ParsePoolsNameGuid(const char *json, int json_len,
    wchar_t names[][64], uint64_t guids[], int maxnames)
{
	jsmn_parser p;
	jsmn_init(&p);
	jsmntok_t tok[512];

	int r = jsmn_parse(&p, json, json_len, tok, (int)_countof(tok));
	if (r < 0)
		return (0);

	// find pools array
	int pools_arr = -1;
	for (int i = 1; i < r - 1; ++i) {
		if (tok[i].type == JSMN_STRING &&
		    tok[i + 1].type == JSMN_ARRAY) {
			int klen = tok[i].end - tok[i].start;
			if (klen == 5 &&
			    memcmp(json + tok[i].start, "pools", 5) == 0) {
				pools_arr = i + 1;
				break;
			}
		}
	}
	if (pools_arr < 0)
		return (0);

	int idx = pools_arr + 1, elems = tok[pools_arr].size, count = 0;

	for (int e = 0; e < elems && count < maxnames && idx < r; ++e) {
		if (tok[idx].type != JSMN_OBJECT) { // skip non-objects
			int end = tok[idx].end; idx++;
			while (idx < r && tok[idx].start < end) idx++;
			continue;
		}
		int obj = idx++, end = tok[obj].end;
		int name_v = -1, guid_v = -1;

		while (idx + 1 < r && tok[idx].start < end) {
			jsmntok_t *k = &tok[idx], *v = &tok[idx + 1];
			if (k->type == JSMN_STRING) {
				int klen = k->end - k->start;
				const char *ks = json + k->start;
				if (klen == 4 && memcmp(ks, "name", 4) == 0)
					name_v = idx + 1;
				else if (klen == 4 &&
				    memcmp(ks, "guid", 4) == 0)
					guid_v = idx + 1;
			}
			int vend = v->end;
			idx += 2;
			while (idx < r && tok[idx].start < vend) idx++;
		}

		if (name_v > 0 && (tok[name_v].type == JSMN_STRING)) {
			int nlen = tok[name_v].end - tok[name_v].start;
			int w = MultiByteToWideChar(CP_UTF8, 0,
			    json + tok[name_v].start, nlen, names[count], 63);
			names[count][w < 0 ? 0 : w] = 0;
			if (guid_v > 0 && (tok[guid_v].type == JSMN_STRING ||
			    tok[guid_v].type == JSMN_PRIMITIVE)) {
				int glen = tok[guid_v].end - tok[guid_v].start;
				guids[count] = parse_u64_from_utf8(
				    json + tok[guid_v].start,
				    glen);
			} else {
				guids[count] = 0ULL;
			}
			count++;
		}
	}
	return (count);
}

static int
ParsePoolNamesUTF16(const char *json, int json_len, wchar_t names[][64],
    int maxnames)
{
	jsmn_parser p;
	jsmn_init(&p);
	jsmntok_t tok[512];
	int r = jsmn_parse(&p, json, json_len, tok, (int)_countof(tok));
	if (r < 0)
		return (0);

	// find "pools" : array
	int pools_arr = -1;
	for (int i = 1; i < r - 1; ++i) {
		if (tok[i].type == JSMN_STRING) {
			int klen = tok[i].end - tok[i].start;
			const char *k = json + tok[i].start;
			if (klen == 5 && memcmp(k, "pools", 5) == 0 &&
			    tok[i + 1].type == JSMN_ARRAY) {
				pools_arr = i + 1;
				break;
			}
		}
	}
	if (pools_arr < 0)
		return (0);

	int count = 0;
	int elems = tok[pools_arr].size;
	int idx = pools_arr + 1;

	for (int e = 0; e < elems && count < maxnames && idx < r; ++e) {
		if (tok[idx].type == JSMN_STRING) {
			int len = tok[idx].end - tok[idx].start;
			MultiByteToWideChar(CP_UTF8, 0, json + tok[idx].start,
			    len, names[count], 63);
			names[count][63] = 0;
			count++; idx++;
		} else if (tok[idx].type == JSMN_OBJECT) {
			int obj_end = tok[idx].end;
			idx++;
			// scan object for "name"
			while (idx + 1 < r && tok[idx].start < obj_end) {
				jsmntok_t *k = &tok[idx], *v = &tok[idx + 1];
				if (k->type == JSMN_STRING &&
				    v->type == JSMN_STRING) {
					int klen = k->end - k->start;
					const char *ks = json + k->start;
					if (klen == 4 &&
					    memcmp(ks, "name", 4) == 0) {
						int len = v->end - v->start;
						MultiByteToWideChar(CP_UTF8, 0,
						    json + v->start, len,
						    names[count], 63);
						names[count][63] = 0;
						count++;
						break;
					}
				}
				// advance over key/value (skip subtree)
				int vend = v->end; idx += 2;
				while (idx < r && tok[idx].start < vend) idx++;
			}
			// skip to after object
			while (idx < r && tok[idx].start < obj_end) idx++;
		} else {
			// skip unknown node
			int end = tok[idx].end; idx++;
			while (idx < r && tok[idx].start < end) idx++;
		}
	}
	return (count);
}

static void
RefreshPoolsFromService(void)
{
	g_pool_count = 0;
	uint32_t st = 0;
	uint8_t *out = NULL;
	uint32_t outlen = 0;
	if (zrpc_call(&g_rpc, OP_LIST_POOLS, NULL, 0, &st, &out, &outlen) &&
	    st == 0 && out) {
		g_pool_count = ParsePoolsNameGuid((const char *)out,
		    (int)outlen, g_pool_names, g_pool_guids, IDM_POOLS_MAX);
	}
	if (out)
		HeapFree(GetProcessHeap(), 0, out);
}

static HRESULT CALLBACK
TaskDialogPosCallback(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
    LONG_PTR lpRefData)
{
	if (msg == TDN_CREATED) {
		PositionNearCursor(hwnd);
		// Match the system light/dark setting -- TaskDialog is a
		// classic dialog under the hood and doesn't do this on
		// its own.
		ApplyThemeFollowSystem(hwnd);
	}
	return (S_OK);
}

// Picks the branded icon (plain / warn / err) matching a pool health
// string, so the summary reads as "alive" state at a glance instead
// of the health word being just more body text to parse.
static int
IconIdForHealth(const wchar_t *health)
{
	if (!health)
		return (IDI_APP);
	if (_wcsicmp(health, L"ONLINE") == 0)
		return (IDI_APP);
	if (_wcsicmp(health, L"DEGRADED") == 0)
		return (IDI_APP_WARN);
	// FAULTED, UNAVAIL, REMOVED, SUSPENDED, etc.
	return (IDI_APP_ERR);
}

static void
ShowPoolSummary(HWND hWnd, const PoolSummary *ps)
{
	wchar_t mainInstr[128];
	_snwprintf_s(mainInstr, _countof(mainInstr), _TRUNCATE, L"%s — %s",
	    ps->name, (ps->health[0] ? ps->health : L"?"));

	// capacity_pct already comes back from the service with its own
	// "%" suffix (e.g. "12%") -- don't double it up into "12%%".
	const wchar_t *cap = ps->capacity_pct[0] ? ps->capacity_pct : L"?";
	wchar_t content[512];
	_snwprintf_s(content, _countof(content), _TRUNCATE,
	    L"Capacity: %s\r\nAllocated: %s\r\nFree: %s\r\nGUID: %llu",
	    cap,
	    (ps->alloc[0] ? ps->alloc : L"?"),
	    (ps->freeb[0] ? ps->freeb : L"?"),
	    (unsigned long long)ps->guid);

	HMODULE hComCtl = LoadLibraryW(L"comctl32.dll");
	if (hComCtl) {
		typedef HRESULT(WINAPI *PFN_TaskDialogIndirect)(
		    const TASKDIALOGCONFIG *, int *, int *, BOOL *);

		PFN_TaskDialogIndirect pTaskDialogIndirect =
		    (PFN_TaskDialogIndirect)GetProcAddress(hComCtl,
		    "TaskDialogIndirect");
		if (pTaskDialogIndirect) {
			HICON hIcon = (HICON)LoadImageW(
			    (HINSTANCE)GetWindowLongPtrW(hWnd,
			    GWLP_HINSTANCE),
			    MAKEINTRESOURCEW(IconIdForHealth(ps->health)),
			    IMAGE_ICON, 32, 32, LR_DEFAULTCOLOR);

			TASKDIALOGCONFIG cfg = { sizeof (cfg) };
			cfg.hwndParent = hWnd;
			cfg.dwFlags = TDF_ALLOW_DIALOG_CANCELLATION;
			if (hIcon)
				cfg.dwFlags |= TDF_USE_HICON_MAIN;
			cfg.pszWindowTitle = L"OpenZFS";
			cfg.pszMainInstruction = mainInstr;
			cfg.pszContent = content;
			cfg.dwCommonButtons = TDCBF_OK_BUTTON;
			cfg.hMainIcon = hIcon;
			// Anchor near the tray icon/cursor instead of the
			// (invisible, zero-size) main window's position.
			cfg.pfCallback = TaskDialogPosCallback;
			pTaskDialogIndirect(&cfg, NULL, NULL, NULL);
			if (hIcon)
				DestroyIcon(hIcon);
			FreeLibrary(hComCtl);
			return;
		}
		FreeLibrary(hComCtl);
	}
	// Fallback
	wchar_t msg[640];
	_snwprintf_s(msg, _countof(msg), _TRUNCATE, L"%s\n\n%s", mainInstr,
	    content);
	MessageBoxW(hWnd, msg, L"OpenZFS", MB_OK | MB_ICONINFORMATION);
}

static HICON
LoadAppIconForTray(HINSTANCE hInst)
{
	int cx = GetSystemMetrics(SM_CXSMICON);
	int cy = GetSystemMetrics(SM_CYSMICON);
	return ((HICON)LoadImageW(hInst, MAKEINTRESOURCEW(IDI_APP),
	    IMAGE_ICON, cx, cy, LR_DEFAULTCOLOR));
}

// Expects {"ok":true, "locked":[{"name":"…"}, ...]}
static int
for_each_locked(const char *json, int len,
    int (*cb)(const char *ds, void *arg), void *arg)
{
	jsmn_parser p;
	jsmntok_t tok[512];
	jsmn_init(&p);

	dprintf("for_each_locked:\n");
	int n = jsmn_parse(&p, json, len, tok, _countof(tok));
	if (n < 1 || tok[0].type != JSMN_OBJECT)
		return (0);
	dprintf("for_each_locked: n %d\n", n);

	int count = 0;
	for (int i = 1; i < n; i++) {
		if (tok[i].type == JSMN_STRING &&
		    jsmn_eq(json, &tok[i], "locked") &&
		    i + 1 < n && tok[i + 1].type == JSMN_ARRAY) {
			int arr = i + 1;
			int j = arr + 1;

			while (j < n && tok[j].start < tok[arr].end) {
				if (tok[j].type == JSMN_OBJECT) {

	// scan object for "name"
	int k = j + 1;
	char name[512] = { 0 };
	while (k < n && tok[k].start < tok[j].end) {
		if (tok[k].type == JSMN_STRING &&
		    jsmn_eq(json, &tok[k], "name") &&
		    k + 1 < n && tok[k + 1].type == JSMN_STRING) {
				jsmn_copy_string(json, &tok[k + 1], name,
				    sizeof (name));
			}
		k++;
	}
	if (name[0]) {
		count++;
		if (cb && cb(name, arg))
			return (count); // allow stop
	}
				}
				j++;
			}
			break;
		}
	}
	return (count);
}

static BOOL
load_key_one(HANDLE rpc, const char *dataset_utf8,
    const uint8_t *pass, uint32_t passlen)
{
	struct {
	    op_load_key_one_req_t hdr;
	    // trailing pass bytes…
	} *pkt;

	size_t pktsz = sizeof (op_load_key_one_req_t) + passlen + 1;
	pkt = (void *)malloc(pktsz);
	ZeroMemory(pkt, pktsz);

	strlcpy(pkt->hdr.dataset, dataset_utf8, sizeof (pkt->hdr.dataset));
	pkt->hdr.passlen = passlen;
	if (passlen)
		memcpy((uint8_t *)pkt + sizeof (op_load_key_one_req_t),
		    pass, passlen + 1); // with NUL

	uint8_t *out = NULL;
	uint32_t st = 0, outlen = 0;
	BOOL ok = zrpc_call(&g_rpc, OP_LOAD_KEY_ONE, pkt, (DWORD)pktsz,
	    &st, &out, &outlen) && st == 0;
	if (out) HeapFree(GetProcessHeap(), 0, out);

	SecureZeroMemory(pkt, pktsz);
	free(pkt);
	return (ok);
}

static BOOL
preflight_pool(const char *pool_utf8, char **out_json, uint32_t *out_len)
{
	op_mount_preflight_req_t rq = { 0 };
	strlcpy(rq.pool_name, pool_utf8, sizeof (rq.pool_name));

	uint32_t st = 0;
	uint8_t *out = NULL;
	uint32_t outlen = 0;
	if (!zrpc_call(&g_rpc, OP_MOUNT_PREFLIGHT, &rq, sizeof (rq),
	    &st, &out, &outlen) || st != 0 || !out)
		return (FALSE);

	*out_json = (char *)out;
	*out_len = outlen;
	return (TRUE); // caller frees *out_json
}

struct unlock_ctx {
    HWND owner;
    zrpc_t *rpc;
    int any_failed;
};

static int
unlock_cb(const char *ds_utf8, void *arg)
{
	struct unlock_ctx *c = (struct unlock_ctx *)arg;

	dprintf("unlock_cb: dataset %s\n", ds_utf8);

	wchar_t dsW[512];
	MultiByteToWideChar(CP_UTF8, 0, ds_utf8, -1, dsW, _countof(dsW));

	uint8_t *pass = NULL;
	uint32_t passlen = 0;
	if (!PromptPassphrase(c->owner, dsW, &pass, &passlen)) {
		// user skipped this one; treat as failure to block mount
		dprintf("unlock_cb: user cancelled for %s\n", ds_utf8);
		c->any_failed = 1;
		return (0); // continue with other roots
	}

	dprintf("unlock_cb: got passphrase for %s, len %u\n", ds_utf8, passlen);
	BOOL ok = load_key_one(c->rpc, ds_utf8, pass, passlen);
	SecureZeroMemory(pass, passlen);
	HeapFree(GetProcessHeap(), 0, pass);

	if (!ok) c->any_failed = 1;
	return (0); // keep iterating
}

void
mount_one_pool(HWND owner, const char *pool_name_utf8, int flags)
{

	dprintf("mount_one_pool: %s (flags 0x%x)\n", pool_name_utf8, flags);

	// If we are to load crypto datasets...
	if (flags & ZIMP_LOADKEYS) {

		// 1) PREFLIGHT
		char *pf = NULL; uint32_t pfl = 0;
		if (!preflight_pool(pool_name_utf8, &pf, &pfl)) {
			MessageBoxW(owner,
			    L"Preflight failed.",
			    L"OpenZFS",
			    MB_OK | MB_ICONERROR);
			return;
		}

		// Count locked roots (you already have for_each_locked)
		int locked_count = for_each_locked(pf, (int)pfl, NULL, NULL);
		dprintf("mount_one_pool: preflight shows %d locked datasets\n",
		    locked_count);
		// 2) Unlock if needed
		if (locked_count > 0) {
			struct unlock_ctx ctx = {
			    .owner = owner,
			    .rpc = &g_rpc,
			    .any_failed = 0
			};
			(void) for_each_locked(pf, (int)pfl, unlock_cb, &ctx);

			HeapFree(GetProcessHeap(), 0, pf);
			pf = NULL;
			pfl = 0;
		}

		HeapFree(GetProcessHeap(), 0, pf);
	} // flags |= ZIMP_LOADKEYS

	// 3) Mount if clear
	op_mount_req_t mreq = { 0 };
	strlcpy(mreq.pool_name, pool_name_utf8, sizeof (mreq.pool_name));

	uint8_t *mout = NULL;
	uint32_t mst = 0, moutlen = 0;

	if (zrpc_call(&g_rpc, OP_MOUNT_POOL, &mreq, sizeof (mreq),
	    &mst, &mout, &moutlen) && mst == 0 && mout) {
		HeapFree(GetProcessHeap(), 0, mout);
	} else {
		MessageBoxW(owner,
		    L"Mount failed.",
		    L"OpenZFS",
		    MB_OK | MB_ICONERROR);
	}
}

static void
ShowImportAndMountResult(HWND hWnd,
    const uint8_t *out_import, uint32_t outlen_import,
    const uint8_t *out_mount, uint32_t outlen_mount)
{
	// Convert import JSON > wide
	int wlen_import = MultiByteToWideChar(CP_UTF8, 0,
	    (const char *)out_import, (int)outlen_import, NULL, 0);
	if (wlen_import <= 0) wlen_import = 0;

	// Convert mount JSON > wide (may be NULL/empty)
	int wlen_mount = 0;
	if (out_mount && outlen_mount) {
		wlen_mount = MultiByteToWideChar(CP_UTF8, 0,
		    (const char *)out_mount, (int)outlen_mount, NULL, 0);
		if (wlen_mount <= 0) wlen_mount = 0;
	}

	// Allocate one buffer: import + separator + mount + NUL
	static const wchar_t *SEP = L"\r\n\r\n— Mount result —\r\n";
	int sep_len = (out_mount && outlen_mount) ? (int)wcslen(SEP) : 0;
	int total = wlen_import + sep_len + wlen_mount + 1;

	wchar_t *wbuf = (wchar_t *)HeapAlloc(GetProcessHeap(), 0,
	    total * sizeof (wchar_t));
	if (!wbuf)
		return;

	int pos = 0;

	// Fill import
	if (wlen_import) {
		(void) MultiByteToWideChar(CP_UTF8, 0,
		    (const char *)out_import, (int)outlen_import,
		    wbuf + pos, wlen_import);
		pos += wlen_import;
	}

	// Separator only if we have mount output
	if (sep_len) {
		memcpy(wbuf + pos, SEP, sep_len * sizeof (wchar_t));
		pos += sep_len;
	}

	// Fill mount
	if (wlen_mount) {
		(void) MultiByteToWideChar(CP_UTF8, 0,
		    (const char *)out_mount, (int)outlen_mount,
		    wbuf + pos, wlen_mount);
		pos += wlen_mount;
	}

	wbuf[pos] = L'\0';

	MessageBoxW(hWnd, wbuf, L"Import & Mount result",
	    MB_OK | MB_ICONINFORMATION);
	HeapFree(GetProcessHeap(), 0, wbuf);
}

static void
mount_all_pools(HWND hWnd, uint8_t **out_mount, uint32_t *outlen_mount)
{
	uint32_t st = 0;

	// === NEW: list imported pools and mount each ===
	if (zrpc_call(&g_rpc, OP_LIST_POOLS, NULL, 0, &st, out_mount,
	    outlen_mount) && st == 0 && *out_mount) {
		// parse {"pools":[{"name":"...","guid":...}, ...]}

		jsmn_parser p;
		jsmntok_t tok[256];
		jsmn_init(&p);
		int n = jsmn_parse(&p, (const char *)*out_mount, *outlen_mount,
		    tok, _countof(tok));
		if (n > 0) {
			// helper to iterate pool objects
			for (int i = 1; i < n; i++) {
				if (tok[i].type == JSMN_STRING &&
				    jsmn_eq((const char *)*out_mount, &tok[i],
				    "name") && (i + 1) < n &&
				    tok[i + 1].type == JSMN_STRING) {

					char name[128] = { 0 };
					jsmn_copy_string((const char *)
					    *out_mount, &tok[i + 1], name,
					    sizeof (name));

					// call OP_MOUNT_POOL for this pool
					mount_one_pool(hWnd, name,
					    ZIMP_LOADKEYS);

				}
			}
		}
	} else {
		dprintf("%s: failed to list pools\n", __func__);
	}
}

static void
unmount_one_pool(const char *pool_name)
{
	const op_unmount_req_t ureq = { 0 };
	strlcpy(ureq.pool_name, pool_name, sizeof (ureq.pool_name));
	uint8_t *uout = NULL;
	uint32_t ust = 0, uoutlen = 0;
	dprintf("unmounting exported pool '%s'\n", pool_name);
	if (zrpc_call(&g_rpc, OP_UNMOUNT_POOL, &ureq, sizeof (ureq),
	    &ust, &uout, &uoutlen) && ust == 0 && uout) {
		// optional: toast or log success per pool
		HeapFree(GetProcessHeap(), 0, uout);
	} else {
		// optional: surface a concise error per pool
	}
}

static void
unmount_all_pools(uint8_t **out_unmount, uint32_t *outlen)
{
	uint32_t st = 0;
	// === NEW: list imported pools and unmount each ===
	if (zrpc_call(&g_rpc, OP_LIST_POOLS, NULL, 0, &st, out_unmount,
	    outlen) && st == 0 && *out_unmount) {
		// parse {"pools":[{"name":"...","guid":...}, ...]}
		dprintf("parsing unmounted pools JSON\n");
		jsmn_parser p;
		jsmntok_t tok[256];
		jsmn_init(&p);
		int n = jsmn_parse(&p, (const char *)*out_unmount,
		    *outlen, tok, _countof(tok));
		dprintf("jsmn_parse returned %d tokens\n", n);
		if (n > 0) {
			// helper to iterate pool objects
			for (int i = 1; i < n; i++) {
				if (tok[i].type == JSMN_STRING &&
				    jsmn_eq((const char *)*out_unmount, &tok[i],
				    "name") && (i + 1) < n &&
				    tok[i + 1].type == JSMN_STRING) {
					char name[128] = { 0 };
					jsmn_copy_string((const char *)
					    *out_unmount, &tok[i + 1], name,
					    sizeof (name));
					// call OP_UNMOUNT_POOL for this pool
					unmount_one_pool(name);
				}
			}
		}
	} else {
		dprintf("%s: failed to list pools\n", __func__);
	}
}

static char *
utf16_to_utf8_alloc(const wchar_t *w)
{
	if (!w)
		return (NULL);
	int need = WideCharToMultiByte(CP_UTF8, 0, w, -1, NULL, 0, NULL, NULL);
	if (need <= 0)
		return (NULL);
	char *u8 = (char *)HeapAlloc(GetProcessHeap(), 0, need);
	if (!u8)
		return (NULL);
	int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, u8, need, NULL, NULL);
	if (n <= 0) {
		HeapFree(GetProcessHeap(), 0, u8);
		return (NULL);
	}
	return (u8); // NUL-terminated
}

static LRESULT CALLBACK
WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{

	switch (msg) {

	case WM_CREATE: {
		zrpc_init(&g_rpc, L"\\\\.\\pipe\\openzfs_zed", 5000);

		ZeroMemory(&g_nid, sizeof (g_nid));
		g_nid.cbSize = sizeof (g_nid);
		g_nid.hWnd = hWnd;
		g_nid.uID = ID_TRAY_ICON;
		g_nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
		g_nid.uCallbackMessage = WM_TRAYICON;
		g_nid.hIcon = LoadAppIconForTray((HINSTANCE)GetWindowLongPtrW(
		    hWnd, GWLP_HINSTANCE));
		g_nid.uVersion = NOTIFYICON_VERSION_4;
		lstrcpynW(g_nid.szTip, L"OpenZFS", ARRAYSIZE(g_nid.szTip));
		// NOTE: NIM_ADD must be called exactly once per icon; a
		// second NIM_ADD for the same hWnd/uID is invalid (unlike
		// NIM_MODIFY, which is what you'd use to change the tip
		// after the fact).
		Shell_NotifyIconW(NIM_ADD, &g_nid);
		Shell_NotifyIconW(NIM_SETVERSION, &g_nid);

		// Start event thread (fake stream)
		HANDLE h = CreateThread(NULL, 0, EventThread, NULL, 0, NULL);
		if (h) CloseHandle(h);

		// Load v6 common controls (makes styles render correctly)
		INITCOMMONCONTROLSEX icc = { sizeof (icc),
		    ICC_STANDARD_CLASSES };
		InitCommonControlsEx(&icc);

		return (0);
	}
	case WM_TRAYICON: {
		UINT code = LOWORD(lParam), nid = HIWORD(lParam);
		if (nid != ID_TRAY_ICON)
			return (0);

		// Legacy sends both WM_CONTEXTMENU and WM_RBUTTONUP;
		// modern sends only WM_CONTEXTMENU
		// if (code == WM_CONTEXTMENU || code == WM_RBUTTONUP) {
		if (code == WM_CONTEXTMENU) {
			POINT pt;
			GetCursorPos(&pt);

			// Always refresh pools just-in-time so the
			// menu is current
			RefreshPoolsFromService();

			HMENU m = CreatePopupMenu();

			// Build Pools submenu
			HMENU pools = CreatePopupMenu();
			if (g_pool_count == 0) {
				AppendMenuW(pools, MF_GRAYED, 0, L"(no pools)");
			} else {
				for (int i = 0; i < g_pool_count; ++i) {
					AppendMenuW(pools, MF_STRING,
					    IDM_POOLS_BASE + i,
					    g_pool_names[i]);
				}
			}
			AppendMenuW(m, MF_POPUP, (UINT_PTR)pools, L"Pools");

			AppendMenuW(m, MF_SEPARATOR, 0, NULL);

			AppendMenuW(m, MF_STRING, IDM_IMPORT_ALL,
			    L"Import (All)…");
			AppendMenuW(m, MF_STRING, IDM_IMPORT_WIN, L"Import…");

			AppendMenuW(m, MF_STRING, IDM_EXPORT_ALL,
			    L"Export (All)…");
			HMENU ex = CreatePopupMenu();
			AppendMenuW(m, MF_POPUP, (UINT_PTR)ex, L"Export ▶");
			// Fill Export > with current imported pools (names),
			// just like Pools >
			for (int i = 0; i < g_pool_count; ++i)
				AppendMenuW(ex, MF_STRING, IDM_EXPORT_BASE + i,
				    g_pool_names[i]);

			AppendMenuW(m, MF_SEPARATOR, 0, NULL);
			AppendMenuW(m, MF_STRING, IDM_EXIT, L"Exit");

			SetForegroundWindow(hWnd);
			TrackPopupMenu(m, TPM_RIGHTBUTTON | TPM_LEFTALIGN, pt.x,
			    pt.y, 0, hWnd, NULL);
			DestroyMenu(m);
			PostMessageW(hWnd, WM_NULL, 0, 0);
			return (0);
		}

		if (code == WM_LBUTTONUP) {
			// Optional: left-click action
			SendMessageW(hWnd, WM_COMMAND, IDM_STATUS, 0);
			return (0);
		}
		return (0);
	}

	case WM_COMMAND: {
		UINT id = LOWORD(wParam);
		if (id >= IDM_POOLS_BASE &&
		    id < IDM_POOLS_BASE + IDM_POOLS_MAX) {
			int idx = (int)(id - IDM_POOLS_BASE);
			if (idx < g_pool_count) {
				op_get_status_by_guid_req_t rq = { 0 };
				rq.verbosity = ZFSV_SUMMARY;
				rq.guid = g_pool_guids[idx];

				uint8_t *out = NULL;
				uint32_t st = 0, outlen = 0;

				if (zrpc_call(&g_rpc, OP_GET_STATUS, &rq,
				    sizeof (rq), &st, &out, &outlen) &&
				    st == 0 && out) {
					PoolSummary ps;
					if (GetPoolSummaryFromStatusJSON(
					    (const char *)out, (int)outlen,
					    &ps))
						ShowPoolSummary(hWnd, &ps);
					else
						MessageBoxW(hWnd,
						    L"Could not parse status.",
						    L"OpenZFS",
						    MB_OK | MB_ICONWARNING);
					HeapFree(GetProcessHeap(), 0, out);
				} else {
					MessageBoxW(hWnd,
					    L"Service unavailable.",
					    L"OpenZFS", MB_OK | MB_ICONWARNING);
				}
			}
			return (0);
		}

		switch (id) {
		case IDM_STATUS:
			// call OP_GET_STATUS, parse, show dialog/balloon…
			return (0);
		case IDM_IMPORT_ALL:
		{
			if (MessageBoxW(hWnd, L"Import all detectable pools?",
			    L"OpenZFS", MB_OKCANCEL | MB_ICONQUESTION) != IDOK)
				break;

			op_import_all_req_t rq = { .flags = 0 };
			uint8_t *out_import = NULL;
			uint8_t *out_mount = NULL;
			uint32_t st = 0, outlen_import = 0, outlen_mount = 0;

			if (!zrpc_call(&g_rpc, OP_IMPORT_ALL, &rq, sizeof (rq),
			    &st, &out_import, &outlen_import) || st != 0) {
				MessageBoxW(hWnd,
				    L"Import failed or service unavailable.",
				    L"OpenZFS",
				    MB_OK | MB_ICONERROR);
				break;
			}

			mount_all_pools(hWnd, &out_mount, &outlen_mount);

			ShowImportAndMountResult(hWnd, out_import,
			    outlen_import, out_mount, outlen_mount);

			HeapFree(GetProcessHeap(), 0, out_mount);
			HeapFree(GetProcessHeap(), 0, out_import);
			return (0);
		}


		case IDM_IMPORT_WIN:
		{
			CreateImportWindow(hWnd, &g_rpc);
			break;
		}

		case IDM_EXPORT_ALL:
		{
			if (MessageBoxW(hWnd, L"Export ALL imported pools?\n"
			    "Datasets will be unmounted.",
			    L"OpenZFS", MB_OKCANCEL | MB_ICONWARNING) != IDOK)
				break;

			uint8_t *out_unmount = NULL;
			uint32_t outlen_unmount = 0;
			unmount_all_pools(&out_unmount, &outlen_unmount);

			op_export_all_req_t rq = { .flags = 0 };
			uint8_t *out_export = NULL;
			uint32_t st = 0, outlen_export = 0;
			if (zrpc_call(&g_rpc, OP_EXPORT_ALL, &rq, sizeof (rq),
			    &st, &out_export, &outlen_export) && st == 0 &&
			    out_export) {
				ShowImportAndMountResult(hWnd, out_export,
				    outlen_export, out_unmount, outlen_unmount);

				RefreshPoolsFromService();
			} else {
				MessageBoxW(hWnd,
				    L"Export failed or service unavailable.",
				    L"OpenZFS", MB_OK | MB_ICONERROR);
			}
			return (0);
		}

		default:
		{
			// Export ▶ items
			if (id >= IDM_EXPORT_BASE &&
			    id < IDM_EXPORT_BASE + IDM_POOLS_MAX) {
				int idx = id - IDM_EXPORT_BASE;
				if (idx < g_pool_count) {
					wchar_t q[256];
					_snwprintf_s(q, _countof(q), _TRUNCATE,
					    L"Export pool “%s”?\n"
					    "Datasets will be unmounted.",
					    g_pool_names[idx]);
					if (MessageBoxW(hWnd, q, L"OpenZFS",
					    MB_OKCANCEL | MB_ICONWARNING) !=
					    IDOK)
						return (0);

					const char *pool = utf16_to_utf8_alloc(
					    g_pool_names[idx]);
					if (pool) {
						unmount_one_pool(pool);
						HeapFree(GetProcessHeap(), 0,
						    (void *)pool);
					}

					op_export_one_req_t rq = { .flags = 0,
					    .guid = g_pool_guids[idx]
					};
					uint8_t *out = NULL;
					uint32_t st = 0, outlen = 0;
					if (zrpc_call(&g_rpc, OP_EXPORT_ONE,
					    &rq, sizeof (rq), &st, &out,
					    &outlen) && st == 0 && out) {
						// show result, refresh pools
						wchar_t w[1024] = { 0 };
						MultiByteToWideChar(CP_UTF8, 0,
						    (char *)out, (int)outlen,
						    w, 1023);
						MessageBoxW(hWnd, w, L"Export",
						    MB_OK | MB_ICONINFORMATION);
						HeapFree(GetProcessHeap(), 0,
						    out);
						RefreshPoolsFromService();
					} else {
			MessageBoxW(hWnd,
			    L"Export failed or service unavailable.",
			    L"OpenZFS", MB_OK | MB_ICONERROR);
					}
				}
				return (0);
			}
		}


		case IDM_EXIT:
			PostQuitMessage(0);
			return (0);
		}
		return (0);
	}

	case WM_DESTROY:
		DestroyIcon(g_nid.hIcon);
		Shell_NotifyIconW(NIM_DELETE, &g_nid);
		PostQuitMessage(0);
		return (0);
	}
	return (DefWindowProcW(hWnd, msg, wParam, lParam));
}

int APIENTRY
wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR, int)
{
	g_hInst = hInst;
	WNDCLASSW wc = {0};
	wc.lpfnWndProc = WndProc;
	wc.hInstance = hInst;
	wc.lpszClassName = L"ZfsTrayWnd";
	RegisterClassW(&wc);
	g_hWnd = CreateWindowExW(0, wc.lpszClassName, L"ZfsTray",
	    0, 0, 0, 0, 0, NULL, NULL, hInst, NULL);

	MSG msg;

	while (GetMessageW(&msg, NULL, 0, 0)) {
		TranslateMessage(&msg);
		DispatchMessageW(&msg);
	}
	return (0);
}

static BOOL
SendRequest(HANDLE h, uint32_t op, const void *in, uint32_t inLen,
    uint32_t *outStatus, BYTE **outBuf, uint32_t *outLen)
{
	req_hdr_t rh = { op, inLen };
	DWORD wr;
	if (!WriteFile(h, &rh, sizeof (rh), &wr, NULL))
		return (FALSE);
	if (inLen) {
		if (!WriteFile(h, in, inLen, &wr, NULL))
			return (FALSE);
	}
	rsp_hdr_t rsp;
	DWORD rd;

	if (!ReadFile(h, &rsp, sizeof (rsp), &rd, NULL))
		return (FALSE);

	*outStatus = rsp.status;
	*outLen = rsp.len;
	*outBuf = NULL;

	if (rsp.len) {
		BYTE *buf = (BYTE*)HeapAlloc(GetProcessHeap(),
		    0, rsp.len + 1);
		if (!buf)
			return (FALSE);
		if (!ReadFile(h, buf, rsp.len, &rd, NULL)) {
			HeapFree(GetProcessHeap(), 0, buf);
			return (FALSE);
		}
		buf[rsp.len] = 0; // NUL-terminate for convenience
		*outBuf = buf;
	}

	return (TRUE);
}

static BOOL
RpcGetStatus(wchar_t *out, size_t cchOut)
{
	BOOL ok = FALSE;
	HANDLE h = CreateFileW(L"\\\\.\\pipe\\openzfs_zed",
	    GENERIC_READ|GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);

	if (h == INVALID_HANDLE_VALUE)
		return (FALSE);
	BYTE *buf = NULL;
	uint32_t len = 0, st = 0;
	ok = SendRequest(h, OP_GET_STATUS, NULL, 0, &st, &buf, &len);
	if (ok && st == 0 && buf) {
		// naïve ANSI->Wide assuming JSON ASCII
		MultiByteToWideChar(CP_ACP, 0, (LPCSTR)buf, -1, out,
		    (int)cchOut);
	}
	if (buf)
		HeapFree(GetProcessHeap(), 0, buf);
	CloseHandle(h);
	return (ok && st == 0);
}

static HANDLE
RpcSubscribe()
{
	HANDLE h = CreateFileW(L"\\\\.\\pipe\\openzfs_zed",
	    GENERIC_READ|GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
	if (h == INVALID_HANDLE_VALUE)
		return (NULL);
	uint32_t st = 0, len = 0;
	BYTE *buf = NULL;

	if (!SendRequest(h, OP_SUBSCRIBE_EVENTS, NULL, 0, &st, &buf, &len)) {
		CloseHandle(h);
		return (NULL);
	}

	if (buf) HeapFree(GetProcessHeap(), 0, buf); // stream header had len=0
	return (h); // read stream lines from h
}

static DWORD WINAPI
EventThread(LPVOID)
{
	HANDLE h = RpcSubscribe();
	if (!h)
		return (0);
	char line[256];
	DWORD rd;
	size_t pos = 0;
	for (;;) {
		if (!ReadFile(h, line+pos, 1, &rd, NULL))
			break;
		if (rd == 0)
			break;
		if (line[pos] == '\n' || pos == sizeof (line)-2) {
			line[pos] = 0;
			wchar_t w[256];
			MultiByteToWideChar(CP_ACP, 0, line, -1, w, 256);
			ShowBalloon(L"ZFS Event", w);
			pos = 0;
		} else {
			pos++;
		}
	}
	CloseHandle(h);
	return (0);
}

static void
ShowBalloon(LPCWSTR title, LPCWSTR msg)
{
	g_nid.uFlags = NIF_INFO;
	lstrcpynW(g_nid.szInfoTitle, title, ARRAYSIZE(g_nid.szInfoTitle));
	lstrcpynW(g_nid.szInfo, msg, ARRAYSIZE(g_nid.szInfo));
	g_nid.dwInfoFlags = NIIF_INFO;
	Shell_NotifyIconW(NIM_MODIFY, &g_nid);
}
