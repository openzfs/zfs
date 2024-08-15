/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://opensource.org/licenses/CDDL-1.0.
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
 * Copyright (c) 2024, Klara Inc.
 */

#include <sys/zfs_context.h>
#include <sys/spa_impl.h>
#include <sys/vdev_impl.h>
#include <sys/spa.h>
#include <zfs_comutil.h>
#include <sys/spa_json_stats.h>
#include <sys/nvpair_impl.h>

#define	JSON_STATUS_VERSION	4

static const char *
vdev_state_string(vdev_state_t state, vdev_aux_t aux)
{
	const char *s;
	switch (state) {
	case VDEV_STATE_UNKNOWN:	s = "HEALTHY";    break;
	case VDEV_STATE_CLOSED:		s = "CLOSED";	  break;
	case VDEV_STATE_OFFLINE:	s = "OFFLINE";    break;
	case VDEV_STATE_REMOVED:	s = "REMOVED";    break;
	case VDEV_STATE_CANT_OPEN:
		if (aux == VDEV_AUX_CORRUPT_DATA || aux == VDEV_AUX_BAD_LOG)
			s = "FAULTED";
		else if (aux == VDEV_AUX_SPLIT_POOL)
			s = "SPLIT";
		else
			s = "UNAVAIL";
		break;
	case VDEV_STATE_FAULTED:	s = "FAULTED";    break;
	case VDEV_STATE_DEGRADED:	s = "DEGRADED";   break;
	case VDEV_STATE_HEALTHY:	s = "HEALTHY";    break;
	default:			s = "?";
	}
	return (s);
}

