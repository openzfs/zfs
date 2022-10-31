/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2022 SRI International
 *
 * This software was developed by SRI International and the University of
 * Cambridge Computer Laboratory (Department of Computer Science and
 * Technology) under Defense Advanced Research Projects Agency (DARPA)
 * Contract No. HR001122C0110 ("ETC").
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/nvpair.h>
#include <sys/kmem.h>
#include <sys/stat.h>

#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define	NNVLISTS	4

#ifndef nitems
#define	nitems(x)	(sizeof (x) / sizeof ((x)[0]))
#endif

static int verbose;
static bool genrefs, ref_match_exact;
static const char *refdir;
static int refdir_fd;

static int tests_run, tests_failed;

static bool nvlist_equal(nvlist_t *nvl_a, nvlist_t *nvl_b);

static void
usage(void)
{
	printf("usage:\n");
	printf("    nvlist_pack [options] -a\n");
	printf("    nvlist_pack [options] <case1> [<case2> [...]]\n");
	printf("    nvlist_pack [options] -l\n");
	printf("options:\n");
	printf("    -a         Run all test cases\n");
	printf("    -l         list test cases\n");
	printf("    -r <dir>   reference directory\n");
	printf("    -R         generate reference files (requires -r)\n");
	printf("    -v         verbose output\n");
	printf("    -x         reference buffers must match exactly\n");
	exit(1);
}

struct nvcase {
	const char	*nvc_name;
	const char	*nvc_nvname;	/* pair name, nvc_name used if NULL */
	data_type_t	 nvc_type;
	int		 nvc_nelem;
	const void	*nvc_data;
	char		*nvc_failure_reason;
};

