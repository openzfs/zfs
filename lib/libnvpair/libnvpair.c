/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
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
/*
 * Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#include <unistd.h>
#include <strings.h>
#include <libintl.h>
#include <sys/types.h>
#include <sys/inttypes.h>
#include "libnvpair.h"

/*
 * libnvpair - A tools library for manipulating <name, value> pairs.
 *
 *	This library provides routines packing an unpacking nv pairs
 *	for transporting data across process boundaries, transporting
 *	between kernel and userland, and possibly saving onto disk files.
 */

static void
indent(FILE *fp, int depth)
{
	while (depth-- > 0)
		(void) fprintf(fp, "\t");
}

/*
 * nvlist_print - Prints elements in an event buffer
 */
static
void
nvlist_print_with_indent(FILE *fp, nvlist_t *nvl, int depth)
{
	int i;
	char *name;
	uint_t nelem;
	nvpair_t *nvp;

	if (nvl == NULL)
		return;

	indent(fp, depth);
	(void) fprintf(fp, "nvlist version: %d\n", NVL_VERSION(nvl));

	nvp = nvlist_next_nvpair(nvl, NULL);

	while (nvp) {
		data_type_t type = nvpair_type(nvp);

		indent(fp, depth);
		name = nvpair_name(nvp);
		(void) fprintf(fp, "\t%s =", name);
		nelem = 0;
		switch (type) {
		case DATA_TYPE_BOOLEAN: {
			(void) fprintf(fp, " 1");
			break;
		}
		case DATA_TYPE_BOOLEAN_VALUE: {
			boolean_t val;
			(void) nvpair_value_boolean_value(nvp, &val);
			(void) fprintf(fp, " %d", val);
			break;
		}
		case DATA_TYPE_BYTE: {
			uchar_t val;
			(void) nvpair_value_byte(nvp, &val);
			(void) fprintf(fp, " 0x%2.2x", val);
			break;
		}
		case DATA_TYPE_INT8: {
			int8_t val;
			(void) nvpair_value_int8(nvp, &val);
			(void) fprintf(fp, " %d", val);
			break;
		}
		case DATA_TYPE_UINT8: {
			uint8_t val;
			(void) nvpair_value_uint8(nvp, &val);
			(void) fprintf(fp, " 0x%x", val);
			break;
		}
		case DATA_TYPE_INT16: {
			int16_t val;
			(void) nvpair_value_int16(nvp, &val);
			(void) fprintf(fp, " %d", val);
			break;
		}
		case DATA_TYPE_UINT16: {
			uint16_t val;
			(void) nvpair_value_uint16(nvp, &val);
			(void) fprintf(fp, " 0x%x", val);
			break;
		}
		case DATA_TYPE_INT32: {
			int32_t val;
			(void) nvpair_value_int32(nvp, &val);
			(void) fprintf(fp, " %d", val);
			break;
		}
		case DATA_TYPE_UINT32: {
			uint32_t val;
			(void) nvpair_value_uint32(nvp, &val);
			(void) fprintf(fp, " 0x%x", val);
			break;
		}
		case DATA_TYPE_INT64: {
			int64_t val;
			(void) nvpair_value_int64(nvp, &val);
			(void) fprintf(fp, " %lld", (longlong_t)val);
			break;
		}
		case DATA_TYPE_UINT64: {
			uint64_t val;
			(void) nvpair_value_uint64(nvp, &val);
			(void) fprintf(fp, " 0x%llx", (u_longlong_t)val);
			break;
		}
		case DATA_TYPE_DOUBLE: {
			double val;
			(void) nvpair_value_double(nvp, &val);
			(void) fprintf(fp, " 0x%llf", val);
			break;
		}
		case DATA_TYPE_STRING: {
			char *val;
			(void) nvpair_value_string(nvp, &val);
			(void) fprintf(fp, " %s", val);
			break;
		}
		case DATA_TYPE_BOOLEAN_ARRAY: {
			boolean_t *val;
			(void) nvpair_value_boolean_array(nvp, &val, &nelem);
			for (i = 0; i < nelem; i++)
				(void) fprintf(fp, " %d", val[i]);
			break;
		}
		case DATA_TYPE_BYTE_ARRAY: {
			uchar_t *val;
			(void) nvpair_value_byte_array(nvp, &val, &nelem);
			for (i = 0; i < nelem; i++)
				(void) fprintf(fp, " 0x%2.2x", val[i]);
			break;
		}
		case DATA_TYPE_INT8_ARRAY: {
			int8_t *val;
			(void) nvpair_value_int8_array(nvp, &val, &nelem);
			for (i = 0; i < nelem; i++)
				(void) fprintf(fp, " %d", val[i]);
			break;
		}
		case DATA_TYPE_UINT8_ARRAY: {
			uint8_t *val;
			(void) nvpair_value_uint8_array(nvp, &val, &nelem);
			for (i = 0; i < nelem; i++)
				(void) fprintf(fp, " 0x%x", val[i]);
			break;
		}
		case DATA_TYPE_INT16_ARRAY: {
			int16_t *val;
			(void) nvpair_value_int16_array(nvp, &val, &nelem);
			for (i = 0; i < nelem; i++)
				(void) fprintf(fp, " %d", val[i]);
			break;
		}
		case DATA_TYPE_UINT16_ARRAY: {
			uint16_t *val;
			(void) nvpair_value_uint16_array(nvp, &val, &nelem);
			for (i = 0; i < nelem; i++)
				(void) fprintf(fp, " 0x%x", val[i]);
			break;
		}
		case DATA_TYPE_INT32_ARRAY: {
			int32_t *val;
			(void) nvpair_value_int32_array(nvp, &val, &nelem);
			for (i = 0; i < nelem; i++)
				(void) fprintf(fp, " %d", val[i]);
			break;
		}
		case DATA_TYPE_UINT32_ARRAY: {
			uint32_t *val;
			(void) nvpair_value_uint32_array(nvp, &val, &nelem);
			for (i = 0; i < nelem; i++)
				(void) fprintf(fp, " 0x%x", val[i]);
			break;
		}
		case DATA_TYPE_INT64_ARRAY: {
			int64_t *val;
			(void) nvpair_value_int64_array(nvp, &val, &nelem);
			for (i = 0; i < nelem; i++)
				(void) fprintf(fp, " %lld", (longlong_t)val[i]);
			break;
		}
		case DATA_TYPE_UINT64_ARRAY: {
			uint64_t *val;
			(void) nvpair_value_uint64_array(nvp, &val, &nelem);
			for (i = 0; i < nelem; i++)
				(void) fprintf(fp, " 0x%llx",
				    (u_longlong_t)val[i]);
			break;
		}
		case DATA_TYPE_STRING_ARRAY: {
			char **val;
			(void) nvpair_value_string_array(nvp, &val, &nelem);
			for (i = 0; i < nelem; i++)
				(void) fprintf(fp, " %s", val[i]);
			break;
		}
		case DATA_TYPE_HRTIME: {
			hrtime_t val;
			(void) nvpair_value_hrtime(nvp, &val);
			(void) fprintf(fp, " 0x%llx", val);
			break;
		}
		case DATA_TYPE_NVLIST: {
			nvlist_t *val;
			(void) nvpair_value_nvlist(nvp, &val);
			(void) fprintf(fp, " (embedded nvlist)\n");
			nvlist_print_with_indent(fp, val, depth + 1);
			indent(fp, depth + 1);
			(void) fprintf(fp, "(end %s)\n", name);
			break;
		}
		case DATA_TYPE_NVLIST_ARRAY: {
			nvlist_t **val;
			(void) nvpair_value_nvlist_array(nvp, &val, &nelem);
			(void) fprintf(fp, " (array of embedded nvlists)\n");
			for (i = 0; i < nelem; i++) {
				indent(fp, depth + 1);
				(void) fprintf(fp,
				    "(start %s[%d])\n", name, i);
				nvlist_print_with_indent(fp, val[i], depth + 1);
				indent(fp, depth + 1);
				(void) fprintf(fp, "(end %s[%d])\n", name, i);
			}
			break;
		}
		default:
			(void) fprintf(fp, " unknown data type (%d)", type);
			break;
		}
		(void) fprintf(fp, "\n");
		nvp = nvlist_next_nvpair(nvl, nvp);
	}
}

