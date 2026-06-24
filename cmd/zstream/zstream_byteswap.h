// SPDX-License-Identifier: CDDL-1.0
/*
 * CDDL HEADER START
 *
 * This file and its contents are supplied under the terms of the Common
 * Development and Distribution License ("CDDL"), version 1.0. You may only use
 * this file in accordance with the terms of version 1.0 of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this source. A
 * copy of the CDDL is also available via the Internet at
 * http://www.illumos.org/license/CDDL.
 *
 * CDDL HEADER END
 */

/*
 * Copyright (c) 2026 by Garth Snyder. All rights reserved.
 */

#ifndef _ZSTREAM_BYTESWAP_H
#define	_ZSTREAM_BYTESWAP_H

#ifdef __cplusplus
extern "C" {
#endif

#include "zstream_io.h"

#define	MAX_BYTESWAP 4  /* Most swapping ops in a chain */

/*
 * Byteswapping is generally done both on input and on output. By default,
 * the stream's endianness is preserved. That is, opposite-endian streams
 * are byteswapped for processing by other modules, then ultimately
 * de-byteswapped for output.
 */
typedef enum { BS_INCOMING, BS_OUTGOING } byteswap_stage_t;

chain_step_t
serial_byteswap(byteswap_stage_t stage);

/*
 * Unconditionally swap a record. drr_type is passed in separately because
 * we don't know whether we're doing input or output swapping. We need
 * that value in native byte order to know how to swap the rest of the
 * record.
 */
extern void
byteswap_record(dmu_replay_record_t *drr, uint32_t drr_type);

#ifdef __cplusplus
}
#endif

#endif  /* _ZSTREAM_BYTESWAP_H */
