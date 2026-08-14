// SPDX-License-Identifier: CDDL-1.0
/*
 * NetBSD ZVOL OS layer.
 *
 * Character and block devices via cdevsw/bdevsw plus disk(9):
 *   /dev/zvol/<pool>/<dataset>   block
 *   /dev/rzvol/<pool>/<dataset>  raw (character)
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/conf.h>
#include <sys/kmem.h>
#include <sys/vnode.h>
#include <sys/namei.h>
#include <sys/fcntl.h>
#include <sys/stat.h>
#include <sys/buf.h>
#include <sys/disk.h>
#include <sys/disklabel.h>
#include <sys/ioctl.h>
#include <sys/dkio.h>
#include <sys/syscall.h>
#include <sys/proc.h>
#include <sys/cpu.h>
#include <sys/vfs_syscalls.h>
#include <miscfs/specfs/specdev.h>
#include <uvm/uvm.h>
#include <uvm/uvm_extern.h>

#include <sys/spa.h>
#include <sys/spa_impl.h>
#include <sys/dmu.h>
#include <sys/dmu_tx.h>
#include <sys/sysmacros.h>
#include <sys/zap.h>
#include <sys/zil.h>
#include <sys/zfs_rlock.h>
#include <sys/zvol.h>
#include <sys/zvol_impl.h>
#include <sys/dataset_kstats.h>
#include <sys/dmu_objset.h>
#include <sys/dsl_prop.h>
#include <sys/fs/zfs.h>
#include <sys/taskq.h>
#include <sys/workqueue.h>

#define	ZVOL_RROOT		"/dev/rzvol"

struct zvol_state_os {
	int		zso_minor;
	dev_t		zso_cdev;
	dev_t		zso_bdev;
	boolean_t	zso_dying;
	boolean_t	zso_draining;
	uint32_t	zso_secsize;
	char		zso_dkname[16];
	struct disk	zso_dk;
	kmutex_t	zso_dklock;
	boolean_t	zso_dk_attached;
	/*
	 * Two pins, one cv.  I/O (strategy/read/write/ioctl/discard)
	 * takes zso_inflight.  Open/close/psize take
	 * zso_devholds.  remove_minor waits only for I/O -- it holds
	 * zv_state_lock, and last-close needs that lock after its
	 * own pin.  Mixing the closer's pin into inflight is an AB
	 * deadlock on export / volmode / destroy.  zvol_os_free
	 * waits for zso_devholds so the closer can finish last_close
	 * without the zvol being freed under it.
	 */
	uint32_t	zso_inflight;
	uint32_t	zso_devholds;
	/* Per spec-vnode last-close. Block and raw share zv_open_count. */
	uint32_t	zso_bopens;
	uint32_t	zso_copens;
	/*
	 * LWP inside our DIOCMWEDGES ioctl, and only while
	 * disk_ioctl() is on that lwp's stack.  dkwedge_discover()
	 * re-opens this device read-only on that same lwp
	 * (character node, then maybe the block node).  A thread
	 * cannot exit mid-syscall, so a non-NULL value names a
	 * live lwp: the address has not been reused.
	 *
	 * Do not record the lwp that opened O_EXCL.  The fd
	 * outlives that thread (another thread, fork, or a
	 * passed descriptor).  The lwp structure is then freed
	 * and the address can be recycled, so a later unrelated
	 * open would compare equal and slip past the lock.
	 */
	struct lwp	*zso_discover_lwp;
};

static uint32_t		zvol_minors;
static uint32_t		zvol_minor_next = 1;
static kmutex_t		zvol_minor_lock;
static kcondvar_t	zvol_minor_cv;
static devmajor_t	zvol_bmajor = -1, zvol_cmajor = -1;
static int		zvol_devsw_attached;
static struct workqueue	*zvol_strat_wq;
static kmutex_t		zvol_strat_lock;
static kcondvar_t	zvol_strat_cv;
static unsigned		zvol_strat_busy;

int zvol_major = 0;
boolean_t zvol_unmap_enabled = B_TRUE;
int zvol_maxphys = DMU_MAX_ACCESS / 2;

/*
 * Swap-on-zvol needs a non-sleeping page map.  That is only
 * PMAP_DIRECT_MAP.  NetBSD has no bdevsw "this may be swap"
 * flag: swap_on() only calls VOP_OPEN and d_psize, then marks
 * the device live.  Reject those from swapctl(2) on arches
 * without a direct map so activation cannot succeed and then
 * fail from the pagedaemon.  Detect the syscall by l_sysent
 * offset (SYS_swapctl), including via the syscall() wrapper
 * (sy_call() retargets l_sysent).  Not a process-name check.
 * Strategy still refuses pagedaemon / interrupt as a fallback.
 */
#ifdef PMAP_DIRECT_MAP
#define	ZVOL_SWAP_OK	1
#else
#define	ZVOL_SWAP_OK	0
#endif

#if !ZVOL_SWAP_OK
static boolean_t
zvol_lwp_is_swapctl(struct lwp *l)
{
	const struct sysent *se;
	const struct emul *e;
	ptrdiff_t off;

	if (l == NULL || l->l_proc == NULL)
		return (B_FALSE);
	se = l->l_sysent;
	e = l->l_proc->p_emul;
	if (se == NULL || e == NULL || e->e_sysent == NULL)
		return (B_FALSE);
	off = se - e->e_sysent;
	return (off == (ptrdiff_t)SYS_swapctl);
}
#endif

static int zvol_cdev_open(dev_t, int, int, struct lwp *);
static int zvol_cdev_close(dev_t, int, int, struct lwp *);
static int zvol_cdev_read(dev_t, struct uio *, int);
static int zvol_cdev_write(dev_t, struct uio *, int);
static int zvol_cdev_ioctl(dev_t, unsigned long, void *, int, struct lwp *);
static int zvol_cdev_discard(dev_t, off_t, off_t);
static void zvol_strategy(struct buf *);
static int zvol_psize(dev_t);
static void zvol_minphys(struct buf *);
static void zvol_fill_disklabel(zvol_state_t *, struct disklabel *);

static struct dkdriver zvol_dkdriver = {
	.d_strategy = zvol_strategy,
	.d_minphys = zvol_minphys,
};

static struct cdevsw zvol_cdevsw = {
	.d_open = zvol_cdev_open,
	.d_close = zvol_cdev_close,
	.d_read = zvol_cdev_read,
	.d_write = zvol_cdev_write,
	.d_ioctl = zvol_cdev_ioctl,
	.d_stop = nostop,
	.d_tty = notty,
	.d_poll = nopoll,
	.d_mmap = nommap,
	.d_kqfilter = nokqfilter,
	.d_discard = zvol_cdev_discard,
	.d_flag = D_DISK | D_MPSAFE
};

static struct bdevsw zvol_bdevsw = {
	.d_open = zvol_cdev_open,
	.d_close = zvol_cdev_close,
	.d_strategy = zvol_strategy,
	.d_ioctl = zvol_cdev_ioctl,
	.d_dump = nodump,
	.d_psize = zvol_psize,
	.d_discard = zvol_cdev_discard,
	.d_flag = D_DISK | D_MPSAFE
};

static int
zvol_isblk(int mode)
{
	return ((mode & S_IFMT) == S_IFBLK);
}

static uint32_t
zvol_opensum(zvol_state_t *zv)
{
	if (zv->zv_zso == NULL)
		return (zv->zv_open_count);
	return (zv->zv_zso->zso_bopens + zv->zv_zso->zso_copens);
}

static zvol_state_t *
zvol_find_by_minor(int unit)
{
	zvol_state_t *zv;
	struct hlist_node *p;
	int i;

	ASSERT(RW_LOCK_HELD(&zvol_state_lock));
	for (i = 0; i < ZVOL_HT_SIZE; i++) {
		hlist_for_each(p, ZVOL_HT_HEAD(i)) {
			zv = hlist_entry(p, zvol_state_t, zv_hlink);
			if (zv->zv_zso != NULL &&
			    zv->zv_zso->zso_minor == unit)
				return (zv);
		}
	}
	return (NULL);
}

