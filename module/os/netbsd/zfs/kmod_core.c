// SPDX-License-Identifier: BSD-2-Clause
/*
 * OpenZFS kernel module entry for NetBSD.
 *
 * Replaces FreeBSD DECLARE_MODULE / cdev / EVENTHANDLER with
 * NetBSD MODULE(9) and cdevsw.
 */

#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/conf.h>
#include <sys/buf.h>
#include <sys/file.h>
#include <sys/filedesc.h>
#include <sys/ioctl.h>
#include <sys/kmem.h>
#include <sys/mutex.h>
#include <sys/vfs_syscalls.h>
#include <sys/stat.h>
#include <sys/namei.h>
#include <sys/proc.h>
#include <sys/event.h>
#include <sys/zfs_ioctl.h>
#include <sys/zfs_vfsops.h>
#include <sys/zfs_onexit.h>
#include <sys/fm/util.h>
#include <sys/fm/protocol.h>
#include <sys/zfs_ioctl_impl.h>
#include <sys/zfs_ioctl_compat.h>
#include <sys/fs/zfs.h>
#include <sys/spa.h>
#include <sys/zvol.h>
#include <sys/mount.h>
#include <sys/sysctl.h>
#include <sys/kauth.h>
#include <sys/lwp.h>
#include <sys/queue.h>
#include <sys/vnode.h>
#include <miscfs/specfs/specdev.h>

/*
 * Compile-time ISA mapping must match FreeBSD Makefile.bsd:
 * HAVE_SIMD(ext) == 1 iff -DHAVE_TOOLCHAIN_##ext is on the command
 * line.  A parallel HAVE_SSE2=1 header is how SIMD dispatch went
 * missing the first time kfpu was wired.
 */
#if defined(__x86_64) || defined(__x86_64__) || defined(__amd64__)
#if !HAVE_SIMD(SSE2) || !HAVE_SIMD(SSSE3) || !HAVE_SIMD(SSE4_1) || \
	!HAVE_SIMD(AVX) || !HAVE_SIMD(AVX2) || !HAVE_SIMD(AVX512F) || \
	!HAVE_SIMD(AVX512VL) || !HAVE_SIMD(AVX512BW) || \
	!HAVE_SIMD(SHA512EXT)
#error "OpenZFS: HAVE_TOOLCHAIN_* ISA flags not mapped (Makefile.inc.netbsd)"
#endif
#endif

extern struct vfsops zfs_vfsops;
extern unsigned int zvol_volmode;
extern unsigned int zvol_inhibit_dev;

#ifdef ZFS_IOCVER_OZFS
static int zfs_version_ioctl = ZFS_IOCVER_OZFS;
#else
static int zfs_version_ioctl = 15;
#endif
#ifndef ZFS_META_VERSION
#define	ZFS_META_VERSION	"unknown"
#endif
#ifndef ZFS_META_RELEASE
#define	ZFS_META_RELEASE	"0"
#endif
static char zfs_version_module[] = ZFS_META_VERSION "-" ZFS_META_RELEASE;
static struct sysctllog *zfs_sysctl_log;
int zfs_ioctl_debug = 0;

/* NetBSD sys/conf/majors: device-major zfs char 190 block 190. */
static devmajor_t zfs_bmajor = 190, zfs_cmajor = 190;
static int zfsdev_opens;
static int zfsdev_unloading;

static int zfsdev_fo_read(struct file *, off_t *, struct uio *,
    kauth_cred_t, int);
static int zfsdev_fo_write(struct file *, off_t *, struct uio *,
    kauth_cred_t, int);
static int zfsdev_fo_ioctl(struct file *, ulong_t, void *);
static int zfsdev_fo_poll(struct file *, int);
static int zfsdev_fo_stat(struct file *, struct stat *);
static int zfsdev_fo_close(struct file *);
static int zfsdev_fo_kqfilter(struct file *, struct knote *);

static int
zfsdev_fo_read(struct file *fp, off_t *off, struct uio *uio,
    kauth_cred_t cred, int flags)
{
	(void) fp;
	(void) off;
	(void) uio;
	(void) cred;
	(void) flags;
	return (ENODEV);
}

static int
zfsdev_fo_write(struct file *fp, off_t *off, struct uio *uio,
    kauth_cred_t cred, int flags)
{
	(void) fp;
	(void) off;
	(void) uio;
	(void) cred;
	(void) flags;
	return (ENODEV);
}

static int
zfsdev_fo_poll(struct file *fp, int events)
{
	(void) fp;
	(void) events;
	return (0);
}

static int
zfsdev_fo_stat(struct file *fp, struct stat *sb)
{
	(void) fp;
	memset(sb, 0, sizeof (*sb));
	sb->st_mode = S_IFCHR | 0666;
	sb->st_nlink = 1;
	sb->st_rdev = makedev(zfs_cmajor, 0);
	return (0);
}

static int
zfsdev_fo_kqfilter(struct file *fp, struct knote *kn)
{
	(void) fp;
	(void) kn;
	return (EINVAL);
}

static int zfsdev_fo_close(struct file *);

/*
 * One file per open, via fd_clone().  f_data is the zfsdev_state_t.
 * Copying specfs fileops onto a private template was the stand-in
 * for this.  d_open returns EMOVEFD; vn_open installs the clone and
 * spec_open drops the vnode open.
 */
static const struct fileops zfsdev_fileops = {
	.fo_name = "zfs",
	.fo_read = zfsdev_fo_read,
	.fo_write = zfsdev_fo_write,
	.fo_ioctl = zfsdev_fo_ioctl,
	.fo_fcntl = fnullop_fcntl,
	.fo_poll = zfsdev_fo_poll,
	.fo_stat = zfsdev_fo_stat,
	.fo_close = zfsdev_fo_close,
	.fo_kqfilter = zfsdev_fo_kqfilter,
	.fo_restart = fnullop_restart,
};

static dev_type_open(zfsdev_open);
static dev_type_open(zfsdev_bopen);
static dev_type_close(zfsdev_close);
static dev_type_ioctl(zfsdev_ioctl);

static void
zfsdev_strategy(struct buf *bp)
{
	bp->b_error = ENODEV;
	bp->b_resid = bp->b_bcount;
	biodone(bp);
}

static struct bdevsw zfs_bdevsw = {
	.d_open = zfsdev_bopen,
	.d_close = zfsdev_close,
	.d_strategy = zfsdev_strategy,
	.d_ioctl = zfsdev_ioctl,
	.d_dump = nodump,
	.d_psize = nosize,
	.d_discard = nodiscard,
	.d_flag = D_OTHER | D_MPSAFE
};

