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
 * O_TMPFILE publication scenarios for crash-recovery tests.
 *
 *   tmpfile_crash run <scenario> <dir> <pool>
 *	Performs the scenario in <dir>, a file system of <pool>.  Setup
 *	state is made durable with "zpool sync" before "START txg=<n>" is
 *	printed.  "ACK txg=<n>" is printed once every sync the scenario
 *	performs has returned; the program then sleeps until killed, so
 *	that a crash state can be captured with its descriptors still open.
 *	<n> is the highest committed TXG from the pool's txgs kstat.
 *
 *	TMPFILE_CRASH_FREEZE	if non-empty, run "zpool freeze <pool>" just
 *				before START, so that nothing after it reaches
 *				the uberblock
 *	TMPFILE_CRASH_GATE	after START, wait until this path exists
 *	ZPOOL			zpool command (default "zpool")
 *
 *   tmpfile_crash verify <scenario> <dir>
 *	Checks a state captured at ACK.  Prints "RESULT <scenario> <outcome>
 *	pass|FAIL ..." and exits 0 only if the outcome is permitted.
 *
 *   tmpfile_crash verify-prefix <scenario> <dir>
 *	Checks a state captured after START but before ACK.
 *
 * Every recovered state must contain the setup file, complete.  The
 * published name "f" must be absent or refer to a regular file with one
 * link and exactly the expected size, contents and (for "meta") metadata;
 * a torn publication is never permitted.  At ACK, "f" must be present
 * unless the scenario never publishes it or never syncs it.  A lookup error
 * other than ENOENT is a failure, never an absence.
 *
 * Scenarios:
 *   dirsync	write, fsync, linkat, fsync(dir)
 *   filesync	write, linkat, fsync
 *   small	as dirsync, below zfs_immediate_write_sz
 *   checkpoint	write and fsync, then "zpool sync" with the file still
 *		unnamed (before START), then write, fsync, linkat, fsync,
 *		fsync(dir)
 *   meta	xattrs, owner, mode and mtime (with nanoseconds) set on the
 *		unnamed file
 *   bigxattr	an xattr too large for the system-attribute area (stored in
 *		a directory xattr on xattr=sa datasets) and a small one
 *   clone	FICLONE from a synced file into the unnamed file
 *   clonerm	as clone, then the source removed and the directory synced
 *		before publication; at ACK "src" must be absent
 *   clonemix	as clone, then part of the unnamed file overwritten, so that
 *		its blocks are partly clones and partly new data
 *   sparse	64M file with 4K of data in the middle
 *   hugesparse	1T file with 4K of data at each end: more blocks than
 *		publication logs, so it waits for its TXG instead
 *   clonesmall	FICLONE of a synced one-block (4K) file
 *   racewrite	as clone, but a second process rewrites every 128K chunk of
 *		the unnamed file, over and over, while it is published; the
 *		writes stop before the file and directory are synced
 *   racegrow	a one-block (4K) unnamed file that a second process rewrites
 *		and extends to 1M in 128K chunks, over and over, while it is
 *		published: the block size grows during the race
 *   hole	64M file with no data at all
 *   sparsemix	data extents of different sizes and alignments separated by
 *		holes, and a trailing hole to an unaligned size
 *   truncfast0, truncfastblk, truncfastmid, clonefast, clonefastmid,
 *   removefast
 *		written and published without any sync, then truncated (to 0,
 *		to 256K, to 256K + 5000), cloned into from a synced file (all
 *		of it, or 256K at 256K), or removed, still without syncing f or
 *		the directory; then a marker file is synced.  Every state must
 *		be no f (only before ACK, except removefast), the published f,
 *		or f after the operation: never, say, the published size with
 *		the operation's range zeroed.
 *   nodump	as dirsync, with FS_NODUMP_FL set on the unnamed file
 *   projid	as dirsync, with project ID 4321 set on the unnamed file
 *   truncrace	as truncfast0, but the truncate runs while another process's
 *		fsync of the directory is committing the publication (the two
 *		are started together, so whether they overlap is timing)
 *   truncevict, cloneevict
 *		as truncfast0 and clonefast, but f is closed, the inode cache
 *		dropped and f reopened by name before the operation
 *   dedupefast, dedupefastmid
 *		published, then FIDEDUPERANGE from a synced file with the
 *		same bytes (all of it, or 256K at 256K); the dedupe must share
 *		every byte requested, and f's contents never change
 *   namedclone	not a tmpfile: a named file written, cloned over (all of it)
 *		from a synced file and synced; at ACK, as after the clone
 *   dense	4M file written at once; on a dataset with small records its
 *		publication needs more log records than one log block holds
 *   unlinked	never published; an ordinary marker file is synced
 *   nosync	published without fsync; an ordinary marker file is synced
 *   multi	as dirsync, then an empty tmpfile published as "e" and synced,
 *		then an ordinary marker file synced: later acknowledged work
 *		that a replay must not lose after replaying the publications
 *   remove	as dirsync, then "f" removed and the directory synced, then an
 *		ordinary marker file synced; at ACK "f" must be absent
 *   relink	as dirsync, then a second name "g" linked and the directory
 *		synced; at ACK both names refer to one complete file
 *   rename	published as "t" and synced, then renamed to "f" and synced;
 *		at ACK only "f", complete
 *   acl	needs acltype=posix: in a directory "d" with a default ACL
 *		(setup), "d/f" gets an explicit access ACL and "d/g" only the
 *		inherited one; both published and "d" synced
 *   syncalways	needs sync=always: write and publish with no fsync at all
 *   osync	the tmpfile is opened O_SYNC and written, then published and
 *		the directory synced (writes to an unnamed file are not logged,
 *		O_SYNC or not, so this exercises the O_SYNC workload, not a
 *		different record order)
 *   reuse	needs TMPFILE_CRASH_FREEZE: "a" is published and removed, then
 *		unnamed files are created and closed until one reuses the
 *		object number of "a"; that one is given mode 0640, published as
 *		"f" and synced.  Prints "REUSE obj=<n>" (or "NOREUSE"), so the
 *		caller can require that the intent log really reuses a number
 *
 * For "filesync" before its fsync and for "nosync", "absent or complete"
 * is the behavior OpenZFS currently provides (publication waits for a TXG
 * sync), not a POSIX requirement: POSIX makes no promise about unsynced
 * data.  It is checked so that a change to it is deliberate.
 */

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/xattr.h>
#include <time.h>
#include <unistd.h>
#include <linux/fs.h>

#ifndef O_TMPFILE
#define	O_TMPFILE	(020000000|O_DIRECTORY)
#endif

#define	MIB		(1024 * 1024)
#define	DATA_SIZE	(MIB + 12345)
#define	SPARSE_SIZE	(64 * MIB)
#define	SPARSE_OFF	(32 * MIB)
#define	SMALL_SIZE	4096
#define	DENSE_SIZE	(4 * MIB)
#define	CHUNK		MIB
#define	META_UID	1234
#define	META_GID	5678
#define	META_MODE	0640
#define	META_MTIME	1700000000
#define	META_MTIME_NS	123456789
#define	HUGE_SIZE	(1ULL << 40)
#define	RACE_CHUNK	(128 * 1024)
#define	RACE_ITERS	400
#define	RACE_PIECE	4096
#define	MIX_OFF		300000
#define	FAST_OFF	(2 * RACE_CHUNK)
#define	META_PROJID	4321
#define	FAST_LEN	(2 * RACE_CHUNK)
#define	MIX_LEN		200000
#define	SPARSEMIX_SIZE	(30 * MIB + 17)
#define	XATTR_SHORT	"user.tmpfile_crash.short"
#define	XATTR_LONG	"user.tmpfile_crash.long"
#define	XATTR_LONG_LEN	300
#define	XATTR_BIG	"user.tmpfile_crash.big"
#define	XATTR_BIG_LEN	60000
#define	ACL_ACCESS	"system.posix_acl_access"
#define	ACL_DEFAULT	"system.posix_acl_default"
#define	ACL_UID		1234
#define	ACL_UID2	5678
#define	REUSE_MODE	0640
#define	REUSE_TRIES	400000

static const char *pool;

/* The data extents of "sparsemix"; the rest of the file is holes. */
static const struct { uint64_t off, len; } sparsemix[] = {
	{ 0, 200000 },
	{ 8 * MIB + 1000, 5000 },
	{ 20 * MIB, 128 * 1024 },
};