void
nvlist_print(FILE *fp, nvlist_t *nvl)
{
	nvlist_print_with_indent(fp, nvl, 0);
}


#define	NVP(elem, type, vtype, ptype, format) { \
	vtype	value; \
\
	(void) nvpair_value_##type(elem, &value); \
	(void) printf("%*s%s: " format "\n", indent, "", \
	    nvpair_name(elem), (ptype)value); \
}

#define	NVPA(elem, type, vtype, ptype, format) { \
	uint_t	i, count; \
	vtype	*value;  \
\
	(void) nvpair_value_##type(elem, &value, &count); \
	for (i = 0; i < count; i++) { \
		(void) printf("%*s%s[%d]: " format "\n", indent, "", \
		    nvpair_name(elem), i, (ptype)value[i]); \
	} \
}

/*
 * Similar to nvlist_print() but handles arrays slightly differently.
 */
void
dump_nvlist(nvlist_t *list, int indent)
{
	nvpair_t	*elem = NULL;
	boolean_t	bool_value;
	nvlist_t	*nvlist_value;
	nvlist_t	**nvlist_array_value;
	uint_t		i, count;

	if (list == NULL) {
		return;
	}

	while ((elem = nvlist_next_nvpair(list, elem)) != NULL) {
		switch (nvpair_type(elem)) {
		case DATA_TYPE_BOOLEAN_VALUE:
			(void) nvpair_value_boolean_value(elem, &bool_value);
			(void) printf("%*s%s: %s\n", indent, "",
			    nvpair_name(elem), bool_value ? "true" : "false");
			break;

		case DATA_TYPE_BYTE:
			NVP(elem, byte, uchar_t, int, "%u");
			break;

		case DATA_TYPE_INT8:
			NVP(elem, int8, int8_t, int, "%d");
			break;

		case DATA_TYPE_UINT8:
			NVP(elem, uint8, uint8_t, int, "%u");
			break;

		case DATA_TYPE_INT16:
			NVP(elem, int16, int16_t, int, "%d");
			break;

		case DATA_TYPE_UINT16:
			NVP(elem, uint16, uint16_t, int, "%u");
			break;

		case DATA_TYPE_INT32:
			NVP(elem, int32, int32_t, long, "%ld");
			break;

		case DATA_TYPE_UINT32:
			NVP(elem, uint32, uint32_t, ulong_t, "%lu");
			break;

		case DATA_TYPE_INT64:
			NVP(elem, int64, int64_t, longlong_t, "%lld");
			break;

		case DATA_TYPE_UINT64:
			NVP(elem, uint64, uint64_t, u_longlong_t, "%llu");
			break;

		case DATA_TYPE_STRING:
			NVP(elem, string, char *, char *, "'%s'");
			break;

		case DATA_TYPE_BYTE_ARRAY:
			NVPA(elem, byte_array, uchar_t, int, "%u");
			break;

		case DATA_TYPE_INT8_ARRAY:
			NVPA(elem, int8_array, int8_t, int, "%d");
			break;

		case DATA_TYPE_UINT8_ARRAY:
			NVPA(elem, uint8_array, uint8_t, int, "%u");
			break;

		case DATA_TYPE_INT16_ARRAY:
			NVPA(elem, int16_array, int16_t, int, "%d");
			break;

		case DATA_TYPE_UINT16_ARRAY:
			NVPA(elem, uint16_array, uint16_t, int, "%u");
			break;

		case DATA_TYPE_INT32_ARRAY:
			NVPA(elem, int32_array, int32_t, long, "%ld");
			break;

		case DATA_TYPE_UINT32_ARRAY:
			NVPA(elem, uint32_array, uint32_t, ulong_t, "%lu");
			break;

		case DATA_TYPE_INT64_ARRAY:
			NVPA(elem, int64_array, int64_t, longlong_t, "%lld");
			break;

		case DATA_TYPE_UINT64_ARRAY:
			NVPA(elem, uint64_array, uint64_t, u_longlong_t,
			    "%llu");
			break;

		case DATA_TYPE_STRING_ARRAY:
			NVPA(elem, string_array, char *, char *, "'%s'");
			break;

		case DATA_TYPE_NVLIST:
			(void) nvpair_value_nvlist(elem, &nvlist_value);
			(void) printf("%*s%s:\n", indent, "",
			    nvpair_name(elem));
			dump_nvlist(nvlist_value, indent + 4);
			break;

		case DATA_TYPE_NVLIST_ARRAY:
			(void) nvpair_value_nvlist_array(elem,
			    &nvlist_array_value, &count);
			for (i = 0; i < count; i++) {
				(void) printf("%*s%s[%u]:\n", indent, "",
				    nvpair_name(elem), i);
				dump_nvlist(nvlist_array_value[i], indent + 4);
			}
			break;

		default:
			(void) printf(dgettext(TEXT_DOMAIN, "bad config type "
			    "%d for %s\n"), nvpair_type(elem),
			    nvpair_name(elem));
		}
	}
}

