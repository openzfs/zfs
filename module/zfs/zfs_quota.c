// SPDX-License-Identifier: CDDL-1.0
/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or https://opensource.org/licenses/CDDL-1.0.
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
 * Copyright (c) 2011 Pawel Jakub Dawidek
 * Copyright (c) 2012, 2015, 2018 by Delphix. All rights reserved.
 * Copyright (c) 2014 Integros [integros.com]
 * Copyright 2016 Nexenta Systems, Inc. All rights reserved.
 */

/* Portions Copyright 2010 Robert Milkowski */

#include <sys/avl.h>
#include <sys/dmu_objset.h>
#include <sys/sa.h>
#include <sys/sa_impl.h>
#include <sys/zap.h>
#include <sys/zfs_project.h>
#include <sys/zfs_quota.h>
#include <sys/zfs_znode.h>
#include <sys/zfs_vnops_os.h>

int
zpl_get_file_info(dmu_object_type_t bonustype, const void *data,
    zfs_file_info_t *zoi)
{
	/*
	 * Is it a valid type of object to track?
	 */
	if (bonustype != DMU_OT_ZNODE && bonustype != DMU_OT_SA)
		return (SET_ERROR(ENOENT));

	zoi->zfi_project = ZFS_DEFAULT_PROJID;

	/*
	 * If we have a NULL data pointer
	 * then assume the id's aren't changing and
	 * return EEXIST to the dmu to let it know to
	 * use the same ids
	 */
	if (data == NULL)
		return (SET_ERROR(EEXIST));

	if (bonustype == DMU_OT_ZNODE) {
		const znode_phys_t *znp = data;
		zoi->zfi_user = znp->zp_uid;
		zoi->zfi_group = znp->zp_gid;
		zoi->zfi_generation = znp->zp_gen;
		return (0);
	}

	const sa_hdr_phys_t *sap = data;
	if (sap->sa_magic == 0) {
		/*
		 * This should only happen for newly created files
		 * that haven't had the znode data filled in yet.
		 */
		zoi->zfi_user = 0;
		zoi->zfi_group = 0;
		zoi->zfi_generation = 0;
		return (0);
	}

	sa_hdr_phys_t sa = *sap;
	boolean_t swap = B_FALSE;
	if (sa.sa_magic == BSWAP_32(SA_MAGIC)) {
		sa.sa_magic = SA_MAGIC;
		sa.sa_layout_info = BSWAP_16(sa.sa_layout_info);
		swap = B_TRUE;
	}
	VERIFY3U(sa.sa_magic, ==, SA_MAGIC);

	int hdrsize = sa_hdrsize(&sa);
	VERIFY3U(hdrsize, >=, sizeof (sa_hdr_phys_t));

	uintptr_t data_after_hdr = (uintptr_t)data + hdrsize;
	zoi->zfi_user = *((uint64_t *)(data_after_hdr + SA_UID_OFFSET));
	zoi->zfi_group = *((uint64_t *)(data_after_hdr + SA_GID_OFFSET));
	zoi->zfi_generation = *((uint64_t *)(data_after_hdr + SA_GEN_OFFSET));
	uint64_t flags = *((uint64_t *)(data_after_hdr + SA_FLAGS_OFFSET));
	if (swap)
		flags = BSWAP_64(flags);

	if (flags & ZFS_PROJID) {
		zoi->zfi_project =
		    *((uint64_t *)(data_after_hdr + SA_PROJID_OFFSET));
	}

	if (swap) {
		zoi->zfi_user = BSWAP_64(zoi->zfi_user);
		zoi->zfi_group = BSWAP_64(zoi->zfi_group);
		zoi->zfi_project = BSWAP_64(zoi->zfi_project);
		zoi->zfi_generation = BSWAP_64(zoi->zfi_generation);
	}
	return (0);
}

static void
fuidstr_to_sid(zfsvfs_t *zfsvfs, const char *fuidstr,
    char *domainbuf, int buflen, uid_t *ridp)
{
	uint64_t fuid;
	const char *domain;

	fuid = zfs_strtonum(fuidstr, NULL);

	domain = zfs_fuid_find_by_idx(zfsvfs, FUID_INDEX(fuid));
	if (domain)
		(void) strlcpy(domainbuf, domain, buflen);
	else
		domainbuf[0] = '\0';
	*ridp = FUID_RID(fuid);
}

static uint64_t
zfs_userquota_prop_to_obj(zfsvfs_t *zfsvfs, zfs_userquota_prop_t type)
{
	switch (type) {
	case ZFS_PROP_USERUSED:
	case ZFS_PROP_USEROBJUSED:
		return (DMU_USERUSED_OBJECT);
	case ZFS_PROP_GROUPUSED:
	case ZFS_PROP_GROUPOBJUSED:
		return (DMU_GROUPUSED_OBJECT);
	case ZFS_PROP_PROJECTUSED:
	case ZFS_PROP_PROJECTOBJUSED:
		return (DMU_PROJECTUSED_OBJECT);
	case ZFS_PROP_USERQUOTA:
		return (zfsvfs->z_userquota_obj);
	case ZFS_PROP_GROUPQUOTA:
		return (zfsvfs->z_groupquota_obj);
	case ZFS_PROP_USEROBJQUOTA:
		return (zfsvfs->z_userobjquota_obj);
	case ZFS_PROP_GROUPOBJQUOTA:
		return (zfsvfs->z_groupobjquota_obj);
	case ZFS_PROP_PROJECTQUOTA:
		return (zfsvfs->z_projectquota_obj);
	case ZFS_PROP_PROJECTOBJQUOTA:
		return (zfsvfs->z_projectobjquota_obj);
	default:
		return (ZFS_NO_OBJECT);
	}
}

static uint64_t
zfs_usedquota_prop_to_default(zfsvfs_t *zfsvfs, zfs_userquota_prop_t type)
{
	switch (type) {
	case ZFS_PROP_USERUSED:
		return (zfsvfs->z_defaultuserquota);
	case ZFS_PROP_USEROBJUSED:
		return (zfsvfs->z_defaultuserobjquota);
	case ZFS_PROP_GROUPUSED:
		return (zfsvfs->z_defaultgroupquota);
	case ZFS_PROP_GROUPOBJUSED:
		return (zfsvfs->z_defaultgroupobjquota);
	case ZFS_PROP_PROJECTUSED:
		return (zfsvfs->z_defaultprojectquota);
	case ZFS_PROP_PROJECTOBJUSED:
		return (zfsvfs->z_defaultprojectobjquota);
	default:
		return (0);
	}
}