static int
zvol_minor_claim(void)
{
	int unit, start;

	ASSERT(MUTEX_HELD(&zvol_minor_lock));
	rw_enter(&zvol_state_lock, RW_READER);
	start = zvol_minor_next;
	for (;;) {
		unit = (int)zvol_minor_next++;
		/*
		 * MAKEDISKDEV stores unit * MAXPARTITIONS + part.
		 * Minors are 20 bits.  Only RAW_PART is published,
		 * so stop before that encoding wraps.
		 */
		if (zvol_minor_next >
		    (0xfffff - RAW_PART) / MAXPARTITIONS)
			zvol_minor_next = 1;
		if (zvol_find_by_minor(unit) == NULL) {
			rw_exit(&zvol_state_lock);
			zvol_minors++;
			return (unit);
		}
		if (zvol_minor_next == (uint32_t)start) {
			rw_exit(&zvol_state_lock);
			return (-1);
		}
	}
}

/*
 * Pin so remove_minor cannot free zv between lookup and state_lock.
 * This is a *devhold*, not an I/O pin: remove_minor waits only for
 * zso_inflight, so a last-close that still holds this pin can take
 * zv_state_lock.  Open/psize reject a dying zvol.  Close must still
 * pin a dying zvol so the in-flight last close can drop opensum --
 * otherwise wait_close never sees zero.
 *
 * cdevsw only hands us a dev_t.  Walk zvol_htable (same list the
 * common layer uses) for zso_minor.  Lock order: zvol_minor_lock
 * then zvol_state_lock.
 */
static zvol_state_t *
zvol_hold_dev_impl(dev_t dev, boolean_t allow_dying)
{
	zvol_state_t *zv;
	int unit;

	/* Not a partition alias (a, b, ...).  Raw part only. */
	if (DISKPART(dev) != RAW_PART)
		return (NULL);
	unit = DISKUNIT(dev);
	if (unit <= 0)
		return (NULL);
	mutex_enter(&zvol_minor_lock);
	rw_enter(&zvol_state_lock, RW_READER);
	zv = zvol_find_by_minor(unit);
	if (zv == NULL) {
		rw_exit(&zvol_state_lock);
		mutex_exit(&zvol_minor_lock);
		return (NULL);
	}
	if (zv->zv_zso != NULL && zv->zv_zso->zso_dying && !allow_dying) {
		rw_exit(&zvol_state_lock);
		mutex_exit(&zvol_minor_lock);
		return (NULL);
	}
	if (zv->zv_zso != NULL)
		zv->zv_zso->zso_devholds++;
	rw_exit(&zvol_state_lock);
	mutex_exit(&zvol_minor_lock);
	return (zv);
}

static zvol_state_t *
zvol_hold_dev(dev_t dev)
{
	return (zvol_hold_dev_impl(dev, B_FALSE));
}

static void
zvol_rele_dev(zvol_state_t *zv)
{
	if (zv == NULL || zv->zv_zso == NULL)
		return;
	mutex_enter(&zvol_minor_lock);
	if (zv->zv_zso->zso_devholds > 0)
		zv->zv_zso->zso_devholds--;
	cv_broadcast(&zvol_minor_cv);
	mutex_exit(&zvol_minor_lock);
}

/*
 * Pin the minor across I/O so zvol_os_remove_minor cannot free zv
 * while strategy/read/write/ioctl is in flight.
 */
static zvol_state_t *
zvol_hold_io(dev_t dev)
{
	zvol_state_t *zv;
	int unit;

	if (DISKPART(dev) != RAW_PART)
		return (NULL);
	unit = DISKUNIT(dev);
	if (unit <= 0)
		return (NULL);
	mutex_enter(&zvol_minor_lock);
	rw_enter(&zvol_state_lock, RW_READER);
	zv = zvol_find_by_minor(unit);
	if (zv == NULL || zv->zv_objset == NULL ||
	    (zv->zv_zso != NULL &&
	    (zv->zv_zso->zso_dying || zv->zv_zso->zso_draining))) {
		rw_exit(&zvol_state_lock);
		mutex_exit(&zvol_minor_lock);
		return (NULL);
	}
	if (zv->zv_zso != NULL)
		zv->zv_zso->zso_inflight++;
	rw_exit(&zvol_state_lock);
	mutex_exit(&zvol_minor_lock);
	return (zv);
}

static void
zvol_rele_io(zvol_state_t *zv)
{
	if (zv == NULL || zv->zv_zso == NULL)
		return;
	mutex_enter(&zvol_minor_lock);
	if (zv->zv_zso->zso_inflight > 0)
		zv->zv_zso->zso_inflight--;
	cv_broadcast(&zvol_minor_cv);
	mutex_exit(&zvol_minor_lock);
}

static int
zvol_ensure_zilog(zvol_state_t *zv)
{
	/*
	 * Open a ZIL on first write.  Dropping the suspend lock
	 * to upgrade can race with suspend/rollback -- re-check
	 * after reacquire.
	 */
	if (zv->zv_zilog != NULL)
		return (0);
	if (!rw_tryupgrade(&zv->zv_suspend_lock)) {
		rw_exit(&zv->zv_suspend_lock);
		rw_enter(&zv->zv_suspend_lock, RW_WRITER);
	}
	if (zv->zv_objset == NULL || zv->zv_zso == NULL ||
	    zv->zv_zso->zso_dying) {
		rw_downgrade(&zv->zv_suspend_lock);
		return (SET_ERROR(ENXIO));
	}
	if (zv->zv_zilog == NULL) {
		zv->zv_zilog = zil_open(zv->zv_objset,
		    zvol_get_data, &zv->zv_kstat.dk_zil_sums);
		zv->zv_flags |= ZVOL_WRITTEN_TO;
	}
	rw_downgrade(&zv->zv_suspend_lock);
	return (0);
}

static int
zvol_mkdir_p(const char *path)
{
	char *buf;
	char *p;
	int error;

	buf = kmem_alloc(MAXPATHLEN, KM_SLEEP);
	if (strlcpy(buf, path, MAXPATHLEN) >= MAXPATHLEN) {
		kmem_free(buf, MAXPATHLEN);
		return (ENAMETOOLONG);
	}
	for (p = buf + 1; *p != '\0'; p++) {
		if (*p != '/')
			continue;
		*p = '\0';
		error = do_sys_mkdir(buf, 0755, UIO_SYSSPACE);
		if (error != 0 && error != EEXIST) {
			kmem_free(buf, MAXPATHLEN);
			return (error);
		}
		*p = '/';
	}
	error = do_sys_mkdir(buf, 0755, UIO_SYSSPACE);
	kmem_free(buf, MAXPATHLEN);
	if (error != 0 && error != EEXIST)
		return (error);
	return (0);
}

static int
zvol_mknod_one(const char *root, const char *name, mode_t mode, dev_t dev)
{
	char *path;
	char *slash;
	int error;

	path = kmem_alloc(MAXPATHLEN, KM_SLEEP);
	if (snprintf(path, MAXPATHLEN, "%s/%s", root, name) >= MAXPATHLEN) {
		kmem_free(path, MAXPATHLEN);
		return (ENAMETOOLONG);
	}
	slash = strrchr(path, '/');
	if (slash != NULL) {
		*slash = '\0';
		(void) zvol_mkdir_p(path);
		*slash = '/';
	}
	error = do_sys_mknod(curlwp, path, mode, dev, UIO_SYSSPACE);
	if (error == EEXIST) {
		(void) do_sys_unlink(path, UIO_SYSSPACE);
		error = do_sys_mknod(curlwp, path, mode, dev, UIO_SYSSPACE);
	}
	kmem_free(path, MAXPATHLEN);
	return (error);
}

static int
zvol_symlink_one(const char *root, const char *name, const char *target)
{
	char *path;
	char *slash;
	int error;

	path = kmem_alloc(MAXPATHLEN, KM_SLEEP);
	if (snprintf(path, MAXPATHLEN, "%s/%s", root, name) >= MAXPATHLEN) {
		kmem_free(path, MAXPATHLEN);
		return (ENAMETOOLONG);
	}
	slash = strrchr(path, '/');
	if (slash != NULL) {
		*slash = '\0';
		(void) zvol_mkdir_p(path);
		*slash = '/';
	}
	(void) do_sys_unlink(path, UIO_SYSSPACE);
	error = do_sys_symlink(target, path, UIO_SYSSPACE);
	kmem_free(path, MAXPATHLEN);
	return (error);
}

