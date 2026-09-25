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
 * Copyright (C) 2017 Jorgen Lundman <lundman@lundman.net>
 *
 */

#include <sys/vnode.h>
#include <spl-debug.h>
#include <sys/list.h>
#include <sys/file.h>

#include <sys/taskq.h>

#include <ntdddisk.h>
#include <Ntddstor.h>

#ifdef DEBUG_IOCOUNT
#include <sys/zfs_znode.h>
#endif

#include <Trace.h>

// #define	FIND_MAF


/* Counter for unique vnode ID */
static uint64_t vnode_vid_counter = 6; /* ZFSCTL_INO_SHARES + 1; */

/* Total number of active vnodes */
static uint64_t vnode_active = 0;

/* The kmem cache for vnodes */
static kmem_cache_t *vnode_cache = NULL;

/* List of all vnodes */
static kmutex_t vnode_all_list_lock;
static list_t   vnode_all_list;

/* list of all getf/releasef active */
static kmutex_t spl_getf_lock;
static list_t   spl_getf_list;

enum vtype iftovt_tab[16] = {
	VNON, VFIFO, VCHR, VNON, VDIR, VNON, VBLK, VNON,
	VREG, VNON, VLNK, VNON, VSOCK, VNON, VNON, VBAD,
};
int vttoif_tab[9] = {
	0, S_IFREG, S_IFDIR, S_IFBLK, S_IFCHR, S_IFLNK,
	S_IFSOCK, S_IFIFO, S_IFMT,
};

/*
 * In a real VFS the filesystem would register the callbacks for
 * VNOP_ACTIVE and VNOP_RECLAIM - but here we just call them direct
 */
extern int zfs_vnop_reclaim(struct vnode *);

int vnode_recycle_int(vnode_t *vp, int flags);

int
vn_open(char *pnamep, enum zfs_uio_seg seg, int filemode, int createmode,
    struct vnode **vpp, enum create crwhy, mode_t umask)
{
	// vfs_context_t *vctx;
	int fmode;
	int error = 0;

	fmode = filemode;
	if (crwhy)
		fmode |= O_CREAT;

	return (error);
}

int
vn_openat(char *pnamep, enum zfs_uio_seg seg, int filemode, int createmode,
    struct vnode **vpp, enum create crwhy,
    mode_t umask, struct vnode *startvp)
{
	char *path;
	// int pathlen = MAXPATHLEN;
	int error = 0;

	path = (char *)kmem_zalloc(MAXPATHLEN, KM_SLEEP);

	if (error == 0) {
	}

	kmem_free(path, MAXPATHLEN);
	return (error);
}

extern errno_t vnode_rename(const char *, const char *, int, vfs_context_t *);

errno_t
vnode_rename(const char *from, const char *to, int flags, vfs_context_t *vctx)
{
	/*
	 * We need proper KPI changes to be able to safely update
	 * the zpool.cache file. For now, we return EPERM.
	 */
	return (EPERM);
}

int
vn_rename(char *from, char *to, enum zfs_uio_seg seg)
{
	// vfs_context_t *vctx;
	int error = 0;

	// vctx = vfs_context_create((vfs_context_t)0);

	// error = vnode_rename(from, to, 0, vctx);

	// (void) vfs_context_rele(vctx);

	return (error);
}

extern errno_t vnode_remove(const char *, int, enum vtype, vfs_context_t *);

errno_t
vnode_remove(const char *name, int flag, enum vtype type, vfs_context_t *vctx)
{
	/*
	 * Now that zed ZFS Event Daemon can handle the rename of zpool.cache
	 * we will silence this limitation, and look in zed.d/config.sync.sh
	 */
	return (EPERM);
}


int
vn_remove(char *fnamep, enum zfs_uio_seg seg, enum rm dirflag)
{
	// vfs_context_t *vctx;
	// enum vtype type;
	int error = 0;
	return (error);
}

int zfs_vn_rdwr(enum zfs_uio_rw rw, struct vnode *vp, caddr_t base, ssize_t len,
    offset_t offset, enum zfs_uio_seg seg, int ioflag, rlim64_t ulimit,
    cred_t *cr, ssize_t *residp)
{
	int error = 0;

	if (rw == UIO_READ) {
		//  error = VNOP_READ(vp, auio, ioflag, vctx);
	} else {
		// error = VNOP_WRITE(vp, auio, ioflag, vctx);
	}

	if (residp) {
		// *residp = uio_resid(auio);
	} else {
		//  if (uio_resid(auio) && error == 0)
		error = EIO;
	}

	return (error);
}

int
kernel_ioctl(PDEVICE_OBJECT DeviceObject, FILE_OBJECT *FileObject,
    long cmd, void *inbuf, uint32_t inlen,
    void *outbuf, uint32_t outlen)
{
	// NTSTATUS status;
	// PFILE_OBJECT FileObject;

	dprintf("%s: trying to send kernel ioctl %x\n", __func__, cmd);

	IO_STATUS_BLOCK IoStatusBlock;
	KEVENT Event;
	PIRP Irp;
	NTSTATUS Status;
	// ULONG Remainder;
	// PAGED_CODE();

	/* Build the information IRP */
	KeInitializeEvent(&Event, SynchronizationEvent, FALSE);
	Irp = IoBuildDeviceIoControlRequest(cmd,
	    DeviceObject,
	    inbuf,
	    inlen,
	    outbuf,
	    outlen,
	    FALSE,
	    &Event,
	    &IoStatusBlock);
	if (!Irp)
		return (STATUS_NO_MEMORY);

	/* Override verification */
	IoGetNextIrpStackLocation(Irp)->Flags |= SL_OVERRIDE_VERIFY_VOLUME;

	if (FileObject != NULL)
		IoGetNextIrpStackLocation(Irp)->FileObject = FileObject;

	/* Do the request */
	Status = IoCallDriver(DeviceObject, Irp);
	if (Status == STATUS_PENDING) {
		/* Wait for completion */
		KeWaitForSingleObject(&Event,
		    Executive,
		    KernelMode,
		    FALSE,
		    NULL);
		Status = IoStatusBlock.Status;
	}

	return (Status);
}

/* Linux TRIM API */

#include <sys/mod.h>

uint64_t windows_trim_enabled = 0;
ZFS_MODULE_PARAM(, windows_, trim_enabled, U64, ZMOD_RW,
	"Windows: enable trim");

int
blk_queue_discard(PDEVICE_OBJECT dev)
{
	STORAGE_PROPERTY_QUERY spqTrim;
	spqTrim.PropertyId = (STORAGE_PROPERTY_ID)StorageDeviceTrimProperty;
	spqTrim.QueryType = PropertyStandardQuery;

	// DWORD bytesReturned = 0;
	DEVICE_TRIM_DESCRIPTOR dtd = { 0 };

	if (!windows_trim_enabled)
		return (0);

	if (kernel_ioctl(dev, NULL, IOCTL_STORAGE_QUERY_PROPERTY,
	    &spqTrim, sizeof (spqTrim), &dtd, sizeof (dtd)) == 0) {
		return (dtd.TrimEnabled);
	}
	return (0); // No trim
}

int
blk_queue_discard_secure(PDEVICE_OBJECT dev)
{
	return (0); // No secure trim
}

int
blk_queue_nonrot(PDEVICE_OBJECT dev)
{
	STORAGE_PROPERTY_QUERY spqSeekP;
	spqSeekP.PropertyId =
	    (STORAGE_PROPERTY_ID)StorageDeviceSeekPenaltyProperty;
	spqSeekP.QueryType = PropertyStandardQuery;
	// DWORD bytesReturned = 0;
	DEVICE_SEEK_PENALTY_DESCRIPTOR dspd = { 0 };
	if (kernel_ioctl(dev, NULL, IOCTL_STORAGE_QUERY_PROPERTY,
	    &spqSeekP, sizeof (spqSeekP), &dspd, sizeof (dspd)) == 0) {
		return (!dspd.IncursSeekPenalty);
	}
	return (0); // Not SSD;
}

int
blkdev_issue_discard_bytes(PDEVICE_OBJECT dev, uint64_t offset,
    uint64_t size, uint32_t flags)
{
	int Status = 0;
	struct setAttrAndRange {
		DEVICE_MANAGE_DATA_SET_ATTRIBUTES dmdsa;
		DEVICE_DATA_SET_RANGE range;
	} set;

	if (!windows_trim_enabled)
		return (-ENODEV);

	set.dmdsa.Size = sizeof (DEVICE_MANAGE_DATA_SET_ATTRIBUTES);
	set.dmdsa.Action = DeviceDsmAction_Trim;
	set.dmdsa.Flags = DEVICE_DSM_FLAG_TRIM_NOT_FS_ALLOCATED;
	set.dmdsa.ParameterBlockOffset = 0;
	set.dmdsa.ParameterBlockLength = 0;
	set.dmdsa.DataSetRangesOffset =
	    FIELD_OFFSET(struct setAttrAndRange, range);
	set.dmdsa.DataSetRangesLength = 1 * sizeof (DEVICE_DATA_SET_RANGE);

	set.range.LengthInBytes = size;
	set.range.StartingOffset = offset;

	Status = kernel_ioctl(dev, NULL,
	    IOCTL_STORAGE_MANAGE_DATA_SET_ATTRIBUTES,
	    &set, sizeof (set), NULL, 0);

	if (Status == 0)
		return (0); // TRIM OK

	// Linux returncodes are negative
	return (-Status);
}