static void
vdev_to_nvlist(vdev_t *vd, pool_scan_stat_t *ps, nvlist_t *tree)
{
	uint64_t n;
	int nparity = vdev_get_nparity(vd);
	vdev_t **a;
	const char *s;
	nvlist_t *init_state, *trim_state;

	nvlist_add_string(tree, "type", vd->vdev_ops->vdev_op_type);

	/* dRAID vdevs have additional config keys */
	if (vd->vdev_ops == &vdev_draid_ops &&
	    vd->vdev_ops->vdev_op_config_generate != NULL) {
		nvlist_t *nvl = fnvlist_alloc();
		vd->vdev_ops->vdev_op_config_generate(vd, nvl);
		fnvlist_merge(tree, nvl);
		nvlist_free(nvl);
	} else if (nparity > 0) {
		/* RAIDZ parity */
		fnvlist_add_uint64(tree, "nparity", nparity);
	}

	fnvlist_add_uint64(tree, "id", vd->vdev_id);
	fnvlist_add_uint64(tree, "guid", vd->vdev_guid);
	if (strcmp(vd->vdev_ops->vdev_op_type, "root") != 0) {
		fnvlist_add_uint64(tree, "asize", vd->vdev_asize);
		fnvlist_add_uint64(tree, "ashift", vd->vdev_ashift);
		if (vd->vdev_ops->vdev_op_leaf) {
			fnvlist_add_boolean_value(tree, "whole_disk",
			    (vd->vdev_wholedisk == 0) ? B_FALSE : B_TRUE);
		}
		fnvlist_add_boolean_value(tree, "offline",
		    (vd->vdev_offline == 0) ? B_FALSE : B_TRUE);
		fnvlist_add_boolean_value(tree, "faulted",
		    (vd->vdev_faulted == 0) ? B_FALSE : B_TRUE);
		fnvlist_add_boolean_value(tree, "degraded",
		    (vd->vdev_degraded == 0) ? B_FALSE : B_TRUE);
		fnvlist_add_boolean_value(tree, "removed",
		    (vd->vdev_removed == 0) ? B_FALSE : B_TRUE);
		fnvlist_add_boolean_value(tree, "not_present",
		    (vd->vdev_not_present == 0) ? B_FALSE : B_TRUE);
		fnvlist_add_boolean_value(tree, "is_log",
		    (vd->vdev_islog == 0) ? B_FALSE : B_TRUE);

		if (vd->vdev_path != NULL)
			fnvlist_add_string(tree, "path", vd->vdev_path);
		if (vd->vdev_devid != NULL)
			fnvlist_add_string(tree, "devid",  vd->vdev_devid);
		if (vd->vdev_physpath != NULL)
			fnvlist_add_string(tree, "physpath", vd->vdev_physpath);
		if (vd->vdev_enc_sysfs_path != NULL) {
			fnvlist_add_string(tree, "enc_sysfs_path",
			    vd->vdev_enc_sysfs_path);
		}
		fnvlist_add_string(tree, "state",
		    vdev_state_string(vd->vdev_state, vd->vdev_stat.vs_aux));
		/*
		 * Try for some of the extended status annotations that
		 * zpool status provides.
		 */
		fnvlist_add_boolean_value(tree, "vs_scan_removing",
		    vd->vdev_stat.vs_scan_removing != 0);
		fnvlist_add_boolean_value(tree, "vs_noalloc",
		    vd->vdev_stat.vs_noalloc != 0);
		fnvlist_add_boolean_value(tree, "vs_resilver_deferred",
		    vd->vdev_stat.vs_resilver_deferred);
		s = "none";
		if ((vd->vdev_state == VDEV_STATE_UNKNOWN) ||
		    (vd->vdev_state == VDEV_STATE_HEALTHY)) {
			if (vd->vdev_stat.vs_scan_processed != 0) {
				if (ps &&
				    (ps->pss_state == DSS_SCANNING)) {
					s = (ps->pss_func ==
					    POOL_SCAN_RESILVER) ?
					    "resilvering" : "repairing";
				} else if (ps &&
				    vd->vdev_stat.vs_resilver_deferred) {
					s = "awaiting resilver";
				}
			}
		}
		fnvlist_add_string(tree, "resilver_repair", s);

		init_state = fnvlist_alloc();
		s = "VDEV_INITIALIZE_NONE";
		if (vd->vdev_stat.vs_initialize_state == VDEV_INITIALIZE_ACTIVE)
			s = "VDEV_INITIALIZE_ACTIVE";
		if (vd->vdev_stat.vs_initialize_state ==
		    VDEV_INITIALIZE_SUSPENDED)
			s = "VDEV_INITIALIZE_SUSPENDED";
		if (vd->vdev_stat.vs_initialize_state ==
		    VDEV_INITIALIZE_COMPLETE)
			s = "VDEV_INITIALIZE_COMPLETE";
		fnvlist_add_string(init_state, "vs_initialize_state", s);
		fnvlist_add_uint64(init_state, "vs_initialize_bytes_done:",
		    vd->vdev_stat.vs_initialize_bytes_done);
		fnvlist_add_uint64(init_state, "vs_initialize_bytes_est",
		    vd->vdev_stat.vs_initialize_bytes_est);
		fnvlist_add_uint64(init_state, "vs_initialize_action_time",
		    vd->vdev_stat.vs_initialize_action_time);
		fnvlist_add_nvlist(tree, "initialize_state", init_state);
		fnvlist_free(init_state);

		trim_state = fnvlist_alloc();
		s = "VDEV_UNTRIMMED";
		if (vd->vdev_stat.vs_trim_state == VDEV_TRIM_ACTIVE)
			s = "VDEV_TRIM_ACTIVE";
		if (vd->vdev_stat.vs_trim_state == VDEV_TRIM_SUSPENDED)
			s = "VDEV_TRIM_SUSPENDED";
		if (vd->vdev_stat.vs_trim_state == VDEV_TRIM_COMPLETE)
			s = "VDEV_TRIM_COMPLETE";
		if (vd->vdev_stat.vs_trim_notsup)
			s = "VDEV_TRIM_UNSUPPORTED";
		fnvlist_add_string(trim_state, "vs_trim_state", s);
		if (!vd->vdev_stat.vs_trim_notsup) {
			fnvlist_add_uint64(trim_state, "vs_trim_action_time",
			    vd->vdev_stat.vs_trim_action_time);
			fnvlist_add_uint64(trim_state, "vs_trim_bytes_done",
			    vd->vdev_stat.vs_trim_bytes_done);
			fnvlist_add_uint64(trim_state, "vs_trim_bytes_est",
			    vd->vdev_stat.vs_trim_bytes_est);
		}
		fnvlist_add_nvlist(tree, "trim_state", trim_state);
		fnvlist_free(trim_state);

		fnvlist_add_uint64(tree, "read_errors",
		    vd->vdev_stat.vs_read_errors);
		fnvlist_add_uint64(tree, "write_errors",
		    vd->vdev_stat.vs_write_errors);
		fnvlist_add_uint64(tree, "checksum_errors",
		    vd->vdev_stat.vs_checksum_errors);
		fnvlist_add_uint64(tree, "slow_ios",
		    vd->vdev_stat.vs_slow_ios);
		fnvlist_add_uint64(tree, "trim_errors",
		    vd->vdev_stat.vs_trim_errors);
	}
	n = vd->vdev_children;
	a = vd->vdev_child;
	if (n != 0) {
		fnvlist_add_uint64(tree, "vdev_children", n);
		nvlist_t **ch = kmem_alloc(sizeof (nvlist_t *) * n, KM_NOSLEEP);
		for (uint64_t i = 0; i < n; ++i) {
			ch[i] = fnvlist_alloc();
			vdev_to_nvlist(a[i], ps, ch[i]);
		}
		fnvlist_add_nvlist_array(tree, "children",
		    (const nvlist_t * const *) ch, n);
		for (uint64_t i = 0; i < n; ++i)
			fnvlist_free(ch[i]);
		kmem_free(ch, sizeof (nvlist_t *) * n);
	}
}

