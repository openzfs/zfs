/*
 * json_stats.c
 *
 */

#include <sys/zfs_context.h>
#include <sys/spa_impl.h>
#include <sys/vdev_impl.h>
#include <sys/spa.h>
#include <zfs_comutil.h>
#include <sys/jprint.h>
#include <sys/json_stats.h>
#include <sys/nvpair_impl.h>

/* */
void json_stats_destroy(spa_t *spa)
{
	spa_history_kstat_t *shk = &spa->spa_json_stats.kstat;
	kstat_t *ksp = shk->kstat;

	if (ksp) {
		if (ksp->ks_data)
			kmem_free(ksp->ks_data, sizeof (spa_iostats_t));
		kstat_delete(ksp);
	}
	mutex_destroy(&shk->lock);
}

/*
 * Return string for datatype -- this guides us in implementing
 * json translation. This is only because I find it easier to read...
 */
static const char *datatype_string(data_type_t t) {
	switch (t) {
	case DATA_TYPE_UNKNOWN:       return "DATA_TYPE_UNKNOWN";
	case DATA_TYPE_BOOLEAN:       return "DATA_TYPE_BOOLEAN";
	case DATA_TYPE_BYTE:          return "DATA_TYPE_BYTE";
	case DATA_TYPE_INT16:         return "DATA_TYPE_INT16";
	case DATA_TYPE_UINT16:        return "DATA_TYPE_UINT16";
	case DATA_TYPE_INT32:         return "DATA_TYPE_INT32";
	case DATA_TYPE_UINT32:        return "DATA_TYPE_UINT32";
	case DATA_TYPE_INT64:         return "DATA_TYPE_INT64";
	case DATA_TYPE_UINT64:        return "DATA_TYPE_UINT64";
	case DATA_TYPE_STRING:        return "DATA_TYPE_STRING";
	case DATA_TYPE_BYTE_ARRAY:    return "DATA_TYPE_BYTE_ARRAY";
	case DATA_TYPE_INT16_ARRAY:   return "DATA_TYPE_INT16_ARRAY";
	case DATA_TYPE_UINT16_ARRAY:  return "DATA_TYPE_UINT16_ARRAY";
	case DATA_TYPE_INT32_ARRAY:   return "DATA_TYPE_INT32_ARRAY";
	case DATA_TYPE_UINT32_ARRAY:  return "DATA_TYPE_UINT32_ARRAY";
	case DATA_TYPE_INT64_ARRAY:   return "DATA_TYPE_INT64_ARRAY";
	case DATA_TYPE_UINT64_ARRAY:  return "DATA_TYPE_UINT64_ARRAY";
	case DATA_TYPE_STRING_ARRAY:  return "DATA_TYPE_STRING_ARRAY";
	case DATA_TYPE_HRTIME:        return "DATA_TYPE_HRTIME";
	case DATA_TYPE_NVLIST:        return "DATA_TYPE_NVLIST";
	case DATA_TYPE_NVLIST_ARRAY:  return "DATA_TYPE_NVLIST_ARRAY";
	case DATA_TYPE_BOOLEAN_VALUE: return "DATA_TYPE_BOOLEAN_VALUE";
	case DATA_TYPE_INT8:          return "DATA_TYPE_INT8";
	case DATA_TYPE_UINT8:         return "DATA_TYPE_UINT8";
	case DATA_TYPE_BOOLEAN_ARRAY: return "DATA_TYPE_BOOLEAN_ARRAY";
	case DATA_TYPE_INT8_ARRAY:    return "DATA_TYPE_INT8_ARRAY";
	case DATA_TYPE_UINT8_ARRAY:   return "DATA_TYPE_UINT8_ARRAY";
	default:                      return "UNKNOWN";
	}
}

/*
 * This code is NOT abstracted -- just some duplication, until I
 * actually understand what is needed.
 */