#define	NVCASE_SIMPLE(type, nvtype) {					\
	.nvc_name = #type,						\
	.nvc_type = nvtype,						\
	.nvc_data = &data_##type[0],					\
}
#define	NVCASE_STRING(string) {						\
	.nvc_name = "string_" string,				\
	.nvc_type = DATA_TYPE_STRING,					\
	.nvc_data = string,						\
}
#define	_NVCASE_ARRAY(type, nvtype, array, nelem) {			\
	.nvc_name = (type),						\
	.nvc_type = (nvtype),						\
	.nvc_nelem = (nelem),						\
	.nvc_data = (array),						\
}
#define	_NVCASE_ARRAY_ALL(type, nvtype)					\
	_NVCASE_ARRAY(#type "_array", (nvtype), data_##type,		\
	    nitems(data_##type))
#define	_NVCASE_ARRAY_EMPTY(type, nvtype)				\
	_NVCASE_ARRAY(#type "_array_empty", (nvtype), NULL, 0)
#define	_NVCASE_ARRAY_SINGLE(type, nvtype)				\
	_NVCASE_ARRAY(#type "_array_single", (nvtype), data_##type, 1)
#define	NVCASE_ARRAY(type, nvtype)					\
	_NVCASE_ARRAY_ALL(type, nvtype),				\
	_NVCASE_ARRAY_EMPTY(type, nvtype),				\
	_NVCASE_ARRAY_SINGLE(type, nvtype)

static boolean_t	data_boolean[] = {true, false, false, true};
static uchar_t		data_byte[] = {'a', 'b', 'c', 'd'};
static int8_t		data_int8[] = {0, 1, 2, -1};
static uint8_t		data_uint8[] = {0, 1, 2, 255};
static int16_t		data_int16[] = {0, 1, 2, -1};
static uint16_t		data_uint16[] = {0, 1, 2, 255};
static int32_t		data_int32[] = {0, 1, 2, -1};
static uint32_t		data_uint32[] = {0, 1, 2, UINT_MAX};
static int64_t		data_int64[] = {0, 1, 2, -1};
static uint64_t		data_uint64[] = {0, 1, 2, ULONG_MAX};

static hrtime_t		data_hrtime[] = {0};
static double		data_double[] = {0.0};

static const char	*data_string[] = {"a", "quick", "brown", "fox"};

/*
 * Initialized by init_nvlists().
 * The naming allow the use of NVCASE_ARRAY()
 */
static nvlist_t		_data_nvlist[NNVLISTS];
static nvlist_t		*data_nvlist[NNVLISTS];

static struct nvcase test_cases[] = {
	{
		.nvc_name = "boolean_flag",
		.nvc_type = DATA_TYPE_BOOLEAN,
	},
	NVCASE_SIMPLE(byte,	DATA_TYPE_BYTE),
	NVCASE_SIMPLE(int16,	DATA_TYPE_INT16),
	NVCASE_SIMPLE(uint16,	DATA_TYPE_UINT16),
	NVCASE_SIMPLE(int32,	DATA_TYPE_INT32),
	NVCASE_SIMPLE(uint32,	DATA_TYPE_UINT32),
	NVCASE_SIMPLE(int64,	DATA_TYPE_INT64),
	NVCASE_SIMPLE(uint64,	DATA_TYPE_UINT64),

	/* XXX: use fixed width nvnames to actually hit all aligments */
	NVCASE_STRING(""),
	NVCASE_STRING("0"),
	NVCASE_STRING("01"),
	NVCASE_STRING("012"),
	NVCASE_STRING("0123"),
	NVCASE_STRING("01234"),
	NVCASE_STRING("012345"),
	NVCASE_STRING("0123456"),
	NVCASE_STRING("01234567"),
	NVCASE_STRING("012345678"),
	NVCASE_STRING("0123456789"),
	NVCASE_STRING("0123456789a"),
	NVCASE_STRING("0123456789ab"),
	NVCASE_STRING("0123456789abc"),
	NVCASE_STRING("0123456789abcd"),
	NVCASE_STRING("0123456789abcde"),
	NVCASE_STRING("0123456789abcdef"),
	NVCASE_STRING("0123456789abcdefg"),

	NVCASE_ARRAY(byte,	DATA_TYPE_BYTE_ARRAY),
	NVCASE_ARRAY(int16,	DATA_TYPE_INT16_ARRAY),
	NVCASE_ARRAY(uint16,	DATA_TYPE_UINT16_ARRAY),
	NVCASE_ARRAY(int32,	DATA_TYPE_INT32_ARRAY),
	NVCASE_ARRAY(uint32,	DATA_TYPE_UINT32_ARRAY),
	NVCASE_ARRAY(int64,	DATA_TYPE_INT64_ARRAY),
	NVCASE_ARRAY(uint64,	DATA_TYPE_UINT64_ARRAY),
	NVCASE_ARRAY(string,	DATA_TYPE_STRING_ARRAY),
	NVCASE_SIMPLE(hrtime,	DATA_TYPE_HRTIME),

	{
		.nvc_name = "nvlist0",
		.nvc_type = DATA_TYPE_NVLIST,
		.nvc_data = &_data_nvlist[0],
	},
	{
		.nvc_name = "nvlist1",
		.nvc_type = DATA_TYPE_NVLIST,
		.nvc_data = &_data_nvlist[1],
	},
	{
		.nvc_name = "nvlist2",
		.nvc_type = DATA_TYPE_NVLIST,
		.nvc_data = &_data_nvlist[2],
	},
	{
		.nvc_name = "nvlist3",
		.nvc_type = DATA_TYPE_NVLIST,
		.nvc_data = &_data_nvlist[3],
	},
	NVCASE_ARRAY(nvlist,	DATA_TYPE_NVLIST_ARRAY),

	NVCASE_SIMPLE(boolean,	DATA_TYPE_BOOLEAN_VALUE),
	NVCASE_SIMPLE(int8,	DATA_TYPE_INT8),
	NVCASE_SIMPLE(uint8,	DATA_TYPE_UINT8),
	NVCASE_ARRAY(boolean,	DATA_TYPE_BOOLEAN_ARRAY),
	NVCASE_ARRAY(int8,	DATA_TYPE_INT8_ARRAY),
	NVCASE_ARRAY(uint8,	DATA_TYPE_UINT8_ARRAY),
	NVCASE_SIMPLE(double,	DATA_TYPE_DOUBLE),

	{
		.nvc_name = "empty_name",
		.nvc_nvname = "",
		.nvc_type = DATA_TYPE_BOOLEAN,
	},
};

static void
init_nvlists(void)
{
	nvlist_t *nvp;

	for (int i = 0; i < NNVLISTS; i++) {
		nvp = fnvlist_alloc();
		fnvlist_add_int32(nvp, "index", i);

		switch (i) {
		case 0:
			fnvlist_add_byte(nvp, "byte", 'b');
			fnvlist_add_uint32(nvp, "uint32", UINT_MAX);
			fnvlist_add_int64(nvp, "int64", -1);
			fnvlist_add_string(nvp, "string", "value");
			break;
		case 1:
			fnvlist_add_nvlist(nvp, "nvlist0", data_nvlist[0]);
			break;
		case 2:
			fnvlist_add_nvlist(nvp, "nvlist1", data_nvlist[1]);
			break;
		case 3:
			fnvlist_add_nvlist(nvp, "nvlist2", data_nvlist[2]);
			break;
		default:
			abort();
		}

		data_nvlist[i] = nvp;
		/*
		 * Hack to allow statically allocated nvlist storage
		 * Ideally nvlist_init() would be exposed and be able to
		 * alloc programmer managed storage, but it isn't so we
		 * cheat and copy the allocated one's contents into
		 * static storage to allow test_cases[] to be
		 * initialised at compile time.
		 */
		memcpy(&_data_nvlist[i], nvp, sizeof (_data_nvlist[i]));
	}
}

static void
list_tests(void)
{
	for (int i = 0; i < nitems(test_cases); i++)
		printf("'%s'\n", test_cases[i].nvc_name);
	exit(0);
}

static int
case_populate_nvlist(struct nvcase *tc, nvlist_t *nvl)
{
	const char *name = tc->nvc_nvname != NULL? tc->nvc_nvname :
	    tc->nvc_name;

	switch (tc->nvc_type) {
	case DATA_TYPE_BOOLEAN:
		return (nvlist_add_boolean(nvl, name));
	case DATA_TYPE_BOOLEAN_VALUE:
		return (nvlist_add_boolean_value(nvl, name,
		    *(boolean_t *)tc->nvc_data));
	case DATA_TYPE_BYTE:
		return (nvlist_add_byte(nvl, name,
		    *(uchar_t *)tc->nvc_data));
	case DATA_TYPE_INT8 :
		return (nvlist_add_int8(nvl, name,
		    *(int8_t *)tc->nvc_data));
	case DATA_TYPE_UINT8:
		return (nvlist_add_uint8(nvl, name,
		    *(uint8_t *)tc->nvc_data));
	case DATA_TYPE_INT16:
		return (nvlist_add_int16(nvl, name,
		    *(int16_t *)tc->nvc_data));
	case DATA_TYPE_UINT16:
		return (nvlist_add_uint16(nvl, name,
		    *(uint16_t *)tc->nvc_data));
	case DATA_TYPE_INT32:
		return (nvlist_add_int32(nvl, name,
		    *(int32_t *)tc->nvc_data));
	case DATA_TYPE_UINT32:
		return (nvlist_add_uint32(nvl, name,
		    *(uint32_t *)tc->nvc_data));
	case DATA_TYPE_INT64:
		return (nvlist_add_int64(nvl, name,
		    *(int64_t *)tc->nvc_data));
	case DATA_TYPE_UINT64:
		return (nvlist_add_uint64(nvl, name,
		    *(uint64_t *)tc->nvc_data));
	case DATA_TYPE_HRTIME:
		return (nvlist_add_hrtime(nvl, name,
		    *(hrtime_t *)tc->nvc_data));
	case DATA_TYPE_DOUBLE:
		return (nvlist_add_double(nvl, name,
		    *(double *)tc->nvc_data));

	case DATA_TYPE_BOOLEAN_ARRAY:
		return (nvlist_add_boolean_array(nvl, name,
		    tc->nvc_data, tc->nvc_nelem));
	case DATA_TYPE_BYTE_ARRAY:
		return (nvlist_add_byte_array(nvl, name,
		    tc->nvc_data, tc->nvc_nelem));
	case DATA_TYPE_INT8_ARRAY:
		return (nvlist_add_int8_array(nvl, name,
		    tc->nvc_data, tc->nvc_nelem));
	case DATA_TYPE_UINT8_ARRAY:
		return (nvlist_add_uint8_array(nvl, name,
		    tc->nvc_data, tc->nvc_nelem));
	case DATA_TYPE_INT16_ARRAY:
		return (nvlist_add_int16_array(nvl, name,
		    tc->nvc_data, tc->nvc_nelem));
	case DATA_TYPE_UINT16_ARRAY:
		return (nvlist_add_uint16_array(nvl, name,
		    tc->nvc_data, tc->nvc_nelem));
	case DATA_TYPE_INT32_ARRAY:
		return (nvlist_add_int32_array(nvl, name,
		    tc->nvc_data, tc->nvc_nelem));
	case DATA_TYPE_UINT32_ARRAY:
		return (nvlist_add_uint32_array(nvl, name,
		    tc->nvc_data, tc->nvc_nelem));
	case DATA_TYPE_INT64_ARRAY:
		return (nvlist_add_int64_array(nvl, name,
		    tc->nvc_data, tc->nvc_nelem));
	case DATA_TYPE_UINT64_ARRAY:
		return (nvlist_add_uint64_array(nvl, name,
		    tc->nvc_data, tc->nvc_nelem));

	case DATA_TYPE_STRING:
		return (nvlist_add_string(nvl, name,
		    tc->nvc_data));
	case DATA_TYPE_STRING_ARRAY:
		return (nvlist_add_string_array(nvl, name,
		    tc->nvc_data, tc->nvc_nelem));

	case DATA_TYPE_NVLIST:
		return (nvlist_add_nvlist(nvl, name,
		    tc->nvc_data));
	case DATA_TYPE_NVLIST_ARRAY:
		return (nvlist_add_nvlist_array(nvl, name,
		    tc->nvc_data, tc->nvc_nelem));
	default:
		return (-1);
	}
}

static nvlist_t *
case_create_nvlist(struct nvcase *tc)
{
	nvlist_t *nvl;

	if (nvlist_alloc(&nvl, 0, 0) != 0)
		return (NULL);

	if (case_populate_nvlist(tc, nvl) != 0)
		return (NULL);

	return (nvl);
}

static void
case_failed(struct nvcase *tc, const char *reason)
{
	if (tc->nvc_failure_reason != NULL)
		return;	/* Already called */
	tests_failed++;
	tc->nvc_failure_reason = strdup(reason);
	printf("FAIL: %s: %s\n", tc->nvc_name, reason);
}

static void
run_case(struct nvcase *tc)
{
	nvlist_t *created_nvl, *ref_nvl, *unpacked_nvl;
	char *packed_buffer = NULL;
	size_t buflen;

	tests_run++;

	created_nvl = case_create_nvlist(tc);
	if (created_nvl == NULL) {
		case_failed(tc, "case_create_nvlist");
		return;
	}
	if (nvlist_pack(created_nvl, &packed_buffer, &buflen, NV_ENCODE_XDR,
	    KM_SLEEP) != 0) {
		case_failed(tc, "nvlist_pack");
		return;
	}
	if (nvlist_unpack(packed_buffer, buflen, &unpacked_nvl,
	    KM_SLEEP) != 0) {
		case_failed(tc, "nvlist_unpack (round-trip)");
		return;
	}

	if (!nvlist_equal(created_nvl, unpacked_nvl)) {
		case_failed(tc,
		    "create and unpacked nvlists aren't equal");
		return;
	}

	if (refdir != NULL) {
		struct stat sb;
		char *ref_buffer, *ref_file;
		size_t ref_len;
		int ref_fd;

		if (asprintf(&ref_file, "%s.ref", tc->nvc_name) == -1) {
			case_failed(tc, "asprintf");
			return;
		}
		if (genrefs) {
			ref_fd = openat(refdir_fd, ref_file,
			    O_WRONLY | O_CREAT | O_TRUNC, 0660);
			if (ref_fd == -1) {
				fprintf(stderr,
				    "%s: unable to create ref file %s/%s\n",
				    tc->nvc_name, refdir, ref_file);
				exit(1);
			}
			if (write(ref_fd, packed_buffer, buflen) != buflen) {
				fprintf(stderr, "%s: failed to write packed\n",
				    tc->nvc_name);
				exit(1);
			}
			close(ref_fd);
		}

		ref_fd = openat(refdir_fd, ref_file, O_RDONLY);
		if (ref_fd == -1) {
			case_failed(tc, "failed to open ref file");
			return;
		}
		fstat(ref_fd, &sb);
		ref_len = sb.st_size;
		if (ref_len != buflen) {
			case_failed(tc,
			    "ref_len and buflen aren't the same size");
			close(ref_fd);
			return;
		}
		ref_buffer = malloc(ref_len);
		if (read(ref_fd, ref_buffer, ref_len) != sb.st_size) {
			case_failed(tc, "failed to read from ref file");
			close(ref_fd);
			return;
		}
		close(ref_fd);

		if (nvlist_unpack(ref_buffer, buflen, &ref_nvl,
		    KM_SLEEP) != 0) {
			case_failed(tc, "nvlist_unpack (ref)");
			return;
		}
		if (!nvlist_equal(created_nvl, ref_nvl)) {
			case_failed(tc,
			    "created and ref_unpacked nvlists aren't equal");
			return;
		}
		if (ref_match_exact &&
		    memcmp(packed_buffer, ref_buffer, buflen) != 0) {
			case_failed(tc, "packed and ref buffers differ");
			return;
		}
	}

	printf("PASS: %s\n", tc->nvc_name);
}

static void
run_case_name(const char *name)
{
	int i;

	for (i = 0; i < nitems(test_cases); i++) {
		if (strcmp(name, test_cases[i].nvc_name) == 0) {
			run_case(&test_cases[i]);
			return;
		}
	}
	fprintf(stderr, "unknown test: '%s'\n", name);
	exit(1);
}

/* CSTYLED */
#define	NVP_EQUAL_TYPE(type, name) __extension__({			\
	bool is_equal;							\
	type a, b;							\
	nvpair_value_##name(nvp_a, &a);					\
	nvpair_value_##name(nvp_b, &b);					\
	is_equal = (a == b);						\
	is_equal;							\
})

/* CSTYLED */
#define	NVP_EQUAL_TYPE_ARRAY(type, name) __extension__({		\
	bool is_equal;							\
	type *a, *b;							\
	uint_t nelem_a, nelem_b;					\
	nvpair_value_##name##_array(nvp_a, &a, &nelem_a);		\
	nvpair_value_##name##_array(nvp_b, &b, &nelem_b);		\
	is_equal = (nelem_a == nelem_b);				\
	if (is_equal)							\
		for (uint_t i = 0; i < nelem_a; i++)			\
			if (a[i] != b[i]) {				\
				is_equal = false;			\
				break;					\
			}						\
	is_equal;							\
})

