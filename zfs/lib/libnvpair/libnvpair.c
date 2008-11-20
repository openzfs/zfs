/*
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
/*
 * Copyright 2004 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */



#include <unistd.h>
#include <strings.h>
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