static struct cdevsw zfs_cdevsw = {
	.d_open = zfsdev_open,
	.d_close = zfsdev_close,
	.d_read = noread,
	.d_write = nowrite,
	.d_ioctl = zfsdev_ioctl,
	.d_stop = nostop,
	.d_tty = notty,
	.d_poll = nopoll,
	.d_mmap = nommap,
	.d_kqfilter = nokqfilter,
	.d_discard = nodiscard,
	.d_flag = D_OTHER | D_MPSAFE
};

CTASSERT(sizeof (zfs_iocparm_t) == 24);
CTASSERT(offsetof(zfs_iocparm_t, zfs_ioctl_version) == 0);
CTASSERT(offsetof(zfs_iocparm_t, zfs_pad) == 4);
CTASSERT(offsetof(zfs_iocparm_t, zfs_cmd) == 8);
CTASSERT(offsetof(zfs_iocparm_t, zfs_cmd_size) == 16);
#ifdef ZFS_LEGACY_SUPPORT
CTASSERT(sizeof (zfs_cmd_legacy_t) == 4544);
#endif

/*
 * Decode the ioctl argument.  Only the 24-byte wrapper is
 * accepted.  Stock LP64 tools already produce that size; our
 * ILP32 tools pad explicitly so the layout matches.  The old
 * 20-byte i386 form is not accepted.
 *
 * Do not require zfs_pad == 0.  Stock LP64 zfs(8)/zpool(8)
 * never wrote the hidden alignment hole after the version
 * word, so that field is leftover stack.
 */
static int
zfsdev_iocparm_from_arg(void *arg, uint_t len, zfs_iocparm_t *zp)
{

	if (len != sizeof (zfs_iocparm_t))
		return (EINVAL);
	/*
	 * ioctl(9) stages into a u_long[] on ILP32.  The
	 * uint64_t fields sit at offset 8; memcpy avoids an
	 * unaligned struct assignment on ARM/MIPS/SPARC/PPC32.
	 */
	(void) memcpy(zp, arg, sizeof (*zp));
	return (0);
}

static int
zfsdev_ioctl(dev_t dev, ulong_t zcmd, void *arg, int flag, struct lwp *l)
{
	uint_t len;
	int vecnum;
	zfs_iocparm_t zp;
	zfs_cmd_t *zc;
#ifdef ZFS_LEGACY_SUPPORT
	zfs_cmd_legacy_t *zcl = NULL;
#endif
	int rc, error;
	void *uaddr;
	size_t cmdsz;

	(void) dev;
	(void) flag;
	(void) l;

	len = IOCPARM_LEN(zcmd);
	vecnum = ZFS_IOCREQ(zcmd);

	if (zfsdev_iocparm_from_arg(arg, len, &zp) != 0) {
		if (zfs_ioctl_debug != 0)
			printf("ZFS: ioctl bad iocparm len %u "
			    "(want %zu) cmd=0x%lx\n",
			    len, sizeof (zfs_iocparm_t), (unsigned long)zcmd);
		return (EINVAL);
	}

	uaddr = (void *)(uintptr_t)zp.zfs_cmd;
	cmdsz = (size_t)zp.zfs_cmd_size;
	zc = kmem_zalloc(sizeof (zfs_cmd_t), KM_SLEEP);

	switch (zp.zfs_ioctl_version) {
	case ZFS_IOCVER_OZFS:
		if (cmdsz != sizeof (zfs_cmd_t)) {
			error = EINVAL;
			goto out;
		}
		if (copyin(uaddr, zc, sizeof (zfs_cmd_t)) != 0) {
			error = EFAULT;
			goto out;
		}
		break;
#ifdef ZFS_LEGACY_SUPPORT
	case ZFS_IOCVER_LEGACY:	/* == CURRENT == PAD; stock NetBSD 11 */
	{
		int nvec;

		if (cmdsz != sizeof (zfs_cmd_legacy_t)) {
			if (zfs_ioctl_debug != 0)
				printf("ZFS: legacy ioctl cmdsz %zu != %zu "
				    "(stock NetBSD 11 ABI mismatch)\n",
				    cmdsz, sizeof (zfs_cmd_legacy_t));
			error = EINVAL;
			goto out;
		}
		zcl = kmem_zalloc(sizeof (zfs_cmd_legacy_t), KM_SLEEP);
		if (copyin(uaddr, zcl, sizeof (zfs_cmd_legacy_t)) != 0) {
			error = EFAULT;
			goto out;
		}
		zfs_cmd_legacy_to_ozfs(zcl, zc);
		nvec = zfs_ioctl_legacy_to_ozfs(vecnum);
		if (nvec < 0) {
			error = ENOTTY;
			goto out;
		}
		vecnum = nvec;
		break;
	}
#endif
	default:
		error = ENOTTY;
		goto out;
	}

	/*
	 * Run the ioctl on the calling thread so ddi_copyout of
	 * nvlists (POOL_CONFIGS / STATS / GET) uses the user
	 * process's vmspace.  spa_create/destroy/import/export
	 * also stay on this LWP, same as Linux/FreeBSD.  Only
	 * vnode I/O from a user LWP hops onto the file worker.
	 */
	if (zfs_ioctl_debug != 0)
		printf("ZFS: ioctl vec=%d enter pid=%d %.*s\n", vecnum,
		    (int)curproc->p_pid, (int)sizeof (zc->zc_name),
		    zc->zc_name);
	error = zfsdev_ioctl_common(vecnum, zc, 0);
	if (zfs_ioctl_debug != 0)
		printf("ZFS: ioctl vec=%d exit pid=%d %.*s err=%d\n", vecnum,
		    (int)curproc->p_pid, (int)sizeof (zc->zc_name),
		    zc->zc_name, error);

#ifdef ZFS_LEGACY_SUPPORT
	if (zcl != NULL) {
		zfs_cmd_ozfs_to_legacy(zc, zcl);
		rc = copyout(zcl, uaddr, sizeof (*zcl));
	} else
#endif
	{
		rc = copyout(zc, uaddr, sizeof (*zc));
	}
	if (error == 0 && rc != 0)
		error = EFAULT;
out:
#ifdef ZFS_LEGACY_SUPPORT
	if (zcl != NULL)
		kmem_free(zcl, sizeof (zfs_cmd_legacy_t));
#endif
	kmem_free(zc, sizeof (zfs_cmd_t));
	return (error);
}

