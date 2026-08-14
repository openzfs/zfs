// SPDX-License-Identifier: BSD-2-Clause
/*
 * NetBSD mount-option helpers.
 *
 * NetBSD 11.0 has no vfsopt TAILQ (FreeBSD) and no Illumos
 * vfs_mntopts string.  mount(8) / getvfsstat(2) print
 * statvfs.f_flag, which is a copy of mnt_flag.  Native osnet
 * ZFS therefore maps MNTOPT_* onto MNT_* bits — the same
 * thing FFS does — rather than keeping a parallel option
 * list.  vfs_setmntopt / vfs_clearmntopt do that mapping
 * so `zfs set atime=off` shows up as (noatime) in mount(8).
 *
 * Options with no MNT_* bit (nbmand, xattr, …) stay silent:
 * there is nowhere in struct mount to put them.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/mount.h>
#include <sys/vnode.h>
#include <sys/vfs.h>
#include <sys/kmem.h>
#include <sys/mntent.h>
#include <sys/taskq.h>
#include <sys/debug.h>

struct mntopt_bit {
	const char	*name;
	int		setbits;
	int		clrbits;
};

static const struct mntopt_bit mntopt_map[] = {
	{ MNTOPT_RO,		MNT_RDONLY,	0 },
	{ MNTOPT_RW,		0,		MNT_RDONLY },
	{ MNTOPT_NOEXEC,	MNT_NOEXEC,	0 },
	{ MNTOPT_EXEC,		0,		MNT_NOEXEC },
	{ MNTOPT_NOSETUID,	MNT_NOSUID,	0 },
	{ MNTOPT_SETUID,	0,		MNT_NOSUID },
	{ "nosuid",		MNT_NOSUID,	0 },
	{ "suid",		0,		MNT_NOSUID },
	{ MNTOPT_NODEVICES,	MNT_NODEV,	0 },
	{ MNTOPT_DEVICES,	0,		MNT_NODEV },
	{ "nodev",		MNT_NODEV,	0 },
	{ "dev",		0,		MNT_NODEV },
	{ "nodevices",		MNT_NODEV,	0 },
	{ "devices",		0,		MNT_NODEV },
	{ MNTOPT_NOATIME,	MNT_NOATIME,	0 },
	{ MNTOPT_ATIME,		0,		MNT_NOATIME },
#ifdef MNT_RELATIME
	{ MNTOPT_RELATIME,	MNT_RELATIME,	0 },
	{ MNTOPT_NORELATIME,	0,		MNT_RELATIME },
#endif
#ifdef MNT_SYNCHRONOUS
	{ MNTOPT_SYNC,		MNT_SYNCHRONOUS, 0 },
#endif
#ifdef MNT_ASYNC
	{ MNTOPT_ASYNC,		MNT_ASYNC,	0 },
#endif
};

/*
 * Options with no MNT_* bit (noxattr, xattr, nbmand).  struct mount
 * has nowhere to put them, and zfsvfs does not exist yet when
 * zfs_apply_mntopts() runs.  Remember the token until
 * zfs_register_callbacks() -> vfs_optionisset().
 */
#define	NB_STR_NOXATTR		0x0001
#define	NB_STR_XATTR		0x0002
#define	NB_STR_DIRXATTR		0x0004
#define	NB_STR_SAXATTR		0x0008
#define	NB_STR_NBMAND		0x0010
#define	NB_STR_NONBMAND		0x0020
#define	NB_STR_XATTR_MASK	(NB_STR_NOXATTR | NB_STR_XATTR | \
	NB_STR_DIRXATTR | NB_STR_SAXATTR)
#define	NB_STR_NBMAND_MASK	(NB_STR_NBMAND | NB_STR_NONBMAND)

struct nb_strmnt {
	vfs_t		*sm_vfs;
	uint32_t	sm_bits;
	struct nb_strmnt *sm_next;
};

static kmutex_t nb_strmnt_lock;
static int nb_strmnt_ready;
static struct nb_strmnt *nb_strmnt_list;

static const struct {
	const char	*name;
	uint32_t	set;
	uint32_t	clr;
} nb_str_map[] = {
	{ MNTOPT_NOXATTR,	NB_STR_NOXATTR,	NB_STR_XATTR_MASK },
	{ MNTOPT_XATTR,		NB_STR_XATTR,	NB_STR_XATTR_MASK },
	{ MNTOPT_DIRXATTR,	NB_STR_DIRXATTR, NB_STR_XATTR_MASK },
	{ MNTOPT_SAXATTR,	NB_STR_SAXATTR,	NB_STR_XATTR_MASK },
	{ MNTOPT_NBMAND,	NB_STR_NBMAND,	NB_STR_NBMAND_MASK },
	{ MNTOPT_NONBMAND,	NB_STR_NONBMAND, NB_STR_NBMAND_MASK },
};