int
zfs_userspace_many(zfsvfs_t *zfsvfs, zfs_userquota_prop_t type,
    uint64_t *cookiep, void *vbuf, uint64_t *bufsizep,
    uint64_t *default_quota)
{
	int error;
	zap_cursor_t zc;
	zap_attribute_t *za;
	zfs_useracct_t *buf = vbuf;
	uint64_t obj;
	int offset = 0;

	if (!dmu_objset_userspace_present(zfsvfs->z_os))
		return (SET_ERROR(ENOTSUP));

	if ((type == ZFS_PROP_PROJECTQUOTA || type == ZFS_PROP_PROJECTUSED ||
	    type == ZFS_PROP_PROJECTOBJQUOTA ||
	    type == ZFS_PROP_PROJECTOBJUSED) &&
	    !dmu_objset_projectquota_present(zfsvfs->z_os))
		return (SET_ERROR(ENOTSUP));

	if ((type == ZFS_PROP_USEROBJUSED || type == ZFS_PROP_GROUPOBJUSED ||
	    type == ZFS_PROP_USEROBJQUOTA || type == ZFS_PROP_GROUPOBJQUOTA ||
	    type == ZFS_PROP_PROJECTOBJUSED ||
	    type == ZFS_PROP_PROJECTOBJQUOTA) &&
	    !dmu_objset_userobjspace_present(zfsvfs->z_os))
		return (SET_ERROR(ENOTSUP));

	*default_quota = zfs_usedquota_prop_to_default(zfsvfs, type);

	obj = zfs_userquota_prop_to_obj(zfsvfs, type);
	if (obj == ZFS_NO_OBJECT) {
		*bufsizep = 0;
		return (0);
	}

	if (type == ZFS_PROP_USEROBJUSED || type == ZFS_PROP_GROUPOBJUSED ||
	    type == ZFS_PROP_PROJECTOBJUSED)
		offset = DMU_OBJACCT_PREFIX_LEN;

	za = zap_attribute_alloc();
	for (zap_cursor_init_serialized(&zc, zfsvfs->z_os, obj, *cookiep);
	    (error = zap_cursor_retrieve(&zc, za)) == 0;
	    zap_cursor_advance(&zc)) {
		if ((uintptr_t)buf - (uintptr_t)vbuf + sizeof (zfs_useracct_t) >
		    *bufsizep)
			break;

		/*
		 * skip object quota (with zap name prefix DMU_OBJACCT_PREFIX)
		 * when dealing with block quota and vice versa.
		 */
		if ((offset > 0) != (strncmp(za->za_name, DMU_OBJACCT_PREFIX,
		    DMU_OBJACCT_PREFIX_LEN) == 0))
			continue;

		fuidstr_to_sid(zfsvfs, za->za_name + offset,
		    buf->zu_domain, sizeof (buf->zu_domain), &buf->zu_rid);

		buf->zu_space = za->za_first_integer;
		buf++;
	}
	if (error == ENOENT)
		error = 0;

	ASSERT3U((uintptr_t)buf - (uintptr_t)vbuf, <=, *bufsizep);
	*bufsizep = (uintptr_t)buf - (uintptr_t)vbuf;
	*cookiep = zap_cursor_serialize(&zc);
	zap_cursor_fini(&zc);
	zap_attribute_free(za);
	return (error);
}

int
zfs_userspace_one(zfsvfs_t *zfsvfs, zfs_userquota_prop_t type,
    const char *domain, uint64_t rid, uint64_t *valp)
{
	char buf[20 + DMU_OBJACCT_PREFIX_LEN];
	int offset = 0;
	int err;
	uint64_t obj;

	*valp = 0;

	if (!dmu_objset_userspace_present(zfsvfs->z_os))
		return (SET_ERROR(ENOTSUP));

	if ((type == ZFS_PROP_USEROBJUSED || type == ZFS_PROP_GROUPOBJUSED ||
	    type == ZFS_PROP_USEROBJQUOTA || type == ZFS_PROP_GROUPOBJQUOTA ||
	    type == ZFS_PROP_PROJECTOBJUSED ||
	    type == ZFS_PROP_PROJECTOBJQUOTA) &&
	    !dmu_objset_userobjspace_present(zfsvfs->z_os))
		return (SET_ERROR(ENOTSUP));

	if (type == ZFS_PROP_PROJECTQUOTA || type == ZFS_PROP_PROJECTUSED ||
	    type == ZFS_PROP_PROJECTOBJQUOTA ||
	    type == ZFS_PROP_PROJECTOBJUSED) {
		if (!dmu_objset_projectquota_present(zfsvfs->z_os))
			return (SET_ERROR(ENOTSUP));
		if (!zpl_is_valid_projid(rid))
			return (SET_ERROR(EINVAL));
	}

	obj = zfs_userquota_prop_to_obj(zfsvfs, type);
	if (obj == ZFS_NO_OBJECT)
		return (0);

	if (type == ZFS_PROP_USEROBJUSED || type == ZFS_PROP_GROUPOBJUSED ||
	    type == ZFS_PROP_PROJECTOBJUSED) {
		strlcpy(buf, DMU_OBJACCT_PREFIX, DMU_OBJACCT_PREFIX_LEN + 1);
		offset = DMU_OBJACCT_PREFIX_LEN;
	}

	err = zfs_id_to_fuidstr(zfsvfs, domain, rid, buf + offset,
	    sizeof (buf) - offset, B_FALSE);
	if (err)
		return (err);

	err = zap_lookup(zfsvfs->z_os, obj, buf, 8, 1, valp);
	if (err == ENOENT)
		err = 0;
	return (err);
}

/*
 * This function returns true, if "expected_parent_projid" is hierarchical
 * parent project of "projid", otherwise returns false.
 * zfsvfs->z_projecthierarchy_obj 0 means, no hierarchical projects.
 */
int
zfs_projects_are_hierarchical(zfsvfs_t *zfsvfs, uint64_t projid,
    uint64_t expected_parent_projid, boolean_t *hierarchical_p) {

	boolean_t hierarchical = B_FALSE;
	int err = 0;

	if ((err = zfs_enter(zfsvfs, FTAG)) != 0) {
		return (err);
	}

	if (!zfsvfs->z_projecthierarchy_obj) {
		goto out;
	}

	while (projid != ZFS_DEFAULT_PROJID) {
		char buf[20 + DMU_OBJACCT_PREFIX_LEN];

		(void) snprintf(buf, sizeof (buf), "%llx", (longlong_t)projid);

		uint64_t parentproj_ino[2] = {0, 0};
		uint64_t projhierarchyobj;
		uint64_t parent_projid;

		projhierarchyobj = zfsvfs->z_projecthierarchy_obj;
		err = zap_lookup(zfsvfs->z_os, projhierarchyobj, buf, 8, 2,
		    &parentproj_ino);
		if (err == ENOENT) {
			err = 0;
			break;
		}
		if (err) {
			char dsname[ZFS_MAX_DATASET_NAME_LEN];
			dsl_dataset_name(zfsvfs->z_os->os_dsl_dataset, dsname);
			cmn_err(CE_NOTE, "%s:%d ds=%s hobj=%lld project=%lld "
			    "error=%d on zap_lookup", __func__, __LINE__,
			    dsname, zfsvfs->z_projecthierarchy_obj, projid,
			    err);
			break;
		}

		parent_projid = parentproj_ino[0];
		if (parent_projid == expected_parent_projid) {
			hierarchical = B_TRUE;
			break;
		}
		projid = parent_projid;
	}

out:
	zfs_exit(zfsvfs, FTAG);
	*hierarchical_p = hierarchical;
	return (err);
}

