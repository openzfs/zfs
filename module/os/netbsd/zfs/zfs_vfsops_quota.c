// SPDX-License-Identifier: CDDL-1.0
/*
 * NetBSD vfs_quotactl (struct quotactl_args) and vfs_fsync.
 * Linux has no VFS quota op; userquota is a dataset property.
 * FreeBSD still uses Q_GETQUOTA/dqblk64.  NetBSD 11 does not.
 */

#include <sys/types.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/vnode.h>
#include <sys/mount.h>
#include <sys/kauth.h>
#include <sys/quota.h>
#include <sys/quotactl.h>
#include <sys/zfs_context.h>
#include <sys/zfs_vfsops.h>
#include <sys/zfs_znode.h>
#include <sys/zfs_vnops.h>
#include <sys/fs/zfs.h>
#include <sys/zap.h>
#include <sys/dmu.h>
#include <sys/zfs_ioctl.h>

extern int zfs_set_userquota(zfsvfs_t *, zfs_userquota_prop_t,
    const char *, uint64_t, uint64_t);
extern int zfs_userspace_many(zfsvfs_t *, uint_t, uint64_t *,
    void *, uint64_t *, uint64_t *);

static void
zfs_nb_quota_objs(zfsvfs_t *zfsvfs, int idtype, int objtype,
    uint64_t *usedobjp, uint64_t *quotaobjp, uint64_t *defquotap)
{
	const int isgroup = (idtype == QUOTA_IDTYPE_GROUP);

	/*
	 * Block and object accounting share DMU_{USER,GROUP}USED_OBJECT.
	 * There is no separate *OBJUSED ZAP; file counts live under the
	 * "obj-" key prefix (DMU_OBJACCT_PREFIX) in that same object.
	 * zfs_nb_quota_get() must use the prefix for FILES usage.
	 */
	if (objtype == QUOTA_OBJTYPE_FILES) {
		*usedobjp = isgroup ?
		    DMU_GROUPUSED_OBJECT : DMU_USERUSED_OBJECT;
		*quotaobjp = isgroup ? zfsvfs->z_groupobjquota_obj :
		    zfsvfs->z_userobjquota_obj;
		*defquotap = isgroup ? zfsvfs->z_defaultgroupobjquota :
		    zfsvfs->z_defaultuserobjquota;
	} else {
		*usedobjp = isgroup ?
		    DMU_GROUPUSED_OBJECT : DMU_USERUSED_OBJECT;
		*quotaobjp = isgroup ? zfsvfs->z_groupquota_obj :
		    zfsvfs->z_userquota_obj;
		*defquotap = isgroup ? zfsvfs->z_defaultgroupquota :
		    zfsvfs->z_defaultuserquota;
	}
}

static zfs_userquota_prop_t
zfs_nb_quota_prop(int idtype, int objtype, int want_used)
{
	const int isgroup = (idtype == QUOTA_IDTYPE_GROUP);

	if (objtype == QUOTA_OBJTYPE_FILES) {
		if (want_used)
			return (isgroup ? ZFS_PROP_GROUPOBJUSED :
			    ZFS_PROP_USEROBJUSED);
		return (isgroup ? ZFS_PROP_GROUPOBJQUOTA :
		    ZFS_PROP_USEROBJQUOTA);
	}
	if (want_used)
		return (isgroup ? ZFS_PROP_GROUPUSED : ZFS_PROP_USERUSED);
	return (isgroup ? ZFS_PROP_GROUPQUOTA : ZFS_PROP_USERQUOTA);
}

static int
zfs_nb_quota_get(zfsvfs_t *zfsvfs, const struct quotakey *qk,
    struct quotaval *qv)
{
	char buf[32];
	uint64_t usedobj, quotaobj, defquota, quota = 0, used = 0;
	int error;

	if (qk->qk_idtype != QUOTA_IDTYPE_USER &&
	    qk->qk_idtype != QUOTA_IDTYPE_GROUP)
		return (EINVAL);
	if (qk->qk_objtype != QUOTA_OBJTYPE_BLOCKS &&
	    qk->qk_objtype != QUOTA_OBJTYPE_FILES)
		return (EINVAL);

	memset(qv, 0, sizeof (*qv));
	qv->qv_hardlimit = QUOTA_NOLIMIT;
	qv->qv_softlimit = QUOTA_NOLIMIT;
	qv->qv_expiretime = QUOTA_NOTIME;
	qv->qv_grace = QUOTA_NOTIME;

	zfs_nb_quota_objs(zfsvfs, qk->qk_idtype, qk->qk_objtype,
	    &usedobj, &quotaobj, &defquota);

