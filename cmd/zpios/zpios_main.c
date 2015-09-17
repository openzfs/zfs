/*
 *  ZPIOS is a heavily modified version of the original PIOS test code.
 *  It is designed to have the test code running in the Linux kernel
 *  against ZFS while still being flexibly controlled from user space.
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
#include <getopt.h>
#include <assert.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include "zpios.h"

static const char short_opt[] =
	"t:l:h:e:n:i:j:k:o:m:q:r:c:a:b:g:s:A:B:C:"
	"S:L:p:M:xP:R:G:I:N:T:VzOfHv?";
static const struct option long_opt[] = {
	{"threadcount",		required_argument,	0,	't' },
	{"threadcount_low",	required_argument,	0,	'l' },
	{"threadcount_high",	required_argument,	0,	'h' },
	{"threadcount_incr",	required_argument,	0,	'e' },
	{"regioncount",		required_argument,	0,	'n' },
	{"regioncount_low",	required_argument,	0,	'i' },
	{"regioncount_high",	required_argument,	0,	'j' },
	{"regioncount_incr",	required_argument,	0,	'k' },
	{"offset",		required_argument,	0,	'o' },
	{"offset_low",		required_argument,	0,	'm' },
	{"offset_high",		required_argument,	0,	'q' },
	{"offset_incr",		required_argument,	0,	'r' },
	{"chunksize",		required_argument,	0,	'c' },
	{"chunksize_low",	required_argument,	0,	'a' },
	{"chunksize_high",	required_argument,	0,	'b' },
	{"chunksize_incr",	required_argument,	0,	'g' },
	{"regionsize",		required_argument,	0,	's' },
	{"regionsize_low",	required_argument,	0,	'A' },
	{"regionsize_high",	required_argument,	0,	'B' },
	{"regionsize_incr",	required_argument,	0,	'C' },
	{"blocksize",		required_argument,	0,	'S' },
	{"load",		required_argument,	0,	'L' },
	{"pool",		required_argument,	0,	'p' },
	{"name",		required_argument,	0,	'M' },
	{"cleanup",		no_argument,		0,	'x' },
	{"prerun",		required_argument,	0,	'P' },
	{"postrun",		required_argument,	0,	'R' },
	{"log",			required_argument,	0,	'G' },
	{"regionnoise",		required_argument,	0,	'I' },
	{"chunknoise",		required_argument,	0,	'N' },
	{"threaddelay",		required_argument,	0,	'T' },
	{"verify",		no_argument,		0,	'V' },
	{"zerocopy",		no_argument,		0,	'z' },
	{"nowait",		no_argument,		0,	'O' },
	{"noprefetch",		no_argument,		0,	'f' },
	{"human-readable",	no_argument,		0,	'H' },
	{"verbose",		no_argument,		0,	'v' },
	{"help",		no_argument,		0,	'?' },
	{ 0,			0,			0,	0 },
};

static int zpiosctl_fd;				/* Control file descriptor */
static char zpios_version[VERSION_SIZE];	/* Kernel version string */
static char *zpios_buffer = NULL;		/* Scratch space area */
static int zpios_buffer_size = 0;		/* Scratch space size */

static int
usage(void)
{
	fprintf(stderr, "Usage: zpios\n");
	fprintf(stderr,
		"	--threadcount       -t    =values\n"
		"	--threadcount_low   -l    =value\n"
		"	--threadcount_high  -h    =value\n"
		"	--threadcount_incr  -e    =value\n"
		"	--regioncount       -n    =values\n"
		"	--regioncount_low   -i    =value\n"
		"	--regioncount_high  -j    =value\n"
		"	--regioncount_incr  -k    =value\n"
		"	--offset            -o    =values\n"
		"	--offset_low        -m    =value\n"
		"	--offset_high       -q    =value\n"
		"	--offset_incr       -r    =value\n"
		"	--chunksize         -c    =values\n"
		"	--chunksize_low     -a    =value\n"
		"	--chunksize_high    -b    =value\n"
		"	--chunksize_incr    -g    =value\n"
		"	--regionsize        -s    =values\n"
		"	--regionsize_low    -A    =value\n"
		"	--regionsize_high   -B    =value\n"
		"	--regionsize_incr   -C    =value\n"
		"	--blocksize         -S    =values\n"
		"	--load              -L    =dmuio|ssf|fpp\n"
		"	--pool              -p    =pool name\n"
		"	--name              -M    =test name\n"
		"	--cleanup           -x\n"
		"	--prerun            -P    =pre-command\n"
		"	--postrun           -R    =post-command\n"
		"	--log               -G    =log directory\n"
		"	--regionnoise       -I    =shift\n"
		"	--chunknoise        -N    =bytes\n"
		"	--threaddelay       -T    =jiffies\n"
		"	--verify            -V\n"
		"	--zerocopy          -z\n"
		"	--nowait            -O\n"
		"	--noprefetch        -f\n"
		"	--human-readable    -H\n"
		"	--verbose           -v    =increase verbosity\n"
		"	--help              -?    =this help\n\n");

	return (0);
}

