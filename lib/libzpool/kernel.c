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
 */

#include <assert.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>
#include <sys/signal.h>
#include <sys/spa.h>
#include <sys/stat.h>
#include <sys/processor.h>
#include <sys/zfs_context.h>
#include <sys/rrwlock.h>
#include <sys/utsname.h>
#include <sys/time.h>
#include <sys/systeminfo.h>

/*
 * Emulation of kernel services in userland.
 */

int aok;
uint64_t physmem;
vnode_t *rootdir = (vnode_t *)0xabcd1234;
char hw_serial[HW_HOSTID_LEN];
struct utsname hw_utsname;
vmem_t *zio_arena = NULL;

/* this only exists to have its address taken */
struct proc p0;

/*
 * =========================================================================
 * threads
 * =========================================================================
 */

pthread_cond_t kthread_cond = PTHREAD_COND_INITIALIZER;
pthread_mutex_t kthread_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_key_t kthread_key;
int kthread_nr = 0;

static void
thread_init(void)
{
	kthread_t *kt;

	VERIFY3S(pthread_key_create(&kthread_key, NULL), ==, 0);

	/* Create entry for primary kthread */
	kt = umem_zalloc(sizeof (kthread_t), UMEM_NOFAIL);
	kt->t_tid = pthread_self();
	kt->t_func = NULL;

	VERIFY3S(pthread_setspecific(kthread_key, kt), ==, 0);

	/* Only the main thread should be running at the moment */
	ASSERT3S(kthread_nr, ==, 0);
	kthread_nr = 1;
}

static void
thread_fini(void)
{
	kthread_t *kt = curthread;

	ASSERT(pthread_equal(kt->t_tid, pthread_self()));
	ASSERT3P(kt->t_func, ==, NULL);

	umem_free(kt, sizeof (kthread_t));

	/* Wait for all threads to exit via thread_exit() */
	VERIFY3S(pthread_mutex_lock(&kthread_lock), ==, 0);

	kthread_nr--; /* Main thread is exiting */

	while (kthread_nr > 0)
		VERIFY3S(pthread_cond_wait(&kthread_cond, &kthread_lock), ==,
		    0);

	ASSERT3S(kthread_nr, ==, 0);
	VERIFY3S(pthread_mutex_unlock(&kthread_lock), ==, 0);

	VERIFY3S(pthread_key_delete(kthread_key), ==, 0);
}

kthread_t *
zk_thread_current(void)
{
	kthread_t *kt = pthread_getspecific(kthread_key);

	ASSERT3P(kt, !=, NULL);

	return (kt);
}

void *
zk_thread_helper(void *arg)
{
	kthread_t *kt = (kthread_t *) arg;

	VERIFY3S(pthread_setspecific(kthread_key, kt), ==, 0);

	VERIFY3S(pthread_mutex_lock(&kthread_lock), ==, 0);
	kthread_nr++;
	VERIFY3S(pthread_mutex_unlock(&kthread_lock), ==, 0);
	(void) setpriority(PRIO_PROCESS, 0, kt->t_pri);

	kt->t_tid = pthread_self();
	((thread_func_arg_t) kt->t_func)(kt->t_arg);

	/* Unreachable, thread must exit with thread_exit() */
	abort();

	return (NULL);
}

