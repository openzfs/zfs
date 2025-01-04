// SPDX-License-Identifier: CDDL-1.0
/*
 * This file is part of the ZFS Event Daemon (ZED).
 *
 * Developed at Lawrence Livermore National Laboratory (LLNL-CODE-403049).
 * Copyright (C) 2013-2014 Lawrence Livermore National Security, LLC.
 * Refer to the OpenZFS git commit log for authoritative copyright attribution.
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License Version 1.0 (CDDL-1.0).
 * You can obtain a copy of the license from the top-level file
 * "OPENSOLARIS.LICENSE" or at <http://opensource.org/licenses/CDDL-1.0>.
 * You may not use this file except in compliance with the license.
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