static void
die(const char *what)
{
	(void) fprintf(stderr, "FATAL %s: %s\n", what, strerror(errno));
	exit(2);
}

/* Deterministic, position-dependent content, distinct per scenario. */
static uint8_t
pattern(uint64_t off, unsigned salt)
{
	uint64_t x = (off + 1) * 0x9E3779B97F4A7C15ULL ^
	    salt * 0xBF58476D1CE4E5B9ULL;
	x ^= x >> 29;
	return ((uint8_t)(x ^ (x >> 17)));
}

static void
write_pattern(int fd, uint64_t off, size_t len, unsigned salt)
{
	uint8_t *buf = malloc(len);
	if (buf == NULL)
		die("malloc");
	for (size_t i = 0; i < len; i++)
		buf[i] = pattern(off + i, salt);
	for (size_t done = 0; done < len; ) {
		ssize_t n = pwrite(fd, buf + done, len - done, off + done);
		if (n <= 0)
			die("pwrite");
		done += n;
	}
	free(buf);
}

static long long
committed_txg(void)
{
	char path[256], line[512];
	long long best = -1;

	(void) snprintf(path, sizeof (path), "/proc/spl/kstat/zfs/%s/txgs",
	    pool);
	FILE *f = fopen(path, "r");
	if (f == NULL)
		return (-1);
	while (fgets(line, sizeof (line), f) != NULL) {
		long long txg, birth;
		char state[8];
		if (sscanf(line, "%lld %lld %7s", &txg, &birth, state) == 3 &&
		    strcmp(state, "C") == 0 && txg > best)
			best = txg;
	}
	(void) fclose(f);
	return (best);
}

static void
xfsync(int fd, const char *what)
{
	if (fsync(fd) != 0)
		die(what);
}

static int
open_tmpfile(int dfd)
{
	int fd = openat(dfd, ".", O_TMPFILE | O_RDWR, 0600);
	if (fd < 0)
		die("open O_TMPFILE");
	return (fd);
}

static int
create_file(int dfd, const char *name)
{
	int fd = openat(dfd, name, O_CREAT | O_EXCL | O_RDWR, 0644);
	if (fd < 0)
		die(name);
	return (fd);
}

/*
 * POSIX ACL xattr values, in the kernel's little-endian format: a 4-byte
 * version (2), then 8-byte entries (tag, permissions, id).
 */
#define	ACL_USER_OBJ	0x01
#define	ACL_USER	0x02
#define	ACL_GROUP_OBJ	0x04
#define	ACL_MASK	0x10
#define	ACL_OTHER	0x20
#define	ACL_NO_ID	0xffffffffu

static size_t
acl_value(uint8_t *buf, const uint32_t (*e)[3], int n)
{
	uint8_t *p = buf;

	*p++ = 2; *p++ = 0; *p++ = 0; *p++ = 0;
	for (int i = 0; i < n; i++) {
		*p++ = e[i][0] & 0xff; *p++ = e[i][0] >> 8;
		*p++ = e[i][1] & 0xff; *p++ = e[i][1] >> 8;
		for (int b = 0; b < 32; b += 8)
			*p++ = (e[i][2] >> b) & 0xff;
	}
	return (p - buf);
}

/* Default ACL of "d": u::rwx, u:ACL_UID:rwx, g::r-x, m::rwx, o::r-x */
static const uint32_t acl_default_e[][3] = {
	{ ACL_USER_OBJ, 7, ACL_NO_ID }, { ACL_USER, 7, ACL_UID },
	{ ACL_GROUP_OBJ, 5, ACL_NO_ID }, { ACL_MASK, 7, ACL_NO_ID },
	{ ACL_OTHER, 5, ACL_NO_ID } };
/* Access ACL of "d/f": u::rw-, u:ACL_UID2:r--, g::r--, m::r--, o::--- */
static const uint32_t acl_explicit_e[][3] = {
	{ ACL_USER_OBJ, 6, ACL_NO_ID }, { ACL_USER, 4, ACL_UID2 },
	{ ACL_GROUP_OBJ, 4, ACL_NO_ID }, { ACL_MASK, 4, ACL_NO_ID },
	{ ACL_OTHER, 0, ACL_NO_ID } };
/*
 * Access ACL that "d/g" inherits when created with mode 0600: each class
 * limited by the mode, the mask standing in for the group class.
 */
static const uint32_t acl_inherited_e[][3] = {
	{ ACL_USER_OBJ, 6, ACL_NO_ID }, { ACL_USER, 7, ACL_UID },
	{ ACL_GROUP_OBJ, 5, ACL_NO_ID }, { ACL_MASK, 0, ACL_NO_ID },
	{ ACL_OTHER, 0, ACL_NO_ID } };
#define	ACL_EXPLICIT_MODE	0640
#define	ACL_INHERITED_MODE	0600

static int
open_tmpfile_flags(int dfd, int flags)
{
	int fd = openat(dfd, ".", O_TMPFILE | O_RDWR | flags, 0600);
	if (fd < 0)
		die("open O_TMPFILE");
	return (fd);
}

static void
publish(int fd, int dfd, const char *name)
{
	char proc[64];

	/* linkat(fd, "", dfd, name, AT_EMPTY_PATH) without CAP_DAC_READ */
	(void) snprintf(proc, sizeof (proc), "/proc/self/fd/%d", fd);
	if (linkat(AT_FDCWD, proc, dfd, name, AT_SYMLINK_FOLLOW) != 0)
		die("linkat");
}

static void
zpool_cmd(const char *sub)
{
	const char *zpool = getenv("ZPOOL") ? getenv("ZPOOL") : "zpool";
	char *const argv[] = {
		(char *)zpool, (char *)sub, (char *)pool, NULL };
	pid_t pid;
	int status;

	if ((pid = fork()) < 0)
		die("fork");
	if (pid == 0) {
		(void) execvp(zpool, argv);
		_exit(127);
	}
	if (waitpid(pid, &status, 0) < 0)
		die("waitpid");
	if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
		(void) fprintf(stderr, "FATAL %s %s %s failed\n",
		    zpool, sub, pool);
		exit(2);
	}
}

static unsigned
salt_of(const char *scenario)
{
	unsigned h = 5381;
	while (*scenario)
		h = h * 33 + (unsigned char)*scenario++;
	return (h);
}

static void
fill_xattr(char *v, int len)
{
	for (int i = 0; i < len; i++)
		v[i] = 'a' + i % 26;
}

static void
wait_gate(void)
{
	const char *gate = getenv("TMPFILE_CRASH_GATE");
	struct timespec ts = { .tv_sec = 0, .tv_nsec = 10 * 1000 * 1000 };

	if (gate == NULL)
		return;
	while (access(gate, F_OK) != 0)
		(void) nanosleep(&ts, NULL);
}

/* Scenarios that operate on f after publishing it without a sync. */
static int
is_fast(const char *sc)
{
	return (strncmp(sc, "truncfast", 9) == 0 ||
	    strncmp(sc, "clonefast", 9) == 0 ||
	    strncmp(sc, "dedupefast", 10) == 0 ||
	    strcmp(sc, "removefast") == 0 || strcmp(sc, "truncrace") == 0 ||
	    strcmp(sc, "truncevict") == 0 || strcmp(sc, "cloneevict") == 0 ||
	    strcmp(sc, "namedclone") == 0);
}

static int
is_trunc(const char *sc)
{
	return (strncmp(sc, "truncfast", 9) == 0 ||
	    strcmp(sc, "truncrace") == 0 || strcmp(sc, "truncevict") == 0);
}

/* Clones from a synced "src" with other contents (salt + 7). */
static int
is_clone(const char *sc)
{
	return (strncmp(sc, "clonefast", 9) == 0 ||
	    strcmp(sc, "cloneevict") == 0 || strcmp(sc, "namedclone") == 0);
}

/* The size of f after a truncating scenario's truncate. */
static uint64_t
fast_size(const char *sc)
{
	return (strcmp(sc, "truncfastblk") == 0 ? FAST_OFF :
	    strcmp(sc, "truncfastmid") == 0 ? FAST_OFF + 5000 : 0);
}

/* The salt of the racer's pass i. */
static unsigned
race_salt(unsigned salt, int i)
{
	return (salt + 1000 + i);
}

