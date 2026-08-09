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
 * Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef FILE_COMMON_H
#define	FILE_COMMON_H

/*
 * header file for file_* utilities. These utilities
 * are used by the test cases to perform various file
 * operations (append writes, for example).
 */

#ifdef __cplusplus
extern "C" {
#endif

#ifndef _FILE_OFFSET_BITS
#define	_FILE_OFFSET_BITS 64
#endif

#ifndef _LARGEFILE64_SOURCE
#define	_LARGEFILE64_SOURCE
#endif

#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

#define	BLOCKSZ		8192
#define	DATA		0xa5
#define	DATA_RANGE	120
#define	BIGBUFFERSIZE	0x800000
#define	BIGFILESIZE	20

extern char *optarg;
extern int optind, opterr, optopt;

#ifdef __cplusplus
}
#endif

#endif /* FILE_COMMON_H */
