// SPDX-License-Identifier: CDDL-1.0
/*
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this
 * source.  A copy of the CDDL is also available via the Internet at
 * https://opensource.org/license/CDDL-1.0.
 */

/*
 * Race readers against writers on one file and check every read
 * against the states the file was in while the read ran.  Each 8-byte
 * word holds a generation in its high half and its position in its
 * low half, so a read that mixes two writes, or returns zeros or data
 * from another offset, matches no state.
 *
 * A read overlaps a writer when both calls were in flight at once on
 * ranges that meet.  Each mode counts the reads that did, stops after
 * -n of them or -t seconds, and fails if there were none.  A call
 * that runs longer than -H seconds is reported as a hang, with kernel
 * stacks on Linux.
 *
 * usage: rangelock_race [-pDe] [-t secs] [-n count] [-H secs]
 *     [-r readers] [-w writers] [-f files] mode path
 *
 *   regions	writers rewrite 128K regions of path; readers read
 *		whole regions, parts of one, or across two
 *   clone	writers clone regions from path.a, path.b and other
 *		regions of path into the regions read
 *   dedupe	as clone, with FIDEDUPERANGE (Linux)
 *   append	O_APPEND writers add 256K records to new files in
 *		the directory path; readers read near the end
 *   truncate	one thread truncates path down, back up, and
 *		rewrites the tail; readers read across the cut
 *   blksz	a 4K file in the directory path is extended past
 *		its first block, growing its block size, while
 *		readers read it; repeated on -f files
 *   mmap	readers read path into a shared mapping of path
 *		while other threads dirty the mapping (Linux); the
 *		race is a read throttled in balance_dirty_pages()
 *   cold	one writer rewrites the first 4M of path with one
 *		pwrite; readers read all of it into a new mapping
 *		with only its first 64K resident, and a read counts
 *		as an overlap only if no write was in flight when
 *		it began and one began during it
 *
 *   -p	create the files regions, clone, dedupe and blksz start
 *	from, and exit; sync the pool before the race
 *   -D	regions writers use O_DIRECT
 *   -e	blksz uses the files -p created instead of new ones
 *
 * Exit status: 0 pass, 1 a read matched no state, 2 error, 3 hang,
 * 4 no read overlapped a writer, 5 the race cannot be observed.
 */

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <sched.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/types.h>
#ifdef __linux__
#include <sys/syscall.h>
#endif
#ifdef __FreeBSD__
#include <pthread_np.h>
#endif

/*
 * Not all libcs define the types we need for the ioctl-based clone
 * calls, so define any that are missing directly.
 */

/* Define missing FICLONERANGE and support structs */
#ifdef FICLONERANGE
#define	CF_FICLONERANGE	FICLONERANGE
typedef struct file_clone_range cf_file_clone_range_t;
#else
typedef struct {
	int64_t		src_fd;
	uint64_t	src_offset;
	uint64_t	src_length;
	uint64_t	dest_offset;
} cf_file_clone_range_t;
#define	CF_FICLONERANGE	_IOW(0x94, 13, cf_file_clone_range_t)
#endif

/* Define missing FIDEDUPERANGE and support structs */
#ifdef FIDEDUPERANGE
#define	CF_FIDEDUPERANGE		FIDEDUPERANGE
#define	CF_FILE_DEDUPE_RANGE_SAME	FILE_DEDUPE_RANGE_SAME
#define	CF_FILE_DEDUPE_RANGE_DIFFERS	FILE_DEDUPE_RANGE_DIFFERS
typedef struct file_dedupe_range_info	cf_file_dedupe_range_info_t;
typedef struct file_dedupe_range	cf_file_dedupe_range_t;
#else
typedef struct {
	int64_t dest_fd;
	uint64_t dest_offset;
	uint64_t bytes_deduped;
	int32_t status;
	uint32_t reserved;
} cf_file_dedupe_range_info_t;
typedef struct {
	uint64_t src_offset;
	uint64_t src_length;
	uint16_t dest_count;
	uint16_t reserved1;
	uint32_t reserved2;
	cf_file_dedupe_range_info_t info[0];
} cf_file_dedupe_range_t;
#define	CF_FIDEDUPERANGE		\
	_IOWR(0x94, 54, cf_file_dedupe_range_t)
#define	CF_FILE_DEDUPE_RANGE_SAME	(0)
#define	CF_FILE_DEDUPE_RANGE_DIFFERS	(1)
#endif

#define	RS		(128 * 1024)
#define	NREG		8
#define	REC		(256 * 1024)
#define	MAXREC		64
#define	T_LO		(3 * RS + 4096 + 1000)
#define	T_HI		(7 * RS + 2560)
#define	T_TAIL		(T_HI - T_LO)
#define	B_BASE		4096
#define	B_GROW		(1024 * 1024)
#define	M_DATA		(8 << 20)
#define	M_SLICE		(1 << 20)
#define	M_DIRTY		(16 << 20)
#define	C_LEN		(4 << 20)
#define	C_WARM		(64 * 1024)
#define	MAXW		64
#define	NSEC		1000000000ULL
#define	MAGIC		0xa5000000ULL

#define	LD(p)	__atomic_load_n((p), __ATOMIC_SEQ_CST)
#define	ST(p, v)	__atomic_store_n((p), (v), __ATOMIC_SEQ_CST)
#define	INC(p)	__atomic_add_fetch((p), 1, __ATOMIC_SEQ_CST)
#define	CAS(p, o, n)	__atomic_compare_exchange_n((p), (o), (n), \
	0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST)

typedef unsigned long long ull_t;

typedef enum {
	M_REGIONS, M_CLONE, M_DEDUPE, M_APPEND, M_TRUNC, M_BLKSZ,
	M_MMAP, M_COLD
} rl_mode_t;

typedef struct worker {
	pthread_t	w_thread;
	void		(*w_fn)(struct worker *);
	const char	*w_role;
	int		w_id;
	long		w_tid;
	uint64_t	w_rng;
	uint64_t	w_busy;
	const char	*w_call;
	int		w_exited;
	uint64_t	w_reads;
	uint64_t	w_writes;
	uint64_t	w_ovl;
	uint64_t	w_ovl2;
	uint64_t	w_old;
	uint64_t	w_new;
	uint64_t	w_aligned;
	uint64_t	w_st[3];
	int		w_post;
	int		w_inread;
	uint64_t	w_rseq;
	uint64_t	w_hitseq;
} worker_t;

static rl_mode_t g_mode;
static const char *g_path;
static int opt_prep, opt_direct, opt_existing;
static int opt_readers = -1, opt_writers = -1, opt_files = 200;
static uint64_t opt_secs = 60, opt_target, opt_hang = 120;

static pthread_mutex_t g_out = PTHREAD_MUTEX_INITIALIZER;
static worker_t g_w[MAXW];
static worker_t g_final = { .w_role = "final check" };
static int g_nw;
static int g_halt, g_stop, g_round_done;
static uint64_t g_failed, g_ovl, g_seed, g_deadline;
static uint64_t t_reads, t_writes, t_ovl2, t_old, t_new, t_aligned;
static uint64_t t_st[3];
static long g_pagesize;

static struct {
	int		fd, wfd, fda, fdb;
	uint64_t	started[NREG];
	uint64_t	done[NREG];
} g_reg;

static struct {
	int		fd, afd;
	uint64_t	started, done;
	uint64_t	mine[MAXW];
} g_app;

static struct {
	int		fd;
	uint64_t	started, done;
} g_tr;

static struct {
	int		fd, grow;
	uint64_t	started, done, ovl;
	worker_t	*readers[MAXW];
} g_bs;

static struct {
	int		fd;
	uint64_t	started, done, faulted;
} g_cd;

#ifdef __linux__
static struct {
	int		fd;
	char		*map;
	uint64_t	hits_zr;
	worker_t	*readers[MAXW];
} g_mm;
#endif

static void
say(const char *fmt, ...)
{
	va_list ap;

	(void) pthread_mutex_lock(&g_out);
	va_start(ap, fmt);
	(void) vprintf(fmt, ap);
	va_end(ap);
	(void) pthread_mutex_unlock(&g_out);
}