/*
 * Start a process that rewrites [0, DATA_SIZE) of fd in RACE_CHUNK pieces
 * RACE_ITERS times, each pass with its own salt, and return once its first
 * chunk is written.
 */
static pid_t
start_racer(int fd, unsigned salt)
{
	int p[2];
	char c = 0;

	if (pipe(p) != 0)
		die("pipe");
	pid_t pid = fork();
	if (pid < 0)
		die("fork");
	if (pid == 0) {
		(void) close(p[0]);
		for (int i = 0; i < RACE_ITERS; i++) {
			for (uint64_t off = 0; off < DATA_SIZE;
			    off += RACE_CHUNK) {
				write_pattern(fd, off, off + RACE_CHUNK >
				    DATA_SIZE ? DATA_SIZE - off : RACE_CHUNK,
				    race_salt(salt, i));
				if (i == 0 && off == 0 &&
				    write(p[1], &c, 1) != 1)
					_exit(1);
			}
		}
		_exit(0);
	}
	(void) close(p[1]);
	if (read(p[0], &c, 1) != 1)
		die("racer start");
	(void) close(p[0]);
	return (pid);
}

static void
wait_racer(pid_t pid)
{
	int st;

	if (waitpid(pid, &st, 0) != pid || !WIFEXITED(st) ||
	    WEXITSTATUS(st) != 0)
		die("racer");
}