static int
zfsdev_fo_ioctl(struct file *fp, ulong_t cmd, void *data)
{
	return (zfsdev_ioctl(makedev(zfs_cmajor, 0), cmd, data,
	    fp->f_flag, curlwp));
}

static int
zfsdev_fo_close(struct file *fp)
{
	if (fp->f_data != NULL)
		zfsdev_state_destroy(fp);
	fp->f_data = NULL;
	mutex_enter(&zfsdev_state_lock);
	if (zfsdev_opens > 0)
		zfsdev_opens--;
	mutex_exit(&zfsdev_state_lock);
	return (0);
}

static int
zfsdev_bopen(dev_t dev, int flag, int mode, struct lwp *l)
{
	(void) dev;
	(void) flag;
	(void) mode;
	(void) l;
	return (ENXIO);
}

static int
zfsdev_open(dev_t dev, int flag, int mode, struct lwp *l)
{
	file_t *fp;
	int fd, error;

	(void) dev;
	(void) mode;
	(void) l;

	mutex_enter(&zfsdev_state_lock);
	if (zfsdev_unloading) {
		mutex_exit(&zfsdev_state_lock);
		return (EBUSY);
	}
	mutex_exit(&zfsdev_state_lock);

	/*
	 * fd_clone gives this open its own fileops and f_data.
	 * spec_open treats EMOVEFD as a failed vnode open and
	 * drops the opencnt; vn_open installs the cloned fd.
	 * State is created before the clone so f_data is the
	 * zfsdev_state_t fo_close destroys.  No fileops copy
	 * and no side map.
	 */
	error = fd_allocfile(&fp, &fd);
	if (error != 0)
		return (error);
	fp->f_ops = &zfsdev_fileops;

	mutex_enter(&zfsdev_state_lock);
	if (zfsdev_unloading) {
		mutex_exit(&zfsdev_state_lock);
		fd_abort(curproc, fp, fd);
		return (EBUSY);
	}
	error = zfsdev_state_init(fp);
	if (error == 0)
		zfsdev_opens++;
	mutex_exit(&zfsdev_state_lock);
	if (error != 0) {
		fd_abort(curproc, fp, fd);
		return (error);
	}
	return (fd_clone(fp, fd, flag, &zfsdev_fileops, fp->f_data));
}

static int
zfsdev_close(dev_t dev, int flag, int mode, struct lwp *l)
{
	/*
	 * The cloned fd does not hold the spec vnode open, so
	 * this is not the per-fd close.  fo_close is.
	 */
	(void) dev;
	(void) flag;
	(void) mode;
	(void) l;
	return (0);
}

int
zfsdev_attach(void)
{
	struct vnode *vp;
	struct vattr va;
	int error;

	/*
	 * Character major 190, the number in NetBSD sys/conf/majors
	 * and in MAKEDEV (`sh MAKEDEV zfs`).  The test harness runs
	 * that when the node is missing.  Create it here too so a
	 * modload is enough, and force 0666: MAKEDEV uses 600 and
	 * do_sys_mknod applies the loader umask.
	 */
	error = devsw_attach("zfs", &zfs_bdevsw, &zfs_bmajor,
	    &zfs_cdevsw, &zfs_cmajor);
	if (error != 0 && error != EEXIST) {
		printf("ZFS: devsw_attach zfs failed, rc = %d\n", error);
		return (error);
	}
	if (zfs_cmajor < 0) {
		printf("ZFS: no character major allocated for zfs\n");
		return (EINVAL);
	}
	/*
	 * Replace any stale /dev/zfs (old openzfs or osnet clone).
	 */
	error = do_sys_unlink("/dev/zfs", UIO_SYSSPACE);
	if (error != 0 && error != ENOENT)
		printf("ZFS: unlink /dev/zfs rc = %d\n", error);
	error = do_sys_mknod(curlwp, "/dev/zfs", S_IFCHR | 0666,
	    makedev(zfs_cmajor, 0), UIO_SYSSPACE);
	if (error == EEXIST)
		error = 0;
	if (error != 0) {
		printf("ZFS: mknod /dev/zfs rc = %d major %d\n",
		    error, (int)zfs_cmajor);
		return (error);
	}
	/*
	 * do_sys_mknod applies the loader's umask (typically 022),
	 * so 0666 becomes 0644. Force the mode for ZFS delegation support.
	 */
	error = namei_simple_kernel("/dev/zfs", NSM_FOLLOW_TRYEMULROOT, &vp);
	if (error == 0) {
		vn_lock(vp, LK_EXCLUSIVE | LK_RETRY);
		vattr_null(&va);
		va.va_mode = 0666;
		error = VOP_SETATTR(vp, &va, curlwp->l_cred);
		VOP_UNLOCK(vp);
		vrele(vp);
		if (error != 0)
			printf("ZFS: chmod /dev/zfs 0666 rc = %d\n", error);
	}
	return (0);
}

void
zfsdev_detach(void)
{
	devsw_detach(&zfs_bdevsw, &zfs_cdevsw);
}

void
zfsdev_private_set_state(void *priv, zfsdev_state_t *zs)
{
	struct file *ff = priv;

	if (ff == NULL)
		return;
	ff->f_data = zs;
}

zfsdev_state_t *
zfsdev_private_get_state(void *priv)
{
	struct file *ff = priv;

	if (ff == NULL || ff->f_ops != &zfsdev_fileops)
		return (NULL);
	return (ff->f_data);
}

void *
zfsdev_bind_file(void *ffp)
{
	struct file *ff = ffp;

	if (ff == NULL || ff->f_ops != &zfsdev_fileops)
		return (NULL);
	return (ff->f_data);
}

extern uint64_t metaslab_force_ganging;
extern uint_t metaslab_force_ganging_pct;
extern int zfs_scan_suspend_progress;
extern int zfs_super_owner;
extern uint64_t zfs_arc_max;
extern uint64_t zfs_arc_min;
extern uint64_t zfs_livelist_max_entries;
extern uint64_t zfs_lua_max_memlimit;
extern char zfs_bootfs[];
extern void arc_tuning_update(boolean_t);
extern uint64_t arc_all_memory(void);

#ifndef	MIN_ARC_MAX
#define	MIN_ARC_MAX	(32ULL * 1024ULL * 1024ULL)
#endif

