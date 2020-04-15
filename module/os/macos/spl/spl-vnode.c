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
 *
 * Copyright (C) 2008 MacZFS
 * Copyright (C) 2013 Jorgen Lundman <lundman@lundman.net>
 *
 */

#include <sys/sysmacros.h>
#include <sys/kauth.h>
#include <sys/vnode.h>
#include <sys/debug.h>
#include <sys/malloc.h>
#include <sys/list.h>
#include <sys/file.h>
#include <sys/fcntl.h>
#include <IOKit/IOLib.h>

#include <sys/taskq.h>
#include <TargetConditionals.h>
#include <AvailabilityMacros.h>

int
VOP_SPACE(struct vnode *vp, int cmd, struct flock *fl, int flags, offset_t off,
    cred_t *cr, void *ctx)
{
	int error = 0;
#ifdef F_PUNCHHOLE
	if (cmd == F_FREESP) {
		fpunchhole_t fpht;
		fpht.fp_flags = 0;
		fpht.fp_offset = fl->l_start;
		fpht.fp_length = fl->l_len;
		if (vnode_getwithref(vp) == 0) {
			error = VNOP_IOCTL(vp, F_PUNCHHOLE, (caddr_t)&fpht, 0,
			    ctx);
			(void) vnode_put(vp);
		}
	}
#endif
	return (error);
}

int
VOP_FSYNC(struct vnode *vp, int flags, void* unused, void *uused2)
{
	vfs_context_t vctx;
	int error;

	vctx = vfs_context_create((vfs_context_t)0);
	error = VNOP_FSYNC(vp, (flags == FSYNC), vctx);
	(void) vfs_context_rele(vctx);
	return (error);
}

int
VOP_GETATTR(struct vnode *vp, vattr_t *vap, int flags, void *x3, void *x4)
{
	vfs_context_t vctx;
	int error;

	vctx = vfs_context_create((vfs_context_t)0);
	error = vnode_getattr(vp, vap, vctx);
	(void) vfs_context_rele(vctx);
	return (error);
}

extern errno_t vnode_lookup(const char *path, int flags, struct vnode **vpp,
    vfs_context_t ctx);

extern errno_t vnode_lookupat(const char *path, int flags, struct vnode **vpp,
    vfs_context_t ctx, struct vnode *start_dvp);

errno_t
VOP_LOOKUP(struct vnode *dvp, struct vnode **vpp,
    struct componentname *cn, vfs_context_t ct)
{
	char *path = IOMalloc(MAXPATHLEN);
	char *lookup_name = cn->cn_nameptr;

	/*
	 * Lookup a name, to get vnode.
	 * If dvp is NULL, and it uses full path, just call vnode_lookup().
	 * If dvp is supplied, we need to build path (vnode_lookupat() is
	 * private.exports)
	 * However, VOP_LOOKUP() is only used by OSX calls, finder and rename.
	 * We could re-write that code to use /absolute/path.
	 */
	if (dvp != NULL) {
		int result, len;

		len = MAXPATHLEN;
		result = vn_getpath(dvp, path, &len);
		if (result != 0) {
			IOFree(path, MAXPATHLEN);
			return (result);
		}

		strlcat(path, "/", MAXPATHLEN);
		strlcat(path, cn->cn_nameptr, MAXPATHLEN);

		lookup_name = path;
	}
	IOFree(path, MAXPATHLEN);
	return (vnode_lookup(lookup_name, 0, vpp, ct));
}

void
vfs_mountedfrom(struct mount *vfsp, char *osname)
{
	(void) copystr(osname, vfs_statfs(vfsp)->f_mntfromname, MNAMELEN - 1,
	    0);
}

static kmutex_t	spl_getf_lock;
static list_t	spl_getf_list;

int
spl_vnode_init(void)
{
	mutex_init(&spl_getf_lock, NULL, MUTEX_DEFAULT, NULL);
	list_create(&spl_getf_list, sizeof (struct spl_fileproc),
	    offsetof(struct spl_fileproc, f_next));
	return (0);
}

void
spl_vnode_fini(void)
{
	mutex_destroy(&spl_getf_lock);
	list_destroy(&spl_getf_list);
}

#include <sys/file.h>
struct fileproc;

extern int fp_drop(struct proc *p, int fd, struct fileproc *fp, int locked);
extern int fp_drop_written(struct proc *p, int fd, struct fileproc *fp,
    int locked);
extern int fp_lookup(struct proc *p, int fd, struct fileproc **resultfp,
    int locked);