static __attribute__((noreturn)) void
die(const char *fmt, ...)
{
	va_list ap;

	(void) pthread_mutex_lock(&g_out);
	va_start(ap, fmt);
	(void) vprintf(fmt, ap);
	va_end(ap);
	(void) printf("result error\n");
	(void) fflush(stdout);
	exit(2);
}

static __attribute__((noreturn)) void
syserr(const char *what)
{
	die("%s: %s\n", what, strerror(errno));
}

static void
bad(worker_t *w, const char *fmt, ...)
{
	va_list ap;

	if (INC(&g_failed) > 10)
		return;
	(void) pthread_mutex_lock(&g_out);
	(void) printf("BAD %s %d: ", w->w_role, w->w_id);
	va_start(ap, fmt);
	(void) vprintf(fmt, ap);
	va_end(ap);
	(void) printf("\n");
	(void) pthread_mutex_unlock(&g_out);
}

static uint64_t
now_ns(void)
{
	struct timespec ts;

	(void) clock_gettime(CLOCK_MONOTONIC, &ts);
	return ((uint64_t)ts.tv_sec * NSEC + ts.tv_nsec);
}

static void
sleep_ms(long ms)
{
	struct timespec ts = { ms / 1000, (ms % 1000) * 1000000 };

	(void) nanosleep(&ts, NULL);
}

static uint64_t
rnd(uint64_t *s)
{
	uint64_t x = *s;

	x ^= x << 13;
	x ^= x >> 7;
	x ^= x << 17;
	*s = x;
	return (x);
}

static long
gettid_(void)
{
#if defined(__linux__)
	return ((long)syscall(SYS_gettid));
#elif defined(__FreeBSD__)
	return ((long)pthread_getthreadid_np());
#else
	return (0);
#endif
}

static void *
xalloc(size_t len)
{
	void *p;

	if (posix_memalign(&p, g_pagesize, len) != 0)
		die("posix_memalign %zu failed\n", len);
	return (p);
}

static int
xopen(const char *path, int flags)
{
	int fd = open(path, flags, 0644);

	if (fd < 0)
		die("open %s: %s\n", path, strerror(errno));
	return (fd);
}

static void
xpwrite(int fd, const void *buf, size_t len, off_t off)
{
	ssize_t n = pwrite(fd, buf, len, off);

	if (n < 0)
		syserr("pwrite");
	if ((size_t)n != len)
		die("pwrite of %zu at %lld wrote %zd\n", len,
		    (long long)off, n);
}

static void
xpread(int fd, void *buf, size_t len, off_t off)
{
	ssize_t n = pread(fd, buf, len, off);

	if (n < 0)
		syserr("pread");
	if ((size_t)n != len)
		die("pread of %zu at %lld read %zd\n", len,
		    (long long)off, n);
}

static uint64_t
stamp(uint64_t gen, uint64_t pos)
{
	return ((gen << 32) | MAGIC | ((pos >> 3) & 0xffffff));
}

static void
fill(void *buf, size_t len, uint64_t gen, uint64_t pos)
{
	uint64_t *w = buf;

	for (size_t i = 0; i < len / 8; i++)
		w[i] = stamp(gen, pos + i * 8);
}

/*
 * Return the generation of the stamps in buf, which holds len bytes
 * from position pos, or -1 if a word is not a stamp of that
 * generation for its position; *badp is then that word's offset.
 */
static int64_t
check(const void *buf, size_t len, uint64_t pos, size_t *badp)
{
	const uint64_t *w = buf;
	uint64_t gen = w[0] >> 32;

	for (size_t i = 0; i < len / 8; i++) {
		if (w[i] != stamp(gen, pos + i * 8)) {
			*badp = i * 8;
			return (-1);
		}
	}
	return ((int64_t)gen);
}

static int
zeros(const void *buf, size_t len)
{
	const uint64_t *w = buf;

	for (size_t i = 0; i < len / 8; i++) {
		if (w[i] != 0)
			return (0);
	}
	return (1);
}

static void
call_begin(worker_t *w, const char *call)
{
	ST(&w->w_call, call);
	ST(&w->w_busy, now_ns());
}

static void
call_end(worker_t *w)
{
	ST(&w->w_busy, 0);
}

static int
halted(void)
{
	return (LD(&g_halt));
}

/*
 * Write to stdout without stdio, whose lock a stuck thread may hold.
 */
static void
hsay(const char *fmt, ...)
{
	char buf[512];
	va_list ap;
	int n;

	va_start(ap, fmt);
	n = vsnprintf(buf, sizeof (buf), fmt, ap);
	va_end(ap);
	if (n > (int)sizeof (buf) - 1)
		n = sizeof (buf) - 1;
	if (n > 0) {
		ssize_t rv = write(STDOUT_FILENO, buf, n);
		(void) rv;
	}
}

#ifdef __linux__
static ssize_t
readfile(const char *path, char *buf, size_t len)
{
	ssize_t n = 0, got = 0;
	int fd = open(path, O_RDONLY);

	if (fd < 0)
		return (-1);
	while (got < (ssize_t)len - 1 &&
	    (n = read(fd, buf + got, len - 1 - got)) > 0)
		got += n;
	(void) close(fd);
	buf[got] = '\0';
	return (n < 0 ? -1 : got);
}

static void
show_stack(const char *path)
{
	char buf[8192];

	if (readfile(path, buf, sizeof (buf)) > 0)
		hsay("%s", buf);
	else
		hsay("  (no stack: %s)\n", strerror(errno));
}

/*
 * If task tid of process pid is in D state, print it and its stack.
 */
static int
show_blocked(const char *pid, const char *tid)
{
	char path[PATH_MAX], buf[512], *e;

	(void) snprintf(path, sizeof (path), "/proc/%s/task/%s/stat",
	    pid, tid);
	if (readfile(path, buf, sizeof (buf)) <= 0 ||
	    (e = strrchr(buf, ')')) == NULL || strlen(e) < 3 ||
	    e[2] != 'D')
		return (0);
	e[1] = '\0';
	hsay("blocked task %s: %s\n", tid, strchr(buf, '('));
	(void) snprintf(path, sizeof (path), "/proc/%s/task/%s/stack",
	    pid, tid);
	show_stack(path);
	return (1);
}

static int
blocked_proc(const char *pid)
{
	char path[PATH_MAX];
	struct dirent *t;
	DIR *td;
	int shown = 0;

	(void) snprintf(path, sizeof (path), "/proc/%s/task", pid);
	if ((td = opendir(path)) == NULL)
		return (0);
	while ((t = readdir(td)) != NULL) {
		if (isdigit((unsigned char)t->d_name[0]))
			shown += show_blocked(pid, t->d_name);
	}
	(void) closedir(td);
	return (shown);
}

/*
 * Print the tasks in D state outside this process, so a hang shows
 * what the stuck calls wait for.
 */
static void
blocked_tasks(void)
{
	struct dirent *p;
	DIR *pd;
	int shown = 0;

	if ((pd = opendir("/proc")) == NULL)
		return;
	while ((p = readdir(pd)) != NULL && shown < 50) {
		if (isdigit((unsigned char)p->d_name[0]) &&
		    atol(p->d_name) != (long)getpid())
			shown += blocked_proc(p->d_name);
	}
	(void) closedir(pd);
}
#endif

static __attribute__((noreturn)) void
hang(void)
{
	uint64_t now = now_ns();

	hsay("hang: a call has run for more than %llu s\n",
	    (ull_t)opt_hang);
	for (int i = 0; i < g_nw; i++) {
		worker_t *w = &g_w[i];
		uint64_t b = LD(&w->w_busy);
		long tid = LD(&w->w_tid);

		if (b == 0)
			continue;
		hsay("%s %d, thread %ld, in %s for %llu ms\n",
		    w->w_role, w->w_id, tid, LD(&w->w_call),
		    (ull_t)((now - b) / 1000000));
#ifdef __linux__
		char path[64];

		(void) snprintf(path, sizeof (path),
		    "/proc/self/task/%ld/stack", tid);
		show_stack(path);
#endif
	}
#ifdef __linux__
	blocked_tasks();
#endif
	hsay("result hang\n");
	/* Leave the caller time to read the stacks of stuck calls. */
	(void) sleep(30);
	_exit(3);
}

