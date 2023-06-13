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

#define	JSON_STATUS_VERSION	4

void
json_stats_destroy(spa_t *spa)
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
static const char *
datatype_string(data_type_t t)
{
	switch (t) {
	case DATA_TYPE_UNKNOWN:		return "DATA_TYPE_UNKNOWN";
	case DATA_TYPE_BOOLEAN:		return "DATA_TYPE_BOOLEAN";
	case DATA_TYPE_BYTE:		return "DATA_TYPE_BYTE";
	case DATA_TYPE_INT16:		return "DATA_TYPE_INT16";
	case DATA_TYPE_UINT16:		return "DATA_TYPE_UINT16";
	case DATA_TYPE_INT32:		return "DATA_TYPE_INT32";
	case DATA_TYPE_UINT32:		return "DATA_TYPE_UINT32";
	case DATA_TYPE_INT64:		return "DATA_TYPE_INT64";
	case DATA_TYPE_UINT64:		return "DATA_TYPE_UINT64";
	case DATA_TYPE_STRING:		return "DATA_TYPE_STRING";
	case DATA_TYPE_BYTE_ARRAY:	return "DATA_TYPE_BYTE_ARRAY";
	case DATA_TYPE_INT16_ARRAY:	return "DATA_TYPE_INT16_ARRAY";
	case DATA_TYPE_UINT16_ARRAY:	return "DATA_TYPE_UINT16_ARRAY";
	case DATA_TYPE_INT32_ARRAY:	return "DATA_TYPE_INT32_ARRAY";
	case DATA_TYPE_UINT32_ARRAY:	return "DATA_TYPE_UINT32_ARRAY";
	case DATA_TYPE_INT64_ARRAY:	return "DATA_TYPE_INT64_ARRAY";
	case DATA_TYPE_UINT64_ARRAY:	return "DATA_TYPE_UINT64_ARRAY";
	case DATA_TYPE_STRING_ARRAY:	return "DATA_TYPE_STRING_ARRAY";
	case DATA_TYPE_HRTIME:		return "DATA_TYPE_HRTIME";
	case DATA_TYPE_NVLIST:		return "DATA_TYPE_NVLIST";
	case DATA_TYPE_NVLIST_ARRAY:	return "DATA_TYPE_NVLIST_ARRAY";
	case DATA_TYPE_BOOLEAN_VALUE:	return "DATA_TYPE_BOOLEAN_VALUE";
	case DATA_TYPE_INT8:		return "DATA_TYPE_INT8";
	case DATA_TYPE_UINT8:		return "DATA_TYPE_UINT8";
	case DATA_TYPE_BOOLEAN_ARRAY:	return "DATA_TYPE_BOOLEAN_ARRAY";
	case DATA_TYPE_INT8_ARRAY:	return "DATA_TYPE_INT8_ARRAY";
	case DATA_TYPE_UINT8_ARRAY:	return "DATA_TYPE_UINT8_ARRAY";
	default:			return "UNKNOWN";
	}
}

/*
 * nvlist_to_json takes a filter function. If the functions returns
 * B_TRUE, the case has been handled. If it returns B_FALSE, the
 * case has not been handled, and will be handled. Invoking nvlist_to_json
 * with a NULL filter chooses the default filter, which does nothing.
 *
 * The filtering is passed the jprint_t in case the nesting level is
 * important, name, data type and value.
 */
typedef boolean_t nvj_filter_t(jprint_t *, const char *, data_type_t, void *);

static boolean_t
null_filter(jprint_t *jp, const char *name, data_type_t type, void *value)
{
	jp = jp; name = name; type = type; value = value;
	return (B_FALSE);
}

static void nvlist_to_json(nvlist_t *nvl, jprint_t *jp, nvj_filter_t f);

/*
 * Convert source (src) to string -- up to 105 characters, so pass in 256
 * byte buffer (for future)
 */
static void
source_to_string(uint64_t src, char *buf)
{
	buf[0] = '\0';
	if (src & ZPROP_SRC_NONE) {
		if (buf[0] != '\0')
			strcat(buf, "|");
		strcat(buf, "ZPROP_SRC_NONE");
	}
	if (src & ZPROP_SRC_DEFAULT) {
		if (buf[0] != '\0')
			strcat(buf, "|");
		strcat(buf, "ZPROP_SRC_DEFAULT");
	}
	if (src & ZPROP_SRC_TEMPORARY) {
		if (buf[0] != '\0')
			strcat(buf, "|");
		strcat(buf, "ZPROP_SRC_TEMPORARY");
	}
	if (src & ZPROP_SRC_INHERITED) {
		if (buf[0] != '\0')
			strcat(buf, "|");
		strcat(buf, "ZPROP_SRC_INHERITED");
	}
	if (src & ZPROP_SRC_RECEIVED) {
		if (buf[0] != '\0')
			strcat(buf, "|");
		strcat(buf, "ZPROP_SRC_RECEIVED");
	}
}