/*
 * This function gets the usage of given projid.
 */
static int
zfs_project_get_usage(zfsvfs_t *zfsvfs, uint64_t projid, uint64_t *usedp)
{
	int err;

	/*
	 * get project usage of projid.
	 */
	char buf[20 + DMU_OBJACCT_PREFIX_LEN];
	uint64_t used;

	(void) snprintf(buf, sizeof (buf), "%llx",
	    (longlong_t)projid);
	used = 0;
	err = zap_lookup(zfsvfs->z_os, DMU_PROJECTUSED_OBJECT, buf, 8,
	    1, &used);
	if (err == ENOENT)
		err = 0;
	if (err != 0) {
		cmn_err(CE_NOTE, "%s:%d error=%d on zap_lookup. projid=%lld",
		    __func__, __LINE__, err, projid);
		*usedp = 0;
		return (err);
	}

	*usedp = used;
	return (0);
}

/*
 * This function finds total usages of child projects of given parent project.
 */
static int
zfs_project_hierarchy_get_childrens_usage(zfsvfs_t *zfsvfs, uint64_t *objp,
    uint64_t parent_projid, uint64_t *childrens_usedp)
{
	zap_cursor_t zc;
	zap_attribute_t *za;
	uint64_t parentproj_ino[2];
	uint64_t h_projid, h_parent_projid, h_ino;
	int error = 0;

	zfs_dbgmsg(" Get usage of all child project of projid=%lld",
	    parent_projid);
	za = zap_attribute_alloc();
	for (zap_cursor_init(&zc, zfsvfs->z_os, *objp);
	    zap_cursor_retrieve(&zc, za) == 0; zap_cursor_advance(&zc)) {
		if (za->za_num_integers != 2)
			continue;
		error = zap_lookup(zfsvfs->z_os, *objp,
		    za->za_name, 8, 2, parentproj_ino);
		if (error == ENOENT) {
			error = 0;
			continue;
		}
		if (error) {
			cmn_err(CE_NOTE, "%s:%d error %d on zap lookup. projid="
			    "%s", __func__, __LINE__, error, za->za_name);
			break;
		}
		h_parent_projid = parentproj_ino[0];
		h_ino = parentproj_ino[1];
		h_projid = zfs_strtonum(za->za_name, NULL);
		if (h_parent_projid == parent_projid) {
			uint64_t child_used = 0;
			error = zfs_project_get_usage(zfsvfs, h_projid,
			    &child_used);
			if (error) {
				cmn_err(CE_NOTE, "%s:%d error %d on projid=%s "
				    "get usage", __func__, __LINE__, error,
				    za->za_name);
				break;
			}
			*childrens_usedp += child_used;
		}
	}
	zap_cursor_fini(&zc);
	zap_attribute_free(za);

	return (error);
}

/*
 * This function finds project id associated to given ino.
 */
static int
zfs_project_hierarchy_ino_to_project(zfsvfs_t *zfsvfs, uint64_t *objp,
    uint64_t ino, uint64_t *projidp)
{
	zap_cursor_t zc;
	zap_attribute_t *za;
	uint64_t parentproj_ino[2];
	uint64_t h_projid, h_parent_projid, h_ino;
	uint64_t projid;
	int error = 0;

	projid = 0;
	za = zap_attribute_alloc();
	for (zap_cursor_init(&zc, zfsvfs->z_os, *objp);
	    zap_cursor_retrieve(&zc, za) == 0; zap_cursor_advance(&zc)) {
		if (za->za_num_integers != 2)
			continue;
		error = zap_lookup(zfsvfs->z_os, *objp,
		    za->za_name, 8, 2, parentproj_ino);
		if (error == ENOENT) {
			error = 0;
			continue;
		}
		if (error) {
			cmn_err(CE_NOTE, "%s:%d error %d on zap lookup. projid="
			    "%s", __func__, __LINE__, error, za->za_name);
			break;
		}
		h_parent_projid = parentproj_ino[0];
		h_ino = parentproj_ino[1];
		h_projid = zfs_strtonum(za->za_name, NULL);
		zfs_dbgmsg("zap entry projid=%lld parent_projid=%llx ino=%lld",
		    h_projid, h_parent_projid, h_ino);
		if (h_ino == ino) {
			projid = h_projid;
			break;
		}
	}
	zap_cursor_fini(&zc);
	zap_attribute_free(za);
	zfs_dbgmsg("projid=%lld for ino=%lld. error=%d", projid, ino, error);
	*projidp = projid;

	return (error);
}

/*
 * This functin sets the projid on znode.
 */
static int
zfs_projectino_set_projid(znode_t *zp, uint64_t projid, dmu_tx_t *tx)
{
	boolean_t projid_done = B_FALSE;
	zfsvfs_t *zfsvfs = ZTOZSB(zp);
	int error = 0;

	if (!(zp->z_pflags & ZFS_PROJID)) {
		/*
		 * zp was created before project quota feature upgrade.
		 * It needs sa relayout to set projid.
		 */
		error = sa_add_projid(zp->z_sa_hdl, tx, projid);
		if (unlikely(error == EEXIST)) {
			error = 0;
		} else if (error != 0) {
			goto out;
		} else {
			projid_done = B_TRUE;
		}
	}

	if (!projid_done) {
		zp->z_projid = projid;
		error = sa_update(zp->z_sa_hdl, SA_ZPL_PROJID(zfsvfs),
		    (void *)&zp->z_projid, sizeof (uint64_t), tx);
		if (error)
			goto out;
	}
	zp->z_pflags |= ZFS_PROJINHERIT;
	error = sa_update(zp->z_sa_hdl, SA_ZPL_FLAGS(zfsvfs),
	    (void *)&zp->z_pflags, sizeof (uint64_t), tx);

out:
	zfs_dbgmsg("ino=%lld projid=%lld error=%d", zp->z_id, projid, error);
	if (error) {
		cmn_err(CE_NOTE, "%s:%d error %d projid=%lld, ino=%lld",
		    __func__, __LINE__, error, projid, zp->z_id);
	}
	return (error);
}

/*
 * This function adds the given usage to all parent projects in hierarchy up.
 */
static int
zfs_project_hierarchy_parent_usage_update_all(zfsvfs_t *zfsvfs,
    uint64_t parent_projid, dmu_tx_t *tx, uint64_t *objp, int64_t used)
{
	int err = 0;

	while (parent_projid != ZFS_DEFAULT_PROJID) {
		mutex_enter(&zfsvfs->z_os->os_projecthierarchyused_lock);
		do_projectusage_update(zfsvfs->z_os,
		    parent_projid, used, tx);
		mutex_exit(&zfsvfs->z_os->os_projecthierarchyused_lock);

		char pprojbuf[32];
		(void) snprintf(pprojbuf, sizeof (pprojbuf), "%llx",
		    (longlong_t)parent_projid);

		uint64_t p_parentproj_ino[2];
		err = zap_lookup(zfsvfs->z_os, *objp,
		    pprojbuf, 8, 2,
		    &p_parentproj_ino);
		if (err == 0) {
			parent_projid =
			    p_parentproj_ino[0];
		} else {
			cmn_err(CE_NOTE, "%s:%d error %d on zap lookup. projid="
			    "%lld", __func__, __LINE__, err, parent_projid);
			break;
		}
	}

	return (err);
}

