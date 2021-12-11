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
 * Copyright (c) 2004, 2010, Oracle and/or its affiliates. All rights reserved.
 *
 * Copyright (c) 2016, Intel Corporation.
 */

/*
 * This file implements the minimal FMD module API required to support the
 * fault logic modules in ZED. This support includes module registration,
 * memory allocation, module property accessors, basic case management,
 * one-shot timers and SERD engines.
 *
 * In the ZED runtime, the modules are called from a single thread so no
 * locking is required in this emulated FMD environment.
 */

#include <sys/types.h>
#include <sys/fm/protocol.h>
#include <uuid/uuid.h>
#include <signal.h>
#include <strings.h>
#include <time.h>

#include "fmd_api.h"
#include "fmd_serd.h"

#include "zfs_agents.h"
#include "../zed_log.h"

typedef struct fmd_modstat {
	fmd_stat_t	ms_accepted;	/* total events accepted by module */
	fmd_stat_t	ms_caseopen;	/* cases currently open */
	fmd_stat_t	ms_casesolved;	/* total cases solved by module */
	fmd_stat_t	ms_caseclosed;	/* total cases closed by module */
} fmd_modstat_t;

typedef struct fmd_module {
	const char	*mod_name;	/* basename of module (ro) */
	const fmd_hdl_info_t *mod_info;	/* module info registered with handle */
	void		*mod_spec;	/* fmd_hdl_get/setspecific data value */
	fmd_stat_t	*mod_ustat;	/* module specific custom stats */
	uint_t		mod_ustat_cnt;	/* count of ustat stats */
	fmd_modstat_t	mod_stats;	/* fmd built-in per-module statistics */
	fmd_serd_hash_t	mod_serds;	/* hash of serd engs owned by module */
	char		*mod_vers;	/* a copy of module version string */
} fmd_module_t;

/*
 * ZED has two FMD hardwired module instances
 */
fmd_module_t	zfs_retire_module;
fmd_module_t	zfs_diagnosis_module;

/*
 * Enable a reasonable set of defaults for libumem debugging on DEBUG builds.
 */

#ifdef DEBUG
const char *
_umem_debug_init(void)
{
	return ("default,verbose"); /* $UMEM_DEBUG setting */
}

const char *
_umem_logging_init(void)
{
	return ("fail,contents"); /* $UMEM_LOGGING setting */
}
#endif

/*
 * Register a module with fmd and finish module initialization.
 * Returns an integer indicating whether it succeeded (zero) or
 * failed (non-zero).
 */
int
fmd_hdl_register(fmd_hdl_t *hdl, int version, const fmd_hdl_info_t *mip)
{
	(void) version;
	fmd_module_t *mp = (fmd_module_t *)hdl;

	mp->mod_info = mip;
	mp->mod_name = mip->fmdi_desc + 4;	/* drop 'ZFS ' prefix */
	mp->mod_spec = NULL;

	/* bare minimum module stats */
	(void) strcpy(mp->mod_stats.ms_accepted.fmds_name, "fmd.accepted");
	(void) strcpy(mp->mod_stats.ms_caseopen.fmds_name, "fmd.caseopen");
	(void) strcpy(mp->mod_stats.ms_casesolved.fmds_name, "fmd.casesolved");
	(void) strcpy(mp->mod_stats.ms_caseclosed.fmds_name, "fmd.caseclosed");

	fmd_serd_hash_create(&mp->mod_serds);

	fmd_hdl_debug(hdl, "register module");

	return (0);
}

void
fmd_hdl_unregister(fmd_hdl_t *hdl)
{
	fmd_module_t *mp = (fmd_module_t *)hdl;
	fmd_modstat_t *msp = &mp->mod_stats;
	const fmd_hdl_ops_t *ops = mp->mod_info->fmdi_ops;

	/* dump generic module stats */
	fmd_hdl_debug(hdl, "%s: %llu", msp->ms_accepted.fmds_name,
	    msp->ms_accepted.fmds_value.ui64);
	if (ops->fmdo_close != NULL) {
		fmd_hdl_debug(hdl, "%s: %llu", msp->ms_caseopen.fmds_name,
		    msp->ms_caseopen.fmds_value.ui64);
		fmd_hdl_debug(hdl, "%s: %llu", msp->ms_casesolved.fmds_name,
		    msp->ms_casesolved.fmds_value.ui64);
		fmd_hdl_debug(hdl, "%s: %llu", msp->ms_caseclosed.fmds_name,
		    msp->ms_caseclosed.fmds_value.ui64);
	}

	/* dump module specific stats */
	if (mp->mod_ustat != NULL) {
		int i;

		for (i = 0; i < mp->mod_ustat_cnt; i++) {
			fmd_hdl_debug(hdl, "%s: %llu",
			    mp->mod_ustat[i].fmds_name,
			    mp->mod_ustat[i].fmds_value.ui64);
		}
	}

	fmd_serd_hash_destroy(&mp->mod_serds);

	fmd_hdl_debug(hdl, "unregister module");
}

