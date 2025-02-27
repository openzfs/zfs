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
 * Copyright (c) 2014, Joyent, Inc.
 * Copyright (c) 2017 by Delphix. All rights reserved.
 * Copyright (c) 2025, Klara, Inc.
 */

/*
 * General logic for JSON preparation and generation used by both user and
 * kernel lands.
 */

#include <sys/debug.h>
#include <sys/nvpair_impl.h>

#if defined(_KERNEL)
#include <sys/sysmacros.h>
#else
#include <stdio.h>
#include <stdarg.h>
#endif

typedef struct nvjson_context {
	nvjson_t *r; /* request */
	char *p;
	char tmp[32];
} nvjson_context_t;

static int
nvjson_printf(nvjson_context_t *ctx, const char *fmt, ...)
{
	int ret;
	va_list va;

	if (ctx->p == NULL) {
		/* to the temporary buffer */
		va_start(va, fmt);
		ret = vsnprintf(ctx->tmp, sizeof (ctx->tmp), fmt, va);
		va_end(va);
		if (ret < 0)
			return (ENOMEM);
		if (ret >= sizeof (ctx->tmp))
			return (EFBIG);
		if (ctx->r->writer != NULL) {
			ret = ctx->r->writer(ctx->r->writer_ctx, ctx->tmp);
			if (ret != 0)
				return (ret);
		}
	} else {
		/* directly to the provided buffer */
		if (ctx->p - ctx->r->buf >= ctx->r->size)
			return (ENOMEM);
		va_start(va, fmt);
		ret = vsnprintf(ctx->p, ctx->r->size - (ctx->p - ctx->r->buf),
		    fmt, va);
		va_end(va);
		if (ret < 0)
			return (EFBIG);
		if (ret >= (ctx->r->buf + ctx->r->size - ctx->p))
			return (ENOMEM);
		if (ctx->r->writer != NULL) {
			ret = ctx->r->writer(ctx->r->writer_ctx, ctx->p);
			if (ret != 0)
				return (ret);
		}
		ctx->p += ret;
	}

	return (0);
}

#define	PRINTF(ctx, ...)					\
	do {							\
		int ret = nvjson_printf(ctx, __VA_ARGS__);	\
		if (ret != 0)					\
			return (ret);				\
	} while (0)

static int
nvjson_default_writer(void *context, const char *str)
{
	return (nvjson_printf((nvjson_context_t *)context, str));
}

/* Multibyte chars are written as is. */
static int
nvjson_default_str_handler(const char *str, nvjson_writer_t w, void *wctx)
{
#define	W(x)				\
	do {				\
		int ret = w(wctx, (x));	\
		if (ret != 0)		\
			return (ret);	\
	} while (0)

	char tmp[8];
	int c;

	if (str == NULL) {
		W("null");
		return (0);
	}

	W("\"");
	while (*str) {
		switch (*str) {
		case '"':
			W("\\\"");
			break;
		case '\n':
			W("\\n");
			break;
		case '\r':
			W("\\r");
			break;
		case '\\':
			W("\\\\");
			break;
		case '\f':
			W("\\f");
			break;
		case '\t':
			W("\\t");
			break;
		case '\b':
			W("\\b");
			break;
		default:
			c = (int)*str;
			if (c >= 0x00 && c <= 0x1f)
				snprintf(tmp, sizeof (tmp), "\\u%04x", *str);
			else
				snprintf(tmp, sizeof (tmp), "%c", *str);
			W(tmp);
		}
		str++;
	}
	W("\"");

	return (0);
}

static int
nvjson_print_string(nvjson_context_t *ctx, const char *str)
{
	nvjson_writer_t w;
	void *wctx;

	if (ctx->r->writer == NULL) {
		w = nvjson_default_writer;
		wctx = ctx;
	} else {
		w = ctx->r->writer;
		wctx = ctx->r->writer_ctx;
	}

	if (ctx->r->str_handler == NULL)
		return (nvjson_default_str_handler(str, w, wctx));
	else
		return (ctx->r->str_handler(str, w, wctx));
}

#define	PRINT_STRING(ctx, str)					\
	do {							\
		int ret = nvjson_print_string(ctx, str);	\
		if (ret != 0)					\
			return (ret);				\
	} while (0)