int
VOP_SPACE(HANDLE h, int cmd, struct flock *fl, int flags, offset_t off,
    cred_t *cr, void *ctx)
{
	if (cmd == F_FREESP) {
		NTSTATUS Status;
		// DWORD ret;
		FILE_ZERO_DATA_INFORMATION fzdi;
		fzdi.FileOffset.QuadPart = fl->l_start;
		fzdi.BeyondFinalZero.QuadPart = fl->l_start + fl->l_len;

		Status = ZwFsControlFile(
		    h,
		    NULL,
		    NULL,
		    NULL,
		    NULL,
		    FSCTL_SET_ZERO_DATA,
		    &fzdi, sizeof (fzdi),
		    NULL,
		    0);

		return (Status);
	}

	return (STATUS_NOT_SUPPORTED);
}

int
VOP_CLOSE(struct vnode *vp, int flag, int count, offset_t off,
    void *cr, void *k)
{
	int error = 0;

	return (error);
}

int
VOP_FSYNC(struct vnode *vp, int flags, void* unused, void *uused2)
{
	int error = 0;

	return (error);
}

int
VOP_GETATTR(struct vnode *vp, vattr_t *vap, int flags, void *x3, void *x4)
{
	int error = 0;
	return (error);
}

#if 1
errno_t VNOP_LOOKUP(struct vnode *, struct vnode **, struct componentname *,
    vfs_context_t *);

errno_t
VOP_LOOKUP(struct vnode *vp, struct vnode **vpp, struct componentname *cn,
    vfs_context_t *ct)
{
	return (ENOTSUP);
}
#endif

#if 0
extern errno_t VNOP_REMOVE(struct vnode *, struct vnode *,
    struct componentname *, int, vfs_context_t);
errno_t
VOP_REMOVE(struct vnode *vp, struct vnode *dp,
    struct componentname *cn, int flags,
    vfs_context_t ct)
{
	return (VNOP_REMOVE(vp, dp, cn, flags, ct));
}


extern errno_t VNOP_SYMLINK(struct vnode *, struct vnode **,
    struct componentname *, struct vnode_attr *,
    char *, vfs_context_t);
errno_t
VOP_SYMLINK(struct vnode *vp, struct vnode **vpp,
    struct componentname *cn, struct vnode_attr *attr,
    char *name, vfs_context_t ct)
{
	return (VNOP_SYMLINK(vp, vpp, cn, attr, name, ct));
}
#endif

#undef VFS_ROOT

extern int VFS_ROOT(mount_t *, struct vnode **, vfs_context_t);
int
spl_vfs_root(mount_t *mount, struct vnode **vp)
{
	*vp = NULL;
	return (-1);
}

void
vfs_mountedfrom(struct mount *vfsp, char *osname)
{
}

/*
 * DNLC Name Cache Support
 */
struct vnode *
dnlc_lookup(struct vnode *dvp, char *name)
{
	struct componentname cn;
	struct vnode *vp = NULL;

	memset(&cn, 0, sizeof (cn));

	switch (0 /* cache_lookup(dvp, &vp, &cn) */) {
	case -1:
		break;
	case ENOENT:
		vp = DNLC_NO_VNODE;
		break;
	default:
		vp = NULL;
	}
	return (vp);
}

int
dnlc_purge_vfsp(struct mount *mp, int flags)
{
	struct vnode *rvp;
	IO_STATUS_BLOCK ioStatus;

	mutex_enter(&vnode_all_list_lock);
	for (rvp = list_head(&vnode_all_list);
	    rvp;
	    rvp = list_next(&vnode_all_list, rvp)) {

		if (rvp->v_mount != mp)
			continue;

		if (vnode_isdir(rvp))
			continue;

		CcFlushCache(&rvp->SectionObjectPointers, NULL, 0,
		    &ioStatus);
	}
	mutex_exit(&vnode_all_list_lock);

	return (0);
}

void
dnlc_remove(struct vnode *vp, char *name)
{
}

/*
 *
 *
 */
void
dnlc_update(struct vnode *vp, char *name, struct vnode *tp)
{

}

static int
vnode_fileobject_compare(const void *arg1, const void *arg2)
{
	const vnode_fileobjects_t *node1 = arg1;
	const vnode_fileobjects_t *node2 = arg2;
	if (node1->fileobject > node2->fileobject)
		return (1);
	if (node1->fileobject < node2->fileobject)
		return (-1);
	return (0);
}

static int
zfs_vnode_cache_constructor(void *buf, void *arg, int kmflags)
{
	vnode_t *vp = buf;

	// So the Windows structs have to be zerod, even though we call
	// their setup functions.
	memset(vp, 0, sizeof (*vp));

	mutex_init(&vp->v_mutex, NULL, MUTEX_DEFAULT, NULL);
	avl_create(&vp->v_fileobjects, vnode_fileobject_compare,
	    sizeof (vnode_fileobjects_t), offsetof(vnode_fileobjects_t,
	    avlnode));

	ExInitializeResourceLite(&vp->resource);
	ExInitializeResourceLite(&vp->pageio_resource);
	ExInitializeFastMutex(&vp->AdvancedFcbHeaderMutex);
	return (0);
}

static void
zfs_vnode_cache_destructor(void *buf, void *arg)
{
	vnode_t *vp = buf;

	ExDeleteResourceLite(&vp->pageio_resource);
	ExDeleteResourceLite(&vp->resource);

	avl_destroy(&vp->v_fileobjects);
	mutex_destroy(&vp->v_mutex);
}

int
spl_vnode_init(void)
{
	mutex_init(&spl_getf_lock, NULL, MUTEX_DEFAULT, NULL);
	list_create(&spl_getf_list, sizeof (struct spl_fileproc),
	    offsetof(struct spl_fileproc, f_next));
	mutex_init(&vnode_all_list_lock, NULL, MUTEX_DEFAULT, NULL);
	list_create(&vnode_all_list, sizeof (struct vnode),
	    offsetof(struct vnode, v_list));

	vnode_cache = kmem_cache_create("zfs_vnode_cache",
	    sizeof (vnode_t), 0,
	    zfs_vnode_cache_constructor,
	    zfs_vnode_cache_destructor, NULL, NULL,
	    NULL, 0);

	return (0);
}

void
spl_vnode_fini(void)
{
	// We need to free all delayed vnodes - this can easily go
	// wrong, still haven't figured out how to tell Windows
	// to let go for a FILEOBJECT.
	if (vnode_active > 0) {
		vnode_drain_delayclose(1);
		if (vnode_active > 0) {
			// vnode ages up to 5s. then, we loop all
			// still active nodes, mark them dead, and old
			// so they are immediately freed, as well as
			// go through the tree of fileobjects to free.

			delay(hz*5);
			// hardcoded age, see vnode_drain_delayclose

			dprintf("%s: forcing free (this can go wrong)n",
			    __func__);
			struct vnode *rvp;
			clock_t then = gethrtime() - SEC2NSEC(6); // hardcoded

			mutex_enter(&vnode_all_list_lock);
			for (rvp = list_head(&vnode_all_list);
			    rvp;
			    rvp = list_next(&vnode_all_list, rvp)) {
				vnode_fileobjects_t *node;

				dprintf("%p marked DEAD3\n", rvp);

				rvp->v_flags |= VNODE_DEAD|VNODE_FLUSHING;
				rvp->v_age = then;

				mutex_enter(&rvp->v_mutex);
				while (node = avl_first(&rvp->v_fileobjects)) {
					avl_remove(&rvp->v_fileobjects, node);
					kmem_free(node, sizeof (*node));
				}
				mutex_exit(&rvp->v_mutex);
			}
			mutex_exit(&vnode_all_list_lock);
		}
	}

	// age all marked "old", so here's hopin'
	vnode_drain_delayclose(1);

	ASSERT(list_empty(&vnode_all_list));

	mutex_destroy(&vnode_all_list_lock);
	list_destroy(&vnode_all_list);
	mutex_destroy(&spl_getf_lock);
	list_destroy(&spl_getf_list);

	if (vnode_cache)
		kmem_cache_destroy(vnode_cache);
	vnode_cache = NULL;
}

#include <sys/file.h>

void
cache_purgevfs(mount_t *mp)
{
}

dev_t
vnode_specrdev(vnode_t *vp)
{
	return (0);
}

void
cache_purge(vnode_t *vp)
{
}

void
cache_purge_negatives(vnode_t *vp)
{
}

int
vnode_removefsref(vnode_t *vp)
{
	return (0);
}

/*
 * getf(int fd) - hold a lock on a file descriptor, to be released by calling
 * releasef(). On OSX we will also look up the vnode of the fd for calls
 * to spl_vn_rdwr().
 */
void *
getf(uint64_t fd)
{
	struct spl_fileproc *sfp = NULL;
	// HANDLE h;

#if 1
	struct fileproc *fp  = NULL;
	// struct vnode *vp;
	// uint64_t vid;

	/*
	 * We keep the "fp" pointer as well, both for unlocking in releasef()
	 * and used in vn_rdwr().
	 */

	sfp = kmem_alloc(sizeof (*sfp), KM_SLEEP);
	if (!sfp)
		return (NULL);

	/*
	 * The f_vnode ptr is used to point back to the "sfp" node itself,
	 * as it is the only information passed to vn_rdwr.
	 */
	if (ObReferenceObjectByHandle((HANDLE)fd, 0, 0, KernelMode,
	    (void **)&fp, 0) != STATUS_SUCCESS) {
		dprintf("%s: failed to get fd %d fp 0x\n", __func__, fd);
	}

	sfp->f_vnode	= sfp;

	sfp->f_handle	= (HANDLE) fd;
	sfp->f_offset	= 0;
	sfp->f_proc	= current_proc();
	sfp->f_fp	= (void *)fp;
	sfp->f_file	= (uint64_t)fp;

	mutex_enter(&spl_getf_lock);
	list_insert_tail(&spl_getf_list, sfp);
	mutex_exit(&spl_getf_lock);

	// printf("SPL: new getf(%d) ret %p fp is %p so vnode set to %p\n",
	//  fd, sfp, fp, sfp->f_vnode);
#endif
	return (sfp);
}

