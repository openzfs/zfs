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

#define	_CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <wchar.h>
#include <stdint.h>
#include "resource.h"
#include "pipe_rpc.h"
#include "dlg_util.h"

typedef struct {
    const wchar_t *dsW; // in: dataset label
    wchar_t outW[1024]; // out: passphrase (wide)
    int outLen; // out: chars (no NUL)
    BOOL ok; // out: user pressed OK
    HFONT hHeaderFont;
} PASSCTX;

static INT_PTR CALLBACK
PassphraseDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
	switch (msg) {
	case WM_INITDIALOG: {
		PASSCTX *pc = (PASSCTX *)lParam;
		SetWindowLongPtrW(hDlg, GWLP_USERDATA, (LONG_PTR)pc);
		if (pc && pc->dsW && pc->dsW[0]) {
			wchar_t cap[256];
			_snwprintf_s(cap, _countof(cap), _TRUNCATE,
			    L"Unlock %s", pc->dsW);
			SetWindowTextW(hDlg, cap);
		}
		// ensure masked
		SendDlgItemMessageW(hDlg, IDC_EDIT_PASSPHRASE,
		    EM_SETPASSWORDCHAR, L'*', 0);
		InvalidateRect(GetDlgItem(hDlg, IDC_EDIT_PASSPHRASE),
		    NULL, TRUE);

		// Branding icon + bold heading, matching the Import
		// window's look instead of a bare "Enter passphrase:"
		// label.
		SetDlgIcon(hDlg, IDC_ICON_HDR, IDI_APP, 24, 24);
		if (pc) {
			pc->hHeaderFont = CreateHeaderFont(hDlg);
			if (pc->hHeaderFont)
				SendDlgItemMessageW(hDlg, IDC_TITLE,
				    WM_SETFONT, (WPARAM)pc->hHeaderFont,
				    TRUE);
		}

		// Follow the system light/dark setting.
		ApplyThemeFollowSystem(hDlg);

		// Anchor near the tray icon/cursor instead of screen
		// center.
		PositionNearCursor(hDlg);
		return (TRUE);
	}
	case WM_COMMAND:
		switch (LOWORD(wParam)) {
		case IDC_SHOW: {
			BOOL show = (SendDlgItemMessageW(hDlg, IDC_SHOW,
			    BM_GETCHECK, 0, 0) == BST_CHECKED);
			SendDlgItemMessageW(hDlg, IDC_EDIT_PASSPHRASE,
			    EM_SETPASSWORDCHAR, show ? 0 : L'*', 0);
			InvalidateRect(GetDlgItem(hDlg, IDC_EDIT_PASSPHRASE),
			    NULL, TRUE);
			return (TRUE);
		}
		case IDOK: {
			PASSCTX *pc = (PASSCTX *)GetWindowLongPtrW(hDlg,
			    GWLP_USERDATA);
			if (pc) {
				pc->outLen = GetDlgItemTextW(hDlg,
				    IDC_EDIT_PASSPHRASE, pc->outW,
				    _countof(pc->outW));
				pc->ok = TRUE;
			}
			EndDialog(hDlg, IDOK);
			return (TRUE);
		}
		case IDCANCEL:
			EndDialog(hDlg, IDCANCEL);
			return (TRUE);
		}
		break;

	case WM_DESTROY: {
		PASSCTX *pc = (PASSCTX *)GetWindowLongPtrW(hDlg, GWLP_USERDATA);
		if (pc && pc->hHeaderFont) {
			DeleteObject(pc->hHeaderFont);
			pc->hHeaderFont = NULL;
		}
		return (TRUE);
	}
	}
	return (FALSE);
}

BOOL
PromptPassphrase(HWND owner, const wchar_t *datasetW,
    uint8_t **out, uint32_t *outlen)
{
	*out = NULL;
	*outlen = 0;

	PASSCTX ctx = { 0 };
	ctx.dsW = datasetW;

	INT_PTR r = DialogBoxParamW(GetModuleHandleW(NULL),
	    MAKEINTRESOURCEW(IDD_PASSPHRASE),
	    owner, PassphraseDlgProc, (LPARAM)&ctx);
	if (r != IDOK || !ctx.ok || ctx.outLen <= 0)
		return (FALSE);

	// convert wide UTF-8 (no NUL on wire)
	int need = WideCharToMultiByte(CP_UTF8, 0, ctx.outW, ctx.outLen,
	    NULL, 0, NULL, NULL);
	if (need <= 0) {
		SecureZeroMemory(ctx.outW, sizeof (ctx.outW));
		return (FALSE);
	}

	uint8_t *mem = (uint8_t *)HeapAlloc(GetProcessHeap(), 0, need + 1);
	if (!mem) {
		SecureZeroMemory(ctx.outW, sizeof (ctx.outW));
		return (FALSE);
	}

	WideCharToMultiByte(CP_UTF8, 0, ctx.outW, ctx.outLen, (char *)mem,
	    need, NULL, NULL);
	SecureZeroMemory(ctx.outW, sizeof (ctx.outW));

	mem[need] = '\0';
	*out = mem;
	*outlen = (uint32_t)need;

	return (TRUE);
}
