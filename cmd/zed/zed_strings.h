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
 * This file is part of the ZFS Event Daemon (ZED).
 *
 * Developed at Lawrence Livermore National Laboratory (LLNL-CODE-403049).
 * Copyright (C) 2013-2014 Lawrence Livermore National Security, LLC.
 */

#ifndef	ZED_STRINGS_H
#define	ZED_STRINGS_H

typedef struct zed_strings zed_strings_t;

zed_strings_t *zed_strings_create(void);
void zed_strings_destroy(zed_strings_t *zsp);
int zed_strings_add(zed_strings_t *zsp, const char *key, const char *s);
const char *zed_strings_first(zed_strings_t *zsp);
const char *zed_strings_next(zed_strings_t *zsp);
int zed_strings_count(zed_strings_t *zsp);

#endif	/* !ZED_STRINGS_H */