struct vnode *
getf_vnode(void *fp)
{
	struct vnode *vp = NULL;
#if 0
	struct spl_fileproc *sfp = (struct spl_fileproc *)fp;
	uint32_t vid;

	if (!file_vnode_withvid(sfp->f_fd, &vp, &vid)) {
		file_drop(sfp->f_fd);
	}
#endif
	return (vp);
}

void
releasefp(struct spl_fileproc *fp)
{
	if (fp->f_fp)
		ObDereferenceObject(fp->f_fp);

	/* Remove node from the list */
	mutex_enter(&spl_getf_lock);
	list_remove(&spl_getf_list, fp);
	mutex_exit(&spl_getf_lock);

	/* Free the node */
	kmem_free(fp, sizeof (*fp));
}
void
releasef(uint64_t fd)
{

#if 1
	struct spl_fileproc *fp = NULL;
	struct proc *p = NULL;

	// printf("SPL: releasef(%d)\n", fd);

	p = (void *)current_proc();
	mutex_enter(&spl_getf_lock);
	for (fp = list_head(&spl_getf_list); fp != NULL;
	    fp = list_next(&spl_getf_list, fp)) {
		if ((fp->f_proc == p) && fp->f_fd == fd)
			break;
	}
	mutex_exit(&spl_getf_lock);
	if (!fp)
		return; // Not found

	// printf("SPL: releasing %p\n", fp);
	releasefp(fp);

#endif
}

/*
 * Our version of vn_rdwr, here "vp" is not actually a vnode, but a ptr
 * to the node allocated in getf(). We use the "fp" part of the node to
 * be able to issue IO.
 * You must call getf() before calling spl_vn_rdwr().
 */
int
spl_vn_rdwr(enum zfs_uio_rw rw,
    struct vnode *vp,
    caddr_t base,
    ssize_t len,
    offset_t offset,
    enum zfs_uio_seg seg,
    int ioflag,
    rlim64_t ulimit,    /* meaningful only if rw is UIO_WRITE */
    cred_t *cr,
    ssize_t *residp)
{
	struct spl_fileproc *sfp = (struct spl_fileproc *)vp;
	int error = 0;
	IO_STATUS_BLOCK iob;
	LARGE_INTEGER off;

	off.QuadPart = offset;

	if (rw == UIO_READ) {
		error = ZwReadFile((HANDLE)sfp->f_fd, NULL, NULL, NULL, &iob,
		    base, (ULONG)len, &off, NULL);
	} else {
		error = ZwWriteFile((HANDLE)sfp->f_fd, NULL, NULL, NULL, &iob,
		    base, (ULONG)len, &off, NULL);
		sfp->f_writes = 1;
	}

	if (residp) {
		*residp = len - iob.Information;
	} else {
		if ((iob.Information < len) && error == 0)
			error = EIO;
	}

	return (error);
}

void
spl_rele_async(void *arg)
{
	struct vnode *vp = (struct vnode *)arg;
#ifdef DEBUG_IOCOUNT
	if (vp) {
		znode_t *zp = VTOZ(vp);
		if (zp)
			dprintf("%s: Dec iocount from %u for '%s' \n", __func__,
			    &vp->v_iocount,
			    zp->z_name_cache);
	}
#endif
	if (vp) VN_RELE(vp);
}

void
vn_rele_async(struct vnode *vp, void *taskq)
{
#ifdef DEBUG_IOCOUNT
	if (vp) {
		znode_t *zp = VTOZ(vp);
		if (zp)
			dprintf("%s: Dec iocount in future, now %u for '%s' \n",
			    __func__, vp->v_iocount, zp->z_name_cache);
	}
#endif
	VERIFY(taskq_dispatch((taskq_t *)taskq,
	    (task_func_t *)spl_rele_async, vp, TQ_SLEEP) != 0);
}

vfs_context_t *
spl_vfs_context_kernel(void)
{
	return (NULL);
}

extern int build_path(struct vnode *vp, char *buff, int buflen, int *outlen,
    int flags, vfs_context_t *ctx);

int
spl_build_path(struct vnode *vp, char *buff, int buflen, int *outlen,
    int flags, vfs_context_t *ctx)
{
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
	return (0);
}

extern int vfs_get_notify_attributes(struct vnode_attr *vap);
int
spl_vfs_get_notify_attributes(struct vnode_attr *vap)
{
	return (0);
}

/* Root directory vnode for the system a.k.a. '/' */
/*
 * Must use vfs_rootvnode() to acquire a reference, and
 * vnode_put() to release it
 */

/*
 * From early boot (mountroot) we can not call vfs_rootvnode()
 * or it will panic. So the default here is to return NULL until
 * root has been mounted. XNU will call vfs_root() once that is
 * done, so we use that to inform us that root is mounted. In nonboot,
 * vfs_start is called early from kextload (zfs_osx.cpp).
 */
static int spl_skip_getrootdir = 1;

struct vnode *
getrootdir(void)
{
	struct vnode *rvnode = NULL;
	if (spl_skip_getrootdir)
		return (NULL);

	return (rvnode);
}

void
spl_vfs_start()
{
	spl_skip_getrootdir = 0;
}

int
vnode_vfsisrdonly(vnode_t *vp)
{
	return (0);
}

uint64_t
vnode_vid(vnode_t *vp)
{
	return (vp->v_id);
}

int
vnode_isreg(vnode_t *vp)
{
	return (vp->v_type == VREG);
}

int
vnode_isdir(vnode_t *vp)
{
	return (vp->v_type == VDIR);
}

void *
vnode_fsnode(struct vnode *dvp)
{
	return (dvp->v_data);
}

enum vtype
vnode_vtype(vnode_t *vp)
{
	return (vp->v_type);
}

int
vnode_isblk(vnode_t *vp)
{
	return (vp->v_type == VBLK);
}

int
vnode_ischr(vnode_t *vp)
{
	return (vp->v_type == VCHR);
}

int
vnode_isswap(vnode_t *vp)
{
	return (vp->v_type == VFIFO);
}

int
vnode_isfifo(vnode_t *vp)
{
	return (0);
}

int
vnode_islnk(vnode_t *vp)
{
	return (vp->v_type == VLNK);
}

mount_t *
vnode_mountedhere(vnode_t *vp)
{
	return (NULL);
}

void
ubc_setsize(struct vnode *vp, uint64_t size)
{
}

int
vnode_isinuse(vnode_t *vp, uint64_t refcnt)
{
	// xnu uses usecount +kusecount, not iocount
	if (((vp->v_usecount /* + vp->v_iocount */) > refcnt))
		return (1);
	return (0);
}

int
vnode_isidle(vnode_t *vp)
{
	if ((vp->v_usecount == 0) && (vp->v_iocount <= 1))
		return (1);
	return (0);
}

int
vnode_iocount(vnode_t *vp)
{
	return (vp->v_iocount);
}

vnode_t *
vnode_parent(vnode_t *vp)
{
	return (vp->v_parent);
}

/*
 * Update a vnode's parent, this is typically not done
 * by the FS, except after rename operation when there
 * might be a new parent.
 * We do not expect newparent to be NULL here, as you
 * can not become root. If we need that, we should
 * implement pivot_root()
 */
void
vnode_setparent(vnode_t *vp, vnode_t *newparent)
{
	int error;
	struct vnode *oldparent;

	oldparent = vp->v_parent;
	if (oldparent == newparent)
		return;

	vp->v_parent = NULL;

	if (newparent) {
		/*
		 * If newparent is concurrently being reclaimed (racing
		 * vnode_prune_idle() or a forced unmount) vnode_ref() will
		 * refuse the hold; leave v_parent NULL rather than pointing
		 * it at a vnode we do not hold a reference on.
		 */
		if (vnode_ref(newparent) == 0)
			vp->v_parent = newparent;
	}

	// Try holding it, so we call vnode_put()
	if (oldparent != NULL) {
		error = VN_HOLD(oldparent);
		vnode_rele(oldparent);
		if (!error)
			VN_RELE(oldparent);
	}
}

#ifdef DEBUG_IOCOUNT
int
vnode_getwithref(vnode_t *vp, char *file, int line)
#else
int
vnode_getwithref(vnode_t *vp)
#endif
{
	// KIRQL OldIrql;
	int error = 0;
#ifdef FIND_MAF
	ASSERT(!(vp->v_flags & 0x8000));
#endif

	mutex_enter(&vp->v_mutex);
	if ((vp->v_flags & (VNODE_DEAD | VNODE_MARKTERM))) {
		/*
		 * Once VNODE_MARKTERM is set, vnode_recycle_int() will
		 * call zfs_vnop_reclaim() after dropping v_mutex, and
		 * VNODE_DEAD is not set until after that returns. A new
		 * iocount handed out during that window would let a
		 * caller keep using a znode that is concurrently being
		 * torn down. Match xnu's vnode_get_locked(), which refuses
		 * on VL_TERMINATE as well as VL_DEAD.
		 */
		error = ENOENT;
//	} else if (vnode_deleted(vp)) {
//		error = ENOENT;
	} else {
#ifdef DEBUG_IOCOUNT
		if (vp) {
			znode_t *zp = VTOZ(vp);
			if (zp)
				dprintf("%s: Inc iocount now %u for '%s' "
				    "(%s:%d) thread %p \n", __func__,
				    atomic_inc_32_nv(&vp->v_iocount),
				    zp->z_name_cache,
				    file, line, current_thread());
		}
#else
		atomic_inc_32(&vp->v_iocount);
#endif
	}

	mutex_exit(&vp->v_mutex);
	return (error);
}

