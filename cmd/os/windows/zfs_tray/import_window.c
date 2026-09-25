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

// cmd/os/windows/zfs_tray/import_window.c
#define	_CRT_SECURE_NO_WARNINGS
#define	UNICODE
#define	_UNICODE
#define	_WIN32_WINNT 0x0600
#define	_WIN32_IE    0x0600
#include <windows.h>
#include <commctrl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define	JSMN_STATIC
#include "jsmn.h"
#include "jsmn_utils.h"

#include "pipe_rpc.h"
#include "rpc_client.h"
#include "import_window.h"
#include "resource.h"
#include "dlg_util.h"

#pragma comment(lib, "comctl32.lib")

extern void mount_one_pool(HWND owner, const char *pool_name_utf8, int flags);

#ifndef ARRAYSIZE
#define	ARRAYSIZE(a) (sizeof (a)/sizeof ((a)[0]))
#endif

// ---- WM_APP messages
#define	WM_APP_SCAN_DONE (WM_APP + 100)
#define	WM_APP_IMPORT_DONE (WM_APP + 101)

// ---- data types
typedef struct {
    wchar_t  name[64];
    uint64_t guid;
    wchar_t  state[32];
} Candidate;

typedef struct {
    Candidate *items;
    int count;
} CandidateList;

typedef struct {
    HWND   hWnd;
    zrpc_t *rpc;
    HANDLE hScanThread;
    HFONT  hHeaderFont;
} ImportCtx;

// Parse: { "candidates":[ {"name":"...", "guid":"...", "state":"..."} ... ] }
static CandidateList *
ParseScanJSON(const char *json, int json_len)
{
	jsmn_parser p;
	jsmn_init(&p);
	jsmntok_t tok[2048];
	int r = jsmn_parse(&p, json, json_len, tok, (int)ARRAYSIZE(tok));
	if (r < 0)
		return (NULL);

	// find candidates array
	int arr = -1;
	for (int i = 1; i < r-1; ++i) {
		if (tok[i].type == JSMN_STRING && tok[i+1].type == JSMN_ARRAY) {
			int klen = tok[i].end - tok[i].start;
			if (klen == 10 &&
			    memcmp(json + tok[i].start, "candidates",
			    10) == 0) {
				arr = i + 1;
				break;
			}
		}
	}
	if (arr < 0)
		return (NULL);

	int elems = tok[arr].size;
	CandidateList *cl = (CandidateList *)HeapAlloc(GetProcessHeap(),
	    HEAP_ZERO_MEMORY, sizeof (*cl));
	if (!cl)
		return (NULL);
	cl->items = (Candidate *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
	    elems * sizeof (Candidate));
	if (!cl->items) {
		HeapFree(GetProcessHeap(), 0, cl);
		return (NULL);
	}
	cl->count = 0;

	int idx = arr + 1;
	for (int e = 0; e < elems && idx < r; ++e) {
		if (tok[idx].type != JSMN_OBJECT) {
			int end = tok[idx].end;
			idx++;
			while (idx < r && tok[idx].start < end) idx++;
			continue;
		}
		int obj = idx++, end = tok[obj].end;
		int name_v = -1, guid_v = -1, state_v = -1;

		while (idx + 1 < r && tok[idx].start < end) {
			jsmntok_t *k = &tok[idx], *v = &tok[idx+1];
			if (k->type == JSMN_STRING) {
				int klen = k->end - k->start;
				const char *ks = json + k->start;
				if (klen == 4 && memcmp(ks,
				    "name", 4) == 0)
					name_v = idx + 1;
				else if (klen == 4 && memcmp(ks,
				    "guid", 4) == 0)
					guid_v = idx + 1;
				else if (klen == 5 && memcmp(ks,
				    "state", 5) == 0)
					state_v = idx + 1;
			}
			int vend = v->end;
			idx += 2;
			while (idx < r && tok[idx].start < vend) idx++;
		}

		Candidate *c = &cl->items[cl->count];
		if (name_v > 0 && tok[name_v].type == JSMN_STRING) {
			int len = tok[name_v].end - tok[name_v].start;
			MultiByteToWideChar(CP_UTF8, 0,
			    json+tok[name_v].start, len, c->name,
			    (int)ARRAYSIZE(c->name)-1);
		}
		if (guid_v > 0 && (tok[guid_v].type == JSMN_STRING ||
		    tok[guid_v].type == JSMN_PRIMITIVE)) {
			int len = tok[guid_v].end - tok[guid_v].start;
			c->guid = parse_u64_from_utf8(json + tok[guid_v].start,
			    len);
		}
		if (state_v > 0 && tok[state_v].type == JSMN_STRING) {
			int len = tok[state_v].end - tok[state_v].start;
			MultiByteToWideChar(CP_UTF8, 0,
			    json + tok[state_v].start, len, c->state,
			    (int)ARRAYSIZE(c->state)-1);
		}
		if (c->name[0] || c->guid) cl->count++;
	}
	return (cl);
}