extern int fo_read(struct fileproc *fp, struct uio *uio, int flags,
    vfs_context_t ctx);
extern int fo_write(struct fileproc *fp, struct uio *uio, int flags,
    vfs_context_t ctx);
extern int file_vnode_withvid(int, struct vnode **, uint32_t *);
extern int file_drop(int);

/*
 * getf(int fd) - hold a lock on a file descriptor, to be released by calling
 * releasef(). On OSX we will also look up the vnode of the fd for calls
 * to spl_vn_rdwr().
 */
void *
getf(int fd)
{
	struct fileproc *fp  = NULL;
	struct spl_fileproc *sfp = NULL;
	struct vnode *vp = NULL;
	uint32_t vid;

	/*
	 * We keep the "fp" pointer as well, both for unlocking in releasef()
	 * and used in vn_rdwr().
	 */

	sfp = kmem_alloc(sizeof (*sfp), KM_SLEEP);
	if (!sfp)
		return (NULL);

	/* We no longer use fp */

	dprintf("current_proc %p: fd %d fp %p vp %p\n", current_proc(),
	    fd, fp, vp);

	sfp->f_vnode	= vp;
	sfp->f_fd		= fd;
	sfp->f_offset	= 0;
	sfp->f_proc		= current_proc();
	sfp->f_fp		= fp;

	/* Also grab vnode, so we can fish out the minor, for onexit */
	if (!file_vnode_withvid(fd, &vp, &vid)) {
		sfp->f_vnode = vp;

		if (vnode_getwithref(vp) != 0) {
			file_drop(fd);
			return (NULL);
		}

		enum vtype type;
		type = vnode_vtype(vp);
		if (type == VCHR || type == VBLK) {
			sfp->f_file = minor(vnode_specrdev(vp));
		}
		file_drop(fd);
	}

	mutex_enter(&spl_getf_lock);
	list_insert_tail(&spl_getf_list, sfp);
	mutex_exit(&spl_getf_lock);

	return (sfp);
}

struct vnode *
getf_vnode(void *fp)
{
	struct spl_fileproc *sfp = (struct spl_fileproc *)fp;
	struct vnode *vp = NULL;
	uint32_t vid;

	if (!file_vnode_withvid(sfp->f_fd, &vp, &vid)) {
		file_drop(sfp->f_fd);
	}

	return (vp);
}

void
releasefp(struct spl_fileproc *fp)
{
	if (fp->f_vnode != NULL)
		vnode_put(fp->f_vnode);

	/* Remove node from the list */
	mutex_enter(&spl_getf_lock);
	list_remove(&spl_getf_list, fp);
	mutex_exit(&spl_getf_lock);

	/* Free the node */
	kmem_free(fp, sizeof (*fp));
}

void
releasef(int fd)
{
	struct spl_fileproc *fp = NULL;
	struct proc *p;

	p = current_proc();
	mutex_enter(&spl_getf_lock);
	for (fp = list_head(&spl_getf_list); fp != NULL;
	    fp = list_next(&spl_getf_list, fp)) {
		if ((fp->f_proc == p) && fp->f_fd == fd) break;
	}
	mutex_exit(&spl_getf_lock);
	if (!fp)
		return; // Not found

	releasefp(fp);
}

/*
 * getf()/releasef() IO handler.
 */
#undef vn_rdwr
extern int vn_rdwr(enum uio_rw rw, struct vnode *vp, caddr_t base, int len,
	off_t offset, enum uio_seg segflg, int ioflg, kauth_cred_t cred,
	int *aresid, struct proc *p);

int spl_vn_rdwr(enum uio_rw rw,	struct spl_fileproc *sfp,
    caddr_t base, ssize_t len, offset_t offset, enum uio_seg seg,
    int ioflag, rlim64_t ulimit, cred_t *cr, ssize_t *residp)
{
	int error = 0;
	int aresid;

	VERIFY3P(sfp->f_vnode, !=, NULL);

	error = vn_rdwr(rw, sfp->f_vnode, base, len, offset, seg, ioflag,
	    cr, &aresid, sfp->f_proc);

	if (residp) {
		*residp = aresid;
	}

	return (error);
}