#ifdef DEBUG_IOCOUNT
int
vnode_debug_getwithvid(vnode_t *vp, uint64_t id, char *file, int line)
#else
int
vnode_getwithvid(vnode_t *vp, uint64_t id)
#endif
{
	// KIRQL OldIrql;
	int error = 0;

#ifdef FIND_MAF
	ASSERT(!(vp->v_flags & 0x8000));
#endif

	mutex_enter(&vp->v_mutex);
	if ((vp->v_flags & VNODE_DEAD)) {
		error = ENOENT;
	} else if (id != vp->v_id) {
		error = ENOENT;
//	} else if (vnode_deleted(vp)) {
//		error = ENOENT;
	} else {
#ifdef DEBUG_IOCOUNT
		if (vp) {
			znode_t *zp = VTOZ(vp);
			if (zp)
				dprintf("%s: Inc iocount now %u for '%s' "
				    "(%s:%d) thread %p\n", __func__,
				    atomic_inc_32_nv(&vp->v_iocount),
				    zp->z_name_cache, file, line,
				    current_thread());
		}
#else
		atomic_inc_32(&vp->v_iocount);
#endif
	}

	mutex_exit(&vp->v_mutex);
	return (error);
}

extern void zfs_inactive(vnode_t *vp, cred_t *cr, caller_context_t *ct);

#ifdef DEBUG_IOCOUNT
int
vnode_put(vnode_t *vp, char *file, int line)
#else
int
vnode_put(vnode_t *vp)
#endif
{
	// ASSERT(!(vp->v_flags & VNODE_DEAD));
	ASSERT(vp->v_iocount > 0);
	ASSERT((vp->v_flags & ~VNODE_VALIDBITS) == 0);

	// Now idle?
	mutex_enter(&vp->v_mutex);

#ifdef DEBUG_IOCOUNT
	if (vp) {
		znode_t *zp = VTOZ(vp);
		if (zp)
			dprintf("%s: Dec iocount now %u for '%s' (%s:%d) "
			    "thread %p \n", __func__,
			    atomic_dec_32_nv(&vp->v_iocount),
			    zp->z_name_cache, file, line, current_thread());
	}
#else
	atomic_dec_32(&vp->v_iocount);
#endif

	if ((vp->v_usecount == 0) && (vp->v_iocount == 0)) {
		// XNU always calls inactive in vnode_put
		vp->v_flags &= ~VNODE_NEEDINACTIVE;
		mutex_exit(&vp->v_mutex);
		zfs_inactive(vp, NULL, NULL);
		mutex_enter(&vp->v_mutex);
	}

	vp->v_flags &= ~VNODE_NEEDINACTIVE;

#if 1
	// Re-test for idle, as we may have dropped lock for inactive
	if ((vp->v_usecount == 0) && (vp->v_iocount == 0)) {
		// Was it marked TERM, but we were waiting for last ref
		if ((vp->v_flags & (VNODE_MARKTERM | VNODE_DEAD)) ==
		    VNODE_MARKTERM) {
			vnode_recycle_int(vp, VNODELOCKED);
			return (0);
		}
	}
#endif
	mutex_exit(&vp->v_mutex);

	return (0);
}

int
vnode_recycle_int(vnode_t *vp, int flags)
{
	// KIRQL OldIrql;
	// ASSERT((vp->v_flags & VNODE_DEAD) == 0);

	// Already locked calling in...
	if (!(flags & VNODELOCKED)) {
		mutex_enter(&vp->v_mutex);
	}

	// Mark it for recycle, if we are not ROOT.
	if (!(vp->v_flags&VNODE_MARKROOT)) {

		if (vp->v_flags & VNODE_MARKTERM) {
			dprintf("already marked\n");
		} else {
			vp->v_flags |= VNODE_MARKTERM; // Mark it terminating
			dprintf("%s: marking %p VNODE_MARKTERM\n",
			    __func__, vp);

			// Call inactive?
			mutex_exit(&vp->v_mutex);
			if (vp->v_flags & VNODE_NEEDINACTIVE) {
				vp->v_flags &= ~VNODE_NEEDINACTIVE;
				zfs_inactive(vp, NULL, NULL);
				VERIFY3U(vp->v_iocount, ==, 1);
			}

			// Call sync? If vnode_write
			// zfs_fsync(vp, 0, NULL, NULL);

			mutex_enter(&vp->v_mutex);
		}
	}

	// Doublecheck CcMgr is gone (should be if avl is empty)
	// If it hasn't quite let go yet, let the node linger on deadlist.
#if 1
	if (vp->SectionObjectPointers.DataSectionObject != NULL ||
	    vp->SectionObjectPointers.ImageSectionObject != NULL ||
	    vp->SectionObjectPointers.SharedCacheMap != NULL) {
		dprintf("%s: %p still has CcMgr, lingering on dead list.\n",
		    __func__, vp);
		mutex_exit(&vp->v_mutex);
		return (-1);
	}
#endif

	/*
	 * CcMgr is fully gone.  Only now is it safe to free the ZFS layer.
	 *
	 * Calling zfs_vnop_reclaim before the SharedCacheMap check sets
	 * vp->v_data = NULL while dirty pages still exist.
	 * AcquireForLazyWrite checks VTOZ(vp): if NULL it returns FALSE,
	 * so dirty pages can never be flushed and SharedCacheMap is never
	 * torn down — a permanent livelock.
	 * zfs_inactive above does not touch v_data or z_sa_hdl in this port,
	 * so deferring the reclaim here is safe.
	 */
	if (vp->v_data != NULL) {
		mutex_exit(&vp->v_mutex);
		if (zfs_vnop_reclaim(vp))
			panic("vnode_recycle: cannot reclaim\n");
		mutex_enter(&vp->v_mutex);
	}

	// We will only reclaim idle nodes, and not mountpoints(ROOT)
	// lets try letting zfs reclaim, then linger nodes.
	if ((flags & FORCECLOSE) ||
	    ((vp->v_usecount == 0) &&
	    (vp->v_iocount == 0) &&
	    /* avl_is_empty(&vp->v_fileobjects) && */
	    ((vp->v_flags&VNODE_MARKROOT) == 0))) {

		ASSERT3P(vp->SectionObjectPointers.DataSectionObject, ==, NULL);
		ASSERT3P(vp->SectionObjectPointers.ImageSectionObject, ==,
		    NULL);
		ASSERT3P(vp->SectionObjectPointers.SharedCacheMap, ==, NULL);

		vp->v_flags |= VNODE_DEAD; // Mark it dead
// Since we might get swapped out (noticably FsRtlTeardownPerStreamContexts)
// we hold a look until the very end.
		dprintf("%p marked DEAD\n", vp);
		atomic_inc_32(&vp->v_iocount);

		mutex_exit(&vp->v_mutex);

		FsRtlTeardownPerStreamContexts(&vp->FileHeader);
		FsRtlUninitializeFileLock(&vp->lock);

		// KIRQL OldIrql;
		mutex_enter(&vp->v_mutex);

		if (avl_numnodes(&vp->v_fileobjects) > 0)
			dprintf("Dropping %d references\n",
			    avl_numnodes(&vp->v_fileobjects));
		vnode_fileobjects_t *node;
		while (node = avl_first(&vp->v_fileobjects)) {
			avl_remove(&vp->v_fileobjects, node);
			kmem_free(node, sizeof (*node));
		}
		ASSERT(avl_is_empty(&vp->v_fileobjects));
		// We are all done with it.
		atomic_dec_32(&vp->v_iocount);
		mutex_exit(&vp->v_mutex);

#ifdef FIND_MAF
		vp->v_flags |= 0x8000;
#endif

/*
 * Windows has a habit of copying FsContext (vp) without our knowledge and
 * attempt to call fsDispatcher. We notice in vnode_getwithref(), which
 * calls mutex_enter so we can not free the vp right here like we want to,
 * or that would be a MAF.
 * So we let it linger and age, there is no great way to know for sure that it
 * has finished trying.
 */
		dprintf("vp %p left on DEAD list\n", vp);
		vp->v_age = gethrtime();

		return (0);
	}

	mutex_exit(&vp->v_mutex);
	return (-1);
}


int
vnode_recycle(vnode_t *vp)
{
	if (vp->v_flags & VNODE_FLUSHING)
		return (-1);
	if (vp->v_flags & VNODE_DEAD)
		return (0);
	return (vnode_recycle_int(vp, 0));
}

typedef struct {
	FSRTL_COMMON_FCB_HEADER Header;
	PFAST_MUTEX FastMutex;
	LIST_ENTRY FilterContexts;
	EX_PUSH_LOCK PushLock;
	PVOID *FileContextSupportPointer;
	union {
		OPLOCK Oplock;
		PVOID ReservedForRemote;
	};
	PVOID ReservedContext;
} FSRTL_ADVANCED_FCB_HEADER_NEW;

POPLOCK
vp_oplock(struct vnode *vp)
{
	// The oplock in header starts with Win8
	if (vp->FileHeader.Version >= FSRTL_FCB_HEADER_V2)
		return (&((FSRTL_ADVANCED_FCB_HEADER_NEW *)&vp->
		    FileHeader)->Oplock);
	else
		return (&vp->oplock);
}