static int
zvol_unlink_one(const char *root, const char *name)
{
	char *path;
	int error;

	path = kmem_alloc(MAXPATHLEN, KM_SLEEP);
	if (snprintf(path, MAXPATHLEN, "%s/%s", root, name) >= MAXPATHLEN) {
		kmem_free(path, MAXPATHLEN);
		return (ENAMETOOLONG);
	}
	error = do_sys_unlink(path, UIO_SYSSPACE);
	kmem_free(path, MAXPATHLEN);
	if (error == ENOENT)
		error = 0;
	return (error);
}

static void zvol_nodes_remove(const char *name);

static int
zvol_nodes_create(const char *name, dev_t bdev, dev_t cdev)
{
	char *target;
	int error;

	/* NetBSD native: /dev/zvol/<name> block, /dev/rzvol/<name> raw. */
	error = zvol_mknod_one(ZVOL_ROOT, name, S_IFBLK | 0640, bdev);
	if (error != 0)
		goto fail;
	error = zvol_mknod_one(ZVOL_RROOT, name, S_IFCHR | 0640, cdev);
	if (error != 0)
		goto fail;
	/*
	 * Solaris / illumos aliases used by ZTS new_fs and dumpadm.
	 * Symlinks, not extra spec vnodes: d_open is per-open and
	 * d_close is last-close of that vnode.  A second block node
	 * on the same minor would share zso_bopens and last-close
	 * of one alias would zvol_last_close under the other.
	 */
	target = kmem_alloc(MAXPATHLEN, KM_SLEEP);
	if (snprintf(target, MAXPATHLEN, "%s/%s", ZVOL_ROOT, name) >=
	    MAXPATHLEN) {
		kmem_free(target, MAXPATHLEN);
		error = ENAMETOOLONG;
		goto fail;
	}
	error = zvol_symlink_one(ZVOL_ROOT "/dsk", name, target);
	if (error != 0) {
		kmem_free(target, MAXPATHLEN);
		goto fail;
	}
	if (snprintf(target, MAXPATHLEN, "%s/%s", ZVOL_RROOT, name) >=
	    MAXPATHLEN) {
		kmem_free(target, MAXPATHLEN);
		error = ENAMETOOLONG;
		goto fail;
	}
	error = zvol_symlink_one(ZVOL_ROOT "/rdsk", name, target);
	kmem_free(target, MAXPATHLEN);
	if (error != 0)
		goto fail;
	return (0);
fail:
	zvol_nodes_remove(name);
	return (error);
}

static void
zvol_nodes_remove(const char *name)
{
	(void) zvol_unlink_one(ZVOL_ROOT, name);
	(void) zvol_unlink_one(ZVOL_RROOT, name);
	(void) zvol_unlink_one(ZVOL_ROOT "/dsk", name);
	(void) zvol_unlink_one(ZVOL_ROOT "/rdsk", name);
}

static void
zvol_disk_set_info(zvol_state_t *zv, uint64_t volsize)
{
	struct disk_geom *dg;
	unsigned secsize;

	if (zv->zv_zso == NULL || !zv->zv_zso->zso_dk_attached)
		return;

	secsize = DEV_BSIZE;
	if (zv->zv_objset != NULL) {
		spa_t *spa = dmu_objset_spa(zv->zv_objset);
		unsigned ashift = 1U << spa->spa_max_ashift;

		if (ashift > secsize)
			secsize = ashift;
	}
	zv->zv_zso->zso_secsize = secsize;
	dg = &zv->zv_zso->zso_dk.dk_geom;
	memset(dg, 0, sizeof (*dg));
	dg->dg_secsize = secsize;
	dg->dg_secperunit = volsize / secsize;
	disk_set_info(NULL, &zv->zv_zso->zso_dk, "ZVOL");
	if (zv->zv_zso->zso_dk.dk_label != NULL)
		zvol_fill_disklabel(zv, zv->zv_zso->zso_dk.dk_label);
}

boolean_t
zvol_os_is_zvol(const char *device)
{
	if (device == NULL)
		return (B_FALSE);
	if (strncmp(device, ZVOL_ROOT "/", strlen(ZVOL_ROOT) + 1) == 0)
		return (B_TRUE);
	if (strncmp(device, ZVOL_RROOT "/", strlen(ZVOL_RROOT) + 1) == 0)
		return (B_TRUE);
	/*
	 * Solaris / illumos aliases.  n is the prefix length, not
	 * including the NUL -- sizeof(lit)-1, same as strlen(lit).
	 */
	if (strncmp(device, "/dev/zvol/dsk/",
	    sizeof ("/dev/zvol/dsk/") - 1) == 0)
		return (B_TRUE);
	if (strncmp(device, "/dev/zvol/rdsk/",
	    sizeof ("/dev/zvol/rdsk/") - 1) == 0)
		return (B_TRUE);
	return (B_FALSE);
}

static int
zvol_alloc(const char *name, uint64_t volsize, uint64_t volblocksize,
    zvol_state_t **zvp)
{
	zvol_state_t *zv;
	struct zvol_state_os *zso;
	uint64_t volmode;
	int error;

	error = dsl_prop_get_integer(name, zfs_prop_to_name(ZFS_PROP_VOLMODE),
	    &volmode, NULL);
	if (error)
		return (error);
	if (volmode == ZFS_VOLMODE_DEFAULT)
		volmode = zvol_volmode;
	if (volmode == ZFS_VOLMODE_NONE) {
		*zvp = NULL;
		return (0);
	}

	zv = kmem_zalloc(sizeof (*zv), KM_SLEEP);
	zso = kmem_zalloc(sizeof (*zso), KM_SLEEP);
	/*
	 * Initialise locks before any other thread can see zv.
	 * The minor is claimed later, after ZIL replay, so hold_io
	 * cannot observe a live objset with zv_dn still NULL.
	 */
	mutex_init(&zv->zv_state_lock, NULL, MUTEX_DEFAULT, NULL);
	cv_init(&zv->zv_removing_cv, NULL, CV_DEFAULT, NULL);
	rw_init(&zv->zv_suspend_lock, NULL, RW_DEFAULT, NULL);
	zfs_rangelock_init(&zv->zv_rangelock, NULL, NULL);
	(void) strlcpy(zv->zv_name, name, sizeof (zv->zv_name));
	zv->zv_volsize = volsize;
	zv->zv_volblocksize = volblocksize;
	/* default folded to zvol_volmode; none returned above. */
	zv->zv_volmode = volmode;
	zv->zv_zso = zso;
	zso->zso_secsize = DEV_BSIZE;
	mutex_init(&zso->zso_dklock, NULL, MUTEX_DEFAULT, NULL);
	/*
	 * disk_init/attach waits until the minor is claimed.
	 * iostat_alloc() uses dk_name as a sysctl component; a
	 * dataset name with '/' (testpool/testvol) panics or
	 * silently kills the ZTS process group.
	 */
	*zvp = zv;
	return (0);
}