void
vfs_strmntopt_init(void)
{
	if (nb_strmnt_ready)
		return;
	mutex_init(&nb_strmnt_lock, NULL, MUTEX_DEFAULT, NULL);
	nb_strmnt_ready = 1;
}

void
vfs_strmntopt_fini(void)
{
	struct nb_strmnt *sm;

	if (!nb_strmnt_ready)
		return;
	mutex_enter(&nb_strmnt_lock);
	while ((sm = nb_strmnt_list) != NULL) {
		nb_strmnt_list = sm->sm_next;
		kmem_free(sm, sizeof (*sm));
	}
	mutex_exit(&nb_strmnt_lock);
	mutex_destroy(&nb_strmnt_lock);
	nb_strmnt_ready = 0;
}

static struct nb_strmnt *
nb_strmnt_find(vfs_t *vfsp)
{
	struct nb_strmnt *sm;

	for (sm = nb_strmnt_list; sm != NULL; sm = sm->sm_next) {
		if (sm->sm_vfs == vfsp)
			return (sm);
	}
	return (NULL);
}

void
vfs_strmntopt_purge(vfs_t *vfsp)
{
	struct nb_strmnt **pp, *sm;

	if (!nb_strmnt_ready || vfsp == NULL)
		return;
	mutex_enter(&nb_strmnt_lock);
	for (pp = &nb_strmnt_list; (sm = *pp) != NULL; pp = &sm->sm_next) {
		if (sm->sm_vfs != vfsp)
			continue;
		*pp = sm->sm_next;
		kmem_free(sm, sizeof (*sm));
		break;
	}
	mutex_exit(&nb_strmnt_lock);
}

static int
nb_strmnt_apply(vfs_t *vfsp, const char *name, int set)
{
	size_t i;
	struct nb_strmnt *sm, *fresh = NULL;

	if (!nb_strmnt_ready || vfsp == NULL || name == NULL)
		return (0);
	for (i = 0; i < __arraycount(nb_str_map); i++) {
		if (strcmp(name, nb_str_map[i].name) == 0)
			break;
	}
	if (i == __arraycount(nb_str_map))
		return (0);
	if (set)
		fresh = kmem_zalloc(sizeof (*fresh), KM_SLEEP);
	mutex_enter(&nb_strmnt_lock);
	sm = nb_strmnt_find(vfsp);
	if (!set) {
		if (sm != NULL)
			sm->sm_bits &= ~nb_str_map[i].set;
	} else if (sm != NULL) {
		sm->sm_bits &= ~nb_str_map[i].clr;
		sm->sm_bits |= nb_str_map[i].set;
	} else {
		fresh->sm_vfs = vfsp;
		fresh->sm_bits = nb_str_map[i].set;
		fresh->sm_next = nb_strmnt_list;
		nb_strmnt_list = fresh;
		fresh = NULL;
	}
	mutex_exit(&nb_strmnt_lock);
	if (fresh != NULL)
		kmem_free(fresh, sizeof (*fresh));
	return (1);
}

static int
nb_strmnt_isset(const vfs_t *vfsp, const char *opt)
{
	size_t i;
	struct nb_strmnt *sm;
	int found = 0;

	if (!nb_strmnt_ready || vfsp == NULL || opt == NULL)
		return (0);
	for (i = 0; i < __arraycount(nb_str_map); i++) {
		if (strcmp(opt, nb_str_map[i].name) == 0)
			break;
	}
	if (i == __arraycount(nb_str_map))
		return (0);
	mutex_enter(&nb_strmnt_lock);
	sm = nb_strmnt_find((vfs_t *)(uintptr_t)vfsp);
	if (sm != NULL && (sm->sm_bits & nb_str_map[i].set) != 0)
		found = 1;
	mutex_exit(&nb_strmnt_lock);
	return (found);
}

static void
mntopt_sync_stat(vfs_t *vfsp)
{
	vfsp->mnt_stat.f_flag = vfsp->mnt_flag;
}