static void
iterate_vdevs(spa_t *spa, pool_scan_stat_t *ps, nvlist_t *nvl)
{
	nvlist_t *vt = fnvlist_alloc();
	vdev_t *v = spa->spa_root_vdev;
	if (v == NULL) {
		zfs_dbgmsg("error: NO ROOT VDEV");
		return;
	}
	vdev_to_nvlist(v, ps, vt);
	int nspares = spa->spa_spares.sav_count;
	if (nspares != 0) {
		nvlist_t **sp = kmem_alloc(sizeof (nvlist_t *) * nspares,
		    KM_NOSLEEP);
		for (int i = 0; i < nspares; i++) {
			v = spa->spa_spares.sav_vdevs[i];
			sp[i] = fnvlist_alloc();
			vdev_to_nvlist(v, ps, sp[i]);
		}
		fnvlist_add_nvlist_array(vt, ZPOOL_CONFIG_SPARES,
		    (const nvlist_t * const *) sp, nspares);
		for (int i = 0; i < nspares; i++)
			fnvlist_free(sp[i]);
		kmem_free(sp, sizeof (nvlist_t *) * nspares);
	}
	int nl2cache = spa->spa_l2cache.sav_count;
	if (nl2cache != 0) {
		nvlist_t **l2 = kmem_alloc(sizeof (nvlist_t *) * nl2cache,
		    KM_NOSLEEP);
		for (int i = 0; i < nl2cache; i++) {
			v = spa->spa_l2cache.sav_vdevs[i];
			l2[i] = fnvlist_alloc();
			vdev_to_nvlist(v, ps, l2[i]);
		}
		fnvlist_add_nvlist_array(vt, ZPOOL_CONFIG_L2CACHE,
		    (const nvlist_t * const *) l2, nl2cache);
		for (int i = 0; i < nspares; i++)
			fnvlist_free(l2[i]);
		kmem_free(l2, sizeof (nvlist_t *) * nl2cache);
	}
	fnvlist_add_nvlist(nvl, "vdev_tree", vt);
	fnvlist_free(vt);
}

static const char *
pss_func_to_string(uint64_t n)
{
	const char *s = "?";
	switch (n) {
		case POOL_SCAN_NONE:		s = "NONE";	break;
		case POOL_SCAN_SCRUB:		s = "SCRUB";	break;
		case POOL_SCAN_RESILVER:	s = "RESILVER";	break;
		case POOL_SCAN_FUNCS:		s = "?";
	}
	return (s);
}