int
zvol_os_create_minor(const char *name)
{
	zvol_state_t *zv = NULL;
	objset_t *os;
	dmu_object_info_t doi;
	uint64_t volsize;
	uint64_t hash;
	uint64_t len;
	int error;

	if (zvol_inhibit_dev)
		return (0);

	hash = zvol_name_hash(name);
	if ((zv = zvol_find_by_name_hash(name, hash, RW_NONE)) != NULL) {
		mutex_exit(&zv->zv_state_lock);
		return (SET_ERROR(EEXIST));
	}

	error = dmu_objset_own(name, DMU_OST_ZVOL, B_TRUE, B_TRUE, FTAG, &os);
	if (error)
		return (error);

	error = dmu_object_info(os, ZVOL_OBJ, &doi);
	if (error)
		goto out_disown;
	error = zap_lookup(os, ZVOL_ZAP_OBJ, "size", 8, 1, &volsize);
	if (error)
		goto out_disown;

	error = zvol_alloc(name, volsize, doi.doi_data_block_size, &zv);
	if (error || zv == NULL)
		goto out_disown;

	zv->zv_hash = hash;
	if (dmu_objset_is_snapshot(os) || !spa_writeable(dmu_objset_spa(os)))
		zv->zv_flags |= ZVOL_RDONLY;
	zv->zv_objset = os;
	error = dataset_kstats_create(&zv->zv_kstat, zv->zv_objset);
	if (error)
		goto out_free;
	zv->zv_zilog = zil_open(os, zvol_get_data, &zv->zv_kstat.dk_zil_sums);
	if (spa_writeable(dmu_objset_spa(os))) {
		if (zil_replay_disable)
			(void) zil_destroy(zv->zv_zilog, B_FALSE);
		else
			(void) zil_replay(os, zv, zvol_replay_vector);
	}
	/*
	 * zil_open is not refcounted: it installs zl_get_data on
	 * os->os_zil.  Close always so the next zil_open (first
	 * write) does not ASSERT0P(zl_get_data).  replayed_zil is
	 * "did we destroy a log", not "did we open".
	 */
	zil_close(zv->zv_zilog);
	zv->zv_zilog = NULL;

	len = MIN(zvol_prefetch_bytes, SPA_MAXBLOCKSIZE);
	if (len > 0) {
		dmu_prefetch(os, ZVOL_OBJ, 0, 0, len, ZIO_PRIORITY_ASYNC_READ);
		dmu_prefetch(os, ZVOL_OBJ, 0, volsize - len, len,
		    ZIO_PRIORITY_ASYNC_READ);
	}

	zv->zv_objset = NULL;
	dmu_objset_disown(os, B_TRUE, FTAG);
	/*
	 * Hold zvol_minor_lock from claim through insert so a
	 * concurrent create cannot reuse this minor before we
	 * are hashed.  Drop it before zvol_os_free.
	 */
	{
		int minor_num;
		int n;

		mutex_enter(&zvol_minor_lock);
		minor_num = zvol_minor_claim();
		if (minor_num < 0) {
			mutex_exit(&zvol_minor_lock);
			zvol_os_free(zv);
			return (SET_ERROR(ENOSPC));
		}
		zv->zv_zso->zso_minor = minor_num;
		zv->zv_zso->zso_cdev = MAKEDISKDEV(zvol_cmajor,
		    minor_num, RAW_PART);
		zv->zv_zso->zso_bdev = MAKEDISKDEV(zvol_bmajor,
		    minor_num, RAW_PART);
		n = snprintf(zv->zv_zso->zso_dkname,
		    sizeof (zv->zv_zso->zso_dkname), "zvol%d", minor_num);
		if (n < 0 || n >= (int)sizeof (zv->zv_zso->zso_dkname)) {
			mutex_exit(&zvol_minor_lock);
			zvol_os_free(zv);
			return (SET_ERROR(ENAMETOOLONG));
		}
		disk_init(&zv->zv_zso->zso_dk, zv->zv_zso->zso_dkname,
		    &zvol_dkdriver);
		disk_attach(&zv->zv_zso->zso_dk);
		zv->zv_zso->zso_dk_attached = B_TRUE;
		zvol_disk_set_info(zv, zv->zv_volsize);
		error = zvol_nodes_create(name, zv->zv_zso->zso_bdev,
		    zv->zv_zso->zso_cdev);
		if (error != 0) {
			mutex_exit(&zvol_minor_lock);
			zvol_os_free(zv);
			return (error);
		}
		rw_enter(&zvol_state_lock, RW_WRITER);
		zvol_insert(zv);
		rw_exit(&zvol_state_lock);
		mutex_exit(&zvol_minor_lock);
	}
	/*
	 * full/geom: look for a GPT.  The minor is hashed and
	 * these locks are not held.  The objset was disowned
	 * above, so discovery may open this device.  No-op when
	 * dkwedge_autodiscover is 0.  Snapshot minors are created
	 * by the common layer; do not walk them here.  Not the
	 * O_EXCL re-entry: leave zso_discover_lwp NULL so a
	 * concurrent exclusive open is still EBUSY.
	 */
	if (zv->zv_volmode == ZFS_VOLMODE_GEOM)
		dkwedge_discover(&zv->zv_zso->zso_dk);
	return (0);

out_free:
	zvol_nodes_remove(name);
	zvol_os_free(zv);
out_disown:
	dmu_objset_disown(os, B_TRUE, FTAG);
	return (error);
}

int
zvol_os_rename_minor(zvol_state_t *zv, const char *newname)
{
	struct zvol_state_os *zso = zv->zv_zso;
	int error;

	ASSERT(RW_WRITE_HELD(&zvol_state_lock));
	ASSERT(MUTEX_HELD(&zv->zv_state_lock));

	if (zso != NULL) {
		error = zvol_nodes_create(newname, zso->zso_bdev,
		    zso->zso_cdev);
		if (error != 0)
			return (error);
		zvol_nodes_remove(zv->zv_name);
		/* Keep iostat name zvolN.  A dataset path has '/'. */
	}
	zv->zv_hash = zvol_name_hash(newname);
	hlist_del(&zv->zv_hlink);
	hlist_add_head(&zv->zv_hlink, ZVOL_HT_HEAD(zv->zv_hash));
	(void) strlcpy(zv->zv_name, newname, sizeof (zv->zv_name));
	dataset_kstats_rename(&zv->zv_kstat, newname);
	return (0);
}

void
zvol_os_free(zvol_state_t *zv)
{
	struct zvol_state_os *zso;

	if (zv == NULL)
		return;
	zso = zv->zv_zso;
	/*
	 * zvol_minors is the live count, same as FreeBSD.  Bumped in
	 * zvol_minor_claim, dropped here.  A create that never claimed
	 * a minor (zso_minor == 0) must not decrement.
	 *
	 * Must run with zv_state_lock dropped (upstream
	 * ASSERT(!MUTEX_HELD)).  Last-close may still hold a
	 * devhold and will take suspend/state after remove_minor
	 * returns.  Wait here so we do not free under the closer.
	 */
	ASSERT(!MUTEX_HELD(&zv->zv_state_lock));
	ASSERT(!RW_LOCK_HELD(&zv->zv_suspend_lock));
	if (zso != NULL) {
		mutex_enter(&zvol_minor_lock);
		while (zso->zso_devholds != 0)
			cv_wait(&zvol_minor_cv, &zvol_minor_lock);
		if (zso->zso_minor > 0) {
			if (zvol_minors > 0)
				zvol_minors--;
			zso->zso_minor = 0;
		}
		mutex_exit(&zvol_minor_lock);
	}
	/*
	 * zil_close may commit and call zvol_get_data, which uses
	 * the rangelock.  Close before destroying locks.  Linux
	 * ASSERT0P(zv_zilog) here; this is the create-error safety
	 * net (create-minor already closed on the success path).
	 */
	if (zv->zv_zilog != NULL) {
		zil_close(zv->zv_zilog);
		zv->zv_zilog = NULL;
	}
	rw_destroy(&zv->zv_suspend_lock);
	zfs_rangelock_fini(&zv->zv_rangelock);
	mutex_destroy(&zv->zv_state_lock);
	cv_destroy(&zv->zv_removing_cv);
	dataset_kstats_destroy(&zv->zv_kstat);
	if (zso != NULL) {
		if (zso->zso_dk_attached) {
			/*
			 * remove_minors has waited for zv_open_count
			 * == 0.  A mounted wedge holds this parent
			 * open, so these wedges are idle.
			 * disk_detach does not remove them.
			 */
			dkwedge_delall(&zso->zso_dk);
			disk_detach(&zso->zso_dk);
			disk_destroy(&zso->zso_dk);
			zso->zso_dk_attached = B_FALSE;
		}
		mutex_destroy(&zso->zso_dklock);
		kmem_free(zso, sizeof (*zso));
	}
	kmem_free(zv, sizeof (*zv));
}

