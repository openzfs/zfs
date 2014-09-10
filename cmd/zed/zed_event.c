/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license from the top-level
 * OPENSOLARIS.LICENSE or <http://opensource.org/licenses/CDDL-1.0>.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each file
 * and include the License file from the top-level OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */

/*
 * Developed at Lawrence Livermore National Laboratory (LLNL-CODE-403049).
 * Copyright (C) 2013-2014 Lawrence Livermore National Security, LLC.
 */

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <libzfs.h>			/* FIXME: Replace with libzfs_core. */
#include <paths.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/zfs_ioctl.h>
#include <time.h>
#include <unistd.h>
#include "zed.h"
#include "zed_conf.h"
#include "zed_exec.h"
#include "zed_file.h"
#include "zed_log.h"
#include "zed_strings.h"

/*
 * Open the libzfs interface.
 */
void
zed_event_init(struct zed_conf *zcp)
{
	if (!zcp)
		zed_log_die("Failed zed_event_init: %s", strerror(EINVAL));

	zcp->zfs_hdl = libzfs_init();
	if (!zcp->zfs_hdl)
		zed_log_die("Failed to initialize libzfs");

	zcp->zevent_fd = open(ZFS_DEV, O_RDWR);
	if (zcp->zevent_fd < 0)
		zed_log_die("Failed to open \"%s\": %s",
		    ZFS_DEV, strerror(errno));
}

/*
 * Close the libzfs interface.
 */
void
zed_event_fini(struct zed_conf *zcp)
{
	if (!zcp)
		zed_log_die("Failed zed_event_fini: %s", strerror(EINVAL));

	if (zcp->zevent_fd >= 0) {
		if (close(zcp->zevent_fd) < 0)
			zed_log_msg(LOG_WARNING, "Failed to close \"%s\": %s",
			    ZFS_DEV, strerror(errno));

		zcp->zevent_fd = -1;
	}
	if (zcp->zfs_hdl) {
		libzfs_fini(zcp->zfs_hdl);
		zcp->zfs_hdl = NULL;
	}
}

/*
 * Seek to the event specified by [saved_eid] and [saved_etime].
 * This protects against processing a given event more than once.
 * Return 0 upon a successful seek to the specified event, or -1 otherwise.
 *
 * A zevent is considered to be uniquely specified by its (eid,time) tuple.
 * The unsigned 64b eid is set to 1 when the kernel module is loaded, and
 * incremented by 1 for each new event.  Since the state file can persist
 * across a kernel module reload, the time must be checked to ensure a match.
 */
int
zed_event_seek(struct zed_conf *zcp, uint64_t saved_eid, int64_t saved_etime[])
{
	uint64_t eid;
	int found;
	nvlist_t *nvl;
	int n_dropped;
	int64_t *etime;
	uint_t nelem;
	int rv;

	if (!zcp) {
		errno = EINVAL;
		zed_log_msg(LOG_ERR, "Failed to seek zevent: %s",
		    strerror(errno));
		return (-1);
	}
	eid = 0;
	found = 0;
	while ((eid < saved_eid) && !found) {
		rv = zpool_events_next(zcp->zfs_hdl, &nvl, &n_dropped,
		    ZEVENT_NONBLOCK, zcp->zevent_fd);

		if ((rv != 0) || !nvl)
			break;

		if (n_dropped > 0) {
			zed_log_msg(LOG_WARNING, "Missed %d events", n_dropped);
			/*
			 * FIXME: Increase max size of event nvlist in
			 *   /sys/module/zfs/parameters/zfs_zevent_len_max ?
			 */
		}
		if (nvlist_lookup_uint64(nvl, "eid", &eid) != 0) {
			zed_log_msg(LOG_WARNING, "Failed to lookup zevent eid");
		} else if (nvlist_lookup_int64_array(nvl, "time",
		    &etime, &nelem) != 0) {
			zed_log_msg(LOG_WARNING,
			    "Failed to lookup zevent time (eid=%llu)", eid);
		} else if (nelem != 2) {
			zed_log_msg(LOG_WARNING,
			    "Failed to lookup zevent time (eid=%llu, nelem=%u)",
			    eid, nelem);
		} else if ((eid != saved_eid) ||
		    (etime[0] != saved_etime[0]) ||
		    (etime[1] != saved_etime[1])) {
			/* no-op */
		} else {
			found = 1;
		}
		free(nvl);
	}
	if (!found && (saved_eid > 0)) {
		if (zpool_events_seek(zcp->zfs_hdl, ZEVENT_SEEK_START,
		    zcp->zevent_fd) < 0)
			zed_log_msg(LOG_WARNING, "Failed to seek to eid=0");
		else
			eid = 0;
	}
	zed_log_msg(LOG_NOTICE, "Processing events since eid=%llu", eid);
	return (found ? 0 : -1);
}

