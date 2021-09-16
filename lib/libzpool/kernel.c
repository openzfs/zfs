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
 * Copyright (c) 2012, 2018 by Delphix. All rights reserved.
 * Copyright (c) 2016 Actifio, Inc. All rights reserved.
 */

#include <assert.h>
#include <fcntl.h>
#include <libgen.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/crypto/icp.h>
#include <sys/processor.h>
#include <sys/rrwlock.h>
#include <sys/spa.h>
#include <sys/stat.h>
#include <sys/systeminfo.h>
#include <sys/time.h>
#include <sys/utsname.h>
#include <sys/zfs_context.h>
#include <sys/zfs_onexit.h>
#include <sys/zfs_vfsops.h>
#include <sys/zstd/zstd.h>
#include <sys/zvol.h>
#include <zfs_fletcher.h>
#include <zlib.h>

/*
 * Emulation of kernel services in userland.
 */

uint64_t physmem;
char hw_serial[HW_HOSTID_LEN];
struct utsname hw_utsname;

/* If set, all blocks read will be copied to the specified directory. */
char *vn_dumpdir = NULL;

/* this only exists to have its address taken */
struct proc p0;

/*
 * =========================================================================
 * threads
 * =========================================================================
 *
 * TS_STACK_MIN is dictated by the minimum allowed pthread stack size.  While
 * TS_STACK_MAX is somewhat arbitrary, it was selected to be large enough for
 * the expected stack depth while small enough to avoid exhausting address
 * space with high thread counts.
 */
#define	TS_STACK_MIN	MAX(PTHREAD_STACK_MIN, 32768)
#define	TS_STACK_MAX	(256 * 1024)

/*ARGSUSED*/
kthread_t *
zk_thread_create(void (*func)(void *), void *arg, size_t stksize, int state)
{
	pthread_attr_t attr;
	pthread_t tid;
	char *stkstr;
	int detachstate = PTHREAD_CREATE_DETACHED;

	VERIFY0(pthread_attr_init(&attr));

	if (state & TS_JOINABLE)
		detachstate = PTHREAD_CREATE_JOINABLE;

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

	/*
	 * If this ever fails, it may be because the stack size is not a
	 * multiple of system page size.
	 */
	VERIFY0(pthread_attr_setstacksize(&attr, stksize));
	VERIFY0(pthread_attr_setguardsize(&attr, PAGESIZE));

	VERIFY0(pthread_create(&tid, &attr, (void *(*)(void *))func, arg));
	VERIFY0(pthread_attr_destroy(&attr));

	return ((void *)(uintptr_t)tid);
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
	VERIFY0(pthread_mutex_init(&mp->m_lock, NULL));
	memset(&mp->m_owner, 0, sizeof (pthread_t));
}

void
mutex_destroy(kmutex_t *mp)
{
	VERIFY0(pthread_mutex_destroy(&mp->m_lock));
}

void
mutex_enter(kmutex_t *mp)
{
	VERIFY0(pthread_mutex_lock(&mp->m_lock));
	mp->m_owner = pthread_self();
}

int
mutex_tryenter(kmutex_t *mp)
{
	int error;

	error = pthread_mutex_trylock(&mp->m_lock);
	if (error == 0) {
		mp->m_owner = pthread_self();
		return (1);
	} else {
		VERIFY3S(error, ==, EBUSY);
		return (0);
	}
}

void
mutex_exit(kmutex_t *mp)
{
	memset(&mp->m_owner, 0, sizeof (pthread_t));
	VERIFY0(pthread_mutex_unlock(&mp->m_lock));
}

/*
 * =========================================================================
 * rwlocks
 * =========================================================================
 */

void
rw_init(krwlock_t *rwlp, char *name, int type, void *arg)
{
	VERIFY0(pthread_rwlock_init(&rwlp->rw_lock, NULL));
	rwlp->rw_readers = 0;
	rwlp->rw_owner = 0;
}

void
rw_destroy(krwlock_t *rwlp)
{
	VERIFY0(pthread_rwlock_destroy(&rwlp->rw_lock));
}