void
zvol_os_remove_minor(zvol_state_t *zv)
{

	if (zv->zv_zso == NULL)
		return;
	zv->zv_zso->zso_dying = B_TRUE;
	zvol_nodes_remove(zv->zv_name);
	/*
	 * Caller holds zv_state_lock.  Wait for in-flight I/O only
	 * (zso_inflight).  Last-close's zso_devholds is not I/O  --
	 * waiting for it here deadlocks: closer holds a devhold and
	 * wants state_lock, we hold state_lock and want the pin.
	 * zvol_os_free waits for the closer after we drop state_lock.
	 */
	mutex_enter(&zvol_minor_lock);
	while (zv->zv_zso->zso_inflight != 0)
		cv_wait(&zvol_minor_cv, &zvol_minor_lock);
	/*
	 * Stay hashed until the common layer zvol_remove after
	 * wait_close.  zso_dying already rejects new open/I/O.
	 * Close looks up by minor on the hash.
	 */
	mutex_exit(&zvol_minor_lock);
}

int
zvol_os_update_volsize(zvol_state_t *zv, uint64_t volsize)
{
	if (zv == NULL)
		return (SET_ERROR(EINVAL));
	zv->zv_volsize = volsize;
	zvol_disk_set_info(zv, volsize);
	return (0);
}

void
zvol_os_set_disk_ro(zvol_state_t *zv, int flags)
{
	(void) zv;
	(void) flags;
}

void
zvol_os_set_capacity(zvol_state_t *zv, uint64_t capacity)
{
	(void) zv;
	(void) capacity;
}

int
zvol_busy(void)
{
	return (zvol_minors != 0);
}

void
zvol_wait_close(zvol_state_t *zv)
{
	/*
	 * Linux is a no-op.  FreeBSD waits, then the shared
	 * zvol_remove_minors_impl waits again until zv_open_count
	 * is 0 before zvol_os_free.  Do not set zso_dying here:
	 * volmode calls us and then remove_minor; a sticky dying
	 * bit would ENXIO every later open if remove did not run.
	 * zvol_os_remove_minor sets dying when the minor is
	 * actually going away.
	 */
	(void) zv;
}

static int
zvol_cdev_open(dev_t dev, int flag, int mode, struct lwp *l)
{
	zvol_state_t *zv;
	int error = 0;
	boolean_t drop_suspend = B_FALSE;
	boolean_t drop_namespace = B_FALSE;
	boolean_t first;
	boolean_t did_first_open;

#if !ZVOL_SWAP_OK
	/*
	 * Primary prohibition: refuse swapctl(2) before the
	 * device is marked SWF_ENABLE.  Regular open/newfs/dd
	 * still work.
	 */
	if (zvol_lwp_is_swapctl(l)) {
		printf("ZFS: swap on zvol requires PMAP_DIRECT_MAP\n");
		return (EOPNOTSUPP);
	}
#else
	(void) l;
#endif

retry:
	error = 0;
	drop_suspend = B_FALSE;
	drop_namespace = B_FALSE;
	did_first_open = B_FALSE;
	zv = zvol_hold_dev(dev);
	if (zv == NULL)
		return (ENXIO);

	mutex_enter(&zv->zv_state_lock);
	if (zv->zv_zso != NULL && zv->zv_zso->zso_dying) {
		mutex_exit(&zv->zv_state_lock);
		zvol_rele_dev(zv);
		return (ENXIO);
	}
	if (zv->zv_flags & ZVOL_EXCL) {
		/*
		 * gpt(8) holds the raw node O_EXCL across
		 * DIOCMWEDGES.  The scan's read-only re-open is
		 * allowed only while that ioctl is running on
		 * this lwp.  A later open from the same lwp, or
		 * any open from another, is EBUSY.
		 */
		if ((flag & (FWRITE | O_EXCL)) != 0 ||
		    zv->zv_zso == NULL ||
		    zv->zv_zso->zso_discover_lwp != curlwp) {
			mutex_exit(&zv->zv_state_lock);
			zvol_rele_dev(zv);
			return (EBUSY);
		}
	}
	first = (zvol_opensum(zv) == 0);
	if (first) {
		drop_suspend = B_TRUE;
		if (!rw_tryenter(&zv->zv_suspend_lock, RW_READER)) {
			mutex_exit(&zv->zv_state_lock);
			rw_enter(&zv->zv_suspend_lock, RW_READER);
			mutex_enter(&zv->zv_state_lock);
		}
		if (zv->zv_objset == NULL) {
			/*
			 * zvol_first_open ASSERTs spa_namespace_held().
			 * Take it here, same as Linux/FreeBSD, with
			 * tryenter so a zvol-as-vdev open that already
			 * holds the lock does not invert against the
			 * spec vnode (held by spec_open across d_open).
			 */
			if (!spa_namespace_held()) {
				if (!spa_namespace_tryenter(FTAG)) {
					if (drop_suspend)
						rw_exit(&zv->zv_suspend_lock);
					mutex_exit(&zv->zv_state_lock);
					zvol_rele_dev(zv);
					(void) kpause("zvolns", false, 1,
					    NULL);
					goto retry;
				}
				drop_namespace = B_TRUE;
			}
			error = zvol_first_open(zv, (flag & FWRITE) == 0);
			if (drop_namespace)
				spa_namespace_exit(FTAG);
			if (error != 0) {
				if (drop_suspend)
					rw_exit(&zv->zv_suspend_lock);
				mutex_exit(&zv->zv_state_lock);
				zvol_rele_dev(zv);
				return (error);
			}
			did_first_open = B_TRUE;
		}
		zvol_disk_set_info(zv, zv->zv_volsize);
	}
	if ((flag & FWRITE) && (zv->zv_flags & ZVOL_RDONLY)) {
		if (did_first_open)
			zvol_last_close(zv);
		if (drop_suspend)
			rw_exit(&zv->zv_suspend_lock);
		mutex_exit(&zv->zv_state_lock);
		zvol_rele_dev(zv);
		return (EROFS);
	}
	if ((flag & O_EXCL) && zvol_opensum(zv) != 0) {
		if (did_first_open)
			zvol_last_close(zv);
		if (drop_suspend)
			rw_exit(&zv->zv_suspend_lock);
		mutex_exit(&zv->zv_state_lock);
		zvol_rele_dev(zv);
		return (EBUSY);
	}
	if (flag & O_EXCL)
		zv->zv_flags |= ZVOL_EXCL;
	if (zv->zv_zso != NULL) {
		if (zvol_isblk(mode))
			zv->zv_zso->zso_bopens++;
		else
			zv->zv_zso->zso_copens++;
		zv->zv_open_count = zvol_opensum(zv);
	} else {
		zv->zv_open_count++;
	}
	if (drop_suspend)
		rw_exit(&zv->zv_suspend_lock);
	mutex_exit(&zv->zv_state_lock);
	zvol_rele_dev(zv);
	return (0);
}

