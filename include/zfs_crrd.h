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
 * Copyright (c) 2024 Klara Inc.
 *
 * This software was developed by
 * Mariusz Zaborski <mariusz.zaborski@klarasystems.com>
 * Fred Weigel <fred.weigel@klarasystems.com>
 * under sponsorship from Wasabi Technology, Inc. and Klara Inc.
 */

#ifndef _CRRD_H_
#define	_CRRD_H_

#define	RRD_MAX_ENTRIES	256

#define	RRD_ENTRY_SIZE	sizeof (uint64_t)
#define	RRD_STRUCT_ELEM	(sizeof (rrd_t) / RRD_ENTRY_SIZE)

typedef enum {
	DBRRD_FLOOR,
	DBRRD_CEILING
} dbrrd_rounding_t;

typedef struct {
	uint64_t	rrdd_time;
	uint64_t	rrdd_txg;
} rrd_data_t;

typedef struct {
	uint64_t	rrd_head;	/* head (beginning) */
	uint64_t	rrd_tail;	/* tail (end) */
	uint64_t	rrd_length;

	rrd_data_t	rrd_entries[RRD_MAX_ENTRIES];
} rrd_t;

typedef struct {
	rrd_t		dbr_minutes;
	rrd_t		dbr_days;
	rrd_t		dbr_months;
} dbrrd_t;

size_t rrd_len(const rrd_t *rrd);

const rrd_data_t *rrd_entry(const rrd_t *r, size_t i);
rrd_data_t *rrd_tail_entry(rrd_t *rrd);
uint64_t rrd_tail(rrd_t *rrd);
uint64_t rrd_get(const rrd_t *rrd, size_t i);

void rrd_add(rrd_t *rrd, hrtime_t time, uint64_t txg);

void dbrrd_add(dbrrd_t *db, hrtime_t time, uint64_t txg);
uint64_t dbrrd_query(dbrrd_t *r, hrtime_t tv, dbrrd_rounding_t rouding);
hrtime_t dbrrd_latest_time(dbrrd_t *r);

#endif