/*
 * fmd_hdl_setspecific() is used to associate a data pointer with
 * the specified handle for the duration of the module's lifetime.
 * This pointer can be retrieved using fmd_hdl_getspecific().
 */
void
fmd_hdl_setspecific(fmd_hdl_t *hdl, void *spec)
{
	fmd_module_t *mp = (fmd_module_t *)hdl;

	mp->mod_spec = spec;
}

/*
 * Return the module-specific data pointer previously associated
 * with the handle using fmd_hdl_setspecific().
 */
void *
fmd_hdl_getspecific(fmd_hdl_t *hdl)
{
	fmd_module_t *mp = (fmd_module_t *)hdl;

	return (mp->mod_spec);
}

void *
fmd_hdl_alloc(fmd_hdl_t *hdl, size_t size, int flags)
{
	(void) hdl;
	return (umem_alloc(size, flags));
}

void *
fmd_hdl_zalloc(fmd_hdl_t *hdl, size_t size, int flags)
{
	(void) hdl;
	return (umem_zalloc(size, flags));
}

void
fmd_hdl_free(fmd_hdl_t *hdl, void *data, size_t size)
{
	(void) hdl;
	umem_free(data, size);
}

/*
 * Record a module debug message using the specified format.
 */
void
fmd_hdl_debug(fmd_hdl_t *hdl, const char *format, ...)
{
	char message[256];
	va_list vargs;
	fmd_module_t *mp = (fmd_module_t *)hdl;

	va_start(vargs, format);
	(void) vsnprintf(message, sizeof (message), format, vargs);
	va_end(vargs);

	/* prefix message with module name */
	zed_log_msg(LOG_INFO, "%s: %s", mp->mod_name, message);
}

/* Property Retrieval */

int32_t
fmd_prop_get_int32(fmd_hdl_t *hdl, const char *name)
{
	(void) hdl;

	/*
	 * These can be looked up in mp->modinfo->fmdi_props
	 * For now we just hard code for phase 2. In the
	 * future, there can be a ZED based override.
	 */
	if (strcmp(name, "spare_on_remove") == 0)
		return (1);

	if (strcmp(name, "io_N") == 0 || strcmp(name, "checksum_N") == 0)
		return (10);	/* N = 10 events */

	return (0);
}

int64_t
fmd_prop_get_int64(fmd_hdl_t *hdl, const char *name)
{
	(void) hdl;

	/*
	 * These can be looked up in mp->modinfo->fmdi_props
	 * For now we just hard code for phase 2. In the
	 * future, there can be a ZED based override.
	 */
	if (strcmp(name, "remove_timeout") == 0)
		return (15ULL * 1000ULL * 1000ULL * 1000ULL);	/* 15 sec */

	if (strcmp(name, "io_T") == 0 || strcmp(name, "checksum_T") == 0)
		return (1000ULL * 1000ULL * 1000ULL * 600ULL);	/* 10 min */

	return (0);
}

/* FMD Statistics */

fmd_stat_t *
fmd_stat_create(fmd_hdl_t *hdl, uint_t flags, uint_t nstats, fmd_stat_t *statv)
{
	fmd_module_t *mp = (fmd_module_t *)hdl;

	if (flags == FMD_STAT_NOALLOC) {
		mp->mod_ustat = statv;
		mp->mod_ustat_cnt = nstats;
	}

	return (statv);
}

/* Case Management */

fmd_case_t *
fmd_case_open(fmd_hdl_t *hdl, void *data)
{
	fmd_module_t *mp = (fmd_module_t *)hdl;
	uuid_t uuid;

	fmd_case_t *cp;

	cp = fmd_hdl_zalloc(hdl, sizeof (fmd_case_t), FMD_SLEEP);
	cp->ci_mod = hdl;
	cp->ci_state = FMD_CASE_UNSOLVED;
	cp->ci_flags = FMD_CF_DIRTY;
	cp->ci_data = data;
	cp->ci_bufptr = NULL;
	cp->ci_bufsiz = 0;

	uuid_generate(uuid);
	uuid_unparse(uuid, cp->ci_uuid);

	fmd_hdl_debug(hdl, "case opened (%s)", cp->ci_uuid);
	mp->mod_stats.ms_caseopen.fmds_value.ui64++;

	return (cp);
}