static int
zvol_cdev_close(dev_t dev, int flag, int mode, struct lwp *l)
{
	zvol_state_t *zv;

	(void) flag;
	(void) l;
	/*
	 * Pin under zvol_minor_lock so remove_minor cannot free zv
	 * between lookup and the end of last-close.  Allow dying:
	 * wait_close sets zso_dying before waiting for opensum==0,
	 * and this close is what must drop the count.
	 */
	zv = zvol_hold_dev_impl(dev, B_TRUE);
	if (zv == NULL)
		return (ENXIO);
	mutex_enter(&zv->zv_state_lock);
	if (zvol_opensum(zv) == 0) {
		mutex_exit(&zv->zv_state_lock);
		zvol_rele_dev(zv);
		return (ENXIO);
	}
	/*
	 * d_close is last-close of THIS spec vnode.  Block and raw
	 * each have their own vnode; only drop the objset when both
	 * are gone.
	 */
	if (zv->zv_zso != NULL) {
		if (zvol_isblk(mode))
			zv->zv_zso->zso_bopens = 0;
		else
			zv->zv_zso->zso_copens = 0;
		zv->zv_open_count = zvol_opensum(zv);
	} else {
		zv->zv_open_count = 0;
	}
	if (zv->zv_open_count != 0 || zv->zv_objset == NULL) {
		if (zv->zv_open_count == 0)
			cv_broadcast(&zv->zv_removing_cv);
		mutex_exit(&zv->zv_state_lock);
		zvol_rele_dev(zv);
		return (0);
	}
	/*
	 * Last close of both specs.  Readers do not exclude each
	 * other, so last_close would race strategy/read/write if
	 * those were still in flight.  hold_io is taken before
	 * suspend, so WRITER alone is not enough either: set
	 * draining so new I/O fails, wait until I/O pins are gone,
	 * then take READER and disown.  zvol_last_close ASSERTs
	 * RW_READ_HELD; on NetBSD rw_read_held() is false for a
	 * writer (unlike rw_lock_held), so WRITER panics on
	 * ZFS_DEBUG / DIAGNOSTIC.  first_open error paths already
	 * last-close under READER.
	 * The closer's own pin is a devhold, not inflight, so we
	 * wait for inflight == 0 (not > 1).
	 *
	 * Dual last-close (block + raw) can both see opensum==0.
	 * Only one closer drains; the other sees draining and leaves.
	 */
	if (zv->zv_zso != NULL) {
		if (zv->zv_zso->zso_draining) {
			mutex_exit(&zv->zv_state_lock);
			zvol_rele_dev(zv);
			return (0);
		}
		zv->zv_zso->zso_draining = B_TRUE;
	}
	mutex_exit(&zv->zv_state_lock);

	mutex_enter(&zvol_minor_lock);
	while (zv->zv_zso != NULL && zv->zv_zso->zso_inflight != 0)
		cv_wait(&zvol_minor_cv, &zvol_minor_lock);
	mutex_exit(&zvol_minor_lock);

	rw_enter(&zv->zv_suspend_lock, RW_READER);
	mutex_enter(&zv->zv_state_lock);
	if (zv->zv_objset != NULL && zvol_opensum(zv) == 0) {
		zv->zv_flags &= ~ZVOL_EXCL;
		zvol_last_close(zv);
		cv_broadcast(&zv->zv_removing_cv);
	}
	if (zv->zv_zso != NULL)
		zv->zv_zso->zso_draining = B_FALSE;
	mutex_exit(&zv->zv_state_lock);
	rw_exit(&zv->zv_suspend_lock);
	zvol_rele_dev(zv);
	return (0);
}

static int
zvol_cdev_read(dev_t dev, struct uio *uio_s, int ioflag)
{
	zvol_state_t *zv;
	zfs_uio_t uio;
	zfs_locked_range_t *lr;
	uint64_t volsize;
	int error = 0;

	(void) ioflag;
	zv = zvol_hold_io(dev);
	if (zv == NULL)
		return (ENXIO);
	zfs_uio_init(&uio, uio_s);
	volsize = zv->zv_volsize;
	if (zfs_uio_resid(&uio) > 0 &&
	    (zfs_uio_offset(&uio) < 0 ||
	    zfs_uio_offset(&uio) > (off_t)volsize)) {
		zvol_rele_io(zv);
		return (EIO);
	}

	rw_enter(&zv->zv_suspend_lock, RW_READER);
	lr = zfs_rangelock_enter(&zv->zv_rangelock, zfs_uio_offset(&uio),
	    zfs_uio_resid(&uio), RL_READER);
	while (zfs_uio_resid(&uio) > 0 &&
	    zfs_uio_offset(&uio) < (off_t)volsize) {
		uint64_t bytes = MIN(zfs_uio_resid(&uio), DMU_MAX_ACCESS >> 1);
		if (bytes > volsize - zfs_uio_offset(&uio))
			bytes = volsize - zfs_uio_offset(&uio);
		error = dmu_read_uio_dnode(zv->zv_dn, &uio, bytes,
		    DMU_READ_PREFETCH);
		if (error) {
			if (error == ECKSUM)
				error = EIO;
			break;
		}
	}
	zfs_rangelock_exit(lr);
	rw_exit(&zv->zv_suspend_lock);

	zvol_rele_io(zv);
	return (error);
}

static int
zvol_cdev_write(dev_t dev, struct uio *uio_s, int ioflag)
{
	zvol_state_t *zv;
	zfs_uio_t uio;
	zfs_locked_range_t *lr;
	uint64_t volsize;
	int error = 0;
	boolean_t commit;

	zv = zvol_hold_io(dev);
	if (zv == NULL)
		return (ENXIO);
	if (zv->zv_flags & ZVOL_RDONLY) {
		zvol_rele_io(zv);
		return (EROFS);
	}
	zfs_uio_init(&uio, uio_s);
	volsize = zv->zv_volsize;
	if (zfs_uio_resid(&uio) > 0 &&
	    (zfs_uio_offset(&uio) < 0 ||
	    zfs_uio_offset(&uio) > (off_t)volsize)) {
		zvol_rele_io(zv);
		return (EIO);
	}
	commit = (ioflag & IO_SYNC) ||
	    (zv->zv_objset->os_sync == ZFS_SYNC_ALWAYS);

	rw_enter(&zv->zv_suspend_lock, RW_READER);
	if ((error = zvol_ensure_zilog(zv)) != 0) {
		rw_exit(&zv->zv_suspend_lock);
		zvol_rele_io(zv);
		return (error);
	}
	lr = zfs_rangelock_enter(&zv->zv_rangelock, zfs_uio_offset(&uio),
	    zfs_uio_resid(&uio), RL_WRITER);
	while (zfs_uio_resid(&uio) > 0 &&
	    zfs_uio_offset(&uio) < (off_t)volsize) {
		uint64_t bytes = MIN(zfs_uio_resid(&uio), DMU_MAX_ACCESS >> 1);
		uint64_t off = zfs_uio_offset(&uio);
		dmu_tx_t *tx = dmu_tx_create(zv->zv_objset);

		if (bytes > volsize - off)
			bytes = volsize - off;
		dmu_tx_hold_write_by_dnode(tx, zv->zv_dn, off, bytes);
		error = dmu_tx_assign(tx, DMU_TX_WAIT);
		if (error) {
			dmu_tx_abort(tx);
			break;
		}
		error = dmu_write_uio_dnode(zv->zv_dn, &uio, bytes, tx,
		    DMU_READ_PREFETCH);
		if (error == 0)
			zvol_log_write(zv, tx, off, bytes, commit);
		dmu_tx_commit(tx);
		if (error)
			break;
	}
	zfs_rangelock_exit(lr);
	if (error == 0 && commit)
		error = zil_commit(zv->zv_zilog, ZVOL_OBJ);
	rw_exit(&zv->zv_suspend_lock);

	zvol_rele_io(zv);
	return (error);
}

static int
zvol_unmap(zvol_state_t *zv, uint64_t offset, uint64_t length)
{
	zfs_locked_range_t *lr;
	dmu_tx_t *tx;
	uint64_t start, end, size;
	int error;
	boolean_t sync;

	if (!zvol_unmap_enabled)
		return (0);
	if (zv->zv_flags & ZVOL_RDONLY)
		return (SET_ERROR(EROFS));
	if ((offset % DEV_BSIZE) != 0 || (length % DEV_BSIZE) != 0 ||
	    offset >= zv->zv_volsize || length == 0)
		return (SET_ERROR(EINVAL));
	if (offset + length > zv->zv_volsize)
		length = zv->zv_volsize - offset;

	/*
	 * Align to the volume block size.  Unaligned dmu_free_long_range
	 * does a slow RMW of the edge blocks, and a 2G DIOCDISCARD from
	 * newfs would hold the rangelock across that whole object.
	 */
	start = P2ROUNDUP(offset, zv->zv_volblocksize);
	end = P2ALIGN_TYPED(offset + length, zv->zv_volblocksize, uint64_t);
	if (start >= end)
		return (0);
	size = end - start;

	rw_enter(&zv->zv_suspend_lock, RW_READER);
	error = zvol_ensure_zilog(zv);
	if (error != 0) {
		rw_exit(&zv->zv_suspend_lock);
		return (error);
	}
	lr = zfs_rangelock_enter(&zv->zv_rangelock, start, size, RL_WRITER);
	tx = dmu_tx_create(zv->zv_objset);
	dmu_tx_mark_netfree(tx);
	error = dmu_tx_assign(tx, DMU_TX_WAIT);
	if (error != 0) {
		dmu_tx_abort(tx);
		zfs_rangelock_exit(lr);
		rw_exit(&zv->zv_suspend_lock);
		return (error);
	}
	sync = (zv->zv_objset->os_sync == ZFS_SYNC_ALWAYS);
	zvol_log_truncate(zv, tx, start, size);
	dmu_tx_commit(tx);
	error = dmu_free_long_range(zv->zv_objset, ZVOL_OBJ, start, size);
	zfs_rangelock_exit(lr);
	if (error == 0 && sync)
		error = zil_commit(zv->zv_zilog, ZVOL_OBJ);
	rw_exit(&zv->zv_suspend_lock);
	return (error);
}