	if (qk->qk_id == QUOTA_DEFAULTID) {
		if (defquota != 0) {
			qv->qv_hardlimit = defquota;
			qv->qv_softlimit = defquota;
		}
		return (0);
	}

	(void) snprintf(buf, sizeof (buf), "%llx", (longlong_t)qk->qk_id);
	if (quotaobj != 0) {
		error = zap_lookup(zfsvfs->z_os, quotaobj, buf, 8, 1, &quota);
		if (error == ENOENT)
			quota = defquota;
		else if (error != 0)
			return (error);
	} else {
		quota = defquota;
	}
	if (quota != 0) {
		qv->qv_hardlimit = quota;
		qv->qv_softlimit = quota;
	}

	if (qk->qk_objtype == QUOTA_OBJTYPE_FILES) {
		(void) snprintf(buf, sizeof (buf),
		    DMU_OBJACCT_PREFIX "%llx", (longlong_t)qk->qk_id);
	} else {
		(void) snprintf(buf, sizeof (buf), "%llx",
		    (longlong_t)qk->qk_id);
	}
	error = zap_lookup(zfsvfs->z_os, usedobj, buf, 8, 1, &used);
	if (error == ENOENT)
		used = 0;
	else if (error != 0)
		return (error);
	qv->qv_usage = used;
	return (0);
}

static int
zfs_nb_quota_put(zfsvfs_t *zfsvfs, const struct quotakey *qk,
    const struct quotaval *qv)
{
	uint64_t limit;

	if (qk->qk_idtype != QUOTA_IDTYPE_USER &&
	    qk->qk_idtype != QUOTA_IDTYPE_GROUP)
		return (EINVAL);
	if (qk->qk_objtype != QUOTA_OBJTYPE_BLOCKS &&
	    qk->qk_objtype != QUOTA_OBJTYPE_FILES)
		return (EINVAL);
	if (zfs_is_readonly(zfsvfs))
		return (EROFS);
	if (qk->qk_id == QUOTA_DEFAULTID)
		return (EOPNOTSUPP);

	limit = qv->qv_hardlimit;
	if (limit == QUOTA_NOLIMIT)
		limit = qv->qv_softlimit;
	if (limit == QUOTA_NOLIMIT)
		limit = 0;

	return (zfs_set_userquota(zfsvfs,
	    zfs_nb_quota_prop(qk->qk_idtype, qk->qk_objtype, 0),
	    "", (uint64_t)qk->qk_id, limit));
}

struct zfs_qkc {
	uint64_t	cookie;
	int		idtype;
	int		objtype;
	int		opened;
};

#ifdef CTASSERT
CTASSERT(sizeof (struct zfs_qkc) <= 64);
#endif

/*
 * Match UFS ufs_quota.c for USER: own-id GET is always allowed.
 * UFS compares only euid and so lets a user read the group quota
 * whose gid equals their uid.  Distinguish idtype: USER matches
 * euid; GROUP matches egid or a supplementary group.
 */
static int
zfs_nb_quota_get_auth(vfs_t *vfsp, const struct quotakey *qk)
{
	kauth_cred_t cr = curlwp->l_cred;
	id_t id;
	int ismember = 0;

	if (qk == NULL)
		return (EINVAL);
	id = qk->qk_id;
	if (qk->qk_idtype == QUOTA_IDTYPE_USER) {
		if (id == kauth_cred_geteuid(cr))
			return (0);
	} else if (qk->qk_idtype == QUOTA_IDTYPE_GROUP) {
		if (id == kauth_cred_getegid(cr))
			return (0);
		if (kauth_cred_ismember_gid(cr, (gid_t)id, &ismember) == 0 &&
		    ismember)
			return (0);
	}
	return (kauth_authorize_system(cr, KAUTH_SYSTEM_FS_QUOTA,
	    KAUTH_REQ_SYSTEM_FS_QUOTA_GET, vfsp, KAUTH_ARG(id), NULL));
}

/*
 * PUT/DEL remap QUOTA_DEFAULTID to 0 — setting the default is root.
 */
static int
zfs_nb_quota_manage_auth(vfs_t *vfsp, id_t id)
{
	if (id == QUOTA_DEFAULTID)
		id = 0;
	return (kauth_authorize_system(curlwp->l_cred, KAUTH_SYSTEM_FS_QUOTA,
	    KAUTH_REQ_SYSTEM_FS_QUOTA_MANAGE, vfsp, KAUTH_ARG(id), NULL));
}

static int
zfs_nb_quota_cursor_auth(vfs_t *vfsp)
{
	return (kauth_authorize_system(curlwp->l_cred, KAUTH_SYSTEM_FS_QUOTA,
	    KAUTH_REQ_SYSTEM_FS_QUOTA_GET, vfsp, NULL, NULL));
}