static void
run(const char *sc, int dfd)
{
	unsigned salt = salt_of(sc);
	int fd = -1, sfd;

	/*
	 * Setup.  The first zil_commit() on a dataset creates the log and
	 * waits for a TXG sync; do that here so that it cannot make the
	 * scenario's operations durable as a side effect.
	 */
	sfd = create_file(dfd, "setup");
	write_pattern(sfd, 0, SMALL_SIZE, salt);
	xfsync(sfd, "fsync setup");
	(void) close(sfd);
	if (strcmp(sc, "clonesmall") == 0) {
		sfd = create_file(dfd, "src");
		write_pattern(sfd, 0, SMALL_SIZE, salt);
		xfsync(sfd, "fsync src");
		(void) close(sfd);
	} else if (is_clone(sc) || strncmp(sc, "dedupefast", 10) == 0) {
		sfd = create_file(dfd, "src");
		write_pattern(sfd, 0, DATA_SIZE,
		    is_clone(sc) ? salt + 7 : salt);
		xfsync(sfd, "fsync src");
		(void) close(sfd);
	} else if (strncmp(sc, "clone", 5) == 0 ||
	    strcmp(sc, "racewrite") == 0) {
		sfd = create_file(dfd, "src");
		write_pattern(sfd, 0, DATA_SIZE, salt);
		xfsync(sfd, "fsync src");
		(void) close(sfd);
	}
	if (strcmp(sc, "checkpoint") == 0) {
		/* The unnamed inode and half its data reach a synced TXG. */
		fd = open_tmpfile(dfd);
		write_pattern(fd, 0, DATA_SIZE / 2, salt);
		xfsync(fd, "fsync tmpfile 1");
	}
	if (strcmp(sc, "acl") == 0) {
		uint8_t v[64];
		size_t n = acl_value(v, acl_default_e, 5);
		if (mkdirat(dfd, "d", 0755) != 0)
			die("mkdir d");
		int ddfd = openat(dfd, "d", O_RDONLY | O_DIRECTORY);
		if (ddfd < 0 || fsetxattr(ddfd, ACL_DEFAULT, v, n, 0) != 0)
			die("default ACL on d");
		xfsync(ddfd, "fsync d (setup)");
		(void) close(ddfd);
	}
	xfsync(dfd, "fsync dir (setup)");
	zpool_cmd("sync");
	const char *freeze = getenv("TMPFILE_CRASH_FREEZE");
	if (freeze != NULL && *freeze != '\0')
		zpool_cmd("freeze");
	(void) printf("START txg=%lld\n", committed_txg());
	(void) fflush(stdout);
	wait_gate();

	if (strcmp(sc, "dirsync") == 0) {
		fd = open_tmpfile(dfd);
		write_pattern(fd, 0, DATA_SIZE, salt);
		xfsync(fd, "fsync tmpfile");
		publish(fd, dfd, "f");
		xfsync(dfd, "fsync dir");
	} else if (strcmp(sc, "filesync") == 0) {
		fd = open_tmpfile(dfd);
		write_pattern(fd, 0, DATA_SIZE, salt);
		publish(fd, dfd, "f");
		xfsync(fd, "fsync file");
	} else if (strcmp(sc, "small") == 0) {
		fd = open_tmpfile(dfd);
		write_pattern(fd, 0, SMALL_SIZE, salt);
		xfsync(fd, "fsync tmpfile");
		publish(fd, dfd, "f");
		xfsync(dfd, "fsync dir");
	} else if (strcmp(sc, "checkpoint") == 0) {
		write_pattern(fd, DATA_SIZE / 2, DATA_SIZE - DATA_SIZE / 2,
		    salt);
		xfsync(fd, "fsync tmpfile 2");
		publish(fd, dfd, "f");
		xfsync(fd, "fsync file");
		xfsync(dfd, "fsync dir");
	} else if (strcmp(sc, "meta") == 0) {
		char v[XATTR_LONG_LEN];
		struct timespec ts[2] = {
			{ .tv_sec = META_MTIME, .tv_nsec = META_MTIME_NS },
			{ .tv_sec = META_MTIME, .tv_nsec = META_MTIME_NS } };
		fd = open_tmpfile(dfd);
		write_pattern(fd, 0, SMALL_SIZE, salt);
		fill_xattr(v, XATTR_LONG_LEN);
		if (fsetxattr(fd, XATTR_SHORT, "v1", 2, 0) != 0 ||
		    fsetxattr(fd, XATTR_LONG, v, sizeof (v), 0) != 0)
			die("fsetxattr");
		if (fchown(fd, META_UID, META_GID) != 0)
			die("fchown");
		if (fchmod(fd, META_MODE) != 0)
			die("fchmod");
		if (futimens(fd, ts) != 0)
			die("futimens");
		xfsync(fd, "fsync tmpfile");
		publish(fd, dfd, "f");
		xfsync(fd, "fsync file");
		xfsync(dfd, "fsync dir");
	} else if (strcmp(sc, "bigxattr") == 0) {
		char *v = malloc(XATTR_BIG_LEN);
		if (v == NULL)
			die("malloc");
		fill_xattr(v, XATTR_BIG_LEN);
		fd = open_tmpfile(dfd);
		write_pattern(fd, 0, SMALL_SIZE, salt);
		if (fsetxattr(fd, XATTR_SHORT, "v1", 2, 0) != 0 ||
		    fsetxattr(fd, XATTR_BIG, v, XATTR_BIG_LEN, 0) != 0)
			die("fsetxattr");
		free(v);
		xfsync(fd, "fsync tmpfile");
		publish(fd, dfd, "f");
		xfsync(fd, "fsync file");
		xfsync(dfd, "fsync dir");
	} else if (strncmp(sc, "clone", 5) == 0 && !is_fast(sc)) {
		sfd = openat(dfd, "src", O_RDONLY);
		if (sfd < 0)
			die("open src");
		fd = open_tmpfile(dfd);
		if (ioctl(fd, FICLONE, sfd) != 0)
			die("FICLONE");
		(void) close(sfd);
		if (strcmp(sc, "clonemix") == 0)
			write_pattern(fd, MIX_OFF, MIX_LEN, salt + 1);
		xfsync(fd, "fsync tmpfile");
		if (strcmp(sc, "clonerm") == 0) {
			if (unlinkat(dfd, "src", 0) != 0)
				die("unlink src");
			xfsync(dfd, "fsync dir (src removed)");
		}
		publish(fd, dfd, "f");
		xfsync(fd, "fsync file");
		xfsync(dfd, "fsync dir");
	} else if (strcmp(sc, "sparse") == 0) {
		fd = open_tmpfile(dfd);
		if (ftruncate(fd, SPARSE_SIZE) != 0)
			die("ftruncate");
		write_pattern(fd, SPARSE_OFF, SMALL_SIZE, salt);
		xfsync(fd, "fsync tmpfile");
		publish(fd, dfd, "f");
		xfsync(fd, "fsync file");
		xfsync(dfd, "fsync dir");
	} else if (strcmp(sc, "hole") == 0) {
		fd = open_tmpfile(dfd);
		if (ftruncate(fd, SPARSE_SIZE) != 0)
			die("ftruncate");
		xfsync(fd, "fsync tmpfile");
		publish(fd, dfd, "f");
		xfsync(fd, "fsync file");
		xfsync(dfd, "fsync dir");
	} else if (strcmp(sc, "sparsemix") == 0) {
		fd = open_tmpfile(dfd);
		for (size_t i = 0; i < sizeof (sparsemix) /
		    sizeof (sparsemix[0]); i++)
			write_pattern(fd, sparsemix[i].off, sparsemix[i].len,
			    salt);
		if (ftruncate(fd, SPARSEMIX_SIZE) != 0)
			die("ftruncate");
		xfsync(fd, "fsync tmpfile");
		publish(fd, dfd, "f");
		xfsync(fd, "fsync file");
		xfsync(dfd, "fsync dir");
	} else if (strcmp(sc, "hugesparse") == 0) {
		fd = open_tmpfile(dfd);
		if (ftruncate(fd, HUGE_SIZE) != 0)
			die("ftruncate");
		write_pattern(fd, 0, SMALL_SIZE, salt);
		write_pattern(fd, HUGE_SIZE - SMALL_SIZE, SMALL_SIZE, salt);
		xfsync(fd, "fsync tmpfile");
		publish(fd, dfd, "f");
		xfsync(fd, "fsync file");
		xfsync(dfd, "fsync dir");
	} else if (strcmp(sc, "racewrite") == 0 ||
	    strcmp(sc, "racegrow") == 0) {
		fd = open_tmpfile(dfd);
		if (strcmp(sc, "racewrite") == 0) {
			sfd = openat(dfd, "src", O_RDONLY);
			if (sfd < 0 || ioctl(fd, FICLONE, sfd) != 0)
				die("FICLONE");
			(void) close(sfd);
		} else {
			write_pattern(fd, 0, SMALL_SIZE, salt);
		}
		pid_t racer = start_racer(fd, salt);
		publish(fd, dfd, "f");
		wait_racer(racer);
		xfsync(fd, "fsync file");
		xfsync(dfd, "fsync dir");
	} else if (strcmp(sc, "nodump") == 0 || strcmp(sc, "projid") == 0) {
		fd = open_tmpfile(dfd);
		write_pattern(fd, 0, DATA_SIZE, salt);
		if (strcmp(sc, "nodump") == 0) {
			int fl = 0;
			if (ioctl(fd, FS_IOC_GETFLAGS, &fl) != 0)
				die("FS_IOC_GETFLAGS");
			fl |= FS_NODUMP_FL;
			if (ioctl(fd, FS_IOC_SETFLAGS, &fl) != 0)
				die("FS_IOC_SETFLAGS");
		} else {
			struct fsxattr fsx;
			if (ioctl(fd, FS_IOC_FSGETXATTR, &fsx) != 0)
				die("FS_IOC_FSGETXATTR");
			fsx.fsx_projid = META_PROJID;
			if (ioctl(fd, FS_IOC_FSSETXATTR, &fsx) != 0)
				die("FS_IOC_FSSETXATTR");
		}
		xfsync(fd, "fsync tmpfile");
		publish(fd, dfd, "f");
		xfsync(dfd, "fsync dir");
	} else if (is_fast(sc)) {
		if (strcmp(sc, "namedclone") == 0) {
			fd = create_file(dfd, "f");
			write_pattern(fd, 0, DATA_SIZE, salt);
		} else {
			fd = open_tmpfile(dfd);
			write_pattern(fd, 0, DATA_SIZE, salt);
			publish(fd, dfd, "f");
		}
		if (strcmp(sc, "truncevict") == 0 ||
		    strcmp(sc, "cloneevict") == 0) {
			int dc;
			(void) close(fd);
			dc = open("/proc/sys/vm/drop_caches", O_WRONLY);
			if (dc < 0 || write(dc, "2", 1) != 1)
				die("drop_caches");
			(void) close(dc);
			if ((fd = openat(dfd, "f", O_RDWR)) < 0)
				die("reopen f");
		}
		if (strcmp(sc, "truncrace") == 0) {
			/* A directory fsync commits the publication. */
			pid_t child = fork();
			struct timespec ts = { 0, 500 * 1000 * 1000 };
			if (child < 0)
				die("fork");
			if (child == 0) {
				xfsync(dfd, "fsync dir (child)");
				_exit(0);
			}
			(void) nanosleep(&ts, NULL);
			if (ftruncate(fd, 0) != 0)
				die("ftruncate");
			int st;
			if (waitpid(child, &st, 0) != child || !WIFEXITED(st) ||
			    WEXITSTATUS(st) != 0)
				die("child fsync");
		} else if (is_trunc(sc)) {
			if (ftruncate(fd, fast_size(sc)) != 0)
				die("ftruncate");
		} else if (strcmp(sc, "removefast") == 0) {
			if (unlinkat(dfd, "f", 0) != 0)
				die("unlink f");
		} else if (strncmp(sc, "dedupefast", 10) == 0) {
			int whole = strcmp(sc, "dedupefast") == 0;
			uint64_t off = whole ? 0 : FAST_OFF;
			uint64_t len = whole ? DATA_SIZE : FAST_LEN;
			struct file_dedupe_range *r = calloc(1, sizeof (*r) +
			    sizeof (struct file_dedupe_range_info));
			if (r == NULL)
				die("calloc");
			sfd = openat(dfd, "src", O_RDONLY);
			if (sfd < 0)
				die("open src");
			r->src_offset = off;
			r->src_length = len;
			r->dest_count = 1;
			r->info[0].dest_fd = fd;
			r->info[0].dest_offset = off;
			if (ioctl(sfd, FIDEDUPERANGE, r) != 0)
				die("FIDEDUPERANGE");
			if (r->info[0].status != FILE_DEDUPE_RANGE_SAME ||
			    r->info[0].bytes_deduped != len)
				die("FIDEDUPERANGE did not share it all");
			free(r);
			(void) close(sfd);
		} else {
			sfd = openat(dfd, "src", O_RDONLY);
			if (sfd < 0)
				die("open src");
			if (strcmp(sc, "clonefastmid") != 0) {
				if (ioctl(fd, FICLONE, sfd) != 0)
					die("FICLONE");
			} else {
				struct file_clone_range fcr = {
					.src_fd = sfd, .src_offset = FAST_OFF,
					.src_length = FAST_LEN,
					.dest_offset = FAST_OFF };
				if (ioctl(fd, FICLONERANGE, &fcr) != 0)
					die("FICLONERANGE");
			}
			(void) close(sfd);
		}
		if (strcmp(sc, "namedclone") == 0)
			xfsync(fd, "fsync f");
		sfd = create_file(dfd, "marker");
		write_pattern(sfd, 0, SMALL_SIZE, salt);
		xfsync(sfd, "fsync marker");
	} else if (strcmp(sc, "dense") == 0) {
		fd = open_tmpfile(dfd);
		write_pattern(fd, 0, DENSE_SIZE, salt);
		xfsync(fd, "fsync tmpfile");
		publish(fd, dfd, "f");
		xfsync(dfd, "fsync dir");
	} else if (strcmp(sc, "unlinked") == 0) {
		fd = open_tmpfile(dfd);
		write_pattern(fd, 0, DATA_SIZE, salt);
		xfsync(fd, "fsync tmpfile");
		sfd = create_file(dfd, "marker");
		write_pattern(sfd, 0, SMALL_SIZE, salt);
		xfsync(sfd, "fsync marker");
		xfsync(dfd, "fsync dir");
	} else if (strcmp(sc, "multi") == 0) {
		fd = open_tmpfile(dfd);
		write_pattern(fd, 0, DATA_SIZE, salt);
		xfsync(fd, "fsync tmpfile");
		publish(fd, dfd, "f");
		xfsync(dfd, "fsync dir");
		sfd = open_tmpfile(dfd);
		publish(sfd, dfd, "e");
		xfsync(dfd, "fsync dir (e)");
		(void) close(sfd);
		sfd = create_file(dfd, "marker");
		write_pattern(sfd, 0, SMALL_SIZE, salt);
		xfsync(sfd, "fsync marker");
		xfsync(dfd, "fsync dir (marker)");
	} else if (strcmp(sc, "remove") == 0) {
		fd = open_tmpfile(dfd);
		write_pattern(fd, 0, DATA_SIZE, salt);
		xfsync(fd, "fsync tmpfile");
		publish(fd, dfd, "f");
		xfsync(dfd, "fsync dir");
		if (unlinkat(dfd, "f", 0) != 0)
			die("unlink f");
		xfsync(dfd, "fsync dir (remove)");
		sfd = create_file(dfd, "marker");
		write_pattern(sfd, 0, SMALL_SIZE, salt);
		xfsync(sfd, "fsync marker");
		xfsync(dfd, "fsync dir (marker)");
	} else if (strcmp(sc, "relink") == 0) {
		fd = open_tmpfile(dfd);
		write_pattern(fd, 0, DATA_SIZE, salt);
		xfsync(fd, "fsync tmpfile");
		publish(fd, dfd, "f");
		xfsync(dfd, "fsync dir");
		if (linkat(dfd, "f", dfd, "g", 0) != 0)
			die("link g");
		xfsync(dfd, "fsync dir (g)");
	} else if (strcmp(sc, "rename") == 0) {
		fd = open_tmpfile(dfd);
		write_pattern(fd, 0, DATA_SIZE, salt);
		xfsync(fd, "fsync tmpfile");
		publish(fd, dfd, "t");
		xfsync(dfd, "fsync dir");
		if (renameat(dfd, "t", dfd, "f") != 0)
			die("rename t f");
		xfsync(dfd, "fsync dir (rename)");
	} else if (strcmp(sc, "acl") == 0) {
		uint8_t v[64];
		size_t n = acl_value(v, acl_explicit_e, 5);
		int ddfd = openat(dfd, "d", O_RDONLY | O_DIRECTORY);
		if (ddfd < 0)
			die("open d");
		fd = open_tmpfile(ddfd);
		write_pattern(fd, 0, SMALL_SIZE, salt);
		if (fsetxattr(fd, ACL_ACCESS, v, n, 0) != 0)
			die("access ACL on f");
		xfsync(fd, "fsync tmpfile f");
		publish(fd, ddfd, "f");
		/*
		 * Commit f's publication before g exists: on a frozen pool,
		 * committing a TX_WRITE advances the TXG, and g would then be
		 * published in a later TXG than it was created in.
		 */
		xfsync(ddfd, "fsync d (f)");
		sfd = open_tmpfile(ddfd);
		write_pattern(sfd, 0, SMALL_SIZE, salt);
		xfsync(sfd, "fsync tmpfile g");
		publish(sfd, ddfd, "g");
		(void) close(sfd);
		xfsync(ddfd, "fsync d");
		(void) close(ddfd);
	} else if (strcmp(sc, "syncalways") == 0) {
		fd = open_tmpfile(dfd);
		write_pattern(fd, 0, DATA_SIZE, salt);
		publish(fd, dfd, "f");
	} else if (strcmp(sc, "osync") == 0) {
		fd = open_tmpfile_flags(dfd, O_SYNC);
		write_pattern(fd, 0, DATA_SIZE, salt);
		publish(fd, dfd, "f");
		xfsync(dfd, "fsync dir");
	} else if (strcmp(sc, "reuse") == 0) {
		fd = open_tmpfile(dfd);
		write_pattern(fd, 0, SMALL_SIZE, salt);
		xfsync(fd, "fsync tmpfile a");
		publish(fd, dfd, "a");
		xfsync(dfd, "fsync dir (a)");
		struct stat sa;
		if (fstat(fd, &sa) != 0)
			die("fstat a");
		(void) close(fd);
		if (unlinkat(dfd, "a", 0) != 0)
			die("unlink a");
		xfsync(dfd, "fsync dir (remove a)");
		/*
		 * Freed object numbers are only reused once enough objects
		 * have been freed and allocation crosses an L1 block of the
		 * meta-dnode; churn unnamed files until that happens.
		 */
		fd = -1;
		for (int i = 0; i < REUSE_TRIES && fd < 0; i++) {
			struct stat sb;
			int t = open_tmpfile(dfd);
			if (fstat(t, &sb) != 0)
				die("fstat");
			if (sb.st_ino == sa.st_ino)
				fd = t;
			else
				(void) close(t);
			if (i % 4096 == 4095)
				zpool_cmd("sync");
		}
		(void) printf("%s obj=%llu\n", fd < 0 ? "NOREUSE" : "REUSE",
		    (unsigned long long)sa.st_ino);
		if (fd < 0)
			fd = open_tmpfile(dfd);
		if (fchmod(fd, REUSE_MODE) != 0)
			die("fchmod");
		write_pattern(fd, 0, DATA_SIZE, salt);
		xfsync(fd, "fsync tmpfile f");
		publish(fd, dfd, "f");
		xfsync(dfd, "fsync dir");
	} else if (strcmp(sc, "nosync") == 0) {
		fd = open_tmpfile(dfd);
		write_pattern(fd, 0, DATA_SIZE, salt);
		publish(fd, dfd, "f");
		sfd = create_file(dfd, "marker");
		write_pattern(sfd, 0, SMALL_SIZE, salt);
		xfsync(sfd, "fsync marker");
		xfsync(dfd, "fsync dir");
	} else {
		(void) fprintf(stderr, "unknown scenario %s\n", sc);
		exit(2);
	}
	(void) printf("ACK txg=%lld\n", committed_txg());
	(void) fflush(stdout);
	for (;;)
		(void) pause();
}

