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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <assert.h>
#include <regex.h>
#include "zpios.h"

/* extracts an unsigned int (64) and K,M,G,T from the string */
/* and returns a 64 bit value converted to the proper units */
static int
kmgt_to_uint64(const char *str, uint64_t *val)
{
	char *endptr;
	int rc = 0;

	*val = strtoll(str, &endptr, 0);
	if ((str == endptr) && (*val == 0))
		return (EINVAL);

	switch (endptr[0]) {
		case 'k': case 'K':
			*val = (*val) << 10;
			break;
		case 'm': case 'M':
			*val = (*val) << 20;
			break;
		case 'g': case 'G':
			*val = (*val) << 30;
			break;
		case 't': case 'T':
			*val = (*val) << 40;
			break;
		case '\0':
			break;
		default:
			rc = EINVAL;
	}

	return (rc);
}

static char *
uint64_to_kmgt(char *str, uint64_t val)
{
	char postfix[] = "kmgt";
	int i = -1;

	while ((val >= KB) && (i < 4)) {
		val = (val >> 10);
		i++;
	}

	if (i >= 4)
		(void) snprintf(str, KMGT_SIZE-1, "inf");
	else
		(void) snprintf(str, KMGT_SIZE-1, "%lu%c", (unsigned long)val,
		    (i == -1) ? '\0' : postfix[i]);

	return (str);
}

static char *
kmgt_per_sec(char *str, uint64_t v, double t)
{
	char postfix[] = "kmgt";
	double val = ((double)v) / t;
	int i = -1;

	while ((val >= (double)KB) && (i < 4)) {
		val /= (double)KB;
		i++;
	}

	if (i >= 4)
		(void) snprintf(str, KMGT_SIZE-1, "inf");
	else
		(void) snprintf(str, KMGT_SIZE-1, "%.2f%c", val,
		    (i == -1) ? '\0' : postfix[i]);

	return (str);
}

static char *
print_flags(char *str, uint32_t flags)
{
	str[0] = (flags & DMU_WRITE)  ? 'w' : '-';
	str[1] = (flags & DMU_READ)   ? 'r' : '-';
	str[2] = (flags & DMU_VERIFY) ? 'v' : '-';
	str[3] = (flags & DMU_REMOVE) ? 'c' : '-';
	str[4] = (flags & DMU_FPP)    ? 'p' : 's';
	str[5] = (flags & (DMU_WRITE_ZC | DMU_READ_ZC)) ? 'z' : '-';
	str[6] = (flags & DMU_WRITE_NOWAIT) ? 'O' : '-';
	str[7] = '\0';

	return (str);
}

static int
regex_match(const char *string, char *pattern)
{
	regex_t re = { 0 };
	int rc;

	rc = regcomp(&re, pattern, REG_EXTENDED | REG_NOSUB | REG_ICASE);
	if (rc) {
		fprintf(stderr, "Error: Couldn't do regcomp, %d\n", rc);
		return (rc);
	}

	rc = regexec(&re, string, (size_t) 0, NULL, 0);
	regfree(&re);

	return (rc);
}

/* fills the pios_range_repeat structure of comma separated values */
static int
split_string(const char *optarg, char *pattern, range_repeat_t *range)
{
	const char comma[] = ",";
	char *cp, *token[32];
	int rc, i = 0;

	if ((rc = regex_match(optarg, pattern)))
		return (rc);

	cp = strdup(optarg);
	if (cp == NULL)
		return (ENOMEM);

	do {
		/*
		 * STRTOK(3) Each subsequent call, with a null pointer as the
		 * value of the * first  argument, starts searching from the
		 * saved pointer and behaves as described above.
		 */
		token[i] = strtok(cp, comma);
		cp = NULL;
	} while ((token[i++] != NULL) && (i < 32));

	range->val_count = i - 1;

	for (i = 0; i < range->val_count; i++)
		kmgt_to_uint64(token[i], &range->val[i]);

	free(cp);
	return (0);
}

int
set_count(char *pattern1, char *pattern2, range_repeat_t *range,
    char *optarg, uint32_t *flags, char *arg)
{
	uint64_t count = range->val_count;

	if (flags)
		*flags |= FLAG_SET;

	range->next_val = 0;

	if (regex_match(optarg, pattern1) == 0) {
		kmgt_to_uint64(optarg, &range->val[0]);
		range->val_count = 1;
	} else if (split_string(optarg, pattern2, range) < 0) {
		fprintf(stderr, "Error: Incorrect pattern for %s, '%s'\n",
		    arg, optarg);
		return (EINVAL);
	} else if (count == range->val_count) {
		fprintf(stderr, "Error: input ignored for %s, '%s'\n",
		    arg, optarg);
	}

	return (0);
}

/*
 * Validates the value with regular expression and sets low, high, incr
 * according to value at which flag will be set. Sets the flag after.
 */
