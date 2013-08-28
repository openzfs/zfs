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
 * Copyright (c) 2005, 2010, Oracle and/or its affiliates. All rights reserved.
 * Copyright (c) 2012 by Delphix. All rights reserved.
 */

#ifndef	_SYS_DSL_PROP_H
#define	_SYS_DSL_PROP_H

#include <sys/dmu.h>
#include <sys/dsl_pool.h>
#include <sys/zfs_context.h>
#include <sys/dsl_synctask.h>

#ifdef	__cplusplus
extern "C" {
#endif

struct dsl_dataset;
struct dsl_dir;

/* The callback func may not call into the DMU or DSL! */
typedef void (dsl_prop_changed_cb_t)(void *arg, uint64_t newval);

typedef struct dsl_prop_cb_record {
	list_node_t cbr_node; /* link on dd_prop_cbs */
	struct dsl_dataset *cbr_ds;
	const char *cbr_propname;
	dsl_prop_changed_cb_t *cbr_func;
	void *cbr_arg;
} dsl_prop_cb_record_t;

typedef struct dsl_props_arg {
	nvlist_t *pa_props;
	zprop_source_t pa_source;
} dsl_props_arg_t;

typedef struct dsl_prop_set_arg {
	const char *psa_name;
	zprop_source_t psa_source;
	int psa_intsz;
	int psa_numints;
	const void *psa_value;

	/*
	 * Used to handle the special requirements of the quota and reservation
	 * properties.
	 */
	uint64_t psa_effective_value;
} dsl_prop_setarg_t;

int dsl_prop_register(struct dsl_dataset *ds, const char *propname,
    dsl_prop_changed_cb_t *callback, void *cbarg);
int dsl_prop_unregister(struct dsl_dataset *ds, const char *propname,
    dsl_prop_changed_cb_t *callback, void *cbarg);
int dsl_prop_numcb(struct dsl_dataset *ds);

int dsl_prop_get(const char *ddname, const char *propname,
    int intsz, int numints, void *buf, char *setpoint);
int dsl_prop_get_integer(const char *ddname, const char *propname,
    uint64_t *valuep, char *setpoint);
int dsl_prop_get_all(objset_t *os, nvlist_t **nvp);
int dsl_prop_get_received(objset_t *os, nvlist_t **nvp);
int dsl_prop_get_ds(struct dsl_dataset *ds, const char *propname,
    int intsz, int numints, void *buf, char *setpoint);
int dsl_prop_get_dd(struct dsl_dir *dd, const char *propname,
    int intsz, int numints, void *buf, char *setpoint,
    boolean_t snapshot);

dsl_syncfunc_t dsl_props_set_sync;
int dsl_prop_set(const char *ddname, const char *propname,
    zprop_source_t source, int intsz, int numints, const void *buf);
int dsl_props_set(const char *dsname, zprop_source_t source, nvlist_t *nvl);

void dsl_prop_setarg_init_uint64(dsl_prop_setarg_t *psa, const char *propname,
    zprop_source_t source, uint64_t *value);
int dsl_prop_predict_sync(dsl_dir_t *dd, dsl_prop_setarg_t *psa);
#ifdef	ZFS_DEBUG
void dsl_prop_check_prediction(dsl_dir_t *dd, dsl_prop_setarg_t *psa);
#define	DSL_PROP_CHECK_PREDICTION(dd, psa)	\
	dsl_prop_check_prediction((dd), (psa))
#else
#define	DSL_PROP_CHECK_PREDICTION(dd, psa)	/* nothing */
#endif

/* flag first receive on or after SPA_VERSION_RECVD_PROPS */
boolean_t dsl_prop_get_hasrecvd(objset_t *os);
void dsl_prop_set_hasrecvd(objset_t *os);
void dsl_prop_unset_hasrecvd(objset_t *os);

void dsl_prop_nvlist_add_uint64(nvlist_t *nv, zfs_prop_t prop, uint64_t value);
void dsl_prop_nvlist_add_string(nvlist_t *nv,
    zfs_prop_t prop, const char *value);

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_DSL_PROP_H */