static const char *pss_state_to_string(uint64_t n)
{
	const char *s = "?";
	switch (n) {
		case DSS_NONE:		s = "NONE";	break;
		case DSS_SCANNING:	s = "SCANNING";	break;
		case DSS_FINISHED:	s = "FINISHED";	break;
		case DSS_CANCELED:	s = "CANCELED";	break;
		case DSS_NUM_STATES:	s = "?";
	}
	return (s);
}

static int
spa_props_json(spa_t *spa, nvlist_t **nvl)
{
	nvpair_t *curr = NULL, *item = NULL;
	nvlist_t *prop;
	data_type_t type;
	char buf[256];
	const char *name;
	uint64_t src;

	if (spa_prop_get(spa, nvl) != 0)
		return (-1);

	for (curr = nvlist_next_nvpair(*nvl, NULL); curr;
	    curr = nvlist_next_nvpair(*nvl, curr)) {
		if (nvpair_type(curr) == DATA_TYPE_NVLIST) {
			prop = fnvpair_value_nvlist(curr);
			for (item = nvlist_next_nvpair(prop, NULL); item;
			    item = nvlist_next_nvpair(prop, item)) {
				name = nvpair_name(item);
				type = nvpair_type(item);
				if ((strcmp(name, "source") == 0) &&
				    (type == DATA_TYPE_UINT64)) {
					src = fnvpair_value_uint64(item);
					memset(buf, 0, 256);
					if (src & ZPROP_SRC_NONE) {
						if (buf[0] != '\0')
							strcat(buf, "|");
						strcat(buf, "ZPROP_SRC_NONE");
					}
					if (src & ZPROP_SRC_DEFAULT) {
						if (buf[0] != '\0')
							strcat(buf, "|");
						strcat(buf,
						    "ZPROP_SRC_DEFAULT");
					}
					if (src & ZPROP_SRC_TEMPORARY) {
						if (buf[0] != '\0')
							strcat(buf, "|");
						strcat(buf,
						    "ZPROP_SRC_TEMPORARY");
					}
					if (src & ZPROP_SRC_INHERITED) {
						if (buf[0] != '\0')
							strcat(buf, "|");
						strcat(buf,
						    "ZPROP_SRC_INHERITED");
					}
					if (src & ZPROP_SRC_RECEIVED) {
						if (buf[0] != '\0')
							strcat(buf, "|");
						strcat(buf,
						    "ZPROP_SRC_RECEIVED");
					}
					fnvlist_add_string(prop, "source", buf);
				}
			}
		}
	}
	return (0);
}

/*
 * Collect the spa status without any locking and return as a JSON string.
 *
 * Currently used by the 'zfs/<pool>/stats.json' kstat.
 */
