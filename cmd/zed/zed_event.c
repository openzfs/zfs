/*
 * This file is part of the ZFS Event Daemon (ZED).
 *
 * Developed at Lawrence Livermore National Laboratory (LLNL-CODE-403049).
 * Copyright (C) 2013-2014 Lawrence Livermore National Security, LLC.
 * Refer to the OpenZFS git commit log for authoritative copyright attribution.
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License Version 1.0 (CDDL-1.0).
 * You can obtain a copy of the license from the top-level file
 * "OPENSOLARIS.LICENSE" or at <http://opensource.org/licenses/CDDL-1.0>.
 * You may not use this file except in compliance with the license.
 */

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <libzfs_core.h>
#include <paths.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/zfs_ioctl.h>
#include <time.h>
#include <unistd.h>
#include <sys/fm/fs/zfs.h>
#include "zed.h"
#include "zed_conf.h"
#include "zed_disk_event.h"
#include "zed_event.h"
#include "zed_exec.h"
#include "zed_file.h"
#include "zed_log.h"
#include "zed_strings.h"

#include "agents/zfs_agents.h"
#include <libzutil.h>

#define	MAXBUF	4096

static int max_zevent_buf_len = 1 << 20;

/*
 * Open the libzfs interface.
 */
int
zed_event_init(struct zed_conf *zcp)
{
	if (!zcp)
		zed_log_die("Failed zed_event_init: %s", strerror(EINVAL));

	zcp->zfs_hdl = libzfs_init();
	if (!zcp->zfs_hdl) {
		if (zcp->do_idle)
			return (-1);
		zed_log_die("Failed to initialize libzfs");
	}

	zcp->zevent_fd = open(ZFS_DEV, O_RDWR | O_CLOEXEC);
	if (zcp->zevent_fd < 0) {
		if (zcp->do_idle)
			return (-1);
		zed_log_die("Failed to open \"%s\": %s",
		    ZFS_DEV, strerror(errno));
	}

	zfs_agent_init(zcp->zfs_hdl);

	if (zed_disk_event_init() != 0) {
		if (zcp->do_idle)
			return (-1);
		zed_log_die("Failed to initialize disk events");
	}

	if (zcp->max_zevent_buf_len != 0)
		max_zevent_buf_len = zcp->max_zevent_buf_len;

	return (0);
}

/*
 * Close the libzfs interface.
 */
void
zed_event_fini(struct zed_conf *zcp)
{
	if (!zcp)
		zed_log_die("Failed zed_event_fini: %s", strerror(EINVAL));

	zed_disk_event_fini();
	zfs_agent_fini();

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

	zed_exec_fini();
}