void
fmd_case_solve(fmd_hdl_t *hdl, fmd_case_t *cp)
{
	fmd_module_t *mp = (fmd_module_t *)hdl;

	/*
	 * For ZED, the event was already sent from fmd_case_add_suspect()
	 */

	if (cp->ci_state >= FMD_CASE_SOLVED)
		fmd_hdl_debug(hdl, "case is already solved or closed");

	cp->ci_state = FMD_CASE_SOLVED;

	fmd_hdl_debug(hdl, "case solved (%s)", cp->ci_uuid);
	mp->mod_stats.ms_casesolved.fmds_value.ui64++;
}

void
fmd_case_close(fmd_hdl_t *hdl, fmd_case_t *cp)
{
	fmd_module_t *mp = (fmd_module_t *)hdl;
	const fmd_hdl_ops_t *ops = mp->mod_info->fmdi_ops;

	fmd_hdl_debug(hdl, "case closed (%s)", cp->ci_uuid);

	if (ops->fmdo_close != NULL)
		ops->fmdo_close(hdl, cp);

	mp->mod_stats.ms_caseopen.fmds_value.ui64--;
	mp->mod_stats.ms_caseclosed.fmds_value.ui64++;

	if (cp->ci_bufptr != NULL && cp->ci_bufsiz > 0)
		fmd_hdl_free(hdl, cp->ci_bufptr, cp->ci_bufsiz);

	fmd_hdl_free(hdl, cp, sizeof (fmd_case_t));
}

void
fmd_case_uuresolved(fmd_hdl_t *hdl, const char *uuid)
{
	fmd_hdl_debug(hdl, "case resolved by uuid (%s)", uuid);
}

int
fmd_case_solved(fmd_hdl_t *hdl, fmd_case_t *cp)
{
	(void) hdl;
	return ((cp->ci_state >= FMD_CASE_SOLVED) ? FMD_B_TRUE : FMD_B_FALSE);
}

void
fmd_case_add_ereport(fmd_hdl_t *hdl, fmd_case_t *cp, fmd_event_t *ep)
{
	(void) hdl, (void) cp, (void) ep;
}

static void
zed_log_fault(nvlist_t *nvl, const char *uuid, const char *code)
{
	nvlist_t *rsrc;
	char *strval;
	uint64_t guid;
	uint8_t byte;

	zed_log_msg(LOG_INFO, "\nzed_fault_event:");

	if (uuid != NULL)
		zed_log_msg(LOG_INFO, "\t%s: %s", FM_SUSPECT_UUID, uuid);
	if (nvlist_lookup_string(nvl, FM_CLASS, &strval) == 0)
		zed_log_msg(LOG_INFO, "\t%s: %s", FM_CLASS, strval);
	if (code != NULL)
		zed_log_msg(LOG_INFO, "\t%s: %s", FM_SUSPECT_DIAG_CODE, code);
	if (nvlist_lookup_uint8(nvl, FM_FAULT_CERTAINTY, &byte) == 0)
		zed_log_msg(LOG_INFO, "\t%s: %llu", FM_FAULT_CERTAINTY, byte);
	if (nvlist_lookup_nvlist(nvl, FM_FAULT_RESOURCE, &rsrc) == 0) {
		if (nvlist_lookup_string(rsrc, FM_FMRI_SCHEME, &strval) == 0)
			zed_log_msg(LOG_INFO, "\t%s: %s", FM_FMRI_SCHEME,
			    strval);
		if (nvlist_lookup_uint64(rsrc, FM_FMRI_ZFS_POOL, &guid) == 0)
			zed_log_msg(LOG_INFO, "\t%s: %llu", FM_FMRI_ZFS_POOL,
			    guid);
		if (nvlist_lookup_uint64(rsrc, FM_FMRI_ZFS_VDEV, &guid) == 0)
			zed_log_msg(LOG_INFO, "\t%s: %llu \n", FM_FMRI_ZFS_VDEV,
			    guid);
	}
}