void
vnode_create(mount_t *mp, struct vnode *dvp, void *v_data, int type, int flags,
    struct vnode **vpp)
{
	struct vnode *vp;
	// cache_alloc does not zero the struct, we need to
	// make sure that those things that need clearing is
	// done here.
	vp = kmem_cache_alloc(vnode_cache, KM_SLEEP);
	*vpp = vp;
	vp->v_flags = 0;
	vp->v_mount = mp;
	vp->v_parent = NULL;
	vp->v_data = v_data;
	vp->v_type = type;
	vp->v_id = atomic_inc_64_nv(&(vnode_vid_counter));
	vp->v_iocount = 1;
	vp->v_usecount = 0;
	vp->v_unlink = 0;
	vp->v_reparse = NULL;
	vp->v_reparse_size = 0;
	vp->security_descriptor = NULL;

	atomic_inc_64(&vnode_active);

	list_link_init(&vp->v_list);
	ASSERT(vnode_fileobject_empty(vp, 1)); // lying about locked is ok.

	if (flags & VNODE_MARKROOT)
		vp->v_flags |= VNODE_MARKROOT;

	// Initialise the Windows specific data.
	memset(&vp->SectionObjectPointers, 0,
	    sizeof (vp->SectionObjectPointers));

	FsRtlSetupAdvancedHeader(&vp->FileHeader, &vp->AdvancedFcbHeaderMutex);

	FsRtlInitializeFileLock(&vp->lock, NULL, NULL);
	FsRtlInitializeOplock(vp_oplock(vp));

	vp->FileHeader.Resource = &vp->resource;
	vp->FileHeader.PagingIoResource = &vp->pageio_resource;

	// Add only to list once we have finished initialising.
	mutex_enter(&vnode_all_list_lock);
	list_insert_tail(&vnode_all_list, vp);
	mutex_exit(&vnode_all_list_lock);
}

int
vnode_isvroot(vnode_t *vp)
{
	return (vp->v_flags & VNODE_MARKROOT);
}

mount_t *
vnode_mount(vnode_t *vp)
{
	return (vp->v_mount);
}

void
vnode_clearfsnode(vnode_t *vp)
{
	vp->v_data = NULL;
}

void *
vnode_sectionpointer(vnode_t *vp)
{
	return (&vp->SectionObjectPointers);
}

int
vnode_ref(vnode_t *vp)
{
	ASSERT(vp->v_iocount > 0);

	/*
	 * Mirror the VN_HOLD/vnode_getwithref check: refuse to hand out
	 * a new long-term (usecount) reference once VNODE_MARKTERM is
	 * set, not just VNODE_DEAD, matching xnu's vnode_ref_ext() which
	 * checks VL_TERMINATE in addition to VL_DEAD. Callers that
	 * already hold a fresh iocount (the common case) can't race this,
	 * since a vnode can not be marked TERM while its iocount is
	 * nonzero except via a forced unmount.
	 */
	if (vp->v_flags & (VNODE_DEAD | VNODE_MARKTERM))
		return (ENOENT);

	atomic_inc_32(&vp->v_usecount);
	return (0);
}

void
vnode_rele(vnode_t *vp)
{
	// KIRQL OldIrql;

	// ASSERT(!(vp->v_flags & VNODE_DEAD));
	ASSERT(vp->v_iocount > 0);
	ASSERT(vp->v_usecount > 0);
	atomic_dec_32(&vp->v_usecount);

	// Grab lock and inspect
	mutex_enter(&vp->v_mutex);

	// If we were the last usecount, but vp is still
	// busy, we set NEEDINACTIVE
	if (vp->v_usecount > 0 || vp->v_iocount > 0) {
		vp->v_flags |= VNODE_NEEDINACTIVE;
	} else {
		// We are idle, call inactive, grab a hold
		// so we can call inactive unlocked
		// ASSERT0(vp->v_flags & VNODE_DEAD);
		vp->v_flags &= ~VNODE_NEEDINACTIVE;
		atomic_inc_32(&vp->v_iocount);
		mutex_exit(&vp->v_mutex);

		zfs_inactive(vp, NULL, NULL);
#ifdef DEBUG_VERBOSE
		if (vp) {
			znode_t *zp = VTOZ(vp);
			if (zp)
				dprintf("%s: Inc iocount to %u for %s \n",
				    __func__, vp->v_iocount, zp->z_name_cache);
		}
#endif
		// Re-check we are still free, and recycle (markterm) was called
		// we can reclaim now
		mutex_enter(&vp->v_mutex);
		atomic_dec_32(&vp->v_iocount);
		if ((vp->v_iocount == 0) && (vp->v_usecount == 0) &&
		    ((vp->v_flags & (VNODE_MARKTERM)))) {
			mutex_exit(&vp->v_mutex);
			vnode_recycle_int(vp, 0);
			return;
		}
	}

	mutex_exit(&vp->v_mutex);
}

/*
 * Periodically walk through list and release vnodes that are now idle.
 * Set force=1 to perform check now.
 * Will return number of vnodes with delete set, but not yet reclaimed.
 */
int
vnode_drain_delayclose(int force)
{
	struct vnode *vp, *next = NULL;
	int ret = 0;
	int candidate = 0;
	static hrtime_t last = 0;
	const hrtime_t interval = SEC2NSEC(2);
	const hrtime_t curtime = gethrtime();

	mutex_enter(&vnode_all_list_lock);
	// This should probably be its own thread, but for now, run every 2s
	if (!force && curtime - last < interval) {
		mutex_exit(&vnode_all_list_lock);
		return (0);
	}
	last = curtime;

	TraceEvent(8, "%s: scanning\n", __func__);

	for (vp = list_head(&vnode_all_list);
	    vp;
	    vp = next) {

		next = list_next(&vnode_all_list, vp);

		// Make sure everything about the vp has been released.
		vnode_lock(vp);

		// If we see a deleted node awaiting recycle, signal return code
		if ((vp->v_flags & VNODE_MARKTERM))
			candidate = 1;
		else
			candidate = 0;

		if ((vp->v_flags & VNODE_MARKTERM) &&
		    !(vp->v_flags & VNODE_DEAD) &&
		    (vp->v_iocount == 0) &&
		    (vp->v_usecount == 0) &&
		    vnode_fileobject_empty(vp, /* locked */ 1) &&
		    !vnode_isvroot(vp) &&
		    (vp->SectionObjectPointers.ImageSectionObject == NULL) &&
		    (vp->SectionObjectPointers.DataSectionObject == NULL)) {
			// We are ready to let go
			dprintf("%s: drain %p\n", __func__, vp);

// Pass VNODELOCKED as we hold vp, recycle will unlock.
// We have to give up all_list due to
// recycle -> reclaim -> rmnode -> purgedir -> zget -> vnode_create
			mutex_exit(&vnode_all_list_lock);
			if (vnode_recycle_int(vp, VNODELOCKED) == 0)
				candidate = 0;
// If recycle was ok, this isnt a node we wait for

			mutex_enter(&vnode_all_list_lock);

			// If successful, vp is freed. Do not use vp from here:

		} else if ((vp->v_flags & VNODE_DEAD) &&
		    (vp->v_age != 0) &&
		    (curtime - vp->v_age > SEC2NSEC(5))) {
// Arbitrary time! fixme? It would be nice to know when Windows really
// wont try this vp again. fastfat seems to clear up the cache of the
// parent directory, perhaps this is the missing bit. It is non-trivial
// to get parent from here though.

			// dprintf("age is %llu %d\n", (curtime - vp->v_age),
			// NSEC2SEC(curtime - vp->v_age));
			dprintf("Dropping %d references 2",
			    avl_numnodes(&vp->v_fileobjects));
			vnode_fileobjects_t *node;
			while (node = avl_first(&vp->v_fileobjects)) {
				avl_remove(&vp->v_fileobjects, node);
				kmem_free(node, sizeof (*node));
			}

			// Finally free vp.
			list_remove(&vnode_all_list, vp);
			vnode_unlock(vp);
			dprintf("%s: freeing DEAD vp %p\n", __func__, vp);

			void *sd = vnode_security(vp);
			if (sd != NULL)
				ExFreePool(sd);
			vnode_setsecurity(vp, NULL);
			vnode_set_reparse(vp, NULL, 0);

			kmem_cache_free(vnode_cache, vp);
			atomic_dec_64(&vnode_active);

		} else {
			vnode_unlock(vp);
		}

		if (candidate) ret++;
	}
	mutex_exit(&vnode_all_list_lock);

	return (ret);
}

/*
 * Reclaim idle, ordinary (not necessarily deleted) vnodes, up to
 * nr_to_scan of them.  This is the Windows backend for the arc_prune
 * callback (see zfs_prune_task() in zfs_vfsops.c): ARC calls it when it
 * wants metadata room back and needs the VFS layer to drop its own holds
 * on idle vnodes so their znode/dnode/dbufs can actually be freed --
 * mirroring FreeBSD's vnlru_free_vfsops() and Linux's zpl_prune_sb(), the
 * standard cross-platform mechanism for this (see arc_add_prune_callback()
 * callers on those platforms).
 *
 * Without this, a vnode created for any file ever opened stays resident
 * for the life of the mount: vnode_drain_delayclose() below only reclaims
 * vnodes already flagged VNODE_MARKTERM (i.e. deleted files awaiting
 * removal), never ordinary idle ones.  Confirmed via kstat: zfs_vnode_
 * cache/zfs_znode_cache/dnode_t/dmu_buf_impl_t/sa_cache all showed
 * buf_inuse == buf_total and slab_free == 0 after filling a small pool --
 * nothing was ever being freed back, regardless of memory pressure,
 * because nothing ever asked the VFS layer to let go of idle vnodes.
 *
 * Same eligibility check as vnode_drain_delayclose()'s recycle branch,
 * minus the VNODE_MARKTERM requirement.
 */