static bool
nvpair_value_equal(nvpair_t *nvp_a, nvpair_t *nvp_b)
{
	if (nvpair_type(nvp_a) != nvpair_type(nvp_b)) {
		if (verbose >= 2)
			printf("%s: pair types differ\n", __func__);
		return (false);
	}
	switch (nvpair_type(nvp_a)) {
	case DATA_TYPE_BOOLEAN:
		return (true);	/* Presence is the value */

	case DATA_TYPE_BOOLEAN_VALUE:
		if (NVP_EQUAL_TYPE(boolean_t, boolean_value))
			return (true);
		break;
	case DATA_TYPE_BYTE:
		if (NVP_EQUAL_TYPE(uchar_t, byte))
			return (true);
		break;
	case DATA_TYPE_INT8 :
		if (NVP_EQUAL_TYPE(int8_t, int8))
			return (true);
		break;
	case DATA_TYPE_UINT8:
		if (NVP_EQUAL_TYPE(uint8_t, uint8))
			return (true);
		break;
	case DATA_TYPE_INT16:
		if (NVP_EQUAL_TYPE(int16_t, int16))
			return (true);
		break;
	case DATA_TYPE_UINT16:
		if (NVP_EQUAL_TYPE(uint16_t, uint16))
			return (true);
		break;
	case DATA_TYPE_INT32:
		if (NVP_EQUAL_TYPE(int32_t, int32))
			return (true);
		break;
	case DATA_TYPE_UINT32:
		if (NVP_EQUAL_TYPE(uint32_t, uint32))
			return (true);
		break;
	case DATA_TYPE_INT64:
		if (NVP_EQUAL_TYPE(int64_t, int64))
			return (true);
		break;
	case DATA_TYPE_UINT64:
		if (NVP_EQUAL_TYPE(uint64_t, uint64))
			return (true);
		break;
	case DATA_TYPE_HRTIME:
		if (NVP_EQUAL_TYPE(hrtime_t, hrtime))
			return (true);
		break;
	case DATA_TYPE_DOUBLE:
		if (NVP_EQUAL_TYPE(double, double))
			return (true);
		break;

	case DATA_TYPE_BOOLEAN_ARRAY:
		if (NVP_EQUAL_TYPE_ARRAY(boolean_t, boolean))
			return (true);
		break;
	case DATA_TYPE_BYTE_ARRAY:
		if (NVP_EQUAL_TYPE_ARRAY(uchar_t, byte))
			return (true);
		break;
	case DATA_TYPE_INT8_ARRAY:
		if (NVP_EQUAL_TYPE_ARRAY(int8_t, int8))
			return (true);
		break;
	case DATA_TYPE_UINT8_ARRAY:
		if (NVP_EQUAL_TYPE_ARRAY(uint8_t, uint8))
			return (true);
		break;
	case DATA_TYPE_INT16_ARRAY:
		if (NVP_EQUAL_TYPE_ARRAY(int16_t, int16))
			return (true);
		break;
	case DATA_TYPE_UINT16_ARRAY:
		if (NVP_EQUAL_TYPE_ARRAY(uint16_t, uint16))
			return (true);
		break;
	case DATA_TYPE_INT32_ARRAY:
		if (NVP_EQUAL_TYPE_ARRAY(int32_t, int32))
			return (true);
		break;
	case DATA_TYPE_UINT32_ARRAY:
		if (NVP_EQUAL_TYPE_ARRAY(uint32_t, uint32))
			return (true);
		break;
	case DATA_TYPE_INT64_ARRAY:
		if (NVP_EQUAL_TYPE_ARRAY(int64_t, int64))
			return (true);
		break;
	case DATA_TYPE_UINT64_ARRAY:
		if (NVP_EQUAL_TYPE_ARRAY(uint64_t, uint64))
			return (true);
		break;

	case DATA_TYPE_STRING: {
		char *str_a, *str_b;
		nvpair_value_string(nvp_a, &str_a);
		nvpair_value_string(nvp_b, &str_b);
		if (strcmp(str_a, str_b) == 0)
			return (true);
		break;
	}
	case DATA_TYPE_STRING_ARRAY: {
		char **stra_a, **stra_b;
		uint_t nelem_a, nelem_b;
		nvpair_value_string_array(nvp_a, &stra_a, &nelem_a);
		nvpair_value_string_array(nvp_b, &stra_b, &nelem_b);
		if (nelem_a != nelem_b)
			goto not_equal;
		for (int i = 0; i < nelem_a; i++) {
			if (strcmp(stra_a[i], stra_b[i]) != 0)
				goto not_equal;
		}
		return (true);
	}

	case DATA_TYPE_NVLIST: {
		nvlist_t *nvl_a, *nvl_b;
		nvpair_value_nvlist(nvp_a, &nvl_a);
		nvpair_value_nvlist(nvp_b, &nvl_b);
		if (nvlist_equal(nvl_a, nvl_b))
			return (true);
		break;
	}
	case DATA_TYPE_NVLIST_ARRAY: {
		nvlist_t **nvla_a, **nvla_b;
		uint_t nelem_a, nelem_b;
		nvpair_value_nvlist_array(nvp_a, &nvla_a, &nelem_a);
		nvpair_value_nvlist_array(nvp_b, &nvla_b, &nelem_b);
		if (nelem_a != nelem_b)
			goto not_equal;
		for (int i = 0; i < nelem_a; i++) {
			if (!nvlist_equal(nvla_a[i], nvla_b[i]))
				goto not_equal;
		}
		return (true);
	}

	case DATA_TYPE_DONTCARE:
	case DATA_TYPE_UNKNOWN:
		if (verbose >= 2)
			printf("%s: unhandled type %d\n", __func__,
			    nvpair_type(nvp_a));
		return (false);
	}

not_equal:
	if (verbose >= 2)
		printf("%s: values are not equal\n", __func__);
	return (false);
}
#undef NVP_EQUAL_TYPE
#undef NVP_EQUAL_TYPE_ARRAY

