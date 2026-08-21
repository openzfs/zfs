// SPDX-License-Identifier: CDDL-1.0
/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or https://opensource.org/licenses/CDDL-1.0.
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

/*
 * Copyright (c) 2026, Klara, Inc.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdint.h>
#include <inttypes.h>
#include <stdbool.h>
#include <string.h>
#include <getopt.h>
#include <errno.h>

/*
 * SI Conversions
 *
 * These translate values to kibibytes, mebibytes, and so forth; the
 * power-of-two version of the SI units.  KiB(1) will yield 1024, for
 * example, not 1000.
 */
#define	KiB(_x) ((_x) << 10)
#define	MiB(_x) ((_x) << 20)
#define	GiB(_x) ((_x) << 30)
#define	TiB(_x) ((_x) << 40)
#define	PiB(_x) ((_x) << 50)

/*
 * Option Data
 *
 * Command line options and global defaults are stored here.
 */
enum {
	WRITE_APPEND,
	WRITE_RANDOM,
	WRITE_CYCLIC
};

#define	MAX_VERBOSITY (10)

struct {
	const char	*path;
	size_t	 file_size;
	uint64_t write_count;
	size_t	 write_size;
	int	 write_type;
	uint64_t cycle_size;
	uint64_t progress_step;
	uint64_t sparse_fsync;
	bool	 close_on_write;
	bool	 do_fsync;
	bool	 use_o_sync;
	bool	 delete_file;
	int	 verbosity;
} options =
{
	.path		= "fsync-test.data",
	.file_size	= MiB(33),
	.write_count	= KiB(1),
	.write_size	= 1,
	.write_type	= WRITE_APPEND,
	.cycle_size	= 0,
	.progress_step	= 64,
	.sparse_fsync	= 1,
	.close_on_write = false,
	.do_fsync	= true,
	.use_o_sync	= false,
	.delete_file	= true,
	.verbosity	= 1,
};

/*
 * Print Usage & Exit
 *
 * This function is invoked when a command line error is encountered, or if the
 * user explicitly asks for help.
 *
 * Arguments:
 *
 * ret -- the value to hand to exit()
 * fmt -- the printf()-style format string to be printed before the usage text,
 *	  or NULL
 * ... -- arguments for the format string
 */

static void
usage(int ret, const char *fmt, ...)
{
	if (fmt) {
		va_list arglist;
		va_start(arglist, fmt);
		vprintf(fmt, arglist);
		va_end(arglist);
	}

	printf("Usage: many-fsync-test [arguments]\n"
	    "    Test writing a series of values to a file with fsync() between"
	    " each\n write.\n"
	    "\n"
	    "  Arguments:\n");
	printf(
	    "    -h                 -- print this help text and quit.\n"
	    "    -q                 -- reduce verbosity one step\n");
	printf(
	    "    -v                 -- increase verbosity one step\n"
	    "                          default verbosity is %d\n",
	    options.verbosity);
	printf(
	    "    --path string      -- specify the path to the test file\n"
	    "                          default is \"%s\"\n", options.path);
	printf(
	    "    --size int         -- specify the byte size of the file\n"
	    "                          can use k, m, g or t as suffixes\n"
	    "                          default is %lu bytes\n",
	    options.file_size);
	printf(
	    "    --no-delete        -- do not remove the test file once the"
	    " test completes\n"
	    "                          default is %s delete the file\n",
	    options.do_fsync ? "to" : "to not");
	printf(
	    "    --no-fsync         -- do not fsync() between writes\n"
	    "                          default is %sfsync() between writes\n",
	    options.do_fsync ? "" : "no ");
	printf(
	    "    --sparse-fsync int -- fsync() only every [arg] writes\n"
	    "                          default is fsync() every write\n");
	printf(
	    "    --o-sync           -- use O_SYNC instead of fsync()\n"
	    "                          default is O_SYNC %s\n",
	    options.use_o_sync ? "on" : "off");
	printf(
	    "    --close-on-write   -- open, write, close the file for each"
	    " write\n"
	    "                          default is %s close between writes\n",
	    options.close_on_write ? "do" : "do not");
	printf(
	    "    --write-count int  -- specify the number of writes to append\n"
	    "                          can use k, m, g or t as suffixes\n"
	    "                          default is %lu bytes\n",
	    options.write_count);
	printf(
	    "    --write-size int   -- specify the size of the test writes\n"
	    "                          default is %lu bytes\n",
	    options.write_size);
	printf(
	    "    --cycle-modify int -- specify the offsets at which to modify"
	    " the test\n"
	    "                          file; writes occur starting at multiples"
	    " of this\n"
	    "                          offset, so (for example) with a test"
	    " file size\n"
	    "                          of 33M, a write size of 1 and a"
	    " cycle-modify of 16M,\n"
	    "                          the first write will be at 33M+1, the"
	    " second at 0,\n"
	    "                          the third at 16M, then 32M, 33M+2, 1,"
	    " 16M+1, 32M+1...\n"
	    "                          can use k, m, g or t as suffixes\n"
	    "                          default is %s cycle\n",
	    (options.write_type == WRITE_CYCLIC) ? "do" : "do not");
	printf(
	    "    --random-write     -- randomize write offsets; each write will"
	    " seek to a\n"
	    "                          random offset before writing\n"
	    "                          default is %s randomize writes\n",
	    (options.write_type == WRITE_APPEND) ? "do" : "do not");

	exit(ret);
}