kthread_t *
zk_thread_create(caddr_t stk, size_t stksize, thread_func_t func, void *arg,
    size_t len, proc_t *pp, int state, pri_t pri, int detachstate)
{
	kthread_t *kt;
	pthread_attr_t attr;
	char *stkstr;

	ASSERT0(state & ~TS_RUN);

	kt = umem_zalloc(sizeof (kthread_t), UMEM_NOFAIL);
	kt->t_func = func;
	kt->t_arg = arg;
	kt->t_pri = pri;

	VERIFY0(pthread_attr_init(&attr));
	VERIFY0(pthread_attr_setdetachstate(&attr, detachstate));

	/*
	 * We allow the default stack size in user space to be specified by
	 * setting the ZFS_STACK_SIZE environment variable.  This allows us
	 * the convenience of observing and debugging stack overruns in
	 * user space.  Explicitly specified stack sizes will be honored.
	 * The usage of ZFS_STACK_SIZE is discussed further in the
	 * ENVIRONMENT VARIABLES sections of the ztest(1) man page.
	 */
	if (stksize == 0) {
		stkstr = getenv("ZFS_STACK_SIZE");

		if (stkstr == NULL)
			stksize = TS_STACK_MAX;
		else
			stksize = MAX(atoi(stkstr), TS_STACK_MIN);
	}

	VERIFY3S(stksize, >, 0);
	stksize = P2ROUNDUP(MAX(stksize, TS_STACK_MIN), PAGESIZE);
	VERIFY0(pthread_attr_setstacksize(&attr, stksize));
	VERIFY0(pthread_attr_setguardsize(&attr, PAGESIZE));

	VERIFY0(pthread_create(&kt->t_tid, &attr, &zk_thread_helper, kt));
	VERIFY0(pthread_attr_destroy(&attr));

	return (kt);
}

void
zk_thread_exit(void)
{
	kthread_t *kt = curthread;

	ASSERT(pthread_equal(kt->t_tid, pthread_self()));

	umem_free(kt, sizeof (kthread_t));

	pthread_mutex_lock(&kthread_lock);
	kthread_nr--;
	pthread_mutex_unlock(&kthread_lock);

	pthread_cond_broadcast(&kthread_cond);
	pthread_exit((void *)TS_MAGIC);
}

void
zk_thread_join(kt_did_t tid)
{
	void *ret;

	pthread_join((pthread_t)tid, &ret);
	VERIFY3P(ret, ==, (void *)TS_MAGIC);
}

/*
 * =========================================================================
 * kstats
 * =========================================================================
 */
/*ARGSUSED*/
kstat_t *
kstat_create(const char *module, int instance, const char *name,
    const char *class, uchar_t type, ulong_t ndata, uchar_t ks_flag)
{
	return (NULL);
}

/*ARGSUSED*/
void
kstat_install(kstat_t *ksp)
{}

/*ARGSUSED*/
void
kstat_delete(kstat_t *ksp)
{}

/*ARGSUSED*/
void
kstat_waitq_enter(kstat_io_t *kiop)
{}

/*ARGSUSED*/
void
kstat_waitq_exit(kstat_io_t *kiop)
{}

/*ARGSUSED*/
void
kstat_runq_enter(kstat_io_t *kiop)
{}

/*ARGSUSED*/
void
kstat_runq_exit(kstat_io_t *kiop)
{}

/*ARGSUSED*/
void
kstat_waitq_to_runq(kstat_io_t *kiop)
{}

/*ARGSUSED*/
void
kstat_runq_back_to_waitq(kstat_io_t *kiop)
{}

void
kstat_set_raw_ops(kstat_t *ksp,
    int (*headers)(char *buf, size_t size),
    int (*data)(char *buf, size_t size, void *data),
    void *(*addr)(kstat_t *ksp, loff_t index))
{}

/*
 * =========================================================================
 * mutexes
 * =========================================================================
 */

void
mutex_init(kmutex_t *mp, char *name, int type, void *cookie)
{
	ASSERT3S(type, ==, MUTEX_DEFAULT);
	ASSERT3P(cookie, ==, NULL);
	mp->m_owner = MTX_INIT;
	mp->m_magic = MTX_MAGIC;
	VERIFY3S(pthread_mutex_init(&mp->m_lock, NULL), ==, 0);
}

void
mutex_destroy(kmutex_t *mp)
{
	ASSERT3U(mp->m_magic, ==, MTX_MAGIC);
	ASSERT3P(mp->m_owner, ==, MTX_INIT);
	ASSERT0(pthread_mutex_destroy(&(mp)->m_lock));
	mp->m_owner = MTX_DEST;
	mp->m_magic = 0;
}

void
mutex_enter(kmutex_t *mp)
{
	ASSERT3U(mp->m_magic, ==, MTX_MAGIC);
	ASSERT3P(mp->m_owner, !=, MTX_DEST);
	ASSERT3P(mp->m_owner, !=, curthread);
	VERIFY3S(pthread_mutex_lock(&mp->m_lock), ==, 0);
	ASSERT3P(mp->m_owner, ==, MTX_INIT);
	mp->m_owner = curthread;
}