/*
 * This function gets the usage of projid and add it to parent_projid.
 */
static int
zfs_project_hierarchy_parent_usage_update(zfsvfs_t *zfsvfs, uint64_t projid,
    uint64_t parent_projid, dmu_tx_t *tx, boolean_t subtract)
{
	int err;

	if (projid != parent_projid && parent_projid != ZFS_DEFAULT_PROJID) {

		/*
		 * get project usage of projid.
		 * Update usage on parent_projid.
		 */
		char buf[20 + DMU_OBJACCT_PREFIX_LEN];
		uint64_t used;
		int64_t delta;

		(void) snprintf(buf, sizeof (buf), "%llx",
		    (longlong_t)projid);
		used = 0;
		err = zap_lookup(zfsvfs->z_os, DMU_PROJECTUSED_OBJECT, buf, 8,
		    1, &used);
		if (err == ENOENT)
			err = 0;
		if (err != 0) {
			cmn_err(CE_NOTE, "%s:%d projid=%lld "
			    "parent_projid=%lld delta=%lld err=%d", __func__,
			    __LINE__, projid, parent_projid, delta, err);
			return (err);
		}
		delta = used;

		if (subtract) {
			delta = -delta;
		}

		mutex_enter(&zfsvfs->z_os->os_projecthierarchyused_lock);
		do_projectusage_update(zfsvfs->z_os,
		    parent_projid, delta, tx);
		mutex_exit(&zfsvfs->z_os->os_projecthierarchyused_lock);
	}

	return (0);
}

/*
 * This function updates zap entry in ZFS_PROJECT_HIERARCHY zap object.
 * Zap entry is formed with name=<projid> and value=<parent_projid, ino>.
 * projid and ino are function arguments. parent_projid
 * is project id on parent inode of "ino" in hierarchy upword, which has
 * entry in ZFS_PROJECT_HIERARCHY, means hierarchical parent project.
 */
static int
zfs_project_hierarchy_zap_update(zfsvfs_t *zfsvfs, uint64_t *objp,
    uint64_t projid, uint64_t ino, char *buf, dmu_tx_t *tx,
    boolean_t parent_usage_update)
{
	uint64_t parent = 0;
	uint64_t parentproj_ino[2];
	uint64_t cur_parentproj_ino[2];
	uint64_t cur_parent_projid = ZFS_DEFAULT_PROJID;
	znode_t *zp = NULL;
	int err = 0;

	zfs_dbgmsg("Update parent_projid for projid=%llx", projid);
	ASSERT(ino != 0);

	/*
	 * Get current parent_projid for existing project.
	 * No entry means that its new project being added, so consider "0" as
	 * current parent project id.
	 */
	err = zap_lookup(zfsvfs->z_os, *objp, buf, 8, 2,
	    &cur_parentproj_ino);
	if (err == 0) {
		cur_parent_projid = cur_parentproj_ino[0];
	} else if (err == ENOENT) {
		cur_parent_projid = 0;
		err = 0;
	} else {
		cmn_err(CE_NOTE, "%s:%d error %d on zap lookup. projid=%lld",
		    __func__, __LINE__, err, projid);
		goto out;
	}

	char pbuf[32];
	uint64_t p_parentproj_ino[2] = {0, 0};
	uint64_t parent_projid = ZFS_DEFAULT_PROJID;

	uint64_t zid = ino;
	err = zfs_zget(zfsvfs, zid, &zp);
	if (err == ENOENT) {
		zfs_dbgmsg("ENOENT on ino=%lld associated to projid=%llx. "
		    "Remove from hierarchy", ino, projid);
		err = 0;
		goto update;
	}
	if (err)
		goto out;
	if (zp->z_projid != projid) {
		zfs_dbgmsg("different projid=%lld on ino=%lld, its associated "
		    "to projid=%llx. Remove from hierarchy", zp->z_projid, ino,
		    projid);
		zrele(zp);
		zp = NULL;
		goto update;
	}

	/*
	 * Get parent inode of ino passed. if project id on parent inode is 0 or
	 * non-zero project id with no entry in zap, then check the next parent
	 * following till the root inode.
	 * When updating child project to associate with newly added parent
	 * project, immediate parent inode of inode corresponding to child
	 * project could have projid "0", if its not an inode corresponding to
	 * parent project. so follow up to find parent inode with non-zero
	 * projid and entry in zap. Inode corresponding to parent project has
	 * projid set.
	 */
	while (zid != zfsvfs->z_root) {
		err = sa_lookup(zp->z_sa_hdl,
		    SA_ZPL_PARENT(zfsvfs), &parent, sizeof (parent));
		zrele(zp);
		zp = NULL;
		if (err) {
			cmn_err(CE_NOTE, "%s:%d error %d on sa_lookup. ino="
			    "%lld", __func__, __LINE__, err, zp->z_id);
			break;
		}

		err = zfs_zget(zfsvfs, parent, &zp);
		if (err) {
			cmn_err(CE_NOTE, "%s:%d error %d on zget. ino=%lld",
			    __func__, __LINE__, err, parent);
			break;
		}
		parent_projid = zp->z_projid;
		if (parent_projid != 0) {
			(void) snprintf(pbuf, sizeof (pbuf), "%llx",
			    (longlong_t)parent_projid);
			err = zap_lookup(zfsvfs->z_os, *objp, pbuf, 8, 2,
			    &p_parentproj_ino);
			if (err == 0)
				break;
		}
		zid = parent;
	}
	if (zp) {
		zrele(zp);
		zp = NULL;
	}

	if (err)
		goto out;

update:
	parentproj_ino[0] = parent_projid;
	parentproj_ino[1] = ino;

	err = zap_update(zfsvfs->z_os, *objp, buf, 8, 2, &parentproj_ino, tx);
	if (parent_usage_update && parent_projid != ZFS_DEFAULT_PROJID &&
	    parent_projid != cur_parent_projid)
		zfs_project_hierarchy_parent_usage_update(zfsvfs, projid,
		    parent_projid, tx, B_FALSE);

out:
	zfs_dbgmsg("updated parent_projid for projid=%llx, cur_parent_projid="
	    "%llx. parent projid=%llx, ino=%lld err=%d", projid,
	    cur_parent_projid, parent_projid, ino, err);
	return (err);
}

/*
 * This function updates each zap entry in PROJECT_HIERARCHY zap object, for the
 * change in parent project due to project add or remove.
 */