static void
check_hang(void)
{
	uint64_t now = now_ns();

	for (int i = 0; i < g_nw; i++) {
		uint64_t b = LD(&g_w[i].w_busy);

		if (b != 0 && now > b && now - b > opt_hang * NSEC)
			hang();
	}
}

static void *
w_main(void *arg)
{
	worker_t *w = arg;

	ST(&w->w_tid, gettid_());
	w->w_fn(w);
	ST(&w->w_exited, 1);
	return (NULL);
}

static worker_t *
spawn(const char *role, int id, void (*fn)(worker_t *))
{
	worker_t *w;
	int err;

	if (g_nw >= MAXW)
		die("too many threads\n");
	w = &g_w[g_nw++];
	(void) memset(w, 0, sizeof (*w));
	w->w_role = role;
	w->w_id = id;
	w->w_fn = fn;
	w->w_rng = g_seed ^ ((uint64_t)g_nw * 0x9e3779b97f4a7c15ULL);
	if (w->w_rng == 0)
		w->w_rng = 1;
	err = pthread_create(&w->w_thread, NULL, w_main, w);
	if (err != 0)
		die("pthread_create: %s\n", strerror(err));
	return (w);
}

static void
tally(worker_t *w)
{
	t_reads += w->w_reads;
	t_writes += w->w_writes;
	t_ovl2 += w->w_ovl2;
	t_old += w->w_old;
	t_new += w->w_new;
	t_aligned += w->w_aligned;
	for (int i = 0; i < 3; i++)
		t_st[i] += w->w_st[i];
}

/*
 * Run the threads of a round until done() says so, enough reads have
 * overlapped a writer (if early), the deadline passes or a read is
 * bad.  Then stop them, watching for hangs while they finish.
 */
static void
run_round(int (*done)(void), int early)
{
	for (;;) {
		sleep_ms(10);
		check_hang();
		if (LD(&g_failed) != 0)
			break;
		if (done != NULL && done())
			break;
		if (early && LD(&g_ovl) >= opt_target)
			break;
		if (now_ns() >= g_deadline) {
			ST(&g_stop, 1);
			break;
		}
	}
	ST(&g_halt, 1);
	for (int i = 0; i < g_nw; i++) {
		while (!LD(&g_w[i].w_exited)) {
			sleep_ms(10);
			check_hang();
		}
	}
	for (int i = 0; i < g_nw; i++) {
		(void) pthread_join(g_w[i].w_thread, NULL);
		tally(&g_w[i]);
	}
	g_nw = 0;
	ST(&g_halt, 0);
}

static int
more_rounds(void)
{
	return (!LD(&g_stop) && LD(&g_failed) == 0 &&
	    LD(&g_ovl) < opt_target && now_ns() < g_deadline);
}

/*
 * regions, clone and dedupe.  A file holds 3 * NREG regions: the hot
 * regions read and written are in the middle, with a low and a high
 * copy on either side that clone and dedupe use as sources within the
 * same file.  path.a and path.b have the same layout.  Positions in
 * the stamps are taken modulo NREG regions, so the copies match.
 *
 * Write j of a hot region leaves generation j for regions, the low
 * copy's 1 (j odd) or the high copy's 2 (j even) for clone, and 1 for
 * dedupe, where all copies hold the same data and only the blocks
 * change.
 */
static uint64_t
reg_gen(uint64_t j)
{
	if (g_mode == M_CLONE)
		return (j == 0 ? 0 : 2 - (j & 1));
	if (g_mode == M_DEDUPE)
		return (1);
	return (j);
}

static int
reg_allowed(uint64_t g, uint64_t d0, uint64_t s1)
{
	if (g_mode == M_REGIONS)
		return (g >= d0 && g <= s1);
	for (uint64_t j = d0; j <= s1 && j < d0 + 3; j++) {
		if (reg_gen(j) == g)
			return (1);
	}
	return (0);
}

static off_t
hot(int r)
{
	return ((off_t)(NREG + r) * RS);
}

static void
reg_file(const char *name, uint64_t lo, uint64_t mid, uint64_t hi)
{
	char *buf = xalloc(RS);
	int fd = xopen(name, O_RDWR | O_CREAT | O_TRUNC);

	for (int i = 0; i < 3 * NREG; i++) {
		uint64_t gen = mid;

		if (i < NREG)
			gen = lo;
		else if (i >= 2 * NREG)
			gen = hi;
		fill(buf, RS, gen, (uint64_t)(i % NREG) * RS);
		xpwrite(fd, buf, RS, (off_t)i * RS);
	}
	if (fsync(fd) != 0)
		syserr("fsync");
	(void) close(fd);
	free(buf);
}

static void
reg_prep(void)
{
	char name[PATH_MAX];

	if (g_mode == M_REGIONS) {
		reg_file(g_path, 0, 0, 0);
		return;
	}
	if (g_mode == M_CLONE)
		reg_file(g_path, 1, 0, 2);
	else
		reg_file(g_path, 1, 1, 1);
	for (int f = 1; f <= 2; f++) {
		uint64_t g = g_mode == M_CLONE ? f : 1;

		(void) snprintf(name, sizeof (name), "%s.%c", g_path,
		    'a' + f - 1);
		reg_file(name, g, g, g);
	}
}

static int
do_clone(int sfd, off_t soff, int dfd, off_t doff)
{
#ifdef __linux__
	cf_file_clone_range_t fcr;

	fcr.src_fd = sfd;
	fcr.src_offset = soff;
	fcr.src_length = RS;
	fcr.dest_offset = doff;
	return (ioctl(dfd, CF_FICLONERANGE, &fcr));
#else
	ssize_t n = copy_file_range(sfd, &soff, dfd, &doff, RS, 0);

	if (n >= 0 && n != RS) {
		say("copy_file_range copied %zd of %d\n", n, RS);
		errno = EIO;
		return (-1);
	}
	return (n < 0 ? -1 : 0);
#endif
}

static int
do_dedupe(int sfd, off_t soff, int dfd, off_t doff)
{
#ifdef __linux__
	union {
		cf_file_dedupe_range_t r;
		uint8_t b[sizeof (cf_file_dedupe_range_t) +
		    sizeof (cf_file_dedupe_range_info_t)];
	} u;

	(void) memset(&u, 0, sizeof (u));
	u.r.src_offset = soff;
	u.r.src_length = RS;
	u.r.dest_count = 1;
	u.r.info[0].dest_fd = dfd;
	u.r.info[0].dest_offset = doff;
	if (ioctl(sfd, CF_FIDEDUPERANGE, &u.r) != 0)
		return (-1);
	if (u.r.info[0].status != CF_FILE_DEDUPE_RANGE_SAME ||
	    u.r.info[0].bytes_deduped != RS) {
		say("FIDEDUPERANGE status %d, %llu bytes\n",
		    u.r.info[0].status,
		    (ull_t)u.r.info[0].bytes_deduped);
		errno = EIO;
		return (-1);
	}
	return (0);
#else
	(void) sfd, (void) soff, (void) dfd, (void) doff;
	errno = ENOTSUP;
	return (-1);
#endif
}

/*
 * Write j of hot region r.  Odd writers of clone and dedupe take the
 * copies in the same file, even ones path.a and path.b.
 */
static int
reg_op(worker_t *w, int r, uint64_t j, char *buf)
{
	int self = w->w_id & 1, odd = j & 1, err;
	int sfd = self ? g_reg.fd : odd ? g_reg.fda : g_reg.fdb;
	off_t soff = hot(r);
	ssize_t n;

	if (self)
		soff = (off_t)(odd ? r : 2 * NREG + r) * RS;
	if (g_mode == M_REGIONS)
		fill(buf, RS, j, (uint64_t)r * RS);
	ST(&g_reg.started[r], j);
	switch (g_mode) {
	case M_REGIONS:
		call_begin(w, "pwrite");
		n = pwrite(g_reg.wfd, buf, RS, hot(r));
		if (n >= 0 && n != RS)
			errno = EIO;
		err = n == RS ? 0 : -1;
		break;
	case M_CLONE:
		call_begin(w, "clone");
		err = do_clone(sfd, soff, g_reg.fd, hot(r));
		break;
	default:
		call_begin(w, "dedupe");
		err = do_dedupe(sfd, soff, g_reg.fd, hot(r));
		break;
	}
	call_end(w);
	return (err);
}