int
mutex_tryenter(kmutex_t *mp)
{
	ASSERT3U(mp->m_magic, ==, MTX_MAGIC);
	ASSERT3P(mp->m_owner, !=, MTX_DEST);
	if (0 == pthread_mutex_trylock(&mp->m_lock)) {
		ASSERT3P(mp->m_owner, ==, MTX_INIT);
		mp->m_owner = curthread;
		return (1);
	} else {
		return (0);
	}
}

void
mutex_exit(kmutex_t *mp)
{
	ASSERT3U(mp->m_magic, ==, MTX_MAGIC);
	ASSERT3P(mutex_owner(mp), ==, curthread);
	mp->m_owner = MTX_INIT;
	VERIFY3S(pthread_mutex_unlock(&mp->m_lock), ==, 0);
}

void *
mutex_owner(kmutex_t *mp)
{
	ASSERT3U(mp->m_magic, ==, MTX_MAGIC);
	return (mp->m_owner);
}

int
mutex_held(kmutex_t *mp)
{
	return (mp->m_owner == curthread);
}

/*
 * =========================================================================
 * rwlocks
 * =========================================================================
 */

void
rw_init(krwlock_t *rwlp, char *name, int type, void *arg)
{
	ASSERT3S(type, ==, RW_DEFAULT);
	ASSERT3P(arg, ==, NULL);
	VERIFY3S(pthread_rwlock_init(&rwlp->rw_lock, NULL), ==, 0);
	rwlp->rw_owner = RW_INIT;
	rwlp->rw_wr_owner = RW_INIT;
	rwlp->rw_readers = 0;
	rwlp->rw_magic = RW_MAGIC;
}

void
rw_destroy(krwlock_t *rwlp)
{
	ASSERT3U(rwlp->rw_magic, ==, RW_MAGIC);
	ASSERT(rwlp->rw_readers == 0 && rwlp->rw_wr_owner == RW_INIT);
	VERIFY3S(pthread_rwlock_destroy(&rwlp->rw_lock), ==, 0);
	rwlp->rw_magic = 0;
}

void
rw_enter(krwlock_t *rwlp, krw_t rw)
{
	ASSERT3U(rwlp->rw_magic, ==, RW_MAGIC);
	ASSERT3P(rwlp->rw_owner, !=, curthread);
	ASSERT3P(rwlp->rw_wr_owner, !=, curthread);

	if (rw == RW_READER) {
		VERIFY3S(pthread_rwlock_rdlock(&rwlp->rw_lock), ==, 0);
		ASSERT3P(rwlp->rw_wr_owner, ==, RW_INIT);

		atomic_inc_uint(&rwlp->rw_readers);
	} else {
		VERIFY3S(pthread_rwlock_wrlock(&rwlp->rw_lock), ==, 0);
		ASSERT3P(rwlp->rw_wr_owner, ==, RW_INIT);
		ASSERT3U(rwlp->rw_readers, ==, 0);

		rwlp->rw_wr_owner = curthread;
	}

	rwlp->rw_owner = curthread;
}

void
rw_exit(krwlock_t *rwlp)
{
	ASSERT3U(rwlp->rw_magic, ==, RW_MAGIC);
	ASSERT(RW_LOCK_HELD(rwlp));

	if (RW_READ_HELD(rwlp))
		atomic_dec_uint(&rwlp->rw_readers);
	else
		rwlp->rw_wr_owner = RW_INIT;

	rwlp->rw_owner = RW_INIT;
	VERIFY3S(pthread_rwlock_unlock(&rwlp->rw_lock), ==, 0);
}