#ifdef SYSCTLFN_ARGS
static int
sysctl_arc_max(SYSCTLFN_ARGS)
{
	struct sysctlnode node;
	uint64_t val;
	int error;

	val = zfs_arc_max;
	node = *rnode;
	node.sysctl_data = &val;
	error = sysctl_lookup(SYSCTLFN_CALL(&node));
	if (error || newp == NULL)
		return (error);
	if (val != 0 && (val < MIN_ARC_MAX || val >= arc_all_memory()))
		return (EINVAL);
	zfs_arc_max = val;
	arc_tuning_update(B_TRUE);
	return (0);
}

static int
sysctl_arc_min(SYSCTLFN_ARGS)
{
	struct sysctlnode node;
	uint64_t val;
	int error;

	val = zfs_arc_min;
	node = *rnode;
	node.sysctl_data = &val;
	error = sysctl_lookup(SYSCTLFN_CALL(&node));
	if (error || newp == NULL)
		return (error);
	if (val != 0 && (val < (2ULL << 17) ||
	    (zfs_arc_max != 0 && val > zfs_arc_max)))
		return (EINVAL);
	zfs_arc_min = val;
	arc_tuning_update(B_TRUE);
	return (0);
}
#endif

/*
 * Parent token is FreeBSD's "_vfs_zfs_prefetch" → "vfs.zfs.prefetch".
 * Strip the leading underscore and turn remaining '_' into '.'.
 */
static void
zfs_nb_sysctl_parent_path(const char *parent, char *out, size_t n)
{
	size_t i, o = 0;

	if (parent == NULL || parent[0] == '\0') {
		(void) strlcpy(out, "vfs.zfs", n);
		return;
	}
	if (parent[0] == '_')
		parent++;
	for (i = 0; parent[i] != '\0' && o + 1 < n; i++)
		out[o++] = (parent[i] == '_') ? '.' : parent[i];
	out[o] = '\0';
}

static const struct sysctlnode *
zfs_sysctl_find_child(const struct sysctlnode *parent, const char *name);

/*
 * NetBSD stores sysctl nodes in a realloc'd array on the parent.
 * Inserting a sibling memmove's existing nodes and rewrites
 * grandchild parent pointers.  Cached struct sysctlnode * values
 * then dangle; the next sysctl_createv panics in sysctl_realloc /
 * kern_malloc (321's load-time fault at zfs_nb_sysctl_install).
 * vfs.zfs itself is stable (we never add siblings of it).  Always
 * walk from that root.
 */
static const struct sysctlnode *
zfs_nb_sysctl_walk(const struct sysctlnode *root, const char *path)
{
	const struct sysctlnode *n;
	const char *p, *dot;
	char name[SYSCTL_NAMELEN];
	size_t l;

	if (root == NULL || path == NULL)
		return (NULL);
	if (strcmp(path, "vfs.zfs") == 0)
		return (root);
	if (strncmp(path, "vfs.zfs.", 8) != 0)
		return (NULL);
	n = root;
	p = path + 8;
	while (*p != '\0' && n != NULL) {
		dot = strchr(p, '.');
		l = (dot != NULL) ? (size_t)(dot - p) : strlen(p);
		if (l == 0 || l >= sizeof (name))
			return (NULL);
		(void) memcpy(name, p, l);
		name[l] = '\0';
		n = zfs_sysctl_find_child(n, name);
		p = (dot != NULL) ? dot + 1 : p + l;
	}
	return (n);
}

/*
 * mkdir -p a dotted path under vfs.zfs.  Re-walk after every
 * createv so the pointer we hand back is live.
 */
static const struct sysctlnode *
zfs_nb_sysctl_ensure(struct sysctllog **logp, const struct sysctlnode *root,
    const char *path)
{
	const struct sysctlnode *pnode, *nnode;
	char parent[96], name[SYSCTL_NAMELEN];
	const char *dot;
	size_t plen, nlen;

	pnode = zfs_nb_sysctl_walk(root, path);
	if (pnode != NULL)
		return (pnode);
	dot = strrchr(path, '.');
	if (dot == NULL || dot == path)
		return (NULL);
	plen = (size_t)(dot - path);
	nlen = strlen(dot + 1);
	if (plen >= sizeof (parent) || nlen == 0 || nlen >= sizeof (name))
		return (NULL);
	(void) memcpy(parent, path, plen);
	parent[plen] = '\0';
	(void) memcpy(name, dot + 1, nlen + 1);
	if (zfs_nb_sysctl_ensure(logp, root, parent) == NULL)
		return (NULL);
	pnode = zfs_nb_sysctl_walk(root, parent);
	if (pnode == NULL)
		return (NULL);
	nnode = NULL;
	(void) (sysctl_createv)(logp, 0, &pnode, &nnode,
	    0, CTLTYPE_NODE, name,
	    SYSCTL_DESCR(name),
	    NULL, 0, NULL, 0, CTL_CREATE, CTL_EOL);
	return (zfs_nb_sysctl_walk(root, path));
}

/*
 * RDTUN and RD are read-only.  RWTUN and RW are read-write.
 * No perm bit is read-write: CTLFLAG_RW used to be defined as 0,
 * so "not RWTUN" rejected zfs_arc_free_target.
 */
static int
zfs_nb_sysctl_writable(int fl)
{
	if ((fl & (CTLFLAG_RWTUN | CTLFLAG_RW)) != 0)
		return (1);
	if ((fl & (CTLFLAG_RDTUN | CTLFLAG_RD)) != 0)
		return (0);
	return (1);
}

#ifdef SYSCTLFN_ARGS
/*
 * Bridge a FreeBSD SYSCTL_PROC handler onto NetBSD SYSCTLFN.  The
 * descriptor is stored as sysctl_data.  req pointers are kernel
 * scratch; sysctl_handle_* (and sbuf_finish) write those, then we
 * copyout.  Scratch is heap: NetBSD kthread stacks cannot hold a
 * PAGE_SIZE buffer.
 */