static int
_zed_event_convert_int8_array(char *buf, int buflen, nvpair_t *nvp)
{
	int8_t *i8p;
	uint_t nelem;
	uint_t i;
	char *p;
	int n;

	assert(buf != NULL);

	(void) nvpair_value_int8_array(nvp, &i8p, &nelem);
	for (i = 0, p = buf; (i < nelem) && (buflen > 0); i++) {
		n = snprintf(p, buflen, "%d ", i8p[i]);
		if ((n < 0) || (n >= buflen)) {
			*buf = '\0';
			return (-1);
		}
		p += n;
		buflen -= n;
	}
	if (nelem > 0)
		*--p = '\0';

	return (p - buf);
}

static int
_zed_event_convert_uint8_array(char *buf, int buflen, nvpair_t *nvp)
{
	uint8_t *u8p;
	uint_t nelem;
	uint_t i;
	char *p;
	int n;

	assert(buf != NULL);

	(void) nvpair_value_uint8_array(nvp, &u8p, &nelem);
	for (i = 0, p = buf; (i < nelem) && (buflen > 0); i++) {
		n = snprintf(p, buflen, "%u ", u8p[i]);
		if ((n < 0) || (n >= buflen)) {
			*buf = '\0';
			return (-1);
		}
		p += n;
		buflen -= n;
	}
	if (nelem > 0)
		*--p = '\0';

	return (p - buf);
}

static int
_zed_event_convert_int16_array(char *buf, int buflen, nvpair_t *nvp)
{
	int16_t *i16p;
	uint_t nelem;
	uint_t i;
	char *p;
	int n;

	assert(buf != NULL);

	(void) nvpair_value_int16_array(nvp, &i16p, &nelem);
	for (i = 0, p = buf; (i < nelem) && (buflen > 0); i++) {
		n = snprintf(p, buflen, "%d ", i16p[i]);
		if ((n < 0) || (n >= buflen)) {
			*buf = '\0';
			return (-1);
		}
		p += n;
		buflen -= n;
	}
	if (nelem > 0)
		*--p = '\0';

	return (p - buf);
}

static int
_zed_event_convert_uint16_array(char *buf, int buflen, nvpair_t *nvp)
{
	uint16_t *u16p;
	uint_t nelem;
	uint_t i;
	char *p;
	int n;

	assert(buf != NULL);

	(void) nvpair_value_uint16_array(nvp, &u16p, &nelem);
	for (i = 0, p = buf; (i < nelem) && (buflen > 0); i++) {
		n = snprintf(p, buflen, "%u ", u16p[i]);
		if ((n < 0) || (n >= buflen)) {
			*buf = '\0';
			return (-1);
		}
		p += n;
		buflen -= n;
	}
	if (nelem > 0)
		*--p = '\0';

	return (p - buf);
}

static int
_zed_event_convert_int32_array(char *buf, int buflen, nvpair_t *nvp)
{
	int32_t *i32p;
	uint_t nelem;
	uint_t i;
	char *p;
	int n;

	assert(buf != NULL);

	(void) nvpair_value_int32_array(nvp, &i32p, &nelem);
	for (i = 0, p = buf; (i < nelem) && (buflen > 0); i++) {
		n = snprintf(p, buflen, "%d ", i32p[i]);
		if ((n < 0) || (n >= buflen)) {
			*buf = '\0';
			return (-1);
		}
		p += n;
		buflen -= n;
	}
	if (nelem > 0)
		*--p = '\0';

	return (p - buf);
}

static int
_zed_event_convert_uint32_array(char *buf, int buflen, nvpair_t *nvp)
{
	uint32_t *u32p;
	uint_t nelem;
	uint_t i;
	char *p;
	int n;

	assert(buf != NULL);

	(void) nvpair_value_uint32_array(nvp, &u32p, &nelem);
	for (i = 0, p = buf; (i < nelem) && (buflen > 0); i++) {
		n = snprintf(p, buflen, "%u ", u32p[i]);
		if ((n < 0) || (n >= buflen)) {
			*buf = '\0';
			return (-1);
		}
		p += n;
		buflen -= n;
	}
	if (nelem > 0)
		*--p = '\0';

	return (p - buf);
}