int
rw_tryenter(krwlock_t *rwlp, krw_t rw)
{
	int rv;

	ASSERT3U(rwlp->rw_magic, ==, RW_MAGIC);

	if (rw == RW_READER)
		rv = pthread_rwlock_tryrdlock(&rwlp->rw_lock);
	else
		rv = pthread_rwlock_trywrlock(&rwlp->rw_lock);

	if (rv == 0) {
		ASSERT3P(rwlp->rw_wr_owner, ==, RW_INIT);

		if (rw == RW_READER)
			atomic_inc_uint(&rwlp->rw_readers);
		else {
			ASSERT3U(rwlp->rw_readers, ==, 0);
			rwlp->rw_wr_owner = curthread;
		}

		rwlp->rw_owner = curthread;
		return (1);
	}

	VERIFY3S(rv, ==, EBUSY);

	return (0);
}

int
rw_tryupgrade(krwlock_t *rwlp)
{
	ASSERT3U(rwlp->rw_magic, ==, RW_MAGIC);

	return (0);
}

/*
 * =========================================================================
 * condition variables
 * =========================================================================
 */

void
cv_init(kcondvar_t *cv, char *name, int type, void *arg)
{
	ASSERT3S(type, ==, CV_DEFAULT);
	cv->cv_magic = CV_MAGIC;
	VERIFY3S(pthread_cond_init(&cv->cv, NULL), ==, 0);
}

void
cv_destroy(kcondvar_t *cv)
{
	ASSERT3U(cv->cv_magic, ==, CV_MAGIC);
	VERIFY3S(pthread_cond_destroy(&cv->cv), ==, 0);
	cv->cv_magic = 0;
}

void
cv_wait(kcondvar_t *cv, kmutex_t *mp)
{
	ASSERT3U(cv->cv_magic, ==, CV_MAGIC);
	ASSERT3P(mutex_owner(mp), ==, curthread);
	mp->m_owner = MTX_INIT;
	int ret = pthread_cond_wait(&cv->cv, &mp->m_lock);
	if (ret != 0)
		VERIFY3S(ret, ==, EINTR);
	mp->m_owner = curthread;
}

clock_t
cv_timedwait(kcondvar_t *cv, kmutex_t *mp, clock_t abstime)
{
	int error;
	struct timeval tv;
	timestruc_t ts;
	clock_t delta;

	ASSERT3U(cv->cv_magic, ==, CV_MAGIC);

top:
	delta = abstime - ddi_get_lbolt();
	if (delta <= 0)
		return (-1);

	VERIFY(gettimeofday(&tv, NULL) == 0);

	ts.tv_sec = tv.tv_sec + delta / hz;
	ts.tv_nsec = tv.tv_usec * 1000 + (delta % hz) * (NANOSEC / hz);
	if (ts.tv_nsec >= NANOSEC) {
		ts.tv_sec++;
		ts.tv_nsec -= NANOSEC;
	}

	ASSERT3P(mutex_owner(mp), ==, curthread);
	mp->m_owner = MTX_INIT;
	error = pthread_cond_timedwait(&cv->cv, &mp->m_lock, &ts);
	mp->m_owner = curthread;

	if (error == ETIMEDOUT)
		return (-1);

	if (error == EINTR)
		goto top;

	VERIFY3S(error, ==, 0);

	return (1);
}

/*ARGSUSED*/
clock_t
cv_timedwait_hires(kcondvar_t *cv, kmutex_t *mp, hrtime_t tim, hrtime_t res,
    int flag)
{
	int error;
	timestruc_t ts;
	hrtime_t delta;

	ASSERT(flag == 0);

top:
	delta = tim - gethrtime();
	if (delta <= 0)
		return (-1);

	ts.tv_sec = delta / NANOSEC;
	ts.tv_nsec = delta % NANOSEC;

	ASSERT(mutex_owner(mp) == curthread);
	mp->m_owner = NULL;
	error = pthread_cond_timedwait(&cv->cv, &mp->m_lock, &ts);
	mp->m_owner = curthread;

	if (error == ETIME)
		return (-1);

	if (error == EINTR)
		goto top;

	ASSERT(error == 0);

	return (1);
}

void
cv_signal(kcondvar_t *cv)
{
	ASSERT3U(cv->cv_magic, ==, CV_MAGIC);
	VERIFY3S(pthread_cond_signal(&cv->cv), ==, 0);
}

