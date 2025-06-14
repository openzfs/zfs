// SPDX-License-Identifier: CDDL-1.0
/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
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
 * Copyright (c) 2024 Klara Inc.
 *
 * This software was developed by
 * Fred Weigel <fred.weigel@klarasystems.com>
 * Mariusz Zaborski <mariusz.zaborski@klarasystems.com>
 * under sponsorship from Wasabi Technology, Inc. and Klara Inc.
 */
/*
 * This file implements a round robin database.
 */
#include <sys/zfs_context.h>

#include "zfs_crrd.h"

const rrd_data_t *
rrd_tail_entry(rrd_t *rrd)
{
	size_t n;

	if (rrd_len(rrd) == 0)
		return (NULL);

	if (rrd->rrd_tail == 0)
		n = RRD_MAX_ENTRIES - 1;
	else
		n = rrd->rrd_tail - 1;

	return (&rrd->rrd_entries[n]);
}

uint64_t
rrd_tail(rrd_t *rrd)
{
	const rrd_data_t *tail;

	tail = rrd_tail_entry(rrd);

	return (tail == NULL ? 0 : tail->rrdd_time);
}

/*
 * Return length of data in the rrd.
 * rrd_get works from 0..rrd_len()-1.
 */
size_t
rrd_len(rrd_t *rrd)
{

	return (rrd->rrd_length);
}

const rrd_data_t *
rrd_entry(rrd_t *rrd, size_t i)
{
	size_t n;

	if (i >= rrd_len(rrd)) {
		return (0);
	}

	n = (rrd->rrd_head + i) % RRD_MAX_ENTRIES;
	return (&rrd->rrd_entries[n]);
}

uint64_t
rrd_get(rrd_t *rrd, size_t i)
{
	const rrd_data_t *data = rrd_entry(rrd, i);

	return (data == NULL ? 0 : data->rrdd_txg);
}

/* Add value to database. */
void
rrd_add(rrd_t *rrd, hrtime_t time, uint64_t txg)
{

	rrd->rrd_entries[rrd->rrd_tail].rrdd_time = time;
	rrd->rrd_entries[rrd->rrd_tail].rrdd_txg = txg;

	rrd->rrd_tail = (rrd->rrd_tail + 1) % RRD_MAX_ENTRIES;

	if (rrd->rrd_length < RRD_MAX_ENTRIES) {
		rrd->rrd_length++;
	} else {
		rrd->rrd_head = (rrd->rrd_head + 1) % RRD_MAX_ENTRIES;
	}
}

void
dbrrd_add(dbrrd_t *db, hrtime_t time, uint64_t txg)
{
	hrtime_t daydiff, monthdiff;

	daydiff = time - rrd_tail(&db->dbr_days);
	monthdiff = time - rrd_tail(&db->dbr_months);

	rrd_add(&db->dbr_minutes, time, txg);
	if (daydiff >= 0 && daydiff >= SEC2NSEC(60 * 3600))
		rrd_add(&db->dbr_days, time, txg);
	if (monthdiff >= 0 && monthdiff >= SEC2NSEC(60 * 3600 * 30))
		rrd_add(&db->dbr_months, time, txg);
}

/*
 * XXXosho: We might want to do a binary search here,
 *          although the data is small, and the routine
 *          isn't used so often that we stick to simple methods.
 */
static const rrd_data_t *
rrd_query(rrd_t *rrd, hrtime_t tv, dbrrd_rounding_t rounding)
{
	const rrd_data_t *data;

	data = NULL;
	for (size_t i = 0; i < rrd_len(rrd); i++) {
		const rrd_data_t *cur = rrd_entry(rrd, i);

		if (rounding == DBRRD_FLOOR) {
			if (tv < cur->rrdd_time) {
				break;
			}
			data = cur;
		} else {
			/* DBRRD_CEILING */
			if (tv <= cur->rrdd_time) {
				data = cur;
				break;
			}
		}
	}

	return (data);
}

static const rrd_data_t *
dbrrd_min(const rrd_data_t *r1, const rrd_data_t *r2)
{
	if (r1 == NULL)
		return (r2);
	if (r2 == NULL)
		return (r1);

	return (r1->rrdd_txg < r2->rrdd_txg ? r1 : r2);
}

static const rrd_data_t *
dbrrd_max(const rrd_data_t *r1, const rrd_data_t *r2)
{
	if (r1 == NULL)
		return (r2);
	if (r2 == NULL)
		return (r1);

	return (r1->rrdd_txg > r2->rrdd_txg ? r1 : r2);
}

uint64_t
dbrrd_query(dbrrd_t *r, hrtime_t tv, dbrrd_rounding_t rounding)
{
	const rrd_data_t *data, *dm, *dd, *dy;

	data = NULL;
	dm = rrd_query(&r->dbr_minutes, tv, rounding);
	dd = rrd_query(&r->dbr_days, tv, rounding);
	dy = rrd_query(&r->dbr_months, tv, rounding);

	if (rounding == DBRRD_FLOOR) {
		data = dbrrd_max(dbrrd_max(dd, dm), dy);
	} else {
		data = dbrrd_min(dbrrd_min(dd, dm), dy);
	}

	return (data == NULL ? 0 : data->rrdd_txg);
}
