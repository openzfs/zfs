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
 * Copyright (c) 2011 Pawel Jakub Dawidek <pawel@dawidek.net>.
 * All rights reserved.
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

int
zfs_userspace_many(zfsvfs_t *zfsvfs, zfs_userquota_prop_t type,
    uint64_t *cookiep, void *vbuf, uint64_t *bufsizep)
{
	int error;
	zap_cursor_t zc;
	zap_attribute_t za;
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

	obj = zfs_userquota_prop_to_obj(zfsvfs, type);
	if (obj == ZFS_NO_OBJECT) {
		*bufsizep = 0;
		return (0);
	}

	if (type == ZFS_PROP_USEROBJUSED || type == ZFS_PROP_GROUPOBJUSED ||
	    type == ZFS_PROP_PROJECTOBJUSED)
		offset = DMU_OBJACCT_PREFIX_LEN;

	for (zap_cursor_init_serialized(&zc, zfsvfs->z_os, obj, *cookiep);
	    (error = zap_cursor_retrieve(&zc, &za)) == 0;
	    zap_cursor_advance(&zc)) {
		if ((uintptr_t)buf - (uintptr_t)vbuf + sizeof (zfs_useracct_t) >
		    *bufsizep)
			break;

		/*
		 * skip object quota (with zap name prefix DMU_OBJACCT_PREFIX)
		 * when dealing with block quota and vice versa.
		 */
		if ((offset > 0) != (strncmp(za.za_name, DMU_OBJACCT_PREFIX,
		    DMU_OBJACCT_PREFIX_LEN) == 0))
			continue;

		fuidstr_to_sid(zfsvfs, za.za_name + offset,
		    buf->zu_domain, sizeof (buf->zu_domain), &buf->zu_rid);

		buf->zu_space = za.za_first_integer;
		buf++;
	}
	if (error == ENOENT)
		error = 0;

	ASSERT3U((uintptr_t)buf - (uintptr_t)vbuf, <=, *bufsizep);
	*bufsizep = (uintptr_t)buf - (uintptr_t)vbuf;
	*cookiep = zap_cursor_serialize(&zc);
	zap_cursor_fini(&zc);
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
	err = dmu_tx_assign(tx, TXG_WAIT);
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

	if (quota == 0) {
		err = zap_remove(zfsvfs->z_os, *objp, buf, tx);
		if (err == ENOENT)
			err = 0;
	} else {
		err = zap_update(zfsvfs->z_os, *objp, buf, 8, 1, &quota, tx);
	}
	ASSERT(err == 0);
	if (fuid_dirtied)
		zfs_fuid_sync(zfsvfs, tx);
	dmu_tx_commit(tx);
	return (err);
}

boolean_t
zfs_id_overobjquota(zfsvfs_t *zfsvfs, uint64_t usedobj, uint64_t id)
{
	char buf[20 + DMU_OBJACCT_PREFIX_LEN];
	uint64_t used, quota, quotaobj;
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
	} else if (usedobj == DMU_USERUSED_OBJECT) {
		quotaobj = zfsvfs->z_userobjquota_obj;
	} else if (usedobj == DMU_GROUPUSED_OBJECT) {
		quotaobj = zfsvfs->z_groupobjquota_obj;
	} else {
		return (B_FALSE);
	}
	if (quotaobj == 0 || zfsvfs->z_replay)
		return (B_FALSE);

	(void) snprintf(buf, sizeof (buf), "%llx", (longlong_t)id);
	err = zap_lookup(zfsvfs->z_os, quotaobj, buf, 8, 1, &quota);
	if (err != 0)
		return (B_FALSE);

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
	uint64_t used, quota, quotaobj;
	int err;

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
	} else if (usedobj == DMU_USERUSED_OBJECT) {
		quotaobj = zfsvfs->z_userquota_obj;
	} else if (usedobj == DMU_GROUPUSED_OBJECT) {
		quotaobj = zfsvfs->z_groupquota_obj;
	} else {
		return (B_FALSE);
	}
	if (quotaobj == 0 || zfsvfs->z_replay)
		return (B_FALSE);

	(void) snprintf(buf, sizeof (buf), "%llx", (longlong_t)id);
	err = zap_lookup(zfsvfs->z_os, quotaobj, buf, 8, 1, &quota);
	if (err != 0)
		return (B_FALSE);

	err = zap_lookup(zfsvfs->z_os, usedobj, buf, 8, 1, &used);
	if (err != 0)
		return (B_FALSE);
	return (used >= quota);
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
EXPORT_SYMBOL(zfs_id_overblockquota);
EXPORT_SYMBOL(zfs_id_overobjquota);
EXPORT_SYMBOL(zfs_id_overquota);
