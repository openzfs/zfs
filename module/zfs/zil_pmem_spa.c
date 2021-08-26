#include <sys/zil_pmem_impl.h>
#define _ZIL_PMEM_SPA_IMPL
#include <sys/zil_pmem_spa.h>
#include <sys/spa_impl.h>

/* called from syncing context */
void
zilpmem_spa_txg_synced(spa_t *spa, uint64_t synced_txg)
{
	if (spa->spa_zil_kind != ZIL_KIND_PMEM) {
		VERIFY3P(spa->spa_zilpmem, ==, NULL);
		return;
	}

	spa_zilpmem_t *szp = spa->spa_zilpmem;
	VERIFY(szp);
	VERIFY3S(szp->szlp_state, ==, SPA_ZILPMEM_LOADED);

	spa_prb_t *sprb;
	for (sprb = list_head(&szp->szlp_prbs); sprb != NULL;
	    sprb = list_next(&szp->szlp_prbs, sprb)) {
		zilpmem_prb_gc(sprb->sprb_prb, synced_txg);
	}
}

static int
spa_prb_handle_cmp(const void *va, const void *vb)
{
	const spa_prb_handle_t *a = va;
	const spa_prb_handle_t *b = vb;
	return (TREE_CMP(a->sprbh_objset_id, b->sprbh_objset_id));
}

typedef struct
{
	spa_zilpmem_t *szp;
	boolean_t creating;
	int err;
} zilpmem_spa_load_setup_prbs_cb_arg_t;

int zfs_zil_pmem_prb_ncommitters = 4;
ZFS_MODULE_PARAM(zfs_zil_pmem, zfs_zil_pmem_, prb_ncommitters, INT, ZMOD_RW,
		 "");

static int
zilpmem_spa_load_setup_prbs_cb(vdev_t *vd, void *varg)
{
	zilpmem_spa_load_setup_prbs_cb_arg_t *arg = varg;
	VERIFY3S(vd->vdev_alloc_bias, ==, VDEV_BIAS_EXEMPT);
	/* FIXME these are cases where we want to skip the vdev */
	VERIFY3S(vd->vdev_islog, ==, B_TRUE);
	VERIFY3S(vd->vdev_isdax, ==, B_TRUE);
	VERIFY3P(vd->vdev_ops->vdev_op_dax_mapping, !=, NULL);

	if (vd->vdev_state != VDEV_STATE_HEALTHY)
	{
		vdev_dbgmsg(vd, "can only dax-map healthy vdev");
		return (0);
	}

	/* XXX dax vdev should increment some refcount on the vdev so that
	 * it doesn't go away (prevents use after free of the mapping */
	void *base;
	uint64_t len;
	int err = vd->vdev_ops->vdev_op_dax_mapping(vd, &base, &len);
	if (err != 0)
	{
		zfs_dbgmsg("cannot setup dax mapping for vdev '%s', err=%d", vd->vdev_path, err);
		arg->err = err;
		return (err);
	}
	VERIFY3U(len, >=, VDEV_LABEL_START_SIZE + VDEV_LABEL_END_SIZE);
	/* FIXME verify len == vdev's asize? */
	base = base + VDEV_LABEL_START_SIZE;
	len = len - VDEV_LABEL_START_SIZE - VDEV_LABEL_END_SIZE;

	if (len < ZILPMEM_PRB_CHUNKSIZE)
	{
		return SET_ERROR(ENOSPC);
	}

	/* FIXME hardcoded parameters */
	zilpmem_prb_t *prb = zilpmem_prb_alloc(zfs_zil_pmem_prb_ncommitters);
	for (uint64_t offset = 0; offset <= (len - ZILPMEM_PRB_CHUNKSIZE); offset += ZILPMEM_PRB_CHUNKSIZE)
	{
		prb_chunk_t *ch = prb_chunk_alloc(base + offset /* XXX overflow check */, ZILPMEM_PRB_CHUNKSIZE);
		VERIFY(ch);
		if (arg->creating)
		{
			VERIFY(spa_writeable(vd->vdev_spa));
			/* TODO ensure that there is no dry-run path or similar that leads to this code */
			zilpmem_prb_add_chunk_for_write(prb, ch);
		}
		else
		{
			zilpmem_prb_add_chunk_for_claim(prb, ch);
		}
	}

	spa_prb_t *sprb = kmem_zalloc(sizeof(spa_prb_t), KM_SLEEP);
	sprb->sprb_prb = prb;
	zfs_refcount_create(&sprb->sprb_rc);

	VERIFY(RRM_WRITE_HELD(&arg->szp->szlp_rwl));
	list_insert_tail(&arg->szp->szlp_prbs, sprb);

	return (0);
}