static int
zfs_project_hierarchy_zap_update_all(zfsvfs_t *zfsvfs, uint64_t *objp,
    uint64_t updated_projid, dmu_tx_t *tx, boolean_t project_removed)
{
	zap_cursor_t zc;
	zap_attribute_t *za;
	uint64_t parentproj_ino[2];
	uint64_t projid, parent_projid, ino;
	int error = 0;

	zfs_dbgmsg("Update all projects. newly %s projid=%llx",
	    (project_removed ? "removed" : "added"), updated_projid);
	za = zap_attribute_alloc();
	for (zap_cursor_init(&zc, zfsvfs->z_os, *objp);
	    zap_cursor_retrieve(&zc, za) == 0; zap_cursor_advance(&zc)) {
		if (za->za_num_integers != 2)
			continue;
		error = zap_lookup(zfsvfs->z_os, *objp,
		    za->za_name, 8, 2, parentproj_ino);
		if (error == ENOENT) {
			error = 0;
			continue;
		}
		if (error) {
			cmn_err(CE_NOTE, "%s:%d error %d on zap lookup projid="
			    "%s", __func__, __LINE__, error, za->za_name);
			break;
		}
		parent_projid = parentproj_ino[0];
		ino = parentproj_ino[1];
		zfs_dbgmsg("zap entry projid=%s parent_projid=%llx ino=%lld",
		    za->za_name, parent_projid, ino);
		if (project_removed && parent_projid != updated_projid)
			continue;
		if (!project_removed && parent_projid == updated_projid)
			continue;
		projid = zfs_strtonum(za->za_name, NULL);

		boolean_t parent_usage_update = B_TRUE;
		if (project_removed) {
			ASSERT(parent_projid == updated_projid);
			/*
			 * "parent_projid" removed, so its association with
			 * "projid" would be removed. projid would be
			 * associated to new parent project in hierarchy up via
			 * zfs_project_hierarchy_zap_update. substract projid
			 * usage from current "parent_projid".
			 */
			error = zfs_project_hierarchy_parent_usage_update(
			    zfsvfs, projid, parent_projid, tx, B_TRUE);
			if (error) {
				cmn_err(CE_NOTE, "%s:%d error %d on parent "
				    "usage update. projid=%lld parent_projid="
				    "%lld", __func__, __LINE__, error, projid,
				    parent_projid);
				break;
			}
			/*
			 * all parent project in hierarchy up already accounts
			 * projid usage, so no update needed to new parent
			 * project via zfs_project_hierarchy_zap_update.
			 */
			parent_usage_update = B_FALSE;
		}

		/*
		 * parent_usage_update would be true, when new project added and
		 * associating child project to it.
		 * zfs_project_hierarchy_zap_update would add "projid" usage to
		 * its new "parent_projid".
		 */
		error = zfs_project_hierarchy_zap_update(zfsvfs, objp, projid,
		    ino, za->za_name, tx, parent_usage_update);
		if (error) {
			cmn_err(CE_NOTE, "%s:%d error %d on hierarchy zap "
			    "update. projid=%lld ino=%lld", __func__, __LINE__,
			    error, projid, ino);
			break;
		}
	}
	zap_cursor_fini(&zc);
	zap_attribute_free(za);
	zfs_dbgmsg("Done Updating all projects. newly %s projid=%llx.error=%d",
	    (project_removed ? "removed" : "added"), updated_projid, error);

	return (error);
}

/*
 * This function removes a zap entry from project hierarchy zap.
 * Its used to remove project entry when associated directory inode is deleted.
 */
static int
zfs_project_hierarchy_remove_entry(zfsvfs_t *zfsvfs, uint64_t projid)
{
	dmu_tx_t *tx;
	int error;
	uint64_t phobj = zfsvfs->z_projecthierarchy_obj;
	if (phobj != 0 && projid != ZFS_DEFAULT_PROJID) {
		uint64_t parentproj_ino[2] = {0, 0};
		char projbuf[32];
		(void) snprintf(projbuf, sizeof (projbuf), "%llx",
		    (longlong_t)projid);
		error = zap_lookup(zfsvfs->z_os, phobj, projbuf, 8, 2,
		    &parentproj_ino);
		if (error == 0) {
			txg_wait_synced(dmu_objset_pool(zfsvfs->z_os), 0);

			mutex_enter(&zfsvfs->z_os->os_projecthierarchyop_lock);
			tx = dmu_tx_create(zfsvfs->z_os);
			dmu_tx_hold_zap(tx, phobj, B_TRUE, NULL);
			error = dmu_tx_assign(tx, DMU_TX_WAIT);
			if (error) {
				dmu_tx_abort(tx);
			} else {
				error = zap_remove(zfsvfs->z_os, phobj, projbuf,
				    tx);
				dmu_tx_commit(tx);
			}
			mutex_exit(&zfsvfs->z_os->os_projecthierarchyop_lock);
		}
		zfs_dbgmsg("removed entry projid=%lld. error=%d", projid,
		    error);
	}

	return (0);
}

/*
 * This function removes association of given project id and ino.
 */
int
zfs_project_hierarchy_remove(zfsvfs_t *zfsvfs, uint64_t projid, uint64_t ino,
    boolean_t toponly)
{
	uint64_t *objp;
	dmu_tx_t *tx;
	int err;

	zfs_dbgmsg("remove projid=%lld from ino=%lld", projid, ino);
	objp = &zfsvfs->z_projecthierarchy_obj;
	if (objp == 0)
		return (0);

	uint64_t h_parentproj = 0, h_ino = 0;
	char projbuf[32];

	(void) snprintf(projbuf, sizeof (projbuf), "%llx", (longlong_t)projid);

	uint64_t parentproj_ino[2];
	err = zap_lookup(zfsvfs->z_os, *objp, projbuf, 8, 2,
	    &parentproj_ino);
	if (err == 0) {
		h_parentproj = parentproj_ino[0];
		h_ino = parentproj_ino[1];
		if (ino != h_ino) {
			cmn_err(CE_NOTE, "%s:%d project %lld associated to ino="
			    "%lld and not to %lld", __func__, __LINE__, projid,
			    h_ino, ino);
			return (EINVAL);
		}
		if (toponly && h_parentproj != 0) {
			cmn_err(CE_NOTE, "%s:%d project %lld assocated to ino="
			    "%lld, is not top of hierarchy", __func__, __LINE__,
			    projid, ino);
			return (EINVAL);
		}
	} else if (err != ENOENT) {
		cmn_err(CE_NOTE, "%s:%d error %d on zap_lookup. projid=%lld "
		    "ino=%lld", __func__, __LINE__, err, projid, ino);
		return (err);
	} else {
		cmn_err(CE_NOTE, "%s:%d projid=%lld not associated to ino=%lld"
		    " and not to any other ino", __func__, __LINE__, projid,
		    ino);
		return (EINVAL);
	}

	mutex_enter(&zfsvfs->z_os->os_projecthierarchyop_lock);
	/*
	 * Get projid own usage to later reduce it from all its parents once its
	 * removed from hierarchy.
	 */
	uint64_t project_used = 0;
	err = zfs_project_get_usage(zfsvfs, projid, &project_used);
	if (err) {
		mutex_exit(&zfsvfs->z_os->os_projecthierarchyop_lock);
		return (err);
	}
	uint64_t childrens_used = 0;
	err = zfs_project_hierarchy_get_childrens_usage(zfsvfs, objp, projid,
	    &childrens_used);
	if (err) {
		mutex_exit(&zfsvfs->z_os->os_projecthierarchyop_lock);
		return (err);
	}

	uint64_t project_own_used;
	if (project_used > childrens_used)
		project_own_used = project_used - childrens_used;
	else
		project_own_used = 0;

	tx = dmu_tx_create(zfsvfs->z_os);
	dmu_tx_hold_zap(tx, *objp, B_TRUE, NULL);
	err = dmu_tx_assign(tx, DMU_TX_WAIT);
	if (err) {
		dmu_tx_abort(tx);
		mutex_exit(&zfsvfs->z_os->os_projecthierarchyop_lock);
		return (err);
	}

	err = zap_remove(zfsvfs->z_os, *objp, projbuf, tx);
	if (err == ENOENT)
		err = 0;
	ASSERT0(err);

	/*
	 * projid removed from hierarchy. Update childrens assocation in
	 * hierarchy.
	 */
	err = zfs_project_hierarchy_zap_update_all(zfsvfs, objp, projid, tx,
	    B_TRUE);
	ASSERT0(err);

	/*
	 * Reduce projid own usage from its parents in hierarchy.
	 */
	err = zfs_project_hierarchy_parent_usage_update_all(zfsvfs,
	    h_parentproj, tx, objp, -project_own_used);
	ASSERT0(err);

	dmu_tx_commit(tx);
	mutex_exit(&zfsvfs->z_os->os_projecthierarchyop_lock);
	zfs_dbgmsg("removed projid=%lld from ino=%lld", projid, ino);

	return (err);
}