static const char *
fmd_fault_mkcode(nvlist_t *fault)
{
	char *class, *code = "-";

	/*
	 * Note: message codes come from: openzfs/usr/src/cmd/fm/dicts/ZFS.po
	 */
	if (nvlist_lookup_string(fault, FM_CLASS, &class) == 0) {
		if (strcmp(class, "fault.fs.zfs.vdev.io") == 0)
			code = "ZFS-8000-FD";
		else if (strcmp(class, "fault.fs.zfs.vdev.checksum") == 0)
			code = "ZFS-8000-GH";
		else if (strcmp(class, "fault.fs.zfs.io_failure_wait") == 0)
			code = "ZFS-8000-HC";
		else if (strcmp(class, "fault.fs.zfs.io_failure_continue") == 0)
			code = "ZFS-8000-JQ";
		else if (strcmp(class, "fault.fs.zfs.log_replay") == 0)
			code = "ZFS-8000-K4";
		else if (strcmp(class, "fault.fs.zfs.pool") == 0)
			code = "ZFS-8000-CS";
		else if (strcmp(class, "fault.fs.zfs.device") == 0)
			code = "ZFS-8000-D3";

	}
	return (code);
}

void
fmd_case_add_suspect(fmd_hdl_t *hdl, fmd_case_t *cp, nvlist_t *fault)
{
	nvlist_t *nvl;
	const char *code = fmd_fault_mkcode(fault);
	int64_t tod[2];
	int err = 0;

	/*
	 * payload derived from fmd_protocol_list()
	 */

	(void) gettimeofday(&cp->ci_tv, NULL);
	tod[0] = cp->ci_tv.tv_sec;
	tod[1] = cp->ci_tv.tv_usec;

	nvl = fmd_nvl_alloc(hdl, FMD_SLEEP);

	err |= nvlist_add_uint8(nvl, FM_VERSION, FM_SUSPECT_VERSION);
	err |= nvlist_add_string(nvl, FM_CLASS, FM_LIST_SUSPECT_CLASS);
	err |= nvlist_add_string(nvl, FM_SUSPECT_UUID, cp->ci_uuid);
	err |= nvlist_add_string(nvl, FM_SUSPECT_DIAG_CODE, code);
	err |= nvlist_add_int64_array(nvl, FM_SUSPECT_DIAG_TIME, tod, 2);
	err |= nvlist_add_uint32(nvl, FM_SUSPECT_FAULT_SZ, 1);
	err |= nvlist_add_nvlist_array(nvl, FM_SUSPECT_FAULT_LIST,
	    (const nvlist_t **)&fault, 1);

	if (err)
		zed_log_die("failed to populate nvlist");

	zed_log_fault(fault, cp->ci_uuid, code);
	zfs_agent_post_event(FM_LIST_SUSPECT_CLASS, NULL, nvl);

	nvlist_free(nvl);
	nvlist_free(fault);
}

void
fmd_case_setspecific(fmd_hdl_t *hdl, fmd_case_t *cp, void *data)
{
	(void) hdl;
	cp->ci_data = data;
}

void *
fmd_case_getspecific(fmd_hdl_t *hdl, fmd_case_t *cp)
{
	(void) hdl;
	return (cp->ci_data);
}

void
fmd_buf_create(fmd_hdl_t *hdl, fmd_case_t *cp, const char *name, size_t size)
{
	assert(strcmp(name, "data") == 0), (void) name;
	assert(cp->ci_bufptr == NULL);
	assert(size < (1024 * 1024));

	cp->ci_bufptr = fmd_hdl_alloc(hdl, size, FMD_SLEEP);
	cp->ci_bufsiz = size;
}

void
fmd_buf_read(fmd_hdl_t *hdl, fmd_case_t *cp,
    const char *name, void *buf, size_t size)
{
	(void) hdl;
	assert(strcmp(name, "data") == 0), (void) name;
	assert(cp->ci_bufptr != NULL);
	assert(size <= cp->ci_bufsiz);

	bcopy(cp->ci_bufptr, buf, size);
}

void
fmd_buf_write(fmd_hdl_t *hdl, fmd_case_t *cp,
    const char *name, const void *buf, size_t size)
{
	(void) hdl;
	assert(strcmp(name, "data") == 0), (void) name;
	assert(cp->ci_bufptr != NULL);
	assert(cp->ci_bufsiz >= size);

	bcopy(buf, cp->ci_bufptr, size);
}

/* SERD Engines */