static int
_zed_event_convert_int64_array(char *buf, int buflen, nvpair_t *nvp)
{
	int64_t *i64p;
	uint_t nelem;
	uint_t i;
	char *p;
	int n;

	assert(buf != NULL);

	(void) nvpair_value_int64_array(nvp, &i64p, &nelem);
	for (i = 0, p = buf; (i < nelem) && (buflen > 0); i++) {
		n = snprintf(p, buflen, "%lld ", (u_longlong_t) i64p[i]);
		if ((n < 0) || (n >= buflen)) {
			*buf = '\0';
			return (-1);
		}
		p += n;
		buflen -= n;
	}
	if (nelem > 0)
		*--p = '\0';

	return (p - buf);
}

static int
_zed_event_convert_uint64_array(char *buf, int buflen, nvpair_t *nvp,
    const char *fmt)
{
	uint64_t *u64p;
	uint_t nelem;
	uint_t i;
	char *p;
	int n;

	assert(buf != NULL);

	(void) nvpair_value_uint64_array(nvp, &u64p, &nelem);
	for (i = 0, p = buf; (i < nelem) && (buflen > 0); i++) {
		n = snprintf(p, buflen, fmt, (u_longlong_t) u64p[i]);
		if ((n < 0) || (n >= buflen)) {
			*buf = '\0';
			return (-1);
		}
		p += n;
		buflen -= n;
	}
	if (nelem > 0)
		*--p = '\0';

	return (p - buf);
}

static int
_zed_event_convert_string_array(char *buf, int buflen, nvpair_t *nvp)
{
	char **strp;
	uint_t nelem;
	uint_t i;
	char *p;
	int n;

	assert(buf != NULL);

	(void) nvpair_value_string_array(nvp, &strp, &nelem);
	for (i = 0, p = buf; (i < nelem) && (buflen > 0); i++) {
		n = snprintf(p, buflen, "%s ", strp[i] ? strp[i] : "<NULL>");
		if ((n < 0) || (n >= buflen)) {
			*buf = '\0';
			return (-1);
		}
		p += n;
		buflen -= n;
	}
	if (nelem > 0)
		*--p = '\0';

	return (p - buf);
}

/*
 * Return non-zero if nvpair [name] should be formatted in hex; o/w, return 0.
 */
static int
_zed_event_value_is_hex(const char *name)
{
	const char *hex_suffix[] = {
		"_guid",
		"_guids",
		NULL
	};
	const char **pp;
	char *p;

	if (!name)
		return (0);

	for (pp = hex_suffix; *pp; pp++) {
		p = strstr(name, *pp);
		if (p && strlen(p) == strlen(*pp))
			return (1);
	}
	return (0);
}

/*
 * Convert the nvpair [nvp] to a string which is added to the environment
 * of the child process.
 * Return 0 on success, -1 on error.
 *
 * FIXME: Refactor with cmd/zpool/zpool_main.c:zpool_do_events_nvprint()?
 */