/*
 * This function creats association of given projid with its parent project.
 * Parent project is discovered by walking up in hierarchy from the given
 * directory ino. It also updates the existing projects parent. Parent project
 * change for existing projects is possible, when a new project is added in
 * middle of the hierarchy of existing projects.
 */
int
zfs_project_hierarchy_add(zfsvfs_t *zfsvfs, uint64_t projid, uint64_t ino)
{
	uint64_t *objp;
	dmu_tx_t *tx;
	char buf[32];
	int err = 0;

	if ((err = zfs_enter(zfsvfs, FTAG)) != 0)
		return (err);

	zfs_dbgmsg("add projid=%lld ino=%lld", projid, ino);
	objp = &zfsvfs->z_projecthierarchy_obj;

	(void) snprintf(buf, sizeof (buf), "%llx", (longlong_t)projid);

	if (*objp != 0) {
		uint64_t parentproj_ino[2];
		err = zap_lookup(zfsvfs->z_os, *objp, buf, 8, 2,
		    &parentproj_ino);
		if (err == 0) {
			uint64_t projino;
			projino = parentproj_ino[1];
			if (ino == projino) {
				cmn_err(CE_NOTE, "%s:%d projid=%lld already "
				    "added in given ino=%lld hierarchy",
				    __func__, __LINE__, projid, ino);
				goto out_exit;
			} else {
				cmn_err(CE_NOTE, "%s:%d projid=%lld already "
				    "added in ino=%lld hierarchy. can't add for"
				    " another ino=%lld", __func__, __LINE__,
				    projid, projino, ino);
				err = (SET_ERROR(EINVAL));
				goto out_exit;
			}
		} else if (err != ENOENT) {
			cmn_err(CE_NOTE, "%s:%d error %d on zap_lookup. projid="
			    "%lld ino=%lld", __func__, __LINE__, err, projid,
			    ino);
			goto out_exit;
		}

		/*
		 * If different project associated to given inode in hierarchy,
		 * then it needs to be removed first from hierarchy.
		 * Can't associate a new project to ino, if another project
		 * is associated and it has quota set.
		 */
		uint64_t h_projid = 0;
		err = zfs_project_hierarchy_ino_to_project(zfsvfs, objp, ino,
		    &h_projid);
		if (err) {
			cmn_err(CE_NOTE, "%s:%d error %d on ino to project. "
			    "projid=%lld ino=%lld", __func__, __LINE__, err,
			    projid, ino);
			goto out_exit;
		}
		if (h_projid != 0) {
			uint64_t *quotaobjp;
			uint64_t quota;
			char hbuf[32];
			quotaobjp = &zfsvfs->z_projectquota_obj;
			(void) snprintf(hbuf, sizeof (hbuf), "%llx",
			    (longlong_t)h_projid);
			err = zap_lookup(zfsvfs->z_os, *quotaobjp, hbuf, 8, 1,
			    &quota);
			if (err == 0) {
				cmn_err(CE_NOTE, "%s:%d ino=%lld already "
				    "associated to projid=%lld with quota set. "
				    "can't associate to another projid=%lld",
				    __func__, __LINE__, ino, h_projid, projid);
				err = (SET_ERROR(EINVAL));
				goto out_exit;
			} else if (err != ENOENT) {
				cmn_err(CE_NOTE, "%s:%d error %d on quota "
				    "zap_lookup. projid=%lld ino=%lld hprojid="
				    "%lld", __func__, __LINE__, err, projid,
				    ino, h_projid);
				goto out_exit;
			}
			err = zfs_project_hierarchy_remove(zfsvfs, h_projid,
			    ino, B_FALSE);
			if (err) {
				cmn_err(CE_NOTE, "%s:%d error %d on hierarchy "
				    "remove. projid=%lld ino=%lld hprojid=%lld",
				    __func__, __LINE__, err, projid, ino,
				    h_projid);
				goto out_exit;
			}
		}
	}

	znode_t *qzp = NULL;

	err = zfs_zget(zfsvfs, ino, &qzp);
	if (err) {
		cmn_err(CE_NOTE, "%s:%d error %d on zget. projid=%lld "
		    "ino=%lld", __func__, __LINE__, err, projid, ino);
		goto out_exit;
	}

	mutex_enter(&zfsvfs->z_os->os_projecthierarchyop_lock);

	tx = dmu_tx_create(zfsvfs->z_os);
	dmu_tx_hold_sa(tx, qzp->z_sa_hdl, B_TRUE);
	dmu_tx_hold_zap(tx, *objp ? *objp : DMU_NEW_OBJECT, B_TRUE, NULL);
	if (*objp == 0) {
		dmu_tx_hold_zap(tx, MASTER_NODE_OBJ, B_TRUE,
		    ZFS_PROJECT_HIERARCHY);
	}
	err = dmu_tx_assign(tx, DMU_TX_WAIT);
	if (err) {
		dmu_tx_abort(tx);
		goto out_exit_rele_unlock;
	}

	mutex_enter(&zfsvfs->z_lock);
	if (*objp == 0) {
		*objp = zap_create(zfsvfs->z_os, DMU_OT_USERGROUP_QUOTA,
		    DMU_OT_NONE, 0, tx);
		VERIFY(0 == zap_add(zfsvfs->z_os, MASTER_NODE_OBJ,
		    ZFS_PROJECT_HIERARCHY, 8,
		    1, objp, tx));
		zfsvfs->z_os->os_projecthierarchy_obj =
		    zfsvfs->z_projecthierarchy_obj;
	}
	mutex_exit(&zfsvfs->z_lock);

	/*
	 * Set the projid on corresponding inode, so that child project can see
	 * and associate with it.
	 */
	err = zfs_projectino_set_projid(qzp, projid, tx);
	ASSERT0(err);

	err = zfs_project_hierarchy_zap_update(zfsvfs, objp, projid, ino, buf,
	    tx, B_FALSE);
	ASSERT0(err);
	err = zfs_project_hierarchy_zap_update_all(zfsvfs, objp, projid, tx,
	    B_FALSE);
	ASSERT0(err);

	dmu_tx_commit(tx);
	/*
	 * As we set the projid on inode, so also set projid on its xattr's.
	 */
	zfs_setattr_xattr_dir(qzp);

out_exit_rele_unlock:
	mutex_exit(&zfsvfs->z_os->os_projecthierarchyop_lock);
	zrele(qzp);
out_exit:
	zfs_exit(zfsvfs, FTAG);
	zfs_dbgmsg("Done add projid=%lld ino=%lld err:%d", projid, ino, err);
	return (err);
}

