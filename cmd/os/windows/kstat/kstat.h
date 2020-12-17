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
 * Copyright 2006 Sun Microsystems, Inc.  All rights reserved.
 * Copyright 2013 David Hoeppner.  All rights reserved.
 * Copyright 2013 Nexenta Systems, Inc.  All rights reserved.
 */

#ifndef _STAT_KSTAT_H
#define	_STAT_KSTAT_H

/*
 * Structures needed by the kstat reader functions.
 */
//#include <sys/var.h>
#include <sys/utsname.h>
//#include <sys/sysinfo.h>
//#include <sys/flock.h>
//#include <sys/dnlc.h>
#include <regex.h>
//#include <nfs/nfs.h>
//#include <nfs/nfs_clnt.h>

#ifdef __sparc
#include <vm/hat_sfmmu.h>
#include <sys/simmstat.h>
#include <sys/sysctrl.h>
#include <sys/fhc.h>
#endif

#define	KSTAT_DATA_HRTIME	(KSTAT_DATA_STRING + 1)

typedef union ks_value {
	char		c[16];
	int32_t		i32;
	uint32_t	ui32;
	struct {
		union {
			char	*ptr;
			char	__pad[8];
		} addr;
		uint32_t	len;
	} str;

	int64_t		i64;
	uint64_t	ui64;
} ks_value_t;