static int
zfs_nb_sysctl_proc(SYSCTLFN_ARGS)
{
	const struct zfs_nb_sysctl *s = rnode->sysctl_data;
	struct sysctl_req req;
	char *kold = NULL, *knew = NULL;
	size_t ksz = PAGE_SIZE;
	int error;

	if (s == NULL || s->handler == NULL)
		return (EOPNOTSUPP);

	/*
	 * sysctl_dispatch does not check write permission.
	 * sysctl_lookup does, and this handler never calls it.
	 * RDTUN and RD stay read-only.  RWTUN and RW are writable
	 * and still require the root sysctl credential.  A missing
	 * perm bit is writable: CTLFLAG_RW used to be 0.
	 */
	if (newp != NULL) {
		if (!zfs_nb_sysctl_writable(s->flags))
			return (EPERM);
		error = kauth_authorize_system(curlwp->l_cred,
		    KAUTH_SYSTEM_SYSCTL, KAUTH_REQ_SYSTEM_SYSCTL_MODIFY,
		    NULL, NULL, NULL);
		if (error != 0)
			return (error);
	}

	kold = kmem_zalloc(ksz, KM_SLEEP);
	memset(&req, 0, sizeof (req));
	req.oldptr = kold;
	req.oldlen = ksz;
	if (newp != NULL && newlen > 0) {
		size_t n = newlen;

		if (n >= ksz)
			n = ksz - 1;
		knew = kmem_zalloc(ksz, KM_SLEEP);
		error = copyin(newp, knew, n);
		if (error != 0) {
			kmem_free(knew, ksz);
			kmem_free(kold, ksz);
			return (error);
		}
		knew[n] = '\0';
		req.newptr = knew;
		req.newlen = n;
	}

	error = (*s->handler)(NULL, s->ptr, s->arg, &req);
	if (error == 0 && oldlenp != NULL) {
		size_t n = req.oldidx;

		if (oldp == NULL || *oldlenp == 0) {
			*oldlenp = n;
		} else if (n > *oldlenp) {
			*oldlenp = n;
			error = ENOMEM;
		} else if (n > 0) {
			error = copyout(kold, oldp, n);
			*oldlenp = n;
		} else {
			*oldlenp = 0;
		}
	}
	if (knew != NULL)
		kmem_free(knew, ksz);
	kmem_free(kold, ksz);
	return (error);
}

static int
zfs_nb_proc_ctype(int flags)
{
	int t = flags & 0xff;

	if (t == CTLTYPE_STRING)
		return (CTLTYPE_STRING);
	if (t == CTLTYPE_QUAD)
		return (CTLTYPE_QUAD);
	if (t == CTLTYPE_LONG)
		return (CTLTYPE_LONG);
	return (CTLTYPE_INT);
}
#endif

static const struct sysctlnode *
zfs_sysctl_find_child(const struct sysctlnode *parent, const char *name)
{
	const struct sysctlnode *c;
	uint_t i, n;

	if (parent == NULL || name == NULL)
		return (NULL);
	n = parent->sysctl_clen;
	c = parent->sysctl_child;
	if (c == NULL || n == 0)
		return (NULL);
	for (i = 0; i < n; i++) {
		if (strcmp(c[i].sysctl_name, name) == 0)
			return (&c[i]);
	}
	return (NULL);
}

static void
zfs_sysctl_add_arc_minmax(struct sysctllog **logp, const struct sysctlnode *arc)
{
	const struct sysctlnode *parent = arc;
	int error;

	if (parent == NULL)
		return;
	if (zfs_sysctl_find_child(parent, "max") == NULL) {
#ifdef SYSCTLFN_ARGS
		error = sysctl_createv(logp, 0, &parent, NULL,
		    CTLFLAG_READWRITE, CTLTYPE_QUAD, "max",
		    SYSCTL_DESCR("maximum ARC size"),
		    sysctl_arc_max, 0, &zfs_arc_max, sizeof (uint64_t),
		    CTL_CREATE, CTL_EOL);
#else
		error = sysctl_createv(logp, 0, &parent, NULL,
		    CTLFLAG_READWRITE, CTLTYPE_QUAD, "max",
		    SYSCTL_DESCR("maximum ARC size"),
		    NULL, 0, &zfs_arc_max, sizeof (uint64_t),
		    CTL_CREATE, CTL_EOL);
#endif
		if (error != 0)
			printf("openzfs: vfs.zfs.arc.max err %d\n", error);
	}
	parent = arc;
	if (zfs_sysctl_find_child(parent, "min") == NULL) {
#ifdef SYSCTLFN_ARGS
		error = sysctl_createv(logp, 0, &parent, NULL,
		    CTLFLAG_READWRITE, CTLTYPE_QUAD, "min",
		    SYSCTL_DESCR("minimum ARC size"),
		    sysctl_arc_min, 0, &zfs_arc_min, sizeof (uint64_t),
		    CTL_CREATE, CTL_EOL);
#else
		error = sysctl_createv(logp, 0, &parent, NULL,
		    CTLFLAG_READWRITE, CTLTYPE_QUAD, "min",
		    SYSCTL_DESCR("minimum ARC size"),
		    NULL, 0, &zfs_arc_min, sizeof (uint64_t),
		    CTL_CREATE, CTL_EOL);
#endif
		if (error != 0)
			printf("openzfs: vfs.zfs.arc.min err %d\n", error);
	}
}

/*
 * NetBSD sysctl(9) node names are at most SYSCTL_NAMELEN-1 bytes.
 * That is a NetBSD limit, not an OpenZFS one.  The C identifiers
 * and the Linux/FreeBSD module-parameter names stay the long form.
 * A leaf that does not fit is not registered.  It is not abbreviated
 * and not truncated: either name would be a platform-specific path,
 * and cross-platform instructions would no longer match the sysctl.
 * Raising the limit is a kernel change (SYSCTL_NAMELEN).
 */
static const char *
zfs_nb_sysctl_leaf(const char *name)
{
	if (strlen(name) >= SYSCTL_NAMELEN) {
		printf("openzfs: sysctl %s not registered "
		    "(NetBSD SYSCTL_NAMELEN-1 is %d)\n",
		    name, SYSCTL_NAMELEN - 1);
		return (NULL);
	}
	return (name);
}

/*
 * Walk link_set_zfs_nb_sysctls via module_find_section().  Using
 * __link_set_decl would reference __start_link_set_zfs_nb_sysctls,
 * which kobj_checksyms cannot resolve in a kmod (those symbols exist
 * only in the statically linked kernel).
 *
 * Re-walk from vfs.zfs before every createv.  See zfs_nb_sysctl_walk.
 */