static void
FreeCandidateList(CandidateList *cl)
{
	if (!cl)
		return;
	if (cl->items)
		HeapFree(GetProcessHeap(), 0, cl->items);
	HeapFree(GetProcessHeap(), 0, cl);
}

// ---- worker: scan thread
static DWORD WINAPI
ScanThread(LPVOID param)
{
	ImportCtx *ctx = (ImportCtx*)param;

	op_import_scan_req_t rq = {0};
	uint8_t *out = NULL;
	uint32_t st = 0, outlen = 0;

	if (zrpc_call(ctx->rpc, OP_IMPORT_SCAN, &rq, sizeof (rq), &st,
	    &out, &outlen) && st == 0 && out) {
		CandidateList *cl = ParseScanJSON((const char *)out,
		    (int)outlen);
		HeapFree(GetProcessHeap(), 0, out);
		PostMessageW(ctx->hWnd, WM_APP_SCAN_DONE, 0, (LPARAM)cl);
	} else {
		PostMessageW(ctx->hWnd, WM_APP_SCAN_DONE, 0, (LPARAM)NULL);
	}
	return (0);
}

// ---- UI helpers
static void
SetStatus(HWND hWnd, LPCWSTR s)
{
	SetDlgItemTextW(hWnd, IDC_LBL_STATUS, s);
}

static void
AddListColumns(HWND hList)
{
	LVCOLUMNW col = {0};
	col.mask = LVCF_WIDTH | LVCF_TEXT | LVCF_SUBITEM;
	col.pszText = L"Name";
	col.cx = 180;
	col.iSubItem = 0;
	ListView_InsertColumn(hList, 0, &col);
	col.pszText = L"GUID";
	col.cx = 180;
	col.iSubItem = 1;
	ListView_InsertColumn(hList, 1, &col);
	col.pszText = L"State";
	col.cx = 100;
	col.iSubItem = 2;
	ListView_InsertColumn(hList, 2, &col);
}

static void
PopulateList(HWND hList, const CandidateList *cl)
{
	ListView_DeleteAllItems(hList);
	if (!cl || cl->count == 0)
		return;

	for (int i = 0; i < cl->count; i++) {
		const Candidate *c = &cl->items[i];
		LVITEMW it = {0};
		it.mask = LVIF_TEXT | LVIF_PARAM;
		it.iItem = i;
		it.pszText = (LPWSTR)c->name;
		it.lParam = (LPARAM)c->guid; // stash guid
		int idx = ListView_InsertItem(hList, &it);

		wchar_t gbuf[32];
		_snwprintf_s(gbuf, ARRAYSIZE(gbuf), _TRUNCATE, L"%llu",
		    (unsigned long long)c->guid);
		ListView_SetItemText(hList, idx, 1, gbuf);
		ListView_SetItemText(hList, idx, 2,
		    (LPWSTR)(c->state[0] ? c->state : L""));
	}
}

static uint64_t
GetSelectedGuid(HWND hList)
{
	int sel = ListView_GetNextItem(hList, -1, LVNI_SELECTED);
	if (sel < 0)
		return (0);
	LVITEMW it = {0};
	it.mask = LVIF_PARAM;
	it.iItem = sel;
	if (ListView_GetItem(hList, &it))
		return ((uint64_t)it.lParam);
	return (0);
}

static DWORD
ComposeImportOneBody(uint8_t *buf, DWORD cap,
    uint32_t flags, uint64_t guid,
    const wchar_t *newNameW, const wchar_t *altrootW)
{
	op_import_one_req_t *rq = (op_import_one_req_t *)buf;
	if (cap < sizeof (*rq))
		return (0);
	rq->flags = flags;
	rq->guid  = guid;

	DWORD off = sizeof (*rq);
	// new_name (optional)
	if (newNameW && newNameW[0]) {
		int n = WideCharToMultiByte(CP_UTF8, 0, newNameW, -1,
		    (char *)buf + off, (int)(cap - off), NULL, NULL);
		if (n <= 0)
			return (0);
		off += (DWORD)n;
	} else {
		((char *)buf)[off++] = '\0';
	}
	// altroot (optional)
	if (altrootW && altrootW[0]) {
		int n = WideCharToMultiByte(CP_UTF8, 0, altrootW, -1,
		    (char *)buf + off, (int)(cap - off), NULL, NULL);
		if (n <= 0)
			return (0);
		off += (DWORD)n;
	} else {
		((char *)buf)[off++] = '\0';
	}
	return (off);
}