static int
result(const char *sc, const char *outcome, int ok, const char *detail)
{
	(void) printf("RESULT %s %s %s%s\n", sc, outcome, ok ? "pass" : "FAIL",
	    detail ? detail : "");
	return (ok ? 0 : 1);
}

/* Expected byte at off of a file written by the scenario. */
static uint8_t
expected(const char *sc, uint64_t off, unsigned salt)
{
	if (strcmp(sc, "sparse") == 0 &&
	    (off < SPARSE_OFF || off >= SPARSE_OFF + SMALL_SIZE))
		return (0);
	if (strcmp(sc, "hole") == 0)
		return (0);
	if (strcmp(sc, "hugesparse") == 0 && off >= SMALL_SIZE &&
	    off < HUGE_SIZE - SMALL_SIZE)
		return (0);
	if (strcmp(sc, "sparsemix") == 0) {
		for (size_t i = 0; i < sizeof (sparsemix) /
		    sizeof (sparsemix[0]); i++) {
			if (off >= sparsemix[i].off &&
			    off < sparsemix[i].off + sparsemix[i].len)
				return (pattern(off, salt));
		}
		return (0);
	}
	if (strcmp(sc, "clonemix") == 0 && off >= MIX_OFF &&
	    off < MIX_OFF + MIX_LEN)
		return (pattern(off, salt + 1));
	return (pattern(off, salt));
}

/*
 * Compare all of fd with the scenario's content, in bounded chunks.
 * Returns NULL or a description of the first mismatch.
 */
static const char *
check_content(int fd, const char *sc, uint64_t size, unsigned salt)
{
	static char detail[128];
	uint8_t *got = malloc(CHUNK);

	if (got == NULL)
		die("malloc");
	for (uint64_t off = 0; off < size; off += CHUNK) {
		size_t len = size - off < CHUNK ? size - off : CHUNK;
		if (pread(fd, got, len, off) != (ssize_t)len) {
			(void) snprintf(detail, sizeof (detail),
			    " short-read@%llu", (unsigned long long)off);
			free(got);
			return (detail);
		}
		for (size_t i = 0; i < len; i++) {
			if (got[i] != expected(sc, off + i, salt)) {
				(void) snprintf(detail, sizeof (detail),
				    " first-mismatch@%llu",
				    (unsigned long long)(off + i));
				free(got);
				return (detail);
			}
		}
	}
	free(got);
	return (NULL);
}

/*
 * Look up a name without following symlinks.  Returns 1 if it exists as a
 * regular file with one link, 0 if it does not exist, and -1 (with detail)
 * for anything else.
 */
static int
lookup(int dfd, const char *name, struct stat *st, char *detail, size_t len)
{
	if (fstatat(dfd, name, st, AT_SYMLINK_NOFOLLOW) != 0) {
		if (errno == ENOENT)
			return (0);
		(void) snprintf(detail, len, " %s: lookup errno %d (%s)", name,
		    errno, strerror(errno));
		return (-1);
	}
	if (!S_ISREG(st->st_mode) || st->st_nlink != 1) {
		(void) snprintf(detail, len, " %s: mode %o nlink %llu", name,
		    (unsigned)st->st_mode, (unsigned long long)st->st_nlink);
		return (-1);
	}
	return (1);
}