void
vfs_setmntopt(vfs_t *vfsp, const char *name, const char *arg, int flags)
{
	size_t i;

	(void) arg;
	(void) flags;
	if (vfsp == NULL || name == NULL)
		return;
	for (i = 0; i < __arraycount(mntopt_map); i++) {
		if (strcmp(name, mntopt_map[i].name) != 0)
			continue;
		vfsp->mnt_flag |= mntopt_map[i].setbits;
		vfsp->mnt_flag &= ~mntopt_map[i].clrbits;
		mntopt_sync_stat(vfsp);
		return;
	}
	(void) nb_strmnt_apply(vfsp, name, 1);
}

void
vfs_clearmntopt(vfs_t *vfsp, const char *name)
{
	size_t i;

	if (vfsp == NULL || name == NULL)
		return;
	/*
	 * Drop only the bits this option would have set.  Do not
	 * force the complementary polarity — the caller issues
	 * vfs_setmntopt() for the opposite name.
	 */
	for (i = 0; i < __arraycount(mntopt_map); i++) {
		if (strcmp(name, mntopt_map[i].name) != 0)
			continue;
		vfsp->mnt_flag &= ~mntopt_map[i].setbits;
		mntopt_sync_stat(vfsp);
		return;
	}
	(void) nb_strmnt_apply(vfsp, name, 0);
}

/*
 * Report only flags that are actually set on the mount.
 * Do not synthesize the opposite polarity (MNTOPT_SETUID
 * from !MNT_NOSUID, etc.).  zfs_register_callbacks treats a
 * true result as "the caller asked for this," and would
 * otherwise stomp dataset properties after the callbacks run.
 */
int
vfs_optionisset(const vfs_t *vfsp, const char *opt, char **argp)
{
	if (argp != NULL)
		*argp = NULL;
	if (vfsp == NULL || opt == NULL)
		return (0);
	if (strcmp(opt, MNTOPT_RO) == 0)
		return ((vfsp->mnt_flag & MNT_RDONLY) != 0);
	/*
	 * rw is the opposite of the rdonly bit, and do_mount already
	 * folded the last ro/rw token into that bit.  zfs_register_callbacks
	 * treats a true result as "the caller asked for this" and restores
	 * it after the readonly property callback.  Without this,
	 * `zfs mount -o rw` on a readonly=on dataset stays ro
	 * (zfs_mount_ro_rw).  Do not do the same for setuid/devices/exec:
	 * the clear bit is the default, not an explicit request.
	 */
	if (strcmp(opt, MNTOPT_RW) == 0)
		return ((vfsp->mnt_flag & MNT_RDONLY) == 0);
	if (strcmp(opt, MNTOPT_NOEXEC) == 0)
		return ((vfsp->mnt_flag & MNT_NOEXEC) != 0);
	if (strcmp(opt, MNTOPT_NOSETUID) == 0 || strcmp(opt, "nosuid") == 0)
		return ((vfsp->mnt_flag & MNT_NOSUID) != 0);
	if (strcmp(opt, MNTOPT_NODEVICES) == 0 || strcmp(opt, "nodev") == 0)
		return ((vfsp->mnt_flag & MNT_NODEV) != 0);
	if (strcmp(opt, MNTOPT_NOATIME) == 0)
		return ((vfsp->mnt_flag & MNT_NOATIME) != 0);
#ifdef MNT_RELATIME
	if (strcmp(opt, MNTOPT_RELATIME) == 0)
		return ((vfsp->mnt_flag & MNT_RELATIME) != 0);
#endif
	if (nb_strmnt_isset(vfsp, opt))
		return (1);
	return (0);
}

int
mount_snapshot(kthread_t *td, vnode_t **vpp, const char *fstype,
    char *fspath, char *fspec, int fsflags)
{
	(void) td;
	(void) vpp;
	(void) fstype;
	(void) fspath;
	(void) fspec;
	(void) fsflags;
	return (SET_ERROR(EOPNOTSUPP));
}

static void
vn_rele_task(void *arg)
{
	vrele(arg);
}

/*
 * Drop a vnode reference on taskq so zfsvfs_teardown()'s
 * taskq_wait_outstanding(dsl_pool_zrele_taskq()) can observe it.
 *
 * TQ_SLEEP dispatch never returns TASKQID_INVALID (kmem KM_SLEEP
 * and the queue is still live — teardown waits on this taskq
 * before destroy).  VERIFY that contract; do not vrele inline
 * and hide a leak or a use-after-destroy.
 */
void
vn_rele_async(vnode_t *vp, struct taskq *taskq)
{
	taskqid_t id;

	VERIFY(vp != NULL);
	VERIFY(taskq != NULL);
	id = taskq_dispatch(taskq, vn_rele_task, vp, TQ_SLEEP);
	VERIFY3U(id, !=, TASKQID_INVALID);
}
