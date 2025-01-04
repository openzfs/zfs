// SPDX-License-Identifier: BSD-2-Clause
/*
 * Copyright (c) 2010 Pawel Jakub Dawidek <pjd@FreeBSD.org>
 * Copyright (c) 2020 iXsystems, Inc.
 * All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHORS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/types.h>
#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/kmem.h>
#include <sys/list.h>
#include <sys/proc.h>
#include <sys/sbuf.h>
#include <sys/nvpair.h>
#include <sys/sunddi.h>
#include <sys/sysevent.h>
#include <sys/fm/protocol.h>
#include <sys/fm/util.h>
#include <sys/bus.h>

static int
log_sysevent(nvlist_t *event)
{
	struct sbuf *sb;
	const char *type;
	char typestr[128];
	nvpair_t *elem = NULL;

	sb = sbuf_new_auto();
	if (sb == NULL)
		return (ENOMEM);
	type = NULL;

	while ((elem = nvlist_next_nvpair(event, elem)) != NULL) {
		switch (nvpair_type(elem)) {
		case DATA_TYPE_BOOLEAN:
		{
			boolean_t value;

			(void) nvpair_value_boolean_value(elem, &value);
			sbuf_printf(sb, " %s=%s", nvpair_name(elem),
			    value ? "true" : "false");
			break;
		}
		case DATA_TYPE_UINT8:
		{
			uint8_t value;

			(void) nvpair_value_uint8(elem, &value);
			sbuf_printf(sb, " %s=%hhu", nvpair_name(elem), value);
			break;
		}
		case DATA_TYPE_INT32:
		{
			int32_t value;

			(void) nvpair_value_int32(elem, &value);
			sbuf_printf(sb, " %s=%jd", nvpair_name(elem),
			    (intmax_t)value);
			break;
		}
		case DATA_TYPE_UINT32:
		{
			uint32_t value;

			(void) nvpair_value_uint32(elem, &value);
			sbuf_printf(sb, " %s=%ju", nvpair_name(elem),
			    (uintmax_t)value);
			break;
		}
		case DATA_TYPE_INT64:
		{
			int64_t value;

			(void) nvpair_value_int64(elem, &value);
			sbuf_printf(sb, " %s=%jd", nvpair_name(elem),
			    (intmax_t)value);
			break;
		}
		case DATA_TYPE_UINT64:
		{
			uint64_t value;

			(void) nvpair_value_uint64(elem, &value);
			sbuf_printf(sb, " %s=%ju", nvpair_name(elem),
			    (uintmax_t)value);
			break;
		}
		case DATA_TYPE_STRING:
		{
			const char *value;

			(void) nvpair_value_string(elem, &value);
			sbuf_printf(sb, " %s=%s", nvpair_name(elem), value);
			if (strcmp(FM_CLASS, nvpair_name(elem)) == 0)
				type = value;
			break;
		}
		case DATA_TYPE_UINT8_ARRAY:
		{
			uint8_t *value;
			uint_t ii, nelem;

			(void) nvpair_value_uint8_array(elem, &value, &nelem);
			sbuf_printf(sb, " %s=", nvpair_name(elem));
			for (ii = 0; ii < nelem; ii++)
				sbuf_printf(sb, "%02hhx", value[ii]);
			break;
		}
		case DATA_TYPE_UINT16_ARRAY:
		{
			uint16_t *value;
			uint_t ii, nelem;

			(void) nvpair_value_uint16_array(elem, &value, &nelem);
			sbuf_printf(sb, " %s=", nvpair_name(elem));
			for (ii = 0; ii < nelem; ii++)
				sbuf_printf(sb, "%04hx", value[ii]);
			break;
		}
		case DATA_TYPE_UINT32_ARRAY:
		{
			uint32_t *value;
			uint_t ii, nelem;

			(void) nvpair_value_uint32_array(elem, &value, &nelem);
			sbuf_printf(sb, " %s=", nvpair_name(elem));
			for (ii = 0; ii < nelem; ii++)
				sbuf_printf(sb, "%08jx", (uintmax_t)value[ii]);
			break;
		}
		case DATA_TYPE_INT64_ARRAY:
		{
			int64_t *value;
			uint_t ii, nelem;

			(void) nvpair_value_int64_array(elem, &value, &nelem);
			sbuf_printf(sb, " %s=", nvpair_name(elem));
			for (ii = 0; ii < nelem; ii++)
				sbuf_printf(sb, "%016lld",
				    (long long)value[ii]);
			break;
		}
		case DATA_TYPE_UINT64_ARRAY:
		{
			uint64_t *value;
			uint_t ii, nelem;

			(void) nvpair_value_uint64_array(elem, &value, &nelem);
			sbuf_printf(sb, " %s=", nvpair_name(elem));
			for (ii = 0; ii < nelem; ii++)
				sbuf_printf(sb, "%016jx", (uintmax_t)value[ii]);
			break;
		}
		case DATA_TYPE_STRING_ARRAY:
		{
			const char **strarr;
			uint_t ii, nelem;

			(void) nvpair_value_string_array(elem, &strarr, &nelem);

			for (ii = 0; ii < nelem; ii++) {
				if (strarr[ii] == NULL)  {
					sbuf_printf(sb, " <NULL>");
					continue;
				}

				sbuf_printf(sb, " %s", strarr[ii]);
				if (strcmp(FM_CLASS, strarr[ii]) == 0)
					type = strarr[ii];
			}
			break;
		}
		case DATA_TYPE_NVLIST:
			/* XXX - requires recursing in log_sysevent */
			break;
		default:
			printf("%s: type %d is not implemented\n", __func__,
			    nvpair_type(elem));
			break;
		}
	}

	if (sbuf_finish(sb) != 0) {
		sbuf_delete(sb);
		return (ENOMEM);
	}

	if (type == NULL)
		type = "";
	if (strncmp(type, "ESC_ZFS_", 8) == 0) {
		snprintf(typestr, sizeof (typestr), "misc.fs.zfs.%s", type + 8);
		type = typestr;
	}
	devctl_notify("ZFS", "ZFS", type, sbuf_data(sb));
	sbuf_delete(sb);

	return (0);
}

static void
sysevent_worker(void *arg __unused)
{
	zfs_zevent_t *ze;
	nvlist_t *event;
	uint64_t dropped = 0;
	uint64_t dst_size;
	int error;

	zfs_zevent_init(&ze);
	for (;;) {
		dst_size = 131072;
		dropped = 0;
		event = NULL;
		error = zfs_zevent_next(ze, &event,
		    &dst_size, &dropped);
		if (error) {
			error = zfs_zevent_wait(ze);
			if (error == ESHUTDOWN)
				break;
		} else {
			VERIFY3P(event, !=, NULL);
			log_sysevent(event);
			nvlist_free(event);
		}
	}

	/*
	 * We avoid zfs_zevent_destroy() here because we're otherwise racing
	 * against fm_fini() destroying the zevent_lock.  zfs_zevent_destroy()
	 * will currently only clear `ze->ze_zevent` from an event list then
	 * free `ze`, so just inline the free() here -- events have already
	 * been drained.
	 */
	VERIFY3P(ze->ze_zevent, ==, NULL);
	kmem_free(ze, sizeof (zfs_zevent_t));

	kthread_exit();
}

void
ddi_sysevent_init(void)
{
	kproc_kthread_add(sysevent_worker, NULL, &system_proc, NULL, 0, 0,
	    "zfskern", "sysevent");
}
