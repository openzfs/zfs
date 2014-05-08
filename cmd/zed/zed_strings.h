/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license from the top-level
 * OPENSOLARIS.LICENSE or <http://opensource.org/licenses/CDDL-1.0>.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each file
 * and include the License file from the top-level OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */

/*
 * Developed at Lawrence Livermore National Laboratory (LLNL-CODE-403049).
 * Copyright (C) 2013-2014 Lawrence Livermore National Security, LLC.
 */

#ifndef	ZED_STRINGS_H
#define	ZED_STRINGS_H

typedef struct zed_strings zed_strings_t;

zed_strings_t * zed_strings_create(void);

void zed_strings_destroy(zed_strings_t *zsp);

int zed_strings_add(zed_strings_t *zsp, const char *s);

const char * zed_strings_first(zed_strings_t *zsp);

const char * zed_strings_next(zed_strings_t *zsp);

int zed_strings_count(zed_strings_t *zsp);

#endif	/* !ZED_STRINGS_H */