/*
 * Writer w owns the hot regions r with r % writers == w, so each
 * region has one writer and its writes are numbered 1, 2, 3...
 */
static void
reg_writer(worker_t *w)
{
	char *buf = xalloc(RS);
	int nw = opt_writers;
	int nown = (NREG - w->w_id + nw - 1) / nw;

	while (!halted()) {
		int r = w->w_id + nw * (int)(rnd(&w->w_rng) % nown);
		uint64_t j = LD(&g_reg.done[r]) + 1;

		if (reg_op(w, r, j, buf) != 0)
			die("writer %d, region %d, write %llu: %s\n",
			    w->w_id, r, (ull_t)j, strerror(errno));
		ST(&g_reg.done[r], j);
		w->w_writes++;
	}
	free(buf);
}

/*
 * Check the part of a read that fell in hot region r: one generation,
 * and one that a state during the read held.
 */
static int
reg_part(worker_t *w, int r, const char *buf, size_t a, size_t len,
    uint64_t d0, uint64_t s1)
{
	size_t badoff = 0;
	int64_t g = check(buf, len, (uint64_t)r * RS + a, &badoff);

	if (g < 0) {
		bad(w, "region %d, bytes %zu-%zu: word at %zu is "
		    "%016llx", r, a, a + len - 1, a + badoff,
		    (ull_t)*(const uint64_t *)(buf + badoff));
		return (-1);
	}
	if (!reg_allowed(g, d0, s1)) {
		bad(w, "region %d, bytes %zu-%zu: generation %lld, "
		    "but writes %llu to %llu were current", r, a,
		    a + len - 1, (long long)g, (ull_t)d0, (ull_t)s1);
		return (-1);
	}
	if (s1 > d0) {
		if ((uint64_t)g != reg_gen(d0))
			w->w_new++;
		else if (reg_gen(d0 + 1) != reg_gen(d0))
			w->w_old++;
	}
	return (0);
}

/*
 * Pick a read: a whole region, part of one, the second half of one
 * and the first half of the next, or a page-aligned part of one into
 * a page-aligned buffer, which direct=always reads with Direct I/O.
 */
static char *
reg_pick(worker_t *w, char *base, int *rp, size_t *ap, size_t *lenp)
{
	uint64_t x = rnd(&w->w_rng);
	size_t ps = g_pagesize, a, len;
	char *buf = base + 8;
	int r = x % NREG;

	switch ((x >> 8) % 4) {
	case 0:
		a = 0;
		len = RS;
		break;
	case 1:
		a = 8 * ((x >> 16) % (RS / 8));
		len = 8 * (1 + (x >> 40) % ((RS - a) / 8));
		break;
	case 2:
		if (r == NREG - 1)
			r--;
		a = RS / 2 + 8 * ((x >> 16) % 1024);
		len = RS;
		break;
	default:
		a = ps * ((x >> 16) % (RS / ps));
		len = ps * (1 + (x >> 40) % ((RS - a) / ps));
		buf = base;
		w->w_aligned++;
		break;
	}
	*rp = r;
	*ap = a;
	*lenp = len;
	return (buf);
}

static void
reg_reader(worker_t *w)
{
	char *base = xalloc(2 * RS + g_pagesize);

	while (!halted()) {
		uint64_t d0[2], s1[2];
		size_t a, len, la;
		int r, nr, ovl = 0;
		char *buf = reg_pick(w, base, &r, &a, &len);

		nr = a + len > RS ? 2 : 1;
		for (int i = 0; i < nr; i++)
			d0[i] = LD(&g_reg.done[r + i]);
		call_begin(w, "pread");
		ssize_t n = pread(g_reg.fd, buf, len, hot(r) + a);
		call_end(w);
		for (int i = 0; i < nr; i++)
			s1[i] = LD(&g_reg.started[r + i]);
		if (n < 0)
			syserr("pread");
		w->w_reads++;
		if ((size_t)n != len) {
			bad(w, "region %d+%zu: read of %zu got %zd",
			    r, a, len, n);
			continue;
		}
		la = nr == 2 ? RS - a : len;
		if (reg_part(w, r, buf, a, la, d0[0], s1[0]) != 0 ||
		    (nr == 2 && reg_part(w, r + 1, buf + la, 0,
		    len - la, d0[1], s1[1]) != 0))
			continue;
		for (int i = 0; i < nr; i++)
			ovl |= s1[i] > d0[i];
		if (ovl) {
			w->w_ovl++;
			INC(&g_ovl);
		}
	}
	free(base);
}

/*
 * Every hot region must hold the generation its last write left.  The
 * reads are page-aligned, so they count as aligned reads.
 */
static void
reg_verify(const char *when)
{
	char *buf = xalloc(RS);

	for (int r = 0; r < NREG; r++) {
		size_t badoff = 0;
		uint64_t want = reg_gen(g_reg.done[r]);
		int64_t g;

		xpread(g_reg.fd, buf, RS, hot(r));
		t_aligned++;
		g = check(buf, RS, (uint64_t)r * RS, &badoff);
		if (g < 0 || (uint64_t)g != want)
			die("region %d holds generation %lld, not "
			    "%llu, %s\n", r, (long long)g,
			    (ull_t)want, when);
	}
	free(buf);
}

static void
reg_race(void)
{
	char name[PATH_MAX];
	struct stat st;

	g_reg.fd = xopen(g_path, O_RDWR);
	if (fstat(g_reg.fd, &st) != 0)
		syserr("fstat");
	if (st.st_size != 3 * NREG * RS)
		die("%s is %lld bytes, run -p first\n", g_path,
		    (long long)st.st_size);
	g_reg.wfd = g_reg.fd;
	if (opt_direct)
		g_reg.wfd = xopen(g_path, O_RDWR | O_DIRECT);
	if (g_mode != M_REGIONS) {
		(void) snprintf(name, sizeof (name), "%s.a", g_path);
		g_reg.fda = xopen(name, O_RDONLY);
		(void) snprintf(name, sizeof (name), "%s.b", g_path);
		g_reg.fdb = xopen(name, O_RDONLY);
	}
	if (opt_writers > NREG)
		die("at most %d writers\n", NREG);
	reg_verify("before the race (run -p first)");
	for (int i = 0; i < opt_writers; i++)
		(void) spawn("writer", i, reg_writer);
	for (int i = 0; i < opt_readers; i++)
		(void) spawn("reader", i, reg_reader);
	run_round(NULL, 1);
	if (LD(&g_failed) == 0)
		reg_verify("after the race");
}

/*
 * append.  Records of writer w carry generation (w + 1) << 24 | seq.
 */
static void
app_writer(worker_t *w)
{
	char *buf = xalloc(REC);
	uint64_t id = (uint64_t)(w->w_id + 1) << 24, seq = 0, s;

	while (!halted()) {
		fill(buf, REC, id | (seq + 1), 0);
		s = LD(&g_app.started);
		do {
			if (s >= MAXREC)
				goto out;
		} while (!CAS(&g_app.started, &s, s + 1));
		call_begin(w, "append");
		ssize_t n = write(g_app.afd, buf, REC);
		call_end(w);
		if (n != REC)
			die("append of %d wrote %zd: %s\n", REC, n,
			    strerror(errno));
		seq++;
		g_app.mine[w->w_id]++;
		w->w_writes++;
		INC(&g_app.done);
	}
out:
	free(buf);
}

/*
 * A read at a record boundary returns whole records, at least up to
 * the appends done before it and none past those begun by its end.
 */