static int
nvlist_to_json_impl(nvjson_context_t *ctx, nvlist_t *nvl)
{
	PRINTF(ctx, "{");

	nvpair_t *nv;
	boolean_t first = B_TRUE;
	int ret;

	nv = nvlist_next_nvpair(nvl, NULL);
	for (; nv; nv = nvlist_next_nvpair(nvl, nv)) {
		if (first)
			first = B_FALSE;
		else
			PRINTF(ctx, ",");

		PRINT_STRING(ctx, nvpair_name(nv));
		PRINTF(ctx, ":");

		switch (nvpair_type(nv)) {
		case DATA_TYPE_STRING:
			PRINT_STRING(ctx, fnvpair_value_string(nv));
			break;
		case DATA_TYPE_BOOLEAN:
			PRINTF(ctx, "true");
			break;
		case DATA_TYPE_BOOLEAN_VALUE:
			PRINTF(ctx, fnvpair_value_boolean_value(nv) == B_TRUE
			    ? "true" : "false");
			break;
		case DATA_TYPE_BYTE:
			PRINTF(ctx, "%hhu", fnvpair_value_byte(nv));
			break;
		case DATA_TYPE_INT8:
			PRINTF(ctx, "%hhd", fnvpair_value_int8(nv));
			break;
		case DATA_TYPE_UINT8:
			PRINTF(ctx, "%hhu", fnvpair_value_uint8(nv));
			break;
		case DATA_TYPE_INT16:
			PRINTF(ctx, "%hd", fnvpair_value_int16(nv));
			break;
		case DATA_TYPE_UINT16:
			PRINTF(ctx, "%hu", fnvpair_value_uint16(nv));
			break;
		case DATA_TYPE_INT32:
			PRINTF(ctx, "%d", fnvpair_value_int32(nv));
			break;
		case DATA_TYPE_UINT32:
			PRINTF(ctx, "%u", fnvpair_value_uint32(nv));
			break;
		case DATA_TYPE_INT64:
			PRINTF(ctx, "%lld",
			    (long long)fnvpair_value_int64(nv));
			break;
		case DATA_TYPE_UINT64:
			PRINTF(ctx, "%llu",
			    (unsigned long long)fnvpair_value_uint64(nv));
			break;
		case DATA_TYPE_HRTIME: {
			hrtime_t val;
			VERIFY0(nvpair_value_hrtime(nv, &val));
			PRINTF(ctx, "%llu", (unsigned long long)val);
			break;
		}
#if !defined(_KERNEL) && !defined(_STANDALONE)
		case DATA_TYPE_DOUBLE: {
			double val;
			VERIFY0(nvpair_value_double(nv, &val));
			PRINTF(ctx, "%f", val);
			break;
		}
#endif
		case DATA_TYPE_NVLIST:
			ret = nvlist_to_json_impl(ctx,
			    fnvpair_value_nvlist(nv));
			if (ret != 0)
				return (ret);
			break;
		case DATA_TYPE_STRING_ARRAY: {
			const char **val;
			uint_t valsz, i;
			VERIFY0(nvpair_value_string_array(nv, &val, &valsz));
			PRINTF(ctx, "[");
			for (i = 0; i < valsz; i++) {
				if (i > 0)
					PRINTF(ctx, ",");
				PRINT_STRING(ctx, val[i]);
			}
			PRINTF(ctx, "]");
			break;
		}
		case DATA_TYPE_NVLIST_ARRAY: {
			nvlist_t **val;
			uint_t valsz, i;
			VERIFY0(nvpair_value_nvlist_array(nv, &val, &valsz));
			PRINTF(ctx, "[");
			for (i = 0; i < valsz; i++) {
				if (i > 0)
					PRINTF(ctx, ",");
				ret = nvlist_to_json_impl(ctx, val[i]);
				if (ret != 0)
					return (ret);
			}
			PRINTF(ctx, "]");
			break;
		}
		case DATA_TYPE_BOOLEAN_ARRAY: {
			boolean_t *val;
			uint_t valsz, i;
			VERIFY0(nvpair_value_boolean_array(nv, &val, &valsz));
			PRINTF(ctx, "[");
			for (i = 0; i < valsz; i++) {
				if (i > 0)
					PRINTF(ctx, ",");
				PRINTF(ctx, val[i] == B_TRUE ?
				    "true" : "false");
			}
			PRINTF(ctx, "]");
			break;
		}
		case DATA_TYPE_BYTE_ARRAY: {
			uchar_t *val;
			uint_t valsz, i;
			VERIFY0(nvpair_value_byte_array(nv, &val, &valsz));
			PRINTF(ctx, "[");
			for (i = 0; i < valsz; i++) {
				if (i > 0)
					PRINTF(ctx, ",");
				PRINTF(ctx, "%hhu", val[i]);
			}
			PRINTF(ctx, "]");
			break;
		}
		case DATA_TYPE_UINT8_ARRAY: {
			uint8_t *val;
			uint_t valsz, i;
			VERIFY0(nvpair_value_uint8_array(nv, &val, &valsz));
			PRINTF(ctx, "[");
			for (i = 0; i < valsz; i++) {
				if (i > 0)
					PRINTF(ctx, ",");
				PRINTF(ctx, "%hhu", val[i]);
			}
			PRINTF(ctx, "]");
			break;
		}
		case DATA_TYPE_INT8_ARRAY: {
			int8_t *val;
			uint_t valsz, i;
			VERIFY0(nvpair_value_int8_array(nv, &val, &valsz));
			PRINTF(ctx, "[");
			for (i = 0; i < valsz; i++) {
				if (i > 0)
					PRINTF(ctx, ",");
				PRINTF(ctx, "%hhd", val[i]);
			}
			PRINTF(ctx, "]");
			break;
		}
		case DATA_TYPE_UINT16_ARRAY: {
			uint16_t *val;
			uint_t valsz, i;
			VERIFY0(nvpair_value_uint16_array(nv, &val, &valsz));
			PRINTF(ctx, "[");
			for (i = 0; i < valsz; i++) {
				if (i > 0)
					PRINTF(ctx, ",");
				PRINTF(ctx, "%hu", val[i]);
			}
			PRINTF(ctx, "]");
			break;
		}
		case DATA_TYPE_INT16_ARRAY: {
			int16_t *val;
			uint_t valsz, i;
			VERIFY0(nvpair_value_int16_array(nv, &val, &valsz));
			PRINTF(ctx, "[");
			for (i = 0; i < valsz; i++) {
				if (i > 0)
					PRINTF(ctx, ",");
				PRINTF(ctx, "%hd", val[i]);
			}
			PRINTF(ctx, "]");
			break;
		}
		case DATA_TYPE_UINT32_ARRAY: {
			uint32_t *val;
			uint_t valsz, i;
			VERIFY0(nvpair_value_uint32_array(nv, &val, &valsz));
			PRINTF(ctx, "[");
			for (i = 0; i < valsz; i++) {
				if (i > 0)
					PRINTF(ctx, ",");
				PRINTF(ctx, "%u", val[i]);
			}
			PRINTF(ctx, "]");
			break;
		}
		case DATA_TYPE_INT32_ARRAY: {
			int32_t *val;
			uint_t valsz, i;
			VERIFY0(nvpair_value_int32_array(nv, &val, &valsz));
			PRINTF(ctx, "[");
			for (i = 0; i < valsz; i++) {
				if (i > 0)
					PRINTF(ctx, ",");
				PRINTF(ctx, "%d", val[i]);
			}
			PRINTF(ctx, "]");
			break;
		}
		case DATA_TYPE_UINT64_ARRAY: {
			uint64_t *val;
			uint_t valsz, i;
			VERIFY0(nvpair_value_uint64_array(nv, &val, &valsz));
			PRINTF(ctx, "[");
			for (i = 0; i < valsz; i++) {
				if (i > 0)
					PRINTF(ctx, ",");
				PRINTF(ctx, "%llu",
				    (unsigned long long)val[i]);
			}
			PRINTF(ctx, "]");
			break;
		}
		case DATA_TYPE_INT64_ARRAY: {
			int64_t *val;
			uint_t valsz, i;
			VERIFY0(nvpair_value_int64_array(nv, &val, &valsz));
			PRINTF(ctx, "[");
			for (i = 0; i < valsz; i++) {
				if (i > 0)
					PRINTF(ctx, ",");
				PRINTF(ctx, "%lld", (long long)val[i]);
			}
			PRINTF(ctx, "]");
			break;
		}
		case DATA_TYPE_UNKNOWN:
		case DATA_TYPE_DONTCARE:
			return (EINVAL);
		}
	}

	PRINTF(ctx, "}");

	return (0);
}

int
nvlist_to_json(nvjson_t *nvjson, nvlist_t *nvl)
{
	nvjson_context_t context = { 0 };
	context.r = nvjson;
	context.p = nvjson->buf;

	if (nvjson->buf != NULL && nvjson->size < 1)
		return (EINVAL);
	if (nvjson->buf == NULL && nvjson->writer == NULL)
		return (EINVAL);

	return (nvlist_to_json_impl(&context, nvl));
}