uint64_t
vnode_prune_idle(uint64_t nr_to_scan)
{
	struct vnode *vp, *next = NULL;
	uint64_t reclaimed = 0;

	mutex_enter(&vnode_all_list_lock);

	for (vp = list_head(&vnode_all_list);
	    vp != NULL && reclaimed < nr_to_scan;
	    vp = next) {

		next = list_next(&vnode_all_list, vp);

		vnode_lock(vp);

		if (!(vp->v_flags & VNODE_DEAD) &&
		    !(vp->v_flags & VNODE_MARKTERM) &&
		    (vp->v_iocount == 0) &&
		    (vp->v_usecount == 0) &&
		    vnode_fileobject_empty(vp, /* locked */ 1) &&
		    !vnode_isvroot(vp) &&
		    (vp->SectionObjectPointers.ImageSectionObject == NULL) &&
		    (vp->SectionObjectPointers.DataSectionObject == NULL)) {

			dprintf("%s: pruning idle %p\n", __func__, vp);

			/*
			 * Pass VNODELOCKED as we hold vp, recycle will
			 * unlock.  Give up all_list first: recycle -> reclaim
			 * -> rmnode -> purgedir -> zget -> vnode_create can
			 * re-enter this list.
			 */
			mutex_exit(&vnode_all_list_lock);
			if (vnode_recycle_int(vp, VNODELOCKED) == 0)
				reclaimed++;
			mutex_enter(&vnode_all_list_lock);
		} else {
			vnode_unlock(vp);
		}
	}

	mutex_exit(&vnode_all_list_lock);

	return (reclaimed);
}

int
mount_count_nodes(struct mount *mp, int flags)
{
	int count = 0;
	struct vnode *rvp;

	mutex_enter(&vnode_all_list_lock);
	for (rvp = list_head(&vnode_all_list);
	    rvp;
	    rvp = list_next(&vnode_all_list, rvp)) {
		if (rvp->v_mount != mp)
			continue;
		if ((flags&SKIPROOT) && vnode_isvroot(rvp))
			continue;
		// if (rvp->v_flags&VNODE_DEAD)
		// 	continue;
		// if (rvp->has_uninit)
		//	continue;
		count++;
	}
	mutex_exit(&vnode_all_list_lock);
	return (count);
}

static void
flush_file_objects(struct vnode *rvp)
{
	// Release the AVL tree
	// Attempt to flush out any caches;

	FILE_OBJECT *fileobject;
	vnode_fileobjects_t *node;
	int Status;

	// Make sure we don't call vnode_flushcache() again from IRP_MJ_CLOSE.
	rvp->v_flags |= VNODE_FLUSHING;

	if (avl_is_empty(&rvp->v_fileobjects))
		return;

	for (node = avl_first(&rvp->v_fileobjects); node != NULL;
	    node = AVL_NEXT(&rvp->v_fileobjects, node)) {
		fileobject = node->fileobject;

		// Because the CC* calls can re-enter ZFS, we need to
		// release the lock, and because we release the lock the
		// while has to start from the top each time. We release
		// the node at end of this while.

		try {
			Status = ObReferenceObjectByPointer(fileobject, 0,
			    *IoFileObjectType, KernelMode);
		} except(EXCEPTION_EXECUTE_HANDLER) {
			Status = GetExceptionCode();
		}

		// Try to lock fileobject before we use it.
		if (NT_SUCCESS(Status)) {
			// Let go of mutex, as flushcache will re-enter
			// (IRP_MJ_CLEANUP)
			mutex_exit(&rvp->v_mutex);
			node->remove = vnode_flushcache(rvp, fileobject, TRUE);
			ObDereferenceObject(fileobject);
			mutex_enter(&rvp->v_mutex);
		} // if ObReferenceObjectByPointer
	} // for

	// Remove any nodes we successfully closed.
restart_remove_closed:
	for (node = avl_first(&rvp->v_fileobjects); node != NULL;
	    node = AVL_NEXT(&rvp->v_fileobjects, node)) {
		if (node->remove) {
			avl_remove(&rvp->v_fileobjects, node);
			kmem_free(node, sizeof (*node));
			goto restart_remove_closed;
		}
	}

	dprintf("vp %p has %d fileobject(s) remaining\n", rvp,
	    avl_numnodes(&rvp->v_fileobjects));
}

static void
print_reclaim_stats(boolean_t init, int reclaims)
{
	static int last_reclaims = 0;
	int reclaims_delta;
	int reclaims_per_second;
	static hrtime_t last_stats_time = 0;
	hrtime_t last_stats_time_delta;

	if (init) {
		last_stats_time = gethrtime();
		return;
	}

	if ((reclaims % 1000) != 0) {
		return;
	}

	reclaims_delta = reclaims - last_reclaims;
	last_stats_time_delta = gethrtime() - last_stats_time;

	reclaims_per_second = (((int64_t)reclaims_delta) * NANOSEC) /
	    MAX(last_stats_time_delta, 1);

	dprintf("%s: %d reclaims processed (%d/s).\n", __func__, reclaims,
	    reclaims_per_second);

	last_reclaims = reclaims;
	last_stats_time = gethrtime();
}


/*
 * Let's try something new. If we are to vflush, lets do everything we can
 * then release the znode struct, and leave vnode with a NULL ptr, marked
 * dead. Future access to vnode will be refused. Move the vnode from
 * the mount's list, onto a deadlist. Only stop module unload
 * until deadlist is empty.
 */

/*
 * Force-flush and purge the Windows Cache Manager for any vnodes on mount
 * mp that are stuck in MARKTERM (pending reclaim) with a live SharedCacheMap.
 *
 * Called during unmount, BEFORE zfsvfs_teardown() sets z_unmounted = TRUE.
 * After that point AcquireForLazyWrite returns FALSE, so dirty pages can
 * never be flushed and SharedCacheMap is never torn down — permanent hang.
 *
 * We must NOT hold vnode_all_list_lock while calling CcFlushCache /
 * CcPurgeCacheSection (they may re-enter vnode creation paths).
 * Instead: scan under the lock to find one candidate, take a brief
 * iocount reference, drop the lock, flush+purge, then repeat.
 */
void
vnode_purge_ccmgr_vnodes(struct mount *mp)
{
	struct vnode *vp;
	boolean_t found;
	int max_passes = 200;

	do {
		found = B_FALSE;
		mutex_enter(&vnode_all_list_lock);
		for (vp = list_head(&vnode_all_list); vp;
		    vp = list_next(&vnode_all_list, vp)) {
			if (mp != NULL && vp->v_mount != mp)
				continue;
			if (!(vp->v_flags & VNODE_MARKTERM))
				continue;
			if (vp->v_flags & VNODE_DEAD)
				continue;
			PSECTION_OBJECT_POINTERS sop =
			    &vp->SectionObjectPointers;
			if (sop->SharedCacheMap == NULL &&
			    sop->DataSectionObject == NULL &&
			    sop->ImageSectionObject == NULL)
				continue;
			/* Hold a reference so vp stays valid after lock drop */
			vnode_lock(vp);
			if (vp->v_flags & VNODE_DEAD) {
				vnode_unlock(vp);
				continue;
			}
			atomic_inc_32(&vp->v_iocount);
			vnode_unlock(vp);
			found = B_TRUE;
			break;
		}
		mutex_exit(&vnode_all_list_lock);

		if (found) {
			dprintf("%s: flushing CcMgr for stuck vp %p\n",
			    __func__, vp);
			IO_STATUS_BLOCK iosb = { 0 };
			CcFlushCache(&vp->SectionObjectPointers,
			    NULL, 0, &iosb);
			/*
			 * Acquire/release PagingIoResource to wait for any
			 * in-flight paging I/O to drain before purging.
			 */
			ExAcquireResourceExclusiveLite(
			    vp->FileHeader.PagingIoResource, TRUE);
			ExReleaseResourceLite(vp->FileHeader.PagingIoResource);
			CcPurgeCacheSection(&vp->SectionObjectPointers,
			    NULL, 0, FALSE);
			atomic_dec_32(&vp->v_iocount);
		}
	} while (found && --max_passes > 0);
}