/*
 * This function removes projects from hierarchy, which doesn't
 * have quota and parent project id 0. Project without quota and
 * no parent project doesn't need hierarchical accounting.
 * It also cleanup the entries corresponding to deleted directory
 * inode.
 */
static int
zfs_project_hierarchy_cleanup(zfsvfs_t *zfsvfs)
{
	zap_cursor_t zc;
	zap_attribute_t *za;
	uint64_t parentproj_ino[2];
	uint64_t h_projid, h_parent_projid, h_ino;
	uint64_t *hobjp, *quotaobjp;

	quotaobjp = &zfsvfs->z_projectquota_obj;
	hobjp = &zfsvfs->z_projecthierarchy_obj;
	if (hobjp == NULL)
		return (0);

more:
	boolean_t remove_more = B_FALSE;
	za = zap_attribute_alloc();
	for (zap_cursor_init(&zc, zfsvfs->z_os, *hobjp);
	    zap_cursor_retrieve(&zc, za) == 0; zap_cursor_advance(&zc)) {
		if (za->za_num_integers != 2)
			continue;
		if (zap_lookup(zfsvfs->z_os, *hobjp, za->za_name, 8, 2,
		    parentproj_ino))
			continue;
		h_parent_projid = parentproj_ino[0];
		h_ino = parentproj_ino[1];
		h_projid = zfs_strtonum(za->za_name, NULL);
		zfs_dbgmsg("zap entry projid=%lld parent_projid=%llx ino=%lld",
		    h_projid, h_parent_projid, h_ino);

		znode_t *zp = NULL;
		uint64_t z_projid = 0;
		int err;
		err = zfs_zget(zfsvfs, h_ino, &zp);
		if (err == 0) {
			z_projid = zp->z_projid;
			zrele(zp);
		}
		if (err != 0 || z_projid != h_projid) {
			zfs_project_hierarchy_remove_entry(zfsvfs, h_projid);
			continue;
		}
		if (h_parent_projid != 0)
			continue;
		uint64_t quota;
		if (!quotaobjp || (zap_lookup(zfsvfs->z_os, *quotaobjp,
		    za->za_name, 8, 1, &quota) == ENOENT)) {
			if (zfs_project_hierarchy_remove(zfsvfs, h_projid,
			    h_ino, B_FALSE) == 0)
				remove_more = B_TRUE;
		}
	}
	zap_cursor_fini(&zc);
	zap_attribute_free(za);
	if (remove_more)
		goto more;
	zfs_dbgmsg("Removed all projects with none quota and parent project 0");
	return (0);
}

int
zfs_set_userquota(zfsvfs_t *zfsvfs, zfs_userquota_prop_t type,
    const char *domain, uint64_t rid, uint64_t quota)
{
	char buf[32];
	int err;
	dmu_tx_t *tx;
	uint64_t *objp;
	boolean_t fuid_dirtied;

	if (zfsvfs->z_version < ZPL_VERSION_USERSPACE)
		return (SET_ERROR(ENOTSUP));

	switch (type) {
	case ZFS_PROP_USERQUOTA:
		objp = &zfsvfs->z_userquota_obj;
		break;
	case ZFS_PROP_GROUPQUOTA:
		objp = &zfsvfs->z_groupquota_obj;
		break;
	case ZFS_PROP_USEROBJQUOTA:
		objp = &zfsvfs->z_userobjquota_obj;
		break;
	case ZFS_PROP_GROUPOBJQUOTA:
		objp = &zfsvfs->z_groupobjquota_obj;
		break;
	case ZFS_PROP_PROJECTQUOTA:
		if (!dmu_objset_projectquota_enabled(zfsvfs->z_os))
			return (SET_ERROR(ENOTSUP));
		if (!zpl_is_valid_projid(rid))
			return (SET_ERROR(EINVAL));

		objp = &zfsvfs->z_projectquota_obj;
		break;
	case ZFS_PROP_PROJECTOBJQUOTA:
		if (!dmu_objset_projectquota_enabled(zfsvfs->z_os))
			return (SET_ERROR(ENOTSUP));
		if (!zpl_is_valid_projid(rid))
			return (SET_ERROR(EINVAL));

		objp = &zfsvfs->z_projectobjquota_obj;
		break;
	default:
		return (SET_ERROR(EINVAL));
	}

	err = zfs_id_to_fuidstr(zfsvfs, domain, rid, buf, sizeof (buf), B_TRUE);
	if (err)
		return (err);
	fuid_dirtied = zfsvfs->z_fuid_dirty;

	tx = dmu_tx_create(zfsvfs->z_os);
	dmu_tx_hold_zap(tx, *objp ? *objp : DMU_NEW_OBJECT, B_TRUE, NULL);
	if (*objp == 0) {
		dmu_tx_hold_zap(tx, MASTER_NODE_OBJ, B_TRUE,
		    zfs_userquota_prop_prefixes[type]);
	}
	if (fuid_dirtied)
		zfs_fuid_txhold(zfsvfs, tx);
	err = dmu_tx_assign(tx, DMU_TX_WAIT);
	if (err) {
		dmu_tx_abort(tx);
		return (err);
	}

	mutex_enter(&zfsvfs->z_lock);
	if (*objp == 0) {
		*objp = zap_create(zfsvfs->z_os, DMU_OT_USERGROUP_QUOTA,
		    DMU_OT_NONE, 0, tx);
		VERIFY(0 == zap_add(zfsvfs->z_os, MASTER_NODE_OBJ,
		    zfs_userquota_prop_prefixes[type], 8, 1, objp, tx));
	}
	mutex_exit(&zfsvfs->z_lock);

	boolean_t cleanup_hierarchy = B_FALSE;
	if (quota == 0) {
		err = zap_remove(zfsvfs->z_os, *objp, buf, tx);
		if (!err)
			cleanup_hierarchy = B_TRUE;
		if (err == ENOENT)
			err = 0;
	} else {
		err = zap_update(zfsvfs->z_os, *objp, buf, 8, 1, &quota, tx);
	}
	ASSERT(err == 0);
	if (fuid_dirtied)
		zfs_fuid_sync(zfsvfs, tx);
	dmu_tx_commit(tx);
	if (cleanup_hierarchy)
		zfs_project_hierarchy_cleanup(zfsvfs);
	return (err);
}