void
fmd_serd_create(fmd_hdl_t *hdl, const char *name, uint_t n, hrtime_t t)
{
	fmd_module_t *mp = (fmd_module_t *)hdl;

	if (fmd_serd_eng_lookup(&mp->mod_serds, name) != NULL) {
		zed_log_msg(LOG_ERR, "failed to create SERD engine '%s': "
		    " name already exists", name);
		return;
	}

	(void) fmd_serd_eng_insert(&mp->mod_serds, name, n, t);
}

void
fmd_serd_destroy(fmd_hdl_t *hdl, const char *name)
{
	fmd_module_t *mp = (fmd_module_t *)hdl;

	fmd_serd_eng_delete(&mp->mod_serds, name);

	fmd_hdl_debug(hdl, "serd_destroy %s", name);
}

int
fmd_serd_exists(fmd_hdl_t *hdl, const char *name)
{
	fmd_module_t *mp = (fmd_module_t *)hdl;

	return (fmd_serd_eng_lookup(&mp->mod_serds, name) != NULL);
}

void
fmd_serd_reset(fmd_hdl_t *hdl, const char *name)
{
	fmd_module_t *mp = (fmd_module_t *)hdl;
	fmd_serd_eng_t *sgp;

	if ((sgp = fmd_serd_eng_lookup(&mp->mod_serds, name)) == NULL) {
		zed_log_msg(LOG_ERR, "serd engine '%s' does not exist", name);
		return;
	}

	fmd_serd_eng_reset(sgp);

	fmd_hdl_debug(hdl, "serd_reset %s", name);
}

int
fmd_serd_record(fmd_hdl_t *hdl, const char *name, fmd_event_t *ep)
{
	fmd_module_t *mp = (fmd_module_t *)hdl;
	fmd_serd_eng_t *sgp;
	int err;

	if ((sgp = fmd_serd_eng_lookup(&mp->mod_serds, name)) == NULL) {
		zed_log_msg(LOG_ERR, "failed to add record to SERD engine '%s'",
		    name);
		return (FMD_B_FALSE);
	}
	err = fmd_serd_eng_record(sgp, ep->ev_hrt);

	return (err);
}

/* FMD Timers */

static void
_timer_notify(union sigval sv)
{
	fmd_timer_t *ftp = sv.sival_ptr;
	fmd_hdl_t *hdl = ftp->ft_hdl;
	fmd_module_t *mp = (fmd_module_t *)hdl;
	const fmd_hdl_ops_t *ops = mp->mod_info->fmdi_ops;
	struct itimerspec its;

	fmd_hdl_debug(hdl, "timer fired (%p)", ftp->ft_tid);

	/* disarm the timer */
	bzero(&its, sizeof (struct itimerspec));
	timer_settime(ftp->ft_tid, 0, &its, NULL);

	/* Note that the fmdo_timeout can remove this timer */
	if (ops->fmdo_timeout != NULL)
		ops->fmdo_timeout(hdl, ftp, ftp->ft_arg);
}

/*
 * Install a new timer which will fire at least delta nanoseconds after the
 * current time. After the timeout has expired, the module's fmdo_timeout
 * entry point is called.
 */
fmd_timer_t *
fmd_timer_install(fmd_hdl_t *hdl, void *arg, fmd_event_t *ep, hrtime_t delta)
{
	(void) ep;
	struct sigevent sev;
	struct itimerspec its;
	fmd_timer_t *ftp;

	ftp = fmd_hdl_alloc(hdl, sizeof (fmd_timer_t), FMD_SLEEP);
	ftp->ft_arg = arg;
	ftp->ft_hdl = hdl;

	its.it_value.tv_sec = delta / 1000000000;
	its.it_value.tv_nsec = delta % 1000000000;
	its.it_interval.tv_sec = its.it_value.tv_sec;
	its.it_interval.tv_nsec = its.it_value.tv_nsec;

	sev.sigev_notify = SIGEV_THREAD;
	sev.sigev_notify_function = _timer_notify;
	sev.sigev_notify_attributes = NULL;
	sev.sigev_value.sival_ptr = ftp;

	timer_create(CLOCK_REALTIME, &sev, &ftp->ft_tid);
	timer_settime(ftp->ft_tid, 0, &its, NULL);

	fmd_hdl_debug(hdl, "installing timer for %d secs (%p)",
	    (int)its.it_value.tv_sec, ftp->ft_tid);

	return (ftp);
}