static int
zilpmem_spa_setup_objset(spa_zilpmem_t *szp, objset_t *os, boolean_t *created)
{
	VERIFY(RRM_WRITE_HELD(&szp->szlp_rwl));

	*created = B_FALSE;

	if (!spa_feature_is_active(dmu_objset_spa(os), SPA_FEATURE_ZIL_KINDS))
		return (0);

	if (os->os_zil_header.zh_v2.zh_kind != ZIL_KIND_PMEM)
		return (0);

	char name[ZFS_MAX_DATASET_NAME_LEN];
	dmu_objset_name(os, name);

	/* FIXME encode prb id / vdev id in zil header and retrieve it
	 * by that id. As a temporary hackaround we pick the first one.
	 * This should beccome a function that returns the refcount-bumped
	 * pointer to sprb */
	size_t nprbs = 0;
	spa_prb_t *sprb;
	for (sprb = list_head(&szp->szlp_prbs); sprb != NULL;
	     sprb = list_next(&szp->szlp_prbs, sprb))
	{
		nprbs += 1;
	}
	if (nprbs == 0 /* FIXME prb with id prb_id not found */)
	{
		zfs_dbgmsg("no prb found for objset %s", name);
		return (ENOENT);
	}
	sprb = list_head(&szp->szlp_prbs);

	spa_prb_handle_t *sprbh =
	    kmem_zalloc(sizeof(spa_prb_handle_t), KM_SLEEP);
	const uint64_t objset_id = dmu_objset_id(os);
	zilpmem_prb_handle_t *zph =
	    zilpmem_prb_setup_objset(sprb->sprb_prb, objset_id);
	VERIFY(zph);
	sprbh->sprbh_hdl = zph;
	zfs_refcount_create(&sprbh->sprbh_rc);
	sprbh->sprbh_objset_id = objset_id;
	sprbh->sprbh_sprb = sprb;
	/*
	 * We remove the refcount on sprb->sprb_rc in zilpmem_spa_destroy_objset
	 * or during unload.
	 */
	zfs_refcount_add(&sprb->sprb_rc, sprbh);

	avl_add(&szp->szlp_handles, sprbh);

	*created = B_TRUE;
	return (0);
}

static int
zilpmem_spa_load_setup_handles_cb(dsl_pool_t *dp, dsl_dataset_t *ds, void *arg)
{
	spa_zilpmem_t *szp = arg;
	objset_t *os;

	int err = dmu_objset_from_ds(ds, &os);
	if (err != 0)
		return (err);

	rrm_enter_write(&szp->szlp_rwl);

	boolean_t created;
	err = zilpmem_spa_setup_objset(szp, os, &created);

	rrm_exit(&szp->szlp_rwl, FTAG);
	return (err);
}

static void
zilpmem_spa_free_handle(spa_prb_handle_t *sprbh, boolean_t abandon_claim,
			zil_header_pmem_t *zh_sync)
{
	VERIFY(zfs_refcount_is_zero(&sprbh->sprbh_rc));
	zfs_refcount_destroy(&sprbh->sprbh_rc);
	zilpmem_prb_teardown_objset(sprbh->sprbh_hdl, abandon_claim, zh_sync);
	sprbh->sprbh_hdl = NULL;
	zfs_refcount_remove(&sprbh->sprbh_sprb->sprb_rc, sprbh);
	kmem_free(sprbh, sizeof(spa_prb_handle_t));
}

typedef enum
{
	ZLP_LDULACT_CREATE,
	ZLP_LDULACT_LOAD,
	ZLP_LDULACT_UNLOAD,
} zilpmem_spa_load_unload_action_t;

