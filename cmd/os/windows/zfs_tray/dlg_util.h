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
 * Copyright (c) 2025 Jorgen Lundman <lundman@lundman.net>.
 */

// cmd/os/windows/zfs_tray/dlg_util.h
//
// Small shared helpers so the tray's popup dialogs/task dialogs:
//  - open anchored near wherever the user just interacted with the
//    tray icon (the mouse cursor position at creation time), instead
//    of the screen/monitor center (DS_CENTER) or the top-left corner
//    (an unset/zero-size owner window rect).
//  - get a bit of visual hierarchy/branding (a bold header font for
//    a title static, an icon on a header STATIC control) instead of
//    being a flat wall of default-font text.
//  - follow the system light/dark theme, since plain Win32 dialogs
//    don't do this automatically even when comctl32 v6 is active.
#pragma once
#include <windows.h>
#include <uxtheme.h>
#include <dwmapi.h>

#pragma comment(lib, "uxtheme.lib")
#pragma comment(lib, "dwmapi.lib")

static void
PositionNearCursor(HWND hWnd)
{
	POINT pt;
	GetCursorPos(&pt);

	RECT rc;
	if (!GetWindowRect(hWnd, &rc))
		return;
	int w = rc.right - rc.left;
	int h = rc.bottom - rc.top;

	HMONITOR hMon = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
	MONITORINFO mi;
	mi.cbSize = sizeof (mi);
	if (!GetMonitorInfo(hMon, &mi))
		return;

	// Anchor like a flyout: open up-and-left of the cursor, since the
	// tray icon is normally in the bottom-right corner of the screen.
	int x = pt.x - w;
	int y = pt.y - h;

	// Not enough room on that side -> flip to the other side instead.
	if (x < mi.rcWork.left)
		x = pt.x;
	if (y < mi.rcWork.top)
		y = pt.y;

	// Final clamp so the window always stays fully on-screen.
	if (x + w > mi.rcWork.right)
		x = mi.rcWork.right - w;
	if (x < mi.rcWork.left)
		x = mi.rcWork.left;
	if (y + h > mi.rcWork.bottom)
		y = mi.rcWork.bottom - h;
	if (y < mi.rcWork.top)
		y = mi.rcWork.top;

	SetWindowPos(hWnd, NULL, x, y, 0, 0,
	    SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

// Creates a bold-ish "Segoe UI" header font sized relative to the
// dialog's own font, for use on a title/heading STATIC control so it
// reads as a heading rather than blending into the body text. Caller
// owns the returned HFONT and must DeleteObject() it (typically on
// WM_DESTROY).
static HFONT
CreateHeaderFont(HWND hWnd)
{
	HDC hdc = GetDC(hWnd);
	int pt = 13; // roughly two points above the dialog's 9pt body font
	int height = -MulDiv(pt, GetDeviceCaps(hdc, LOGPIXELSY), 72);
	ReleaseDC(hWnd, hdc);

	return (CreateFontW(height, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
	    DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
	    CLEARTYPE_QUALITY, VARIABLE_PITCH | FF_SWISS, L"Segoe UI"));
}

// Loads iconId at (cx x cy) and assigns it to the SS_ICON static
// control idc, so the icon is crisp instead of whatever default size
// happens to be baked into the .ico's first frame.
static void
SetDlgIcon(HWND hDlg, int idc, int iconId, int cx, int cy)
{
	HICON hIcon = (HICON)LoadImageW(
	    (HINSTANCE)GetWindowLongPtrW(hDlg, GWLP_HINSTANCE),
	    MAKEINTRESOURCEW(iconId), IMAGE_ICON, cx, cy, LR_DEFAULTCOLOR);
	if (hIcon)
		SendDlgItemMessageW(hDlg, idc, STM_SETICON, (WPARAM)hIcon, 0);
}

// True if the system is currently in dark app mode (per the same
// registry value Explorer/Settings use). Doesn't force either mode --
// callers should just mirror whatever this reports.
static BOOL
SystemIsDarkMode(void)
{
	HKEY hKey;
	if (RegOpenKeyExW(HKEY_CURRENT_USER,
	    L"Software\\Microsoft\\Windows\\CurrentVersion\\"
	    "Themes\\Personalize",
	    0, KEY_READ, &hKey) != ERROR_SUCCESS)
		return (FALSE);

	DWORD val = 1, sz = sizeof (val), type = 0;
	LSTATUS st = RegQueryValueExW(hKey, L"AppsUseLightTheme", NULL, &type,
	    (LPBYTE)&val, &sz);
	RegCloseKey(hKey);
	if (st != ERROR_SUCCESS || type != REG_DWORD)
		return (FALSE);
	return (val == 0);
}

// Applies (or clears) dark titlebar/control theming on hWnd to match
// the system light/dark setting. Classic Win32 dialogs -- even with
// the comctl32 v6 manifest -- don't pick this up on their own.
static void
ApplyThemeFollowSystem(HWND hWnd)
{
	BOOL dark = SystemIsDarkMode();

	// DWMWA_USE_IMMERSIVE_DARK_MODE = 20 (post-20H1); harmless no-op
	// on older builds that don't recognize it.
	BOOL dwmDark = dark;
	DwmSetWindowAttribute(hWnd, 20, &dwmDark, sizeof (dwmDark));

	SetWindowTheme(hWnd, dark ? L"DarkMode_Explorer" : L"Explorer", NULL);
}