/* Check a small ordinary file written by the scenario. */
static int
check_small(int dfd, const char *name, unsigned salt, char *detail,
    size_t len)
{
	struct stat st;
	int r = lookup(dfd, name, &st, detail, len);

	if (r <= 0)
		return (r);
	int fd = openat(dfd, name, O_RDONLY | O_NOFOLLOW);
	if (fd < 0) {
		(void) snprintf(detail, len, " %s: open errno %d", name, errno);
		return (-1);
	}
	const char *bad = st.st_size != SMALL_SIZE ? " wrong size" :
	    check_content(fd, "small", SMALL_SIZE, salt);
	(void) close(fd);
	if (bad != NULL) {
		(void) snprintf(detail, len, " %s:%s", name, bad);
		return (-1);
	}
	return (1);
}

/*
 * A regular file "name" in dfd: returns 1 if it exists with want_nlink
 * links, the given size and the scenario's content, 0 if it does not exist,
 * and -1 (with detail) otherwise.  *ino receives its inode number.
 */
static int
check_file(int dfd, const char *name, const char *sc, uint64_t size,
    nlink_t want_nlink, ino_t *ino, char *detail, size_t len)
{
	struct stat st;

	if (fstatat(dfd, name, &st, AT_SYMLINK_NOFOLLOW) != 0) {
		if (errno == ENOENT)
			return (0);
		(void) snprintf(detail, len, " %s: lookup errno %d", name,
		    errno);
		return (-1);
	}
	if (ino != NULL)
		*ino = st.st_ino;
	if (!S_ISREG(st.st_mode) || st.st_nlink != want_nlink ||
	    (uint64_t)st.st_size != size) {
		(void) snprintf(detail, len,
		    " %s: mode %o nlink %llu size %lld", name,
		    (unsigned)st.st_mode, (unsigned long long)st.st_nlink,
		    (long long)st.st_size);
		return (-1);
	}
	int fd = openat(dfd, name, O_RDONLY | O_NOFOLLOW);
	if (fd < 0) {
		(void) snprintf(detail, len, " %s: open errno %d", name, errno);
		return (-1);
	}
	const char *bad = check_content(fd, sc, size, salt_of(sc));
	(void) close(fd);
	if (bad != NULL) {
		(void) snprintf(detail, len, " %s:%s", name, bad);
		return (-1);
	}
	return (1);
}

/* "f" and its second name "g": each absent, or one complete file. */
static int
check_relink(const char *sc, int dfd, int prefix)
{
	char detail[256] = "";
	struct stat sf, sg;
	ino_t i1 = 0, i2 = 0;
	int hf, hg;

	/* Only ENOENT is absence; any other lookup error is a failure. */
	hf = (fstatat(dfd, "f", &sf, AT_SYMLINK_NOFOLLOW) == 0);
	if (!hf && errno != ENOENT) {
		(void) snprintf(detail, sizeof (detail),
		    " f: lookup errno %d", errno);
		return (result(sc, "LOOKUP-ERROR", 0, detail));
	}
	hg = (fstatat(dfd, "g", &sg, AT_SYMLINK_NOFOLLOW) == 0);
	if (!hg && errno != ENOENT) {
		(void) snprintf(detail, sizeof (detail),
		    " g: lookup errno %d", errno);
		return (result(sc, "LOOKUP-ERROR", 0, detail));
	}

	if (!hf && hg)
		return (result(sc, "ORPHAN-LINK", 0, " g without f"));
	if (!hf)
		return (result(sc, "ABSENT", prefix, " name-not-recovered"));
	int r = check_file(dfd, "f", sc, DATA_SIZE, hg ? 2 : 1, &i1, detail,
	    sizeof (detail));
	if (r != 1)
		return (result(sc, "WRONG", 0, detail));
	if (!hg)
		return (result(sc, prefix ? "OK" : "LINK-LOST", prefix,
		    " one name"));
	r = check_file(dfd, "g", sc, DATA_SIZE, 2, &i2, detail,
	    sizeof (detail));
	if (r != 1 || i1 != i2)
		return (result(sc, "WRONG", 0, r == 1 ? " f and g differ" :
		    detail));
	return (result(sc, "OK", 1, " two names"));
}

/* Published as "t", renamed to "f": at most one name, and complete. */
static int
check_rename(const char *sc, int dfd, int prefix)
{
	char detail[256] = "";
	int rt = check_file(dfd, "t", sc, DATA_SIZE, 1, NULL, detail,
	    sizeof (detail));
	if (rt < 0)
		return (result(sc, "WRONG", 0, detail));
	int rf = check_file(dfd, "f", sc, DATA_SIZE, 1, NULL, detail,
	    sizeof (detail));
	if (rf < 0)
		return (result(sc, "WRONG", 0, detail));
	if (rt == 1 && rf == 1)
		return (result(sc, "BOTH-NAMES", 0, NULL));
	if (rf == 1)
		return (result(sc, "OK", 1, " f"));
	if (rt == 1)
		return (result(sc, prefix ? "OK" : "RENAME-LOST", prefix,
		    " t"));
	return (result(sc, "ABSENT", prefix, " name-not-recovered"));
}

/* One ACL'd file in "d": absent, or content, mode and access ACL. */
static int
check_acl_file(int ddfd, const char *name, const uint32_t (*e)[3],
    mode_t mode, char *detail, size_t len)
{
	uint8_t want[64], got[128];
	size_t n = acl_value(want, e, 5);
	int r = check_file(ddfd, name, "acl", SMALL_SIZE, 1, NULL, detail,
	    len);
	if (r != 1)
		return (r);
	struct stat st;
	if (fstatat(ddfd, name, &st, AT_SYMLINK_NOFOLLOW) != 0 ||
	    (st.st_mode & 07777) != mode) {
		(void) snprintf(detail, len, " %s: mode %o, want %o", name,
		    (unsigned)(st.st_mode & 07777), (unsigned)mode);
		return (-1);
	}
	int fd = openat(ddfd, name, O_RDONLY | O_NOFOLLOW);
	ssize_t g = fd < 0 ? -1 :
	    fgetxattr(fd, ACL_ACCESS, got, sizeof (got));
	if (fd >= 0)
		(void) close(fd);
	if (g < 0 || (size_t)g != n || memcmp(got, want, n) != 0) {
		(void) snprintf(detail, len, " %s: access ACL %s (%zd bytes)",
		    name, g < 0 ? "missing" : "differs", g);
		return (-1);
	}
	return (1);
}

static int
check_acl(const char *sc, int dfd, int prefix)
{
	char detail[256] = "";
	int ddfd = openat(dfd, "d", O_RDONLY | O_DIRECTORY);
	if (ddfd < 0)
		return (result(sc, "SETUP-LOST", 0, " d"));
	int rf = check_acl_file(ddfd, "f", acl_explicit_e, ACL_EXPLICIT_MODE,
	    detail, sizeof (detail));
	int rg = rf < 0 ? rf : check_acl_file(ddfd, "g", acl_inherited_e,
	    ACL_INHERITED_MODE, detail, sizeof (detail));
	(void) close(ddfd);
	if (rf < 0 || rg < 0)
		return (result(sc, "WRONG-ACL", 0, detail));
	if (rf == 1 && rg == 1)
		return (result(sc, "OK", 1, " f and g"));
	return (result(sc, "ABSENT", prefix, rf == 1 ? " g missing" :
	    rg == 1 ? " f missing" : " neither"));
}

/*
 * "racewrite" and "racegrow": each RACE_PIECE of the recovered file must be
 * one written version (the initial contents or one racer pass): writes
 * after the publication are not synced before ACK and may be recovered in
 * part, but never as zeros or other bytes.  At ACK, the whole file is the
 * racer's last pass.
 */
