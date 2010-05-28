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
 * Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef	_LIBNVPAIR_H
#define	_LIBNVPAIR_H

#include <sys/nvpair.h>
#include <stdlib.h>
#include <stdio.h>
#include <regex.h>

#ifdef	__cplusplus
extern "C" {
#endif

void nvlist_print(FILE *, nvlist_t *);
int nvpair_value_match(nvpair_t *, int, char *, char **);
int nvpair_value_match_regex(nvpair_t *, int, char *, regex_t *, char **);
void dump_nvlist(nvlist_t *, int);

#ifdef	__cplusplus
}
#endif

#endif	/* _LIBNVPAIR_H */