void
zfs_nb_sysctl_install(struct sysctllog **logp, const struct sysctlnode *vfs_zfs)
{
	struct zfs_nb_sysctl *const *pp, *const *start, *const *end;
	char parent[96];
	const struct sysctlnode *pnode;
	int ctype, flags, nleaf = 0, nskip = 0;
	size_t sz, secsz;
	void *sec;

	if (vfs_zfs == NULL)
		return;
	if (module_find_section("link_set_zfs_nb_sysctls", &sec, &secsz) != 0 ||
	    sec == NULL || secsz < sizeof (*pp) ||
	    (secsz % sizeof (*pp)) != 0) {
		printf("openzfs: SYSCTL link-set missing or misaligned "
		    "(%zu bytes)\n", secsz);
		return;
	}
	start = sec;
	end = (struct zfs_nb_sysctl *const *)((char *)sec + secsz);

	for (pp = start; pp < end; pp++) {
		const struct zfs_nb_sysctl *s = *pp;
		if (s == NULL || s->kind != ZFS_NB_SYSCTL_NODE)
			continue;
		if (s->name == NULL || s->name[0] == '\0')
			continue;
		if (strlen(s->name) >= SYSCTL_NAMELEN) {
			nskip++;
			continue;
		}
		zfs_nb_sysctl_parent_path(s->parent, parent, sizeof (parent));
		if (zfs_nb_sysctl_ensure(logp, vfs_zfs, parent) == NULL)
			continue;
		(void) snprintf(parent, sizeof (parent), "%s.%s",
		    parent, s->name);
		(void) zfs_nb_sysctl_ensure(logp, vfs_zfs, parent);
	}

	for (pp = start; pp < end; pp++) {
		const struct zfs_nb_sysctl *s = *pp;
		const char *leaf;

		if (s == NULL || s->kind == ZFS_NB_SYSCTL_NODE)
			continue;
		if (s->name == NULL || s->name[0] == '\0')
			continue;
		leaf = zfs_nb_sysctl_leaf(s->name);
		if (leaf == NULL) {
			nskip++;
			continue;
		}
		if (s->kind != ZFS_NB_SYSCTL_PROC && s->ptr == NULL)
			continue;
		if (s->kind == ZFS_NB_SYSCTL_PROC && s->handler == NULL)
			continue;
		zfs_nb_sysctl_parent_path(s->parent, parent, sizeof (parent));
		if (zfs_nb_sysctl_ensure(logp, vfs_zfs, parent) == NULL)
			continue;
		pnode = zfs_nb_sysctl_walk(vfs_zfs, parent);
		if (pnode == NULL)
			continue;
		flags = zfs_nb_sysctl_writable(s->flags) ?
		    CTLFLAG_READWRITE : CTLFLAG_READONLY;
		sz = s->size;
		switch (s->kind) {
		case ZFS_NB_SYSCTL_INT:
		case ZFS_NB_SYSCTL_UINT:
			ctype = CTLTYPE_INT;
			sz = 0;
			break;
		case ZFS_NB_SYSCTL_LONG:
			ctype = CTLTYPE_LONG;
			if (ctype == CTLTYPE_INT)
				sz = 0;
			else if (sz == 0)
				sz = sizeof (long);
			break;
		case ZFS_NB_SYSCTL_U64:
			ctype = CTLTYPE_QUAD;
			if (sz == 0)
				sz = sizeof (uint64_t);
			break;
		case ZFS_NB_SYSCTL_STRING:
			ctype = CTLTYPE_STRING;
			break;
		case ZFS_NB_SYSCTL_PROC:
#ifdef SYSCTLFN_ARGS
			ctype = zfs_nb_proc_ctype(s->flags);
			if (ctype == CTLTYPE_QUAD && sz == 0)
				sz = sizeof (uint64_t);
			if (ctype == CTLTYPE_STRING && sz == 0)
				sz = 256;
			if ((sysctl_createv)(logp, 0, &pnode, NULL, flags,
			    ctype, leaf,
			    SYSCTL_DESCR(s->desc != NULL ? s->desc : s->name),
			    zfs_nb_sysctl_proc, 0, __UNCONST(s), sz,
			    CTL_CREATE, CTL_EOL) == 0 ||
			    /* EEXIST: previous load left CTLFLAG_PERMANENT. */
			    (sysctl_createv)(logp, 0, &pnode, NULL, flags,
			    ctype, leaf,
			    SYSCTL_DESCR(s->desc != NULL ? s->desc : s->name),
			    zfs_nb_sysctl_proc, 0, __UNCONST(s), sz,
			    CTL_EOL) == 0)
				nleaf++;
#endif
			continue;
		default:
			continue;
		}
		if ((sysctl_createv)(logp, 0, &pnode, NULL, flags, ctype,
		    leaf, SYSCTL_DESCR(s->desc != NULL ? s->desc : s->name),
		    NULL, 0, s->ptr, sz, CTL_CREATE, CTL_EOL) == 0 ||
		    (sysctl_createv)(logp, 0, &pnode, NULL, flags, ctype,
		    leaf, SYSCTL_DESCR(s->desc != NULL ? s->desc : s->name),
		    NULL, 0, s->ptr, sz, CTL_EOL) == 0)
			nleaf++;
	}

	pnode = zfs_nb_sysctl_walk(vfs_zfs, "vfs.zfs.arc");
	zfs_sysctl_add_arc_minmax(logp, pnode);

	if (nleaf == 0)
		printf("openzfs: SYSCTL link-set empty "
		    "(rebuild all .o after sysctl.h change)\n");
	(void) nskip;
}