static int
check_race(const char *sc, int dfd, int prefix)
{
	unsigned salt = salt_of(sc);
	char detail[256] = "";
	struct stat st;
	uint8_t *buf;
	int r = lookup(dfd, "f", &st, detail, sizeof (detail));

	if (r < 0)
		return (result(sc, "LOOKUP-ERROR", 0, detail));
	if (r == 0)
		return (result(sc, "ABSENT", prefix, " name-not-recovered"));
	uint64_t size = st.st_size;
	int size_ok = size == DATA_SIZE || (strcmp(sc, "racegrow") == 0 &&
	    (size == SMALL_SIZE || (size % RACE_CHUNK == 0 &&
	    size < DATA_SIZE)));
	(void) snprintf(detail, sizeof (detail), " size=%llu",
	    (unsigned long long)size);
	if (!size_ok || (!prefix && size != DATA_SIZE))
		return (result(sc, "WRONG-SIZE", 0, detail));
	int fd = openat(dfd, "f", O_RDONLY | O_NOFOLLOW);
	if (fd < 0 || (buf = malloc(RACE_PIECE)) == NULL)
		die("open f");
	for (uint64_t off = 0; off < size; off += RACE_PIECE) {
		size_t len = size - off < RACE_PIECE ? size - off : RACE_PIECE;
		if (pread(fd, buf, len, off) != (ssize_t)len)
			die("pread f");
		/* Candidate versions: final pass first, initial last. */
		int found = 0;
		for (int i = RACE_ITERS; i >= 0 && !found; i--) {
			unsigned s = i == 0 ? salt : race_salt(salt, i - 1);
			if (!prefix && i != RACE_ITERS)
				break;
			found = 1;
			for (size_t j = 0; j < len && found; j++)
				found = buf[j] == pattern(off + j, s);
		}
		if (!found) {
			(void) snprintf(detail + strlen(detail),
			    sizeof (detail) - strlen(detail),
			    " no-version@%llu", (unsigned long long)off);
			free(buf);
			(void) close(fd);
			return (result(sc, "WRONG-DATA", 0, detail));
		}
	}
	free(buf);
	(void) close(fd);
	return (result(sc, "OK", 1, detail));
}

/* Expected byte at off of f after a fast scenario's operation. */
static uint8_t
fast_after(const char *sc, uint64_t off, unsigned salt)
{
	if ((is_clone(sc) && strcmp(sc, "clonefastmid") != 0) ||
	    (strcmp(sc, "clonefastmid") == 0 && off >= FAST_OFF &&
	    off < FAST_OFF + FAST_LEN))
		return (pattern(off, salt + 7));
	return (pattern(off, salt));
}

/* Whether fd holds size bytes of the published (0) or later (1) state. */
static int
fast_matches(int fd, const char *sc, uint64_t size, unsigned salt, int after)
{
	uint8_t *buf = malloc(CHUNK);
	int ok = 1;

	if (buf == NULL)
		die("malloc");
	for (uint64_t off = 0; off < size && ok; off += CHUNK) {
		size_t len = size - off < CHUNK ? size - off : CHUNK;
		if (pread(fd, buf, len, off) != (ssize_t)len)
			die("pread f");
		for (size_t j = 0; j < len && ok; j++) {
			ok = buf[j] == (after ? fast_after(sc, off + j, salt) :
			    pattern(off + j, salt));
		}
	}
	free(buf);
	return (ok);
}

/*
 * The operations on a just-published f (is_fast()): f must be absent
 * (before ACK, or for removefast), as published, or as after the
 * operation; the marker is complete at ACK.  namedclone synced f, so at
 * ACK it must be as after the clone.
 */
static int
check_fast(const char *sc, int dfd, int prefix)
{
	unsigned salt = salt_of(sc);
	char detail[256] = "";
	struct stat st;
	int r;

	if (!prefix) {
		r = check_small(dfd, "marker", salt, detail, sizeof (detail));
		if (r != 1)
			return (result(sc, r == 0 ? "MARKER-ABSENT" :
			    "MARKER-WRONG", 0, detail));
	}
	r = lookup(dfd, "f", &st, detail, sizeof (detail));
	if (r < 0)
		return (result(sc, "LOOKUP-ERROR", 0, detail));
	int pin = !prefix && strcmp(sc, "namedclone") == 0;
	if (r == 0) {
		return (result(sc, "ABSENT", prefix ||
		    strcmp(sc, "removefast") == 0, " name-not-recovered"));
	}
	uint64_t size = st.st_size;
	uint64_t later = is_trunc(sc) ? fast_size(sc) : DATA_SIZE;
	(void) snprintf(detail, sizeof (detail), " size=%llu",
	    (unsigned long long)size);
	int fd = openat(dfd, "f", O_RDONLY | O_NOFOLLOW);
	if (fd < 0)
		die("open f");
	const char *state = NULL;
	if (!pin && size == DATA_SIZE && fast_matches(fd, sc, size, salt, 0))
		state = " published";
	else if (strcmp(sc, "removefast") != 0 && size == later &&
	    fast_matches(fd, sc, size, salt, 1))
		state = " after-operation";
	(void) close(fd);
	if (state == NULL)
		return (result(sc, "NO-SUCH-STATE", 0, detail));
	(void) strncat(detail, state, sizeof (detail) - strlen(detail) - 1);
	return (result(sc, "OK", 1, detail));
}

/*
 * "hugesparse": reading all of a 1T file is not practical.  Check the size,
 * the data at each end, and that little besides them is allocated
 * (st_blocks).
 */
static int
check_hugesparse(const char *sc, int dfd, int prefix)
{
	unsigned salt = salt_of(sc);
	char detail[256] = "";
	struct stat st;
	uint8_t buf[SMALL_SIZE];
	int r = lookup(dfd, "f", &st, detail, sizeof (detail));

	if (r < 0)
		return (result(sc, "LOOKUP-ERROR", 0, detail));
	if (r == 0)
		return (result(sc, "ABSENT", prefix, " name-not-recovered"));
	(void) snprintf(detail, sizeof (detail), " size=%lld",
	    (long long)st.st_size);
	if ((uint64_t)st.st_size != HUGE_SIZE)
		return (result(sc, "WRONG-SIZE", 0, detail));
	int fd = openat(dfd, "f", O_RDONLY | O_NOFOLLOW);
	if (fd < 0)
		die("open f");
	uint64_t ends[2] = { 0, HUGE_SIZE - SMALL_SIZE };
	for (int e = 0; e < 2; e++) {
		if (pread(fd, buf, SMALL_SIZE, ends[e]) != SMALL_SIZE)
			die("pread f");
		for (size_t j = 0; j < SMALL_SIZE; j++) {
			if (buf[j] != pattern(ends[e] + j, salt)) {
				(void) close(fd);
				return (result(sc, "WRONG-DATA", 0, detail));
			}
		}
	}
	/*
	 * Only the two ends hold data: what is allocated is bounded by a few
	 * blocks (data and indirect), not by the 1T size.  st_blocks, unlike
	 * SEEK_DATA, does not depend on zfs_dmu_offset_next_sync.
	 */
	(void) close(fd);
	if ((uint64_t)st.st_blocks * 512 > 16ULL * st.st_blksize) {
		(void) snprintf(detail + strlen(detail), sizeof (detail) -
		    strlen(detail), " blocks=%lld", (long long)st.st_blocks);
		return (result(sc, "WRONG-DATA", 0, detail));
	}
	return (result(sc, "OK", 1, detail));
}