static void args_fini(cmd_args_t *args)
{
	assert(args != NULL);
	free(args);
}

/* block size is 128K to 16M, power of 2 */
#define	MIN_BLKSIZE	(128ULL << 10)
#define	MAX_BLKSIZE	(16ULL << 20)
#define	POW_OF_TWO(x)	(((x) & ((x) - 1)) == 0)

static cmd_args_t *
args_init(int argc, char **argv)
{
	cmd_args_t *args;
	uint32_t fl_th = 0;
	uint32_t fl_rc = 0;
	uint32_t fl_of = 0;
	uint32_t fl_rs = 0;
	uint32_t fl_cs = 0;
	uint32_t fl_bs = 0;
	int c, rc, i;

	if (argc == 1) {
		usage();
		return ((cmd_args_t *)NULL);
	}

	/* Configure and populate the args structures */
	args = malloc(sizeof (*args));
	if (args == NULL)
		return (NULL);

	memset(args, 0, sizeof (*args));

	/* provide a default block size of 128K */
	args->B.next_val = 0;
	args->B.val[0] = MIN_BLKSIZE;
	args->B.val_count = 1;

	while ((c = getopt_long(argc, argv, short_opt, long_opt, NULL)) != -1) {
		rc = 0;

		switch (c) {
		case 't': /* --thread count */
			rc = set_count(REGEX_NUMBERS, REGEX_NUMBERS_COMMA,
			    &args->T, optarg, &fl_th, "threadcount");
			break;
		case 'l': /* --threadcount_low */
			rc = set_lhi(REGEX_NUMBERS, &args->T, optarg,
			    FLAG_LOW, &fl_th, "threadcount_low");
			break;
		case 'h': /* --threadcount_high */
			rc = set_lhi(REGEX_NUMBERS, &args->T, optarg,
			    FLAG_HIGH, &fl_th, "threadcount_high");
			break;
		case 'e': /* --threadcount_inc */
			rc = set_lhi(REGEX_NUMBERS, &args->T, optarg,
			    FLAG_INCR, &fl_th, "threadcount_incr");
			break;
		case 'n': /* --regioncount */
			rc = set_count(REGEX_NUMBERS, REGEX_NUMBERS_COMMA,
			    &args->N, optarg, &fl_rc, "regioncount");
			break;
		case 'i': /* --regioncount_low */
			rc = set_lhi(REGEX_NUMBERS, &args->N, optarg,
			    FLAG_LOW, &fl_rc, "regioncount_low");
			break;
		case 'j': /* --regioncount_high */
			rc = set_lhi(REGEX_NUMBERS, &args->N, optarg,
			    FLAG_HIGH, &fl_rc, "regioncount_high");
			break;
		case 'k': /* --regioncount_inc */
			rc = set_lhi(REGEX_NUMBERS, &args->N, optarg,
			    FLAG_INCR, &fl_rc, "regioncount_incr");
			break;
		case 'o': /* --offset */
			rc = set_count(REGEX_SIZE, REGEX_SIZE_COMMA,
			    &args->O, optarg, &fl_of, "offset");
			break;
		case 'm': /* --offset_low */
			rc = set_lhi(REGEX_SIZE, &args->O, optarg,
			    FLAG_LOW, &fl_of, "offset_low");
			break;
		case 'q': /* --offset_high */
			rc = set_lhi(REGEX_SIZE, &args->O, optarg,
			    FLAG_HIGH, &fl_of, "offset_high");
			break;
		case 'r': /* --offset_inc */
			rc = set_lhi(REGEX_NUMBERS, &args->O, optarg,
			    FLAG_INCR, &fl_of, "offset_incr");
			break;
		case 'c': /* --chunksize */
			rc = set_count(REGEX_SIZE, REGEX_SIZE_COMMA,
			    &args->C, optarg, &fl_cs, "chunksize");
			break;
		case 'a': /* --chunksize_low */
			rc = set_lhi(REGEX_SIZE, &args->C, optarg,
			    FLAG_LOW, &fl_cs, "chunksize_low");
			break;
		case 'b': /* --chunksize_high */
			rc = set_lhi(REGEX_SIZE, &args->C, optarg,
			    FLAG_HIGH, &fl_cs, "chunksize_high");
			break;
		case 'g': /* --chunksize_inc */
			rc = set_lhi(REGEX_NUMBERS, &args->C, optarg,
			    FLAG_INCR, &fl_cs, "chunksize_incr");
			break;
		case 's': /* --regionsize */
			rc = set_count(REGEX_SIZE, REGEX_SIZE_COMMA,
			    &args->S, optarg, &fl_rs, "regionsize");
			break;
		case 'A': /* --regionsize_low */
			rc = set_lhi(REGEX_SIZE, &args->S, optarg,
			    FLAG_LOW, &fl_rs, "regionsize_low");
			break;
		case 'B': /* --regionsize_high */
			rc = set_lhi(REGEX_SIZE, &args->S, optarg,
			    FLAG_HIGH, &fl_rs, "regionsize_high");
			break;
		case 'C': /* --regionsize_inc */
			rc = set_lhi(REGEX_NUMBERS, &args->S, optarg,
			    FLAG_INCR, &fl_rs, "regionsize_incr");
			break;
		case 'S': /* --blocksize */
			rc = set_count(REGEX_SIZE, REGEX_SIZE_COMMA,
			    &args->B, optarg, &fl_bs, "blocksize");
			break;
		case 'L': /* --load */
			rc = set_load_params(args, optarg);
			break;
		case 'p': /* --pool */
			args->pool = optarg;
			break;
		case 'M':
			args->name = optarg;
			break;
		case 'x': /* --cleanup */
			args->flags |= DMU_REMOVE;
			break;
		case 'P': /* --prerun */
			strncpy(args->pre, optarg, ZPIOS_PATH_SIZE - 1);
			break;
		case 'R': /* --postrun */
			strncpy(args->post, optarg, ZPIOS_PATH_SIZE - 1);
			break;
		case 'G': /* --log */
			strncpy(args->log, optarg, ZPIOS_PATH_SIZE - 1);
			break;
		case 'I': /* --regionnoise */
			rc = set_noise(&args->regionnoise, optarg,
			    "regionnoise");
			break;
		case 'N': /* --chunknoise */
			rc = set_noise(&args->chunknoise, optarg, "chunknoise");
			break;
		case 'T': /* --threaddelay */
			rc = set_noise(&args->thread_delay, optarg,
			    "threaddelay");
			break;
		case 'V': /* --verify */
			args->flags |= DMU_VERIFY;
			break;
		case 'z': /* --zerocopy */
			args->flags |= (DMU_WRITE_ZC | DMU_READ_ZC);
			break;
		case 'O': /* --nowait */
			args->flags |= DMU_WRITE_NOWAIT;
			break;
		case 'f': /* --noprefetch */
			args->flags |= DMU_READ_NOPF;
			break;
		case 'H': /* --human-readable */
			args->human_readable = 1;
			break;
		case 'v': /* --verbose */
			args->verbose++;
			break;
		case '?':
			rc = 1;
			break;
		default:
			fprintf(stderr, "Unknown option '%s'\n",
			    argv[optind - 1]);
			rc = EINVAL;
			break;
		}

		if (rc) {
			usage();
			args_fini(args);
			return (NULL);
		}
	}

	check_mutual_exclusive_command_lines(fl_th, "threadcount");
	check_mutual_exclusive_command_lines(fl_rc, "regioncount");
	check_mutual_exclusive_command_lines(fl_of, "offset");
	check_mutual_exclusive_command_lines(fl_rs, "regionsize");
	check_mutual_exclusive_command_lines(fl_cs, "chunksize");

	if (args->pool == NULL) {
		fprintf(stderr, "Error: Pool not specificed\n");
		usage();
		args_fini(args);
		return (NULL);
	}

	if ((args->flags & (DMU_WRITE_ZC | DMU_READ_ZC)) &&
	    (args->flags & DMU_VERIFY)) {
		fprintf(stderr, "Error, --zerocopy incompatible --verify, "
		    "used for performance analysis only\n");
		usage();
		args_fini(args);
		return (NULL);
	}

	/* validate block size(s) */
	for (i = 0; i < args->B.val_count; i++) {
		int bs = args->B.val[i];

		if (bs < MIN_BLKSIZE || bs > MAX_BLKSIZE || !POW_OF_TWO(bs)) {
			fprintf(stderr, "Error: invalid block size %d\n", bs);
			args_fini(args);
			return (NULL);
		}
	}

	return (args);
}