static void
zfs_sysctl_init(void)
{
	const struct sysctlnode *rnode, *vnode;

	if (sysctl_createv(&zfs_sysctl_log, 0, NULL, &rnode,
	    CTLFLAG_PERMANENT, CTLTYPE_NODE, "zfs",
	    SYSCTL_DESCR("OpenZFS"),
	    NULL, 0, NULL, 0, CTL_VFS, CTL_CREATE, CTL_EOL) != 0) {
		/*
		 * Previous load left vfs.zfs in place (CTLFLAG_PERMANENT).
		 * Look it up and keep installing children.
		 */
		if (sysctl_createv(&zfs_sysctl_log, 0, NULL, &rnode,
		    0, CTLTYPE_NODE, "zfs",
		    SYSCTL_DESCR("OpenZFS"),
		    NULL, 0, NULL, 0, CTL_VFS, CTL_EOL) != 0) {
			printf("openzfs: vfs.zfs node missing\n");
			return;
		}
	}
	if (sysctl_createv(&zfs_sysctl_log, 0, &rnode, &vnode,
	    CTLFLAG_PERMANENT, CTLTYPE_NODE, "version",
	    SYSCTL_DESCR("version"),
	    NULL, 0, NULL, 0, CTL_CREATE, CTL_EOL) != 0)
		vnode = zfs_sysctl_find_child(rnode, "version");
	if (vnode != NULL) {
		(void) sysctl_createv(&zfs_sysctl_log, 0, &vnode, NULL,
		    CTLFLAG_PERMANENT | CTLFLAG_READONLY,
		    CTLTYPE_INT, "ioctl",
		    SYSCTL_DESCR("ZFS ioctl version"),
		    NULL, 0, &zfs_version_ioctl, 0, CTL_CREATE, CTL_EOL);
		(void) sysctl_createv(&zfs_sysctl_log, 0, &vnode, NULL,
		    CTLFLAG_PERMANENT | CTLFLAG_READONLY,
		    CTLTYPE_STRING, "module",
		    SYSCTL_DESCR("OpenZFS module version"),
		    NULL, 0, zfs_version_module, 0, CTL_CREATE, CTL_EOL);
	}
	if (sysctl_createv(&zfs_sysctl_log, 0, &rnode, &vnode,
	    CTLFLAG_PERMANENT, CTLTYPE_NODE, "vol",
	    SYSCTL_DESCR("ZVOL"),
	    NULL, 0, NULL, 0, CTL_CREATE, CTL_EOL) != 0)
		vnode = zfs_sysctl_find_child(rnode, "vol");
	if (vnode != NULL) {
		(void) sysctl_createv(&zfs_sysctl_log, 0, &vnode, NULL,
		    CTLFLAG_PERMANENT | CTLFLAG_READWRITE,
		    CTLTYPE_INT, "mode",
		    SYSCTL_DESCR("default zvol volmode"),
		    NULL, 0, &zvol_volmode, 0, CTL_CREATE, CTL_EOL);
		(void) sysctl_createv(&zfs_sysctl_log, 0, &vnode, NULL,
		    CTLFLAG_PERMANENT | CTLFLAG_READWRITE,
		    CTLTYPE_INT, "inhibit_dev",
		    SYSCTL_DESCR("do not create zvol device nodes"),
		    NULL, 0, &zvol_inhibit_dev, 0, CTL_CREATE, CTL_EOL);
	}

	/*
	 * Already-global tunables ZTS writes.  Static ones come from
	 * ZFS_MODULE_PARAM → SYSCTL_INT via zfs_nb_sysctl_install().
	 */
	(void) sysctl_createv(&zfs_sysctl_log, 0, &rnode, NULL,
	    CTLFLAG_PERMANENT | CTLFLAG_READWRITE,
	    CTLTYPE_INT, "scan_suspend_progress",
	    SYSCTL_DESCR("suspend scrub/resilver progress"),
	    NULL, 0, &zfs_scan_suspend_progress, 0,
	    CTL_CREATE, CTL_EOL);
	if (sysctl_createv(&zfs_sysctl_log, 0, &rnode, &vnode,
	    CTLFLAG_PERMANENT, CTLTYPE_NODE, "metaslab",
	    SYSCTL_DESCR("ZFS metaslab"),
	    NULL, 0, NULL, 0, CTL_CREATE, CTL_EOL) == 0) {
		(void) sysctl_createv(&zfs_sysctl_log, 0, &vnode, NULL,
		    CTLFLAG_PERMANENT | CTLFLAG_READWRITE,
		    CTLTYPE_QUAD, "force_ganging",
		    SYSCTL_DESCR("force ganging above this size"),
		    NULL, 0, &metaslab_force_ganging, sizeof (uint64_t),
		    CTL_CREATE, CTL_EOL);
		(void) sysctl_createv(&zfs_sysctl_log, 0, &vnode, NULL,
		    CTLFLAG_PERMANENT | CTLFLAG_READWRITE,
		    CTLTYPE_INT, "force_ganging_pct",
		    SYSCTL_DESCR("percent of large blocks to gang"),
		    NULL, 0, &metaslab_force_ganging_pct, 0,
		    CTL_CREATE, CTL_EOL);
	}
	if (sysctl_createv(&zfs_sysctl_log, 0, &rnode, &vnode,
	    CTLFLAG_PERMANENT, CTLTYPE_NODE, "arc",
	    SYSCTL_DESCR("ZFS ARC"),
	    NULL, 0, NULL, 0, CTL_CREATE, CTL_EOL) != 0)
		vnode = zfs_sysctl_find_child(rnode, "arc");
	if (sysctl_createv(&zfs_sysctl_log, 0, &rnode, &vnode,
	    CTLFLAG_PERMANENT, CTLTYPE_NODE, "livelist",
	    SYSCTL_DESCR("ZFS livelist"),
	    NULL, 0, NULL, 0, CTL_CREATE, CTL_EOL) == 0) {
		(void) sysctl_createv(&zfs_sysctl_log, 0, &vnode, NULL,
		    CTLFLAG_PERMANENT | CTLFLAG_READWRITE,
		    CTLTYPE_QUAD, "max_entries",
		    SYSCTL_DESCR("livelist max entries"),
		    NULL, 0, &zfs_livelist_max_entries, sizeof (uint64_t),
		    CTL_CREATE, CTL_EOL);
	}
	if (sysctl_createv(&zfs_sysctl_log, 0, &rnode, &vnode,
	    CTLFLAG_PERMANENT, CTLTYPE_NODE, "lua",
	    SYSCTL_DESCR("ZFS lua"),
	    NULL, 0, NULL, 0, CTL_CREATE, CTL_EOL) == 0) {
		(void) sysctl_createv(&zfs_sysctl_log, 0, &vnode, NULL,
		    CTLFLAG_PERMANENT | CTLFLAG_READWRITE,
		    CTLTYPE_QUAD, "max_memlimit",
		    SYSCTL_DESCR("channel program memory limit"),
		    NULL, 0, &zfs_lua_max_memlimit, sizeof (uint64_t),
		    CTL_CREATE, CTL_EOL);
	}
	(void) sysctl_createv(&zfs_sysctl_log, 0, &rnode, NULL,
	    CTLFLAG_PERMANENT | CTLFLAG_READWRITE,
	    CTLTYPE_INT, "super_owner",
	    SYSCTL_DESCR("dataset mounter is fs owner"),
	    NULL, 0, &zfs_super_owner, 0, CTL_CREATE, CTL_EOL);
	(void) sysctl_createv(&zfs_sysctl_log, 0, &rnode, NULL,
	    CTLFLAG_PERMANENT | CTLFLAG_READWRITE,
	    CTLTYPE_INT, "ioctl_debug",
	    SYSCTL_DESCR("print each /dev/zfs ioctl enter/exit"),
	    NULL, 0, &zfs_ioctl_debug, 0, CTL_CREATE, CTL_EOL);
	(void) sysctl_createv(&zfs_sysctl_log, 0, &rnode, NULL,
	    CTLFLAG_PERMANENT | CTLFLAG_READWRITE,
	    CTLTYPE_STRING, "bootfs",
	    SYSCTL_DESCR("Root dataset for vfs_mountroot"),
	    NULL, 0, zfs_bootfs, MNAMELEN, CTL_CREATE, CTL_EOL);

	zfs_nb_sysctl_install(&zfs_sysctl_log, rnode);
}