static int
check_recovered(const char *sc, int dfd, int prefix)
{
	unsigned salt = salt_of(sc);
	int marked = strcmp(sc, "unlinked") == 0 ||
	    strcmp(sc, "nosync") == 0 || strcmp(sc, "multi") == 0 ||
	    strcmp(sc, "remove") == 0;
	char detail[256] = "";
	struct stat st;
	int r;

	/* Synced before START: must be complete at every crash point. */
	if (check_small(dfd, "setup", salt, detail, sizeof (detail)) != 1)
		return (result(sc, "SETUP-LOST", 0, detail));

	if (strcmp(sc, "relink") == 0)
		return (check_relink(sc, dfd, prefix));
	if (strcmp(sc, "rename") == 0)
		return (check_rename(sc, dfd, prefix));
	if (strcmp(sc, "acl") == 0)
		return (check_acl(sc, dfd, prefix));
	if (strcmp(sc, "racewrite") == 0 || strcmp(sc, "racegrow") == 0)
		return (check_race(sc, dfd, prefix));
	if (strcmp(sc, "hugesparse") == 0)
		return (check_hugesparse(sc, dfd, prefix));
	if (is_fast(sc))
		return (check_fast(sc, dfd, prefix));
	if (strcmp(sc, "reuse") == 0) {
		/* "a" is removed before ACK; until then, absent or complete. */
		r = check_file(dfd, "a", sc, SMALL_SIZE, 1, NULL, detail,
		    sizeof (detail));
		if (r < 0 || (r == 1 && !prefix))
			return (result(sc, r < 0 ? "WRONG" : "RESURRECTED", 0,
			    r < 0 ? detail : " a"));
	}

	/*
	 * The marker is an ordinary file, created and then written; only
	 * once its fsync has returned (at ACK) must it be complete.
	 */
	if (marked && !prefix) {
		r = check_small(dfd, "marker", salt, detail, sizeof (detail));
		if (r != 1)
			return (result(sc, r == 0 ? "MARKER-ABSENT" :
			    "MARKER-WRONG", 0, detail));
	} else if (marked) {
		r = lookup(dfd, "marker", &st, detail, sizeof (detail));
		if (r < 0)
			return (result(sc, "MARKER-WRONG", 0, detail));
	}

	/* The empty publication: absent (before ACK) or empty. */
	if (strcmp(sc, "multi") == 0) {
		r = lookup(dfd, "e", &st, detail, sizeof (detail));
		if (r < 0)
			return (result(sc, "LOOKUP-ERROR", 0, detail));
		if (r == 0 && !prefix)
			return (result(sc, "EMPTY-ABSENT", 0, NULL));
		if (r == 1 && st.st_size != 0)
			return (result(sc, "EMPTY-WRONG-SIZE", 0, NULL));
	}

	r = lookup(dfd, "f", &st, detail, sizeof (detail));
	if (r < 0)
		return (result(sc, "LOOKUP-ERROR", 0, detail));
	if (strcmp(sc, "unlinked") == 0)
		return (r == 0 ? result(sc, "OK", 1, " never-published") :
		    result(sc, "RESURRECTED", 0, NULL));
	/* Removed before ACK: absent there; before it, absent or complete. */
	if (strcmp(sc, "remove") == 0 && r == 0)
		return (result(sc, "OK", 1, " removed-or-unpublished"));
	if (strcmp(sc, "remove") == 0 && !prefix)
		return (result(sc, "RESURRECTED", 0, NULL));
	if (r == 0)
		return (result(sc, "ABSENT", prefix ||
		    strcmp(sc, "nosync") == 0, " name-not-recovered"));

	int fd = openat(dfd, "f", O_RDONLY | O_NOFOLLOW);
	if (fd < 0) {
		(void) snprintf(detail, sizeof (detail), " f: open errno %d",
		    errno);
		return (result(sc, "LOOKUP-ERROR", 0, detail));
	}
	uint64_t want_size = strcmp(sc, "small") == 0 ||
	    strcmp(sc, "clonesmall") == 0 ||
	    strcmp(sc, "meta") == 0 || strcmp(sc, "bigxattr") == 0 ?
	    SMALL_SIZE :
	    strcmp(sc, "sparse") == 0 || strcmp(sc, "hole") == 0 ?
	    SPARSE_SIZE :
	    strcmp(sc, "sparsemix") == 0 ? SPARSEMIX_SIZE :
	    strcmp(sc, "dense") == 0 ? DENSE_SIZE : DATA_SIZE;
	(void) snprintf(detail, sizeof (detail), " size=%lld/%llu uid=%u "
	    "gid=%u mode=%o mtime=%lld.%09ld", (long long)st.st_size,
	    (unsigned long long)want_size, st.st_uid, st.st_gid,
	    st.st_mode & 07777, (long long)st.st_mtim.tv_sec,
	    st.st_mtim.tv_nsec);
	if ((uint64_t)st.st_size != want_size)
		return (result(sc, st.st_size == 0 ? "EMPTY" : "WRONG-SIZE",
		    0, detail));
	const char *bad = check_content(fd, sc, want_size, salt);
	if (bad != NULL) {
		(void) strncat(detail, bad, sizeof (detail) - strlen(detail) -
		    1);
		return (result(sc, "WRONG-DATA", 0, detail));
	}
	if (strcmp(sc, "clonerm") == 0 && !prefix) {
		struct stat sst;
		r = lookup(dfd, "src", &sst, detail, sizeof (detail));
		if (r != 0)
			return (result(sc, r < 0 ? "LOOKUP-ERROR" :
			    "SRC-RESURRECTED", 0, detail));
	}
	if (strcmp(sc, "nodump") == 0 || strcmp(sc, "projid") == 0) {
		int fl = 0, ok;
		struct fsxattr fsx;
		if (strcmp(sc, "nodump") == 0) {
			ok = ioctl(fd, FS_IOC_GETFLAGS, &fl) == 0 &&
			    (fl & FS_NODUMP_FL);
		} else {
			ok = ioctl(fd, FS_IOC_FSGETXATTR, &fsx) == 0 &&
			    fsx.fsx_projid == META_PROJID;
		}
		if (!ok)
			return (result(sc, "WRONG-META", 0, detail));
	}
	if (strcmp(sc, "reuse") == 0 && (st.st_mode & 07777) != REUSE_MODE)
		return (result(sc, "WRONG-META", 0, detail));
	if (strcmp(sc, "meta") == 0) {
		char v[XATTR_LONG_LEN + 1], want[XATTR_LONG_LEN];
		ssize_t n1 = fgetxattr(fd, XATTR_SHORT, v, sizeof (v));
		int ok1 = n1 == 2 && memcmp(v, "v1", 2) == 0;
		ssize_t n2 = fgetxattr(fd, XATTR_LONG, v, sizeof (v));
		fill_xattr(want, XATTR_LONG_LEN);
		int ok2 = n2 == XATTR_LONG_LEN && memcmp(v, want, n2) == 0;
		int okm = st.st_uid == META_UID && st.st_gid == META_GID &&
		    (st.st_mode & 07777) == META_MODE;
		int okt = st.st_mtim.tv_sec == META_MTIME &&
		    st.st_mtim.tv_nsec == META_MTIME_NS;
		(void) snprintf(detail + strlen(detail), sizeof (detail) -
		    strlen(detail), " xattr_short=%s xattr_long=%s",
		    ok1 ? "ok" : "BAD", ok2 ? "ok" : "BAD");
		if (!ok1 || !ok2 || !okm || !okt)
			return (result(sc, "WRONG-META", 0, detail));
	}
	if (strcmp(sc, "bigxattr") == 0) {
		char s[3];
		char *v = malloc(XATTR_BIG_LEN + 1);
		char *want = malloc(XATTR_BIG_LEN);
		if (v == NULL || want == NULL)
			die("malloc");
		ssize_t n1 = fgetxattr(fd, XATTR_SHORT, s, sizeof (s));
		int ok1 = n1 == 2 && memcmp(s, "v1", 2) == 0;
		ssize_t n2 = fgetxattr(fd, XATTR_BIG, v, XATTR_BIG_LEN + 1);
		fill_xattr(want, XATTR_BIG_LEN);
		int ok2 = n2 == XATTR_BIG_LEN && memcmp(v, want, n2) == 0;
		free(v);
		free(want);
		(void) snprintf(detail + strlen(detail), sizeof (detail) -
		    strlen(detail), " xattr_short=%s xattr_big=%s",
		    ok1 ? "ok" : "BAD", ok2 ? "ok" : "BAD");
		if (!ok1 || !ok2)
			return (result(sc, "WRONG-META", 0, detail));
	}
	(void) close(fd);
	return (result(sc, "OK", 1, detail));
}

int
main(int argc, char **argv)
{
	if (argc < 4 || (strcmp(argv[1], "run") == 0 && argc < 5)) {
		(void) fprintf(stderr, "usage: %s run <scenario> <dir> <pool>\n"
		    "       %s verify|verify-prefix <scenario> <dir>\n",
		    argv[0], argv[0]);
		return (2);
	}
	int dfd = open(argv[3], O_RDONLY | O_DIRECTORY);
	if (dfd < 0)
		die("open dir");
	if (strcmp(argv[1], "run") == 0) {
		pool = argv[4];
		run(argv[2], dfd);
	}
	if (strcmp(argv[1], "verify") == 0)
		return (check_recovered(argv[2], dfd, 0));
	if (strcmp(argv[1], "verify-prefix") == 0)
		return (check_recovered(argv[2], dfd, 1));
	(void) fprintf(stderr, "unknown mode %s\n", argv[1]);
	return (2);
}