static int
dev_clear(void)
{
	zpios_cfg_t cfg;
	int rc;

	memset(&cfg, 0, sizeof (cfg));
	cfg.cfg_magic = ZPIOS_CFG_MAGIC;
	cfg.cfg_cmd   = ZPIOS_CFG_BUFFER_CLEAR;
	cfg.cfg_arg1  = 0;

	rc = ioctl(zpiosctl_fd, ZPIOS_CFG, &cfg);
	if (rc)
		fprintf(stderr, "Ioctl() error %lu / %d: %d\n",
		    (unsigned long) ZPIOS_CFG, cfg.cfg_cmd, errno);

	lseek(zpiosctl_fd, 0, SEEK_SET);

	return (rc);
}

/* Passing a size of zero simply results in querying the current size */
static int
dev_size(int size)
{
	zpios_cfg_t cfg;
	int rc;

	memset(&cfg, 0, sizeof (cfg));
	cfg.cfg_magic = ZPIOS_CFG_MAGIC;
	cfg.cfg_cmd   = ZPIOS_CFG_BUFFER_SIZE;
	cfg.cfg_arg1  = size;

	rc = ioctl(zpiosctl_fd, ZPIOS_CFG, &cfg);
	if (rc) {
		fprintf(stderr, "Ioctl() error %lu / %d: %d\n",
		    (unsigned long) ZPIOS_CFG, cfg.cfg_cmd, errno);
		return (rc);
	}

	return (cfg.cfg_rc1);
}