static void
_zed_event_add_nvpair(uint64_t eid, zed_strings_t *zsp, nvpair_t *nvp)
{
	const char *name;
	data_type_t type;
	char buf[4096];
	int buflen;
	int n;
	char *p;
	const char *q;
	const char *fmt;

	boolean_t b;
	double d;
	uint8_t i8;
	uint16_t i16;
	uint32_t i32;
	uint64_t i64;
	char *str;

	assert(zsp != NULL);
	assert(nvp != NULL);

	name = nvpair_name(nvp);
	type = nvpair_type(nvp);
	buflen = sizeof (buf);

	/* Copy NAME prefix for ZED zevent namespace. */
	n = strlcpy(buf, ZEVENT_VAR_PREFIX, sizeof (buf));
	if (n >= sizeof (buf)) {
		zed_log_msg(LOG_WARNING,
		    "Failed to convert nvpair \"%s\" for eid=%llu: %s",
		    name, eid, "Exceeded buffer size");
		return;
	}
	buflen -= n;
	p = buf + n;

	/* Convert NAME to alphanumeric uppercase. */
	for (q = name; *q && (buflen > 0); q++) {
		*p++ = isalnum(*q) ? toupper(*q) : '_';
		buflen--;
	}

	/* Separate NAME from VALUE. */
	if (buflen > 0) {
		*p++ = '=';
		buflen--;
	}
	*p = '\0';

	/* Convert VALUE. */
	switch (type) {
	case DATA_TYPE_BOOLEAN:
		n = snprintf(p, buflen, "%s", "1");
		break;
	case DATA_TYPE_BOOLEAN_VALUE:
		(void) nvpair_value_boolean_value(nvp, &b);
		n = snprintf(p, buflen, "%s", b ? "1" : "0");
		break;
	case DATA_TYPE_BYTE:
		(void) nvpair_value_byte(nvp, &i8);
		n = snprintf(p, buflen, "%d", i8);
		break;
	case DATA_TYPE_INT8:
		(void) nvpair_value_int8(nvp, (int8_t *) &i8);
		n = snprintf(p, buflen, "%d", i8);
		break;
	case DATA_TYPE_UINT8:
		(void) nvpair_value_uint8(nvp, &i8);
		n = snprintf(p, buflen, "%u", i8);
		break;
	case DATA_TYPE_INT16:
		(void) nvpair_value_int16(nvp, (int16_t *) &i16);
		n = snprintf(p, buflen, "%d", i16);
		break;
	case DATA_TYPE_UINT16:
		(void) nvpair_value_uint16(nvp, &i16);
		n = snprintf(p, buflen, "%u", i16);
		break;
	case DATA_TYPE_INT32:
		(void) nvpair_value_int32(nvp, (int32_t *) &i32);
		n = snprintf(p, buflen, "%d", i32);
		break;
	case DATA_TYPE_UINT32:
		(void) nvpair_value_uint32(nvp, &i32);
		n = snprintf(p, buflen, "%u", i32);
		break;
	case DATA_TYPE_INT64:
		(void) nvpair_value_int64(nvp, (int64_t *) &i64);
		n = snprintf(p, buflen, "%lld", (longlong_t) i64);
		break;
	case DATA_TYPE_UINT64:
		(void) nvpair_value_uint64(nvp, &i64);
		fmt = _zed_event_value_is_hex(name) ? "0x%.16llX" : "%llu";
		n = snprintf(p, buflen, fmt, (u_longlong_t) i64);
		break;
	case DATA_TYPE_DOUBLE:
		(void) nvpair_value_double(nvp, &d);
		n = snprintf(p, buflen, "%g", d);
		break;
	case DATA_TYPE_HRTIME:
		(void) nvpair_value_hrtime(nvp, (hrtime_t *) &i64);
		n = snprintf(p, buflen, "%llu", (u_longlong_t) i64);
		break;
	case DATA_TYPE_NVLIST:
		/* FIXME */
		n = snprintf(p, buflen, "%s", "_NOT_IMPLEMENTED_");
		break;
	case DATA_TYPE_STRING:
		(void) nvpair_value_string(nvp, &str);
		n = snprintf(p, buflen, "%s", (str ? str : "<NULL>"));
		break;
	case DATA_TYPE_BOOLEAN_ARRAY:
		/* FIXME */
		n = snprintf(p, buflen, "%s", "_NOT_IMPLEMENTED_");
		break;
	case DATA_TYPE_BYTE_ARRAY:
		/* FIXME */
		n = snprintf(p, buflen, "%s", "_NOT_IMPLEMENTED_");
		break;
	case DATA_TYPE_INT8_ARRAY:
		n = _zed_event_convert_int8_array(p, buflen, nvp);
		break;
	case DATA_TYPE_UINT8_ARRAY:
		n = _zed_event_convert_uint8_array(p, buflen, nvp);
		break;
	case DATA_TYPE_INT16_ARRAY:
		n = _zed_event_convert_int16_array(p, buflen, nvp);
		break;
	case DATA_TYPE_UINT16_ARRAY:
		n = _zed_event_convert_uint16_array(p, buflen, nvp);
		break;
	case DATA_TYPE_INT32_ARRAY:
		n = _zed_event_convert_int32_array(p, buflen, nvp);
		break;
	case DATA_TYPE_UINT32_ARRAY:
		n = _zed_event_convert_uint32_array(p, buflen, nvp);
		break;
	case DATA_TYPE_INT64_ARRAY:
		n = _zed_event_convert_int64_array(p, buflen, nvp);
		break;
	case DATA_TYPE_UINT64_ARRAY:
		fmt = _zed_event_value_is_hex(name) ? "0x%.16llX " : "%llu ";
		n = _zed_event_convert_uint64_array(p, buflen, nvp, fmt);
		break;
	case DATA_TYPE_STRING_ARRAY:
		n = _zed_event_convert_string_array(p, buflen, nvp);
		break;
	case DATA_TYPE_NVLIST_ARRAY:
		/* FIXME */
		n = snprintf(p, buflen, "%s", "_NOT_IMPLEMENTED_");
		break;
	default:
		zed_log_msg(LOG_WARNING,
		    "Failed to convert nvpair \"%s\" for eid=%llu: "
		    "Unrecognized type=%u", name, eid, (unsigned int) type);
		return;
	}
	if ((n < 0) || (n >= sizeof (buf))) {
		zed_log_msg(LOG_WARNING,
		    "Failed to convert nvpair \"%s\" for eid=%llu: %s",
		    name, eid, "Exceeded buffer size");
		return;
	}
	if (zed_strings_add(zsp, buf) < 0) {
		zed_log_msg(LOG_WARNING,
		    "Failed to convert nvpair \"%s\" for eid=%llu: %s",
		    name, eid, strerror(ENOMEM));
		return;
	}
}