void
rw_enter(krwlock_t *rwlp, krw_t rw)
{
	if (rw == RW_READER) {
		VERIFY0(pthread_rwlock_rdlock(&rwlp->rw_lock));
		atomic_inc_uint(&rwlp->rw_readers);
	} else {
		VERIFY0(pthread_rwlock_wrlock(&rwlp->rw_lock));
		rwlp->rw_owner = pthread_self();
	}
}

void
rw_exit(krwlock_t *rwlp)
{
	if (RW_READ_HELD(rwlp))
		atomic_dec_uint(&rwlp->rw_readers);
	else
		rwlp->rw_owner = 0;

	VERIFY0(pthread_rwlock_unlock(&rwlp->rw_lock));
}

int
rw_tryenter(krwlock_t *rwlp, krw_t rw)
{
	int error;

	if (rw == RW_READER)
		error = pthread_rwlock_tryrdlock(&rwlp->rw_lock);
	else
		error = pthread_rwlock_trywrlock(&rwlp->rw_lock);

	if (error == 0) {
		if (rw == RW_READER)
			atomic_inc_uint(&rwlp->rw_readers);
		else
			rwlp->rw_owner = pthread_self();

		return (1);
	}

	VERIFY3S(error, ==, EBUSY);

	return (0);
}

/* ARGSUSED */
uint32_t
zone_get_hostid(void *zonep)
{
	/*
	 * We're emulating the system's hostid in userland.
	 */
	return (strtoul(hw_serial, NULL, 10));
}

int
rw_tryupgrade(krwlock_t *rwlp)
{
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
	VERIFY0(pthread_cond_init(cv, NULL));
}

void
cv_destroy(kcondvar_t *cv)
{
	VERIFY0(pthread_cond_destroy(cv));
}

void
cv_wait(kcondvar_t *cv, kmutex_t *mp)
{
	memset(&mp->m_owner, 0, sizeof (pthread_t));
	VERIFY0(pthread_cond_wait(cv, &mp->m_lock));
	mp->m_owner = pthread_self();
}

int
cv_wait_sig(kcondvar_t *cv, kmutex_t *mp)
{
	cv_wait(cv, mp);
	return (1);
}

int
cv_timedwait(kcondvar_t *cv, kmutex_t *mp, clock_t abstime)
{
	int error;
	struct timeval tv;
	struct timespec ts;
	clock_t delta;

	delta = abstime - ddi_get_lbolt();
	if (delta <= 0)
		return (-1);

	VERIFY(gettimeofday(&tv, NULL) == 0);

	ts.tv_sec = tv.tv_sec + delta / hz;
	ts.tv_nsec = tv.tv_usec * NSEC_PER_USEC + (delta % hz) * (NANOSEC / hz);
	if (ts.tv_nsec >= NANOSEC) {
		ts.tv_sec++;
		ts.tv_nsec -= NANOSEC;
	}

	memset(&mp->m_owner, 0, sizeof (pthread_t));
	error = pthread_cond_timedwait(cv, &mp->m_lock, &ts);
	mp->m_owner = pthread_self();

	if (error == ETIMEDOUT)
		return (-1);

	VERIFY0(error);

	return (1);
}

/*ARGSUSED*/
int
cv_timedwait_hires(kcondvar_t *cv, kmutex_t *mp, hrtime_t tim, hrtime_t res,
    int flag)
{
	int error;
	struct timeval tv;
	struct timespec ts;
	hrtime_t delta;

	ASSERT(flag == 0 || flag == CALLOUT_FLAG_ABSOLUTE);

	delta = tim;
	if (flag & CALLOUT_FLAG_ABSOLUTE)
		delta -= gethrtime();

	if (delta <= 0)
		return (-1);

	VERIFY0(gettimeofday(&tv, NULL));

	ts.tv_sec = tv.tv_sec + delta / NANOSEC;
	ts.tv_nsec = tv.tv_usec * NSEC_PER_USEC + (delta % NANOSEC);
	if (ts.tv_nsec >= NANOSEC) {
		ts.tv_sec++;
		ts.tv_nsec -= NANOSEC;
	}

	memset(&mp->m_owner, 0, sizeof (pthread_t));
	error = pthread_cond_timedwait(cv, &mp->m_lock, &ts);
	mp->m_owner = pthread_self();

	if (error == ETIMEDOUT)
		return (-1);

	VERIFY0(error);

	return (1);
}