/*
 * One cursor step.  Walks BLOCKS then FILES for the current idtype.
 * ENOENT means this idtype is exhausted (not a syscall error).
 */
static int
zfs_nb_quota_cursor_next(zfsvfs_t *zfsvfs, struct zfs_qkc *kc,
    struct quotakey *key, struct quotaval *val)
{
	zfs_useracct_t acct;
	uint64_t bufsz, dummy;
	zfs_userquota_prop_t type;
	int error;

	for (;;) {
		bufsz = sizeof (acct);
		dummy = 0;
		type = zfs_nb_quota_prop(kc->idtype, kc->objtype, 1);
		error = zfs_userspace_many(zfsvfs, type, &kc->cookie,
		    &acct, &bufsz, &dummy);
		if (error == ENOTSUP) {
			error = 0;
			bufsz = 0;
		}
		if (error != 0)
			return (error);
		if (bufsz >= sizeof (acct)) {
			key->qk_idtype = kc->idtype;
			key->qk_id = (id_t)acct.zu_rid;
			key->qk_objtype = kc->objtype;
			return (zfs_nb_quota_get(zfsvfs, key, val));
		}
		if (kc->objtype == QUOTA_OBJTYPE_BLOCKS) {
			kc->objtype = QUOTA_OBJTYPE_FILES;
			kc->cookie = 0;
			continue;
		}
		return (ENOENT);
	}
}

