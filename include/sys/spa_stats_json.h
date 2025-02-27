/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://opensource.org/licenses/CDDL-1.0.
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
 * Copyright (c) 2024, Klara, Inc.
 */

#ifndef _SYS_SPA_STATS_JSON_H
#define	_SYS_SPA_STATS_JSON_H

#include <sys/spa.h>
#include <sys/kstat.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct spa_stats_json {
	kmutex_t	ssj_lock;
	kstat_t		*ssj_kstat;
} spa_stats_json_t;

extern int spa_stats_json_generate(spa_t *spa, char *buf, size_t size);

#ifdef __cplusplus
}
#endif

#endif /* _SYS_SPA_STATS_JSON_H */