static int
zilpmem_spa_load_unload_impl(spa_t *spa, zilpmem_spa_load_unload_action_t act)
{
	int ret;
	spa_zilpmem_t *szp = spa->spa_zilpmem;
	int follow_state;
	spa_prb_handle_t *sprbh;
	void *cookie;
	spa_prb_t *sprb;

	VERIFY(szp);
	VERIFY3U(act, <=, ZLP_LDULACT_UNLOAD);

	rrm_enter_write(&szp->szlp_rwl);

	if (act == ZLP_LDULACT_UNLOAD)
	{
		ret = 0;
		follow_state = SPA_ZILPMEM_UNLOADED;
		if (szp->szlp_state == SPA_ZILPMEM_LOADED)
		{
			goto unload;
		}
		else if (szp->szlp_state == SPA_ZILPMEM_LOADCREATE_FAILED)
		{
			goto out;
		}
		else
		{
			panic("unexpected state %d", szp->szlp_state);
		}
	}

	VERIFY(act == ZLP_LDULACT_LOAD || act == ZLP_LDULACT_CREATE);

	VERIFY3S(szp->szlp_state, ==, SPA_ZILPMEM_UNINIT);
	szp->szlp_state = SPA_ZILPMEM_LOADCREATING;

	list_create(&szp->szlp_prbs, sizeof(spa_prb_t),
		    offsetof(spa_prb_t, sprb_list_node));

	zilpmem_spa_load_setup_prbs_cb_arg_t find_arg = {
	    .szp = szp,
	    .creating = act == ZLP_LDULACT_CREATE ? B_TRUE : B_FALSE,
	    .err = 0,
	};

	spa_iter_dax_vdevs(spa, zilpmem_spa_load_setup_prbs_cb, &find_arg);
	/* FIXME The prbs should probably hold some kind of refcount on the DAX vdevs by now */

	if (find_arg.err != 0)
	{
		ret = find_arg.err;
		follow_state = SPA_ZILPMEM_LOADCREATE_FAILED;
		goto unload_free_sprbs;
	}

	avl_create(&szp->szlp_handles, spa_prb_handle_cmp,
		   sizeof(spa_prb_handle_t),
		   offsetof(spa_prb_handle_t, sprbh_avl_node));

	/*
	 * Drop rwl because dmu_objset_find_dp is parallel => each callback
	 * invocation must aquire it again. This wouldn't be a problem
	 * if DS_FIND_SERIALIZE wasn't broken.
	 * Since we have set SPA_ZILPMEM_LOADCREATING the current thread is still
	 * the only one who can execute this function due to the VERIFY above.
	 */
	rrm_exit(&szp->szlp_rwl, FTAG);
	dsl_pool_t *dp = spa_get_dsl(spa);
	int err;
	if (act == ZLP_LDULACT_LOAD)
	{
		VERIFY(dp);
		err = dmu_objset_find_dp(dp, dp->dp_root_dir_obj,
					 zilpmem_spa_load_setup_handles_cb, szp, DS_FIND_CHILDREN);
	}
	else
	{
		VERIFY(act == ZLP_LDULACT_CREATE);

		/* Assert that we are called before dsl_pool_create */
		VERIFY3P(dp, ==, NULL);
		/*
		 * Since there are no datasets yet there is no need to setup
		 * any handles, they will be set up by
		 * zilpmem_spa_create_objset().
		 */
		err = 0;
	}
	rrm_enter_write(&szp->szlp_rwl);
	if (err != 0)
	{
		ret = err;
		follow_state = SPA_ZILPMEM_LOADCREATE_FAILED;
		goto unload_teardown_objsets_noabandon;
	}

	follow_state = SPA_ZILPMEM_LOADED;
	ret = 0;
	goto out;

unload:
	/* start */

unload_teardown_objsets_noabandon:

	for (sprb = list_head(&szp->szlp_prbs); sprb != NULL;
	     sprb = list_next(&szp->szlp_prbs, sprb))
	{
		zilpmem_prb_promise_no_more_gc(sprb->sprb_prb);
	}

	cookie = NULL;
	while ((sprbh = avl_destroy_nodes(&szp->szlp_handles, &cookie)) != NULL)
	{
		zilpmem_spa_free_handle(sprbh, B_FALSE, NULL);
	}
	avl_destroy(&szp->szlp_handles);

unload_free_sprbs:
	while ((sprb = list_remove_head(&szp->szlp_prbs)) != NULL)
	{
		VERIFY3U(zfs_refcount_count(&sprb->sprb_rc), ==, 0);
		zfs_refcount_destroy(&sprb->sprb_rc);
		zilpmem_prb_free(sprb->sprb_prb, B_TRUE);
		kmem_free(sprb, sizeof(spa_prb_t));
	}
	list_destroy(&szp->szlp_prbs);

out:
	szp->szlp_state = follow_state;

	rrm_exit(&szp->szlp_rwl, FTAG);
	return (ret);
}

static spa_zilpmem_t *
zilpmem_spa_alloc(spa_t *spa)
{
	spa_zilpmem_t *szp = spa->spa_zilpmem;
	VERIFY3P(szp, ==, NULL);
	szp = kmem_zalloc(sizeof(spa_zilpmem_t), KM_SLEEP);
	rrm_init(&szp->szlp_rwl, B_FALSE);
	spa->spa_zilpmem = szp;
	VERIFY3S(szp->szlp_state, ==, SPA_ZILPMEM_UNINIT);
	return szp;
}