static int
zvol_cdev_discard(dev_t dev, off_t offset, off_t len)
{
	zvol_state_t *zv = zvol_hold_io(dev);
	int error;

	if (zv == NULL)
		return (ENXIO);
	if (offset < 0 || len <= 0) {
		zvol_rele_io(zv);
		return (EINVAL);
	}
	error = zvol_unmap(zv, (uint64_t)offset, (uint64_t)len);
	zvol_rele_io(zv);
	return (error);
}

/*
 * disk_ioctl() returns EBUSY when dk_label is NULL, and newfs retries
 * DIOCGDINFO on that.  Fill a synthetic label so the ioctl succeeds.
 */
static void
zvol_fill_disklabel(zvol_state_t *zv, struct disklabel *lp)
{
	struct disk_geom *dg;
	uint32_t secsize;
	uint64_t nsec;
	int p;

	memset(lp, 0, sizeof (*lp));
	dg = &zv->zv_zso->zso_dk.dk_geom;
	secsize = dg->dg_secsize != 0 ? dg->dg_secsize : DEV_BSIZE;
	nsec = dg->dg_secperunit;
	if (nsec == 0 && secsize != 0)
		nsec = zv->zv_volsize / secsize;
	lp->d_magic = DISKMAGIC;
	lp->d_magic2 = DISKMAGIC;
	lp->d_type = DKTYPE_SCSI;
	lp->d_secsize = secsize;
	lp->d_nsectors = 63;
	lp->d_ntracks = 16;
	lp->d_secpercyl = lp->d_nsectors * lp->d_ntracks;
	lp->d_secperunit = nsec;
	if (lp->d_secpercyl != 0)
		lp->d_ncylinders = nsec / lp->d_secpercyl;
	(void) strncpy(lp->d_typename, "ZVOL", sizeof (lp->d_typename));
	(void) strncpy(lp->d_packname, "zvol", sizeof (lp->d_packname));
	lp->d_npartitions = MAXPARTITIONS;
	for (p = 0; p < MAXPARTITIONS; p++) {
		lp->d_partitions[p].p_offset = 0;
		lp->d_partitions[p].p_size = 0;
		lp->d_partitions[p].p_fstype = FS_UNUSED;
	}
	/* 'a': whole volume, FFS so newfs without -I accepts it. */
	lp->d_partitions[0].p_size = nsec;
	lp->d_partitions[0].p_fstype = FS_BSDFFS;
	lp->d_partitions[RAW_PART].p_size = nsec;
	lp->d_partitions[RAW_PART].p_fstype = FS_UNUSED;
	lp->d_checksum = dkcksum(lp);
}

static int
zvol_cdev_ioctl(dev_t dev, unsigned long cmd, void *data, int flag,
    struct lwp *l)
{
	zvol_state_t *zv;
	int error;

	zv = zvol_hold_io(dev);
	if (zv == NULL)
		return (ENXIO);
	if (zv->zv_zso == NULL || !zv->zv_zso->zso_dk_attached) {
		zvol_rele_io(zv);
		return (ENXIO);
	}

	switch (cmd) {
	case DIOCGDINFO:
	case DIOCGDEFLABEL:
		zvol_fill_disklabel(zv, data);
		zvol_rele_io(zv);
		return (0);

	case DIOCCACHESYNC:
		rw_enter(&zv->zv_suspend_lock, RW_READER);
		if (zv->zv_zilog != NULL)
			error = zil_commit(zv->zv_zilog, ZVOL_OBJ);
		else
			error = 0;
		rw_exit(&zv->zv_suspend_lock);
		zvol_rele_io(zv);
		return (error);

	case DIOCAWEDGE:
		/*
		 * dev mode is a raw node.  full/geom must pass the
		 * real dev_t: disk_ioctl returns EPASSTHROUGH for
		 * these commands when dev is NODEV.  Adding a
		 * wedge does not scan, so it does not re-open us.
		 */
		if (zv->zv_volmode != ZFS_VOLMODE_GEOM) {
			zvol_rele_io(zv);
			return (ENOTTY);
		}
		break;

	case DIOCMWEDGES: {
		struct zvol_state_os *zso = zv->zv_zso;

		if (zv->zv_volmode != ZFS_VOLMODE_GEOM) {
			zvol_rele_io(zv);
			return (ENOTTY);
		}
		/*
		 * Mark this lwp only across the discover call.
		 * disk_ioctl() reaches dkwedge_discover() for
		 * this command alone.  Do not steal a marker
		 * another thread already set: this scan then
		 * gets EBUSY.  Clear only our own.
		 */
		mutex_enter(&zv->zv_state_lock);
		if (zso->zso_discover_lwp == NULL)
			zso->zso_discover_lwp = curlwp;
		mutex_exit(&zv->zv_state_lock);
		error = disk_ioctl(&zso->zso_dk, dev, cmd, data, flag, l);
		if (error == EPASSTHROUGH)
			error = ENOTTY;
		mutex_enter(&zv->zv_state_lock);
		if (zso->zso_discover_lwp == curlwp)
			zso->zso_discover_lwp = NULL;
		mutex_exit(&zv->zv_state_lock);
		zvol_rele_io(zv);
		return (error);
	}

	case DIOCGPARTINFO: {
		struct partinfo *pi = data;
		struct disk_geom *dg = &zv->zv_zso->zso_dk.dk_geom;

		memset(pi, 0, sizeof (*pi));
		pi->pi_offset = 0;
		pi->pi_secsize = dg->dg_secsize != 0 ?
		    dg->dg_secsize : DEV_BSIZE;
		pi->pi_size = dg->dg_secperunit;
		if (pi->pi_size == 0 && pi->pi_secsize != 0)
			pi->pi_size = zv->zv_volsize / pi->pi_secsize;
		pi->pi_fstype = FS_BSDFFS;
		pi->pi_bsize = MAX(BLKDEV_IOSIZE, pi->pi_secsize);
		zvol_rele_io(zv);
		return (0);
	}

	default:
		break;
	}

	error = disk_ioctl(&zv->zv_zso->zso_dk, dev, cmd, data, flag, l);
	if (error == EPASSTHROUGH)
		error = ENOTTY;
	zvol_rele_io(zv);
	return (error);
}

static int
zvol_psize(dev_t dev)
{
	zvol_state_t *zv = zvol_hold_dev(dev);
	uint64_t nsec;

	if (zv == NULL)
		return (-1);
#if !ZVOL_SWAP_OK
	/*
	 * Second activation gate.  swap_on() treats d_psize == -1
	 * as ENXIO and never sets SWF_ENABLE.
	 */
	if (zvol_lwp_is_swapctl(curlwp)) {
		zvol_rele_dev(zv);
		return (-1);
	}
#endif
	/*
	 * NetBSD d_psize is always in 512-byte (DEV_BSIZE) units
	 * (vndsize, wdsize, …).  Strategy already does
	 * b_blkno * DEV_BSIZE.  Ioctls keep native zso_secsize.
	 *
	 * d_psize returns int, so a zvol larger than INT_MAX
	 * sectors (1 TB) cannot be a swap device: swap_on()
	 * treats -1 as ENXIO.  Same ceiling as wd(4)/ld(4).
	 * DIOCGMEDIASIZE still reports the full size.
	 */
	nsec = zv->zv_volsize / DEV_BSIZE;
	zvol_rele_dev(zv);
	if (nsec > (uint64_t)INT_MAX)
		return (-1);
	return ((int)nsec);
}