static bool
nvpair_equal(nvpair_t *nvp_a, nvpair_t *nvp_b)
{
	if (strcmp(nvpair_name(nvp_a), nvpair_name(nvp_b)) != 0) {
		if (verbose >= 2)
			printf("%s: pair names differ\n", __func__);
		return (false);
	}
	return (nvpair_value_equal(nvp_a, nvp_b));
}

/*
 * nvlist_equal - check if two nvlists are equal.
 *
 * Each pair be present in each list and they must appear in the same
 * order.  While ordering does not matter from an API perspective, it
 * must hold for the packed forms to be identical.
 */
static bool
nvlist_equal(nvlist_t *nvl_a, nvlist_t *nvl_b)
{
	nvpair_t *nvp_a, *nvp_b;

	if (fnvlist_num_pairs(nvl_a) != fnvlist_num_pairs(nvl_b))
		return (false);

	if (verbose >= 3) {
		nvpair_t *pair;
		pair = NULL;
		printf("dumping nvp_a\n");
		while ((pair = nvlist_next_nvpair(nvl_a, pair)) != NULL)
			printf("'%s'\n", nvpair_name(pair));
		printf("dumping nvp_b\n");
		while ((pair = nvlist_next_nvpair(nvl_b, pair)) != NULL)
			printf("'%s'\n", nvpair_name(pair));
	}

	for (nvp_a = nvlist_next_nvpair(nvl_a, NULL),
	    nvp_b = nvlist_next_nvpair(nvl_b, NULL);
	    nvp_a != NULL && nvp_b != NULL;
	    nvp_a = nvlist_next_nvpair(nvl_a, nvp_a),
	    nvp_b = nvlist_next_nvpair(nvl_b, nvp_b)) {
		if (!nvpair_equal(nvp_a, nvp_b))
			return (false);
	}
	if (nvp_a == NULL && nvp_b == NULL)
		return (true);

	return (false);
}