static void
_bump_event_queue_length(void)
{
	int zzlm = -1, wr;
	char qlen_buf[12] = {0}; /* parameter is int => max "-2147483647\n" */
	long int qlen, orig_qlen;

	zzlm = open("/sys/module/zfs/parameters/zfs_zevent_len_max", O_RDWR);
	if (zzlm < 0)
		goto done;

	if (read(zzlm, qlen_buf, sizeof (qlen_buf)) < 0)
		goto done;
	qlen_buf[sizeof (qlen_buf) - 1] = '\0';

	errno = 0;
	orig_qlen = qlen = strtol(qlen_buf, NULL, 10);
	if (errno == ERANGE)
		goto done;

	if (qlen <= 0)
		qlen = 512; /* default zfs_zevent_len_max value */
	else
		qlen *= 2;

	/*
	 * Don't consume all of kernel memory with event logs if something
	 * goes wrong.
	 */
	if (qlen > max_zevent_buf_len)
		qlen = max_zevent_buf_len;
	if (qlen == orig_qlen)
		goto done;
	wr = snprintf(qlen_buf, sizeof (qlen_buf), "%ld", qlen);
	if (wr >= sizeof (qlen_buf)) {
		wr = sizeof (qlen_buf) - 1;
		zed_log_msg(LOG_WARNING, "Truncation in %s()", __func__);
	}

	if (pwrite(zzlm, qlen_buf, wr + 1, 0) < 0)
		goto done;

	zed_log_msg(LOG_WARNING, "Bumping queue length to %ld", qlen);

done:
	if (zzlm > -1)
		(void) close(zzlm);
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
			_bump_event_queue_length();
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
 * Add an environment variable for [eid] to the container [zsp].
 *
 * The variable name is the concatenation of [prefix] and [name] converted to
 * uppercase with non-alphanumeric characters converted to underscores;
 * [prefix] is optional, and [name] must begin with an alphabetic character.
 * If the converted variable name already exists within the container [zsp],
 * its existing value will be replaced with the new value.
 *
 * The variable value is specified by the format string [fmt].
 *
 * Returns 0 on success, and -1 on error (with errno set).
 *
 * All environment variables in [zsp] should be added through this function.
 */
static __attribute__((format(printf, 5, 6))) int
_zed_event_add_var(uint64_t eid, zed_strings_t *zsp,
    const char *prefix, const char *name, const char *fmt, ...)
{
	char keybuf[MAXBUF];
	char valbuf[MAXBUF];
	char *dstp;
	const char *srcp;
	const char *lastp;
	int n;
	int buflen;
	va_list vargs;

	assert(zsp != NULL);
	assert(fmt != NULL);

	if (!name) {
		errno = EINVAL;
		zed_log_msg(LOG_WARNING,
		    "Failed to add variable for eid=%llu: Name is empty", eid);
		return (-1);
	} else if (!isalpha(name[0])) {
		errno = EINVAL;
		zed_log_msg(LOG_WARNING,
		    "Failed to add variable for eid=%llu: "
		    "Name \"%s\" is invalid", eid, name);
		return (-1);
	}
	/*
	 * Construct the string key by converting PREFIX (if present) and NAME.
	 */
	dstp = keybuf;
	lastp = keybuf + sizeof (keybuf);
	if (prefix) {
		for (srcp = prefix; *srcp && (dstp < lastp); srcp++)
			*dstp++ = isalnum(*srcp) ? toupper(*srcp) : '_';
	}
	for (srcp = name; *srcp && (dstp < lastp); srcp++)
		*dstp++ = isalnum(*srcp) ? toupper(*srcp) : '_';

	if (dstp == lastp) {
		errno = ENAMETOOLONG;
		zed_log_msg(LOG_WARNING,
		    "Failed to add variable for eid=%llu: Name too long", eid);
		return (-1);
	}
	*dstp = '\0';
	/*
	 * Construct the string specified by "[PREFIX][NAME]=[FMT]".
	 */
	dstp = valbuf;
	buflen = sizeof (valbuf);
	n = strlcpy(dstp, keybuf, buflen);
	if (n >= sizeof (valbuf)) {
		errno = EMSGSIZE;
		zed_log_msg(LOG_WARNING, "Failed to add %s for eid=%llu: %s",
		    keybuf, eid, "Exceeded buffer size");
		return (-1);
	}
	dstp += n;
	buflen -= n;

	*dstp++ = '=';
	buflen--;

	if (buflen <= 0) {
		errno = EMSGSIZE;
		zed_log_msg(LOG_WARNING, "Failed to add %s for eid=%llu: %s",
		    keybuf, eid, "Exceeded buffer size");
		return (-1);
	}

	va_start(vargs, fmt);
	n = vsnprintf(dstp, buflen, fmt, vargs);
	va_end(vargs);

	if ((n < 0) || (n >= buflen)) {
		errno = EMSGSIZE;
		zed_log_msg(LOG_WARNING, "Failed to add %s for eid=%llu: %s",
		    keybuf, eid, "Exceeded buffer size");
		return (-1);
	} else if (zed_strings_add(zsp, keybuf, valbuf) < 0) {
		zed_log_msg(LOG_WARNING, "Failed to add %s for eid=%llu: %s",
		    keybuf, eid, strerror(errno));
		return (-1);
	}
	return (0);
}

static int
_zed_event_add_array_err(uint64_t eid, const char *name)
{
	errno = EMSGSIZE;
	zed_log_msg(LOG_WARNING,
	    "Failed to convert nvpair \"%s\" for eid=%llu: "
	    "Exceeded buffer size", name, eid);
	return (-1);
}

static int
_zed_event_add_int8_array(uint64_t eid, zed_strings_t *zsp,
    const char *prefix, nvpair_t *nvp)
{
	char buf[MAXBUF];
	int buflen = sizeof (buf);
	const char *name;
	int8_t *i8p;
	uint_t nelem;
	uint_t i;
	char *p;
	int n;

	assert((nvp != NULL) && (nvpair_type(nvp) == DATA_TYPE_INT8_ARRAY));

	name = nvpair_name(nvp);
	(void) nvpair_value_int8_array(nvp, &i8p, &nelem);
	for (i = 0, p = buf; (i < nelem) && (buflen > 0); i++) {
		n = snprintf(p, buflen, "%d ", i8p[i]);
		if ((n < 0) || (n >= buflen))
			return (_zed_event_add_array_err(eid, name));
		p += n;
		buflen -= n;
	}
	if (nelem > 0)
		*--p = '\0';

	return (_zed_event_add_var(eid, zsp, prefix, name, "%s", buf));
}

static int
_zed_event_add_uint8_array(uint64_t eid, zed_strings_t *zsp,
    const char *prefix, nvpair_t *nvp)
{
	char buf[MAXBUF];
	int buflen = sizeof (buf);
	const char *name;
	uint8_t *u8p;
	uint_t nelem;
	uint_t i;
	char *p;
	int n;

	assert((nvp != NULL) && (nvpair_type(nvp) == DATA_TYPE_UINT8_ARRAY));

	name = nvpair_name(nvp);
	(void) nvpair_value_uint8_array(nvp, &u8p, &nelem);
	for (i = 0, p = buf; (i < nelem) && (buflen > 0); i++) {
		n = snprintf(p, buflen, "%u ", u8p[i]);
		if ((n < 0) || (n >= buflen))
			return (_zed_event_add_array_err(eid, name));
		p += n;
		buflen -= n;
	}
	if (nelem > 0)
		*--p = '\0';

	return (_zed_event_add_var(eid, zsp, prefix, name, "%s", buf));
}

static int
_zed_event_add_int16_array(uint64_t eid, zed_strings_t *zsp,
    const char *prefix, nvpair_t *nvp)
{
	char buf[MAXBUF];
	int buflen = sizeof (buf);
	const char *name;
	int16_t *i16p;
	uint_t nelem;
	uint_t i;
	char *p;
	int n;

	assert((nvp != NULL) && (nvpair_type(nvp) == DATA_TYPE_INT16_ARRAY));

	name = nvpair_name(nvp);
	(void) nvpair_value_int16_array(nvp, &i16p, &nelem);
	for (i = 0, p = buf; (i < nelem) && (buflen > 0); i++) {
		n = snprintf(p, buflen, "%d ", i16p[i]);
		if ((n < 0) || (n >= buflen))
			return (_zed_event_add_array_err(eid, name));
		p += n;
		buflen -= n;
	}
	if (nelem > 0)
		*--p = '\0';

	return (_zed_event_add_var(eid, zsp, prefix, name, "%s", buf));
}

static int
_zed_event_add_uint16_array(uint64_t eid, zed_strings_t *zsp,
    const char *prefix, nvpair_t *nvp)
{
	char buf[MAXBUF];
	int buflen = sizeof (buf);
	const char *name;
	uint16_t *u16p;
	uint_t nelem;
	uint_t i;
	char *p;
	int n;

	assert((nvp != NULL) && (nvpair_type(nvp) == DATA_TYPE_UINT16_ARRAY));

	name = nvpair_name(nvp);
	(void) nvpair_value_uint16_array(nvp, &u16p, &nelem);
	for (i = 0, p = buf; (i < nelem) && (buflen > 0); i++) {
		n = snprintf(p, buflen, "%u ", u16p[i]);
		if ((n < 0) || (n >= buflen))
			return (_zed_event_add_array_err(eid, name));
		p += n;
		buflen -= n;
	}
	if (nelem > 0)
		*--p = '\0';

	return (_zed_event_add_var(eid, zsp, prefix, name, "%s", buf));
}

static int
_zed_event_add_int32_array(uint64_t eid, zed_strings_t *zsp,
    const char *prefix, nvpair_t *nvp)
{
	char buf[MAXBUF];
	int buflen = sizeof (buf);
	const char *name;
	int32_t *i32p;
	uint_t nelem;
	uint_t i;
	char *p;
	int n;

	assert((nvp != NULL) && (nvpair_type(nvp) == DATA_TYPE_INT32_ARRAY));

	name = nvpair_name(nvp);
	(void) nvpair_value_int32_array(nvp, &i32p, &nelem);
	for (i = 0, p = buf; (i < nelem) && (buflen > 0); i++) {
		n = snprintf(p, buflen, "%d ", i32p[i]);
		if ((n < 0) || (n >= buflen))
			return (_zed_event_add_array_err(eid, name));
		p += n;
		buflen -= n;
	}
	if (nelem > 0)
		*--p = '\0';

	return (_zed_event_add_var(eid, zsp, prefix, name, "%s", buf));
}

static int
_zed_event_add_uint32_array(uint64_t eid, zed_strings_t *zsp,
    const char *prefix, nvpair_t *nvp)
{
	char buf[MAXBUF];
	int buflen = sizeof (buf);
	const char *name;
	uint32_t *u32p;
	uint_t nelem;
	uint_t i;
	char *p;
	int n;

	assert((nvp != NULL) && (nvpair_type(nvp) == DATA_TYPE_UINT32_ARRAY));

	name = nvpair_name(nvp);
	(void) nvpair_value_uint32_array(nvp, &u32p, &nelem);
	for (i = 0, p = buf; (i < nelem) && (buflen > 0); i++) {
		n = snprintf(p, buflen, "%u ", u32p[i]);
		if ((n < 0) || (n >= buflen))
			return (_zed_event_add_array_err(eid, name));
		p += n;
		buflen -= n;
	}
	if (nelem > 0)
		*--p = '\0';

	return (_zed_event_add_var(eid, zsp, prefix, name, "%s", buf));
}

static int
_zed_event_add_int64_array(uint64_t eid, zed_strings_t *zsp,
    const char *prefix, nvpair_t *nvp)
{
	char buf[MAXBUF];
	int buflen = sizeof (buf);
	const char *name;
	int64_t *i64p;
	uint_t nelem;
	uint_t i;
	char *p;
	int n;

	assert((nvp != NULL) && (nvpair_type(nvp) == DATA_TYPE_INT64_ARRAY));

	name = nvpair_name(nvp);
	(void) nvpair_value_int64_array(nvp, &i64p, &nelem);
	for (i = 0, p = buf; (i < nelem) && (buflen > 0); i++) {
		n = snprintf(p, buflen, "%lld ", (u_longlong_t)i64p[i]);
		if ((n < 0) || (n >= buflen))
			return (_zed_event_add_array_err(eid, name));
		p += n;
		buflen -= n;
	}
	if (nelem > 0)
		*--p = '\0';

	return (_zed_event_add_var(eid, zsp, prefix, name, "%s", buf));
}

static int
_zed_event_add_uint64_array(uint64_t eid, zed_strings_t *zsp,
    const char *prefix, nvpair_t *nvp)
{
	char buf[MAXBUF];
	int buflen = sizeof (buf);
	const char *name;
	const char *fmt;
	uint64_t *u64p;
	uint_t nelem;
	uint_t i;
	char *p;
	int n;

	assert((nvp != NULL) && (nvpair_type(nvp) == DATA_TYPE_UINT64_ARRAY));

	name = nvpair_name(nvp);
	fmt = _zed_event_value_is_hex(name) ? "0x%.16llX " : "%llu ";
	(void) nvpair_value_uint64_array(nvp, &u64p, &nelem);
	for (i = 0, p = buf; (i < nelem) && (buflen > 0); i++) {
		n = snprintf(p, buflen, fmt, (u_longlong_t)u64p[i]);
		if ((n < 0) || (n >= buflen))
			return (_zed_event_add_array_err(eid, name));
		p += n;
		buflen -= n;
	}
	if (nelem > 0)
		*--p = '\0';

	return (_zed_event_add_var(eid, zsp, prefix, name, "%s", buf));
}

static int
_zed_event_add_string_array(uint64_t eid, zed_strings_t *zsp,
    const char *prefix, nvpair_t *nvp)
{
	char buf[MAXBUF];
	int buflen = sizeof (buf);
	const char *name;
	const char **strp;
	uint_t nelem;
	uint_t i;
	char *p;
	int n;

	assert((nvp != NULL) && (nvpair_type(nvp) == DATA_TYPE_STRING_ARRAY));

	name = nvpair_name(nvp);
	(void) nvpair_value_string_array(nvp, &strp, &nelem);
	for (i = 0, p = buf; (i < nelem) && (buflen > 0); i++) {
		n = snprintf(p, buflen, "%s ", strp[i] ? strp[i] : "<NULL>");
		if ((n < 0) || (n >= buflen))
			return (_zed_event_add_array_err(eid, name));
		p += n;
		buflen -= n;
	}
	if (nelem > 0)
		*--p = '\0';

	return (_zed_event_add_var(eid, zsp, prefix, name, "%s", buf));
}

/*
 * Convert the nvpair [nvp] to a string which is added to the environment
 * of the child process.
 * Return 0 on success, -1 on error.
 */
static void
_zed_event_add_nvpair(uint64_t eid, zed_strings_t *zsp, nvpair_t *nvp)
{
	const char *name;
	data_type_t type;
	const char *prefix = ZEVENT_VAR_PREFIX;
	boolean_t b;
	double d;
	uint8_t i8;
	uint16_t i16;
	uint32_t i32;
	uint64_t i64;
	const char *str;

	assert(zsp != NULL);
	assert(nvp != NULL);

	name = nvpair_name(nvp);
	type = nvpair_type(nvp);

	switch (type) {
	case DATA_TYPE_BOOLEAN:
		_zed_event_add_var(eid, zsp, prefix, name, "%s", "1");
		break;
	case DATA_TYPE_BOOLEAN_VALUE:
		(void) nvpair_value_boolean_value(nvp, &b);
		_zed_event_add_var(eid, zsp, prefix, name, "%s", b ? "1" : "0");
		break;
	case DATA_TYPE_BYTE:
		(void) nvpair_value_byte(nvp, &i8);
		_zed_event_add_var(eid, zsp, prefix, name, "%d", i8);
		break;
	case DATA_TYPE_INT8:
		(void) nvpair_value_int8(nvp, (int8_t *)&i8);
		_zed_event_add_var(eid, zsp, prefix, name, "%d", i8);
		break;
	case DATA_TYPE_UINT8:
		(void) nvpair_value_uint8(nvp, &i8);
		_zed_event_add_var(eid, zsp, prefix, name, "%u", i8);
		break;
	case DATA_TYPE_INT16:
		(void) nvpair_value_int16(nvp, (int16_t *)&i16);
		_zed_event_add_var(eid, zsp, prefix, name, "%d", i16);
		break;
	case DATA_TYPE_UINT16:
		(void) nvpair_value_uint16(nvp, &i16);
		_zed_event_add_var(eid, zsp, prefix, name, "%u", i16);
		break;
	case DATA_TYPE_INT32:
		(void) nvpair_value_int32(nvp, (int32_t *)&i32);
		_zed_event_add_var(eid, zsp, prefix, name, "%d", i32);
		break;
	case DATA_TYPE_UINT32:
		(void) nvpair_value_uint32(nvp, &i32);
		_zed_event_add_var(eid, zsp, prefix, name, "%u", i32);
		break;
	case DATA_TYPE_INT64:
		(void) nvpair_value_int64(nvp, (int64_t *)&i64);
		_zed_event_add_var(eid, zsp, prefix, name,
		    "%lld", (longlong_t)i64);
		break;
	case DATA_TYPE_UINT64:
		(void) nvpair_value_uint64(nvp, &i64);
		_zed_event_add_var(eid, zsp, prefix, name,
		    (_zed_event_value_is_hex(name) ? "0x%.16llX" : "%llu"),
		    (u_longlong_t)i64);
		/*
		 * shadow readable strings for vdev state pairs
		 */
		if (strcmp(name, FM_EREPORT_PAYLOAD_ZFS_VDEV_STATE) == 0 ||
		    strcmp(name, FM_EREPORT_PAYLOAD_ZFS_VDEV_LASTSTATE) == 0) {
			char alt[32];

			(void) snprintf(alt, sizeof (alt), "%s_str", name);
			_zed_event_add_var(eid, zsp, prefix, alt, "%s",
			    zpool_state_to_name(i64, VDEV_AUX_NONE));
		} else
		/*
		 * shadow readable strings for pool state
		 */
		if (strcmp(name, FM_EREPORT_PAYLOAD_ZFS_POOL_STATE) == 0) {
			char alt[32];

			(void) snprintf(alt, sizeof (alt), "%s_str", name);
			_zed_event_add_var(eid, zsp, prefix, alt, "%s",
			    zpool_pool_state_to_name(i64));
		}
		break;
	case DATA_TYPE_DOUBLE:
		(void) nvpair_value_double(nvp, &d);
		_zed_event_add_var(eid, zsp, prefix, name, "%g", d);
		break;
	case DATA_TYPE_HRTIME:
		(void) nvpair_value_hrtime(nvp, (hrtime_t *)&i64);
		_zed_event_add_var(eid, zsp, prefix, name,
		    "%llu", (u_longlong_t)i64);
		break;
	case DATA_TYPE_STRING:
		(void) nvpair_value_string(nvp, &str);
		_zed_event_add_var(eid, zsp, prefix, name,
		    "%s", (str ? str : "<NULL>"));
		break;
	case DATA_TYPE_INT8_ARRAY:
		_zed_event_add_int8_array(eid, zsp, prefix, nvp);
		break;
	case DATA_TYPE_UINT8_ARRAY:
		_zed_event_add_uint8_array(eid, zsp, prefix, nvp);
		break;
	case DATA_TYPE_INT16_ARRAY:
		_zed_event_add_int16_array(eid, zsp, prefix, nvp);
		break;
	case DATA_TYPE_UINT16_ARRAY:
		_zed_event_add_uint16_array(eid, zsp, prefix, nvp);
		break;
	case DATA_TYPE_INT32_ARRAY:
		_zed_event_add_int32_array(eid, zsp, prefix, nvp);
		break;
	case DATA_TYPE_UINT32_ARRAY:
		_zed_event_add_uint32_array(eid, zsp, prefix, nvp);
		break;
	case DATA_TYPE_INT64_ARRAY:
		_zed_event_add_int64_array(eid, zsp, prefix, nvp);
		break;
	case DATA_TYPE_UINT64_ARRAY:
		_zed_event_add_uint64_array(eid, zsp, prefix, nvp);
		break;
	case DATA_TYPE_STRING_ARRAY:
		_zed_event_add_string_array(eid, zsp, prefix, nvp);
		break;
	case DATA_TYPE_NVLIST:
	case DATA_TYPE_BOOLEAN_ARRAY:
	case DATA_TYPE_BYTE_ARRAY:
	case DATA_TYPE_NVLIST_ARRAY:
		_zed_event_add_var(eid, zsp, prefix, name, "_NOT_IMPLEMENTED_");
		break;
	default:
		errno = EINVAL;
		zed_log_msg(LOG_WARNING,
		    "Failed to convert nvpair \"%s\" for eid=%llu: "
		    "Unrecognized type=%u", name, eid, (unsigned int) type);
		break;
	}
}

/*
 * Restrict various environment variables to safe and sane values
 * when constructing the environment for the child process, unless
 * we're running with a custom $PATH (like under the ZFS test suite).
 *
 * Reference: Secure Programming Cookbook by Viega & Messier, Section 1.1.
 */
static void
_zed_event_add_env_restrict(uint64_t eid, zed_strings_t *zsp,
    const char *path)
{
	const char *env_restrict[][2] = {
		{ "IFS",		" \t\n" },
		{ "PATH",		_PATH_STDPATH },
		{ "ZDB",		SBINDIR "/zdb" },
		{ "ZED",		SBINDIR "/zed" },
		{ "ZFS",		SBINDIR "/zfs" },
		{ "ZINJECT",		SBINDIR "/zinject" },
		{ "ZPOOL",		SBINDIR "/zpool" },
		{ "ZFS_ALIAS",		ZFS_META_ALIAS },
		{ "ZFS_VERSION",	ZFS_META_VERSION },
		{ "ZFS_RELEASE",	ZFS_META_RELEASE },
		{ NULL,			NULL }
	};

	/*
	 * If we have a custom $PATH, use the default ZFS binary locations
	 * instead of the hard-coded ones.
	 */
	const char *env_path[][2] = {
		{ "IFS",		" \t\n" },
		{ "PATH",		NULL }, /* $PATH copied in later on */
		{ "ZDB",		"zdb" },
		{ "ZED",		"zed" },
		{ "ZFS",		"zfs" },
		{ "ZINJECT",		"zinject" },
		{ "ZPOOL",		"zpool" },
		{ "ZFS_ALIAS",		ZFS_META_ALIAS },
		{ "ZFS_VERSION",	ZFS_META_VERSION },
		{ "ZFS_RELEASE",	ZFS_META_RELEASE },
		{ NULL,			NULL }
	};
	const char *(*pa)[2];

	assert(zsp != NULL);

	pa = path != NULL ? env_path : env_restrict;

	for (; *(*pa); pa++) {
		/* Use our custom $PATH if we have one */
		if (path != NULL && strcmp((*pa)[0], "PATH") == 0)
			(*pa)[1] = path;

		_zed_event_add_var(eid, zsp, NULL, (*pa)[0], "%s", (*pa)[1]);
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
	const char **keyp;
	const char *val;

	assert(zsp != NULL);

	for (keyp = env_preserve; *keyp; keyp++) {
		if ((val = getenv(*keyp)))
			_zed_event_add_var(eid, zsp, NULL, *keyp, "%s", val);
	}
}

/*
 * Compute the "subclass" by removing the first 3 components of [class]
 * (which will always be of the form "*.fs.zfs").  Return a pointer inside
 * the string [class], or NULL if insufficient components exist.
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
	struct tm stp;
	char buf[32];

	assert(zsp != NULL);
	assert(etime != NULL);

	_zed_event_add_var(eid, zsp, ZEVENT_VAR_PREFIX, "TIME_SECS",
	    "%" PRId64, etime[0]);
	_zed_event_add_var(eid, zsp, ZEVENT_VAR_PREFIX, "TIME_NSECS",
	    "%" PRId64, etime[1]);

	if (!localtime_r((const time_t *) &etime[0], &stp)) {
		zed_log_msg(LOG_WARNING, "Failed to add %s%s for eid=%llu: %s",
		    ZEVENT_VAR_PREFIX, "TIME_STRING", eid, "localtime error");
	} else if (!strftime(buf, sizeof (buf), "%Y-%m-%d %H:%M:%S%z", &stp)) {
		zed_log_msg(LOG_WARNING, "Failed to add %s%s for eid=%llu: %s",
		    ZEVENT_VAR_PREFIX, "TIME_STRING", eid, "strftime error");
	} else {
		_zed_event_add_var(eid, zsp, ZEVENT_VAR_PREFIX, "TIME_STRING",
		    "%s", buf);
	}
}


static void
_zed_event_update_enc_sysfs_path(nvlist_t *nvl)
{
	const char *vdev_path;

	if (nvlist_lookup_string(nvl, FM_EREPORT_PAYLOAD_ZFS_VDEV_PATH,
	    &vdev_path) != 0) {
		return; /* some other kind of event, ignore it */
	}

	if (vdev_path == NULL) {
		return;
	}

	update_vdev_config_dev_sysfs_path(nvl, vdev_path,
	    FM_EREPORT_PAYLOAD_ZFS_VDEV_ENC_SYSFS_PATH);
}

