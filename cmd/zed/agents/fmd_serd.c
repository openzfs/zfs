// SPDX-License-Identifier: CDDL-1.0
/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License, Version 1.0 only
 * (the "License").  You may not use this file except in compliance
 * with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or https://opensource.org/licenses/CDDL-1.0.
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
 * Copyright 2004 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 *
 * Copyright (c) 2016, Intel Corporation.
 */

#include <assert.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <sys/list.h>
#include <sys/time.h>

#include "fmd_api.h"
#include "fmd_serd.h"
#include "../zed_log.h"


#define	FMD_STR_BUCKETS		211


#ifdef SERD_ENG_DEBUG
#define	serd_log_msg(fmt, ...) \
	zed_log_msg(LOG_INFO, fmt, __VA_ARGS__)
#else
#define	serd_log_msg(fmt, ...)
#endif


/*
 * SERD Engine Backend
 */

/*
 * Compute the delta between events in nanoseconds.  To account for very old
 * events which are replayed, we must handle the case where time is negative.
 * We convert the hrtime_t's to unsigned 64-bit integers and then handle the
 * case where 'old' is greater than 'new' (i.e. high-res time has wrapped).
 */
static hrtime_t
fmd_event_delta(hrtime_t t1, hrtime_t t2)
{
	uint64_t old = t1;
	uint64_t new = t2;

	return (new >= old ? new - old : (UINT64_MAX - old) + new + 1);
}

static fmd_serd_eng_t *
fmd_serd_eng_alloc(const char *name, uint64_t n, hrtime_t t)
{
	fmd_serd_eng_t *sgp;

	sgp = malloc(sizeof (fmd_serd_eng_t));
	if (sgp == NULL) {
		perror("malloc");
		exit(EXIT_FAILURE);
	}
	memset(sgp, 0, sizeof (fmd_serd_eng_t));

	sgp->sg_name = strdup(name);
	if (sgp->sg_name == NULL) {
		perror("strdup");
		exit(EXIT_FAILURE);
	}

	sgp->sg_flags = FMD_SERD_DIRTY;
	sgp->sg_n = n;
	sgp->sg_t = t;

	list_create(&sgp->sg_list, sizeof (fmd_serd_elem_t),
	    offsetof(fmd_serd_elem_t, se_list));

	return (sgp);
}

static void
fmd_serd_eng_free(fmd_serd_eng_t *sgp)
{
	fmd_serd_eng_reset(sgp);
	free(sgp->sg_name);
	list_destroy(&sgp->sg_list);
	free(sgp);
}

/*
 * sourced from fmd_string.c
 */
static ulong_t
fmd_strhash(const char *key)
{
	ulong_t g, h = 0;
	const char *p;

	for (p = key; *p != '\0'; p++) {
		h = (h << 4) + *p;

		if ((g = (h & 0xf0000000)) != 0) {
			h ^= (g >> 24);
			h ^= g;
		}
	}

	return (h);
}

void
fmd_serd_hash_create(fmd_serd_hash_t *shp)
{
	shp->sh_hashlen = FMD_STR_BUCKETS;
	shp->sh_hash = calloc(shp->sh_hashlen, sizeof (void *));
	shp->sh_count = 0;

	if (shp->sh_hash == NULL) {
		perror("calloc");
		exit(EXIT_FAILURE);
	}

}

void
fmd_serd_hash_destroy(fmd_serd_hash_t *shp)
{
	fmd_serd_eng_t *sgp, *ngp;
	uint_t i;

	for (i = 0; i < shp->sh_hashlen; i++) {
		for (sgp = shp->sh_hash[i]; sgp != NULL; sgp = ngp) {
			ngp = sgp->sg_next;
			fmd_serd_eng_free(sgp);
		}
	}

	free(shp->sh_hash);
	memset(shp, 0, sizeof (fmd_serd_hash_t));
}

void
fmd_serd_hash_apply(fmd_serd_hash_t *shp, fmd_serd_eng_f *func, void *arg)
{
	fmd_serd_eng_t *sgp;
	uint_t i;

	for (i = 0; i < shp->sh_hashlen; i++) {
		for (sgp = shp->sh_hash[i]; sgp != NULL; sgp = sgp->sg_next)
			func(sgp, arg);
	}
}

fmd_serd_eng_t *
fmd_serd_eng_insert(fmd_serd_hash_t *shp, const char *name,
    uint_t n, hrtime_t t)
{
	uint_t h = fmd_strhash(name) % shp->sh_hashlen;
	fmd_serd_eng_t *sgp = fmd_serd_eng_alloc(name, n, t);

	serd_log_msg("  SERD Engine: inserting  %s N %d T %llu",
	    name, (int)n, (long long unsigned)t);

	sgp->sg_next = shp->sh_hash[h];
	shp->sh_hash[h] = sgp;
	shp->sh_count++;

	return (sgp);
}

