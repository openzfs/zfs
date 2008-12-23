/*
 * This file is part of the ZFS Linux port.
 *
 *  Copyright (c) 2008 Lawrence Livermore National Security, LLC.
 *  Produced at Lawrence Livermore National Laboratory
 *  Written by:
 *          Brian Behlendorf <behlendorf1@llnl.gov>,
 *          Herb Wartens <wartens2@llnl.gov>,
 *          Jim Garlick <garlick@llnl.gov>
 * LLNL-CODE-403049
 *
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License, Version 1.0 only
 * (the "License").  You may not use this file except in compliance
 * with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
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

#ifndef _ZPIOS_H
#define _ZPIOS_H

#include <zpios-ctl.h>

#define VERSION_SIZE		64

/* Regular expressions */
#define REGEX_NUMBERS		"^[0-9]*[0-9]$"
#define REGEX_NUMBERS_COMMA	"^([0-9]+,)*[0-9]+$"
#define REGEX_SIZE		"^[0-9][0-9]*[kmgt]$"
#define REGEX_SIZE_COMMA	"^([0-9][0-9]*[kmgt]+,)*[0-9][0-9]*[kmgt]$"

/* Flags for low, high, incr */
#define FLAG_SET		0x01
#define FLAG_LOW		0x02
#define FLAG_HIGH		0x04
#define FLAG_INCR		0x08

#define TRUE			1
#define FALSE			0

#define KB			(1024)
#define MB			(KB * 1024)
#define GB			(MB * 1024)
#define TB			(GB * 1024)

/* All offsets, sizes and counts can be passed to the application in
 * multiple ways.
 * 1. a value (stored in val[0], val_count will be 1)
 * 2. a comma separated list of values (stored in val[], using val_count)
 * 3. a range and block sizes, low, high, factor (val_count must be 0)
 */
typedef struct pios_range_repeat {
	uint64_t val[32];        /* Comma sep array, or low, high, inc */
	uint64_t val_count;      /* Num of values */
	uint64_t val_low;
	uint64_t val_high;
	uint64_t val_inc_perc;
	uint64_t next_val;       /* Used for multiple runs in get_next() */
} range_repeat_t;

typedef struct cmd_args {
	range_repeat_t T;           /* Thread count */
	range_repeat_t N;           /* Region count */
	range_repeat_t O;           /* Offset count */
	range_repeat_t C;           /* Chunksize */
	range_repeat_t S;           /* Regionsize */

	const char *pool;           /* Pool */
	uint32_t flags;             /* Flags */
	uint32_t io_type;           /* DMUIO only */
	uint32_t verbose;           /* Verbose */
	uint32_t human_readable;    /* Human readable output */

	uint64_t regionnoise;       /* Region noise */
	uint64_t chunknoise;        /* Chunk noise */
	uint64_t thread_delay;      /* Thread delay */

	char pre[KPIOS_PATH_SIZE];  /* Pre-exec hook */
	char post[KPIOS_PATH_SIZE]; /* Post-exec hook */
	char log[KPIOS_PATH_SIZE];  /* Requested log dir */

	/* Control */
	int current_id;
	uint64_t current_T;
	uint64_t current_N;
	uint64_t current_C;
	uint64_t current_S;
	uint64_t current_O;

	uint32_t rc;
} cmd_args_t;

int set_count(char *pattern1, char *pattern2, range_repeat_t *range,
	      char *optarg, uint32_t *flags, char *arg);
int set_lhi(char *pattern, range_repeat_t *range, char *optarg,
	    int flag, uint32_t *flag_thread, char *arg);
int set_noise(uint64_t *noise, char *optarg, char *arg);
int set_load_params(cmd_args_t *args, char *optarg);
int check_mutual_exclusive_command_lines(uint32_t flag, char *arg);
void print_stats_header(void);
void print_stats(cmd_args_t *args, kpios_cmd_t *cmd);

#endif /* _ZPIOS_H */