void
fmd_timer_remove(fmd_hdl_t *hdl, fmd_timer_t *ftp)
{
	fmd_hdl_debug(hdl, "removing timer (%p)", ftp->ft_tid);

	timer_delete(ftp->ft_tid);

	fmd_hdl_free(hdl, ftp, sizeof (fmd_timer_t));
}

/* Name-Value Pair Lists */

nvlist_t *
fmd_nvl_create_fault(fmd_hdl_t *hdl, const char *class, uint8_t certainty,
    nvlist_t *asru, nvlist_t *fru, nvlist_t *resource)
{
	(void) hdl;
	nvlist_t *nvl;
	int err = 0;

	if (nvlist_alloc(&nvl, NV_UNIQUE_NAME, 0) != 0)
		zed_log_die("failed to xalloc fault nvlist");

	err |= nvlist_add_uint8(nvl, FM_VERSION, FM_FAULT_VERSION);
	err |= nvlist_add_string(nvl, FM_CLASS, class);
	err |= nvlist_add_uint8(nvl, FM_FAULT_CERTAINTY, certainty);

	if (asru != NULL)
		err |= nvlist_add_nvlist(nvl, FM_FAULT_ASRU, asru);
	if (fru != NULL)
		err |= nvlist_add_nvlist(nvl, FM_FAULT_FRU, fru);
	if (resource != NULL)
		err |= nvlist_add_nvlist(nvl, FM_FAULT_RESOURCE, resource);

	if (err)
		zed_log_die("failed to populate nvlist: %s\n", strerror(err));

	return (nvl);
}

/*
 * sourced from fmd_string.c
 */
static int
fmd_strmatch(const char *s, const char *p)
{
	char c;

	if (p == NULL)
		return (0);

	if (s == NULL)
		s = ""; /* treat NULL string as the empty string */

	do {
		if ((c = *p++) == '\0')
			return (*s == '\0');

		if (c == '*') {
			while (*p == '*')
				p++; /* consecutive *'s can be collapsed */

			if (*p == '\0')
				return (1);

			while (*s != '\0') {
				if (fmd_strmatch(s++, p) != 0)
					return (1);
			}

			return (0);
		}
	} while (c == *s++);

	return (0);
}

int
fmd_nvl_class_match(fmd_hdl_t *hdl, nvlist_t *nvl, const char *pattern)
{
	(void) hdl;
	char *class;

	return (nvl != NULL &&
	    nvlist_lookup_string(nvl, FM_CLASS, &class) == 0 &&
	    fmd_strmatch(class, pattern));
}

nvlist_t *
fmd_nvl_alloc(fmd_hdl_t *hdl, int flags)
{
	(void) hdl, (void) flags;
	nvlist_t *nvl = NULL;

	if (nvlist_alloc(&nvl, NV_UNIQUE_NAME, 0) != 0)
		return (NULL);

	return (nvl);
}


/*
 * ZED Agent specific APIs
 */

fmd_hdl_t *
fmd_module_hdl(const char *name)
{
	if (strcmp(name, "zfs-retire") == 0)
		return ((fmd_hdl_t *)&zfs_retire_module);
	if (strcmp(name, "zfs-diagnosis") == 0)
		return ((fmd_hdl_t *)&zfs_diagnosis_module);

	return (NULL);
}

boolean_t
fmd_module_initialized(fmd_hdl_t *hdl)
{
	fmd_module_t *mp = (fmd_module_t *)hdl;

	return (mp->mod_info != NULL);
}

/*
 * fmd_module_recv is called for each event that is received by
 * the fault manager that has a class that matches one of the
 * module's subscriptions.
 */
void
fmd_module_recv(fmd_hdl_t *hdl, nvlist_t *nvl, const char *class)
{
	fmd_module_t *mp = (fmd_module_t *)hdl;
	const fmd_hdl_ops_t *ops = mp->mod_info->fmdi_ops;
	fmd_event_t faux_event = {0};
	int64_t *tv;
	uint_t n;

	/*
	 * Will need to normalized this if we persistently store the case data
	 */
	if (nvlist_lookup_int64_array(nvl, FM_EREPORT_TIME, &tv, &n) == 0)
		faux_event.ev_hrt = tv[0] * NANOSEC + tv[1];
	else
		faux_event.ev_hrt = 0;

	ops->fmdo_recv(hdl, &faux_event, nvl, class);

	mp->mod_stats.ms_accepted.fmds_value.ui64++;

	/* TBD - should we initiate fm_module_gc() periodically? */
}
