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
 * Copyright (c) 2018 Julian Heuking <J.Heuking@beckhoff.com>
 */

#pragma once

#include <windows.h>
#include <SetupAPI.h>
#include <stdio.h>
#include <winsvc.h>

DWORD zfs_install(char *);
DWORD zfs_uninstall(char *);
DWORD executeInfSection(const char *, char *);
DWORD startService(char *);
void printUsage();
DWORD send_zfs_ioc_unregister_fs();
DWORD installRootDevice(char *inf_path);
DWORD uninstallRootDevice(char *inf_path);
