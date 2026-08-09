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
 * Copyright (c) 2018 by Delphix. All rights reserved.
 * Copyright (c) 2018 Datto Inc.
 */

#ifndef _SYS_DATASET_KSTATS_H
#define	_SYS_DATASET_KSTATS_H

#include <sys/wmsum.h>
#include <sys/dmu.h>
#include <sys/kstat.h>
#include <sys/zil.h>

typedef struct dataset_sum_stats_t {
	wmsum_t dss_writes;
	wmsum_t dss_nwritten;
	wmsum_t dss_reads;
	wmsum_t dss_nread;
	wmsum_t dss_nunlinks;
	wmsum_t dss_nunlinked;
} dataset_sum_stats_t;

typedef struct dataset_kstat_values {
	kstat_named_t dkv_ds_name;
	kstat_named_t dkv_writes;
	kstat_named_t dkv_nwritten;
	kstat_named_t dkv_reads;
	kstat_named_t dkv_nread;
	/*
	 * nunlinks is initialized to the unlinked set size on mount and
	 * is incremented whenever a new entry is added to the unlinked set
	 */
	kstat_named_t dkv_nunlinks;
	/*
	 * nunlinked is initialized to zero on mount and is incremented when an
	 * entry is removed from the unlinked set
	 */
	kstat_named_t dkv_nunlinked;
	/*
	 * Per dataset zil kstats
	 */
	zil_kstat_values_t dkv_zil_stats;
} dataset_kstat_values_t;

typedef struct dataset_kstats {
	dataset_sum_stats_t dk_sums;
	zil_sums_t dk_zil_sums;
	kstat_t *dk_kstats;
} dataset_kstats_t;

int dataset_kstats_create(dataset_kstats_t *, objset_t *);
void dataset_kstats_destroy(dataset_kstats_t *);
void dataset_kstats_rename(dataset_kstats_t *dk, const char *);

void dataset_kstats_update_write_kstats(dataset_kstats_t *, int64_t);
void dataset_kstats_update_read_kstats(dataset_kstats_t *, int64_t);

void dataset_kstats_update_nunlinks_kstat(dataset_kstats_t *, int64_t);
void dataset_kstats_update_nunlinked_kstat(dataset_kstats_t *, int64_t);

#endif /* _SYS_DATASET_KSTATS_H */