static int
app_check(worker_t *w, const char *buf, uint64_t off, uint64_t len,
    ssize_t n, uint64_t lo, uint64_t hi)
{
	uint64_t end = off + n, want = off + len;
	size_t badoff = 0;

	if (want > lo)
		want = lo;
	if (n % REC != 0 || end < want || end > hi) {
		bad(w, "read of %llu at %llu got %zd; %llu appended "
		    "before it, %llu begun by its end", (ull_t)len,
		    (ull_t)off, n, (ull_t)lo, (ull_t)hi);
		return (-1);
	}
	for (ssize_t i = 0; i < n; i += REC) {
		int64_t g = check(buf + i, REC, 0, &badoff);

		if (g < 0 || (g >> 24) < 1 ||
		    (g >> 24) > opt_writers) {
			const char *p = buf + i + badoff;

			bad(w, "record at %llu: word %zu is %016llx",
			    (ull_t)(off + i), badoff,
			    (ull_t)*(const uint64_t *)p);
			return (-1);
		}
	}
	return (0);
}

/*
 * A read overlaps when an append was in flight and every append that
 * could have been fell inside its range.
 */
static void
app_reader(worker_t *w)
{
	char *buf = xalloc(6 * REC);

	while (!halted()) {
		uint64_t x = rnd(&w->w_rng);
		uint64_t d0 = LD(&g_app.done), back = x % 3;
		uint64_t off = (d0 - (back < d0 ? back : d0)) * REC;
		uint64_t len = (2 + (x >> 8) % 5) * REC, lo, hi;

		call_begin(w, "pread");
		ssize_t n = pread(g_app.fd, buf, len, off);
		call_end(w);
		hi = LD(&g_app.started) * REC;
		if (n < 0)
			syserr("pread");
		w->w_reads++;
		lo = d0 * REC;
		if (app_check(w, buf, off, len, n, lo, hi) != 0 ||
		    hi <= lo || off + len < hi)
			continue;
		w->w_ovl++;
		INC(&g_ovl);
		if (off + n > lo)
			w->w_new++;
		else
			w->w_old++;
	}
	free(buf);
}

static int
app_done(void)
{
	return (LD(&g_app.done) >= MAXREC);
}

/*
 * After a round the file holds every record whole, and each writer's
 * records in the order it wrote them.
 */
static void
app_verify(const char *name)
{
	char *buf = xalloc(REC);
	uint64_t last[MAXW] = { 0 }, count[MAXW] = { 0 };
	struct stat st;

	if (fstat(g_app.fd, &st) != 0)
		syserr("fstat");
	if ((uint64_t)st.st_size != g_app.done * REC)
		die("%s is %lld bytes after %llu appends\n", name,
		    (long long)st.st_size, (ull_t)g_app.done);
	for (uint64_t i = 0; i < g_app.done; i++) {
		size_t badoff = 0;
		int64_t g;
		int wr;

		xpread(g_app.fd, buf, REC, i * REC);
		g = check(buf, REC, 0, &badoff);
		wr = g < 0 ? 0 : (int)(g >> 24);
		if (wr < 1 || wr > opt_writers ||
		    (uint64_t)(g & 0xffffff) <= last[wr - 1])
			die("%s: record %llu is not whole or out of "
			    "order\n", name, (ull_t)i);
		last[wr - 1] = g & 0xffffff;
		count[wr - 1]++;
	}
	for (int w = 0; w < opt_writers; w++) {
		if (count[w] != g_app.mine[w])
			die("%s: writer %d appended %llu records, "
			    "the file has %llu\n", name, w,
			    (ull_t)g_app.mine[w], (ull_t)count[w]);
	}
	free(buf);
}

static void
app_race(void)
{
	char name[PATH_MAX];

	for (int round = 0; more_rounds(); round++) {
		(void) snprintf(name, sizeof (name),
		    "%s/rl_append.%d", g_path, round);
		g_app.fd = xopen(name, O_RDWR | O_CREAT | O_EXCL);
		g_app.afd = xopen(name, O_WRONLY | O_APPEND);
		g_app.started = g_app.done = 0;
		(void) memset(g_app.mine, 0, sizeof (g_app.mine));
		for (int i = 0; i < opt_writers; i++)
			(void) spawn("appender", i, app_writer);
		for (int i = 0; i < opt_readers; i++)
			(void) spawn("reader", i, app_reader);
		run_round(app_done, 1);
		if (LD(&g_failed) != 0)
			return;
		app_verify(name);
		(void) close(g_app.afd);
		(void) close(g_app.fd);
		if (unlink(name) != 0)
			syserr("unlink");
	}
}

/*
 * truncate.  [0, T_LO) never changes.  Operation j of the truncator
 * truncates to T_LO (j % 3 == 1), extends to T_HI, leaving zeros
 * (j % 3 == 2), or rewrites [T_LO, T_HI) with generation 2 + j / 3
 * (j % 3 == 0, which j == 0 stands for at the start).
 */
#define	TS_SHORT	(-1)
#define	TS_ZEROS	(-2)
#define	TS_NONE		(-3)

static int
tr_allowed(int64_t state, uint64_t d0, uint64_t s1)
{
	for (uint64_t j = d0; j <= s1; j++) {
		if (j % 3 == 1 && state == TS_SHORT)
			return (1);
		if (j % 3 == 2 && state == TS_ZEROS)
			return (1);
		if (j % 3 == 0 && state == (int64_t)(2 + j / 3))
			return (1);
	}
	return (0);
}

static void
tr_writer(worker_t *w)
{
	char *buf = xalloc(T_TAIL);

	for (uint64_t j = 1; !halted(); j++) {
		ssize_t n;
		int err;

		if (j % 3 == 0)
			fill(buf, T_TAIL, 2 + j / 3, T_LO);
		ST(&g_tr.started, j);
		if (j % 3 == 1) {
			call_begin(w, "ftruncate down");
			err = ftruncate(g_tr.fd, T_LO);
		} else if (j % 3 == 2) {
			call_begin(w, "ftruncate up");
			err = ftruncate(g_tr.fd, T_HI);
		} else {
			call_begin(w, "pwrite");
			n = pwrite(g_tr.fd, buf, T_TAIL, T_LO);
			err = n == T_TAIL ? 0 : -1;
		}
		call_end(w);
		if (err != 0)
			die("truncator op %llu: %s\n", (ull_t)j,
			    strerror(errno));
		ST(&g_tr.done, j);
		w->w_writes++;
	}
	free(buf);
}

/*
 * Work out the state a read of [off, off + len) saw from where it
 * ended and what the tail held: TS_SHORT, TS_ZEROS or the tail's
 * generation.  Every read reaches past T_LO.  TS_NONE if no state
 * fits.
 */
static int64_t
tr_state(worker_t *w, const char *buf, uint64_t off, uint64_t len,
    uint64_t n)
{
	uint64_t end = off + n, cut = off < T_LO ? T_LO - off : 0;
	size_t badoff = 0;
	int64_t g;

	if (n == 0 && off >= T_LO)
		return (TS_SHORT);
	if (cut > 0 && n > 0 &&
	    check(buf, n < cut ? n : cut, off, &badoff) != 1) {
		bad(w, "read at %llu: byte %llu below the cut is "
		    "wrong", (ull_t)off, (ull_t)(off + badoff));
		return (TS_NONE);
	}
	if (end == T_LO)
		return (TS_SHORT);
	if (end < T_LO || end > T_HI || (n < len && end != T_HI)) {
		bad(w, "read of %llu at %llu ended at %llu, a size "
		    "never set", (ull_t)len, (ull_t)off, (ull_t)end);
		return (TS_NONE);
	}
	if (zeros(buf + cut, n - cut))
		return (TS_ZEROS);
	g = check(buf + cut, n - cut, off + cut, &badoff);
	if (g < 2) {
		bad(w, "read at %llu: tail byte %llu is not zero "
		    "or one rewrite", (ull_t)off,
		    (ull_t)(off + cut + badoff));
		return (TS_NONE);
	}
	return (g);
}

/*
 * Count a read that overlapped operations d0 + 1 to s1: as racing a
 * truncate if any of them was one, else as racing a rewrite.
 */