void
cv_signal(kcondvar_t *cv)
{
	VERIFY0(pthread_cond_signal(cv));
}

void
cv_broadcast(kcondvar_t *cv)
{
	VERIFY0(pthread_cond_broadcast(cv));
}

/*
 * =========================================================================
 * procfs list
 * =========================================================================
 */

void
seq_printf(struct seq_file *m, const char *fmt, ...)
{}

void
procfs_list_install(const char *module,
    const char *submodule,
    const char *name,
    mode_t mode,
    procfs_list_t *procfs_list,
    int (*show)(struct seq_file *f, void *p),
    int (*show_header)(struct seq_file *f),
    int (*clear)(procfs_list_t *procfs_list),
    size_t procfs_list_node_off)
{
	mutex_init(&procfs_list->pl_lock, NULL, MUTEX_DEFAULT, NULL);
	list_create(&procfs_list->pl_list,
	    procfs_list_node_off + sizeof (procfs_list_node_t),
	    procfs_list_node_off + offsetof(procfs_list_node_t, pln_link));
	procfs_list->pl_next_id = 1;
	procfs_list->pl_node_offset = procfs_list_node_off;
}

void
procfs_list_uninstall(procfs_list_t *procfs_list)
{}

void
procfs_list_destroy(procfs_list_t *procfs_list)
{
	ASSERT(list_is_empty(&procfs_list->pl_list));
	list_destroy(&procfs_list->pl_list);
	mutex_destroy(&procfs_list->pl_lock);
}

#define	NODE_ID(procfs_list, obj) \
		(((procfs_list_node_t *)(((char *)obj) + \
		(procfs_list)->pl_node_offset))->pln_id)

void
procfs_list_add(procfs_list_t *procfs_list, void *p)
{
	ASSERT(MUTEX_HELD(&procfs_list->pl_lock));
	NODE_ID(procfs_list, p) = procfs_list->pl_next_id++;
	list_insert_tail(&procfs_list->pl_list, p);
}