void
cv_broadcast(kcondvar_t *cv)
{
	ASSERT3U(cv->cv_magic, ==, CV_MAGIC);
	VERIFY3S(pthread_cond_broadcast(&cv->cv), ==, 0);
}

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
	int old_umask = 0;
	char *realpath;
	struct stat64 st;
	int err;

	realpath = umem_alloc(MAXPATHLEN, UMEM_NOFAIL);

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
		if (fd == -1) {
			err = errno;
			free(realpath);
			return (err);
		}
		if (fstat64(fd, &st) == -1) {
			err = errno;
			close(fd);
			free(realpath);
			return (err);
		}
		close(fd);
		(void) sprintf(realpath, "%s", path);
		dsk = strstr(path, "/dsk/");
		if (dsk != NULL)
			(void) sprintf(realpath + (dsk - path) + 1, "r%s",
			    dsk + 1);
	} else {
		(void) sprintf(realpath, "%s", path);
		if (!(flags & FCREAT) && stat64(realpath, &st) == -1) {
			err = errno;
			free(realpath);
			return (err);
		}
	}

	if (!(flags & FCREAT) && S_ISBLK(st.st_mode)) {
#ifdef __linux__
		flags |= O_DIRECT;
#endif
		/* We shouldn't be writing to block devices in userspace */
		VERIFY(!(flags & FWRITE));
	}

	if (flags & FCREAT)
		old_umask = umask(0);

	/*
	 * The construct 'flags - FREAD' conveniently maps combinations of
	 * FREAD and FWRITE to the corresponding O_RDONLY, O_WRONLY, and O_RDWR.
	 */
	fd = open64(realpath, flags - FREAD, mode);
	free(realpath);

	if (flags & FCREAT)
		(void) umask(old_umask);

	if (fd == -1)
		return (errno);

	if (fstat64_blk(fd, &st) == -1) {
		err = errno;
		close(fd);
		return (err);
	}

	(void) fcntl(fd, F_SETFD, FD_CLOEXEC);

	*vpp = vp = umem_zalloc(sizeof (vnode_t), UMEM_NOFAIL);

	vp->v_fd = fd;
	vp->v_size = st.st_size;
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
	ssize_t rc, done = 0, split;

	if (uio == UIO_READ) {
		rc = pread64(vp->v_fd, addr, len, offset);
	} else {
		/*
		 * To simulate partial disk writes, we split writes into two
		 * system calls so that the process can be killed in between.
		 */
		int sectors = len >> SPA_MINBLOCKSHIFT;
		split = (sectors > 0 ? rand() % sectors : 0) <<
		    SPA_MINBLOCKSHIFT;
		rc = pwrite64(vp->v_fd, addr, split, offset);
		if (rc != -1) {
			done = rc;
			rc = pwrite64(vp->v_fd, (char *)addr + split,
			    len - split, offset + split);
		}
	}

#ifdef __linux__
	if (rc == -1 && errno == EINVAL) {
		/*
		 * Under Linux, this most likely means an alignment issue
		 * (memory or disk) due to O_DIRECT, so we abort() in order to
		 * catch the offender.
		 */
		abort();
	}
#endif
	if (rc == -1)
		return (errno);

	done += rc;

	if (residp)
		*residp = len - done;
	else if (done != len)
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

/*
 * At a minimum we need to update the size since vdev_reopen()
 * will no longer call vn_openat().
 */
int
fop_getattr(vnode_t *vp, vattr_t *vap)
{
	struct stat64 st;
	int err;

	if (fstat64_blk(vp->v_fd, &st) == -1) {
		err = errno;
		close(vp->v_fd);
		return (err);
	}

	vap->va_size = st.st_size;
	return (0);
}

/*
 * =========================================================================
 * Figure out which debugging statements to print
 * =========================================================================
 */

static char *dprintf_string;
static int dprintf_print_all;

int
dprintf_find_string(const char *string)
{
	char *tmp_str = dprintf_string;
	int len = strlen(string);

	/*
	 * Find out if this is a string we want to print.
	 * String format: file1.c,function_name1,file2.c,file3.c
	 */

	while (tmp_str != NULL) {
		if (strncmp(tmp_str, string, len) == 0 &&
		    (tmp_str[len] == ',' || tmp_str[len] == '\0'))
			return (1);
		tmp_str = strchr(tmp_str, ',');
		if (tmp_str != NULL)
			tmp_str++; /* Get rid of , */
	}
	return (0);
}