/*
 * Determine if string 'value' matches 'nvp' value.  The 'value' string is
 * converted, depending on the type of 'nvp', prior to match.  For numeric
 * types, a radix independent sscanf conversion of 'value' is used. If 'nvp'
 * is an array type, 'ai' is the index into the array against which we are
 * checking for match. If nvp is of DATA_TYPE_STRING*, the caller can pass
 * in a regex_t compilation of value in 'value_regex' to trigger regular
 * expression string match instead of simple strcmp().
 *
 * Return 1 on match, 0 on no-match, and -1 on error.  If the error is
 * related to value syntax error and 'ep' is non-NULL, *ep will point into
 * the 'value' string at the location where the error exists.
 *
 * NOTE: It may be possible to move the non-regex_t version of this into
 * common code used by library/kernel/boot.
 */
int
nvpair_value_match_regex(nvpair_t *nvp, int ai,
    char *value, regex_t *value_regex, char **ep)
{
	char	*evalue;
	uint_t	a_len;
	int	sr;

	if (ep)
		*ep = NULL;

	if ((nvp == NULL) || (value == NULL))
		return (-1);		/* error fail match - invalid args */

	/* make sure array and index combination make sense */
	if ((nvpair_type_is_array(nvp) && (ai < 0)) ||
	    (!nvpair_type_is_array(nvp) && (ai >= 0)))
		return (-1);		/* error fail match - bad index */

	/* non-string values should be single 'chunk' */
	if ((nvpair_type(nvp) != DATA_TYPE_STRING) &&
	    (nvpair_type(nvp) != DATA_TYPE_STRING_ARRAY)) {
		value += strspn(value, " \t");
		evalue = value + strcspn(value, " \t");
		if (*evalue) {
			if (ep)
				*ep = evalue;
			return (-1);	/* error fail match - syntax */
		}
	}

	sr = EOF;
	switch (nvpair_type(nvp)) {
	case DATA_TYPE_STRING: {
		char	*val;

		/* check string value for match */
		if (nvpair_value_string(nvp, &val) == 0) {
			if (value_regex) {
				if (regexec(value_regex, val,
				    (size_t)0, NULL, 0) == 0)
					return (1);	/* match */
			} else {
				if (strcmp(value, val) == 0)
					return (1);	/* match */
			}
		}
		break;
	}
	case DATA_TYPE_STRING_ARRAY: {
		char **val_array;

		/* check indexed string value of array for match */
		if ((nvpair_value_string_array(nvp, &val_array, &a_len) == 0) &&
		    (ai < a_len)) {
			if (value_regex) {
				if (regexec(value_regex, val_array[ai],
				    (size_t)0, NULL, 0) == 0)
					return (1);
			} else {
				if (strcmp(value, val_array[ai]) == 0)
					return (1);
			}
		}
		break;
	}
	case DATA_TYPE_BYTE: {
		uchar_t val, val_arg;

		/* scanf uchar_t from value and check for match */
		sr = sscanf(value, "%c", &val_arg);
		if ((sr == 1) && (nvpair_value_byte(nvp, &val) == 0) &&
		    (val == val_arg))
			return (1);
		break;
	}
	case DATA_TYPE_BYTE_ARRAY: {
		uchar_t *val_array, val_arg;


		/* check indexed value of array for match */
		sr = sscanf(value, "%c", &val_arg);
		if ((sr == 1) &&
		    (nvpair_value_byte_array(nvp, &val_array, &a_len) == 0) &&
		    (ai < a_len) &&
		    (val_array[ai] == val_arg))
			return (1);
		break;
	}
	case DATA_TYPE_INT8: {
		int8_t val, val_arg;

		/* scanf int8_t from value and check for match */
		sr = sscanf(value, "%"SCNi8, &val_arg);
		if ((sr == 1) &&
		    (nvpair_value_int8(nvp, &val) == 0) &&
		    (val == val_arg))
			return (1);
		break;
	}
	case DATA_TYPE_INT8_ARRAY: {
		int8_t *val_array, val_arg;

		/* check indexed value of array for match */
		sr = sscanf(value, "%"SCNi8, &val_arg);
		if ((sr == 1) &&
		    (nvpair_value_int8_array(nvp, &val_array, &a_len) == 0) &&
		    (ai < a_len) &&
		    (val_array[ai] == val_arg))
			return (1);
		break;
	}
	case DATA_TYPE_UINT8: {
		uint8_t val, val_arg;

		/* scanf uint8_t from value and check for match */
		sr = sscanf(value, "%"SCNi8, (int8_t *)&val_arg);
		if ((sr == 1) &&
		    (nvpair_value_uint8(nvp, &val) == 0) &&
		    (val == val_arg))
			return (1);
		break;
	}
	case DATA_TYPE_UINT8_ARRAY: {
		uint8_t *val_array, val_arg;

		/* check indexed value of array for match */
		sr = sscanf(value, "%"SCNi8, (int8_t *)&val_arg);
		if ((sr == 1) &&
		    (nvpair_value_uint8_array(nvp, &val_array, &a_len) == 0) &&
		    (ai < a_len) &&
		    (val_array[ai] == val_arg))
			return (1);
		break;
	}
	case DATA_TYPE_INT16: {
		int16_t val, val_arg;

		/* scanf int16_t from value and check for match */
		sr = sscanf(value, "%"SCNi16, &val_arg);
		if ((sr == 1) &&
		    (nvpair_value_int16(nvp, &val) == 0) &&
		    (val == val_arg))
			return (1);
		break;
	}
	case DATA_TYPE_INT16_ARRAY: {
		int16_t *val_array, val_arg;

		/* check indexed value of array for match */
		sr = sscanf(value, "%"SCNi16, &val_arg);
		if ((sr == 1) &&
		    (nvpair_value_int16_array(nvp, &val_array, &a_len) == 0) &&
		    (ai < a_len) &&
		    (val_array[ai] == val_arg))
			return (1);
		break;
	}
	case DATA_TYPE_UINT16: {
		uint16_t val, val_arg;

		/* scanf uint16_t from value and check for match */
		sr = sscanf(value, "%"SCNi16, (int16_t *)&val_arg);
		if ((sr == 1) &&
		    (nvpair_value_uint16(nvp, &val) == 0) &&
		    (val == val_arg))
			return (1);
		break;
	}
	case DATA_TYPE_UINT16_ARRAY: {
		uint16_t *val_array, val_arg;

		/* check indexed value of array for match */
		sr = sscanf(value, "%"SCNi16, (int16_t *)&val_arg);
		if ((sr == 1) &&
		    (nvpair_value_uint16_array(nvp, &val_array, &a_len) == 0) &&
		    (ai < a_len) &&
		    (val_array[ai] == val_arg))
			return (1);
		break;
	}
	case DATA_TYPE_INT32: {
		int32_t val, val_arg;

		/* scanf int32_t from value and check for match */
		sr = sscanf(value, "%"SCNi32, &val_arg);
		if ((sr == 1) &&
		    (nvpair_value_int32(nvp, &val) == 0) &&
		    (val == val_arg))
			return (1);
		break;
	}
	case DATA_TYPE_INT32_ARRAY: {
		int32_t *val_array, val_arg;

		/* check indexed value of array for match */
		sr = sscanf(value, "%"SCNi32, &val_arg);
		if ((sr == 1) &&
		    (nvpair_value_int32_array(nvp, &val_array, &a_len) == 0) &&
		    (ai < a_len) &&
		    (val_array[ai] == val_arg))
			return (1);
		break;
	}
	case DATA_TYPE_UINT32: {
		uint32_t val, val_arg;

		/* scanf uint32_t from value and check for match */
		sr = sscanf(value, "%"SCNi32, (int32_t *)&val_arg);
		if ((sr == 1) &&
		    (nvpair_value_uint32(nvp, &val) == 0) &&
		    (val == val_arg))
			return (1);
		break;
	}
	case DATA_TYPE_UINT32_ARRAY: {
		uint32_t *val_array, val_arg;

		/* check indexed value of array for match */
		sr = sscanf(value, "%"SCNi32, (int32_t *)&val_arg);
		if ((sr == 1) &&
		    (nvpair_value_uint32_array(nvp, &val_array, &a_len) == 0) &&
		    (ai < a_len) &&
		    (val_array[ai] == val_arg))
			return (1);
		break;
	}
	case DATA_TYPE_INT64: {
		int64_t val, val_arg;

		/* scanf int64_t from value and check for match */
		sr = sscanf(value, "%"SCNi64, &val_arg);
		if ((sr == 1) &&
		    (nvpair_value_int64(nvp, &val) == 0) &&
		    (val == val_arg))
			return (1);
		break;
	}
	case DATA_TYPE_INT64_ARRAY: {
		int64_t *val_array, val_arg;

		/* check indexed value of array for match */
		sr = sscanf(value, "%"SCNi64, &val_arg);
		if ((sr == 1) &&
		    (nvpair_value_int64_array(nvp, &val_array, &a_len) == 0) &&
		    (ai < a_len) &&
		    (val_array[ai] == val_arg))
				return (1);
		break;
	}
	case DATA_TYPE_UINT64: {
		uint64_t val_arg, val;

		/* scanf uint64_t from value and check for match */
		sr = sscanf(value, "%"SCNi64, (int64_t *)&val_arg);
		if ((sr == 1) &&
		    (nvpair_value_uint64(nvp, &val) == 0) &&
		    (val == val_arg))
			return (1);
		break;
	}
	case DATA_TYPE_UINT64_ARRAY: {
		uint64_t *val_array, val_arg;

		/* check indexed value of array for match */
		sr = sscanf(value, "%"SCNi64, (int64_t *)&val_arg);
		if ((sr == 1) &&
		    (nvpair_value_uint64_array(nvp, &val_array, &a_len) == 0) &&
		    (ai < a_len) &&
		    (val_array[ai] == val_arg))
			return (1);
		break;
	}
	case DATA_TYPE_BOOLEAN_VALUE: {
		boolean_t val, val_arg;

		/* scanf boolean_t from value and check for match */
		sr = sscanf(value, "%"SCNi32, &val_arg);
		if ((sr == 1) &&
		    (nvpair_value_boolean_value(nvp, &val) == 0) &&
		    (val == val_arg))
			return (1);
		break;
	}
	case DATA_TYPE_BOOLEAN_ARRAY: {
		boolean_t *val_array, val_arg;

		/* check indexed value of array for match */
		sr = sscanf(value, "%"SCNi32, &val_arg);
		if ((sr == 1) &&
		    (nvpair_value_boolean_array(nvp,
		    &val_array, &a_len) == 0) &&
		    (ai < a_len) &&
		    (val_array[ai] == val_arg))
			return (1);
		break;
	}
	case DATA_TYPE_HRTIME:
	case DATA_TYPE_NVLIST:
	case DATA_TYPE_NVLIST_ARRAY:
	case DATA_TYPE_BOOLEAN:
	case DATA_TYPE_DOUBLE:
	case DATA_TYPE_UNKNOWN:
	default:
		/*
		 * unknown/unsupported data type
		 */
		return (-1);		/* error fail match */
	}

	/*
	 * check to see if sscanf failed conversion, return approximate
	 * pointer to problem
	 */
	if (sr != 1) {
		if (ep)
			*ep = value;
		return (-1);		/* error fail match  - syntax */
	}

	return (0);			/* fail match */
}

int
nvpair_value_match(nvpair_t *nvp, int ai, char *value, char **ep)
{
	return (nvpair_value_match_regex(nvp, ai, value, NULL, ep));
}