static void
zilpmem_spa_free(spa_t *spa)
{
	rrm_destroy(&spa->spa_zilpmem->szlp_rwl);
	kmem_free(spa->spa_zilpmem, sizeof(spa_zilpmem_t));
	spa->spa_zilpmem = NULL;
}

static int
zilpmem_spa_load_impl(spa_t *spa, boolean_t creating)
{
	spa_zilpmem_t *szp = spa->spa_zilpmem;
	VERIFY(szp);
	int err = zilpmem_spa_load_unload_impl(spa,
					       creating ? ZLP_LDULACT_CREATE : ZLP_LDULACT_LOAD);
	IMPLY(err != 0, szp->szlp_state == SPA_ZILPMEM_LOADCREATE_FAILED);
	IMPLY(err == 0, szp->szlp_state == SPA_ZILPMEM_LOADED);
	return (err);
}

int zilpmem_spa_create(spa_t *spa)
{
	/* NB: spa_feature_is_active(dmu_objset_spa(os), SPA_FEATURE_ZIL_KINDS)
	 * does not work here yet since it is not yet enabled */
	if (spa->spa_zil_kind != ZIL_KIND_PMEM)
	{
		spa->spa_zilpmem = NULL;
		return (0);
	}

	spa_zilpmem_t *szp = zilpmem_spa_alloc(spa);

	/* test-create and see that exactly one prb exists */
	int err = 0;
	err = zilpmem_spa_load_impl(spa, B_TRUE);
	if (err != 0)
		goto efree;

	VERIFY(avl_is_empty(&szp->szlp_handles));

	if (list_is_empty(&szp->szlp_prbs))
	{
		err = ZFS_ERR_ZIL_PMEM_INVALID_SLOG_CONFIG;
	}
	else
	{
		spa_prb_t *prb = list_head(&szp->szlp_prbs);
		VERIFY(prb);
		prb = list_next(&szp->szlp_prbs, prb);
		if (prb != NULL)
		{
			err = ZFS_ERR_ZIL_PMEM_INVALID_SLOG_CONFIG;
		}
	}
	if (err != 0)
		goto eunload;

	return (0);

eunload:
	VERIFY0(zilpmem_spa_load_unload_impl(spa, ZLP_LDULACT_UNLOAD));
efree:
	zilpmem_spa_free(spa);
	return (err);
}

int zilpmem_spa_load(spa_t *spa)
{
	if (spa->spa_zil_kind != ZIL_KIND_PMEM)
	{
		spa->spa_zilpmem = NULL;
		return (0);
	}

	zilpmem_spa_alloc(spa);
	spa_config_enter(spa, SCL_VDEV, FTAG, RW_READER);
	int ret = zilpmem_spa_load_impl(spa, B_FALSE);
	spa_config_exit(spa, SCL_VDEV, FTAG);
	return (ret);
}

void zilpmem_spa_unload(spa_t *spa)
{
	if (spa->spa_zil_kind != ZIL_KIND_PMEM)
	{
		VERIFY3P(spa->spa_zilpmem, ==, NULL);
		return;
	}

	/* XXX: we also encode zilpmem_spa_create_begin as NULL, should be an internal state */
	if (spa->spa_zilpmem == NULL)
	{
#ifdef _KERNEL
		pr_debug("zilpmem_spa_unload(): apparently zilpmem_spa_create_begin failed.\n");
#endif
		return;
	}

	VERIFY0(zilpmem_spa_load_unload_impl(spa, ZLP_LDULACT_UNLOAD));
	zilpmem_spa_free(spa);
}


static spa_prb_handle_t *
zilpmem_spa_prb_hold_impl(objset_t *os, void *holder)
{
	spa_zilpmem_t *szp = dmu_objset_spa(os)->spa_zilpmem;
	VERIFY(RRM_LOCK_HELD(&szp->szlp_rwl));

	VERIFY3S(szp->szlp_state, ==, SPA_ZILPMEM_LOADED);

	const uint64_t objset_id = dmu_objset_id(os);
	spa_prb_handle_t q = {
		.sprbh_objset_id = objset_id,
	};
	avl_index_t where;
	spa_prb_handle_t *sprbh = avl_find(&szp->szlp_handles, &q, &where);
	VERIFY3P(sprbh, !=, NULL);
	zfs_refcount_add(&sprbh->sprbh_rc, holder);

	return (sprbh);
}