/* Regular vnode vn_rdwr */
int zfs_vn_rdwr(enum uio_rw rw, struct vnode *vp, caddr_t base, ssize_t len,
    offset_t offset, enum uio_seg seg, int ioflag, rlim64_t ulimit,
    cred_t *cr, ssize_t *residp)
{
	uio_t *auio;
	int spacetype;
	int error = 0;
	vfs_context_t vctx;

	spacetype = UIO_SEG_IS_USER_SPACE(seg) ? UIO_USERSPACE32 : UIO_SYSSPACE;

	vctx = vfs_context_create((vfs_context_t)0);
	auio = uio_create(1, 0, spacetype, rw);
	uio_reset(auio, offset, spacetype, rw);
	uio_addiov(auio, (uint64_t)(uintptr_t)base, len);

	if (rw == UIO_READ) {
		error = VNOP_READ(vp, auio, ioflag, vctx);
	} else {
		error = VNOP_WRITE(vp, auio, ioflag, vctx);
	}

	if (residp) {
		*residp = uio_resid(auio);
	} else {
		if (uio_resid(auio) && error == 0)
			error = EIO;
	}

	uio_free(auio);
	vfs_context_rele(vctx);

	return (error);
}

void
spl_rele_async(void *arg)
{
	struct vnode *vp = (struct vnode *)arg;
	if (vp) vnode_put(vp);
}

void
vn_rele_async(struct vnode *vp, void *taskq)
{
	VERIFY(taskq_dispatch((taskq_t *)taskq,
	    (task_func_t *)spl_rele_async, vp, TQ_SLEEP) != 0);
}

vfs_context_t
spl_vfs_context_kernel(void)
{
	return (NULL);
}

#undef build_path
extern int build_path(struct vnode *vp, char *buff, int buflen, int *outlen,
    int flags, vfs_context_t ctx);

int spl_build_path(struct vnode *vp, char *buff, int buflen, int *outlen,
    int flags, vfs_context_t ctx)
{
	// Private.exports
	// return (build_path(vp, buff, buflen, outlen, flags, ctx));
	printf("%s: missing implementation. All will fail.\n", __func__);

	buff[0] = 0;
	*outlen = 0;
	return (0);
}

/*
 * vnode_notify was moved from KERNEL_PRIVATE to KERNEL in 10.11, but to be
 * backward compatible, we keep the wrapper for now.
 */
extern int vnode_notify(struct vnode *, uint32_t, struct vnode_attr *);
int
spl_vnode_notify(struct vnode *vp, uint32_t type, struct vnode_attr *vap)
{
#if defined(MAC_OS_X_VERSION_10_11) &&	\
	(MAC_OS_X_VERSION_MIN_REQUIRED >= MAC_OS_X_VERSION_10_11)
	return (vnode_notify(vp, type, vap));
#else
	return (0);
#endif
}

extern int vfs_get_notify_attributes(struct vnode_attr *vap);
int
spl_vfs_get_notify_attributes(struct vnode_attr *vap)
{
#if defined(MAC_OS_X_VERSION_10_11) &&	\
	(MAC_OS_X_VERSION_MIN_REQUIRED >= MAC_OS_X_VERSION_10_11)
	return (vfs_get_notify_attributes(vap));
#else
	return (0);
#endif
}

/* Root directory vnode for the system a.k.a. '/' */
/*
 * Must use vfs_rootvnode() to acquire a reference, and
 * vnode_put() to release it
 */

struct vnode *
getrootdir(void)
{
	struct vnode *rvnode;

	rvnode = vfs_rootvnode();
	if (rvnode)
		vnode_put(rvnode);
	return (rvnode);
}


static inline int
spl_cache_purgevfs_impl(struct vnode *vp, void *arg)
{
	cache_purge(vp);
	cache_purge_negatives(vp);
	return (VNODE_RETURNED);
}

/*
 * Apple won't let us call cache_purgevfs() so let's try to get
 * as close as possible
 */
void
spl_cache_purgevfs(mount_t mp)
{
	(void) vnode_iterate(mp, VNODE_RELOAD, spl_cache_purgevfs_impl, NULL);
}

/* Gross hacks - find solutions */

/*
 * Sorry, but this is gross. But unable to find a way around it yet..
 * Maybe one day Apple will allow it.
 */
int
vnode_iocount(struct vnode *vp)
{
	int32_t *binvp;

	binvp = (int32_t *)vp;

	return (binvp[25]);
}

cred_t *
spl_kcred(void)
{
	cred_t *ret;

	/*
	 * How bad is it to return a released reference?
	 * We have no way to return it when we are done with it. But
	 * it is the kernel, so it should not go away.
	 */
	cred_t *cr = kauth_cred_proc_ref(kernproc);
	ret = cr;
	kauth_cred_unref(&cr);

	return (ret);
}