/*
 * Service the next zevent, blocking until one is available.
 */
int
zed_event_service(struct zed_conf *zcp)
{
	nvlist_t *nvl;
	nvpair_t *nvp;
	int n_dropped;
	zed_strings_t *zsp;
	uint64_t eid;
	int64_t *etime;
	uint_t nelem;
	const char *class;
	const char *subclass;
	int rv;

	if (!zcp) {
		errno = EINVAL;
		zed_log_msg(LOG_ERR, "Failed to service zevent: %s",
		    strerror(errno));
		return (EINVAL);
	}
	rv = zpool_events_next(zcp->zfs_hdl, &nvl, &n_dropped, ZEVENT_NONE,
	    zcp->zevent_fd);

	if ((rv != 0) || !nvl)
		return (errno);

	if (n_dropped > 0) {
		zed_log_msg(LOG_WARNING, "Missed %d events", n_dropped);
		_bump_event_queue_length();
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
		/*
		 * Special case: If we can dynamically detect an enclosure sysfs
		 * path, then use that value rather than the one stored in the
		 * vd->vdev_enc_sysfs_path.  There have been rare cases where
		 * vd->vdev_enc_sysfs_path becomes outdated.  However, there
		 * will be other times when we can not dynamically detect the
		 * sysfs path (like if a disk disappears) and have to rely on
		 * the old value for things like turning on the fault LED.
		 */
		_zed_event_update_enc_sysfs_path(nvl);

		/* let internal modules see this event first */
		zfs_agent_post_event(class, NULL, nvl);

		zsp = zed_strings_create();

		nvp = NULL;
		while ((nvp = nvlist_next_nvpair(nvl, nvp)))
			_zed_event_add_nvpair(eid, zsp, nvp);

		_zed_event_add_env_restrict(eid, zsp, zcp->path);
		_zed_event_add_env_preserve(eid, zsp);

		_zed_event_add_var(eid, zsp, ZED_VAR_PREFIX, "PID",
		    "%d", (int)getpid());
		_zed_event_add_var(eid, zsp, ZED_VAR_PREFIX, "ZEDLET_DIR",
		    "%s", zcp->zedlet_dir);
		subclass = _zed_event_get_subclass(class);
		_zed_event_add_var(eid, zsp, ZEVENT_VAR_PREFIX, "SUBCLASS",
		    "%s", (subclass ? subclass : class));

		_zed_event_add_time_strings(eid, zsp, etime);

		zed_exec_process(eid, class, subclass, zcp, zsp);

		zed_conf_write_state(zcp, eid, etime);

		zed_strings_destroy(zsp);
	}
	nvlist_free(nvl);
	return (0);
}