spa_prb_handle_t *
zilpmem_spa_prb_hold(zilog_pmem_t *zilog)
{
	spa_zilpmem_t *szp = ZL_SPA(zilog)->spa_zilpmem;
	rrm_enter_read(&szp->szlp_rwl, FTAG);
	spa_prb_handle_t *sprbh;
	sprbh = zilpmem_spa_prb_hold_impl(ZL_OS(zilog), zilog);
	rrm_exit(&szp->szlp_rwl, FTAG);
	return (sprbh);
}

zilpmem_prb_handle_t *
zilpmem_spa_prb_handle_ref_inner(spa_prb_handle_t *sprbh)
{
	VERIFY(sprbh);
	VERIFY3S(zfs_refcount_count(&sprbh->sprbh_rc), >, 0);
	return sprbh->sprbh_hdl;
}

static void
zilpmem_spa_prb_rele_impl(objset_t *os, spa_prb_handle_t *sprbh, void *holder)
{
	spa_zilpmem_t *szp = dmu_objset_spa(os)->spa_zilpmem;
	VERIFY(RRM_LOCK_HELD(&szp->szlp_rwl));

	VERIFY3S(szp->szlp_state, ==, SPA_ZILPMEM_LOADED);

	avl_index_t where;
	void *found = avl_find(&szp->szlp_handles, sprbh, &where);
	VERIFY3P(found, ==, sprbh);

	zfs_refcount_remove(&sprbh->sprbh_rc, holder);
}

void
zilpmem_spa_prb_rele(zilog_pmem_t *zilog, spa_prb_handle_t *sprbh)
{
	spa_zilpmem_t *szp = ZL_SPA(zilog)->spa_zilpmem;
	rrm_enter_read(&szp->szlp_rwl, FTAG);
	zilpmem_spa_prb_rele_impl(ZL_OS(zilog), sprbh, zilog);
	rrm_exit(&szp->szlp_rwl, FTAG);
}


void zilpmem_spa_create_objset(spa_t *spa, objset_t *os, dmu_tx_t *tx)
{
	if (spa->spa_zil_kind != ZIL_KIND_PMEM) {
		VERIFY3P(spa->spa_zilpmem, ==, NULL);
		return;
	}

	spa_zilpmem_t *szp = spa->spa_zilpmem;

	rrm_enter_write(&szp->szlp_rwl);

	VERIFY3S(szp->szlp_state, ==, SPA_ZILPMEM_LOADED);

	VERIFY(dsl_pool_config_held(dmu_objset_pool(os)));
	/* assert this is not called for the MOS*/
	VERIFY(dmu_objset_ds(os));

	boolean_t created;
	VERIFY0(zilpmem_spa_setup_objset(szp, os, &created));

	if (!created)
		goto out;

	spa_prb_handle_t *sprbh = zilpmem_spa_prb_hold_impl(os, FTAG);

	VERIFY(dmu_tx_is_syncing(tx));
	zil_header_pmem_t *zh =
	    zil_header_pmem_from_zil_header_in_syncing_context(
	    spa, &os->os_zil_header);
	zilpmem_prb_destroy_log(sprbh->sprbh_hdl, zh);

	zilpmem_spa_prb_rele_impl(os, sprbh, FTAG);

out:

	rrm_exit(&szp->szlp_rwl, FTAG);

}

/* must be called from syncing context */
void
zilpmem_spa_destroy_objset(objset_t *os, zil_header_pmem_t *zh_sync)
{
	spa_t *spa = dmu_objset_spa(os);
	if (spa->spa_zil_kind != ZIL_KIND_PMEM) {
		VERIFY3P(spa->spa_zilpmem, ==, NULL);
		return;
	}

	spa_zilpmem_t *szp = spa->spa_zilpmem;

	rrm_enter_write(&szp->szlp_rwl);

	VERIFY3S(szp->szlp_state, ==, SPA_ZILPMEM_LOADED);

	const uint64_t objset_id = dmu_objset_id(os);
	spa_prb_handle_t q = {
		.sprbh_objset_id = objset_id,
	};
	avl_index_t where;

	spa_prb_handle_t *sprbh = avl_find(&szp->szlp_handles, &q, &where);
	VERIFY3P(sprbh, !=, NULL);
	avl_remove(&szp->szlp_handles, sprbh);

	zilpmem_spa_free_handle(sprbh, B_TRUE, zh_sync);

	rrm_exit(&szp->szlp_rwl, FTAG);
}