static void
dev_fini(void)
{
	if (zpios_buffer)
		free(zpios_buffer);

	if (zpiosctl_fd != -1) {
		if (close(zpiosctl_fd) == -1) {
			fprintf(stderr, "Unable to close %s: %d\n",
			    ZPIOS_DEV, errno);
		}
	}
}

static int
dev_init(void)
{
	int rc;

	zpiosctl_fd = open(ZPIOS_DEV, O_RDONLY);
	if (zpiosctl_fd == -1) {
		fprintf(stderr, "Unable to open %s: %d\n"
		    "Is the zpios module loaded?\n", ZPIOS_DEV, errno);
		rc = errno;
		goto error;
	}

	if ((rc = dev_clear()))
		goto error;

	if ((rc = dev_size(0)) < 0)
		goto error;

	zpios_buffer_size = rc;
	zpios_buffer = (char *)malloc(zpios_buffer_size);
	if (zpios_buffer == NULL) {
		rc = ENOMEM;
		goto error;
	}

	memset(zpios_buffer, 0, zpios_buffer_size);
	return (0);
error:
	if (zpiosctl_fd != -1) {
		if (close(zpiosctl_fd) == -1) {
			fprintf(stderr, "Unable to close %s: %d\n",
			    ZPIOS_DEV, errno);
		}
	}

	return (rc);
}

static int
get_next(uint64_t *val, range_repeat_t *range)
{
	/* if low, incr, high is given */
	if (range->val_count == 0) {
		*val = (range->val_low) +
		    (range->val_low * range->next_val / 100);

		if (*val > range->val_high)
			return (0); /* No more values, limit exceeded */

		if (!range->next_val)
			range->next_val = range->val_inc_perc;
		else
			range->next_val = range->next_val + range->val_inc_perc;

		return (1); /* more values to come */

	/* if only one val is given */
	} else if (range->val_count == 1) {
		if (range->next_val)
			return (0); /* No more values, we only have one */

		*val = range->val[0];
		range->next_val = 1;
		return (1); /* more values to come */

	/* if comma separated values are given */
	} else if (range->val_count > 1) {
		if (range->next_val > range->val_count - 1)
			return (0); /* No more values, limit exceeded */

		*val = range->val[range->next_val];
		range->next_val++;
		return (1); /* more values to come */
	}

	return (0);
}