int
vflush(struct mount *mp, struct vnode *skipvp, int flags)
{
	// Iterate the vnode list and call reclaim
	// flags:
	//   SKIPROOT : dont release root nodes (mountpoints)
	// SKIPSYSTEM : dont release vnodes marked as system
	// FORCECLOSE : release everything, force unmount

	// if mp is NULL, we are reclaiming nodes, until threshold
	int reclaims = 0;
	vnode_fileobjects_t *node;
	struct vnode *rvp;
	boolean_t filesonly = B_TRUE;

	dprintf("vflush start\n");

	mutex_enter(&vnode_all_list_lock);

	print_reclaim_stats(B_TRUE, 0);

filesanddirs:
	for (rvp = list_head(&vnode_all_list); rvp;
	    rvp = list_next(&vnode_all_list, rvp)) {
		// skip vnodes not belonging to this mount
		if (mp && rvp->v_mount != mp)
			continue;

		if (filesonly && vnode_isdir(rvp))
			continue;

		// If we aren't FORCE and asked to SKIPROOT, and node
		// is MARKROOT, then go to next.
		if (!(flags & FORCECLOSE)) {
			if ((flags & SKIPROOT))
				if (rvp->v_flags & VNODE_MARKROOT)
					continue;
#if 0 // when we use SYSTEM vnodes
			if ((flags & SKIPSYSTEM))
				if (rvp->v_flags & VNODE_MARKSYSTEM)
					continue;
#endif
		}
		// We are to remove this node, even if ROOT - unmark it.

		if (rvp->v_flags & VNODE_DEAD) {
			continue;
		}

		mutex_enter(&rvp->v_mutex);

		// this hack is no longer needed
		// flush_file_objects(rvp);

		// vnode_recycle_int() will exit v_mutex
		// re-check flags, due to releasing locks
		if (!vnode_recycle_int(rvp, (flags & FORCECLOSE) |
		    VNODELOCKED)) {
			reclaims++;
			print_reclaim_stats(B_FALSE, reclaims);
		}
	}

	if (filesonly) {
		filesonly = B_FALSE;
		goto filesanddirs;
	}

	mutex_exit(&vnode_all_list_lock);

	if (reclaims > 0) {
		dprintf("%s: %d reclaims processed.\n", __func__, reclaims);
	}


	kpreempt(KPREEMPT_SYNC);
#if 0
	/*
	 * Process all remaining nodes, release znode, and set vnode to NULL
	 * move to dead list.
	 */
	int deadlist = 0;
	mutex_enter(&vnode_all_list_lock);
	for (rvp = list_head(&vnode_all_list);
	    rvp;
	    rvp = list_next(&vnode_all_list, rvp)) {
		if (rvp->v_mount == mp) {
			if (rvp->v_data) {
				deadlist++;
				// mutex_exit(&vnode_all_list_lock);
				zfs_vnop_reclaim(rvp);
				// mutex_enter(&vnode_all_list_lock);
				// Also empty fileobjects
				while (node = avl_first(&rvp->v_fileobjects)) {
					avl_remove(&rvp->v_fileobjects, node);
					kmem_free(node, sizeof (*node));
				}
			} else {
				rvp->v_age = gethrtime() - SEC2NSEC(6);
			}
			dprintf("%p marked DEAD2\n", rvp);

			rvp->v_flags |= VNODE_DEAD;
			rvp->v_data = NULL;
		}
	}
	mutex_exit(&vnode_all_list_lock);

	dprintf("vflush end: deadlisted %d nodes\n", deadlist);
#endif
	if (flags & FORCECLOSE)
		vnode_drain_delayclose(1);

	return (reclaims > 0 ? EBUSY : 0);
}

int
vnode_umount_preflight(struct mount *mp, struct vnode *skipvp, int flags)
{
	struct vnode *rvp;
	int Status;

	dprintf("%s start\n", __func__);

	mutex_enter(&vnode_all_list_lock);

	for (rvp = list_head(&vnode_all_list);
	    rvp;
	    rvp = list_next(&vnode_all_list, rvp)) {

		// skip vnodes not belonging to this mount
		if (mp && rvp->v_mount != mp)
			continue;

		if (vnode_isdir(rvp))
			continue;

		if (rvp == skipvp)
			continue;

		if (!(flags & FORCECLOSE)) {
			if ((flags & SKIPROOT))
				if (rvp->v_flags & VNODE_MARKROOT)
					continue;
#if 0 // when we use SYSTEM vnodes
			if ((flags & SKIPSYSTEM))
				if (rvp->v_flags & VNODE_MARKSYSTEM)
					continue;
#endif
		}

		if (rvp->v_usecount != 0) {
			mutex_exit(&vnode_all_list_lock);
			return (EBUSY);
		}
#if 0
		else if (rvp->v_iocount > 0) {
			// macOS waits upto 3s here and tries again.
		}
#endif
	} // for all vnodes

	mutex_exit(&vnode_all_list_lock);
	return (0);
}

/*
 * Set the Windows SecurityPolicy
 */
void
vnode_setsecurity(vnode_t *vp, void *sd)
{
	vp->security_descriptor = sd;
}

void *
vnode_security(vnode_t *vp)
{
	return (vp->security_descriptor);
}

extern CACHE_MANAGER_CALLBACKS CacheManagerCallbacks;

void
vnode_couplefileobject(vnode_t *vp, FILE_OBJECT *fileobject, uint64_t size)
{
	if (fileobject) {

		fileobject->FsContext = vp;

		// Make sure it is pointing to the right vp.
		if (fileobject->SectionObjectPointer != NULL)
			VERIFY3P(vnode_sectionpointer(vp), ==, fileobject->
			    SectionObjectPointer);
#if 0
		if (fileobject->SectionObjectPointer !=
		    vnode_sectionpointer(vp)) {
			fileobject->SectionObjectPointer =
			    vnode_sectionpointer(vp);
		}
#endif
		// If this fo's CcMgr hasn't been initialised, do so now
		// this ties each fileobject to CcMgr, it is not about
		// the vp itself. CcInit will be called many times on a vp,
		// once for each fileobject.
		dprintf("%s: vp %p fo %p\n", __func__, vp, fileobject);

		// Add this fileobject to the list of known ones.
		vnode_fileobject_add(vp, fileobject);

		if (vnode_isvroot(vp))
			return;

		vnode_pager_setsize(fileobject, vp, size, FALSE);

	}
}

// Attempt to boot CcMgr out of the fileobject, return
// true if we could
int
vnode_flushcache(vnode_t *vp, FILE_OBJECT *fileobject, boolean_t hard)
{
	CACHE_UNINITIALIZE_EVENT UninitializeCompleteEvent;
	// NTSTATUS WaitStatus;
	LARGE_INTEGER Zero = { .QuadPart = 0 };
	int ret = 1;

	if (vp == NULL)
		return (1);

	if (fileobject == NULL)
		return (1);

	// Have CcMgr already released it?
	if (fileobject->SectionObjectPointer == NULL)
		return (1);

	if (FlagOn(fileobject->Flags, FO_CLEANUP_COMPLETE)) {
		// return 1;
	}

	if (avl_numnodes(&vp->v_fileobjects) > 1) {
		dprintf("warning, has other fileobjects: %d\n",
		    avl_numnodes(&vp->v_fileobjects));
	}

	int lastclose = 0;

	if (vp->v_iocount <= 1 && vp->v_usecount == 0)
		lastclose = 1;

// Because CcUninitializeCacheMap() can call MJ_CLOSE immediately, and we
// don't want to free anything in *that* call, take a usecount++ here, that
// way we skip the vnode_isinuse() test
	atomic_inc_32(&vp->v_usecount);

	if (fileobject->SectionObjectPointer->ImageSectionObject) {
		if (hard)
			(void) MmForceSectionClosed(
			    fileobject->SectionObjectPointer, TRUE);
		else
			(void) MmFlushImageSection(
			    fileobject->SectionObjectPointer, MmFlushForWrite);
	}
#if 1
	if (lastclose && FlagOn(fileobject->Flags, FO_CACHE_SUPPORTED) &&
	    !FlagOn(fileobject->Flags, FO_CLEANUP_COMPLETE)) {
		// DataSection next
		if (fileobject->SectionObjectPointer->DataSectionObject) {
			CcFlushCache(fileobject->SectionObjectPointer, NULL, 0,
			    NULL);
			ExAcquireResourceExclusiveLite(
			    vp->FileHeader.PagingIoResource, TRUE);
			ExReleaseResourceLite(vp->FileHeader.PagingIoResource);
		}

		CcPurgeCacheSection(fileobject->SectionObjectPointer, NULL,
		    0, hard);

#if 0
		if (vp->has_uninit > 3) {
			NTSTATUS ntstatus;
			IO_STATUS_BLOCK IoStatus;
			ntstatus = ZwDeviceIoControlFile(fileobject, 0, 0, 0,
			    &IoStatus, FSCTL_DISMOUNT_VOLUME, 0, 0, 0, 0);
			dprintf("Said 0x%x\n", ntstatus);
			ntstatus = ZwDeviceIoControlFile(fileobject, 0, 0, 0,
			    &IoStatus, IRP_MN_REMOVE_DEVICE, 0, 0, 0, 0);
			dprintf("Said 0x%x\n", ntstatus);
		}
#endif

	}
#endif

	if (!hard && avl_numnodes(&vp->v_fileobjects) > 1) {
	// dprintf("leaving early due to v_fileobjects > 1 - flush only\n");
	// ret = 0;
	// goto out;
	}

	if (fileobject->PrivateCacheMap == NULL) {
		KeInitializeEvent(&UninitializeCompleteEvent.Event,
		    SynchronizationEvent,
		    FALSE);

		// Try to release cache
		TraceEvent(8, "calling CcUninit: fo %p\n", fileobject);
		CcUninitializeCacheMap(fileobject,
		    hard ? &Zero : NULL,
		    NULL);
		TraceEvent(8, "complete CcUninit\n");
	}

	ret = 1;
	if (fileobject && fileobject->SectionObjectPointer)
		if ((fileobject->SectionObjectPointer->ImageSectionObject !=
		    NULL) ||
		    (fileobject->SectionObjectPointer->DataSectionObject !=
		    NULL) ||
		    (fileobject->SectionObjectPointer->SharedCacheMap !=
		    NULL)) {
		ret = 0;
		dprintf("vp %p: Non^NULL entires so saying failed\n", vp);
	}

	// if (ret)
	//  fileobject->SectionObjectPointer = NULL;
// out:
	// Remove usecount lock held above.
	atomic_dec_32(&vp->v_usecount);

	// Unable to fully release CcMgr
	TraceEvent(8, "%s: ret %d : vp %p fo %p\n", __func__, ret,
	    vp, fileobject);

	return (ret);
}


void
vnode_decouplefileobject(vnode_t *vp, FILE_OBJECT *fileobject)
{
	if (fileobject && fileobject->FsContext) {
		dprintf("%s: fo %p -X-> %p\n", __func__, fileobject, vp);

		vnode_fileobject_remove(vp, fileobject);

		// If we are flushing, we do nothing here.
		if (vp->v_flags & VNODE_FLUSHING) {
			dprintf("Already flushing; FS re-entry\n");
			return;
		}

		// if (vnode_flushcache(vp, fileobject, FALSE))

		//	fileobject->FsContext = NULL;
	}
}