static void nvlist_to_json(nvlist_t *nvl, jprint_t *jp) {
	const nvpriv_t *priv;
	const i_nvp_t *curr;

	if ((priv = (const nvpriv_t *)(uintptr_t)nvl->nvl_priv) == NULL)
		return;

	for (curr = priv->nvp_list; curr != NULL; curr = curr->nvi_next) {
		const nvpair_t *nvp = &curr->nvi_nvp;
		const char *name = (const char *)NVP_NAME(nvp);
		data_type_t type = NVP_TYPE(nvp);
		void *p = NVP_VALUE(nvp);
		uint64_t *u;
		nvlist_t **a;

		if (jp_error(jp) != JPRINT_OK)
			return;

		switch (type) {

		/*
		 * Array types
		 */
		case DATA_TYPE_UINT64_ARRAY:
			u = (uint64_t *)p;
			jp_printf(jp, "%k: [", name);
			for (int i = 0; i < NVP_NELEM(nvp); ++i) {
				if (jp_error(jp) != JPRINT_OK)
					break;
				jp_printf(jp, "%U", u[i]);
			}
			jp_printf(jp, "]");
			break;
		case DATA_TYPE_NVLIST_ARRAY:
			a = (nvlist_t **)p;
			jp_printf(jp, "%k: [", name);
			for (int i = 0; i < NVP_NELEM(nvp); ++i)  {
				if (jp_error(jp) != JPRINT_OK)
					break;
				jp_printf(jp, "{");
				nvlist_to_json(a[i], jp);
				jp_printf(jp, "}");
			}
			jp_printf(jp, "]");
			break;
		/*
		 * Primitive types
		 */
		case DATA_TYPE_UINT64:
			jp_printf(jp, "%k: %U", name, *(uint64_t *)p);
			break;
		case DATA_TYPE_INT64:
			jp_printf(jp, "%k: %D", name, *(int64_t *)p);
			break;
		case DATA_TYPE_STRING:
			jp_printf(jp, "%k: %s", name, (char *)p);
			break;
		case DATA_TYPE_BOOLEAN:
			jp_printf(jp, "%k: %b", name, B_TRUE);
			break;
		case DATA_TYPE_BOOLEAN_VALUE:
			jp_printf(jp, "%k: %b", name, *(boolean_t *)p);
			break;

		/*
		 * Object types
		 */
		case DATA_TYPE_NVLIST:
			/*
			 * We are going to suppress vdev_tree, and generate
			 * later -- needs fixing... quick hack for wasabi
			 */
			if ((jp->stackp == 0) && (strcmp(name, "vdev_tree") == 0))
				break;
			jp_printf(jp, "%k: {", name);
			nvlist_to_json((nvlist_t *)p , jp);
			jp_printf(jp, "}");
			break;

		/*
		 * Default -- tell us what we are missing
		 */
		default:
			jp_printf(jp, "%k: %s", name, datatype_string(type));
			zfs_dbgmsg("name = %s type = %d %s", name,
			    (int)type, datatype_string(type));
		break;
		}
	}
}