/*
 * Add the environment variable specified by the format string [fmt].
 */
static void
_zed_event_add_var(uint64_t eid, zed_strings_t *zsp, const char *fmt, ...)
{
	char buf[4096];
	va_list vargs;
	int n;
	const char *p;
	size_t namelen;

	assert(zsp != NULL);
	assert(fmt != NULL);

	va_start(vargs, fmt);
	n = vsnprintf(buf, sizeof (buf), fmt, vargs);
	va_end(vargs);
	p = strchr(buf, '=');
	namelen = (p) ? p - buf : strlen(buf);

	if ((n < 0) || (n >= sizeof (buf))) {
		zed_log_msg(LOG_WARNING, "Failed to add %.*s for eid=%llu: %s",
		    namelen, buf, eid, "Exceeded buffer size");
	} else if (!p) {
		zed_log_msg(LOG_WARNING, "Failed to add %.*s for eid=%llu: %s",
		    namelen, buf, eid, "Missing assignment");
	} else if (zed_strings_add(zsp, buf) < 0) {
		zed_log_msg(LOG_WARNING, "Failed to add %.*s for eid=%llu: %s",
		    namelen, buf, eid, strerror(ENOMEM));
	}
}

/*
 * Restrict various environment variables to safe and sane values
 * when constructing the environment for the child process.
 *
 * Reference: Secure Programming Cookbook by Viega & Messier, Section 1.1.
 */
static void
_zed_event_add_env_restrict(uint64_t eid, zed_strings_t *zsp)
{
	const char *env_restrict[] = {
		"IFS= \t\n",
		"PATH=" _PATH_STDPATH,
		"ZDB=" SBINDIR "/zdb",
		"ZED=" SBINDIR "/zed",
		"ZFS=" SBINDIR "/zfs",
		"ZINJECT=" SBINDIR "/zinject",
		"ZPOOL=" SBINDIR "/zpool",
		"ZFS_ALIAS=" ZFS_META_ALIAS,
		"ZFS_VERSION=" ZFS_META_VERSION,
		"ZFS_RELEASE=" ZFS_META_RELEASE,
		NULL
	};
	const char **pp;

	assert(zsp != NULL);

	for (pp = env_restrict; *pp; pp++) {
		_zed_event_add_var(eid, zsp, "%s", *pp);
	}
}

/*
 * Preserve specified variables from the parent environment
 * when constructing the environment for the child process.
 *
 * Reference: Secure Programming Cookbook by Viega & Messier, Section 1.1.
 */
static void
_zed_event_add_env_preserve(uint64_t eid, zed_strings_t *zsp)
{
	const char *env_preserve[] = {
		"TZ",
		NULL
	};
	const char **pp;
	const char *p;

	assert(zsp != NULL);

	for (pp = env_preserve; *pp; pp++) {
		if ((p = getenv(*pp)))
			_zed_event_add_var(eid, zsp, "%s=%s", *pp, p);
	}
}

/*
 * Compute the "subclass" by removing the first 3 components of [class]
 * (which seem to always be either "ereport.fs.zfs" or "resource.fs.zfs").
 * Return a pointer inside the string [class], or NULL if insufficient
 * components exist.
 */
static const char *
_zed_event_get_subclass(const char *class)
{
	const char *p;
	int i;

	if (!class)
		return (NULL);

	p = class;
	for (i = 0; i < 3; i++) {
		p = strchr(p, '.');
		if (!p)
			break;
		p++;
	}
	return (p);
}

/*
 * Convert the zevent time from a 2-element array of 64b integers
 * into a more convenient form:
 * - TIME_SECS is the second component of the time.
 * - TIME_NSECS is the nanosecond component of the time.
 * - TIME_STRING is an almost-RFC3339-compliant string representation.
 */