void
dprintf_setup(int *argc, char **argv)
{
	int i, j;

	/*
	 * Debugging can be specified two ways: by setting the
	 * environment variable ZFS_DEBUG, or by including a
	 * "debug=..."  argument on the command line.  The command
	 * line setting overrides the environment variable.
	 */

	for (i = 1; i < *argc; i++) {
		int len = strlen("debug=");
		/* First look for a command line argument */
		if (strncmp("debug=", argv[i], len) == 0) {
			dprintf_string = argv[i] + len;
			/* Remove from args */
			for (j = i; j < *argc; j++)
				argv[j] = argv[j+1];
			argv[j] = NULL;
			(*argc)--;
		}
	}

	if (dprintf_string == NULL) {
		/* Look for ZFS_DEBUG environment variable */
		dprintf_string = getenv("ZFS_DEBUG");
	}

	/*
	 * Are we just turning on all debugging?
	 */
	if (dprintf_find_string("on"))
		dprintf_print_all = 1;

	if (dprintf_string != NULL)
		zfs_flags |= ZFS_DEBUG_DPRINTF;
}

/*
 * =========================================================================
 * debug printfs
 * =========================================================================
 */
void
__dprintf(const char *file, const char *func, int line, const char *fmt, ...)
{
	const char *newfile;
	va_list adx;

	/*
	 * Get rid of annoying "../common/" prefix to filename.
	 */
	newfile = strrchr(file, '/');
	if (newfile != NULL) {
		newfile = newfile + 1; /* Get rid of leading / */
	} else {
		newfile = file;
	}

	if (dprintf_print_all ||
	    dprintf_find_string(newfile) ||
	    dprintf_find_string(func)) {
		/* Print out just the function name if requested */
		flockfile(stdout);
		if (dprintf_find_string("pid"))
			(void) printf("%d ", getpid());
		if (dprintf_find_string("tid"))
			(void) printf("%u ", (uint_t) pthread_self());
		if (dprintf_find_string("cpu"))
			(void) printf("%u ", getcpuid());
		if (dprintf_find_string("time"))
			(void) printf("%llu ", gethrtime());
		if (dprintf_find_string("long"))
			(void) printf("%s, line %d: ", newfile, line);
		(void) printf("%s: ", func);
		va_start(adx, fmt);
		(void) vprintf(fmt, adx);
		va_end(adx);
		funlockfile(stdout);
	}
}

/*
 * =========================================================================
 * cmn_err() and panic()
 * =========================================================================
 */
static char ce_prefix[CE_IGNORE][10] = { "", "NOTICE: ", "WARNING: ", "" };
static char ce_suffix[CE_IGNORE][2] = { "", "\n", "\n", "" };

void
vpanic(const char *fmt, va_list adx)
{
	(void) fprintf(stderr, "error: ");
	(void) vfprintf(stderr, fmt, adx);
	(void) fprintf(stderr, "\n");

	abort();	/* think of it as a "user-level crash dump" */
}

void
panic(const char *fmt, ...)
{
	va_list adx;

	va_start(adx, fmt);
	vpanic(fmt, adx);
	va_end(adx);
}

void
vcmn_err(int ce, const char *fmt, va_list adx)
{
	if (ce == CE_PANIC)
		vpanic(fmt, adx);
	if (ce != CE_NOTE) {	/* suppress noise in userland stress testing */
		(void) fprintf(stderr, "%s", ce_prefix[ce]);
		(void) vfprintf(stderr, fmt, adx);
		(void) fprintf(stderr, "%s", ce_suffix[ce]);
	}
}

/*PRINTFLIKE2*/
void
cmn_err(int ce, const char *fmt, ...)
{
	va_list adx;

	va_start(adx, fmt);
	vcmn_err(ce, fmt, adx);
	va_end(adx);
}

/*
 * =========================================================================
 * kobj interfaces
 * =========================================================================
 */