int
zfs_netbsd_quotactl(vfs_t *vfsp, struct quotactl_args *args)
{
	zfsvfs_t *zfsvfs = vfsp->mnt_data;
	struct zfs_qkc *kc;
	int error = 0;

	if (zfsvfs == NULL || args == NULL)
		return (EINVAL);
	if ((error = zfs_enter(zfsvfs, FTAG)) != 0)
		return (error);

	switch (args->qc_op) {
	case QUOTACTL_STAT: {
		struct quotastat *qs = args->u.stat.qc_info;

		memset(qs, 0, sizeof (*qs));
		(void) strlcpy(qs->qs_implname, "zfs",
		    sizeof (qs->qs_implname));
		qs->qs_numidtypes = 2;
		qs->qs_numobjtypes = 2;
		qs->qs_restrictions = QUOTA_RESTRICT_UNIFORMGRACE;
		break;
	}
	case QUOTACTL_IDTYPESTAT: {
		struct quotaidtypestat *is = args->u.idtypestat.qc_info;
		int idt = args->u.idtypestat.qc_idtype;

		memset(is, 0, sizeof (*is));
		if (idt == QUOTA_IDTYPE_USER)
			(void) strlcpy(is->qis_name, "user",
			    sizeof (is->qis_name));
		else if (idt == QUOTA_IDTYPE_GROUP)
			(void) strlcpy(is->qis_name, "group",
			    sizeof (is->qis_name));
		else
			error = EINVAL;
		break;
	}
	case QUOTACTL_OBJTYPESTAT: {
		struct quotaobjtypestat *os = args->u.objtypestat.qc_info;
		int ot = args->u.objtypestat.qc_objtype;

		memset(os, 0, sizeof (*os));
		if (ot == QUOTA_OBJTYPE_BLOCKS) {
			(void) strlcpy(os->qos_name, "block",
			    sizeof (os->qos_name));
			os->qos_isbytes = 1;
		} else if (ot == QUOTA_OBJTYPE_FILES) {
			(void) strlcpy(os->qos_name, "file",
			    sizeof (os->qos_name));
			os->qos_isbytes = 0;
		} else {
			error = EINVAL;
		}
		break;
	}
	case QUOTACTL_GET:
		error = zfs_nb_quota_get_auth(vfsp, args->u.get.qc_key);
		if (error == 0)
			error = zfs_nb_quota_get(zfsvfs, args->u.get.qc_key,
			    args->u.get.qc_val);
		break;
	case QUOTACTL_PUT:
		error = zfs_nb_quota_manage_auth(vfsp,
		    args->u.put.qc_key->qk_id);
		if (error == 0)
			error = zfs_nb_quota_put(zfsvfs, args->u.put.qc_key,
			    args->u.put.qc_val);
		break;
	case QUOTACTL_DEL: {
		struct quotaval zv;

		error = zfs_nb_quota_manage_auth(vfsp,
		    args->u.del.qc_key->qk_id);
		if (error != 0)
			break;
		memset(&zv, 0, sizeof (zv));
		zv.qv_hardlimit = QUOTA_NOLIMIT;
		zv.qv_softlimit = QUOTA_NOLIMIT;
		zv.qv_expiretime = QUOTA_NOTIME;
		zv.qv_grace = QUOTA_NOTIME;
		error = zfs_nb_quota_put(zfsvfs, args->u.del.qc_key, &zv);
		break;
	}
	case QUOTACTL_QUOTAON:
	case QUOTACTL_QUOTAOFF:
		/*
		 * ZFS quotas are dataset properties, not a toggle.
		 * Authorize like UFS then fail — quotaon(8) as root
		 * gets EOPNOTSUPP, not a false success.
		 */
		error = kauth_authorize_system(curlwp->l_cred,
		    KAUTH_SYSTEM_FS_QUOTA, KAUTH_REQ_SYSTEM_FS_QUOTA_ONOFF,
		    vfsp, NULL, NULL);
		if (error == 0)
			error = EOPNOTSUPP;
		break;
	case QUOTACTL_CURSOROPEN:
		error = zfs_nb_quota_cursor_auth(vfsp);
		if (error != 0)
			break;
		kc = (struct zfs_qkc *)args->u.cursoropen.qc_cursor;
		memset(kc, 0, sizeof (*kc));
		kc->opened = 1;
		break;
	case QUOTACTL_CURSORCLOSE:
		error = zfs_nb_quota_cursor_auth(vfsp);
		if (error != 0)
			break;
		kc = (struct zfs_qkc *)args->u.cursorclose.qc_cursor;
		if (kc->opened == 0) {
			error = EINVAL;
			break;
		}
		memset(kc, 0, sizeof (*kc));
		break;
	case QUOTACTL_CURSORREWIND:
		kc = (struct zfs_qkc *)args->u.cursorrewind.qc_cursor;
		if (kc->opened == 0) {
			error = EINVAL;
			break;
		}
		kc->cookie = 0;
		kc->objtype = QUOTA_OBJTYPE_BLOCKS;
		break;
	case QUOTACTL_CURSORSKIPIDTYPE:
		kc = (struct zfs_qkc *)args->u.cursorskipidtype.qc_cursor;
		if (kc->opened == 0) {
			error = EINVAL;
			break;
		}
		kc->idtype = args->u.cursorskipidtype.qc_idtype;
		kc->objtype = QUOTA_OBJTYPE_BLOCKS;
		kc->cookie = 0;
		break;
	case QUOTACTL_CURSORGET: {
		unsigned n = 0;

		error = zfs_nb_quota_cursor_auth(vfsp);
		if (error != 0)
			break;
		kc = (struct zfs_qkc *)args->u.cursorget.qc_cursor;
		if (kc->opened == 0) {
			error = EINVAL;
			break;
		}
		if (args->u.cursorget.qc_maxnum == 0) {
			*args->u.cursorget.qc_ret = 0;
			break;
		}
		error = zfs_nb_quota_cursor_next(zfsvfs, kc,
		    &args->u.cursorget.qc_keys[0],
		    &args->u.cursorget.qc_vals[0]);
		if (error == ENOENT) {
			error = 0;
			n = 0;
		} else if (error == 0) {
			n = 1;
		}
		*args->u.cursorget.qc_ret = n;
		break;
	}
	case QUOTACTL_CURSORATEND: {
		struct zfs_qkc tmp;
		struct quotakey qk;
		struct quotaval qv;

		kc = (struct zfs_qkc *)args->u.cursoratend.qc_cursor;
		if (kc->opened == 0) {
			error = EINVAL;
			break;
		}
		tmp = *kc;
		error = zfs_nb_quota_cursor_next(zfsvfs, &tmp, &qk, &qv);
		*args->u.cursoratend.qc_ret = (error == ENOENT);
		error = 0;
		break;
	}
	default:
		error = EOPNOTSUPP;
		break;
	}

	zfs_exit(zfsvfs, FTAG);
	return (error);
}

int
zfs_netbsd_vfs_fsync(vnode_t *vp, int flags)
{
	znode_t *zp;

	/*
	 * Native osnet leaves vfs_fsync as eopnotsupp (VBLK
	 * cache flush).  We map it to per-file zfs_fsync like
	 * Linux zpl_fsync.  Same reclaim/lazy skip as VOP_FSYNC.
	 * Periodic vfs_sync is zfs_sync() and already no-ops
	 * MNT_LAZY; this hook is the per-vnode path.
	 */
	if (vp == NULL || vp->v_data == NULL)
		return (0);
	if ((flags & (FSYNC_RECLAIM | FSYNC_LAZY)) != 0)
		return (0);
	zp = VTOZ(vp);
	if (zp == NULL || zp->z_zfsvfs == NULL)
		return (0);
	return (zfs_fsync(zp, flags, kcred));
}