static void
_zed_event_add_time_strings(uint64_t eid, zed_strings_t *zsp, int64_t etime[])
{
	struct tm *stp;
	char buf[32];

	assert(zsp != NULL);
	assert(etime != NULL);

	_zed_event_add_var(eid, zsp, "%s%s=%lld",
	    ZEVENT_VAR_PREFIX, "TIME_SECS", (long long int) etime[0]);
	_zed_event_add_var(eid, zsp, "%s%s=%lld",
	    ZEVENT_VAR_PREFIX, "TIME_NSECS", (long long int) etime[1]);

	if (!(stp = localtime((const time_t *) &etime[0]))) {
		zed_log_msg(LOG_WARNING, "Failed to add %s%s for eid=%llu: %s",
		    ZEVENT_VAR_PREFIX, "TIME_STRING", eid, "localtime error");
	} else if (!strftime(buf, sizeof (buf), "%Y-%m-%d %H:%M:%S%z", stp)) {
		zed_log_msg(LOG_WARNING, "Failed to add %s%s for eid=%llu: %s",
		    ZEVENT_VAR_PREFIX, "TIME_STRING", eid, "strftime error");
	} else {
		_zed_event_add_var(eid, zsp, "%s%s=%s",
		    ZEVENT_VAR_PREFIX, "TIME_STRING", buf);
	}
}

/*
 * Service the next zevent, blocking until one is available.
 */
void
zed_event_service(struct zed_conf *zcp)
{
	nvlist_t *nvl;
	nvpair_t *nvp;
	int n_dropped;
	zed_strings_t *zsp;
	uint64_t eid;
	int64_t *etime;
	uint_t nelem;
	char *class;
	const char *subclass;
	int rv;

	if (!zcp) {
		errno = EINVAL;
		zed_log_msg(LOG_ERR, "Failed to service zevent: %s",
		    strerror(errno));
		return;
	}
	rv = zpool_events_next(zcp->zfs_hdl, &nvl, &n_dropped, ZEVENT_NONE,
	    zcp->zevent_fd);

	if ((rv != 0) || !nvl)
		return;

	if (n_dropped > 0) {
		zed_log_msg(LOG_WARNING, "Missed %d events", n_dropped);
		/*
		 * FIXME: Increase max size of event nvlist in
		 * /sys/module/zfs/parameters/zfs_zevent_len_max ?
		 */
	}
	if (nvlist_lookup_uint64(nvl, "eid", &eid) != 0) {
		zed_log_msg(LOG_WARNING, "Failed to lookup zevent eid");
	} else if (nvlist_lookup_int64_array(
	    nvl, "time", &etime, &nelem) != 0) {
		zed_log_msg(LOG_WARNING,
		    "Failed to lookup zevent time (eid=%llu)", eid);
	} else if (nelem != 2) {
		zed_log_msg(LOG_WARNING,
		    "Failed to lookup zevent time (eid=%llu, nelem=%u)",
		    eid, nelem);
	} else if (nvlist_lookup_string(nvl, "class", &class) != 0) {
		zed_log_msg(LOG_WARNING,
		    "Failed to lookup zevent class (eid=%llu)", eid);
	} else {
		zsp = zed_strings_create();

		nvp = NULL;
		while ((nvp = nvlist_next_nvpair(nvl, nvp)))
			_zed_event_add_nvpair(eid, zsp, nvp);

		_zed_event_add_env_restrict(eid, zsp);
		_zed_event_add_env_preserve(eid, zsp);

		_zed_event_add_var(eid, zsp, "%s%s=%d",
		    ZED_VAR_PREFIX, "PID", (int) getpid());
		_zed_event_add_var(eid, zsp, "%s%s=%s",
		    ZED_VAR_PREFIX, "SCRIPT_DIR", zcp->script_dir);

		subclass = _zed_event_get_subclass(class);
		_zed_event_add_var(eid, zsp, "%s%s=%s",
		    ZEVENT_VAR_PREFIX, "SUBCLASS",
		    (subclass ? subclass : class));
		_zed_event_add_time_strings(eid, zsp, etime);

		zed_exec_process(eid, class, subclass,
		    zcp->script_dir, zcp->scripts, zsp, zcp->zevent_fd);

		zed_conf_write_state(zcp, eid, etime);

		zed_strings_destroy(zsp);
	}
	nvlist_free(nvl);
}
