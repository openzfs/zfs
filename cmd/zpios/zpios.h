/*
 *  ZPIOS is a heavily modified version of the original PIOS test code.
 *  It is designed to have the test code running in the Linux kernel
 *  against ZFS while still being flexibly controled from user space.
 *
 *  Copyright (C) 2008-2010 Lawrence Livermore National Security, LLC.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Brian Behlendorf <behlendorf1@llnl.gov>.
 *  LLNL-CODE-403049
 *
 *  Original PIOS Test Code
 *  Copyright (C) 2004 Cluster File Systems, Inc.
 *  Written by Peter Braam <braam@clusterfs.com>
 *             Atul Vidwansa <atul@clusterfs.com>
 *             Milind Dumbare <milind@clusterfs.com>
 *
 *  This file is part of ZFS on Linux.
 *  For details, see <http://zfsonlinux.org/>.
 *
 *  ZPIOS is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the
 *  Free Software Foundation; either version 2 of the License, or (at your
 *  option) any later version.
 *
 *  ZPIOS is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with ZPIOS.  If not, see <http://www.gnu.org/licenses/>.
 *
 *  Copyright (c) 2015, Intel Corporation.
 */

#ifndef _ZPIOS_H
#define	_ZPIOS_H

#include <zpios-ctl.h>

#define	VERSION_SIZE		64

/* Regular expressions */
#define	REGEX_NUMBERS		"^[0-9]+$"
#define	REGEX_NUMBERS_COMMA	"^([0-9]+,)*[0-9]+$"
#define	REGEX_SIZE		"^[0-9]+[kKmMgGtT]?$"
#define	REGEX_SIZE_COMMA	"^([0-9]+[kKmMgGtT]?,)*[0-9]+[kKmMgGtT]?$"

/* Flags for low, high, incr */
#define	FLAG_SET		0x01
#define	FLAG_LOW		0x02
#define	FLAG_HIGH		0x04
#define	FLAG_INCR		0x08

#define	TRUE			1
#define	FALSE			0

#define	KB			(1024)
#define	MB			(KB * 1024)
#define	GB			(MB * 1024)
#define	TB			(GB * 1024)

#define	KMGT_SIZE		16

/*
 * All offsets, sizes and counts can be passed to the application in
 * multiple ways.
 * 1. a value (stored in val[0], val_count will be 1)
 * 2. a comma separated list of values (stored in val[], using val_count)
 * 3. a range and block sizes, low, high, factor (val_count must be 0)
 */
typedef struct pios_range_repeat {
	uint64_t val[32];		/* Comma sep array, or low, high, inc */
	uint64_t val_count;		/* Num of values */
	uint64_t val_low;
	uint64_t val_high;
	uint64_t val_inc_perc;
	uint64_t next_val;		/* For multiple runs in get_next() */
} range_repeat_t;

typedef struct cmd_args {
	range_repeat_t T;		/* Thread count */
	range_repeat_t N;		/* Region count */
	range_repeat_t O;		/* Offset count */
	range_repeat_t C;		/* Chunksize */
	range_repeat_t S;		/* Regionsize */
	range_repeat_t B;		/* Blocksize */

	const char *pool;		/* Pool */
	const char *name;		/* Name */
	uint32_t flags;			/* Flags */
	uint32_t block_size;		/* ZFS block size */
	uint32_t io_type;		/* DMUIO only */
	uint32_t verbose;		/* Verbose */
	uint32_t human_readable;	/* Human readable output */

	uint64_t regionnoise;		/* Region noise */
	uint64_t chunknoise;		/* Chunk noise */
	uint64_t thread_delay;		/* Thread delay */

	char pre[ZPIOS_PATH_SIZE];	/* Pre-exec hook */
	char post[ZPIOS_PATH_SIZE];	/* Post-exec hook */
	char log[ZPIOS_PATH_SIZE];	/* Requested log dir */

	/* Control */
	int current_id;
	uint64_t current_T;
	uint64_t current_N;
	uint64_t current_C;
	uint64_t current_S;
	uint64_t current_O;
	uint64_t current_B;

	uint32_t rc;
} cmd_args_t;

int set_count(char *pattern1, char *pattern2, range_repeat_t *range,
    char *optarg, uint32_t *flags, char *arg);
int set_lhi(char *pattern, range_repeat_t *range, char *optarg,
    int flag, uint32_t *flag_thread, char *arg);
int set_noise(uint64_t *noise, char *optarg, char *arg);
int set_load_params(cmd_args_t *args, char *optarg);
int check_mutual_exclusive_command_lines(uint32_t flag, char *arg);
void print_stats_header(cmd_args_t *args);
void print_stats(cmd_args_t *args, zpios_cmd_t *cmd);

#endif /* _ZPIOS_H */