static int parse_import_pool(const char *json, int len,
    char *pool_out, size_t pool_out_sz,
    uint64_t *guid_out /* optional */)
{
	jsmn_parser p;
	jsmntok_t tok[256];
	jsmn_init(&p);
	int n = jsmn_parse(&p, json, len, tok, _countof(tok));
	if (n < 1 || tok[0].type != JSMN_OBJECT)
		return (0);

	int got_pool = 0;
	for (int i = 1; i < n; i++) {
		if (tok[i].type != JSMN_STRING)
			continue;

		if (jsmn_eq(json, &tok[i], "name") && i + 1 < n &&
		    tok[i + 1].type == JSMN_STRING) {
			jsmn_copy_string(json, &tok[i + 1],
			    pool_out, pool_out_sz);
			got_pool = 1;
			i++;
			continue;
		}
		if (guid_out && jsmn_eq(json, &tok[i], "guid") && i + 1 < n) {
			const jsmntok_t *v = &tok[i + 1];
			if (v->type == JSMN_PRIMITIVE) {
				char num[32] = { 0 };
				jsmn_copy_string(json, v, num, sizeof (num));
				*guid_out = _strtoui64(num, NULL, 10);
			}
			i++;
			continue;
		}
	}
	return (got_pool);
}

static INT_PTR CALLBACK
ImportDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
	ImportCtx *ctx = (ImportCtx *)GetWindowLongPtrW(hDlg, GWLP_USERDATA);

	switch (msg) {
	case WM_INITDIALOG: {
		// lParam is ctx from CreateDialogParamW/DialogBoxParamW
		SetWindowLongPtrW(hDlg, GWLP_USERDATA, (LONG_PTR)lParam);
		ctx = (ImportCtx *)lParam;
		ctx->hWnd = hDlg;

		INITCOMMONCONTROLSEX icc = {
		    sizeof (icc),
		    ICC_LISTVIEW_CLASSES | ICC_STANDARD_CLASSES
		};
		InitCommonControlsEx(&icc);

		CheckDlgButton(hDlg, IDC_CHK_LOADKEYS, BST_CHECKED);

		// Branding icon + bold heading, so this reads as a
		// deliberate "Import ZFS Pool" screen rather than a
		// bare list dumped in a box.
		SetDlgIcon(hDlg, IDC_ICON_HDR, IDI_APP, 32, 32);
		ctx->hHeaderFont = CreateHeaderFont(hDlg);
		if (ctx->hHeaderFont)
			SendDlgItemMessageW(hDlg, IDC_TITLE, WM_SETFONT,
			    (WPARAM)ctx->hHeaderFont, TRUE);

		// Follow the system light/dark setting (mainly the
		// titlebar -- classic dialogs don't do this on their
		// own even with the comctl32 v6 manifest).
		ApplyThemeFollowSystem(hDlg);

		// Anchor near the tray icon/cursor instead of screen
		// center.
		PositionNearCursor(hDlg);

		// init listview columns, set status text, kick scan thread
		AddListColumns(GetDlgItem(hDlg, IDC_LIST));
		SetDlgItemTextW(hDlg, IDC_STATUS,
		    L"Scanning for importable pools...");
		ctx->hScanThread = CreateThread(NULL, 0, ScanThread,
		    ctx, 0, NULL);
		return (TRUE);
	}

	case WM_COMMAND:
		switch (LOWORD(wParam)) {
		case IDC_BTN_CLOSE:
			DestroyWindow(hDlg); // modeless
			return (TRUE);

		case IDC_BTN_IMPORT: {
			HWND hList = GetDlgItem(hDlg, IDC_LIST);
			uint64_t guid = GetSelectedGuid(hList);
			if (!guid) {
				MessageBoxW(hDlg,
				    L"Select a pool to import.",
				    L"OpenZFS",
				    MB_OK | MB_ICONINFORMATION);
				return (TRUE);
			}

			uint32_t flags = 0;
			if (IsDlgButtonChecked(hDlg, IDC_CHK_FORCE) ==
			    BST_CHECKED)
				flags |= ZIMP_FORCE;
			if (IsDlgButtonChecked(hDlg, IDC_CHK_READONLY) ==
			    BST_CHECKED)
				flags |= ZIMP_READONLY;
			if (IsDlgButtonChecked(hDlg, IDC_CHK_NOMOUNT) ==
			    BST_CHECKED)
				flags |= ZIMP_NOMOUNT;
			if (IsDlgButtonChecked(hDlg, IDC_CHK_LOADKEYS) ==
			    BST_CHECKED)
				flags |= ZIMP_LOADKEYS;

			wchar_t altrootW[MAX_PATH] = L"";
			GetDlgItemTextW(hDlg, IDC_ED_ALTROOT, altrootW,
			    _countof(altrootW));

			uint8_t body[1024];
			DWORD blen =
			    ComposeImportOneBody(body, sizeof (body), flags,
			    guid, NULL, altrootW);
			if (!blen) {
				MessageBoxW(hDlg,
				    L"Invalid parameters.",
				    L"OpenZFS",
				    MB_OK | MB_ICONERROR);
				return (TRUE);
			}

			EnableWindow(GetDlgItem(hDlg, IDC_BTN_IMPORT), FALSE);
			SetDlgItemTextW(hDlg, IDC_STATUS, L"Importing…");

			uint8_t *out = NULL;
			uint32_t st = 0, outlen = 0;
			BOOL ok = zrpc_call(ctx->rpc, OP_IMPORT_ONE, body, blen,
			    &st, &out, &outlen) && st == 0 && out;

			if (ok) {
				char poolA[128] = { 0 };
				uint64_t guid2 = 0;
				if (parse_import_pool((const char *)out,
				    (int)outlen, poolA, sizeof (poolA),
				    &guid2)) {
					mount_one_pool(hDlg, poolA, flags);
				}
				HeapFree(GetProcessHeap(), 0, out);
				PostMessageW(hDlg, WM_APP_IMPORT_DONE, TRUE, 0);
			} else {
				if (out) HeapFree(GetProcessHeap(), 0, out);
				PostMessageW(hDlg, WM_APP_IMPORT_DONE, FALSE,
				    0);
			}
			return (TRUE);
			}
		}
		return (FALSE);

	case WM_NOTIFY: {
		LPNMHDR nh = (LPNMHDR)lParam;
		if (nh->idFrom == IDC_LIST && nh->code == LVN_ITEMCHANGED) {
			NMLISTVIEW *lv = (NMLISTVIEW *)lParam;
			if ((lv->uChanged & LVIF_STATE) &&
			    (lv->uNewState & LVIS_SELECTED))
				EnableWindow(GetDlgItem(hDlg, IDC_BTN_IMPORT),
				    TRUE);
		}
		return (FALSE);
	}

	case WM_APP_SCAN_DONE: {
		CandidateList *cl = (CandidateList *)lParam;
		HWND hList = GetDlgItem(hDlg, IDC_LIST);
		if (!cl || cl->count == 0) {
			SetDlgItemTextW(hDlg, IDC_STATUS,
			    L"No importable pools found.");
		} else {
			SetDlgItemTextW(hDlg, IDC_STATUS,
			    L"Select a pool and click Import.");
			PopulateList(hList, cl);
		}
		FreeCandidateList(cl);
		return (TRUE);
	}

	case WM_APP_IMPORT_DONE: {
		BOOL ok = (BOOL)wParam;
		MessageBoxW(hDlg, ok ? L"Import completed." : L"Import failed.",
		    L"OpenZFS",
		    MB_OK | (ok ? MB_ICONINFORMATION : MB_ICONERROR));
		SetDlgItemTextW(hDlg, IDC_STATUS, L"Ready.");
		EnableWindow(GetDlgItem(hDlg, IDC_BTN_IMPORT), TRUE);
		return (TRUE);
	}

	case WM_DESTROY:
		if (ctx) {
			if (ctx->hScanThread) {
				CloseHandle(ctx->hScanThread);
				ctx->hScanThread = NULL;
			}
			if (ctx->hHeaderFont) {
				DeleteObject(ctx->hHeaderFont);
				ctx->hHeaderFont = NULL;
			}
			HeapFree(GetProcessHeap(), 0, ctx);
			SetWindowLongPtrW(hDlg, GWLP_USERDATA, 0);
		}
		return (TRUE);
	}
	return (FALSE); // DialogProc returns BOOL/INT_PTR, not DefWindowProc
}

// ---- public entry
HWND
CreateImportWindow(HWND hParent, zrpc_t *rpc)
{
	ImportCtx *ctx = (ImportCtx*)HeapAlloc(GetProcessHeap(),
	    HEAP_ZERO_MEMORY, sizeof (*ctx));
	if (!ctx)
		return (NULL);
	ctx->rpc = rpc;

	// Modeless
	HWND hDlg = CreateDialogParamW(GetModuleHandleW(NULL),
	    MAKEINTRESOURCEW(IDD_IMPORT),
	    hParent, ImportDlgProc, (LPARAM)ctx);
	if (hDlg) ShowWindow(hDlg, SW_SHOW);
}