int
set_lhi(char *pattern, range_repeat_t *range, char *optarg,
    int flag, uint32_t *flag_thread, char *arg)
{
	int rc;

	if ((rc = regex_match(optarg, pattern))) {
		fprintf(stderr, "Error: Wrong pattern in %s, '%s'\n",
			arg, optarg);
		return (rc);
	}

	switch (flag) {
		case FLAG_LOW:
			kmgt_to_uint64(optarg, &range->val_low);
			break;
		case FLAG_HIGH:
			kmgt_to_uint64(optarg, &range->val_high);
			break;
		case FLAG_INCR:
			kmgt_to_uint64(optarg, &range->val_inc_perc);
			break;
		default:
			assert(0);
	}

	*flag_thread |= flag;

	return (0);
}

int
set_noise(uint64_t *noise, char *optarg, char *arg)
{
	if (regex_match(optarg, REGEX_NUMBERS) == 0) {
		kmgt_to_uint64(optarg, noise);
	} else {
		fprintf(stderr, "Error: Incorrect pattern for %s\n", arg);
		return (EINVAL);
	}

	return (0);
}

int
set_load_params(cmd_args_t *args, char *optarg)
{
	char *param, *search, comma[] = ",";
	int rc = 0;

	search = strdup(optarg);
	if (search == NULL)
		return (ENOMEM);

	while ((param = strtok(search, comma)) != NULL) {
		search = NULL;

		if (strcmp("fpp", param) == 0) {
			args->flags |= DMU_FPP; /* File Per Process/Thread */
		} else if (strcmp("ssf", param) == 0) {
			args->flags &= ~DMU_FPP; /* Single Shared File */
		} else if (strcmp("dmuio", param) == 0) {
			args->io_type |= DMU_IO;
			args->flags |= (DMU_WRITE | DMU_READ);
		} else {
			fprintf(stderr, "Invalid load: %s\n", param);
			rc = EINVAL;
		}
	}

	free(search);

	return (rc);
}


/*
 * Checks the low, high, increment values against the single value for
 * mutual exclusion, for e.g threadcount is mutually exclusive to
 * threadcount_low, ..._high, ..._incr
 */
int
check_mutual_exclusive_command_lines(uint32_t flag, char *arg)
{
	if ((flag & FLAG_SET) && (flag & (FLAG_LOW | FLAG_HIGH | FLAG_INCR))) {
		fprintf(stderr, "Error: --%s can not be given with --%s_low, "
		    "--%s_high or --%s_incr.\n", arg, arg, arg, arg);
		return (0);
	}

	if ((flag & (FLAG_LOW | FLAG_HIGH | FLAG_INCR)) && !(flag & FLAG_SET)) {
		if (flag != (FLAG_LOW | FLAG_HIGH | FLAG_INCR)) {
			fprintf(stderr, "Error: One or more values missing "
			    "from --%s_low, --%s_high, --%s_incr.\n",
			    arg, arg, arg);
			return (0);
		}
	}

	return (1);
}

void
print_stats_header(cmd_args_t *args)
{
	if (args->verbose) {
		printf(
		    "status    name        id\tth-cnt\trg-cnt\trg-sz\t"
		    "ch-sz\toffset\trg-no\tch-no\tth-dly\tflags\tblksz\ttime\t"
		    "cr-time\trm-time\twr-time\trd-time\twr-data\twr-ch\t"
		    "wr-bw\trd-data\trd-ch\trd-bw\n");
		printf(
		    "-------------------------------------------------"
		    "-------------------------------------------------"
		    "-------------------------------------------------"
		    "--------------------------------------------------\n");
	} else {
		printf(
		    "status    name        id\t"
		    "wr-data\twr-ch\twr-bw\t"
		    "rd-data\trd-ch\trd-bw\n");
		printf(
		    "-----------------------------------------"
		    "--------------------------------------\n");
	}
}