static void
zvol_strategy_body(struct buf *bp)
{
	zvol_state_t *zv = zvol_hold_io(bp->b_dev);
	zfs_locked_range_t *lr;
	uint64_t off, resid, done = 0;
	int error = 0;
	boolean_t reading;
	boolean_t commit = B_FALSE;

	if (zv == NULL) {
		bp->b_error = ENXIO;
		bp->b_resid = bp->b_bcount;
		biodone(bp);
		return;
	}

	if (bp->b_blkno < 0 ||
	    (uint64_t)bp->b_blkno > UINT64_MAX / DEV_BSIZE) {
		bp->b_error = EIO;
		bp->b_resid = bp->b_bcount;
		zvol_rele_io(zv);
		biodone(bp);
		return;
	}
	off = (uint64_t)bp->b_blkno * DEV_BSIZE;
	resid = bp->b_bcount;
	reading = (bp->b_flags & B_READ) != 0;

	if (!reading && (zv->zv_flags & ZVOL_RDONLY)) {
		bp->b_error = EROFS;
		bp->b_resid = bp->b_bcount;
		zvol_rele_io(zv);
		biodone(bp);
		return;
	}

	if (off > zv->zv_volsize ||
	    (!reading && off == zv->zv_volsize)) {
		bp->b_error = EIO;
		bp->b_resid = bp->b_bcount;
		zvol_rele_io(zv);
		biodone(bp);
		return;
	}
	if (off == zv->zv_volsize) {
		bp->b_resid = bp->b_bcount;
		zvol_rele_io(zv);
		biodone(bp);
		return;
	}
	if (resid > zv->zv_volsize - off)
		resid = zv->zv_volsize - off;

	rw_enter(&zv->zv_suspend_lock, RW_READER);
	if (!reading) {
		error = zvol_ensure_zilog(zv);
		if (error != 0) {
			rw_exit(&zv->zv_suspend_lock);
			bp->b_error = error;
			bp->b_resid = bp->b_bcount;
			zvol_rele_io(zv);
			biodone(bp);
			return;
		}
	}
	if (zv->zv_zso != NULL && zv->zv_zso->zso_dk_attached) {
		mutex_enter(&zv->zv_zso->zso_dklock);
		disk_busy(&zv->zv_zso->zso_dk);
		mutex_exit(&zv->zv_zso->zso_dklock);
	}
	lr = zfs_rangelock_enter(&zv->zv_rangelock, off, resid,
	    reading ? RL_READER : RL_WRITER);
	while (done < resid) {
		uint64_t bytes = MIN(resid - done, DMU_MAX_ACCESS >> 1);
		char *addr = (char *)bp->b_data + done;

		if (reading) {
			error = dmu_read(zv->zv_objset, ZVOL_OBJ, off + done,
			    bytes, addr, DMU_READ_PREFETCH);
			if (error == ECKSUM)
				error = EIO;
		} else {
			dmu_tx_t *tx = dmu_tx_create(zv->zv_objset);

			dmu_tx_hold_write_by_dnode(tx, zv->zv_dn,
			    off + done, bytes);
			error = dmu_tx_assign(tx, DMU_TX_WAIT);
			if (error != 0) {
				dmu_tx_abort(tx);
				break;
			}
			commit = ((bp->b_flags & B_ASYNC) == 0) ||
			    ((bp->b_flags & B_MEDIA_FUA) != 0) ||
			    (zv->zv_objset->os_sync == ZFS_SYNC_ALWAYS);
			error = dmu_write_by_dnode(zv->zv_dn, off + done, bytes,
			    addr, tx, 0);
			if (error == 0)
				zvol_log_write(zv, tx, off + done, bytes,
				    commit);
			dmu_tx_commit(tx);
		}
		if (error != 0)
			break;
		done += bytes;
	}
	zfs_rangelock_exit(lr);
	if (error == 0 && commit)
		error = zil_commit(zv->zv_zilog, ZVOL_OBJ);
	rw_exit(&zv->zv_suspend_lock);

	bp->b_error = error;
	bp->b_resid = bp->b_bcount - done;
	if (zv->zv_zso != NULL && zv->zv_zso->zso_dk_attached) {
		mutex_enter(&zv->zv_zso->zso_dklock);
		disk_unbusy(&zv->zv_zso->zso_dk, (long)done, reading);
		mutex_exit(&zv->zv_zso->zso_dklock);
	}
	zvol_rele_io(zv);
	biodone(bp);
}

static void
zvol_minphys(struct buf *bp)
{
	if (bp->b_bcount > zvol_maxphys)
		bp->b_bcount = zvol_maxphys;
}

static void
zvol_strategy_wk(struct work *wk, void *arg)
{
	struct buf *bp = (struct buf *)wk;

	(void) arg;
	zvol_strategy_body(bp);
	mutex_enter(&zvol_strat_lock);
	ASSERT(zvol_strat_busy > 0);
	zvol_strat_busy--;
	cv_broadcast(&zvol_strat_cv);
	mutex_exit(&zvol_strat_lock);
}

static void
zvol_strategy(struct buf *bp)
{
	struct workqueue *wq;

	/*
	 * d_strategy must not sleep.  Always hop to the IPL_VM
	 * workqueue: threads that splbio()'d still take this path,
	 * and the pagedaemon / biodone already require it.  Per-buf
	 * b_work so a producer cannot re-enqueue the same struct
	 * work while the callback is still running (workqueue(9)).
	 */
	mutex_enter(&zvol_strat_lock);
	wq = zvol_strat_wq;
	if (wq == NULL) {
		mutex_exit(&zvol_strat_lock);
		bp->b_error = ENXIO;
		bp->b_resid = bp->b_bcount;
		biodone(bp);
		return;
	}
	zvol_strat_busy++;
	workqueue_enqueue(wq, &bp->b_work, NULL);
	mutex_exit(&zvol_strat_lock);
}

int
zvol_init(void)
{
	int error;

	/*
	 * zvol_volmode stays at the common default, GEOM (full).
	 * Forcing DEV hid partitions unless volmode was set.
	 */

	mutex_init(&zvol_minor_lock, NULL, MUTEX_DEFAULT, NULL);
	cv_init(&zvol_minor_cv, NULL, CV_DEFAULT, NULL);
	mutex_init(&zvol_strat_lock, NULL, MUTEX_DEFAULT, NULL);
	cv_init(&zvol_strat_cv, NULL, CV_DEFAULT, NULL);
	error = workqueue_create(&zvol_strat_wq, "z_zvol_strat",
	    zvol_strategy_wk, NULL, PRI_BIO, IPL_VM, WQ_MPSAFE | WQ_PERCPU);
	if (error != 0) {
		cv_destroy(&zvol_strat_cv);
		cv_destroy(&zvol_minor_cv);
		mutex_destroy(&zvol_minor_lock);
		mutex_destroy(&zvol_strat_lock);
		return (error);
	}
	if (!zvol_devsw_attached) {
		error = devsw_attach("zvol", &zvol_bdevsw, &zvol_bmajor,
		    &zvol_cdevsw, &zvol_cmajor);
		if (error != 0 && error != EEXIST) {
			workqueue_destroy(zvol_strat_wq);
			zvol_strat_wq = NULL;
			cv_destroy(&zvol_strat_cv);
			cv_destroy(&zvol_minor_cv);
			mutex_destroy(&zvol_minor_lock);
			mutex_destroy(&zvol_strat_lock);
			return (error);
		}
		zvol_devsw_attached = 1;
		zvol_major = zvol_cmajor;
	}
	(void) do_sys_mkdir(ZVOL_ROOT, 0755, UIO_SYSSPACE);
	(void) do_sys_mkdir(ZVOL_RROOT, 0755, UIO_SYSSPACE);
	return (zvol_init_impl());
}

void
zvol_fini(void)
{
	struct workqueue *wq;

	zvol_fini_impl();
	mutex_enter(&zvol_strat_lock);
	wq = zvol_strat_wq;
	zvol_strat_wq = NULL;
	while (zvol_strat_busy != 0)
		cv_wait(&zvol_strat_cv, &zvol_strat_lock);
	mutex_exit(&zvol_strat_lock);
	if (wq != NULL)
		workqueue_destroy(wq);
	if (zvol_devsw_attached) {
		devsw_detach(&zvol_bdevsw, &zvol_cdevsw);
		zvol_devsw_attached = 0;
	}
	cv_destroy(&zvol_strat_cv);
	mutex_destroy(&zvol_strat_lock);
	mutex_destroy(&zvol_minor_lock);
	cv_destroy(&zvol_minor_cv);
}