struct _buf *
kobj_open_file(char *name)
{
	struct _buf *file;
	vnode_t *vp;

	/* set vp as the _fd field of the file */
	if (vn_openat(name, UIO_SYSSPACE, FREAD, 0, &vp, 0, 0, rootdir,
	    -1) != 0)
		return ((void *)-1UL);

	file = umem_zalloc(sizeof (struct _buf), UMEM_NOFAIL);
	file->_fd = (intptr_t)vp;
	return (file);
}

int
kobj_read_file(struct _buf *file, char *buf, unsigned size, unsigned off)
{
	ssize_t resid;

	vn_rdwr(UIO_READ, (vnode_t *)file->_fd, buf, size, (offset_t)off,
	    UIO_SYSSPACE, 0, 0, 0, &resid);

	return (size - resid);
}

void
kobj_close_file(struct _buf *file)
{
	vn_close((vnode_t *)file->_fd);
	umem_free(file, sizeof (struct _buf));
}

int
kobj_get_filesize(struct _buf *file, uint64_t *size)
{
	struct stat64 st;
	vnode_t *vp = (vnode_t *)file->_fd;

	if (fstat64(vp->v_fd, &st) == -1) {
		vn_close(vp);
		return (errno);
	}
	*size = st.st_size;
	return (0);
}

/*
 * =========================================================================
 * misc routines
 * =========================================================================
 */

void
delay(clock_t ticks)
{
	poll(0, 0, ticks * (1000 / hz));
}

/*
 * Find highest one bit set.
 *	Returns bit number + 1 of highest bit that is set, otherwise returns 0.
 * High order bit is 31 (or 63 in _LP64 kernel).
 */
int
highbit64(uint64_t i)
{
	register int h = 1;

	if (i == 0)
		return (0);
	if (i & 0xffffffff00000000ULL) {
		h += 32; i >>= 32;
	}
	if (i & 0xffff0000) {
		h += 16; i >>= 16;
	}
	if (i & 0xff00) {
		h += 8; i >>= 8;
	}
	if (i & 0xf0) {
		h += 4; i >>= 4;
	}
	if (i & 0xc) {
		h += 2; i >>= 2;
	}
	if (i & 0x2) {
		h += 1;
	}
	return (h);
}

static int random_fd = -1, urandom_fd = -1;

static int
random_get_bytes_common(uint8_t *ptr, size_t len, int fd)
{
	size_t resid = len;
	ssize_t bytes;

	ASSERT(fd != -1);

	while (resid != 0) {
		bytes = read(fd, ptr, resid);
		ASSERT3S(bytes, >=, 0);
		ptr += bytes;
		resid -= bytes;
	}

	return (0);
}

int
random_get_bytes(uint8_t *ptr, size_t len)
{
	return (random_get_bytes_common(ptr, len, random_fd));
}

int
random_get_pseudo_bytes(uint8_t *ptr, size_t len)
{
	return (random_get_bytes_common(ptr, len, urandom_fd));
}

int
ddi_strtoul(const char *hw_serial, char **nptr, int base, unsigned long *result)
{
	char *end;

	*result = strtoul(hw_serial, &end, base);
	if (*result == 0)
		return (errno);
	return (0);
}

int
ddi_strtoull(const char *str, char **nptr, int base, u_longlong_t *result)
{
	char *end;

	*result = strtoull(str, &end, base);
	if (*result == 0)
		return (errno);
	return (0);
}

utsname_t *
utsname(void)
{
	return (&hw_utsname);
}

/*
 * =========================================================================
 * kernel emulation setup & teardown
 * =========================================================================
 */
static int
umem_out_of_memory(void)
{
	char errmsg[] = "out of memory -- generating core dump\n";

	(void) fprintf(stderr, "%s", errmsg);
	abort();
	return (0);
}

static unsigned long
get_spl_hostid(void)
{
	FILE *f;
	unsigned long hostid;

	f = fopen("/sys/module/spl/parameters/spl_hostid", "r");
	if (!f)
		return (0);
	if (fscanf(f, "%lu", &hostid) != 1)
		hostid = 0;
	fclose(f);
	return (hostid & 0xffffffff);
}