int
spa_generate_json_stats(spa_t *spa, char *buf, size_t size)
{
	int error = 0;
	int ps_error = 0;
	char *curr = buf;
	nvlist_t *spa_config, *spa_props = NULL, *scan_stats, *nvl;
	uint64_t loadtimes[2];
	pool_scan_stat_t ps;
	int scl_config_lock;

	nvl = fnvlist_alloc();
	if (nvlist_dup(spa->spa_config, &spa_config, 0) != 0) {
		zfs_dbgmsg("json_data: nvlist_dup failed");
		return (0);
	}
	fnvlist_add_nvlist(spa_config, ZPOOL_CONFIG_LOAD_INFO,
	    spa->spa_load_info);

	scl_config_lock =
	    spa_config_tryenter(spa, SCL_CONFIG, FTAG, RW_READER);

	ps_error = spa_scan_get_stats(spa, &ps);
	(void) ps_error;

	if (spa_props_json(spa, &spa_props) == 0)
		fnvlist_add_nvlist(spa_config, "spa_props", spa_props);

	loadtimes[0] = spa->spa_loaded_ts.tv_sec;
	loadtimes[1] = spa->spa_loaded_ts.tv_nsec;
	fnvlist_add_uint64_array(spa_config, ZPOOL_CONFIG_LOADED_TIME,
	    loadtimes, 2);
	fnvlist_add_uint64(spa_config, ZPOOL_CONFIG_ERRCOUNT,
	    spa_approx_errlog_size(spa));
	fnvlist_add_boolean_value(spa_config, ZPOOL_CONFIG_SUSPENDED,
	    spa_suspended(spa));
	if (spa_suspended(spa)) {
		const char *failmode;
		switch (spa->spa_failmode) {
		case ZIO_FAILURE_MODE_WAIT:
			failmode = "wait";
			break;
		case ZIO_FAILURE_MODE_CONTINUE:
			failmode = "continue";
			break;
		case ZIO_FAILURE_MODE_PANIC:
			failmode = "panic";
			break;
		default:
			failmode = "???";
		}
		fnvlist_add_string(spa_config, "failmode", failmode);
		if (spa->spa_suspended != ZIO_SUSPEND_NONE) {
			fnvlist_add_string(spa_config,
			    ZPOOL_CONFIG_SUSPENDED_REASON,
			    (spa->spa_suspended == ZIO_SUSPEND_MMP) ?
			    "MMP" : "IO");
		}
	}

	fnvlist_add_uint32(nvl, "status_json_version", JSON_STATUS_VERSION);
	fnvlist_add_boolean_value(nvl, "scl_config_lock", scl_config_lock != 0);
	fnvlist_add_uint32(nvl, "scan_error", ps_error);

	scan_stats = fnvlist_alloc();
	if (ps_error == 0) {
		fnvlist_add_string(scan_stats, "func",
		    pss_func_to_string(ps.pss_func));
		fnvlist_add_string(scan_stats, "state",
		    pss_state_to_string(ps.pss_state));
		fnvlist_add_uint64(scan_stats, "start_time", ps.pss_start_time);
		fnvlist_add_uint64(scan_stats, "end_time", ps.pss_end_time);
		fnvlist_add_uint64(scan_stats, "to_examine", ps.pss_to_examine);
		fnvlist_add_uint64(scan_stats, "examined", ps.pss_examined);
		fnvlist_add_uint64(scan_stats, "processed", ps.pss_processed);
		fnvlist_add_uint64(scan_stats, "errors", ps.pss_errors);
		fnvlist_add_uint64(scan_stats, "pass_exam", ps.pss_pass_exam);
		fnvlist_add_uint64(scan_stats, "pass_start", ps.pss_pass_start);
		fnvlist_add_uint64(scan_stats, "pass_scrub_pause",
		    ps.pss_pass_scrub_pause);
		fnvlist_add_uint64(scan_stats, "pass_scrub_spent_paused",
		    ps.pss_pass_scrub_spent_paused);
		fnvlist_add_uint64(scan_stats, "pass_issued",
		    ps.pss_pass_issued);
		fnvlist_add_uint64(scan_stats, "issued", ps.pss_issued);
	} else if (ps_error == ENOENT) {
		fnvlist_add_string(scan_stats, "func", "NONE");
		fnvlist_add_string(scan_stats, "state", "NONE");
	} else {
		fnvlist_add_string(scan_stats, "func", "NONE");
		fnvlist_add_string(scan_stats, "state", "NONE");
	}
	fnvlist_add_nvlist(nvl, "scan_stats", scan_stats);
	fnvlist_add_string(nvl, "state", spa_state_to_name(spa));

	fnvlist_remove(spa_config, "state");
	spa_add_spares(spa, spa_config);
	spa_add_l2cache(spa, spa_config);
	spa_add_feature_stats(spa, spa_config);

	/* add spa_config to output nvlist */
	fnvlist_merge(nvl, spa_config);
	iterate_vdevs(spa, &ps, nvl);

	if (scl_config_lock)
		spa_config_exit(spa, SCL_CONFIG, FTAG);

	error = nvlist_to_json(nvl, &curr, size);
	nvlist_free(nvl);
	nvlist_free(spa_config);
	nvlist_free(spa_props);
	nvlist_free(scan_stats);

	return (error);
}