static void vdev_to_json(vdev_t *v, pool_scan_stat_t *ps, jprint_t *jp)
{
	uint64_t i, n;
	vdev_t **a;
	const char *s;
	jp_printf(jp, "type: %s", v->vdev_ops->vdev_op_type);
	jp_printf(jp, "id: %U", v->vdev_id);
	jp_printf(jp, "guid: %U", v->vdev_guid);
	if (strcmp(v->vdev_ops->vdev_op_type, "root") != 0) {
		jp_printf(jp, "asize: %U", v->vdev_asize);
		jp_printf(jp, "ashift: %U", v->vdev_ashift);
		jp_printf(jp, "whole_disk: %b",
		    (v->vdev_wholedisk == 0) ? B_FALSE : B_TRUE);
		jp_printf(jp, "offline: %b",
		    (v->vdev_offline == 0) ? B_FALSE : B_TRUE);
		jp_printf(jp, "faulted: %b",
		    (v->vdev_faulted == 0) ? B_FALSE : B_TRUE);
		jp_printf(jp, "degraded: %b",
		    (v->vdev_degraded == 0) ? B_FALSE : B_TRUE);
		jp_printf(jp, "removed: %b",
		    (v->vdev_removed == 0) ? B_FALSE : B_TRUE);
		jp_printf(jp, "not_present: %b",
		    (v->vdev_not_present == 0) ? B_FALSE : B_TRUE);
		jp_printf(jp, "is_log: %b",
		    (v->vdev_islog == 0) ? B_FALSE : B_TRUE);

		jp_printf(jp, "path: %s", v->vdev_path);
		if (v->vdev_devid != NULL)
			jp_printf(jp, "devid: %s",  v->vdev_devid);
		if (v->vdev_physpath != NULL)
			jp_printf(jp, "physpath: %s", v->vdev_physpath);
		if (v->vdev_enc_sysfs_path != NULL)
			jp_printf(jp, "enc_sysfs_path: %s",
			    v->vdev_enc_sysfs_path);
		switch (v->vdev_stat.vs_state) {
		case VDEV_STATE_UNKNOWN:   s = "HEALTHY";    break;
		case VDEV_STATE_CLOSED:    s = "CLOSED";     break;
		case VDEV_STATE_OFFLINE:   s = "OFFLINE";    break;
		case VDEV_STATE_REMOVED:   s = "REMOVED";    break;
		case VDEV_STATE_CANT_OPEN: s = "CAN'T OPEN"; break;
		case VDEV_STATE_FAULTED:   s = "FAULTED";    break;
		case VDEV_STATE_DEGRADED:  s = "DEGRADED";   break;
		case VDEV_STATE_HEALTHY:   s = "HEALTHY";    break;
		default:                   s = "?";
		}
		jp_printf(jp, "state: %s", s);
		/* Try for some of the extended status annotations that
		 * zpool status provides.
		 */
		/* (removing) */
		jp_printf(jp, "vs_scan_removing: %b",
		    v->vdev_stat.vs_scan_removing != 0);
#if 0 /* XXX: The vs_noalloc member is not available in ZFS 2.1.5. */
		/* (non-allocating) */
		jp_printf(jp, "vs_noalloc: %b",
		    v->vdev_stat.vs_noalloc != 0);
#endif
		/* (awaiting resilver) */
		jp_printf(jp, "vs_resilver_deferred: %b",
		    v->vdev_stat.vs_resilver_deferred);
		s = "none";
		if ((v->vdev_state == VDEV_STATE_UNKNOWN) ||
	            (v->vdev_state == VDEV_STATE_HEALTHY)) {
			if (v->vdev_stat.vs_scan_processed != 0) {
				if (ps &&
				    (ps->pss_state == DSS_SCANNING)) {
					s = (ps->pss_func == POOL_SCAN_RESILVER) ?
					    "resilvering" : "repairing";
				} else if (ps &&
				    v->vdev_stat.vs_resilver_deferred) {
					s = "awaiting resilver";
				}
			}
		}
		jp_printf(jp, "resilver_repair: %s", s);
		jp_printf(jp, "initialize_state: {");
		s = "VDEV_INITIALIZE_NONE";
		if (v->vdev_stat.vs_initialize_state == VDEV_INITIALIZE_ACTIVE)
			s = "VDEV_INITIALIZE_ACTIVE";
		if (v->vdev_stat.vs_initialize_state == VDEV_INITIALIZE_SUSPENDED)
			s = "VDEV_INITIALIZE_SUSPENDED";
		if (v->vdev_stat.vs_initialize_state == VDEV_INITIALIZE_COMPLETE)
			s = "VDEV_INITIALIZE_COMPLETE";
		jp_printf(jp, "vs_initialize_state: %s", s);
		jp_printf(jp, "vs_initialize_bytes_done: %U",
		    v->vdev_stat.vs_initialize_bytes_done);
		jp_printf(jp, "vs_initialize_bytes_est: %U",
		    v->vdev_stat.vs_initialize_bytes_est);
		jp_printf(jp, "vs_initialize_action_time: %U",
		    v->vdev_stat.vs_initialize_action_time);
		jp_printf(jp, "}");

		jp_printf(jp, "trim_state: {");
		s = "VDEV_UNTRIMMED";
		if (v->vdev_stat.vs_trim_state == VDEV_TRIM_ACTIVE)
			s = "VDEV_TRIM_ACTIVE";
		if (v->vdev_stat.vs_trim_state == VDEV_TRIM_SUSPENDED)
			s = "VDEV_TRIM_SUSPENDED";
		if (v->vdev_stat.vs_trim_state == VDEV_TRIM_COMPLETE)
			s = "VDEV_TRIM_COMPLETE";
		if (v->vdev_stat.vs_trim_notsup)
			s = "VDEV_TRIM_UNSUPPORTED";
		jp_printf(jp, "vs_trim_state: %s", s);
		if (!v->vdev_stat.vs_trim_notsup) {
			jp_printf(jp, "vs_trim_action_time: %U",
			    v->vdev_stat.vs_trim_action_time);
			jp_printf(jp, "vs_trim_bytes_done: %U",
			    v->vdev_stat.vs_trim_bytes_done);
			jp_printf(jp, "vs_trim_bytes_est: %U",
			    v->vdev_stat.vs_trim_bytes_est);
		}
		jp_printf(jp, "}");

		jp_printf(jp, "read_errors: %U",
		    v->vdev_stat.vs_read_errors);
		jp_printf(jp, "write_errors: %U",
		    v->vdev_stat.vs_write_errors);
		jp_printf(jp, "checksum_errors: %U",
		    v->vdev_stat.vs_checksum_errors);
		jp_printf(jp, "slow_ios: %U",
		    v->vdev_stat.vs_slow_ios);
		jp_printf(jp, "trim_errors: %U",
		    v->vdev_stat.vs_trim_errors);
	}
	/*
	 * Please note that children of children will translate to
	 * json... we will not put out the number, but that is not
	 * needed in json anyway.
	 */
	n = v->vdev_children;
	a = v->vdev_child;
	if (n != 0) {
		jp_printf(jp, "children: [");
		for (i = 0; i < n; ++i) {
			jp_printf(jp, "{");
			vdev_to_json(a[i], ps, jp);
			jp_printf(jp, "}");
		}
		jp_printf(jp, "]");
	}
}