static void
tr_overlap(worker_t *w, uint64_t d0, uint64_t s1)
{
	for (uint64_t j = d0 + 1; j <= s1; j++) {
		if (j % 3 != 0) {
			w->w_ovl++;
			INC(&g_ovl);
			return;
		}
	}
	w->w_ovl2++;
}

static void
tr_reader(worker_t *w)
{
	char *buf = xalloc(T_HI + 65536);

	while (!halted()) {
		uint64_t x = rnd(&w->w_rng), off, len, lo, top;
		uint64_t d0, s1;
		int64_t state;

		if ((x & 3) != 0)
			off = T_LO - 8 * (1 + (x >> 8) % 4096);
		else
			off = T_LO + 8 * ((x >> 8) % (T_TAIL / 8));
		lo = off < T_LO ? T_LO - off + 8 : 8;
		top = T_HI + 65536 - off;
		len = lo + 8 * ((x >> 32) % ((top - lo) / 8));
		d0 = LD(&g_tr.done);
		call_begin(w, "pread");
		ssize_t n = pread(g_tr.fd, buf, len, off);
		call_end(w);
		s1 = LD(&g_tr.started);
		if (n < 0)
			syserr("pread");
		w->w_reads++;
		state = tr_state(w, buf, off, len, n);
		if (state == TS_NONE)
			continue;
		w->w_st[state < 0 ? -state - 1 : 2]++;
		if (!tr_allowed(state, d0, s1)) {
			bad(w, "read of %llu at %llu saw state %lld, "
			    "but ops %llu to %llu were current",
			    (ull_t)len, (ull_t)off, (long long)state,
			    (ull_t)d0, (ull_t)s1);
			continue;
		}
		if (s1 > d0)
			tr_overlap(w, d0, s1);
	}
	free(buf);
}

static void
tr_race(void)
{
	char *buf = xalloc(T_HI + 8);
	struct stat st;
	int64_t state;
	uint64_t j;
	ssize_t n;

	g_tr.fd = xopen(g_path, O_RDWR | O_CREAT | O_TRUNC);
	fill(buf, T_LO, 1, 0);
	fill(buf + T_LO, T_TAIL, 2, T_LO);
	xpwrite(g_tr.fd, buf, T_HI, 0);
	(void) spawn("truncator", 0, tr_writer);
	for (int i = 0; i < opt_readers; i++)
		(void) spawn("reader", i, tr_reader);
	run_round(NULL, 1);
	if (LD(&g_failed) != 0)
		return;

	j = g_tr.done;
	if (fstat(g_tr.fd, &st) != 0)
		syserr("fstat");
	n = pread(g_tr.fd, buf, T_HI + 8, 0);
	if (n < 0)
		syserr("pread");
	state = n != st.st_size ? TS_NONE :
	    tr_state(&g_final, buf, 0, T_HI + 8, n);
	if (state == TS_NONE || !tr_allowed(state, j, j))
		die("after op %llu the file is %lld bytes, state "
		    "%lld\n", (ull_t)j, (long long)st.st_size,
		    (long long)state);
	say("saw_short %llu\nsaw_zeros %llu\nsaw_data %llu\n",
	    (ull_t)t_st[0], (ull_t)t_st[1], (ull_t)t_st[2]);
	free(buf);
}

/*
 * blksz.  A file holds B_BASE bytes of generation 1 in one block.
 * Even files grow by a write of generation 2 at B_BASE that ends past
 * the first block, odd ones by a truncate to B_GROW, which leaves
 * zeros.  Either grows the block size.  A file counts as overlapped
 * when a read was in flight during the extension.
 */
static void
bs_wait(int post)
{
	for (int i = 0; i < opt_readers && !halted(); ) {
		worker_t *r = g_bs.readers[i];

		if (post ? LD(&r->w_post) : LD(&r->w_reads) > 0)
			i++;
		else
			sleep_ms(0);
	}
}

/*
 * Extend the file once every reader has read it, then wait until
 * every reader has read it again after the extension ended.
 */
static void
bs_extender(worker_t *w)
{
	char *buf = xalloc(RS);
	ssize_t n;
	int err;

	fill(buf, RS, 2, B_BASE);
	bs_wait(0);
	if (halted())
		goto out;
	ST(&g_bs.started, 1);
	if (g_bs.grow == 0) {
		call_begin(w, "pwrite");
		n = pwrite(g_bs.fd, buf, RS, B_BASE);
		err = n == RS ? 0 : -1;
	} else {
		call_begin(w, "ftruncate");
		err = ftruncate(g_bs.fd, B_GROW);
	}
	call_end(w);
	if (err != 0)
		die("extending: %s\n", strerror(errno));
	ST(&g_bs.done, 1);
	w->w_writes++;
	bs_wait(1);
	ST(&g_round_done, 1);
out:
	free(buf);
}

static int
bs_extension(const char *buf, size_t n)
{
	size_t badoff, len = n - B_BASE;

	if (g_bs.grow)
		return (zeros(buf + B_BASE, len));
	return (check(buf + B_BASE, len, B_BASE, &badoff) == 2);
}

/*
 * A read returns the first block alone, or with all of the extension
 * up to the new end of file.  Before the extension has begun it
 * cannot see it, and once it has ended it cannot miss it.
 */
static void
bs_reader(worker_t *w)
{
	static const size_t lens[] = { B_BASE, 2 * B_BASE,
	    RS + 2 * B_BASE };
	char *buf = xalloc(RS + 2 * B_BASE);
	uint64_t size = g_bs.grow ? B_GROW : B_BASE + RS;

	while (!halted()) {
		size_t len = lens[rnd(&w->w_rng) % 3], badoff = 0;
		size_t full = len < size ? len : size;
		uint64_t d0 = LD(&g_bs.done), s1;
		int post;

		call_begin(w, "pread");
		ssize_t n = pread(g_bs.fd, buf, len, 0);
		call_end(w);
		s1 = LD(&g_bs.started);
		if (n < 0)
			syserr("pread");
		post = n > B_BASE;
		if (n != B_BASE && (size_t)n != full) {
			bad(w, "read of %zu got %zd", len, n);
		} else if (check(buf, B_BASE, 0, &badoff) != 1) {
			bad(w, "read of %zu: byte %zu of the first "
			    "block is wrong", len, badoff);
		} else if (post && !bs_extension(buf, n)) {
			bad(w, "read of %zu: the extension is not "
			    "whole", len);
		}
		if (len > B_BASE && (post ? s1 != 1 : d0 != 0))
			bad(w, "read of %zu %s the extension, which "
			    "had %s", len, post ? "saw" : "missed",
			    post ? "not begun" : "ended");
		if (s1 > d0) {
			w->w_ovl++;
			ST(&g_bs.ovl, 1);
		}
		if (d0 == 1)
			ST(&w->w_post, 1);
		INC(&w->w_reads);
	}
	free(buf);
}

static int
bs_done(void)
{
	return (LD(&g_round_done));
}

static int
bs_open(int i, int create)
{
	char name[PATH_MAX];
	char *buf;
	int fd;

	(void) snprintf(name, sizeof (name), "%s/rl_blksz.%d",
	    g_path, i);
	if (!create)
		return (xopen(name, O_RDWR));
	fd = xopen(name, O_RDWR | O_CREAT | O_EXCL);
	buf = xalloc(B_BASE);
	fill(buf, B_BASE, 1, 0);
	xpwrite(fd, buf, B_BASE, 0);
	free(buf);
	return (fd);
}

