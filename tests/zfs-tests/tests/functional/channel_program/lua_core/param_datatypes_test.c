/*
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this
 * source.  A copy of the CDDL is also available via the Internet at
 * http://www.illumos.org/license/CDDL.
 */

/*
 * Copyright (c) 2022 by Chris Lindee. All rights reserved.
 */

#include <stdint.h>
#include <libzfs.h>

#include <fcntl.h>
#include <sys/stat.h>
#include <stdio.h>
#include <unistd.h>

const int INSTRUCTION_LIMIT = 10 * 1000 * 1000;	/* 10 million */
const int MEMORY_LIMIT = 10 * (1 << 20);		/* 10 MiB */
const int EUSER = 253;

int run_test(const char * const, const char * const);

int
main(int argc, char **argv)
{
	if (argc != 3) {
		fprintf(stderr,
		    "Test requires two arguments: <testpool> <test.lua>\n");
		return (EUSER);
	}

	int fd = open(argv[2], O_RDONLY);
	if (fd == -1) {
		perror("Unable to open Lua file");
		return (EUSER);
	}

	struct stat filestat;
	if (fstat(fd, &filestat)) {
		perror("Unable to stat filehandle");
		return (EUSER);
	}

	char * const script = (char *)
	    malloc(sizeof (char) * (filestat.st_size + 1));
	char *ptr = script;
	off_t to_read = filestat.st_size;
	while (to_read) {
		ssize_t ret = read(fd, ptr, to_read);
		if (ret == 0)
			break;
		if (ret == -1) {
			perror("Unable to read from file");
			return (EUSER);
		}

		ptr += ret;
		to_read -= ret;
	}
	*ptr = '\0';

	libzfs_core_init();
	int rc = run_test(argv[1], script);
	libzfs_core_fini();

	free(script);
	return (rc);
}

int
run_test(const char * const pool, const char * const script)
{
	nvlist_t * const args = fnvlist_alloc();
	fnvlist_add_boolean_value(args, "bTrue", B_TRUE);
	fnvlist_add_boolean_value(args, "bFalse", B_FALSE);
	/*
	 * While Lua strings are not NUL terminated (i.e. they have an explicit
	 * length), the nvpair library appears to only support NUL terminated
	 * strings.
	 */
	fnvlist_add_string(args, "string", "string\0<hidden>");
	fnvlist_add_uint64(args, "uint64", 64);
	fnvlist_add_int64(args, "int64", -64);

	nvlist_t * const table = fnvlist_alloc();
	fnvlist_add_string(table, "key", "value");
	nvlist_t * const nvlist = fnvlist_alloc();
	fnvlist_add_nvlist(nvlist, "table", table);
	fnvlist_add_boolean_value(nvlist, "boolean", B_FALSE);
	fnvlist_add_int64(nvlist, "integer", 42);
	fnvlist_add_string(nvlist, "string", "answer");
	/* */
	fnvlist_add_nvlist(args, "nvlist", nvlist);

	const boolean_t booleanArray[] = {
		B_FALSE, B_FALSE, B_TRUE, B_FALSE,
		B_TRUE,  B_FALSE, B_TRUE, B_FALSE
	};
	fnvlist_add_boolean_array(args, "booleanArray", booleanArray, 8);

	const char * const stringArray[] = {
		"array", "of", "strings"
	};
	fnvlist_add_string_array(args, "stringArray", stringArray, 3);

	const uint64_t uint64Array[] = {
		2,   4,   6,   8,   10,  12,  14,  16,
		18,  20,  22,  24,  26,  28,  30,  32,
		34,  36,  38,  40,  42,  44,  46,  48,
		50,  52,  54,  56,  58,  60,  62,  64,
		66,  68,  70,  72,  74,  76,  78,  80,
		82,  84,  86,  88,  90,  92,  94,  96,
		98,  100, 102, 104, 106, 108, 110, 112,
		114, 116, 118, 120, 122, 124, 126, 128
	};
	fnvlist_add_uint64_array(args, "uint64Array", uint64Array, 64);

	const int64_t int64Array[] = {
		-2,   -4,   -6,   -8,   -10,  -12,  -14,  -16,
		-18,  -20,  -22,  -24,  -26,  -28,  -30,  -32,
		-34,  -36,  -38,  -40,  -42,  -44,  -46,  -48,
		-50,  -52,  -54,  -56,  -58,  -60,  -62,  -64,
		-66,  -68,  -70,  -72,  -74,  -76,  -78,  -80,
		-82,  -84,  -86,  -88,  -90,  -92,  -94,  -96,
		-98,  -100, -102, -104, -106, -108, -110, -112,
		-114, -116, -118, -120, -122, -124, -126, -128
	};
	fnvlist_add_int64_array(args, "int64Array", int64Array, 64);

	const uint64_t overflowArray[] = {
		9223372036854775808u,    /* 2⁶³   wraps to -2⁶³ */
		18446744073709551615u    /* 2⁶⁴-1 wraps to -1 */
	};
	fnvlist_add_uint64_array(args, "overflowArray", overflowArray, 2);

	/* 2⁶³ + 2⁶² wraps to -2⁶² */
	fnvlist_add_uint64(args, "overflowScalar", 13835058055282163712u);

	nvlist_t *ret = NULL;
	const int error = lzc_channel_program(
	    pool, script, INSTRUCTION_LIMIT, MEMORY_LIMIT, args, &ret);
	fnvlist_free(args);
	fnvlist_free(nvlist);
	fnvlist_free(table);

	if (error)
		dump_nvlist(ret, STDERR_FILENO);

	fnvlist_free(ret);

	return (error);
}