static void iterate_vdevs(spa_t *spa, pool_scan_stat_t *ps, jprint_t *jp)
{
	vdev_t *v = spa->spa_root_vdev;
	if (v == NULL) {
		jp_printf(jp, "error: %s", "NO ROOT VDEV");
		return;
	}
	jp_printf(jp, "vdev_tree: {");
	vdev_to_json(v, ps, jp);
	jp_printf(jp, "}");
}

static int json_data(char *buf, size_t size, void *data) {
	spa_t *spa = (spa_t *)data;
	int error = 0;
	int ps_error = 0;
	jprint_t jp;
	nvlist_t *nvl, *pnvl;
	uint64_t loadtimes[2];
	pool_scan_stat_t ps;
	const char *s = NULL;

	if (nvlist_dup(spa->spa_config, &nvl, 0) != 0) {
		/*
		 * FIXME, what to do here?!?
		 */
		zfs_dbgmsg("json_data: nvlist_dup failed");
		return (0);
	}
	fnvlist_add_nvlist(nvl, ZPOOL_CONFIG_LOAD_INFO, spa->spa_load_info);
	    
	spa_config_enter(spa, SCL_CONFIG, FTAG, RW_READER);

	ps_error = spa_scan_get_stats(spa, &ps);

	error = spa_prop_get(spa, &pnvl);
	fnvlist_add_nvlist(nvl, "spa_props", pnvl);

	loadtimes[0] = spa->spa_loaded_ts.tv_sec;
	loadtimes[1] = spa->spa_loaded_ts.tv_nsec;
	fnvlist_add_uint64_array(nvl, ZPOOL_CONFIG_LOADED_TIME, loadtimes, 2);
	fnvlist_add_uint64(nvl, ZPOOL_CONFIG_ERRCOUNT,
	    spa_get_errlog_size(spa));
	fnvlist_add_boolean_value(nvl, "is_suspended", spa_suspended(spa));
	fnvlist_add_uint64(nvl, ZPOOL_CONFIG_SUSPENDED, spa->spa_failmode);
	fnvlist_add_uint64(nvl, ZPOOL_CONFIG_SUSPENDED_REASON,
	    spa->spa_suspended);

	jp_open(&jp, buf, size); 
	jp_printf(&jp, "{");

	jp_printf(&jp, "status_json_version: %d", 2);
	jp_printf(&jp, "scan_error: %d", ps_error);
	jp_printf(&jp, "scan_stats: {");
	if (ps_error == 0) {
		switch (ps.pss_func) {
		case POOL_SCAN_NONE:     s = "NONE";     break;
		case POOL_SCAN_SCRUB:    s = "SCRUB";    break;
		case POOL_SCAN_RESILVER: s = "RESILVER"; break;
		case POOL_SCAN_FUNCS:    s = "?";
		}
		jp_printf(&jp, "func: %s", s);
		switch (ps.pss_state) {
		case DSS_NONE:       s = "NONE";     break;
		case DSS_SCANNING:   s = "SCANNING"; break;
		case DSS_FINISHED:   s = "FINISHED"; break;
		case DSS_CANCELED:   s = "CANCELED"; break;
		case DSS_NUM_STATES: s = "?";
		}
		jp_printf(&jp, "state: %s", s);
		jp_printf(&jp, "start_time: %U", ps.pss_start_time);
		jp_printf(&jp, "end_time: %U", ps.pss_end_time);
		jp_printf(&jp, "to_examine: %U", ps.pss_to_examine);
		jp_printf(&jp, "examined: %U", ps.pss_examined);
		jp_printf(&jp, "processed: %U", ps.pss_processed);
		jp_printf(&jp, "errors: %U", ps.pss_errors);

		jp_printf(&jp, "pass_exam: %U", ps.pss_pass_exam);
		jp_printf(&jp, "pass_start: %U", ps.pss_pass_start);
		jp_printf(&jp, "pass_scrub_pause: %U", ps.pss_pass_scrub_pause);
		jp_printf(&jp, "pass_scrub_spent_paused: %U",
		    ps.pss_pass_scrub_spent_paused);
		jp_printf(&jp, "pass_issued: %U", ps.pss_pass_issued);
		jp_printf(&jp, "issued: %U", ps.pss_issued);
	} else if (ps_error == ENOENT) {
		jp_printf(&jp, "func: %s", "NONE");
		jp_printf(&jp, "state: %s", "NONE");
	} else {
		jp_printf(&jp, "func: %s", "?");
		jp_printf(&jp, "state: %s", "?");
	}
	jp_printf(&jp, "}");
	s = "?";
	switch (spa->spa_state) {
	case POOL_STATE_ACTIVE:             s = "ACTIVE"; break;
	case POOL_STATE_EXPORTED:           s = "EXPORTED"; break;
	case POOL_STATE_DESTROYED:          s = "DESTROYED"; break;
	case POOL_STATE_SPARE:              s = "SPARE"; break;
	case POOL_STATE_L2CACHE:            s = "L2CACHE"; break;
	case POOL_STATE_UNINITIALIZED:      s = "UNINITIALIZED"; break;
	case POOL_STATE_UNAVAIL:            s = "UNAVAIL"; break;
	case POOL_STATE_POTENTIALLY_ACTIVE: s = "POTENTIALLY ACTIVE"; break;
	}
	jp_printf(&jp, "pool_state: %s", s);

	spa_add_spares(spa, nvl);
	spa_add_l2cache(spa, nvl);
	spa_add_feature_stats(spa, nvl);

	/* iterate and transfer nvl to json */
	nvlist_to_json(nvl, &jp);

	iterate_vdevs(spa, &ps, &jp);

	/*
	 * close the root object
	 */
	jp_printf(&jp, "}");

	spa_config_exit(spa, SCL_CONFIG, FTAG);
	nvlist_free(nvl);

        error = jp_close(&jp);
	if (error == JPRINT_BUF_FULL) {
		 error = SET_ERROR(ENOMEM);
	} else if (error != 0) {
		/*
		 * Another error from jprint, format an error message
		 * but this is not ever to happen (this would be a
		 * defect elsewhere).
		 *
		 * If this does happen, we simply put the string where
		 * the json should go... this is expected to trigger
		 * a json decode error, and report "upstream"
		 */
		snprintf(buf, size,
		    "jprint error %s (%d) callno %d, size %ld\n",
		    jp_errorstring(error), error, jp_errorpos(&jp), size);
		error = 0;
	}
	return (error);
}


static void *json_addr(kstat_t *ksp, loff_t n)
{
	if (n == 0)
	    return (ksp->ks_private);
	return (NULL);
}


void json_stats_init(spa_t *spa)
{
	spa_history_kstat_t *shk = &spa->spa_json_stats.kstat;
	char *name;
	kstat_t *ksp;

	mutex_init(&shk->lock, NULL, MUTEX_DEFAULT, NULL);
	name = kmem_asprintf("zfs/%s", spa_name(spa));
	ksp = kstat_create(name, 0, "status.json", "misc",
	    KSTAT_TYPE_RAW, 0, KSTAT_FLAG_VIRTUAL);
	shk->kstat = ksp;
	if (ksp) {
		ksp->ks_lock = &shk->lock;
		ksp->ks_data = NULL;
		ksp->ks_private = spa;
		ksp->ks_flags |= KSTAT_FLAG_NO_HEADERS;
		kstat_set_raw_ops(ksp, NULL, json_data, json_addr);
		kstat_install(ksp);
	}

	kmem_strfree(name);
}