/*
 * spa_props_filter replace source: with string. The way source is
 * defined it could be bitmap -- so generate the | sequence as
 * needed.
 */
static boolean_t
spa_props_filter(jprint_t *jp, const char *name, data_type_t type, void *value)
{
	if ((strcmp(name, "source") == 0) &&
	    (type == DATA_TYPE_UINT64)) {
		uint64_t src = *(uint64_t *)value;
		char buf[256];
		source_to_string(src, buf);
		jp_printf(jp, "source: %s", buf);
		return (B_TRUE);
	}
	return (B_FALSE);
}

/*
 * stats_filter removes parts of the nvlist we don't want to visit.
 */
static boolean_t
stats_filter(jprint_t *jp, const char *name, data_type_t type, void *value)
{
	/*
	 * Suppress root object state:
	 */
	if ((jp->stackp == 0) &&
	    (type == DATA_TYPE_UINT64) &&
	    (strcmp(name, "state") == 0))
		return (B_TRUE);

	/*
	 * Suppress root object vdev_children: -- we will
	 * output at one level down.
	 */
	if ((jp->stackp == 0) &&
	    (type == DATA_TYPE_UINT64) &&
	    (strcmp(name, "vdev_children") == 0))
		return (B_TRUE);

	/*
	 * We are going to suppress vdev_tree:, and generate the
	 * data our rselves.
	 * It does seem like a bit of a waste going through this
	 * twice... but for now, this seems prudent.
	 */
	if ((jp->stackp == 0) &&
	    (type == DATA_TYPE_NVLIST) &&
	    (strcmp(name, "vdev_tree") == 0))
		return (B_TRUE);

	/*
	 * Process spa_props:. Here we recurse, but with a filter that
	 * modifies source.
	 */
	if ((jp->stackp == 0) &&
	    (type == DATA_TYPE_NVLIST) &&
	    (strcmp(name, "spa_props") == 0)) {
		jp_printf(jp, "spa_props: {");
		nvlist_to_json((nvlist_t *)value, jp, spa_props_filter);
		jp_printf(jp, "}");
		return (B_TRUE);
	}

	return (B_FALSE);
}

/*
 * This code is NOT hightly abstracted -- just some duplication, until I
 * actually understand what is needed.
 *
 * In avoiding early abstraction, we find the need for a "filter". Which
 * is now implemented. This does appear in the spirit of other ZFS
 * coding.
 */
static void
nvlist_to_json(nvlist_t *nvl, jprint_t *jp, nvj_filter_t f)
{
	const nvpriv_t *priv;
	const i_nvp_t *curr;
	uint64_t *u = NULL;
	nvlist_t **a = NULL;

	if (f == NULL)
		f = null_filter;

	if ((priv = (const nvpriv_t *)(uintptr_t)nvl->nvl_priv) == NULL)
		return;

	for (curr = priv->nvp_list; curr != NULL; curr = curr->nvi_next) {
		const nvpair_t *nvp = &curr->nvi_nvp;
		const char *name = (const char *)NVP_NAME(nvp);
		data_type_t type = NVP_TYPE(nvp);
		void *p = NVP_VALUE(nvp);

		if (jp_error(jp) != JPRINT_OK)
			return;

		if (f(jp, name, type, p))
			continue;
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
				nvlist_to_json(a[i], jp, f);
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
		case DATA_TYPE_UINT32:
			jp_printf(jp, "%k: %u", name, *(uint32_t *)p);
			break;
		case DATA_TYPE_INT32:
			jp_printf(jp, "%k: %d", name, *(int32_t *)p);
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
			jp_printf(jp, "%k: {", name);
			nvlist_to_json((nvlist_t *)p, jp, f);
			jp_printf(jp, "}");
			break;

		/*
		 * Default -- tell us what we are missing. This is done to
		 * simply avoid writing ALL the cases, YAGNI (yah ain't
		 * gonna need it).
		 */
		default:
			jp_printf(jp, "%k: %s", name, datatype_string(type));
			zfs_dbgmsg("name = %s type = %d %s", name,
			    (int)type, datatype_string(type));
		break;
		}
	}
}