fmd_serd_eng_t *
fmd_serd_eng_lookup(fmd_serd_hash_t *shp, const char *name)
{
	uint_t h = fmd_strhash(name) % shp->sh_hashlen;
	fmd_serd_eng_t *sgp;

	for (sgp = shp->sh_hash[h]; sgp != NULL; sgp = sgp->sg_next) {
		if (strcmp(name, sgp->sg_name) == 0)
			return (sgp);
	}

	return (NULL);
}

void
fmd_serd_eng_delete(fmd_serd_hash_t *shp, const char *name)
{
	uint_t h = fmd_strhash(name) % shp->sh_hashlen;
	fmd_serd_eng_t *sgp, **pp = &shp->sh_hash[h];

	serd_log_msg("  SERD Engine: deleting %s", name);

	for (sgp = *pp; sgp != NULL; sgp = sgp->sg_next) {
		if (strcmp(sgp->sg_name, name) != 0)
			pp = &sgp->sg_next;
		else
			break;
	}

	if (sgp != NULL) {
		*pp = sgp->sg_next;
		fmd_serd_eng_free(sgp);
		assert(shp->sh_count != 0);
		shp->sh_count--;
	}
}

static void
fmd_serd_eng_discard(fmd_serd_eng_t *sgp, fmd_serd_elem_t *sep)
{
	list_remove(&sgp->sg_list, sep);
	sgp->sg_count--;

	serd_log_msg("  SERD Engine: discarding %s, %d remaining",
	    sgp->sg_name, (int)sgp->sg_count);

	free(sep);
}

int
fmd_serd_eng_record(fmd_serd_eng_t *sgp, hrtime_t hrt)
{
	fmd_serd_elem_t *sep, *oep;

	/*
	 * If the fired flag is already set, return false and discard the
	 * event.  This means that the caller will only see the engine "fire"
	 * once until fmd_serd_eng_reset() is called.  The fmd_serd_eng_fired()
	 * function can also be used in combination with fmd_serd_eng_record().
	 */
	if (sgp->sg_flags & FMD_SERD_FIRED) {
		serd_log_msg("  SERD Engine: record %s already fired!",
		    sgp->sg_name);
		return (B_FALSE);
	}

	while (sgp->sg_count >= sgp->sg_n)
		fmd_serd_eng_discard(sgp, list_tail(&sgp->sg_list));

	sep = malloc(sizeof (fmd_serd_elem_t));
	if (sep == NULL) {
		perror("malloc");
		exit(EXIT_FAILURE);
	}
	sep->se_hrt = hrt;

	list_insert_head(&sgp->sg_list, sep);
	sgp->sg_count++;

	serd_log_msg("  SERD Engine: recording %s of %d (%llu)",
	    sgp->sg_name, (int)sgp->sg_count, (long long unsigned)hrt);

	/*
	 * Pick up the oldest element pointer for comparison to 'sep'.  We must
	 * do this after adding 'sep' because 'oep' and 'sep' can be the same.
	 */
	oep = list_tail(&sgp->sg_list);

	if (sgp->sg_count >= sgp->sg_n &&
	    fmd_event_delta(oep->se_hrt, sep->se_hrt) <= sgp->sg_t) {
		sgp->sg_flags |= FMD_SERD_FIRED | FMD_SERD_DIRTY;
		serd_log_msg("  SERD Engine: fired %s", sgp->sg_name);
		return (B_TRUE);
	}

	sgp->sg_flags |= FMD_SERD_DIRTY;
	return (B_FALSE);
}

int
fmd_serd_eng_fired(fmd_serd_eng_t *sgp)
{
	return (sgp->sg_flags & FMD_SERD_FIRED);
}

int
fmd_serd_eng_empty(fmd_serd_eng_t *sgp)
{
	return (sgp->sg_count == 0);
}

void
fmd_serd_eng_reset(fmd_serd_eng_t *sgp)
{
	serd_log_msg("  SERD Engine: resetting %s", sgp->sg_name);

	while (sgp->sg_count != 0)
		fmd_serd_eng_discard(sgp, list_head(&sgp->sg_list));

	sgp->sg_flags &= ~FMD_SERD_FIRED;
	sgp->sg_flags |= FMD_SERD_DIRTY;
}

void
fmd_serd_eng_gc(fmd_serd_eng_t *sgp, void *arg)
{
	(void) arg;
	fmd_serd_elem_t *sep, *nep;
	hrtime_t hrt;

	if (sgp->sg_count == 0 || (sgp->sg_flags & FMD_SERD_FIRED))
		return; /* no garbage collection needed if empty or fired */

	sep = list_head(&sgp->sg_list);
	if (sep == NULL)
		return;

	hrt = sep->se_hrt - sgp->sg_t;

	for (sep = list_head(&sgp->sg_list); sep != NULL; sep = nep) {
		if (sep->se_hrt >= hrt)
			break; /* sep and subsequent events are all within T */

		nep = list_next(&sgp->sg_list, sep);
		fmd_serd_eng_discard(sgp, sep);
		sgp->sg_flags |= FMD_SERD_DIRTY;
	}
}