/*
 * Parse a Number with a Possible SI Suffix
 *
 * This is intended for command line argument parsing; it converts strings to
 * unsigned integer values.  If it encounters one of `kmgp` as a suffix, it
 * multiplies the value by the appropriate power of two.
 *
 * Arguments:
 *
 * str -- the string to parse
 *
 * Returns:
 *
 * A uint64_t value.  In the event of a parse failure, this function will print
 * usage and exit the program with an error.
 */
static uint64_t
parse_number(const char *str)
{
	char *end;
	uint64_t val = strtoul(str, &end, 10);

	/*
	 * If we've got invalid characters as far as strtol() is concerned, it
	 * may be a size suffix.
	 */
	if (end[0]) {
		if (end[1]) usage(1, "Couldn't parse integer \"%s\"\n\n", str);

		switch (end[0]) {
		case 'K':
		case 'k':
			val = KiB(val);
			break;

		case 'M':
		case 'm':
			val = MiB(val);
			break;

		case 'G':
		case 'g':
			val = GiB(val);
			break;

		case 'P':
		case 'p':
			val = PiB(val);
			break;

		default:
			usage(1, "Couldn't parse integer \"%s\"\n\n", str);
			break; // Unreached.
		}
	}

	return (val);
}

/*
 * Parse Command Line Arguments
 *
 * This function fills out the Option structure, overriding defaults in
 * response to command line directives.	 If the command line does not parse
 * cleanly, this function displays usage and exits with an error.
 */
enum {
	ARG_NONE,
	ARG_HELP,
	ARG_PATH,
	ARG_SIZE,
	ARG_NO_FSYNC,
	ARG_O_SYNC,
	ARG_SPARSE_FSYNC,
	ARG_NO_DELETE,
	ARG_CLOSE_ON_WRITE,
	ARG_WRITE_COUNT,
	ARG_WRITE_SIZE,
	ARG_CYCLE_MODIFY,
	ARG_RANDOM_WRITE,
	ARG_MAX,
};

#define	OPT_FLAG(_str, _sym) { _str, no_argument, NULL, _sym }
#define	OPT_1ARG(_str, _sym) { _str, required_argument, NULL, _sym }

struct option long_opts[] =
{
	OPT_FLAG("help",		ARG_HELP),
	OPT_1ARG("path",		ARG_PATH),
	OPT_1ARG("size",		ARG_SIZE),
	OPT_FLAG("no-fsync",		ARG_NO_FSYNC),
	OPT_FLAG("o-sync",		ARG_O_SYNC),
	OPT_1ARG("sparse-fsync",	ARG_SPARSE_FSYNC),
	OPT_FLAG("no-delete",		ARG_NO_DELETE),
	OPT_FLAG("close-on-write",	ARG_CLOSE_ON_WRITE),
	OPT_1ARG("write-count",		ARG_WRITE_COUNT),
	OPT_1ARG("write-size",		ARG_WRITE_SIZE),
	OPT_1ARG("cycle-modify",	ARG_CYCLE_MODIFY),
	OPT_FLAG("random-write",	ARG_RANDOM_WRITE),
	{0, 0, 0, 0}
};