static void
zfs_sysctl_fini(void)
{
	sysctl_teardown(&zfs_sysctl_log);
}

static int
zfs__init(void)
{
	int error;

	extern void vdev_disk_init(void);
	extern int solaris__init(void);
	extern int zcommon_init(void);
	extern int zstd_init(void);
	extern int vdev_raidz_impl_get(char *, size_t);

	if ((error = solaris__init()) != 0)
		return (error);

	/*
	 * Linux module_init(openzfs_init):
	 *   zcommon_init(); icp_init(); zstd_init(); openzfs_init_os()
	 *     -> zfs_kmod_init() -> spa_init()
	 *        -> zio_init, vdev_raidz_math_init, chksum_init, qat_init
	 *
	 * FreeBSD:
	 *   module_init_early(zcommon_init)  SYSINIT SI_SUB_INT_CONFIG_HOOKS
	 *   module_init(zstd_init)           SYSINIT SI_SUB_LAST
	 *   zfs__init() calls zfs_kmod_init() only
	 *   icp_init is not compiled (Makefile.bsd has no illumos-crypto/kcf)
	 *
	 * NetBSD MODULE(9) has no SYSINIT.  include/os/netbsd/spl/sys/mod.h
	 * therefore leaves module_init() empty and this function is the
	 * hook.  Call the same functions, same order.  Do not skip
	 * zcommon_init -- without fletcher_4_init(), kfpu_allowed()==1
	 * picks fletcher_4_fastest_impl whose init_native is still NULL
	 * and zpool create dies at rip=0 (SMEP).
	 *
	 * icp_init() is Linux-only here: it registers KCF + aes_mod_init
	 * + sha2_mod_init.  FreeBSD/NetBSD do not compile that object.
	 * SHA2/BLAKE3 checksum SIMD is not KCF -- generic_impl_init()
	 * runs from chksum_init() -> chksum_benchmark() inside spa_init.
	 */
	if ((error = zcommon_init()) != 0) {
		printf("ZFS: zcommon_init failed, rc = %d\n", error);
		goto fail_solaris;
	}
	if ((error = zstd_init()) != 0) {
		printf("ZFS: zstd_init failed, rc = %d\n", error);
		goto fail_zcommon;
	}

	{
		extern int zfs_icp_aead_selftest(void);

		if ((error = zfs_icp_aead_selftest()) != 0) {
			printf("ZFS: AES-GCM/CCM self-test failed, rc = %d\n",
			    error);
			goto fail_selftest;
		}
	}

	vdev_disk_init();

	/*
	 * Small guests (ZTS 2 GB + file vdevs) OOM if ARC takes half
	 * of RAM. Only default a 256MB cap when physical memory is
	 * small; larger machines use arc_init's arc_all_memory() path.
	 */
	if (zfs_arc_max == 0 &&
	    (uint64_t)physmem * PAGE_SIZE <= 4ULL * 1024 * 1024 * 1024)
		zfs_arc_max = 256ULL * 1024ULL * 1024ULL;
	if (zfs_arc_min == 0)
		zfs_arc_min = 32ULL * 1024ULL * 1024ULL;
	if ((error = vfs_attach(&zfs_vfsops)) != 0) {
		printf("ZFS: vfs_attach failed, rc = %d\n", error);
		goto fail_zstd;
	}
	if ((error = zfs_kmod_init()) != 0) {
		printf("ZFS: Failed to load OpenZFS filesystem, rc = %d\n",
		    error);
		goto fail_vfs;
	}
	zfs_sysctl_init();

	printf("ZFS storage pool version: features support (%s)\n",
	    SPA_VERSION_STRING);
	return (0);

fail_vfs:
	(void) vfs_detach(&zfs_vfsops);
fail_zstd:
	{
		extern void vdev_disk_fini(void);
		extern void zstd_fini(void);
		vdev_disk_fini();
		zstd_fini();
	}
	goto fail_zcommon;
fail_selftest:
	{
		extern void zstd_fini(void);
		zstd_fini();
	}
fail_zcommon:
	{
		extern void zcommon_fini(void);
		zcommon_fini();
	}
fail_solaris:
	{
		extern int solaris__fini(void);
		(void) solaris__fini();
	}
	return (error);
}

static int
zfs__fini(void)
{
	if (zfs_busy() || zvol_busy())
		return (EBUSY);
	mutex_enter(&zfsdev_state_lock);
	if (zfsdev_opens != 0 || zfsdev_unloading) {
		mutex_exit(&zfsdev_state_lock);
		return (EBUSY);
	}
	zfsdev_unloading = 1;
	mutex_exit(&zfsdev_state_lock);
	{
		spa_t *spa;

		spa_namespace_enter(FTAG);
		spa = spa_next(NULL);
		spa_namespace_exit(FTAG);
		if (spa != NULL) {
			mutex_enter(&zfsdev_state_lock);
			zfsdev_unloading = 0;
			mutex_exit(&zfsdev_state_lock);
			return (EBUSY);
		}
	}
	{
		int error;

		error = vfs_detach(&zfs_vfsops);
		if (error != 0) {
			mutex_enter(&zfsdev_state_lock);
			zfsdev_unloading = 0;
			mutex_exit(&zfsdev_state_lock);
			return (error);
		}
	}
	zfs_sysctl_fini();
	zfs_kmod_fini();
	{
		extern void vdev_disk_fini(void);
		extern void zstd_fini(void);
		extern void zcommon_fini(void);
		extern int solaris__fini(void);
		vdev_disk_fini();
		zstd_fini();
		zcommon_fini();
		(void) solaris__fini();
	}
	return (0);
}


MODULE(MODULE_CLASS_VFS, openzfs, NULL);

static int
openzfs_modcmd(modcmd_t cmd, void *arg)
{
	int error = 0;

	(void) arg;
	switch (cmd) {
	case MODULE_CMD_INIT:
		error = zfs__init();
		break;
	case MODULE_CMD_FINI:
		error = zfs__fini();
		break;
	default:
		error = ENOTTY;
		break;
	}
	return (error);
}