int
main(int argc, char **argv)
{
	int i, opt;
	bool list, run_all;

	list = run_all = false;

	while ((opt = getopt(argc, argv, "alr:Rvx")) != -1) {
		switch (opt) {
		case 'a':
			run_all = true;
			break;
		case 'l':
			list = true;
			break;
		case 'r':
			refdir = optarg;
			break;
		case 'R':
			genrefs = true;
			break;
		case 'v':
			verbose++;
			break;
		case 'x':
			ref_match_exact = true;
			break;
		default:
			fprintf(stderr, "unknown argument %c\n", opt);
			usage();
		}
	}
	argc -= optind;
	argv += optind;
	if (run_all && list) {
		fprintf(stderr, "-a and -l are incompatible\n");
		usage();
	}
	if (list && genrefs) {
		fprintf(stderr, "-l and -R are incompatible\n");
		usage();
	}
	if (list && refdir != NULL) {
		fprintf(stderr, "-l and -r are incompatible\n");
		usage();
	}
	if (list) {
		if (argc == 0)
			list_tests();
		fprintf(stderr, "-l and a list of test are incompatible\n");
		usage();
	}
	if (argc == 0 && !run_all)
		usage();
	if (argc > 0 && run_all) {
		fprintf(stderr, "-a and a list of cases are incompatible\n");
		usage();
	}

	if (refdir != NULL) {
#ifdef O_PATH
		const int o_path_flag = O_PATH;
#else
		const int o_path_flag = O_RDONLY;
#endif

		refdir_fd = open(refdir, O_DIRECTORY | O_CLOEXEC | o_path_flag);
		if (refdir_fd == -1) {
			fprintf(stderr, "Failed to open refdir %s: %s\n",
			    refdir, strerror(errno));
			exit(1);
		}
	}

	init_nvlists();

	if (run_all) {
		for (i = 0; i < nitems(test_cases); i++)
			run_case(&test_cases[i]);
	} else {
		for (i = 0; i < argc; i++)
			run_case_name(argv[i]);
	}

	if (verbose > 0 && tests_failed > 0) {
		printf("Unexpected failures:\n");
		for (i = 0; i < nitems(test_cases); i++) {
			if (test_cases[i].nvc_failure_reason != NULL) {
				printf("\t%s: %s\n", test_cases[i].nvc_name,
				    test_cases[i].nvc_failure_reason);
			}
		}
	}
	if (verbose >= 0) {
		printf("SUMMARY");
		if (tests_run - tests_failed > 0)
			printf(": passed %d", tests_run - tests_failed);
		if (tests_failed > 0)
			printf(": failed %d", tests_failed);
		printf("\n");
	}

	return (tests_failed);
}