static void
print_stats_human_readable(cmd_args_t *args, zpios_cmd_t *cmd)
{
	zpios_stats_t *summary_stats;
	double t_time, wr_time, rd_time, cr_time, rm_time;
	char str[KMGT_SIZE];

	if (args->rc)
		printf("FAIL: %3d ", args->rc);
	else
		printf("PASS:     ");

	printf("%-12s", args->name ? args->name : ZPIOS_NAME);
	printf("%2u\t", cmd->cmd_id);

	if (args->verbose) {
		printf("%u\t", cmd->cmd_thread_count);
		printf("%u\t", cmd->cmd_region_count);
		printf("%s\t", uint64_to_kmgt(str, cmd->cmd_region_size));
		printf("%s\t", uint64_to_kmgt(str, cmd->cmd_chunk_size));
		printf("%s\t", uint64_to_kmgt(str, cmd->cmd_offset));
		printf("%s\t", uint64_to_kmgt(str, cmd->cmd_region_noise));
		printf("%s\t", uint64_to_kmgt(str, cmd->cmd_chunk_noise));
		printf("%s\t", uint64_to_kmgt(str, cmd->cmd_thread_delay));
		printf("%s\t", print_flags(str, cmd->cmd_flags));
		printf("%s\t", uint64_to_kmgt(str, cmd->cmd_block_size));
	}

	if (args->rc) {
		printf("\n");
		return;
	}

	summary_stats = (zpios_stats_t *)cmd->cmd_data_str;
	t_time  = zpios_timespec_to_double(summary_stats->total_time.delta);
	wr_time = zpios_timespec_to_double(summary_stats->wr_time.delta);
	rd_time = zpios_timespec_to_double(summary_stats->rd_time.delta);
	cr_time = zpios_timespec_to_double(summary_stats->cr_time.delta);
	rm_time = zpios_timespec_to_double(summary_stats->rm_time.delta);

	if (args->verbose) {
		printf("%.2f\t", t_time);
		printf("%.3f\t", cr_time);
		printf("%.3f\t", rm_time);
		printf("%.2f\t", wr_time);
		printf("%.2f\t", rd_time);
	}

	printf("%s\t", uint64_to_kmgt(str, summary_stats->wr_data));
	printf("%s\t", uint64_to_kmgt(str, summary_stats->wr_chunks));
	printf("%s\t", kmgt_per_sec(str, summary_stats->wr_data, wr_time));

	printf("%s\t", uint64_to_kmgt(str, summary_stats->rd_data));
	printf("%s\t", uint64_to_kmgt(str, summary_stats->rd_chunks));
	printf("%s\n", kmgt_per_sec(str, summary_stats->rd_data, rd_time));
	fflush(stdout);
}

static void
print_stats_table(cmd_args_t *args, zpios_cmd_t *cmd)
{
	zpios_stats_t *summary_stats;
	double wr_time, rd_time;

	if (args->rc)
		printf("FAIL: %3d ", args->rc);
	else
		printf("PASS:     ");

	printf("%-12s", args->name ? args->name : ZPIOS_NAME);
	printf("%2u\t", cmd->cmd_id);

	if (args->verbose) {
		printf("%u\t", cmd->cmd_thread_count);
		printf("%u\t", cmd->cmd_region_count);
		printf("%llu\t", (long long unsigned)cmd->cmd_region_size);
		printf("%llu\t", (long long unsigned)cmd->cmd_chunk_size);
		printf("%llu\t", (long long unsigned)cmd->cmd_offset);
		printf("%u\t", cmd->cmd_region_noise);
		printf("%u\t", cmd->cmd_chunk_noise);
		printf("%u\t", cmd->cmd_thread_delay);
		printf("0x%x\t", cmd->cmd_flags);
		printf("%u\t", cmd->cmd_block_size);
	}

	if (args->rc) {
		printf("\n");
		return;
	}

	summary_stats = (zpios_stats_t *)cmd->cmd_data_str;
	wr_time = zpios_timespec_to_double(summary_stats->wr_time.delta);
	rd_time = zpios_timespec_to_double(summary_stats->rd_time.delta);

	if (args->verbose) {
		printf("%ld.%02ld\t",
		    (long)summary_stats->total_time.delta.ts_sec,
		    (long)summary_stats->total_time.delta.ts_nsec);
		printf("%ld.%02ld\t",
		    (long)summary_stats->cr_time.delta.ts_sec,
		    (long)summary_stats->cr_time.delta.ts_nsec);
		printf("%ld.%02ld\t",
		    (long)summary_stats->rm_time.delta.ts_sec,
		    (long)summary_stats->rm_time.delta.ts_nsec);
		printf("%ld.%02ld\t",
		    (long)summary_stats->wr_time.delta.ts_sec,
		    (long)summary_stats->wr_time.delta.ts_nsec);
		printf("%ld.%02ld\t",
		    (long)summary_stats->rd_time.delta.ts_sec,
		    (long)summary_stats->rd_time.delta.ts_nsec);
	}

	printf("%lld\t", (long long unsigned)summary_stats->wr_data);
	printf("%lld\t", (long long unsigned)summary_stats->wr_chunks);
	printf("%.4f\t", (double)summary_stats->wr_data / wr_time);

	printf("%lld\t", (long long unsigned)summary_stats->rd_data);
	printf("%lld\t", (long long unsigned)summary_stats->rd_chunks);
	printf("%.4f\n", (double)summary_stats->rd_data / rd_time);
	fflush(stdout);
}

void
print_stats(cmd_args_t *args, zpios_cmd_t *cmd)
{
	if (args->human_readable)
		print_stats_human_readable(args, cmd);
	else
		print_stats_table(args, cmd);
}
