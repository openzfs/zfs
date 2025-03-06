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
 * Copyright (c) 2025, Klara, Inc.
 */

#include <sys/zfs_context.h>
#include <sys/spa_impl.h>
#include <sys/vdev_impl.h>
#include <sys/vdev_draid.h>
#include <sys/spa.h>
#include <zfs_comutil.h>
#include <sys/spa_stats_json.h>
#include <sys/nvpair_impl.h>

#include "json.h"
#include "literals.h"

#define	GUID_STRBUF_LEN	32
#define	BUF_LEN	256

static uint_t zfs_lockless_read_enabled = 0;

static char *
vdev_name(vdev_t *vd)
{
	char *name = kmem_alloc(BUF_LEN, KM_SLEEP);

	if (strcmp(vd->vdev_ops->vdev_op_type, "root") == 0) {
		strlcpy(name, spa_name(vd->vdev_spa), BUF_LEN);
		return (name);
	}

	if (vd->vdev_not_present) {
		snprintf(name, BUF_LEN, "%llu", (u_longlong_t)vd->vdev_guid);
		return (name);
	}

	if (vd->vdev_path != NULL) {
		/* No path or partition stripping. */
		strlcpy(name, vd->vdev_path, BUF_LEN);
		return (name);
	}

	if (strcmp(vd->vdev_ops->vdev_op_type, VDEV_TYPE_RAIDZ) == 0) {
		snprintf(name, BUF_LEN, "%s%llu",
		    vd->vdev_ops->vdev_op_type,
		    (u_longlong_t)vdev_get_nparity(vd));
	} else if (strcmp(vd->vdev_ops->vdev_op_type, VDEV_TYPE_DRAID) == 0) {
		vdev_draid_config_t *vdc = vd->vdev_tsd;
		snprintf(name, BUF_LEN, "%s%llu:%llud:%lluc:%llus",
		    VDEV_TYPE_DRAID,
		    (u_longlong_t)vdc->vdc_nparity,
		    (u_longlong_t)vdc->vdc_ndata,
		    (u_longlong_t)vd->vdev_children,
		    (u_longlong_t)vdc->vdc_nspares);
	} else {
		strlcpy(name, vd->vdev_ops->vdev_op_type, BUF_LEN);
	}

	const size_t namelen = strlen(name);
	if (namelen < BUF_LEN - 1)
		snprintf(name + namelen, BUF_LEN - namelen, "-%llu",
		    (u_longlong_t)vd->vdev_id);

	return (name);
}