boolean_t
zfs_id_overobjquota(zfsvfs_t *zfsvfs, uint64_t usedobj, uint64_t id)
{
	char buf[20 + DMU_OBJACCT_PREFIX_LEN];
	uint64_t used, quota, quotaobj, default_quota = 0;
	int err;

	if (!dmu_objset_userobjspace_present(zfsvfs->z_os)) {
		if (dmu_objset_userobjspace_upgradable(zfsvfs->z_os)) {
			dsl_pool_config_enter(
			    dmu_objset_pool(zfsvfs->z_os), FTAG);
			dmu_objset_id_quota_upgrade(zfsvfs->z_os);
			dsl_pool_config_exit(
			    dmu_objset_pool(zfsvfs->z_os), FTAG);
		}
		return (B_FALSE);
	}

	if (usedobj == DMU_PROJECTUSED_OBJECT) {
		if (!dmu_objset_projectquota_present(zfsvfs->z_os)) {
			if (dmu_objset_projectquota_upgradable(zfsvfs->z_os)) {
				dsl_pool_config_enter(
				    dmu_objset_pool(zfsvfs->z_os), FTAG);
				dmu_objset_id_quota_upgrade(zfsvfs->z_os);
				dsl_pool_config_exit(
				    dmu_objset_pool(zfsvfs->z_os), FTAG);
			}
			return (B_FALSE);
		}
		quotaobj = zfsvfs->z_projectobjquota_obj;
		default_quota = zfsvfs->z_defaultprojectobjquota;
	} else if (usedobj == DMU_USERUSED_OBJECT) {
		quotaobj = zfsvfs->z_userobjquota_obj;
		default_quota = zfsvfs->z_defaultuserobjquota;
	} else if (usedobj == DMU_GROUPUSED_OBJECT) {
		quotaobj = zfsvfs->z_groupobjquota_obj;
		default_quota = zfsvfs->z_defaultgroupobjquota;
	} else {
		return (B_FALSE);
	}
	if (zfsvfs->z_replay)
		return (B_FALSE);

	(void) snprintf(buf, sizeof (buf), "%llx", (longlong_t)id);
	if (quotaobj == 0) {
		if (default_quota == 0)
			return (B_FALSE);
		quota = default_quota;
	} else {
		err = zap_lookup(zfsvfs->z_os, quotaobj, buf, 8, 1, &quota);
		if (err != 0 && ((quota = default_quota) == 0))
			return (B_FALSE);
	}

	(void) snprintf(buf, sizeof (buf), DMU_OBJACCT_PREFIX "%llx",
	    (longlong_t)id);
	err = zap_lookup(zfsvfs->z_os, usedobj, buf, 8, 1, &used);
	if (err != 0)
		return (B_FALSE);
	return (used >= quota);
}

boolean_t
zfs_id_overblockquota(zfsvfs_t *zfsvfs, uint64_t usedobj, uint64_t id)
{
	char buf[20];
	uint64_t used, quota, quotaobj, default_quota = 0;
	int err;
	uint64_t projecthierarchyobj = 0;
	uint64_t parentproj_ino[2] = {0, 0};
	uint64_t parent_projid = ZFS_DEFAULT_PROJID;

	if (usedobj == DMU_PROJECTUSED_OBJECT) {
		if (!dmu_objset_projectquota_present(zfsvfs->z_os)) {
			if (dmu_objset_projectquota_upgradable(zfsvfs->z_os)) {
				dsl_pool_config_enter(
				    dmu_objset_pool(zfsvfs->z_os), FTAG);
				dmu_objset_id_quota_upgrade(zfsvfs->z_os);
				dsl_pool_config_exit(
				    dmu_objset_pool(zfsvfs->z_os), FTAG);
			}
			return (B_FALSE);
		}
		quotaobj = zfsvfs->z_projectquota_obj;
		default_quota = zfsvfs->z_defaultprojectquota;
		projecthierarchyobj = zfsvfs->z_projecthierarchy_obj;
	} else if (usedobj == DMU_USERUSED_OBJECT) {
		quotaobj = zfsvfs->z_userquota_obj;
		default_quota = zfsvfs->z_defaultuserquota;
	} else if (usedobj == DMU_GROUPUSED_OBJECT) {
		quotaobj = zfsvfs->z_groupquota_obj;
		default_quota = zfsvfs->z_defaultgroupquota;
	} else {
		return (B_FALSE);
	}
	if (zfsvfs->z_replay)
		return (B_FALSE);

checkquota:
	(void) snprintf(buf, sizeof (buf), "%llx", (longlong_t)id);
	if (quotaobj == 0) {
		if (default_quota == 0)
			return (B_FALSE);
		quota = default_quota;
	} else {
		err = zap_lookup(zfsvfs->z_os, quotaobj, buf, 8, 1, &quota);
		if (err != 0 && ((quota = default_quota) == 0))
			return (B_FALSE);
	}

	err = zap_lookup(zfsvfs->z_os, usedobj, buf, 8, 1, &used);
	if (err != 0)
		return (B_FALSE);

	if (used  >= quota)
		return (B_TRUE);

	if (projecthierarchyobj) {
		err = zap_lookup(zfsvfs->z_os, projecthierarchyobj, buf, 8, 2,
		    &parentproj_ino);
		parent_projid = parentproj_ino[0];
		if (parent_projid != ZFS_DEFAULT_PROJID) {
			id = parent_projid;
			goto checkquota;
		}
	}

	return (B_FALSE);
}

boolean_t
zfs_id_overquota(zfsvfs_t *zfsvfs, uint64_t usedobj, uint64_t id)
{
	return (zfs_id_overblockquota(zfsvfs, usedobj, id) ||
	    zfs_id_overobjquota(zfsvfs, usedobj, id));
}

EXPORT_SYMBOL(zpl_get_file_info);
EXPORT_SYMBOL(zfs_userspace_one);
EXPORT_SYMBOL(zfs_userspace_many);
EXPORT_SYMBOL(zfs_set_userquota);
EXPORT_SYMBOL(zfs_project_hierarchy_add);
EXPORT_SYMBOL(zfs_project_hierarchy_remove);
EXPORT_SYMBOL(zfs_id_overblockquota);
EXPORT_SYMBOL(zfs_id_overobjquota);
EXPORT_SYMBOL(zfs_id_overquota);
