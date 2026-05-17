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

#ifndef _ZSTREAM_MODULES_H
#define	_ZSTREAM_MODULES_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * This file aggregates all zstream_chain utility modules into a single
 * header and defines macros for standard input and output operations.
 */

#include "zstream_byteswap.h"
#include "zstream_chain.h"
#include "zstream_fletcher4.h"
#include "zstream_io.h"
#include "zstream_recompress.h"
#include "zstream_util.h"
#include "zstream_validate.h"

#define	READ_STEP 0

#define	STANDARD_INPUT_STACK(infile)					\
	serial_read_stream(infile),					\
	serial_validate_fletcher4(),					\
	serial_byteswap(BS_INCOMING),					\
	serial_validate_records()

#define	STANDARD_OUTPUT_STACK(outfile)					\
	serial_byteswap(BS_OUTGOING),					\
	serial_add_fletcher4(),						\
	serial_write_stream(outfile),					\
	chain_terminator()

#define	NULL_OUTPUT_STACK()						\
	serial_null_output(),						\
	chain_terminator()

#ifdef __cplusplus
}
#endif

#endif  /* _ZSTREAM_MODULES_H */