static void
add_vdev(nvlist_t *parent, vdev_t *vd)
{
	/* Numbers are provided as literals composed into strings */

	if (strcmp(vd->vdev_ops->vdev_op_type, VDEV_TYPE_INDIRECT) == 0)
		return;

	nvlist_t *nvl = fnvlist_alloc();
	char *buf = kmem_alloc(BUF_LEN, KM_SLEEP);

	vdev_stat_t *vs = kmem_alloc(sizeof (*vs), KM_SLEEP);
	vdev_get_stats_ex(vd, vs, NULL);

	char *vname = vdev_name(vd);
	fnvlist_add_string(nvl, "name", vname);

	fnvlist_add_string(nvl, "vdev_type", vd->vdev_ops->vdev_op_type);

	if (vd->vdev_guid != 0) {
		snprintf(buf, BUF_LEN, "%llu", (u_longlong_t)vd->vdev_guid);
		fnvlist_add_string(nvl, "guid", buf);
	}

	if (vd->vdev_path != NULL)
		fnvlist_add_string(nvl, "path", vd->vdev_path);
	if (vd->vdev_physpath != NULL)
		fnvlist_add_string(nvl, "phys_path", vd->vdev_physpath);
	if (vd->vdev_devid != NULL)
		fnvlist_add_string(nvl, "devid", vd->vdev_devid);

	if (vd->vdev_ishole)
		fnvlist_add_string(nvl, "class", VDEV_TYPE_HOLE);
	else if (vd->vdev_isl2cache)
		fnvlist_add_string(nvl, "class", VDEV_TYPE_L2CACHE);
	else if (vd->vdev_isspare)
		fnvlist_add_string(nvl, "class", VDEV_TYPE_SPARE);
	else if (vd->vdev_islog)
		fnvlist_add_string(nvl, "class", VDEV_TYPE_LOG);
	else {
		vdev_alloc_bias_t bias = vd->vdev_alloc_bias;
		if (bias == VDEV_BIAS_NONE && vd->vdev_parent != NULL)
			bias = vd->vdev_parent->vdev_alloc_bias;
		switch (bias) {
		case VDEV_BIAS_LOG:
			fnvlist_add_string(nvl, "class",
			    VDEV_ALLOC_BIAS_LOG);
			break;
		case VDEV_BIAS_SPECIAL:
			fnvlist_add_string(nvl, "class",
			    VDEV_ALLOC_BIAS_SPECIAL);
			break;
		case VDEV_BIAS_DEDUP:
			fnvlist_add_string(nvl, "class",
			    VDEV_ALLOC_BIAS_DEDUP);
			break;
		case VDEV_BIAS_NONE:
		default:
			fnvlist_add_string(nvl, "class", "normal");
		}
	}

	fnvlist_add_string(nvl, "state", vdev_state_string(vd->vdev_state));

	if (vd->vdev_isspare) {
		if (vd->vdev_stat.vs_aux == VDEV_AUX_SPARED) {
			fnvlist_add_string(nvl, "state", "INUSE");
			/* used_by is not provided */
		} else if (vd->vdev_state == VDEV_STATE_HEALTHY) {
			fnvlist_add_string(nvl, "state", "AVAIL");
		}
	} else {
		if (vs->vs_alloc) {
			snprintf(buf, BUF_LEN, "%llu",
			    (u_longlong_t)vs->vs_alloc);
			fnvlist_add_string(nvl, "alloc_space", buf);
		}
		if (vs->vs_space) {
			snprintf(buf, BUF_LEN, "%llu",
			    (u_longlong_t)vs->vs_space);
			fnvlist_add_string(nvl, "total_space", buf);
		}
		if (vs->vs_dspace) {
			snprintf(buf, BUF_LEN, "%llu",
			    (u_longlong_t)vs->vs_dspace);
			fnvlist_add_string(nvl, "def_space", buf);
		}
		if (vs->vs_rsize) {
			snprintf(buf, BUF_LEN, "%llu",
			    (u_longlong_t)vs->vs_rsize);
			fnvlist_add_string(nvl, "rep_dev_size", buf);
		}
		if (vs->vs_esize) {
			snprintf(buf, BUF_LEN, "%llu",
			    (u_longlong_t)vs->vs_esize);
			fnvlist_add_string(nvl, "ex_dev_size", buf);
		}
		if (vs->vs_self_healed) {
			snprintf(buf, BUF_LEN, "%llu",
			    (u_longlong_t)vs->vs_self_healed);
			fnvlist_add_string(nvl, "self_healed", buf);
		}
		if (vs->vs_pspace) {
			snprintf(buf, BUF_LEN, "%llu",
			    (u_longlong_t)vs->vs_pspace);
			fnvlist_add_string(nvl, "phys_space", buf);
		}
		snprintf(buf, BUF_LEN, "%llu",
		    (u_longlong_t)vs->vs_read_errors);
		fnvlist_add_string(nvl, "read_errors", buf);
		snprintf(buf, BUF_LEN, "%llu",
		    (u_longlong_t)vs->vs_write_errors);
		fnvlist_add_string(nvl, "write_errors", buf);
		snprintf(buf, BUF_LEN, "%llu",
		    (u_longlong_t)vs->vs_checksum_errors);
		fnvlist_add_string(nvl, "checksum_errors", buf);
		if (vs->vs_scan_processed) {
			snprintf(buf, BUF_LEN, "%llu",
			    (u_longlong_t)vs->vs_scan_processed);
			fnvlist_add_string(nvl, "scan_processed", buf);
		}
		if (vs->vs_checkpoint_space) {
			snprintf(buf, BUF_LEN, "%llu",
			    (u_longlong_t)vs->vs_checkpoint_space);
			fnvlist_add_string(nvl, "checkpoint_space", buf);
		}
		if (vs->vs_resilver_deferred) {
			snprintf(buf, BUF_LEN, "%llu",
			    (u_longlong_t)vs->vs_resilver_deferred);
			fnvlist_add_string(nvl, "resilver_deferred", buf);
		}
		if (vd->vdev_children == 0) {
			snprintf(buf, BUF_LEN, "%llu",
			    (u_longlong_t)vs->vs_slow_ios);
			fnvlist_add_string(nvl, "slow_ios", buf);
		}
	}

	if (vd->vdev_not_present) {
		fnvlist_add_string(nvl, "not_present", "1");
		if (vd->vdev_path != NULL)
			fnvlist_add_string(nvl, "was", vd->vdev_path);
	} else if (vs->vs_aux != VDEV_AUX_NONE) {
		fnvlist_add_string(nvl, "aux", vdev_aux_string(vs->vs_aux));
	} else if (vd->vdev_children == 0 && !vd->vdev_isspare &&
	    vs->vs_configured_ashift < vs->vs_physical_ashift) {
		snprintf(buf, BUF_LEN, "%llu",
		    (u_longlong_t)vs->vs_configured_ashift);
		fnvlist_add_string(nvl, "configured_ashift", buf);
		snprintf(buf, BUF_LEN, "%llu",
		    (u_longlong_t)vs->vs_physical_ashift);
		fnvlist_add_string(nvl, "physical_ashift", buf);
	}

	if (vs->vs_scan_removing != 0) {
		snprintf(buf, BUF_LEN, "%llu",
		    (u_longlong_t)vs->vs_scan_removing);
		fnvlist_add_string(nvl, "removing", buf);
	} else if (vs->vs_noalloc != 0) {
		snprintf(buf, BUF_LEN, "%llu", (u_longlong_t)vs->vs_noalloc);
		fnvlist_add_string(nvl, "noalloc", buf);
	}

	/* The vdev init & trim info is not provided */

	const uint64_t n = vd->vdev_children;
	const boolean_t is_root =
	    (strcmp(vd->vdev_ops->vdev_op_type, "root") == 0) ?
	    B_TRUE : B_FALSE;
	if (n > 0) {
		nvlist_t *vdevs = fnvlist_alloc();
		for (uint64_t i = 0; i < n; i++) {
			vdev_t *ch = vd->vdev_child[i];
			if (ch == NULL)
				continue;
			/* logs/dedup/special are provided separately */
			if (is_root == B_TRUE && ch->vdev_alloc_bias
			    != VDEV_BIAS_NONE)
				continue;
			add_vdev(vdevs, vd->vdev_child[i]);
		}
		fnvlist_add_nvlist(nvl, "vdevs", vdevs);
		nvlist_free(vdevs);
	}

	fnvlist_add_nvlist(parent, vname, nvl);
	nvlist_free(nvl);
	kmem_free(vname, BUF_LEN);
	kmem_free(vs, sizeof (*vs));
	kmem_free(buf, BUF_LEN);
}