static const char *
vdev_state_string(uint64_t n)
{
	const char *s;
	switch (n) {
	case VDEV_STATE_UNKNOWN:	s = "HEALTHY";    break;
	case VDEV_STATE_CLOSED:		s = "CLOSED";	  break;
	case VDEV_STATE_OFFLINE:	s = "OFFLINE";    break;
	case VDEV_STATE_REMOVED:	s = "REMOVED";    break;
	case VDEV_STATE_CANT_OPEN:	s = "CAN'T OPEN"; break;
	case VDEV_STATE_FAULTED:	s = "FAULTED";    break;
	case VDEV_STATE_DEGRADED:	s = "DEGRADED";   break;
	case VDEV_STATE_HEALTHY:	s = "HEALTHY";    break;
	default:			s = "?";
	}
	return (s);
}

static void
vdev_to_json(vdev_t *v, pool_scan_stat_t *ps, jprint_t *jp)
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
		jp_printf(jp, "state: %s", vdev_state_string(v->vdev_state));
		/*
		 * Try for some of the extended status annotations that
		 * zpool status provides.
		 */
		/* (removing) */
		jp_printf(jp, "vs_scan_removing: %b",
		    v->vdev_stat.vs_scan_removing != 0);
		/*
		 * (non-allocating)
		 *
		 * -- for later, wasabi does not support this yet
		 *
		 * jp_printf(jp, "vs_noalloc: %b",
		 *   v->vdev_stat.vs_noalloc != 0);
		 */
		/* (awaiting resilver) */
		jp_printf(jp, "vs_resilver_deferred: %b",
		    v->vdev_stat.vs_resilver_deferred);
		s = "none";
		if ((v->vdev_state == VDEV_STATE_UNKNOWN) ||
		    (v->vdev_state == VDEV_STATE_HEALTHY)) {
			if (v->vdev_stat.vs_scan_processed != 0) {
				if (ps &&
				    (ps->pss_state == DSS_SCANNING)) {
					s = (ps->pss_func ==
					    POOL_SCAN_RESILVER) ?
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
		if (v->vdev_stat.vs_initialize_state ==
		    VDEV_INITIALIZE_SUSPENDED)
			s = "VDEV_INITIALIZE_SUSPENDED";
		if (v->vdev_stat.vs_initialize_state ==
		    VDEV_INITIALIZE_COMPLETE)
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
	n = v->vdev_children;
	a = v->vdev_child;
	jp_printf(jp, "vdev_children: %U", n);
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

static void
iterate_vdevs(spa_t *spa, pool_scan_stat_t *ps, jprint_t *jp)
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
	return s;
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
	return s;
}

static int
json_data(char *buf, size_t size, void *data)
{
	spa_t *spa = (spa_t *)data;
	int error = 0;
	int ps_error = 0;
	jprint_t jp;
	nvlist_t *nvl, *pnvl;
	uint64_t loadtimes[2];
	pool_scan_stat_t ps;
	int scl_config_lock;

	if (nvlist_dup(spa->spa_config, &nvl, 0) != 0) {
		/*
		 * FIXME, what to do here?!?
		 */
		zfs_dbgmsg("json_data: nvlist_dup failed");
		return (0);
	}
	fnvlist_add_nvlist(nvl, ZPOOL_CONFIG_LOAD_INFO, spa->spa_load_info);

	scl_config_lock =
	    spa_config_tryenter(spa, SCL_CONFIG, FTAG, RW_READER);

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

	jp_printf(&jp, "status_json_version: %d", JSON_STATUS_VERSION);
	jp_printf(&jp, "scl_config_lock: %b", scl_config_lock != 0);
	jp_printf(&jp, "scan_error: %d", ps_error);
	jp_printf(&jp, "scan_stats: {");
	if (ps_error == 0) {
		jp_printf(&jp, "func: %s", pss_func_to_string(ps.pss_func));
		jp_printf(&jp, "state: %s", pss_state_to_string(ps.pss_state));
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

	jp_printf(&jp, "state: %s", spa_state_to_name(spa));

	spa_add_spares(spa, nvl);
	spa_add_l2cache(spa, nvl);
	spa_add_feature_stats(spa, nvl);

	/* iterate and transfer nvl to json */
	nvlist_to_json(nvl, &jp, stats_filter);

	iterate_vdevs(spa, &ps, &jp);

	/*
	 * close the root object
	 */
	jp_printf(&jp, "}");

	if (scl_config_lock)
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


static void *
json_addr(kstat_t *ksp, loff_t n)
{
	if (n == 0)
		return (ksp->ks_private);
	return (NULL);
}


void
json_stats_init(spa_t *spa)
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
