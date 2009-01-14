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
 *
 * Kernel PIOS DMU implemenation originally derived from PIOS test code.
 * Character control interface derived from SPL code.
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
		return EINVAL;

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

	return rc;
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
		sprintf(str, "inf");
	else
		sprintf(str, "%lu%c", (unsigned long)val,
		        (i == -1) ? '\0' : postfix[i]);

	return str;
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
		sprintf(str, "inf");
	else
		sprintf(str, "%.2f%c", val,
		        (i == -1) ? '\0' : postfix[i]);

	return str;
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
	str[6] = '\0';

	return str;
}

static double
timespec_to_double(struct timespec t)
{
	return ((double)(t.tv_sec) +
	        ((double)(t.tv_nsec) / (double)(1000*1000*1000)));
}

static int
regex_match(const char *string, char *pattern)
{
	regex_t re;
	int rc;

	rc = regcomp(&re, pattern, REG_EXTENDED | REG_NOSUB | REG_ICASE);
	if (rc) {
		fprintf(stderr, "Error: Couldn't do regcomp, %d\n", rc);
		return rc;
	}

	rc = regexec(&re, string, (size_t) 0, NULL, 0);
	regfree(&re);

	return rc;
}

/* fills the pios_range_repeat structure of comma separated values */
static int
split_string(const char *optarg, char *pattern, range_repeat_t *range)
{
	const char comma[] = ",";
	char *cp, *token[32];
	int rc, i = 0;

	if ((rc = regex_match(optarg, pattern)))
		return rc;

	cp = strdup(optarg);
	if (cp == NULL)
		return ENOMEM;

	do {
		/* STRTOK(3) Each subsequent call, with a null pointer as the
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
	return 0;
}

int
set_count(char *pattern1, char *pattern2, range_repeat_t *range,
	  char *optarg, uint32_t *flags, char *arg)
{
	if (flags)
		*flags |= FLAG_SET;

	range->next_val = 0;

	if (regex_match(optarg, pattern1) == 0) {
		kmgt_to_uint64(optarg, &range->val[0]);
		range->val_count = 1;
	} else if (split_string(optarg, pattern2, range) < 0) {
		fprintf(stderr, "Error: Incorrect pattern for %s, '%s'\n",
		        arg, optarg);
		return EINVAL;
	}

	return 0;
}

/* validates the value with regular expression and sets low, high, incr
 * according to value at which flag will be set. Sets the flag after. */
int
set_lhi(char *pattern, range_repeat_t *range, char *optarg,
        int flag, uint32_t *flag_thread, char *arg)
{
	int rc;

	if ((rc = regex_match(optarg, pattern))) {
		fprintf(stderr, "Error: Wrong pattern in %s, '%s'\n",
			arg, optarg);
		return rc;
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

	return 0;
}

int
set_noise(uint64_t *noise, char *optarg, char *arg)
{
	if (regex_match(optarg, REGEX_NUMBERS) == 0) {
		kmgt_to_uint64(optarg, noise);
	} else {
		fprintf(stderr, "Error: Incorrect pattern for %s\n", arg);
		return EINVAL;
	}

	return 0;
}

int
set_load_params(cmd_args_t *args, char *optarg)
{
	char *param, *search, comma[] = ",";
	int rc = 0;

	search = strdup(optarg);
	if (search == NULL)
		return ENOMEM;

	while ((param = strtok(search, comma)) != NULL) {
		search = NULL;

		if (strcmp("fpp", param) == 0) {
			args->flags |= DMU_FPP; /* File Per Process/Thread */
		} else if (strcmp("sff", param) == 0) {
			args->flags &= ~DMU_FPP; /* Shared Shared File */
		} else if (strcmp("dmuio", param) == 0) {
			args->io_type |= DMU_IO;
			args->flags |= (DMU_WRITE | DMU_READ);
		} else {
			fprintf(stderr, "Invalid load: %s\n", param);
			rc = EINVAL;
		}
	}

	free(search);

	return rc;
}


/* checks the low, high, increment values against the single value for
 * mutual exclusion, for e.g threadcount is mutually exclusive to
 * threadcount_low, ..._high, ..._incr */
int
check_mutual_exclusive_command_lines(uint32_t flag, char *arg)
{
	if ((flag & FLAG_SET) && (flag & (FLAG_LOW | FLAG_HIGH | FLAG_INCR))) {
		fprintf(stderr, "Error: --%s can not be given with --%s_low, "
		        "--%s_high or --%s_incr.\n", arg, arg, arg, arg);
		return 0;
	}

	if ((flag & (FLAG_LOW | FLAG_HIGH | FLAG_INCR)) && !(flag & FLAG_SET)) {
		if (flag != (FLAG_LOW | FLAG_HIGH | FLAG_INCR)) {
			fprintf(stderr, "Error: One or more values missing "
			        "from --%s_low, --%s_high, --%s_incr.\n",
			        arg, arg, arg);
			return 0;
		}
	}

	return 1;
}

void
print_stats_header(void)
{
	printf("ret-code    id\tth-cnt\trg-cnt\trg-sz\tch-sz\toffset\trg-no\tch-no\t"
	       "th-dly\tflags\ttime\tcr-time\trm-time\twr-time\t"
	       "rd-time\twr-data\twr-ch\twr-bw\trd-data\trd-ch\trd-bw\n");
	printf("------------------------------------------------------------"
	       "------------------------------------------------------------"
	       "-----------------------------------------------------------\n");
}

static void
print_stats_human_readable(cmd_args_t *args, zpios_cmd_t *cmd)
{
	zpios_stats_t *summary_stats;
	double t_time, wr_time, rd_time, cr_time, rm_time;
	char str[16];

	if (args->rc)
		printf("FAILED: %3d ", args->rc);
	else
		printf("PASSED: %3d ", args->rc);

        printf("%u\t", cmd->cmd_id);
        printf("%u\t", cmd->cmd_thread_count);
        printf("%u\t", cmd->cmd_region_count);
        printf("%s\t", uint64_to_kmgt(str, cmd->cmd_region_size));
        printf("%s\t", uint64_to_kmgt(str, cmd->cmd_chunk_size));
        printf("%s\t", uint64_to_kmgt(str, cmd->cmd_offset));
        printf("%s\t", uint64_to_kmgt(str, cmd->cmd_region_noise));
        printf("%s\t", uint64_to_kmgt(str, cmd->cmd_chunk_noise));
        printf("%s\t", uint64_to_kmgt(str, cmd->cmd_thread_delay));
	printf("%s\t", print_flags(str, cmd->cmd_flags));

	if (args->rc) {
		printf("\n");
		return;
	}

	summary_stats = (zpios_stats_t *)cmd->cmd_data_str;
	t_time  = timespec_to_double(summary_stats->total_time.delta);
	wr_time = timespec_to_double(summary_stats->wr_time.delta);
	rd_time = timespec_to_double(summary_stats->rd_time.delta);
	cr_time = timespec_to_double(summary_stats->cr_time.delta);
	rm_time = timespec_to_double(summary_stats->rm_time.delta);

	printf("%.2f\t", t_time);
	printf("%.3f\t", cr_time);
	printf("%.3f\t", rm_time);
	printf("%.2f\t", wr_time);
	printf("%.2f\t", rd_time);

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
		printf("FAILED: %3d ", args->rc);
	else
		printf("PASSED: %3d ", args->rc);

        printf("%u\t", cmd->cmd_id);
        printf("%u\t", cmd->cmd_thread_count);
        printf("%u\t", cmd->cmd_region_count);
        printf("%llu\t", (long long unsigned)cmd->cmd_region_size);
        printf("%llu\t", (long long unsigned)cmd->cmd_chunk_size);
        printf("%llu\t", (long long unsigned)cmd->cmd_offset);
        printf("%u\t", cmd->cmd_region_noise);
        printf("%u\t", cmd->cmd_chunk_noise);
        printf("%u\t", cmd->cmd_thread_delay);
	printf("0x%x\t", cmd->cmd_flags);

	if (args->rc) {
		printf("\n");
		return;
	}

	summary_stats = (zpios_stats_t *)cmd->cmd_data_str;
	wr_time = timespec_to_double(summary_stats->wr_time.delta);
	rd_time = timespec_to_double(summary_stats->rd_time.delta);

	printf("%ld.%02ld\t",
	       summary_stats->total_time.delta.tv_sec,
	       summary_stats->total_time.delta.tv_nsec);
	printf("%ld.%02ld\t",
	       summary_stats->cr_time.delta.tv_sec,
	       summary_stats->cr_time.delta.tv_nsec);
	printf("%ld.%02ld\t",
	       summary_stats->rm_time.delta.tv_sec,
	       summary_stats->rm_time.delta.tv_nsec);
	printf("%ld.%02ld\t",
	       summary_stats->wr_time.delta.tv_sec,
	       summary_stats->wr_time.delta.tv_nsec);
	printf("%ld.%02ld\t",
	       summary_stats->rd_time.delta.tv_sec,
	       summary_stats->rd_time.delta.tv_nsec);

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