static void
parse_args(int argc, char **argv)
{
	int lindex = 0;
	bool done = false;

	while (!done) {
		int o = getopt_long(argc, argv, "hqv", long_opts, &lindex);

		switch (o) {
		case -1:
			done = true;
			break;

		case '?':
			usage(1, "");
			break; /* Unreached. */

		case 'q':
			if (options.verbosity > 0) {
				options.verbosity--;
			}
			break;

		case 'v':
			if (options.verbosity < MAX_VERBOSITY) {
				options.verbosity++;
			}
			break;

		case 'h':
		case ARG_HELP:
			usage(0, "");
			break; /* Unreached. */

		case ARG_PATH:
			options.path = strdup(optarg);
			break;

		case ARG_SIZE:
			options.file_size = parse_number(optarg);
			break;

		case ARG_NO_FSYNC:
			options.do_fsync = false;
			break;

		case ARG_O_SYNC:
			options.do_fsync = false;
			options.use_o_sync = true;
			break;

		case ARG_SPARSE_FSYNC:
			options.sparse_fsync = parse_number(optarg);

			if (!options.sparse_fsync) {
				options.do_fsync = false;
			}
			break;

		case ARG_NO_DELETE:
			options.delete_file = false;
			break;

		case ARG_CLOSE_ON_WRITE:
			options.close_on_write = true;
			break;

		case ARG_WRITE_COUNT:
			options.write_count = parse_number(optarg);
			break;

		case ARG_WRITE_SIZE:
			options.write_size = parse_number(optarg);
			break;

		case ARG_CYCLE_MODIFY:
			options.cycle_size = parse_number(optarg);
			options.write_type = WRITE_CYCLIC;
			break;

		case ARG_RANDOM_WRITE:
			options.write_type = WRITE_RANDOM;
			break;

		default:
			printf("Unknown argument.\n");
			usage(1, "");
			break; /* Unreached. */
		}
	}

	if (options.verbosity > 2) {
		printf("%s\n", argv[0]);
		printf("  Path:		    \"%s\"\n", options.path);
		printf("  File Size:	    %"PRIu64"\n",
		    (uint64_t)options.file_size);
		printf("  Write Count:	    %"PRIu64"\n",
		    options.write_count);
		printf("  Write Size:	    %"PRIu64"b\n",
		    (uint64_t)options.write_size);
		printf("  Delete File	    %s\n",
		    options.delete_file ? "Yes" : "No");
		printf("  Close on Write:   %s\n",
		    options.close_on_write ? "Yes" : "No");
		printf("  fsync() on Write: %s\n",
		    options.do_fsync ? "Yes" : "No");
		printf("  Use O_SYNC:	    %s\n",
		    options.use_o_sync ? "Yes" : "No");
		printf("  Write Type:	    ");

		switch (options.write_type) {
		case WRITE_APPEND:
			printf("Append\n");
			break;

		case WRITE_CYCLIC:
			printf("Cyclic\n");
			printf("    Size:	    %"PRIu64"b\n",
			    options.cycle_size);
			break;

		case WRITE_RANDOM:
			printf("Random\n");
			break;

		default:
			printf("UNKNOWN?\n");
			break;
		}

		printf("  Verbosity:	    %u\n", options.verbosity);
	}
}

/*
 * Create the Test File
 *
 * This function creates the file which will be used to test incremental writes
 * with fsync().
 *
 * Arguments:
 *
 * path -- the full path of the file, including name
 * size -- the byte size of the file
 *
 * Returns:
 *
 * Success (`true`) or failure (`false`).  On failure, the reason for the
 * failure is printed to stderr.
 */
static bool
make_test_file(const char *path, size_t size)
{
	FILE *f = fopen(path, "w");
	size_t chunk = KiB(128);
	size_t i;

	if (chunk > size)
		chunk = size;

	if (!f) {
		fprintf(stderr,
		    "\nCouldn't fopen(\"%s\", \"w\"),"
		    " the OS said:\n\t\"%s\"\n\n", path, strerror(errno));
		return (false);
	}

	uint8_t *fill = malloc(chunk);

	if (!fill) {
		fprintf(stderr,
		    "\nCouldn't malloc(%lu), the OS said:\n\t\"%s\"\n\n",
		    chunk, strerror(errno));
		return (false);
	}

	for (i = 0; i < chunk; i++) {
		fill[i] = (i & 0xFFull);
	}

	for (i = 0; i < size; i += chunk) {
		size_t write_size = chunk;
		if ((i + chunk) > size) write_size = size - i;

		fwrite(fill, write_size, 1, f);
	}

	free(fill);
	fclose(f);
	return (true);
}