#define	SAVE_HRTIME(I, S, N)				\
{							\
	ks_value_t v;					\
	v.ui64 = S->N;					\
	nvpair_insert(I, #N, &v, KSTAT_DATA_UINT64);	\
}

#define	SAVE_INT32(I, S, N)				\
{							\
	ks_value_t v;					\
	v.i32 = S->N;					\
	nvpair_insert(I, #N, &v, KSTAT_DATA_INT32);	\
}

#define	SAVE_UINT32(I, S, N)				\
{							\
	ks_value_t v;					\
	v.ui32 = S->N;					\
	nvpair_insert(I, #N, &v, KSTAT_DATA_UINT32);	\
}

#define	SAVE_INT64(I, S, N)             		\
{							\
	ks_value_t v;					\
	v.i64 = S->N;					\
	nvpair_insert(I, #N, &v, KSTAT_DATA_INT64);	\
}

#define	SAVE_UINT64(I, S, N)				\
{							\
	ks_value_t v;					\
	v.ui64 = S->N;					\
	nvpair_insert(I, #N, &v, KSTAT_DATA_UINT64);	\
}

/*
 * We dont want const "strings" because we free
 * the instances later.
 */
#define	SAVE_STRING(I, S, N)				\
{							\
	ks_value_t v;					\
	v.str.addr.ptr = _strdup(S->N);		\
	v.str.len = (uint32_t) strlen(S->N);			\
	nvpair_insert(I, #N, &v, KSTAT_DATA_STRING);	\
}

#define	SAVE_HRTIME_X(I, N, V)				\
{							\
	ks_value_t v;					\
	v.ui64 = V;					\
	nvpair_insert(I, N, &v, KSTAT_DATA_HRTIME);	\
}

#define	SAVE_INT32_X(I, N, V)				\
{							\
	ks_value_t v;					\
	v.i32 = V;					\
	nvpair_insert(I, N, &v, KSTAT_DATA_INT32);	\
}

#define	SAVE_UINT32_X(I, N, V)				\
{							\
	ks_value_t v;					\
	v.ui32 = V;					\
	nvpair_insert(I, N, &v, KSTAT_DATA_UINT32);	\
}

#define	SAVE_UINT64_X(I, N, V)				\
{							\
	ks_value_t v;					\
	v.ui64 = V;					\
	nvpair_insert(I, N, &v, KSTAT_DATA_UINT64);	\
}

#define	SAVE_STRING_X(I, N, V)				\
{							\
	ks_value_t v;					\
	v.str.addr.ptr = _strdup(V);		\
	v.str.len = (uint32_t)( (V) ? strlen(V) : 0 );		\
	nvpair_insert(I, N, &v, KSTAT_DATA_STRING);	\
}

#define	SAVE_CHAR_X(I, N, V)				\
{							\
	ks_value_t v;					\
	(void) asprintf(&v.str.addr.ptr, "%c", V);	\
	v.str.len = 1;					\
	nvpair_insert(I, N, &v, KSTAT_DATA_STRING);	\
}

#define	DFLT_FMT					\
	"module: %-30.30s  instance: %-6d\n"		\
	"name:   %-30.30s  class:    %-.30s\n"

#define	KS_DFMT	"\t%-30s  "
#define	KS_PFMT	"%s:%d:%s:%s"

typedef struct ks_instance {
	list_node_t	ks_next;
	char		ks_name[KSTAT_STRLEN];
	char		ks_module[KSTAT_STRLEN];
	char		ks_class[KSTAT_STRLEN];
	int 		ks_instance;
	uchar_t		ks_type;
	hrtime_t	ks_snaptime;
	list_t		ks_nvlist;
} ks_instance_t;

typedef struct ks_nvpair {
	list_node_t	nv_next;
	char		name[KSTAT_STRLEN];
	uchar_t		data_type;
	ks_value_t	value;
} ks_nvpair_t;

typedef struct ks_pattern {
	char		*pstr;
	regex_t		preg;
} ks_pattern_t;

typedef struct ks_selector {
	list_node_t	ks_next;
	ks_pattern_t	ks_module;
	ks_pattern_t	ks_instance;
	ks_pattern_t	ks_name;
	ks_pattern_t	ks_statistic;
} ks_selector_t;

static void	usage(void);
static int	compare_instances(ks_instance_t *, ks_instance_t *);
static void	nvpair_insert(ks_instance_t *, char *, ks_value_t *, uchar_t);
static boolean_t	ks_match(const char *, ks_pattern_t *);
static ks_selector_t	*new_selector(void);
static void	ks_instances_read(kstat_ctl_t *);
static void	ks_value_print(ks_nvpair_t *);
static void	ks_instances_print(void);
static char	*ks_safe_strdup(char *);
static void	ks_sleep_until(hrtime_t *, hrtime_t, int, int *);
static int write_mode(int argc, char **argv);

/* Raw kstat readers */
#ifndef WIN32
static void	save_cpu_stat(kstat_t *, ks_instance_t *);
static void	save_var(kstat_t *, ks_instance_t *);
static void	save_ncstats(kstat_t *, ks_instance_t *);
static void	save_sysinfo(kstat_t *, ks_instance_t *);
static void	save_vminfo(kstat_t *, ks_instance_t *);
static void	save_nfs(kstat_t *, ks_instance_t *);
#ifdef __sparc
static void	save_sfmmu_global_stat(kstat_t *, ks_instance_t *);
static void	save_sfmmu_tsbsize_stat(kstat_t *, ks_instance_t *);
static void	save_simmstat(kstat_t *, ks_instance_t *);
/* Helper function for save_temperature() */
static char	*short_array_to_string(short *, int);
static void	save_temperature(kstat_t *, ks_instance_t *);
static void	save_temp_over(kstat_t *, ks_instance_t *);
static void	save_ps_shadow(kstat_t *, ks_instance_t *);
static void	save_fault_list(kstat_t *, ks_instance_t *);
#endif
#endif

/* Named kstat readers */
static void	save_named(kstat_t *, ks_instance_t *);
static void	save_intr(kstat_t *, ks_instance_t *);
static void	save_io(kstat_t *, ks_instance_t *);
static void	save_timer(kstat_t *, ks_instance_t *);

/* Typedef for raw kstat reader functions */
typedef void	(*kstat_raw_reader_t)(kstat_t *, ks_instance_t *);

static struct {
	kstat_raw_reader_t fn;
	char *name;
} ks_raw_lookup[] = {
	/* Function name		kstat name		*/
#ifndef WIN32
	{save_cpu_stat,			"cpu_stat:cpu_stat"},
	{save_var,			"unix:var"},
	{save_ncstats,			"unix:ncstats"},
	{save_sysinfo,			"unix:sysinfo"},
	{save_vminfo,			"unix:vminfo"},
	{save_nfs,			"nfs:mntinfo"},
#ifdef __sparc
	{save_sfmmu_global_stat,	"unix:sfmmu_global_stat"},
	{save_sfmmu_tsbsize_stat,	"unix:sfmmu_tsbsize_stat"},
	{save_simmstat,			"unix:simm-status"},
	{save_temperature,		"unix:temperature"},
	{save_temp_over,		"unix:temperature override"},
	{save_ps_shadow,		"unix:ps_shadow"},
	{save_fault_list,		"unix:fault_list"},
#endif
#endif
	{NULL, NULL},
};

static kstat_raw_reader_t	lookup_raw_kstat_fn(char *, char *);

#endif /* _STAT_KSTAT_H */