static void
bs_file(int i)
{
	char name[PATH_MAX];
	struct stat st;

	g_bs.fd = bs_open(i, !opt_existing);
	if (fstat(g_bs.fd, &st) != 0)
		syserr("fstat");
	if (st.st_size != B_BASE || st.st_blksize != B_BASE)
		die("file %d: %lld bytes in %ld byte blocks, not "
		    "one 4K block\n", i, (long long)st.st_size,
		    (long)st.st_blksize);
	g_bs.grow = i & 1;
	g_bs.started = g_bs.done = g_bs.ovl = 0;
	g_round_done = 0;
	for (int r = 0; r < opt_readers; r++)
		g_bs.readers[r] = spawn("reader", r, bs_reader);
	(void) spawn("extender", 0, bs_extender);
	run_round(bs_done, 0);
	if (LD(&g_failed) != 0)
		return;
	if (fstat(g_bs.fd, &st) != 0)
		syserr("fstat");
	if (g_bs.done && st.st_blksize <= B_BASE)
		die("file %d: block size still %ld at %lld bytes\n",
		    i, (long)st.st_blksize, (long long)st.st_size);
	if (LD(&g_bs.ovl))
		INC(&g_ovl);
	(void) close(g_bs.fd);
	(void) snprintf(name, sizeof (name), "%s/rl_blksz.%d",
	    g_path, i);
	if (unlink(name) != 0)
		syserr("unlink");
}

static void
bs_race(void)
{
	int i;

	if (opt_prep) {
		for (i = 0; i < opt_files; i++)
			(void) close(bs_open(i, 1));
		return;
	}
	for (i = 0; i < opt_files && !LD(&g_stop) &&
	    LD(&g_failed) == 0; i++)
		bs_file(i);
	say("files %d\n", i);
}

/*
 * mmap (Linux).  Readers copy the data area into their slice of a
 * shared mapping of the same file, and dirtiers store into the rest
 * of the mapping, so with low dirty limits the copy faults and is
 * throttled while writeback runs zfs_putpage() on the file.
 */
#ifdef __linux__
static void
mm_reader(worker_t *w)
{
	char *slice = g_mm.map + (size_t)w->w_id * M_SLICE;

	while (!halted()) {
		uint64_t x = rnd(&w->w_rng);
		size_t len = 4096 * (16 + x % 49), badoff = 0;
		uint64_t src = 8 * ((x >> 8) % ((M_DATA - len) / 8));
		size_t pg = (x >> 32) % ((M_SLICE - len) / 4096 + 1);
		char *dst = slice + pg * 4096;

		INC(&w->w_rseq);
		ST(&w->w_inread, 1);
		call_begin(w, "pread");
		ssize_t n = pread(g_mm.fd, dst, len, src);
		call_end(w);
		ST(&w->w_inread, 0);
		if (n < 0)
			syserr("pread");
		w->w_reads++;
		if ((size_t)n != len)
			bad(w, "read of %zu got %zd", len, n);
		else if (check(dst, len, src, &badoff) != 1)
			bad(w, "read of %zu at %llu: word %zu is "
			    "wrong", len, (ull_t)src, badoff);
	}
}

static void
mm_dirtier(worker_t *w)
{
	size_t half = M_DIRTY / opt_writers, ps = g_pagesize;
	char *base = g_mm.map + (size_t)opt_readers * M_SLICE +
	    (size_t)w->w_id * half;

	for (uint64_t k = 0; !halted(); k++) {
		volatile uint64_t *p;

		for (size_t o = 0; o < half && !halted(); o += ps) {
			p = (volatile uint64_t *)(base + o) + k % 512;
			call_begin(w, "store");
			*p = k;
			call_end(w);
			w->w_writes++;
		}
	}
}

/*
 * A read is throttled when its kernel stack, read while it is in
 * pread(), shows balance_dirty_pages().
 */
static int
mm_sample(worker_t *r, char *buf, size_t len)
{
	char path[64];
	uint64_t seq = LD(&r->w_rseq);

	if (!LD(&r->w_inread) || seq == r->w_hitseq)
		return (0);
	(void) snprintf(path, sizeof (path),
	    "/proc/self/task/%ld/stack", LD(&r->w_tid));
	if (readfile(path, buf, len) <= 0 || !LD(&r->w_inread) ||
	    LD(&r->w_rseq) != seq ||
	    strstr(buf, "balance_dirty_pages") == NULL)
		return (0);
	r->w_hitseq = seq;
	return (1);
}

static void
mm_sampler(worker_t *w)
{
	char buf[8192];

	(void) w;
	while (!halted()) {
		for (int i = 0; i < opt_readers; i++) {
			worker_t *r = g_mm.readers[i];

			if (!mm_sample(r, buf, sizeof (buf)))
				continue;
			if (INC(&g_ovl) == 1)
				say("first throttled read, reader "
				    "%d:\n%s", i, buf);
			if (strstr(buf, "zfs_read") != NULL)
				INC(&g_mm.hits_zr);
		}
		sleep_ms(1);
	}
}

static long long
vmstat(const char *key)
{
	char buf[16384], *p = buf;
	size_t kl = strlen(key);

	if (readfile("/proc/vmstat", buf, sizeof (buf)) <= 0)
		return (-1);
	while (p != NULL) {
		if (strncmp(p, key, kl) == 0 && p[kl] == ' ')
			return (atoll(p + kl + 1));
		if ((p = strchr(p, '\n')) != NULL)
			p++;
	}
	return (-1);
}

static void
mm_race(void)
{
	static const char *vm[] = { "dirty_bytes",
	    "dirty_background_bytes" };
	size_t msz = (size_t)opt_readers * M_SLICE + M_DIRTY;
	char path[64], buf[4096];
	char *data = xalloc(M_DATA);
	long long wr0;

	(void) snprintf(path, sizeof (path),
	    "/proc/self/task/%ld/stack", gettid_());
	if (readfile(path, buf, sizeof (buf)) <= 0) {
		say("cannot read %s: %s\nresult unobservable\n", path,
		    strerror(errno));
		exit(5);
	}
	for (int i = 0; i < 2; i++) {
		(void) snprintf(path, sizeof (path),
		    "/proc/sys/vm/%s", vm[i]);
		if (readfile(path, buf, sizeof (buf)) > 0)
			say("vm.%s %s", vm[i], buf);
	}
	g_mm.fd = xopen(g_path, O_RDWR | O_CREAT | O_TRUNC);
	for (size_t o = 0; o < M_DATA + msz; o += M_SLICE) {
		fill(data, M_SLICE, 1, o);
		xpwrite(g_mm.fd, data, M_SLICE, o);
	}
	if (fsync(g_mm.fd) != 0)
		syserr("fsync");
	xpread(g_mm.fd, data, M_DATA, 0);
	g_mm.map = mmap(NULL, msz, PROT_READ | PROT_WRITE, MAP_SHARED,
	    g_mm.fd, M_DATA);
	if (g_mm.map == MAP_FAILED)
		syserr("mmap");
	wr0 = vmstat("nr_written");
	for (int i = 0; i < opt_readers; i++)
		g_mm.readers[i] = spawn("reader", i, mm_reader);
	for (int i = 0; i < opt_writers; i++)
		(void) spawn("dirtier", i, mm_dirtier);
	for (int i = 0; i < opt_readers; i++) {
		while (LD(&g_mm.readers[i]->w_tid) == 0)
			sleep_ms(1);
	}
	(void) spawn("sampler", 0, mm_sampler);
	run_round(NULL, 1);
	say("hits_zfs_read %llu\n", (ull_t)LD(&g_mm.hits_zr));
	say("nr_written %lld\n", vmstat("nr_written") - wr0);
	(void) munmap(g_mm.map, msz);
	free(data);
}
#endif

/*
 * cold.  One writer rewrites all C_LEN bytes of the file with one
 * pwrite, generation j for write j.  Readers read all of it into a
 * new mapping of which only the first C_WARM bytes were written, so
 * the copy faults partway through.  Each reader waits for a gap
 * between writes before it starts a read.  A read counts as an
 * overlap if no write was in flight when it began and one began
 * during it.
 */
static uint64_t
thread_faults(void)
{
	struct rusage ru;

	if (getrusage(RUSAGE_THREAD, &ru) != 0)
		syserr("getrusage");
	return ((uint64_t)ru.ru_minflt + (uint64_t)ru.ru_majflt);
}