int
spa_stats_json_generate(spa_t *spa, char *buf, size_t size)
{
	int error = 0;
	nvlist_t *nvroot, *pools, *pool, *vdevs;
	vdev_t *rootvd;
	char *str;
	int locked = 0;
	int n = 0;

	str = kmem_alloc(GUID_STRBUF_LEN, KM_SLEEP);

	while (locked == 0 && n++ < 10)
		locked = spa_config_tryenter(spa, SCL_CONFIG, FTAG, RW_READER);
	if (locked == 0 && zfs_lockless_read_enabled == 0)
		return (EAGAIN);

	pool = fnvlist_alloc();
	fnvlist_add_string(pool, "name", spa_name(spa));
	fnvlist_add_string(pool, "state", spa_state_to_name(spa));

	snprintf(str, GUID_STRBUF_LEN, "%llu", (u_longlong_t)spa_guid(spa));
	fnvlist_add_string(pool, ZPOOL_CONFIG_POOL_GUID, str);
	snprintf(str, GUID_STRBUF_LEN, "%llu",
	    (u_longlong_t)(spa->spa_config_txg));
	fnvlist_add_string(pool, ZPOOL_CONFIG_POOL_TXG, str);

	fnvlist_add_string(pool, "spa_version", SPA_VERSION_STRING);
	fnvlist_add_string(pool, "zpl_version", ZPL_VERSION_STRING);

	/* The status, action, msgid, moreinfo are not provided */

	/* root vdev */
	rootvd = spa->spa_root_vdev;
	if (rootvd != NULL) {
		vdevs = fnvlist_alloc();
		add_vdev(vdevs, rootvd);
		fnvlist_add_nvlist(pool, "vdevs", vdevs);
		nvlist_free(vdevs);
	}

	/* dedup */
	n = 0;
	vdevs = fnvlist_alloc();
	for (size_t i = 0; rootvd != NULL && i < rootvd->vdev_children; i++) {
		vdev_t *vd = rootvd->vdev_child[i];
		if (vd->vdev_alloc_bias != VDEV_BIAS_DEDUP)
			continue;
		n++;
		add_vdev(vdevs, vd);
	}
	if (n > 0)
		fnvlist_add_nvlist(pool, "dedup", vdevs);
	nvlist_free(vdevs);

	/* special */
	n = 0;
	vdevs = fnvlist_alloc();
	for (size_t i = 0; rootvd != NULL && i < rootvd->vdev_children; i++) {
		vdev_t *vd = rootvd->vdev_child[i];
		if (vd->vdev_alloc_bias != VDEV_BIAS_SPECIAL)
			continue;
		n++;
		add_vdev(vdevs, vd);
	}
	if (n > 0)
		fnvlist_add_nvlist(pool, "special", vdevs);
	nvlist_free(vdevs);

	/* logs */
	n = 0;
	vdevs = fnvlist_alloc();
	for (size_t i = 0; rootvd != NULL && i < rootvd->vdev_children; i++) {
		vdev_t *vd = rootvd->vdev_child[i];
		if (vd->vdev_alloc_bias != VDEV_BIAS_LOG)
			continue;
		n++;
		add_vdev(vdevs, vd);
	}
	if (n > 0)
		fnvlist_add_nvlist(pool, "logs", vdevs);
	nvlist_free(vdevs);

	/* l2cache */
	if (spa->spa_l2cache.sav_count > 0) {
		vdevs = fnvlist_alloc();
		for (int i = 0; i < spa->spa_l2cache.sav_count; i++)
			add_vdev(vdevs, spa->spa_l2cache.sav_vdevs[i]);
		fnvlist_add_nvlist(pool, "l2cache", vdevs);
		nvlist_free(vdevs);
	}

	/* spares */
	if (spa->spa_spares.sav_count > 0) {
		vdevs = fnvlist_alloc();
		for (int i = 0; i < spa->spa_spares.sav_count; i++)
			add_vdev(vdevs, spa->spa_spares.sav_vdevs[i]);
		fnvlist_add_nvlist(pool, "spares", vdevs);
		nvlist_free(vdevs);
	}

	snprintf(str, GUID_STRBUF_LEN, "%llu",
	    (u_longlong_t)spa_approx_errlog_size(spa));
	fnvlist_add_string(pool, ZPOOL_CONFIG_ERRCOUNT, str);

	nvroot = fnvlist_alloc();
	json_add_output_version(nvroot, "kstat zpool status", 0, 1);
	pools = fnvlist_alloc();
	fnvlist_add_nvlist(pools, spa_name(spa), pool);
	nvlist_free(pool);
	fnvlist_add_nvlist(nvroot, "pools", pools);
	nvlist_free(pools);

	if (locked != 0)
		spa_config_exit(spa, SCL_CONFIG, FTAG);

	nvjson_t nvjson = {
		.buf = buf,
		.size = size,
	};
	error = nvlist_to_json(&nvjson, nvroot);
	nvlist_free(nvroot);

	kmem_free(str, GUID_STRBUF_LEN);

	return (error);
}

ZFS_MODULE_PARAM(zfs, zfs_, lockless_read_enabled, UINT, ZMOD_RW,
	"Enables lockless traversal of kernel structures in emergencies");
