#include <sys/sysmacros.h>
#include "config.h"

/*
 * XXX: currently borrrowed from libsolcompat until this
 * can be adapted to the linux kernel interfaces.
 */
#if 0
/*
 * =========================================================================
 * vnode operations
 * =========================================================================
 */
/*
 * Note: for the xxxat() versions of these functions, we assume that the
 * starting vp is always rootdir (which is true for spa_directory.c, the only
 * ZFS consumer of these interfaces).  We assert this is true, and then emulate
 * them by adding '/' in front of the path.
 */

/*ARGSUSED*/
int
vn_open(char *path, int x1, int flags, int mode, vnode_t **vpp, int x2, int x3)
{
	int fd;
	vnode_t *vp;
	int old_umask;
	char realpath[MAXPATHLEN];
	struct stat64 st;

	/*
	 * If we're accessing a real disk from userland, we need to use
	 * the character interface to avoid caching.  This is particularly
	 * important if we're trying to look at a real in-kernel storage
	 * pool from userland, e.g. via zdb, because otherwise we won't
	 * see the changes occurring under the segmap cache.
	 * On the other hand, the stupid character device returns zero
	 * for its size.  So -- gag -- we open the block device to get
	 * its size, and remember it for subsequent VOP_GETATTR().
	 */
#if defined(__sun__) || defined(__sun)
	if (strncmp(path, "/dev/", 5) == 0) {
#else
	if (0) {
#endif
		char *dsk;
		fd = open64(path, O_RDONLY);
		if (fd == -1)
			return (errno);
		if (fstat64(fd, &st) == -1) {
			close(fd);
			return (errno);
		}
		close(fd);
		(void) sprintf(realpath, "%s", path);
		dsk = strstr(path, "/dsk/");
		if (dsk != NULL)
			(void) sprintf(realpath + (dsk - path) + 1, "r%s",
			    dsk + 1);
	} else {
		(void) sprintf(realpath, "%s", path);
		if (!(flags & FCREAT) && stat64(realpath, &st) == -1)
			return (errno);
	}

#ifdef __linux__
	if (!(flags & FCREAT) && S_ISBLK(st.st_mode)) {
		flags |= O_DIRECT;
		if (flags & FWRITE)
			flags |= O_EXCL;
	}
#endif

	if (flags & FCREAT)
		old_umask = umask(0);

	/*
	 * The construct 'flags - FREAD' conveniently maps combinations of
	 * FREAD and FWRITE to the corresponding O_RDONLY, O_WRONLY, and O_RDWR.
	 */
	fd = open64(realpath, flags - FREAD, mode);

	if (flags & FCREAT)
		(void) umask(old_umask);

	if (fd == -1)
		return (errno);

	if (fstat64(fd, &st) == -1) {
		close(fd);
		return (errno);
	}

	(void) fcntl(fd, F_SETFD, FD_CLOEXEC);

	*vpp = vp = umem_zalloc(sizeof (vnode_t), UMEM_NOFAIL);

	vp->v_fd = fd;
	vp->v_size = st.st_size;
	vp->v_mode = st.st_mode;
	vp->v_path = spa_strdup(path);

	return (0);
}

/*ARGSUSED*/
int
vn_openat(char *path, int x1, int flags, int mode, vnode_t **vpp, int x2,
    int x3, vnode_t *startvp, int fd)
{
	char *realpath = umem_alloc(strlen(path) + 2, UMEM_NOFAIL);
	int ret;

	ASSERT(startvp == rootdir);
	(void) sprintf(realpath, "/%s", path);

	/* fd ignored for now, need if want to simulate nbmand support */
	ret = vn_open(realpath, x1, flags, mode, vpp, x2, x3);

	umem_free(realpath, strlen(path) + 2);

	return (ret);
}

/*ARGSUSED*/
int
vn_rdwr(int uio, vnode_t *vp, void *addr, ssize_t len, offset_t offset,
	int x1, int x2, rlim64_t x3, void *x4, ssize_t *residp)
{
	ssize_t iolen, split;

	if (uio == UIO_READ) {
		iolen = pread64(vp->v_fd, addr, len, offset);
	} else {
		/*
		 * To simulate partial disk writes, we split writes into two
		 * system calls so that the process can be killed in between.
		 */
#ifdef ZFS_DEBUG
		if (!S_ISBLK(vp->v_mode) && !S_ISCHR(vp->v_mode)) {
			split = (len > 0 ? rand() % len : 0);
			iolen = pwrite64(vp->v_fd, addr, split, offset);
			iolen += pwrite64(vp->v_fd, (char *)addr + split,
			    len - split, offset + split);
		} else
			iolen = pwrite64(vp->v_fd, addr, len, offset);
#else
		iolen = pwrite64(vp->v_fd, addr, len, offset);
#endif
	}

	if (iolen < 0)
		return (errno);
	if (residp)
		*residp = len - iolen;
	else if (iolen != len)
		return (EIO);
	return (0);
}

void
vn_close(vnode_t *vp)
{
	close(vp->v_fd);
	spa_strfree(vp->v_path);
	umem_free(vp, sizeof (vnode_t));
}
#endif
