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
 * Mariusz Zaborski <mariusz.zaborski@klarasystems.com>
 * Fred Weigel <fred.weigel@klarasystems.com>
 * under sponsorship from Wasabi Technology, Inc. and Klara Inc.
 */
/*
 * This file implements a round-robin database that stores timestamps and txg
 * numbers. Due to limited space, we use a round-robin approach, where
 * the oldest records are overwritten when there is no longer enough room.
 * This is a best-effort mechanism, and the database should be treated as
 * an approximation. Consider this before consuming it.
 *
 * The database is linear, meaning we assume each new entry is newer than the
 * ones already stored. Because of this, if time is manipulated, the database
 * will only accept records that are newer than the existing ones.
 * (For example, jumping 10 years into the future and then back can lead to
 * situation when for 10 years we wont write anything to database)
 *
 * All times stored in the database use UTC, which makes it easy to convert to
 * and from local time.
 *
 * Each database holds 256 records (as defined in the `RRD_MAX_ENTRIES` macro).
 * This limit comes from the maximum size of a ZAP object, where we store the
 * binary blob.
 *
 * We've split the database into three smaller ones.
 * The `minute database` provides high resolution (default: every 10 minutes),
 * but only covers approximately 1.5 days. This gives a detailed view of recent
 * activity, useful, for example, when performing a scrub of the last hour.
 * The `daily database` records one txg per day. With 256 entries, it retains
 * roughly 8 months of data. This allows users to scrub or analyze txgs across
 * a range of days.
 * The `monthly database` stores one record per month, giving approximately
 * 21 years of history.
 * All these calculations assume the worst-case scenario: the pool is always
 * online and actively written to.
 *
 * A potential source of confusion is that the database does not store data
 * while the pool is offline, leading to potential gaps in timeline. Also,
 * the database contains no records from before this feature was enabled.
 * Both, upon reflection, are expected.
 */
#include <sys/zfs_context.h>

#include "zfs_crrd.h"

rrd_data_t *
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
	rrd_data_t *tail;

	tail = rrd_tail_entry(rrd);
	if (tail != NULL && tail->rrdd_time == time) {
		if (tail->rrdd_txg < txg) {
			tail->rrdd_txg = txg;
		} else {
			return;
		}
	}

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
	hrtime_t daydiff, monthdiff, minutedif;

	minutedif = time - rrd_tail(&db->dbr_minutes);
	daydiff = time - rrd_tail(&db->dbr_days);
	monthdiff = time - rrd_tail(&db->dbr_months);

	if (monthdiff >= 0 && monthdiff >= 30 * 24 * 60 * 60)
		rrd_add(&db->dbr_months, time, txg);
	else if (daydiff >= 0 && daydiff >= 24 * 60 * 60)
		rrd_add(&db->dbr_days, time, txg);
	else if (minutedif >= 0)
		rrd_add(&db->dbr_minutes, time, txg);
}

/*
 * We could do a binary search here, but the routine isn't frequently
 * called and the data is small so we stick to a simple loop.
 */
static const rrd_data_t *
rrd_query(rrd_t *rrd, hrtime_t tv, dbrrd_rounding_t rounding)
{
	const rrd_data_t *data = NULL;

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
dbrrd_closest(hrtime_t tv, const rrd_data_t *r1, const rrd_data_t *r2)
{

	if (r1 == NULL)
		return (r2);
	if (r2 == NULL)
		return (r1);

	return (ABS(tv - (hrtime_t)r1->rrdd_time) <
	    ABS(tv - (hrtime_t)r2->rrdd_time) ? r1 : r2);
}

uint64_t
dbrrd_query(dbrrd_t *r, hrtime_t tv, dbrrd_rounding_t rounding)
{
	const rrd_data_t *data, *dm, *dd, *dy;

	data = NULL;
	dm = rrd_query(&r->dbr_minutes, tv, rounding);
	dd = rrd_query(&r->dbr_days, tv, rounding);
	dy = rrd_query(&r->dbr_months, tv, rounding);

	data = dbrrd_closest(tv, dbrrd_closest(tv, dd, dm), dy);

	return (data == NULL ? 0 : data->rrdd_txg);
}