/*
 * =========================================================================
 * vnode operations
 * =========================================================================
 */

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
__dprintf(boolean_t dprint, const char *file, const char *func,
    int line, const char *fmt, ...)
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

	if (dprint) {
		/* dprintf messages are printed immediately */

		if (!dprintf_print_all &&
		    !dprintf_find_string(newfile) &&
		    !dprintf_find_string(func))
			return;

		/* Print out just the function name if requested */
		flockfile(stdout);
		if (dprintf_find_string("pid"))
			(void) printf("%d ", getpid());
		if (dprintf_find_string("tid"))
			(void) printf("%ju ",
			    (uintmax_t)(uintptr_t)pthread_self());
		if (dprintf_find_string("cpu"))
			(void) printf("%u ", getcpuid());
		if (dprintf_find_string("time"))
			(void) printf("%llu ", gethrtime());
		if (dprintf_find_string("long"))
			(void) printf("%s, line %d: ", newfile, line);
		(void) printf("dprintf: %s: ", func);
		va_start(adx, fmt);
		(void) vprintf(fmt, adx);
		va_end(adx);
		funlockfile(stdout);
	} else {
		/* zfs_dbgmsg is logged for dumping later */
		size_t size;
		char *buf;
		int i;

		size = 1024;
		buf = umem_alloc(size, UMEM_NOFAIL);
		i = snprintf(buf, size, "%s:%d:%s(): ", newfile, line, func);

		if (i < size) {
			va_start(adx, fmt);
			(void) vsnprintf(buf + i, size - i, fmt, adx);
			va_end(adx);
		}

		__zfs_dbgmsg(buf);

		umem_free(buf, size);
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
 * misc routines
 * =========================================================================
 */

void
delay(clock_t ticks)
{
	(void) poll(0, 0, ticks * (1000 / hz));
}

/*
 * Find highest one bit set.
 * Returns bit number + 1 of highest bit that is set, otherwise returns 0.
 * The __builtin_clzll() function is supported by both GCC and Clang.
 */
int
highbit64(uint64_t i)
{
	if (i == 0)
	return (0);

	return (NBBY * sizeof (uint64_t) - __builtin_clzll(i));
}

/*
 * Find lowest one bit set.
 * Returns bit number + 1 of lowest bit that is set, otherwise returns 0.
 * The __builtin_ffsll() function is supported by both GCC and Clang.
 */
int
lowbit64(uint64_t i)
{
	if (i == 0)
		return (0);

	return (__builtin_ffsll(i));
}

const char *random_path = "/dev/random";
const char *urandom_path = "/dev/urandom";
static int random_fd = -1, urandom_fd = -1;

void
random_init(void)
{
	VERIFY((random_fd = open(random_path, O_RDONLY | O_CLOEXEC)) != -1);
	VERIFY((urandom_fd = open(urandom_path, O_RDONLY | O_CLOEXEC)) != -1);
}

void
random_fini(void)
{
	close(random_fd);
	close(urandom_fd);

	random_fd = -1;
	urandom_fd = -1;
}

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

void
kernel_init(int mode)
{
	extern uint_t rrw_tsd_key;

	umem_nofail_callback(umem_out_of_memory);

	physmem = sysconf(_SC_PHYS_PAGES);

	dprintf("physmem = %llu pages (%.2f GB)\n", (u_longlong_t)physmem,
	    (double)physmem * sysconf(_SC_PAGE_SIZE) / (1ULL << 30));

	(void) snprintf(hw_serial, sizeof (hw_serial), "%ld",
	    (mode & SPA_MODE_WRITE) ? get_system_hostid() : 0);

	random_init();

	VERIFY0(uname(&hw_utsname));

	system_taskq_init();
	icp_init();

	zstd_init();

	spa_init((spa_mode_t)mode);

	fletcher_4_init();

	tsd_create(&rrw_tsd_key, rrw_tsd_destroy);
}

void
kernel_fini(void)
{
	fletcher_4_fini();
	spa_fini();

	zstd_fini();

	icp_fini();
	system_taskq_fini();

	random_fini();
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

int
secpolicy_zfs(const cred_t *cr)
{
	return (0);
}

int
secpolicy_zfs_proc(const cred_t *cr, proc_t *proc)
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
zfs_file_t *
zfs_onexit_fd_hold(int fd, minor_t *minorp)
{
	*minorp = 0;
	return (NULL);
}

/* ARGSUSED */
void
zfs_onexit_fd_rele(zfs_file_t *fp)
{
}

/* ARGSUSED */
int
zfs_onexit_add_cb(minor_t minor, void (*func)(void *), void *data,
    uint64_t *action_handle)
{
	return (0);
}

fstrans_cookie_t
spl_fstrans_mark(void)
{
	return ((fstrans_cookie_t)0);
}

void
spl_fstrans_unmark(fstrans_cookie_t cookie)
{
}

int
__spl_pf_fstrans_check(void)
{
	return (0);
}

int
kmem_cache_reap_active(void)
{
	return (0);
}

void *zvol_tag = "zvol_tag";

void
zvol_create_minor(const char *name)
{
}

void
zvol_create_minors_recursive(const char *name)
{
}

void
zvol_remove_minors(spa_t *spa, const char *name, boolean_t async)
{
}

void
zvol_rename_minors(spa_t *spa, const char *oldname, const char *newname,
    boolean_t async)
{
}

/*
 * Open file
 *
 * path - fully qualified path to file
 * flags - file attributes O_READ / O_WRITE / O_EXCL
 * fpp - pointer to return file pointer
 *
 * Returns 0 on success underlying error on failure.
 */
int
zfs_file_open(const char *path, int flags, int mode, zfs_file_t **fpp)
{
	int fd = -1;
	int dump_fd = -1;
	int err;
	int old_umask = 0;
	zfs_file_t *fp;
	struct stat64 st;

	if (!(flags & O_CREAT) && stat64(path, &st) == -1)
		return (errno);

	if (!(flags & O_CREAT) && S_ISBLK(st.st_mode))
		flags |= O_DIRECT;

	if (flags & O_CREAT)
		old_umask = umask(0);

	fd = open64(path, flags, mode);
	if (fd == -1)
		return (errno);

	if (flags & O_CREAT)
		(void) umask(old_umask);

	if (vn_dumpdir != NULL) {
		char *dumppath = umem_zalloc(MAXPATHLEN, UMEM_NOFAIL);
		char *inpath = basename((char *)(uintptr_t)path);

		(void) snprintf(dumppath, MAXPATHLEN,
		    "%s/%s", vn_dumpdir, inpath);
		dump_fd = open64(dumppath, O_CREAT | O_WRONLY, 0666);
		umem_free(dumppath, MAXPATHLEN);
		if (dump_fd == -1) {
			err = errno;
			close(fd);
			return (err);
		}
	} else {
		dump_fd = -1;
	}

	(void) fcntl(fd, F_SETFD, FD_CLOEXEC);

	fp = umem_zalloc(sizeof (zfs_file_t), UMEM_NOFAIL);
	fp->f_fd = fd;
	fp->f_dump_fd = dump_fd;
	*fpp = fp;

	return (0);
}

void
zfs_file_close(zfs_file_t *fp)
{
	close(fp->f_fd);
	if (fp->f_dump_fd != -1)
		close(fp->f_dump_fd);

	umem_free(fp, sizeof (zfs_file_t));
}

/*
 * Stateful write - use os internal file pointer to determine where to
 * write and update on successful completion.
 *
 * fp -  pointer to file (pipe, socket, etc) to write to
 * buf - buffer to write
 * count - # of bytes to write
 * resid -  pointer to count of unwritten bytes  (if short write)
 *
 * Returns 0 on success errno on failure.
 */
int
zfs_file_write(zfs_file_t *fp, const void *buf, size_t count, ssize_t *resid)
{
	ssize_t rc;

	rc = write(fp->f_fd, buf, count);
	if (rc < 0)
		return (errno);

	if (resid) {
		*resid = count - rc;
	} else if (rc != count) {
		return (EIO);
	}

	return (0);
}

/*
 * Stateless write - os internal file pointer is not updated.
 *
 * fp -  pointer to file (pipe, socket, etc) to write to
 * buf - buffer to write
 * count - # of bytes to write
 * off - file offset to write to (only valid for seekable types)
 * resid -  pointer to count of unwritten bytes
 *
 * Returns 0 on success errno on failure.
 */
int
zfs_file_pwrite(zfs_file_t *fp, const void *buf,
    size_t count, loff_t pos, ssize_t *resid)
{
	ssize_t rc, split, done;
	int sectors;

	/*
	 * To simulate partial disk writes, we split writes into two
	 * system calls so that the process can be killed in between.
	 * This is used by ztest to simulate realistic failure modes.
	 */
	sectors = count >> SPA_MINBLOCKSHIFT;
	split = (sectors > 0 ? rand() % sectors : 0) << SPA_MINBLOCKSHIFT;
	rc = pwrite64(fp->f_fd, buf, split, pos);
	if (rc != -1) {
		done = rc;
		rc = pwrite64(fp->f_fd, (char *)buf + split,
		    count - split, pos + split);
	}
#ifdef __linux__
	if (rc == -1 && errno == EINVAL) {
		/*
		 * Under Linux, this most likely means an alignment issue
		 * (memory or disk) due to O_DIRECT, so we abort() in order
		 * to catch the offender.
		 */
		abort();
	}
#endif

	if (rc < 0)
		return (errno);

	done += rc;

	if (resid) {
		*resid = count - done;
	} else if (done != count) {
		return (EIO);
	}

	return (0);
}

/*
 * Stateful read - use os internal file pointer to determine where to
 * read and update on successful completion.
 *
 * fp -  pointer to file (pipe, socket, etc) to read from
 * buf - buffer to write
 * count - # of bytes to read
 * resid -  pointer to count of unread bytes (if short read)
 *
 * Returns 0 on success errno on failure.
 */
int
zfs_file_read(zfs_file_t *fp, void *buf, size_t count, ssize_t *resid)
{
	int rc;

	rc = read(fp->f_fd, buf, count);
	if (rc < 0)
		return (errno);

	if (resid) {
		*resid = count - rc;
	} else if (rc != count) {
		return (EIO);
	}

	return (0);
}

/*
 * Stateless read - os internal file pointer is not updated.
 *
 * fp -  pointer to file (pipe, socket, etc) to read from
 * buf - buffer to write
 * count - # of bytes to write
 * off - file offset to read from (only valid for seekable types)
 * resid -  pointer to count of unwritten bytes (if short write)
 *
 * Returns 0 on success errno on failure.
 */
int
zfs_file_pread(zfs_file_t *fp, void *buf, size_t count, loff_t off,
    ssize_t *resid)
{
	ssize_t rc;

	rc = pread64(fp->f_fd, buf, count, off);
	if (rc < 0) {
#ifdef __linux__
		/*
		 * Under Linux, this most likely means an alignment issue
		 * (memory or disk) due to O_DIRECT, so we abort() in order to
		 * catch the offender.
		 */
		if (errno == EINVAL)
			abort();
#endif
		return (errno);
	}

	if (fp->f_dump_fd != -1) {
		int status;

		status = pwrite64(fp->f_dump_fd, buf, rc, off);
		ASSERT(status != -1);
	}

	if (resid) {
		*resid = count - rc;
	} else if (rc != count) {
		return (EIO);
	}

	return (0);
}

/*
 * lseek - set / get file pointer
 *
 * fp -  pointer to file (pipe, socket, etc) to read from
 * offp - value to seek to, returns current value plus passed offset
 * whence - see man pages for standard lseek whence values
 *
 * Returns 0 on success errno on failure (ESPIPE for non seekable types)
 */
int
zfs_file_seek(zfs_file_t *fp, loff_t *offp, int whence)
{
	loff_t rc;

	rc = lseek(fp->f_fd, *offp, whence);
	if (rc < 0)
		return (errno);

	*offp = rc;

	return (0);
}

/*
 * Get file attributes
 *
 * filp - file pointer
 * zfattr - pointer to file attr structure
 *
 * Currently only used for fetching size and file mode
 *
 * Returns 0 on success or error code of underlying getattr call on failure.
 */
int
zfs_file_getattr(zfs_file_t *fp, zfs_file_attr_t *zfattr)
{
	struct stat64 st;

	if (fstat64_blk(fp->f_fd, &st) == -1)
		return (errno);

	zfattr->zfa_size = st.st_size;
	zfattr->zfa_mode = st.st_mode;

	return (0);
}

/*
 * Sync file to disk
 *
 * filp - file pointer
 * flags - O_SYNC and or O_DSYNC
 *
 * Returns 0 on success or error code of underlying sync call on failure.
 */
int
zfs_file_fsync(zfs_file_t *fp, int flags)
{
	int rc;

	rc = fsync(fp->f_fd);
	if (rc < 0)
		return (errno);

	return (0);
}

/*
 * fallocate - allocate or free space on disk
 *
 * fp - file pointer
 * mode (non-standard options for hole punching etc)
 * offset - offset to start allocating or freeing from
 * len - length to free / allocate
 *
 * OPTIONAL
 */
int
zfs_file_fallocate(zfs_file_t *fp, int mode, loff_t offset, loff_t len)
{
#ifdef __linux__
	return (fallocate(fp->f_fd, mode, offset, len));
#else
	return (EOPNOTSUPP);
#endif
}

/*
 * Request current file pointer offset
 *
 * fp - pointer to file
 *
 * Returns current file offset.
 */
loff_t
zfs_file_off(zfs_file_t *fp)
{
	return (lseek(fp->f_fd, SEEK_CUR, 0));
}

/*
 * unlink file
 *
 * path - fully qualified file path
 *
 * Returns 0 on success.
 *
 * OPTIONAL
 */
int
zfs_file_unlink(const char *path)
{
	return (remove(path));
}

/*
 * Get reference to file pointer
 *
 * fd - input file descriptor
 *
 * Returns pointer to file struct or NULL.
 * Unsupported in user space.
 */
zfs_file_t *
zfs_file_get(int fd)
{
	abort();

	return (NULL);
}
/*
 * Drop reference to file pointer
 *
 * fp - pointer to file struct
 *
 * Unsupported in user space.
 */
void
zfs_file_put(zfs_file_t *fp)
{
	abort();
}

void
zfsvfs_update_fromname(const char *oldname, const char *newname)
{
}