void
vnode_setsizechange(vnode_t *vp, int set)
{
	if (set)
		vp->v_flags |= VNODE_SIZECHANGE;
	else
		vp->v_flags &= ~VNODE_SIZECHANGE;
}

int
vnode_sizechange(vnode_t *vp)
{
	return (vp->v_flags & VNODE_SIZECHANGE);
}

int
vnode_isrecycled(vnode_t *vp)
{
	return (vp->v_flags&(VNODE_MARKTERM | VNODE_DEAD));
}

void
vnode_lock(vnode_t *vp)
{
	mutex_enter(&vp->v_mutex);
}

void
vnode_unlock(vnode_t *vp)
{
	mutex_exit(&vp->v_mutex);
}

int
vnode_fileobject_member(vnode_t *vp, void *fo)
{
	avl_index_t idx;
	mutex_enter(&vp->v_mutex);
	// Early out to avoid memory alloc
	vnode_fileobjects_t search;
	search.fileobject = fo;
	if (avl_find(&vp->v_fileobjects, &search, &idx) != NULL) {
		mutex_exit(&vp->v_mutex);
		return (1);
	}
	mutex_exit(&vp->v_mutex);
	return (0);
}

/*
 * Add a FileObject to the list of FO in the vnode.
 * Return 1 if we actually added it
 * Return 0 if it was already in the list.
 */
int
vnode_fileobject_add(vnode_t *vp, void *fo)
{
	vnode_fileobjects_t *node;
	avl_index_t idx;
	// KIRQL OldIrql;
	mutex_enter(&vp->v_mutex);
	// Early out to avoid memory alloc
	vnode_fileobjects_t search;
	search.fileobject = fo;
	if (avl_find(&vp->v_fileobjects, &search, &idx) != NULL) {
		mutex_exit(&vp->v_mutex);
		return (0);
	}
	mutex_exit(&vp->v_mutex);

	node = kmem_alloc(sizeof (*node), KM_SLEEP);
	node->fileobject = fo;
	node->remove = 0;
	node->cleanedup = 0;

	mutex_enter(&vp->v_mutex);
	if (avl_find(&vp->v_fileobjects, node, &idx) == NULL) {
		avl_insert(&vp->v_fileobjects, node, idx);
		mutex_exit(&vp->v_mutex);
		dprintf("%s: added FO %p to vp %p\n", __func__, fo, vp);
		return (1);
	} else {
		mutex_exit(&vp->v_mutex);
		kmem_free(node, sizeof (*node));
		return (0);
	}
	// not reached.
	mutex_exit(&vp->v_mutex);
	return (0);
}

/*
 * Remove a FileObject from the list of FO in the vnode.
 * Return 1 if we actually removed it
 * Return 0 if it was not in the list.
 */
int
vnode_fileobject_remove(vnode_t *vp, void *fo)
{
	vnode_fileobjects_t search, *node;
	// KIRQL OldIrql;
	mutex_enter(&vp->v_mutex);
	search.fileobject = fo;
	node = avl_find(&vp->v_fileobjects, &search, NULL);
	if (node == NULL) {
		mutex_exit(&vp->v_mutex);
		return (0);
	}
	avl_remove(&vp->v_fileobjects, node);
	mutex_exit(&vp->v_mutex);
	kmem_free(node, sizeof (*node));

	dprintf("%s: remed FO %p fm vp %p\n", __func__, fo, vp);

	if (avl_numnodes(&vp->v_fileobjects) == 0)
		dprintf("vp %p no more fileobjects, it should be released\n",
		    vp);

	return (1);
}

/*
 * Check and make sure the list of FileObjects is empty
 */
int
vnode_fileobject_empty(vnode_t *vp, int locked)
{
	// KIRQL OldIrql;

	if (!locked)
		mutex_enter(&vp->v_mutex);
	boolean_t ret = avl_is_empty(&vp->v_fileobjects);
	if (!locked)
		mutex_exit(&vp->v_mutex);

	return (ret);
}

/*
 * Number of FILE_OBJECTs coupled to this vnode that have NOT yet gone
 * through their own IRP_MJ_CLEANUP (vnode_fileobject_mark_cleanedup()).
 *
 * A FILE_OBJECT is only decoupled (removed from v_fileobjects) at its own
 * separate IRP_MJ_CLOSE, which Windows can delay arbitrarily long after
 * IRP_MJ_CLEANUP -- e.g. while the Cache Manager finishes tearing down a
 * SectionObjectPointer/CcUninitializeCacheMap asynchronously in the
 * background.  During that window the FO is still in v_fileobjects but is,
 * for every user-visible purpose, already closed.  Counting it as "still
 * attached" made a delete-on-close gate here get permanently stuck once a
 * file was opened/closed/reopened fast enough (confirmed: FreeFileSync's
 * lock-file churn) for such a zombie FO to still be present when the real
 * delete's own IRP_MJ_CLEANUP ran, leaving the vnode's unlink flag set
 * forever and STATUS_DELETE_PENDING returned to every later open of that
 * name (surfaces to callers as generic access-denied).
 *
 * So this only counts FOs that have not been marked cleaned-up: "is anyone
 * else genuinely still using this file" should compare the result to 0,
 * not 1 -- call vnode_fileobject_mark_cleanedup() for the FO whose own
 * cleanup is running before checking this, so it excludes itself too.
 */
uint64_t
vnode_fileobject_count(vnode_t *vp, int locked)
{
	if (!locked)
		mutex_enter(&vp->v_mutex);
	uint64_t ret = 0;
	for (vnode_fileobjects_t *node = avl_first(&vp->v_fileobjects);
	    node != NULL; node = AVL_NEXT(&vp->v_fileobjects, node)) {
		if (!node->cleanedup)
			ret++;
	}
	if (!locked)
		mutex_exit(&vp->v_mutex);

	return (ret);
}

/*
 * Mark a FILE_OBJECT as having gone through its own IRP_MJ_CLEANUP, so
 * vnode_fileobject_count() stops counting it.  It remains in v_fileobjects
 * (and vnode_fileobject_member()/vnode_fileobject_empty() still see it)
 * until its eventual IRP_MJ_CLOSE calls vnode_fileobject_remove() -- this
 * only affects whether it counts as "still actively attached" for the
 * delete-on-close gate in zfs_fileobject_cleanup().
 */
int
vnode_fileobject_mark_cleanedup(vnode_t *vp, void *fo)
{
	vnode_fileobjects_t search, *node;

	mutex_enter(&vp->v_mutex);
	search.fileobject = fo;
	node = avl_find(&vp->v_fileobjects, &search, NULL);
	if (node == NULL) {
		mutex_exit(&vp->v_mutex);
		return (0);
	}
	node->cleanedup = 1;
	mutex_exit(&vp->v_mutex);

	return (1);
}

// Get cached EA size, returns 1 is it is cached, 0 if not.
int
vnode_easize(struct vnode *vp, uint64_t *size)
{
	if (vp->v_flags & VNODE_EASIZE) {
		*size = vp->v_easize;
		return (1);
	}
	return (0);
}

void
vnode_set_easize(struct vnode *vp, uint64_t size)
{
	vp->v_easize = size;
	vp->v_flags |= VNODE_EASIZE;
}

void
vnode_clear_easize(struct vnode *vp)
{
	vp->v_flags &= ~VNODE_EASIZE;
}

void
vnode_set_reparse(struct vnode *vp, REPARSE_DATA_BUFFER *rpp, size_t size)
{
	if (vp->v_reparse != NULL && size > 0) {
		kmem_free(vp->v_reparse, vp->v_reparse_size);
	}
	vp->v_reparse = NULL;
	vp->v_reparse_size = 0;

	if (rpp != NULL && size > 0) {
		vp->v_reparse = kmem_alloc(size, KM_SLEEP);
		vp->v_reparse_size = size;
		memcpy(vp->v_reparse, rpp, size);
	}
}

ULONG
vnode_get_reparse_tag(struct vnode *vp)
{
	return (vp->v_reparse ? vp->v_reparse->ReparseTag : 0);
}

int
vnode_get_reparse_point(struct vnode *vp, REPARSE_DATA_BUFFER **rpp,
    size_t *size)
{
	if (vp->v_reparse == NULL || vp->v_reparse_size == 0)
		return (ENOENT);
	ASSERT3P(rpp, !=, NULL);
	ASSERT3P(size, !=, NULL);
	*rpp = vp->v_reparse;
	*size = vp->v_reparse_size;
	return (0);
}

#ifdef DEBUG_IOCOUNT
void
vnode_check_iocount(void)
{
	/* Iterate all vnodes, checking that iocount is zero. */
	struct vnode *rvp;
	mutex_enter(&vnode_all_list_lock);
	for (rvp = list_head(&vnode_all_list);
	    rvp;
	    rvp = list_next(&vnode_all_list, rvp)) {
		ASSERT0(rvp->v_iocount);
	}
	mutex_exit(&vnode_all_list_lock);
}
#endif

// Currently not used by Windows
void
vnode_pager_setsize(void *fo, vnode_t *vp, uint64_t size, boolean_t delay)
{
}

void
vfs_changeowner(mount_t *from, mount_t *to)
{
	struct vnode *rvp;
	mutex_enter(&vnode_all_list_lock);
	for (rvp = list_head(&vnode_all_list);
	    rvp;
	    rvp = list_next(&vnode_all_list, rvp)) {
		if (rvp->v_mount == from)
			rvp->v_mount = to;

	}
	mutex_exit(&vnode_all_list_lock);
}

uint32_t
vnode_unlink(struct vnode *vp)
{
	return (vp->v_unlink);
}

void
vnode_setunlink(struct vnode *vp, uint32_t set)
{
	vp->v_unlink = set;
}