/*
 * Open File With Error Reporting
 */
static int
open_file(const char *path)
{
	int flags = (O_WRONLY |
	    (options.use_o_sync ? O_SYNC : 0) |
	    (options.write_type == WRITE_APPEND ? O_APPEND : 0));

	int f = open(path, flags);

	if (f < 0) {
		fprintf(stderr, "\nUnable to open %s for writing.\n\n",
		    options.path);
	}

	return (f);
}

/*
 * Run Test
 */
static bool
run_fsync_test(void)
{
	uint64_t progress = 0;
	uint64_t cycle	  = 0;
	uint64_t offset;
	uint8_t *data = calloc(1, options.write_size);
	int	 file = -1;

	if (!data) {
		fprintf(stderr, "\nUnable to allocate %lu bytes.\n\n",
		    options.write_size);
		return (false);
	}

	data[0] = 0xFF; /* Flag the beginning. */

	if (!options.close_on_write) {
		if (0 > (file = open_file(options.path))) {
			return (false);
		}
	}

	for (uint64_t i = 0; i < options.write_count; i++) {
		/* Open if necessary... */

		if (options.close_on_write) {
			if (0 > (file = open_file(options.path))) {
				return (false);
			}
		}

		/* Position the file pointer. */

		switch (options.write_type) {
		case WRITE_APPEND:
			/* File opened with O_APPEND, nothing to do. */
			break;

		case WRITE_CYCLIC:
			offset = cycle;
			cycle = (offset + options.cycle_size +
			    options.write_size) % options.file_size;
			if (options.verbosity > 4) {
				printf("Offset: %"PRIu64"\n", offset);
			}
			lseek(file, offset, SEEK_SET);
			break;

		case WRITE_RANDOM:
			offset = (uint64_t)(drand48() *
			    (double)options.file_size);
			if (options.verbosity > 4) {
				printf("Offset: %"PRIu64"\n", offset);
			}
			lseek(file, offset, SEEK_SET);
			break;
		}

		/* Write options.size bytes. */

		ssize_t res = write(file, data, options.write_size);

		if (res != options.write_size && options.verbosity > 0) {
			fprintf(stderr,
			    "Incomplete write() returned %d, \"%s\"\n",
			    errno, strerror(errno));
		}

		/* fsync() if necessary. */

		if (options.do_fsync && !(i % options.sparse_fsync)) {
			int err = fsync(file);

			if (err && options.verbosity > 0) {
				fprintf(stderr,
				    "fsync(\"%s\") returned %d, \"%s\"\n",
				    options.path, err, strerror(err));
			}
		}

		/* close() if necessary. */

		if (options.close_on_write) {
			close(file);
		}

		/* Progress. */

		if (options.verbosity > 2 && i >= progress) {
			progress += options.progress_step;
			printf("Done: %3"PRIu64"%% (%"PRIu64"/%"PRIu64")\n",
			    (i * 100) / options.write_count,
			    i, options.write_count);
		}
	}

	if (options.verbosity > 2) {
		printf("Done: 100%% (%"PRIu64"/%"PRIu64")\n",
		    options.write_count, options.write_count);
	}

	if (!options.close_on_write) {
		close(file);
	}

	free(data);

	return (true);
}

/*
 * Entry Point
 *
 * Execution starts here.
 *
 * Arguments:
 *
 * argc -- the size of the argument vector
 * argv -- an array of argument strings
 *
 * Returns:
 *
 * Success (0) or failure (nonzero).
 */
int
main(int argc, char **argv)
{
	int result = 0;

	parse_args(argc, argv);

	if (!make_test_file(options.path, options.file_size)) {
		return (1);
	}

	if (!run_fsync_test()) {
		result = 1;
	}

	if (options.delete_file) {
		unlink(options.path);
	}

	return (result);
}