static int
run_one(cmd_args_t *args, uint32_t id, uint32_t T, uint32_t N,
    uint64_t C, uint64_t S, uint64_t O, uint64_t B)
{
	zpios_cmd_t *cmd;
	int rc, rc2, cmd_size;

	dev_clear();

	cmd_size =
		sizeof (zpios_cmd_t)
		+ ((T + N + 1) * sizeof (zpios_stats_t));
	cmd = (zpios_cmd_t *)malloc(cmd_size);
	if (cmd == NULL)
		return (ENOMEM);

	memset(cmd, 0, cmd_size);
	cmd->cmd_magic = ZPIOS_CMD_MAGIC;
	strncpy(cmd->cmd_pool, args->pool, ZPIOS_NAME_SIZE - 1);
	strncpy(cmd->cmd_pre, args->pre, ZPIOS_PATH_SIZE - 1);
	strncpy(cmd->cmd_post, args->post, ZPIOS_PATH_SIZE - 1);
	strncpy(cmd->cmd_log, args->log, ZPIOS_PATH_SIZE - 1);
	cmd->cmd_id = id;
	cmd->cmd_chunk_size = C;
	cmd->cmd_thread_count = T;
	cmd->cmd_region_count = N;
	cmd->cmd_region_size = S;
	cmd->cmd_offset = O;
	cmd->cmd_block_size = B;
	cmd->cmd_region_noise = args->regionnoise;
	cmd->cmd_chunk_noise = args->chunknoise;
	cmd->cmd_thread_delay = args->thread_delay;
	cmd->cmd_flags = args->flags;
	cmd->cmd_data_size = (T + N + 1) * sizeof (zpios_stats_t);

	rc = ioctl(zpiosctl_fd, ZPIOS_CMD, cmd);
	if (rc)
		args->rc = errno;

	print_stats(args, cmd);

	if (args->verbose) {
		rc2 = read(zpiosctl_fd, zpios_buffer, zpios_buffer_size - 1);
		if (rc2 < 0) {
			fprintf(stdout, "Error reading results: %d\n", rc2);
		} else if ((rc2 > 0) && (strlen(zpios_buffer) > 0)) {
			fprintf(stdout, "\n%s\n", zpios_buffer);
			fflush(stdout);
		}
	}

	free(cmd);

	return (rc);
}

static int
run_offsets(cmd_args_t *args)
{
	int rc = 0;

	while (rc == 0 && get_next(&args->current_O, &args->O)) {
		rc = run_one(args, args->current_id,
		    args->current_T, args->current_N, args->current_C,
		    args->current_S, args->current_O, args->current_B);
		args->current_id++;
	}

	args->O.next_val = 0;
	return (rc);
}

static int
run_region_counts(cmd_args_t *args)
{
	int rc = 0;

	while (rc == 0 && get_next((uint64_t *)&args->current_N, &args->N))
		rc = run_offsets(args);

	args->N.next_val = 0;
	return (rc);
}

static int
run_region_sizes(cmd_args_t *args)
{
	int rc = 0;

	while (rc == 0 && get_next(&args->current_S, &args->S)) {
		if (args->current_S < args->current_C) {
			fprintf(stderr, "Error: in any run chunksize must "
			    "be strictly smaller than regionsize.\n");
			return (EINVAL);
		}

		rc = run_region_counts(args);
	}

	args->S.next_val = 0;
	return (rc);
}

static int
run_chunk_sizes(cmd_args_t *args)
{
	int rc = 0;

	while (rc == 0 && get_next(&args->current_C, &args->C)) {
		rc = run_region_sizes(args);
	}

	args->C.next_val = 0;
	return (rc);
}

static int
run_block_sizes(cmd_args_t *args)
{
	int rc = 0;

	while (rc == 0 && get_next(&args->current_B, &args->B)) {
		rc = run_chunk_sizes(args);
	}

	args->B.next_val = 0;
	return (rc);
}


static int
run_thread_counts(cmd_args_t *args)
{
	int rc = 0;

	while (rc == 0 && get_next((uint64_t *)&args->current_T, &args->T))
		rc = run_block_sizes(args);

	return (rc);
}

int
main(int argc, char **argv)
{
	cmd_args_t *args;
	int rc = 0;

	/* Argument init and parsing */
	if ((args = args_init(argc, argv)) == NULL) {
		rc = -1;
		goto out;
	}

	/* Device specific init */
	if ((rc = dev_init()))
		goto out;

	/* Generic kernel version string */
	if (args->verbose)
		fprintf(stdout, "%s", zpios_version);

	print_stats_header(args);
	rc = run_thread_counts(args);
out:
	if (args != NULL)
		args_fini(args);

	dev_fini();
	return (rc);
}
