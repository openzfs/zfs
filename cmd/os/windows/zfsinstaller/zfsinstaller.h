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
 * Copyright (c) 2018 Julian Heuking <J.Heuking@beckhoff.com>
 */

#pragma once

#include <windows.h>
#include <SetupAPI.h>
#include <stdio.h>
#include <winsvc.h>

DWORD zfs_install(char *);
DWORD zfs_uninstall(char *);
DWORD zvol_install(char *);
DWORD zvol_uninstall(char *);
DWORD executeInfSection(const char *, char *);
DWORD startService(char *);
void printUsage();
DWORD send_zfs_ioc_unregister_fs();
DWORD installRootDevice(char *inf_path, bool IsServiceRunning, const char *);
DWORD uninstallRootDevice(char *inf_path, const char *);
