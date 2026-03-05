// SPDX-License-Identifier: CDDL-1.0
/*
 * CDDL HEADER START
 *
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this
 * source.  A copy of the CDDL is also available via the Internet at
 * http://www.illumos.org/license/CDDL.
 *
 * CDDL HEADER END
 */

/*
 * Copyright (c) 2026 by Garth Snyder. All rights reserved.
 */

#ifndef	_ZSTREAM_UTIL_H
#define	_ZSTREAM_UTIL_H

#ifdef	__cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <sys/zfs_ioctl.h>
#include <sys/zio_checksum.h>

/*
 * The safe_ versions of the functions below terminate the process if the
 * operation doesn't succeed instead of returning an error.
 */
extern void *
safe_malloc(size_t size);

extern void *
safe_calloc(size_t n);

extern int
sfread(void *buf, size_t size, FILE *fp);

/*
 * 1) Update checksum with the record header up to drr_checksum.
 * 2) Update checksum field in the record header.
 * 3) Update checksum with the checksum field in the record header.
 * 4) Update checksum with the contents of the payload.
 * 5) Write header and payload to fd.
 */
extern int
dump_record(dmu_replay_record_t *drr, void *payload, size_t payload_len,
	zio_cksum_t *zc, int outfd);

#ifdef __cplusplus
}
#endif

#endif  /* _ZSTREAM_UTIL_H */