unsigned long
get_system_hostid(void)
{
	unsigned long system_hostid = get_spl_hostid();
	if (system_hostid == 0)
		system_hostid = gethostid() & 0xffffffff;
	return (system_hostid);
}

void
kernel_init(int mode)
{
	extern uint_t rrw_tsd_key;

	umem_nofail_callback(umem_out_of_memory);

	physmem = sysconf(_SC_PHYS_PAGES);

	dprintf("physmem = %llu pages (%.2f GB)\n", physmem,
	    (double)physmem * sysconf(_SC_PAGE_SIZE) / (1ULL << 30));

	(void) snprintf(hw_serial, sizeof (hw_serial), "%ld",
	    (mode & FWRITE) ? get_system_hostid() : 0);

	VERIFY((random_fd = open("/dev/random", O_RDONLY)) != -1);
	VERIFY((urandom_fd = open("/dev/urandom", O_RDONLY)) != -1);
	VERIFY0(uname(&hw_utsname));

	thread_init();
	system_taskq_init();

	spa_init(mode);

	tsd_create(&rrw_tsd_key, rrw_tsd_destroy);
}

void
kernel_fini(void)
{
	spa_fini();

	system_taskq_fini();
	thread_fini();

	close(random_fd);
	close(urandom_fd);

	random_fd = -1;
	urandom_fd = -1;
}

uid_t
crgetuid(cred_t *cr)
{
	return (0);
}

uid_t
crgetruid(cred_t *cr)
{
	return (0);
}

gid_t
crgetgid(cred_t *cr)
{
	return (0);
}

int
crgetngroups(cred_t *cr)
{
	return (0);
}

gid_t *
crgetgroups(cred_t *cr)
{
	return (NULL);
}

int
zfs_secpolicy_snapshot_perms(const char *name, cred_t *cr)
{
	return (0);
}

int
zfs_secpolicy_rename_perms(const char *from, const char *to, cred_t *cr)
{
	return (0);
}

int
zfs_secpolicy_destroy_perms(const char *name, cred_t *cr)
{
	return (0);
}

ksiddomain_t *
ksid_lookupdomain(const char *dom)
{
	ksiddomain_t *kd;

	kd = umem_zalloc(sizeof (ksiddomain_t), UMEM_NOFAIL);
	kd->kd_name = spa_strdup(dom);
	return (kd);
}

void
ksiddomain_rele(ksiddomain_t *ksid)
{
	spa_strfree(ksid->kd_name);
	umem_free(ksid, sizeof (ksiddomain_t));
}

char *
kmem_vasprintf(const char *fmt, va_list adx)
{
	char *buf = NULL;
	va_list adx_copy;

	va_copy(adx_copy, adx);
	VERIFY(vasprintf(&buf, fmt, adx_copy) != -1);
	va_end(adx_copy);

	return (buf);
}

char *
kmem_asprintf(const char *fmt, ...)
{
	char *buf = NULL;
	va_list adx;

	va_start(adx, fmt);
	VERIFY(vasprintf(&buf, fmt, adx) != -1);
	va_end(adx);

	return (buf);
}

/* ARGSUSED */
int
zfs_onexit_fd_hold(int fd, minor_t *minorp)
{
	*minorp = 0;
	return (0);
}

/* ARGSUSED */
void
zfs_onexit_fd_rele(int fd)
{
}

/* ARGSUSED */
int
zfs_onexit_add_cb(minor_t minor, void (*func)(void *), void *data,
    uint64_t *action_handle)
{
	return (0);
}

/* ARGSUSED */
int
zfs_onexit_del_cb(minor_t minor, uint64_t action_handle, boolean_t fire)
{
	return (0);
}

/* ARGSUSED */
int
zfs_onexit_cb_data(minor_t minor, uint64_t action_handle, void **data)
{
	return (0);
}

fstrans_cookie_t
spl_fstrans_mark(void)
{
	return ((fstrans_cookie_t) 0);
}

void
spl_fstrans_unmark(fstrans_cookie_t cookie)
{
}

int
spl_fstrans_check(void)
{
	return (0);
}