static void
cd_writer(worker_t *w)
{
	char *buf = xalloc(C_LEN);

	for (uint64_t j = 1; !halted(); j++) {
		fill(buf, C_LEN, j, 0);
		ST(&g_cd.started, j);
		call_begin(w, "pwrite");
		ssize_t n = pwrite(g_cd.fd, buf, C_LEN, 0);
		call_end(w);
		if (n != C_LEN)
			die("write %llu of %d wrote %zd: %s\n",
			    (ull_t)j, C_LEN, n, strerror(errno));
		ST(&g_cd.done, j);
		w->w_writes++;
	}
	free(buf);
}

static void
cd_check(worker_t *w, const char *buf, ssize_t n, uint64_t d0,
    uint64_t s0, uint64_t s1)
{
	const uint64_t *wd = (const uint64_t *)buf;
	size_t badoff = 0;
	int64_t g;

	if (n != C_LEN) {
		bad(w, "read of %d got %zd", C_LEN, n);
		return;
	}
	g = check(buf, C_LEN, 0, &badoff);
	if (g < 0) {
		bad(w, "generation %llu up to byte %zu, then "
		    "%016llx; writes %llu to %llu were current",
		    (ull_t)(wd[0] >> 32), badoff,
		    (ull_t)wd[badoff / 8], (ull_t)d0, (ull_t)s1);
		return;
	}
	if ((uint64_t)g < d0 || (uint64_t)g > s1) {
		bad(w, "generation %lld, but writes %llu to %llu "
		    "were current", (long long)g, (ull_t)d0,
		    (ull_t)s1);
		return;
	}
	if (s0 == d0 && s1 > s0) {
		w->w_ovl++;
		INC(&g_ovl);
		if ((uint64_t)g == d0)
			w->w_old++;
		else
			w->w_new++;
	}
}

static void
cd_reader(worker_t *w)
{
	while (!halted()) {
		char *buf = mmap(NULL, C_LEN, PROT_READ | PROT_WRITE,
		    MAP_PRIVATE | MAP_ANON, -1, 0);
		uint64_t d0, s0, s1, flt;

		if (buf == MAP_FAILED)
			syserr("mmap");
#ifdef MADV_NOHUGEPAGE
		(void) madvise(buf, C_LEN, MADV_NOHUGEPAGE);
#endif
		(void) memset(buf, 0xff, C_WARM);
		flt = thread_faults();
		while (!halted() &&
		    LD(&g_cd.started) != LD(&g_cd.done))
			(void) sched_yield();
		d0 = LD(&g_cd.done);
		s0 = LD(&g_cd.started);
		call_begin(w, "pread");
		ssize_t n = pread(g_cd.fd, buf, C_LEN, 0);
		call_end(w);
		s1 = LD(&g_cd.started);
		flt = thread_faults() - flt;
		if (n < 0)
			syserr("pread");
		w->w_reads++;
		if (flt > 0)
			INC(&g_cd.faulted);
		cd_check(w, buf, n, d0, s0, s1);
		(void) munmap(buf, C_LEN);
	}
}

static void
cd_race(void)
{
	char *buf = xalloc(C_LEN);
	const uint64_t *wd = (const uint64_t *)buf;
	size_t badoff = 0;
	int64_t g;

	g_cd.fd = xopen(g_path, O_RDWR | O_CREAT | O_TRUNC);
	fill(buf, C_LEN, 0, 0);
	xpwrite(g_cd.fd, buf, C_LEN, 0);
	(void) spawn("writer", 0, cd_writer);
	for (int i = 0; i < opt_readers; i++)
		(void) spawn("reader", i, cd_reader);
	run_round(NULL, 1);
	say("faulted_reads %llu\n", (ull_t)g_cd.faulted);
	if (LD(&g_failed) == 0) {
		xpread(g_cd.fd, buf, C_LEN, 0);
		g = check(buf, C_LEN, 0, &badoff);
		if (g < 0)
			bad(&g_final, "after write %llu the file "
			    "holds generation %llu up to byte %zu, "
			    "then %016llx", (ull_t)g_cd.done,
			    (ull_t)(wd[0] >> 32), badoff,
			    (ull_t)wd[badoff / 8]);
		else if ((uint64_t)g != g_cd.done)
			bad(&g_final, "after write %llu the file "
			    "holds generation %lld",
			    (ull_t)g_cd.done, (long long)g);
	}
	free(buf);
}

static __attribute__((noreturn)) void
usage(void)
{
	(void) fprintf(stderr, "usage: rangelock_race [-pDe] "
	    "[-t secs] [-n count] [-H secs]\n"
	    "    [-r readers] [-w writers] [-f files] mode path\n"
	    "mode: regions clone dedupe append truncate blksz "
	    "mmap cold\n");
	exit(2);
}

int
main(int argc, char *argv[])
{
	static const char *modes[] = { "regions", "clone", "dedupe",
	    "append", "truncate", "blksz", "mmap", "cold" };
	static const uint64_t targets[] = { 100000, 20000, 20000,
	    10000, 20000, 0, 20, 20000 };
	uint64_t start;
	int c, m;

	while ((c = getopt(argc, argv, "pDet:n:H:r:w:f:")) != -1) {
		switch (c) {
		case 'p':
			opt_prep = 1;
			break;
		case 'D':
			opt_direct = 1;
			break;
		case 'e':
			opt_existing = 1;
			break;
		case 't':
			opt_secs = strtoull(optarg, NULL, 10);
			break;
		case 'n':
			opt_target = strtoull(optarg, NULL, 10);
			break;
		case 'H':
			opt_hang = strtoull(optarg, NULL, 10);
			break;
		case 'r':
			opt_readers = atoi(optarg);
			break;
		case 'w':
			opt_writers = atoi(optarg);
			break;
		case 'f':
			opt_files = atoi(optarg);
			break;
		default:
			usage();
		}
	}
	if (argc - optind != 2)
		usage();
	for (m = 0; m < 8 && strcmp(argv[optind], modes[m]) != 0; m++)
		;
	if (m == 8)
		usage();
	g_mode = m;
	g_path = argv[optind + 1];
	if (opt_target == 0)
		opt_target = targets[m] ? targets[m] : opt_files;
	if (opt_readers < 1)
		opt_readers = g_mode == M_BLKSZ ? 3 : 4;
	if (g_mode == M_TRUNC || g_mode == M_BLKSZ ||
	    g_mode == M_COLD)
		opt_writers = 1;
	else if (opt_writers < 1)
		opt_writers = 2;
	if (opt_readers + opt_writers + 1 > MAXW)
		usage();
#ifndef __linux__
	if (g_mode == M_DEDUPE || g_mode == M_MMAP)
		die("%s needs Linux\n", modes[m]);
#endif
	(void) setvbuf(stdout, NULL, _IOLBF, 0);
	g_pagesize = sysconf(_SC_PAGESIZE);
	g_seed = now_ns() ^ ((uint64_t)getpid() << 32);
	start = now_ns();
	g_deadline = start + opt_secs * NSEC;

	switch (g_mode) {
	case M_REGIONS:
	case M_CLONE:
	case M_DEDUPE:
		if (opt_prep) {
			reg_prep();
			return (0);
		}
		reg_race();
		break;
	case M_APPEND:
		app_race();
		break;
	case M_TRUNC:
		tr_race();
		break;
	case M_BLKSZ:
		bs_race();
		if (opt_prep)
			return (0);
		break;
	case M_MMAP:
#ifdef __linux__
		mm_race();
#endif
		break;
	case M_COLD:
		cd_race();
		break;
	}

	say("reads %llu\nwrites %llu\naligned_reads %llu\n",
	    (ull_t)t_reads, (ull_t)t_writes, (ull_t)t_aligned);
	say("overlaps %llu\noverlaps_rewrite %llu\n", (ull_t)g_ovl,
	    (ull_t)t_ovl2);
	say("saw_old %llu\nsaw_new %llu\n", (ull_t)t_old,
	    (ull_t)t_new);
	say("elapsed_ms %llu\n",
	    (ull_t)((now_ns() - start) / 1000000));
	if (g_failed != 0) {
		say("bad_reads %llu\nresult fail\n", (ull_t)g_failed);
		return (1);
	}
	if (g_ovl == 0) {
		say("result norace\n");
		return (4);
	}
	say("result pass\n");
	return (0);
}
