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
 * Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

/*
 * The objective of this program is to provide a DMU/ZAP/SPA stress test
 * that runs entirely in userland, is easy to use, and easy to extend.
 *
 * The overall design of the ztest program is as follows:
 *
 * (1) For each major functional area (e.g. adding vdevs to a pool,
 *     creating and destroying datasets, reading and writing objects, etc)
 *     we have a simple routine to test that functionality.  These
 *     individual routines do not have to do anything "stressful".
 *
 * (2) We turn these simple functionality tests into a stress test by
 *     running them all in parallel, with as many threads as desired,
 *     and spread across as many datasets, objects, and vdevs as desired.
 *
 * (3) While all this is happening, we inject faults into the pool to
 *     verify that self-healing data really works.
 *
 * (4) Every time we open a dataset, we change its checksum and compression
 *     functions.  Thus even individual objects vary from block to block
 *     in which checksum they use and whether they're compressed.
 *
 * (5) To verify that we never lose on-disk consistency after a crash,
 *     we run the entire test in a child of the main process.
 *     At random times, the child self-immolates with a SIGKILL.
 *     This is the software equivalent of pulling the power cord.
 *     The parent then runs the test again, using the existing
 *     storage pool, as many times as desired.
 *
 * (6) To verify that we don't have future leaks or temporal incursions,
 *     many of the functional tests record the transaction group number
 *     as part of their data.  When reading old data, they verify that
 *     the transaction group number is less than the current, open txg.
 *     If you add a new test, please do this if applicable.
 *
 * When run with no arguments, ztest runs for about five minutes and
 * produces no output if successful.  To get a little bit of information,
 * specify -V.  To get more information, specify -VV, and so on.
 *
 * To turn this into an overnight stress test, use -T to specify run time.
 *
 * You can ask more more vdevs [-v], datasets [-d], or threads [-t]
 * to increase the pool capacity, fanout, and overall stress level.
 *
 * The -N(okill) option will suppress kills, so each child runs to completion.
 * This can be useful when you're trying to distinguish temporal incursions
 * from plain old race conditions.
 */

#include <sys/zfs_context.h>
#include <sys/spa.h>
#include <sys/dmu.h>
#include <sys/txg.h>
#include <sys/zap.h>
#include <sys/dmu_objset.h>
#include <sys/poll.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/zio.h>
#include <sys/zio_checksum.h>
#include <sys/zio_compress.h>
#include <sys/zil.h>
#include <sys/vdev_impl.h>
#include <sys/vdev_file.h>
#include <sys/spa_impl.h>
#include <sys/dsl_prop.h>
#include <sys/refcount.h>
#include <stdio.h>
#include <stdio_ext.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <umem.h>
#include <dlfcn.h>
#include <ctype.h>
#include <math.h>
#include <sys/fs/zfs.h>

static char cmdname[] = "ztest";
static char *zopt_pool = cmdname;

static uint64_t zopt_vdevs = 5;
static uint64_t zopt_vdevtime;
static int zopt_ashift = SPA_MINBLOCKSHIFT;
static int zopt_mirrors = 2;
static int zopt_raidz = 4;
static int zopt_raidz_parity = 1;
static size_t zopt_vdev_size = SPA_MINDEVSIZE;
static int zopt_datasets = 7;
static int zopt_threads = 23;
static uint64_t zopt_passtime = 60;	/* 60 seconds */
static uint64_t zopt_killrate = 70;	/* 70% kill rate */
static int zopt_verbose = 0;
static int zopt_init = 1;
static char *zopt_dir = "/tmp";
static uint64_t zopt_time = 300;	/* 5 minutes */
static int zopt_maxfaults;

typedef struct ztest_block_tag {
	uint64_t	bt_objset;
	uint64_t	bt_object;
	uint64_t	bt_offset;
	uint64_t	bt_txg;
	uint64_t	bt_thread;
	uint64_t	bt_seq;
} ztest_block_tag_t;

typedef struct ztest_args {
	char		za_pool[MAXNAMELEN];
	spa_t		*za_spa;
	objset_t	*za_os;
	zilog_t		*za_zilog;
	thread_t	za_thread;
	uint64_t	za_instance;
	uint64_t	za_random;
	uint64_t	za_diroff;
	uint64_t	za_diroff_shared;
	uint64_t	za_zil_seq;
	hrtime_t	za_start;
	hrtime_t	za_stop;
	hrtime_t	za_kill;
	/*
	 * Thread-local variables can go here to aid debugging.
	 */
	ztest_block_tag_t za_rbt;
	ztest_block_tag_t za_wbt;
	dmu_object_info_t za_doi;
	dmu_buf_t	*za_dbuf;
} ztest_args_t;

typedef void ztest_func_t(ztest_args_t *);

/*
 * Note: these aren't static because we want dladdr() to work.
 */
ztest_func_t ztest_dmu_read_write;
ztest_func_t ztest_dmu_write_parallel;
ztest_func_t ztest_dmu_object_alloc_free;
ztest_func_t ztest_zap;
ztest_func_t ztest_zap_parallel;
ztest_func_t ztest_traverse;
ztest_func_t ztest_dsl_prop_get_set;
ztest_func_t ztest_dmu_objset_create_destroy;
ztest_func_t ztest_dmu_snapshot_create_destroy;
ztest_func_t ztest_spa_create_destroy;
ztest_func_t ztest_fault_inject;
ztest_func_t ztest_spa_rename;
ztest_func_t ztest_vdev_attach_detach;
ztest_func_t ztest_vdev_LUN_growth;
ztest_func_t ztest_vdev_add_remove;
ztest_func_t ztest_vdev_aux_add_remove;
ztest_func_t ztest_scrub;

typedef struct ztest_info {
	ztest_func_t	*zi_func;	/* test function */
	uint64_t	zi_iters;	/* iterations per execution */
	uint64_t	*zi_interval;	/* execute every <interval> seconds */
	uint64_t	zi_calls;	/* per-pass count */
	uint64_t	zi_call_time;	/* per-pass time */
	uint64_t	zi_call_total;	/* cumulative total */
	uint64_t	zi_call_target;	/* target cumulative total */
} ztest_info_t;

uint64_t zopt_always = 0;		/* all the time */
uint64_t zopt_often = 1;		/* every second */
uint64_t zopt_sometimes = 10;		/* every 10 seconds */
uint64_t zopt_rarely = 60;		/* every 60 seconds */

ztest_info_t ztest_info[] = {
	{ ztest_dmu_read_write,			1,	&zopt_always	},
	{ ztest_dmu_write_parallel,		30,	&zopt_always	},
	{ ztest_dmu_object_alloc_free,		1,	&zopt_always	},
	{ ztest_zap,				30,	&zopt_always	},
	{ ztest_zap_parallel,			100,	&zopt_always	},
	{ ztest_dsl_prop_get_set,		1,	&zopt_sometimes	},
	{ ztest_dmu_objset_create_destroy,	1,	&zopt_sometimes },
	{ ztest_dmu_snapshot_create_destroy,	1,	&zopt_sometimes },
	{ ztest_spa_create_destroy,		1,	&zopt_sometimes },
	{ ztest_fault_inject,			1,	&zopt_sometimes	},
	{ ztest_spa_rename,			1,	&zopt_rarely	},
	{ ztest_vdev_attach_detach,		1,	&zopt_rarely	},
	{ ztest_vdev_LUN_growth,		1,	&zopt_rarely	},
	{ ztest_vdev_add_remove,		1,	&zopt_vdevtime	},
	{ ztest_vdev_aux_add_remove,		1,	&zopt_vdevtime	},
	{ ztest_scrub,				1,	&zopt_vdevtime	},
};

#define	ZTEST_FUNCS	(sizeof (ztest_info) / sizeof (ztest_info_t))

#define	ZTEST_SYNC_LOCKS	16

/*
 * Stuff we need to share writably between parent and child.
 */
typedef struct ztest_shared {
	mutex_t		zs_vdev_lock;
	rwlock_t	zs_name_lock;
	uint64_t	zs_vdev_primaries;
	uint64_t	zs_vdev_aux;
	uint64_t	zs_enospc_count;
	hrtime_t	zs_start_time;
	hrtime_t	zs_stop_time;
	uint64_t	zs_alloc;
	uint64_t	zs_space;
	ztest_info_t	zs_info[ZTEST_FUNCS];
	mutex_t		zs_sync_lock[ZTEST_SYNC_LOCKS];
	uint64_t	zs_seq[ZTEST_SYNC_LOCKS];
} ztest_shared_t;

static char ztest_dev_template[] = "%s/%s.%llua";
static char ztest_aux_template[] = "%s/%s.%s.%llu";
static ztest_shared_t *ztest_shared;

static int ztest_random_fd;
static int ztest_dump_core = 1;

static boolean_t ztest_exiting;

extern uint64_t metaslab_gang_bang;

#define	ZTEST_DIROBJ		1
#define	ZTEST_MICROZAP_OBJ	2
#define	ZTEST_FATZAP_OBJ	3

#define	ZTEST_DIROBJ_BLOCKSIZE	(1 << 10)
#define	ZTEST_DIRSIZE		256

static void usage(boolean_t) __NORETURN;

/*
 * These libumem hooks provide a reasonable set of defaults for the allocator's
 * debugging facilities.
 */
const char *
_umem_debug_init()
{
	return ("default,verbose"); /* $UMEM_DEBUG setting */
}

const char *
_umem_logging_init(void)
{
	return ("fail,contents"); /* $UMEM_LOGGING setting */
}

#define	FATAL_MSG_SZ	1024

char *fatal_msg;

static void
fatal(int do_perror, char *message, ...)
{
	va_list args;
	int save_errno = errno;
	char buf[FATAL_MSG_SZ];

	(void) fflush(stdout);

	va_start(args, message);
	(void) sprintf(buf, "ztest: ");
	/* LINTED */
	(void) vsprintf(buf + strlen(buf), message, args);
	va_end(args);
	if (do_perror) {
		(void) snprintf(buf + strlen(buf), FATAL_MSG_SZ - strlen(buf),
		    ": %s", strerror(save_errno));
	}
	(void) fprintf(stderr, "%s\n", buf);
	fatal_msg = buf;			/* to ease debugging */
	if (ztest_dump_core)
		abort();
	exit(3);
}

static int
str2shift(const char *buf)
{
	const char *ends = "BKMGTPEZ";
	int i;

	if (buf[0] == '\0')
		return (0);
	for (i = 0; i < strlen(ends); i++) {
		if (toupper(buf[0]) == ends[i])
			break;
	}
	if (i == strlen(ends)) {
		(void) fprintf(stderr, "ztest: invalid bytes suffix: %s\n",
		    buf);
		usage(B_FALSE);
	}
	if (buf[1] == '\0' || (toupper(buf[1]) == 'B' && buf[2] == '\0')) {
		return (10*i);
	}
	(void) fprintf(stderr, "ztest: invalid bytes suffix: %s\n", buf);
	usage(B_FALSE);
	/* NOTREACHED */
}

static uint64_t
nicenumtoull(const char *buf)
{
	char *end;
	uint64_t val;

	val = strtoull(buf, &end, 0);
	if (end == buf) {
		(void) fprintf(stderr, "ztest: bad numeric value: %s\n", buf);
		usage(B_FALSE);
	} else if (end[0] == '.') {
		double fval = strtod(buf, &end);
		fval *= pow(2, str2shift(end));
		if (fval > UINT64_MAX) {
			(void) fprintf(stderr, "ztest: value too large: %s\n",
			    buf);
			usage(B_FALSE);
		}
		val = (uint64_t)fval;
	} else {
		int shift = str2shift(end);
		if (shift >= 64 || (val << shift) >> shift != val) {
			(void) fprintf(stderr, "ztest: value too large: %s\n",
			    buf);
			usage(B_FALSE);
		}
		val <<= shift;
	}
	return (val);
}

static void
usage(boolean_t requested)
{
	char nice_vdev_size[10];
	char nice_gang_bang[10];
	FILE *fp = requested ? stdout : stderr;

	nicenum(zopt_vdev_size, nice_vdev_size);
	nicenum(metaslab_gang_bang, nice_gang_bang);

	(void) fprintf(fp, "Usage: %s\n"
	    "\t[-v vdevs (default: %llu)]\n"
	    "\t[-s size_of_each_vdev (default: %s)]\n"
	    "\t[-a alignment_shift (default: %d) (use 0 for random)]\n"
	    "\t[-m mirror_copies (default: %d)]\n"
	    "\t[-r raidz_disks (default: %d)]\n"
	    "\t[-R raidz_parity (default: %d)]\n"
	    "\t[-d datasets (default: %d)]\n"
	    "\t[-t threads (default: %d)]\n"
	    "\t[-g gang_block_threshold (default: %s)]\n"
	    "\t[-i initialize pool i times (default: %d)]\n"
	    "\t[-k kill percentage (default: %llu%%)]\n"
	    "\t[-p pool_name (default: %s)]\n"
	    "\t[-f file directory for vdev files (default: %s)]\n"
	    "\t[-V(erbose)] (use multiple times for ever more blather)\n"
	    "\t[-E(xisting)] (use existing pool instead of creating new one)\n"
	    "\t[-T time] total run time (default: %llu sec)\n"
	    "\t[-P passtime] time per pass (default: %llu sec)\n"
	    "\t[-h] (print help)\n"
	    "",
	    cmdname,
	    (u_longlong_t)zopt_vdevs,			/* -v */
	    nice_vdev_size,				/* -s */
	    zopt_ashift,				/* -a */
	    zopt_mirrors,				/* -m */
	    zopt_raidz,					/* -r */
	    zopt_raidz_parity,				/* -R */
	    zopt_datasets,				/* -d */
	    zopt_threads,				/* -t */
	    nice_gang_bang,				/* -g */
	    zopt_init,					/* -i */
	    (u_longlong_t)zopt_killrate,		/* -k */
	    zopt_pool,					/* -p */
	    zopt_dir,					/* -f */
	    (u_longlong_t)zopt_time,			/* -T */
	    (u_longlong_t)zopt_passtime);		/* -P */
	exit(requested ? 0 : 1);
}

static uint64_t
ztest_random(uint64_t range)
{
	uint64_t r;

	if (range == 0)
		return (0);

	if (read(ztest_random_fd, &r, sizeof (r)) != sizeof (r))
		fatal(1, "short read from /dev/urandom");

	return (r % range);
}

static void
ztest_record_enospc(char *s)
{
	dprintf("ENOSPC doing: %s\n", s ? s : "<unknown>");
	ztest_shared->zs_enospc_count++;
}

static void
process_options(int argc, char **argv)
{
	int opt;
	uint64_t value;

	/* By default, test gang blocks for blocks 32K and greater */
	metaslab_gang_bang = 32 << 10;

	while ((opt = getopt(argc, argv,
	    "v:s:a:m:r:R:d:t:g:i:k:p:f:VET:P:h")) != EOF) {
		value = 0;
		switch (opt) {
		case 'v':
		case 's':
		case 'a':
		case 'm':
		case 'r':
		case 'R':
		case 'd':
		case 't':
		case 'g':
		case 'i':
		case 'k':
		case 'T':
		case 'P':
			value = nicenumtoull(optarg);
		}
		switch (opt) {
		case 'v':
			zopt_vdevs = value;
			break;
		case 's':
			zopt_vdev_size = MAX(SPA_MINDEVSIZE, value);
			break;
		case 'a':
			zopt_ashift = value;
			break;
		case 'm':
			zopt_mirrors = value;
			break;
		case 'r':
			zopt_raidz = MAX(1, value);
			break;
		case 'R':
			zopt_raidz_parity = MIN(MAX(value, 1), 2);
			break;
		case 'd':
			zopt_datasets = MAX(1, value);
			break;
		case 't':
			zopt_threads = MAX(1, value);
			break;
		case 'g':
			metaslab_gang_bang = MAX(SPA_MINBLOCKSIZE << 1, value);
			break;
		case 'i':
			zopt_init = value;
			break;
		case 'k':
			zopt_killrate = value;
			break;
		case 'p':
			zopt_pool = strdup(optarg);
			break;
		case 'f':
			zopt_dir = strdup(optarg);
			break;
		case 'V':
			zopt_verbose++;
			break;
		case 'E':
			zopt_init = 0;
			break;
		case 'T':
			zopt_time = value;
			break;
		case 'P':
			zopt_passtime = MAX(1, value);
			break;
		case 'h':
			usage(B_TRUE);
			break;
		case '?':
		default:
			usage(B_FALSE);
			break;
		}
	}

	zopt_raidz_parity = MIN(zopt_raidz_parity, zopt_raidz - 1);

	zopt_vdevtime = (zopt_vdevs > 0 ? zopt_time / zopt_vdevs : UINT64_MAX);
	zopt_maxfaults = MAX(zopt_mirrors, 1) * (zopt_raidz_parity + 1) - 1;
}

static uint64_t
ztest_get_ashift(void)
{
	if (zopt_ashift == 0)
		return (SPA_MINBLOCKSHIFT + ztest_random(3));
	return (zopt_ashift);
}

static nvlist_t *
make_vdev_file(char *path, char *aux, size_t size, uint64_t ashift)
{
	char pathbuf[MAXPATHLEN];
	uint64_t vdev;
	nvlist_t *file;

	if (ashift == 0)
		ashift = ztest_get_ashift();

	if (path == NULL) {
		path = pathbuf;

		if (aux != NULL) {
			vdev = ztest_shared->zs_vdev_aux;
			(void) sprintf(path, ztest_aux_template,
			    zopt_dir, zopt_pool, aux, vdev);
		} else {
			vdev = ztest_shared->zs_vdev_primaries++;
			(void) sprintf(path, ztest_dev_template,
			    zopt_dir, zopt_pool, vdev);
		}
	}

	if (size != 0) {
		int fd = open(path, O_RDWR | O_CREAT | O_TRUNC, 0666);
		if (fd == -1)
			fatal(1, "can't open %s", path);
		if (ftruncate(fd, size) != 0)
			fatal(1, "can't ftruncate %s", path);
		(void) close(fd);
	}

	VERIFY(nvlist_alloc(&file, NV_UNIQUE_NAME, 0) == 0);
	VERIFY(nvlist_add_string(file, ZPOOL_CONFIG_TYPE, VDEV_TYPE_FILE) == 0);
	VERIFY(nvlist_add_string(file, ZPOOL_CONFIG_PATH, path) == 0);
	VERIFY(nvlist_add_uint64(file, ZPOOL_CONFIG_ASHIFT, ashift) == 0);

	return (file);
}

static nvlist_t *
make_vdev_raidz(char *path, char *aux, size_t size, uint64_t ashift, int r)
{
	nvlist_t *raidz, **child;
	int c;

	if (r < 2)
		return (make_vdev_file(path, aux, size, ashift));
	child = umem_alloc(r * sizeof (nvlist_t *), UMEM_NOFAIL);

	for (c = 0; c < r; c++)
		child[c] = make_vdev_file(path, aux, size, ashift);

	VERIFY(nvlist_alloc(&raidz, NV_UNIQUE_NAME, 0) == 0);
	VERIFY(nvlist_add_string(raidz, ZPOOL_CONFIG_TYPE,
	    VDEV_TYPE_RAIDZ) == 0);
	VERIFY(nvlist_add_uint64(raidz, ZPOOL_CONFIG_NPARITY,
	    zopt_raidz_parity) == 0);
	VERIFY(nvlist_add_nvlist_array(raidz, ZPOOL_CONFIG_CHILDREN,
	    child, r) == 0);

	for (c = 0; c < r; c++)
		nvlist_free(child[c]);

	umem_free(child, r * sizeof (nvlist_t *));

	return (raidz);
}

static nvlist_t *
make_vdev_mirror(char *path, char *aux, size_t size, uint64_t ashift,
	int r, int m)
{
	nvlist_t *mirror, **child;
	int c;

	if (m < 1)
		return (make_vdev_raidz(path, aux, size, ashift, r));

	child = umem_alloc(m * sizeof (nvlist_t *), UMEM_NOFAIL);

	for (c = 0; c < m; c++)
		child[c] = make_vdev_raidz(path, aux, size, ashift, r);

	VERIFY(nvlist_alloc(&mirror, NV_UNIQUE_NAME, 0) == 0);
	VERIFY(nvlist_add_string(mirror, ZPOOL_CONFIG_TYPE,
	    VDEV_TYPE_MIRROR) == 0);
	VERIFY(nvlist_add_nvlist_array(mirror, ZPOOL_CONFIG_CHILDREN,
	    child, m) == 0);

	for (c = 0; c < m; c++)
		nvlist_free(child[c]);

	umem_free(child, m * sizeof (nvlist_t *));

	return (mirror);
}

static nvlist_t *
make_vdev_root(char *path, char *aux, size_t size, uint64_t ashift,
	int log, int r, int m, int t)
{
	nvlist_t *root, **child;
	int c;

	ASSERT(t > 0);

	child = umem_alloc(t * sizeof (nvlist_t *), UMEM_NOFAIL);

	for (c = 0; c < t; c++) {
		child[c] = make_vdev_mirror(path, aux, size, ashift, r, m);
		VERIFY(nvlist_add_uint64(child[c], ZPOOL_CONFIG_IS_LOG,
		    log) == 0);
	}

	VERIFY(nvlist_alloc(&root, NV_UNIQUE_NAME, 0) == 0);
	VERIFY(nvlist_add_string(root, ZPOOL_CONFIG_TYPE, VDEV_TYPE_ROOT) == 0);
	VERIFY(nvlist_add_nvlist_array(root, aux ? aux : ZPOOL_CONFIG_CHILDREN,
	    child, t) == 0);

	for (c = 0; c < t; c++)
		nvlist_free(child[c]);

	umem_free(child, t * sizeof (nvlist_t *));

	return (root);
}

static void
ztest_set_random_blocksize(objset_t *os, uint64_t object, dmu_tx_t *tx)
{
	int bs = SPA_MINBLOCKSHIFT +
	    ztest_random(SPA_MAXBLOCKSHIFT - SPA_MINBLOCKSHIFT + 1);
	int ibs = DN_MIN_INDBLKSHIFT +
	    ztest_random(DN_MAX_INDBLKSHIFT - DN_MIN_INDBLKSHIFT + 1);
	int error;

	error = dmu_object_set_blocksize(os, object, 1ULL << bs, ibs, tx);
	if (error) {
		char osname[300];
		dmu_objset_name(os, osname);
		fatal(0, "dmu_object_set_blocksize('%s', %llu, %d, %d) = %d",
		    osname, object, 1 << bs, ibs, error);
	}
}

static uint8_t
ztest_random_checksum(void)
{
	uint8_t checksum;

	do {
		checksum = ztest_random(ZIO_CHECKSUM_FUNCTIONS);
	} while (zio_checksum_table[checksum].ci_zbt);

	if (checksum == ZIO_CHECKSUM_OFF)
		checksum = ZIO_CHECKSUM_ON;

	return (checksum);
}

static uint8_t
ztest_random_compress(void)
{
	return ((uint8_t)ztest_random(ZIO_COMPRESS_FUNCTIONS));
}

typedef struct ztest_replay {
	objset_t	*zr_os;
	uint64_t	zr_assign;
} ztest_replay_t;

static int
ztest_replay_create(ztest_replay_t *zr, lr_create_t *lr, boolean_t byteswap)
{
	objset_t *os = zr->zr_os;
	dmu_tx_t *tx;
	int error;

	if (byteswap)
		byteswap_uint64_array(lr, sizeof (*lr));

	tx = dmu_tx_create(os);
	dmu_tx_hold_bonus(tx, DMU_NEW_OBJECT);
	error = dmu_tx_assign(tx, zr->zr_assign);
	if (error) {
		dmu_tx_abort(tx);
		return (error);
	}

	error = dmu_object_claim(os, lr->lr_doid, lr->lr_mode, 0,
	    DMU_OT_NONE, 0, tx);
	ASSERT3U(error, ==, 0);
	dmu_tx_commit(tx);

	if (zopt_verbose >= 5) {
		char osname[MAXNAMELEN];
		dmu_objset_name(os, osname);
		(void) printf("replay create of %s object %llu"
		    " in txg %llu = %d\n",
		    osname, (u_longlong_t)lr->lr_doid,
		    (u_longlong_t)zr->zr_assign, error);
	}

	return (error);
}

static int
ztest_replay_remove(ztest_replay_t *zr, lr_remove_t *lr, boolean_t byteswap)
{
	objset_t *os = zr->zr_os;
	dmu_tx_t *tx;
	int error;

	if (byteswap)
		byteswap_uint64_array(lr, sizeof (*lr));

	tx = dmu_tx_create(os);
	dmu_tx_hold_free(tx, lr->lr_doid, 0, DMU_OBJECT_END);
	error = dmu_tx_assign(tx, zr->zr_assign);
	if (error) {
		dmu_tx_abort(tx);
		return (error);
	}

	error = dmu_object_free(os, lr->lr_doid, tx);
	dmu_tx_commit(tx);

	return (error);
}

zil_replay_func_t *ztest_replay_vector[TX_MAX_TYPE] = {
	NULL,			/* 0 no such transaction type */
	ztest_replay_create,	/* TX_CREATE */
	NULL,			/* TX_MKDIR */
	NULL,			/* TX_MKXATTR */
	NULL,			/* TX_SYMLINK */
	ztest_replay_remove,	/* TX_REMOVE */
	NULL,			/* TX_RMDIR */
	NULL,			/* TX_LINK */
	NULL,			/* TX_RENAME */
	NULL,			/* TX_WRITE */
	NULL,			/* TX_TRUNCATE */
	NULL,			/* TX_SETATTR */
	NULL,			/* TX_ACL */
};

/*
 * Verify that we can't destroy an active pool, create an existing pool,
 * or create a pool with a bad vdev spec.
 */
void
ztest_spa_create_destroy(ztest_args_t *za)
{
	int error;
	spa_t *spa;
	nvlist_t *nvroot;

	/*
	 * Attempt to create using a bad file.
	 */
	nvroot = make_vdev_root("/dev/bogus", NULL, 0, 0, 0, 0, 0, 1);
	error = spa_create("ztest_bad_file", nvroot, NULL, NULL, NULL);
	nvlist_free(nvroot);
	if (error != ENOENT)
		fatal(0, "spa_create(bad_file) = %d", error);

	/*
	 * Attempt to create using a bad mirror.
	 */
	nvroot = make_vdev_root("/dev/bogus", NULL, 0, 0, 0, 0, 2, 1);
	error = spa_create("ztest_bad_mirror", nvroot, NULL, NULL, NULL);
	nvlist_free(nvroot);
	if (error != ENOENT)
		fatal(0, "spa_create(bad_mirror) = %d", error);

	/*
	 * Attempt to create an existing pool.  It shouldn't matter
	 * what's in the nvroot; we should fail with EEXIST.
	 */
	(void) rw_rdlock(&ztest_shared->zs_name_lock);
	nvroot = make_vdev_root("/dev/bogus", NULL, 0, 0, 0, 0, 0, 1);
	error = spa_create(za->za_pool, nvroot, NULL, NULL, NULL);
	nvlist_free(nvroot);
	if (error != EEXIST)
		fatal(0, "spa_create(whatever) = %d", error);

	error = spa_open(za->za_pool, &spa, FTAG);
	if (error)
		fatal(0, "spa_open() = %d", error);

	error = spa_destroy(za->za_pool);
	if (error != EBUSY)
		fatal(0, "spa_destroy() = %d", error);

	spa_close(spa, FTAG);
	(void) rw_unlock(&ztest_shared->zs_name_lock);
}

static vdev_t *
vdev_lookup_by_path(vdev_t *vd, const char *path)
{
	vdev_t *mvd;

	if (vd->vdev_path != NULL && strcmp(path, vd->vdev_path) == 0)
		return (vd);

	for (int c = 0; c < vd->vdev_children; c++)
		if ((mvd = vdev_lookup_by_path(vd->vdev_child[c], path)) !=
		    NULL)
			return (mvd);

	return (NULL);
}

/*
 * Verify that vdev_add() works as expected.
 */
void
ztest_vdev_add_remove(ztest_args_t *za)
{
	spa_t *spa = za->za_spa;
	uint64_t leaves = MAX(zopt_mirrors, 1) * zopt_raidz;
	nvlist_t *nvroot;
	int error;

	(void) mutex_lock(&ztest_shared->zs_vdev_lock);

	spa_config_enter(spa, SCL_VDEV, FTAG, RW_READER);

	ztest_shared->zs_vdev_primaries =
	    spa->spa_root_vdev->vdev_children * leaves;

	spa_config_exit(spa, SCL_VDEV, FTAG);

	/*
	 * Make 1/4 of the devices be log devices.
	 */
	nvroot = make_vdev_root(NULL, NULL, zopt_vdev_size, 0,
	    ztest_random(4) == 0, zopt_raidz, zopt_mirrors, 1);

	error = spa_vdev_add(spa, nvroot);
	nvlist_free(nvroot);

	(void) mutex_unlock(&ztest_shared->zs_vdev_lock);

	if (error == ENOSPC)
		ztest_record_enospc("spa_vdev_add");
	else if (error != 0)
		fatal(0, "spa_vdev_add() = %d", error);
}

/*
 * Verify that adding/removing aux devices (l2arc, hot spare) works as expected.
 */
void
ztest_vdev_aux_add_remove(ztest_args_t *za)
{
	spa_t *spa = za->za_spa;
	vdev_t *rvd = spa->spa_root_vdev;
	spa_aux_vdev_t *sav;
	char *aux;
	uint64_t guid = 0;
	int error;

	if (ztest_random(2) == 0) {
		sav = &spa->spa_spares;
		aux = ZPOOL_CONFIG_SPARES;
	} else {
		sav = &spa->spa_l2cache;
		aux = ZPOOL_CONFIG_L2CACHE;
	}

	(void) mutex_lock(&ztest_shared->zs_vdev_lock);

	spa_config_enter(spa, SCL_VDEV, FTAG, RW_READER);

	if (sav->sav_count != 0 && ztest_random(4) == 0) {
		/*
		 * Pick a random device to remove.
		 */
		guid = sav->sav_vdevs[ztest_random(sav->sav_count)]->vdev_guid;
	} else {
		/*
		 * Find an unused device we can add.
		 */
		ztest_shared->zs_vdev_aux = 0;
		for (;;) {
			char path[MAXPATHLEN];
			int c;
			(void) sprintf(path, ztest_aux_template, zopt_dir,
			    zopt_pool, aux, ztest_shared->zs_vdev_aux);
			for (c = 0; c < sav->sav_count; c++)
				if (strcmp(sav->sav_vdevs[c]->vdev_path,
				    path) == 0)
					break;
			if (c == sav->sav_count &&
			    vdev_lookup_by_path(rvd, path) == NULL)
				break;
			ztest_shared->zs_vdev_aux++;
		}
	}

	spa_config_exit(spa, SCL_VDEV, FTAG);

	if (guid == 0) {
		/*
		 * Add a new device.
		 */
		nvlist_t *nvroot = make_vdev_root(NULL, aux,
		    (zopt_vdev_size * 5) / 4, 0, 0, 0, 0, 1);
		error = spa_vdev_add(spa, nvroot);
		if (error != 0)
			fatal(0, "spa_vdev_add(%p) = %d", nvroot, error);
		nvlist_free(nvroot);
	} else {
		/*
		 * Remove an existing device.  Sometimes, dirty its
		 * vdev state first to make sure we handle removal
		 * of devices that have pending state changes.
		 */
		if (ztest_random(2) == 0)
			(void) vdev_online(spa, guid, B_FALSE, NULL);

		error = spa_vdev_remove(spa, guid, B_FALSE);
		if (error != 0 && error != EBUSY)
			fatal(0, "spa_vdev_remove(%llu) = %d", guid, error);
	}

	(void) mutex_unlock(&ztest_shared->zs_vdev_lock);
}

/*
 * Verify that we can attach and detach devices.
 */
void
ztest_vdev_attach_detach(ztest_args_t *za)
{
	spa_t *spa = za->za_spa;
	spa_aux_vdev_t *sav = &spa->spa_spares;
	vdev_t *rvd = spa->spa_root_vdev;
	vdev_t *oldvd, *newvd, *pvd;
	nvlist_t *root;
	uint64_t leaves = MAX(zopt_mirrors, 1) * zopt_raidz;
	uint64_t leaf, top;
	uint64_t ashift = ztest_get_ashift();
	uint64_t oldguid;
	size_t oldsize, newsize;
	char oldpath[MAXPATHLEN], newpath[MAXPATHLEN];
	int replacing;
	int oldvd_has_siblings = B_FALSE;
	int newvd_is_spare = B_FALSE;
	int oldvd_is_log;
	int error, expected_error;

	(void) mutex_lock(&ztest_shared->zs_vdev_lock);

	spa_config_enter(spa, SCL_VDEV, FTAG, RW_READER);

	/*
	 * Decide whether to do an attach or a replace.
	 */
	replacing = ztest_random(2);

	/*
	 * Pick a random top-level vdev.
	 */
	top = ztest_random(rvd->vdev_children);

	/*
	 * Pick a random leaf within it.
	 */
	leaf = ztest_random(leaves);

	/*
	 * Locate this vdev.
	 */
	oldvd = rvd->vdev_child[top];
	if (zopt_mirrors >= 1)
		oldvd = oldvd->vdev_child[leaf / zopt_raidz];
	if (zopt_raidz > 1)
		oldvd = oldvd->vdev_child[leaf % zopt_raidz];

	/*
	 * If we're already doing an attach or replace, oldvd may be a
	 * mirror vdev -- in which case, pick a random child.
	 */
	while (oldvd->vdev_children != 0) {
		oldvd_has_siblings = B_TRUE;
		ASSERT(oldvd->vdev_children == 2);
		oldvd = oldvd->vdev_child[ztest_random(2)];
	}

	oldguid = oldvd->vdev_guid;
	oldsize = vdev_get_rsize(oldvd);
	oldvd_is_log = oldvd->vdev_top->vdev_islog;
	(void) strcpy(oldpath, oldvd->vdev_path);
	pvd = oldvd->vdev_parent;

	/*
	 * If oldvd has siblings, then half of the time, detach it.
	 */
	if (oldvd_has_siblings && ztest_random(2) == 0) {
		spa_config_exit(spa, SCL_VDEV, FTAG);
		error = spa_vdev_detach(spa, oldguid, B_FALSE);
		if (error != 0 && error != ENODEV && error != EBUSY)
			fatal(0, "detach (%s) returned %d",
			    oldpath, error);
		(void) mutex_unlock(&ztest_shared->zs_vdev_lock);
		return;
	}

	/*
	 * For the new vdev, choose with equal probability between the two
	 * standard paths (ending in either 'a' or 'b') or a random hot spare.
	 */
	if (sav->sav_count != 0 && ztest_random(3) == 0) {
		newvd = sav->sav_vdevs[ztest_random(sav->sav_count)];
		newvd_is_spare = B_TRUE;
		(void) strcpy(newpath, newvd->vdev_path);
	} else {
		(void) snprintf(newpath, sizeof (newpath), ztest_dev_template,
		    zopt_dir, zopt_pool, top * leaves + leaf);
		if (ztest_random(2) == 0)
			newpath[strlen(newpath) - 1] = 'b';
		newvd = vdev_lookup_by_path(rvd, newpath);
	}

	if (newvd) {
		newsize = vdev_get_rsize(newvd);
	} else {
		/*
		 * Make newsize a little bigger or smaller than oldsize.
		 * If it's smaller, the attach should fail.
		 * If it's larger, and we're doing a replace,
		 * we should get dynamic LUN growth when we're done.
		 */
		newsize = 10 * oldsize / (9 + ztest_random(3));
	}

	/*
	 * If pvd is not a mirror or root, the attach should fail with ENOTSUP,
	 * unless it's a replace; in that case any non-replacing parent is OK.
	 *
	 * If newvd is already part of the pool, it should fail with EBUSY.
	 *
	 * If newvd is too small, it should fail with EOVERFLOW.
	 */
	if (pvd->vdev_ops != &vdev_mirror_ops &&
	    pvd->vdev_ops != &vdev_root_ops && (!replacing ||
	    pvd->vdev_ops == &vdev_replacing_ops ||
	    pvd->vdev_ops == &vdev_spare_ops))
		expected_error = ENOTSUP;
	else if (newvd_is_spare && (!replacing || oldvd_is_log))
		expected_error = ENOTSUP;
	else if (newvd == oldvd)
		expected_error = replacing ? 0 : EBUSY;
	else if (vdev_lookup_by_path(rvd, newpath) != NULL)
		expected_error = EBUSY;
	else if (newsize < oldsize)
		expected_error = EOVERFLOW;
	else if (ashift > oldvd->vdev_top->vdev_ashift)
		expected_error = EDOM;
	else
		expected_error = 0;

	spa_config_exit(spa, SCL_VDEV, FTAG);

	/*
	 * Build the nvlist describing newpath.
	 */
	root = make_vdev_root(newpath, NULL, newvd == NULL ? newsize : 0,
	    ashift, 0, 0, 0, 1);

	error = spa_vdev_attach(spa, oldguid, root, replacing);

	nvlist_free(root);

	/*
	 * If our parent was the replacing vdev, but the replace completed,
	 * then instead of failing with ENOTSUP we may either succeed,
	 * fail with ENODEV, or fail with EOVERFLOW.
	 */
	if (expected_error == ENOTSUP &&
	    (error == 0 || error == ENODEV || error == EOVERFLOW))
		expected_error = error;

	/*
	 * If someone grew the LUN, the replacement may be too small.
	 */
	if (error == EOVERFLOW || error == EBUSY)
		expected_error = error;

	/* XXX workaround 6690467 */
	if (error != expected_error && expected_error != EBUSY) {
		fatal(0, "attach (%s %llu, %s %llu, %d) "
		    "returned %d, expected %d",
		    oldpath, (longlong_t)oldsize, newpath,
		    (longlong_t)newsize, replacing, error, expected_error);
	}

	(void) mutex_unlock(&ztest_shared->zs_vdev_lock);
}

/*
 * Verify that dynamic LUN growth works as expected.
 */
/* ARGSUSED */
void
ztest_vdev_LUN_growth(ztest_args_t *za)
{
	spa_t *spa = za->za_spa;
	char dev_name[MAXPATHLEN];
	uint64_t leaves = MAX(zopt_mirrors, 1) * zopt_raidz;
	uint64_t vdev;
	size_t fsize;
	int fd;

	(void) mutex_lock(&ztest_shared->zs_vdev_lock);

	/*
	 * Pick a random leaf vdev.
	 */
	spa_config_enter(spa, SCL_VDEV, FTAG, RW_READER);
	vdev = ztest_random(spa->spa_root_vdev->vdev_children * leaves);
	spa_config_exit(spa, SCL_VDEV, FTAG);

	(void) sprintf(dev_name, ztest_dev_template, zopt_dir, zopt_pool, vdev);

	if ((fd = open(dev_name, O_RDWR)) != -1) {
		/*
		 * Determine the size.
		 */
		fsize = lseek(fd, 0, SEEK_END);

		/*
		 * If it's less than 2x the original size, grow by around 3%.
		 */
		if (fsize < 2 * zopt_vdev_size) {
			size_t newsize = fsize + ztest_random(fsize / 32);
			(void) ftruncate(fd, newsize);
			if (zopt_verbose >= 6) {
				(void) printf("%s grew from %lu to %lu bytes\n",
				    dev_name, (ulong_t)fsize, (ulong_t)newsize);
			}
		}
		(void) close(fd);
	}

	(void) mutex_unlock(&ztest_shared->zs_vdev_lock);
}

/* ARGSUSED */
static void
ztest_create_cb(objset_t *os, void *arg, cred_t *cr, dmu_tx_t *tx)
{
	/*
	 * Create the directory object.
	 */
	VERIFY(dmu_object_claim(os, ZTEST_DIROBJ,
	    DMU_OT_UINT64_OTHER, ZTEST_DIROBJ_BLOCKSIZE,
	    DMU_OT_UINT64_OTHER, 5 * sizeof (ztest_block_tag_t), tx) == 0);

	VERIFY(zap_create_claim(os, ZTEST_MICROZAP_OBJ,
	    DMU_OT_ZAP_OTHER, DMU_OT_NONE, 0, tx) == 0);

	VERIFY(zap_create_claim(os, ZTEST_FATZAP_OBJ,
	    DMU_OT_ZAP_OTHER, DMU_OT_NONE, 0, tx) == 0);
}

static int
ztest_destroy_cb(char *name, void *arg)
{
	ztest_args_t *za = arg;
	objset_t *os;
	dmu_object_info_t *doi = &za->za_doi;
	int error;

	/*
	 * Verify that the dataset contains a directory object.
	 */
	error = dmu_objset_open(name, DMU_OST_OTHER,
	    DS_MODE_USER | DS_MODE_READONLY, &os);
	ASSERT3U(error, ==, 0);
	error = dmu_object_info(os, ZTEST_DIROBJ, doi);
	if (error != ENOENT) {
		/* We could have crashed in the middle of destroying it */
		ASSERT3U(error, ==, 0);
		ASSERT3U(doi->doi_type, ==, DMU_OT_UINT64_OTHER);
		ASSERT3S(doi->doi_physical_blks, >=, 0);
	}
	dmu_objset_close(os);

	/*
	 * Destroy the dataset.
	 */
	error = dmu_objset_destroy(name);
	if (error) {
		(void) dmu_objset_open(name, DMU_OST_OTHER,
		    DS_MODE_USER | DS_MODE_READONLY, &os);
		fatal(0, "dmu_objset_destroy(os=%p) = %d\n", &os, error);
	}
	return (0);
}

/*
 * Verify that dmu_objset_{create,destroy,open,close} work as expected.
 */
static uint64_t
ztest_log_create(zilog_t *zilog, dmu_tx_t *tx, uint64_t object, int mode)
{
	itx_t *itx;
	lr_create_t *lr;
	size_t namesize;
	char name[24];

	(void) sprintf(name, "ZOBJ_%llu", (u_longlong_t)object);
	namesize = strlen(name) + 1;

	itx = zil_itx_create(TX_CREATE, sizeof (*lr) + namesize +
	    ztest_random(ZIL_MAX_BLKSZ));
	lr = (lr_create_t *)&itx->itx_lr;
	bzero(lr + 1, lr->lr_common.lrc_reclen - sizeof (*lr));
	lr->lr_doid = object;
	lr->lr_foid = 0;
	lr->lr_mode = mode;
	lr->lr_uid = 0;
	lr->lr_gid = 0;
	lr->lr_gen = dmu_tx_get_txg(tx);
	lr->lr_crtime[0] = time(NULL);
	lr->lr_crtime[1] = 0;
	lr->lr_rdev = 0;
	bcopy(name, (char *)(lr + 1), namesize);

	return (zil_itx_assign(zilog, itx, tx));
}

void
ztest_dmu_objset_create_destroy(ztest_args_t *za)
{
	int error;
	objset_t *os, *os2;
	char name[100];
	int basemode, expected_error;
	zilog_t *zilog;
	uint64_t seq;
	uint64_t objects;
	ztest_replay_t zr;

	(void) rw_rdlock(&ztest_shared->zs_name_lock);
	(void) snprintf(name, 100, "%s/%s_temp_%llu", za->za_pool, za->za_pool,
	    (u_longlong_t)za->za_instance);

	basemode = DS_MODE_TYPE(za->za_instance);
	if (basemode != DS_MODE_USER && basemode != DS_MODE_OWNER)
		basemode = DS_MODE_USER;

	/*
	 * If this dataset exists from a previous run, process its replay log
	 * half of the time.  If we don't replay it, then dmu_objset_destroy()
	 * (invoked from ztest_destroy_cb() below) should just throw it away.
	 */
	if (ztest_random(2) == 0 &&
	    dmu_objset_open(name, DMU_OST_OTHER, DS_MODE_OWNER, &os) == 0) {
		zr.zr_os = os;
		zil_replay(os, &zr, &zr.zr_assign, ztest_replay_vector, NULL);
		dmu_objset_close(os);
	}

	/*
	 * There may be an old instance of the dataset we're about to
	 * create lying around from a previous run.  If so, destroy it
	 * and all of its snapshots.
	 */
	(void) dmu_objset_find(name, ztest_destroy_cb, za,
	    DS_FIND_CHILDREN | DS_FIND_SNAPSHOTS);

	/*
	 * Verify that the destroyed dataset is no longer in the namespace.
	 */
	error = dmu_objset_open(name, DMU_OST_OTHER, basemode, &os);
	if (error != ENOENT)
		fatal(1, "dmu_objset_open(%s) found destroyed dataset %p",
		    name, os);

	/*
	 * Verify that we can create a new dataset.
	 */
	error = dmu_objset_create(name, DMU_OST_OTHER, NULL, 0,
	    ztest_create_cb, NULL);
	if (error) {
		if (error == ENOSPC) {
			ztest_record_enospc("dmu_objset_create");
			(void) rw_unlock(&ztest_shared->zs_name_lock);
			return;
		}
		fatal(0, "dmu_objset_create(%s) = %d", name, error);
	}

	error = dmu_objset_open(name, DMU_OST_OTHER, basemode, &os);
	if (error) {
		fatal(0, "dmu_objset_open(%s) = %d", name, error);
	}

	/*
	 * Open the intent log for it.
	 */
	zilog = zil_open(os, NULL);

	/*
	 * Put a random number of objects in there.
	 */
	objects = ztest_random(20);
	seq = 0;
	while (objects-- != 0) {
		uint64_t object;
		dmu_tx_t *tx = dmu_tx_create(os);
		dmu_tx_hold_write(tx, DMU_NEW_OBJECT, 0, sizeof (name));
		error = dmu_tx_assign(tx, TXG_WAIT);
		if (error) {
			dmu_tx_abort(tx);
		} else {
			object = dmu_object_alloc(os, DMU_OT_UINT64_OTHER, 0,
			    DMU_OT_NONE, 0, tx);
			ztest_set_random_blocksize(os, object, tx);
			seq = ztest_log_create(zilog, tx, object,
			    DMU_OT_UINT64_OTHER);
			dmu_write(os, object, 0, sizeof (name), name, tx);
			dmu_tx_commit(tx);
		}
		if (ztest_random(5) == 0) {
			zil_commit(zilog, seq, object);
		}
		if (ztest_random(100) == 0) {
			error = zil_suspend(zilog);
			if (error == 0) {
				zil_resume(zilog);
			}
		}
	}

	/*
	 * Verify that we cannot create an existing dataset.
	 */
	error = dmu_objset_create(name, DMU_OST_OTHER, NULL, 0, NULL, NULL);
	if (error != EEXIST)
		fatal(0, "created existing dataset, error = %d", error);

	/*
	 * Verify that multiple dataset holds are allowed, but only when
	 * the new access mode is compatible with the base mode.
	 */
	if (basemode == DS_MODE_OWNER) {
		error = dmu_objset_open(name, DMU_OST_OTHER, DS_MODE_USER,
		    &os2);
		if (error)
			fatal(0, "dmu_objset_open('%s') = %d", name, error);
		else
			dmu_objset_close(os2);
	}
	error = dmu_objset_open(name, DMU_OST_OTHER, DS_MODE_OWNER, &os2);
	expected_error = (basemode == DS_MODE_OWNER) ? EBUSY : 0;
	if (error != expected_error)
		fatal(0, "dmu_objset_open('%s') = %d, expected %d",
		    name, error, expected_error);
	if (error == 0)
		dmu_objset_close(os2);

	zil_close(zilog);
	dmu_objset_close(os);

	error = dmu_objset_destroy(name);
	if (error)
		fatal(0, "dmu_objset_destroy(%s) = %d", name, error);

	(void) rw_unlock(&ztest_shared->zs_name_lock);
}

/*
 * Verify that dmu_snapshot_{create,destroy,open,close} work as expected.
 */
void
ztest_dmu_snapshot_create_destroy(ztest_args_t *za)
{
	int error;
	objset_t *os = za->za_os;
	char snapname[100];
	char osname[MAXNAMELEN];

	(void) rw_rdlock(&ztest_shared->zs_name_lock);
	dmu_objset_name(os, osname);
	(void) snprintf(snapname, 100, "%s@%llu", osname,
	    (u_longlong_t)za->za_instance);

	error = dmu_objset_destroy(snapname);
	if (error != 0 && error != ENOENT)
		fatal(0, "dmu_objset_destroy() = %d", error);
	error = dmu_objset_snapshot(osname, strchr(snapname, '@')+1, FALSE);
	if (error == ENOSPC)
		ztest_record_enospc("dmu_take_snapshot");
	else if (error != 0 && error != EEXIST)
		fatal(0, "dmu_take_snapshot() = %d", error);
	(void) rw_unlock(&ztest_shared->zs_name_lock);
}

/*
 * Verify that dmu_object_{alloc,free} work as expected.
 */
void
ztest_dmu_object_alloc_free(ztest_args_t *za)
{
	objset_t *os = za->za_os;
	dmu_buf_t *db;
	dmu_tx_t *tx;
	uint64_t batchobj, object, batchsize, endoff, temp;
	int b, c, error, bonuslen;
	dmu_object_info_t *doi = &za->za_doi;
	char osname[MAXNAMELEN];

	dmu_objset_name(os, osname);

	endoff = -8ULL;
	batchsize = 2;

	/*
	 * Create a batch object if necessary, and record it in the directory.
	 */
	VERIFY3U(0, ==, dmu_read(os, ZTEST_DIROBJ, za->za_diroff,
	    sizeof (uint64_t), &batchobj));
	if (batchobj == 0) {
		tx = dmu_tx_create(os);
		dmu_tx_hold_write(tx, ZTEST_DIROBJ, za->za_diroff,
		    sizeof (uint64_t));
		dmu_tx_hold_bonus(tx, DMU_NEW_OBJECT);
		error = dmu_tx_assign(tx, TXG_WAIT);
		if (error) {
			ztest_record_enospc("create a batch object");
			dmu_tx_abort(tx);
			return;
		}
		batchobj = dmu_object_alloc(os, DMU_OT_UINT64_OTHER, 0,
		    DMU_OT_NONE, 0, tx);
		ztest_set_random_blocksize(os, batchobj, tx);
		dmu_write(os, ZTEST_DIROBJ, za->za_diroff,
		    sizeof (uint64_t), &batchobj, tx);
		dmu_tx_commit(tx);
	}

	/*
	 * Destroy the previous batch of objects.
	 */
	for (b = 0; b < batchsize; b++) {
		VERIFY3U(0, ==, dmu_read(os, batchobj, b * sizeof (uint64_t),
		    sizeof (uint64_t), &object));
		if (object == 0)
			continue;
		/*
		 * Read and validate contents.
		 * We expect the nth byte of the bonus buffer to be n.
		 */
		VERIFY(0 == dmu_bonus_hold(os, object, FTAG, &db));
		za->za_dbuf = db;

		dmu_object_info_from_db(db, doi);
		ASSERT(doi->doi_type == DMU_OT_UINT64_OTHER);
		ASSERT(doi->doi_bonus_type == DMU_OT_PLAIN_OTHER);
		ASSERT3S(doi->doi_physical_blks, >=, 0);

		bonuslen = doi->doi_bonus_size;

		for (c = 0; c < bonuslen; c++) {
			if (((uint8_t *)db->db_data)[c] !=
			    (uint8_t)(c + bonuslen)) {
				fatal(0,
				    "bad bonus: %s, obj %llu, off %d: %u != %u",
				    osname, object, c,
				    ((uint8_t *)db->db_data)[c],
				    (uint8_t)(c + bonuslen));
			}
		}

		dmu_buf_rele(db, FTAG);
		za->za_dbuf = NULL;

		/*
		 * We expect the word at endoff to be our object number.
		 */
		VERIFY(0 == dmu_read(os, object, endoff,
		    sizeof (uint64_t), &temp));

		if (temp != object) {
			fatal(0, "bad data in %s, got %llu, expected %llu",
			    osname, temp, object);
		}

		/*
		 * Destroy old object and clear batch entry.
		 */
		tx = dmu_tx_create(os);
		dmu_tx_hold_write(tx, batchobj,
		    b * sizeof (uint64_t), sizeof (uint64_t));
		dmu_tx_hold_free(tx, object, 0, DMU_OBJECT_END);
		error = dmu_tx_assign(tx, TXG_WAIT);
		if (error) {
			ztest_record_enospc("free object");
			dmu_tx_abort(tx);
			return;
		}
		error = dmu_object_free(os, object, tx);
		if (error) {
			fatal(0, "dmu_object_free('%s', %llu) = %d",
			    osname, object, error);
		}
		object = 0;

		dmu_object_set_checksum(os, batchobj,
		    ztest_random_checksum(), tx);
		dmu_object_set_compress(os, batchobj,
		    ztest_random_compress(), tx);

		dmu_write(os, batchobj, b * sizeof (uint64_t),
		    sizeof (uint64_t), &object, tx);

		dmu_tx_commit(tx);
	}

	/*
	 * Before creating the new batch of objects, generate a bunch of churn.
	 */
	for (b = ztest_random(100); b > 0; b--) {
		tx = dmu_tx_create(os);
		dmu_tx_hold_bonus(tx, DMU_NEW_OBJECT);
		error = dmu_tx_assign(tx, TXG_WAIT);
		if (error) {
			ztest_record_enospc("churn objects");
			dmu_tx_abort(tx);
			return;
		}
		object = dmu_object_alloc(os, DMU_OT_UINT64_OTHER, 0,
		    DMU_OT_NONE, 0, tx);
		ztest_set_random_blocksize(os, object, tx);
		error = dmu_object_free(os, object, tx);
		if (error) {
			fatal(0, "dmu_object_free('%s', %llu) = %d",
			    osname, object, error);
		}
		dmu_tx_commit(tx);
	}

	/*
	 * Create a new batch of objects with randomly chosen
	 * blocksizes and record them in the batch directory.
	 */
	for (b = 0; b < batchsize; b++) {
		uint32_t va_blksize;
		u_longlong_t va_nblocks;

		tx = dmu_tx_create(os);
		dmu_tx_hold_write(tx, batchobj, b * sizeof (uint64_t),
		    sizeof (uint64_t));
		dmu_tx_hold_bonus(tx, DMU_NEW_OBJECT);
		dmu_tx_hold_write(tx, DMU_NEW_OBJECT, endoff,
		    sizeof (uint64_t));
		error = dmu_tx_assign(tx, TXG_WAIT);
		if (error) {
			ztest_record_enospc("create batchobj");
			dmu_tx_abort(tx);
			return;
		}
		bonuslen = (int)ztest_random(dmu_bonus_max()) + 1;

		object = dmu_object_alloc(os, DMU_OT_UINT64_OTHER, 0,
		    DMU_OT_PLAIN_OTHER, bonuslen, tx);

		ztest_set_random_blocksize(os, object, tx);

		dmu_object_set_checksum(os, object,
		    ztest_random_checksum(), tx);
		dmu_object_set_compress(os, object,
		    ztest_random_compress(), tx);

		dmu_write(os, batchobj, b * sizeof (uint64_t),
		    sizeof (uint64_t), &object, tx);

		/*
		 * Write to both the bonus buffer and the regular data.
		 */
		VERIFY(dmu_bonus_hold(os, object, FTAG, &db) == 0);
		za->za_dbuf = db;
		ASSERT3U(bonuslen, <=, db->db_size);

		dmu_object_size_from_db(db, &va_blksize, &va_nblocks);
		ASSERT3S(va_nblocks, >=, 0);

		dmu_buf_will_dirty(db, tx);

		/*
		 * See comments above regarding the contents of
		 * the bonus buffer and the word at endoff.
		 */
		for (c = 0; c < bonuslen; c++)
			((uint8_t *)db->db_data)[c] = (uint8_t)(c + bonuslen);

		dmu_buf_rele(db, FTAG);
		za->za_dbuf = NULL;

		/*
		 * Write to a large offset to increase indirection.
		 */
		dmu_write(os, object, endoff, sizeof (uint64_t), &object, tx);

		dmu_tx_commit(tx);
	}
}

/*
 * Verify that dmu_{read,write} work as expected.
 */
typedef struct bufwad {
	uint64_t	bw_index;
	uint64_t	bw_txg;
	uint64_t	bw_data;
} bufwad_t;

typedef struct dmu_read_write_dir {
	uint64_t	dd_packobj;
	uint64_t	dd_bigobj;
	uint64_t	dd_chunk;
} dmu_read_write_dir_t;

void
ztest_dmu_read_write(ztest_args_t *za)
{
	objset_t *os = za->za_os;
	dmu_read_write_dir_t dd;
	dmu_tx_t *tx;
	int i, freeit, error;
	uint64_t n, s, txg;
	bufwad_t *packbuf, *bigbuf, *pack, *bigH, *bigT;
	uint64_t packoff, packsize, bigoff, bigsize;
	uint64_t regions = 997;
	uint64_t stride = 123456789ULL;
	uint64_t width = 40;
	int free_percent = 5;

	/*
	 * This test uses two objects, packobj and bigobj, that are always
	 * updated together (i.e. in the same tx) so that their contents are
	 * in sync and can be compared.  Their contents relate to each other
	 * in a simple way: packobj is a dense array of 'bufwad' structures,
	 * while bigobj is a sparse array of the same bufwads.  Specifically,
	 * for any index n, there are three bufwads that should be identical:
	 *
	 *	packobj, at offset n * sizeof (bufwad_t)
	 *	bigobj, at the head of the nth chunk
	 *	bigobj, at the tail of the nth chunk
	 *
	 * The chunk size is arbitrary. It doesn't have to be a power of two,
	 * and it doesn't have any relation to the object blocksize.
	 * The only requirement is that it can hold at least two bufwads.
	 *
	 * Normally, we write the bufwad to each of these locations.
	 * However, free_percent of the time we instead write zeroes to
	 * packobj and perform a dmu_free_range() on bigobj.  By comparing
	 * bigobj to packobj, we can verify that the DMU is correctly
	 * tracking which parts of an object are allocated and free,
	 * and that the contents of the allocated blocks are correct.
	 */

	/*
	 * Read the directory info.  If it's the first time, set things up.
	 */
	VERIFY(0 == dmu_read(os, ZTEST_DIROBJ, za->za_diroff,
	    sizeof (dd), &dd));
	if (dd.dd_chunk == 0) {
		ASSERT(dd.dd_packobj == 0);
		ASSERT(dd.dd_bigobj == 0);
		tx = dmu_tx_create(os);
		dmu_tx_hold_write(tx, ZTEST_DIROBJ, za->za_diroff, sizeof (dd));
		dmu_tx_hold_bonus(tx, DMU_NEW_OBJECT);
		error = dmu_tx_assign(tx, TXG_WAIT);
		if (error) {
			ztest_record_enospc("create r/w directory");
			dmu_tx_abort(tx);
			return;
		}

		dd.dd_packobj = dmu_object_alloc(os, DMU_OT_UINT64_OTHER, 0,
		    DMU_OT_NONE, 0, tx);
		dd.dd_bigobj = dmu_object_alloc(os, DMU_OT_UINT64_OTHER, 0,
		    DMU_OT_NONE, 0, tx);
		dd.dd_chunk = (1000 + ztest_random(1000)) * sizeof (uint64_t);

		ztest_set_random_blocksize(os, dd.dd_packobj, tx);
		ztest_set_random_blocksize(os, dd.dd_bigobj, tx);

		dmu_write(os, ZTEST_DIROBJ, za->za_diroff, sizeof (dd), &dd,
		    tx);
		dmu_tx_commit(tx);
	}

	/*
	 * Prefetch a random chunk of the big object.
	 * Our aim here is to get some async reads in flight
	 * for blocks that we may free below; the DMU should
	 * handle this race correctly.
	 */
	n = ztest_random(regions) * stride + ztest_random(width);
	s = 1 + ztest_random(2 * width - 1);
	dmu_prefetch(os, dd.dd_bigobj, n * dd.dd_chunk, s * dd.dd_chunk);

	/*
	 * Pick a random index and compute the offsets into packobj and bigobj.
	 */
	n = ztest_random(regions) * stride + ztest_random(width);
	s = 1 + ztest_random(width - 1);

	packoff = n * sizeof (bufwad_t);
	packsize = s * sizeof (bufwad_t);

	bigoff = n * dd.dd_chunk;
	bigsize = s * dd.dd_chunk;

	packbuf = umem_alloc(packsize, UMEM_NOFAIL);
	bigbuf = umem_alloc(bigsize, UMEM_NOFAIL);

	/*
	 * free_percent of the time, free a range of bigobj rather than
	 * overwriting it.
	 */
	freeit = (ztest_random(100) < free_percent);

	/*
	 * Read the current contents of our objects.
	 */
	error = dmu_read(os, dd.dd_packobj, packoff, packsize, packbuf);
	ASSERT3U(error, ==, 0);
	error = dmu_read(os, dd.dd_bigobj, bigoff, bigsize, bigbuf);
	ASSERT3U(error, ==, 0);

	/*
	 * Get a tx for the mods to both packobj and bigobj.
	 */
	tx = dmu_tx_create(os);

	dmu_tx_hold_write(tx, dd.dd_packobj, packoff, packsize);

	if (freeit)
		dmu_tx_hold_free(tx, dd.dd_bigobj, bigoff, bigsize);
	else
		dmu_tx_hold_write(tx, dd.dd_bigobj, bigoff, bigsize);

	error = dmu_tx_assign(tx, TXG_WAIT);

	if (error) {
		ztest_record_enospc("dmu r/w range");
		dmu_tx_abort(tx);
		umem_free(packbuf, packsize);
		umem_free(bigbuf, bigsize);
		return;
	}

	txg = dmu_tx_get_txg(tx);

	/*
	 * For each index from n to n + s, verify that the existing bufwad
	 * in packobj matches the bufwads at the head and tail of the
	 * corresponding chunk in bigobj.  Then update all three bufwads
	 * with the new values we want to write out.
	 */
	for (i = 0; i < s; i++) {
		/* LINTED */
		pack = (bufwad_t *)((char *)packbuf + i * sizeof (bufwad_t));
		/* LINTED */
		bigH = (bufwad_t *)((char *)bigbuf + i * dd.dd_chunk);
		/* LINTED */
		bigT = (bufwad_t *)((char *)bigH + dd.dd_chunk) - 1;

		ASSERT((uintptr_t)bigH - (uintptr_t)bigbuf < bigsize);
		ASSERT((uintptr_t)bigT - (uintptr_t)bigbuf < bigsize);

		if (pack->bw_txg > txg)
			fatal(0, "future leak: got %llx, open txg is %llx",
			    pack->bw_txg, txg);

		if (pack->bw_data != 0 && pack->bw_index != n + i)
			fatal(0, "wrong index: got %llx, wanted %llx+%llx",
			    pack->bw_index, n, i);

		if (bcmp(pack, bigH, sizeof (bufwad_t)) != 0)
			fatal(0, "pack/bigH mismatch in %p/%p", pack, bigH);

		if (bcmp(pack, bigT, sizeof (bufwad_t)) != 0)
			fatal(0, "pack/bigT mismatch in %p/%p", pack, bigT);

		if (freeit) {
			bzero(pack, sizeof (bufwad_t));
		} else {
			pack->bw_index = n + i;
			pack->bw_txg = txg;
			pack->bw_data = 1 + ztest_random(-2ULL);
		}
		*bigH = *pack;
		*bigT = *pack;
	}

	/*
	 * We've verified all the old bufwads, and made new ones.
	 * Now write them out.
	 */
	dmu_write(os, dd.dd_packobj, packoff, packsize, packbuf, tx);

	if (freeit) {
		if (zopt_verbose >= 6) {
			(void) printf("freeing offset %llx size %llx"
			    " txg %llx\n",
			    (u_longlong_t)bigoff,
			    (u_longlong_t)bigsize,
			    (u_longlong_t)txg);
		}
		VERIFY(0 == dmu_free_range(os, dd.dd_bigobj, bigoff,
		    bigsize, tx));
	} else {
		if (zopt_verbose >= 6) {
			(void) printf("writing offset %llx size %llx"
			    " txg %llx\n",
			    (u_longlong_t)bigoff,
			    (u_longlong_t)bigsize,
			    (u_longlong_t)txg);
		}
		dmu_write(os, dd.dd_bigobj, bigoff, bigsize, bigbuf, tx);
	}

	dmu_tx_commit(tx);

	/*
	 * Sanity check the stuff we just wrote.
	 */
	{
		void *packcheck = umem_alloc(packsize, UMEM_NOFAIL);
		void *bigcheck = umem_alloc(bigsize, UMEM_NOFAIL);

		VERIFY(0 == dmu_read(os, dd.dd_packobj, packoff,
		    packsize, packcheck));
		VERIFY(0 == dmu_read(os, dd.dd_bigobj, bigoff,
		    bigsize, bigcheck));

		ASSERT(bcmp(packbuf, packcheck, packsize) == 0);
		ASSERT(bcmp(bigbuf, bigcheck, bigsize) == 0);

		umem_free(packcheck, packsize);
		umem_free(bigcheck, bigsize);
	}

	umem_free(packbuf, packsize);
	umem_free(bigbuf, bigsize);
}

void
ztest_dmu_check_future_leak(ztest_args_t *za)
{
	objset_t *os = za->za_os;
	dmu_buf_t *db;
	ztest_block_tag_t *bt;
	dmu_object_info_t *doi = &za->za_doi;

	/*
	 * Make sure that, if there is a write record in the bonus buffer
	 * of the ZTEST_DIROBJ, that the txg for this record is <= the
	 * last synced txg of the pool.
	 */
	VERIFY(dmu_bonus_hold(os, ZTEST_DIROBJ, FTAG, &db) == 0);
	za->za_dbuf = db;
	VERIFY(dmu_object_info(os, ZTEST_DIROBJ, doi) == 0);
	ASSERT3U(doi->doi_bonus_size, >=, sizeof (*bt));
	ASSERT3U(doi->doi_bonus_size, <=, db->db_size);
	ASSERT3U(doi->doi_bonus_size % sizeof (*bt), ==, 0);
	bt = (void *)((char *)db->db_data + doi->doi_bonus_size - sizeof (*bt));
	if (bt->bt_objset != 0) {
		ASSERT3U(bt->bt_objset, ==, dmu_objset_id(os));
		ASSERT3U(bt->bt_object, ==, ZTEST_DIROBJ);
		ASSERT3U(bt->bt_offset, ==, -1ULL);
		ASSERT3U(bt->bt_txg, <, spa_first_txg(za->za_spa));
	}
	dmu_buf_rele(db, FTAG);
	za->za_dbuf = NULL;
}

void
ztest_dmu_write_parallel(ztest_args_t *za)
{
	objset_t *os = za->za_os;
	ztest_block_tag_t *rbt = &za->za_rbt;
	ztest_block_tag_t *wbt = &za->za_wbt;
	const size_t btsize = sizeof (ztest_block_tag_t);
	dmu_buf_t *db;
	int b, error;
	int bs = ZTEST_DIROBJ_BLOCKSIZE;
	int do_free = 0;
	uint64_t off, txg, txg_how;
	mutex_t *lp;
	char osname[MAXNAMELEN];
	char iobuf[SPA_MAXBLOCKSIZE];
	blkptr_t blk = { 0 };
	uint64_t blkoff;
	zbookmark_t zb;
	dmu_tx_t *tx = dmu_tx_create(os);

	dmu_objset_name(os, osname);

	/*
	 * Have multiple threads write to large offsets in ZTEST_DIROBJ
	 * to verify that having multiple threads writing to the same object
	 * in parallel doesn't cause any trouble.
	 */
	if (ztest_random(4) == 0) {
		/*
		 * Do the bonus buffer instead of a regular block.
		 * We need a lock to serialize resize vs. others,
		 * so we hash on the objset ID.
		 */
		b = dmu_objset_id(os) % ZTEST_SYNC_LOCKS;
		off = -1ULL;
		dmu_tx_hold_bonus(tx, ZTEST_DIROBJ);
	} else {
		b = ztest_random(ZTEST_SYNC_LOCKS);
		off = za->za_diroff_shared + (b << SPA_MAXBLOCKSHIFT);
		if (ztest_random(4) == 0) {
			do_free = 1;
			dmu_tx_hold_free(tx, ZTEST_DIROBJ, off, bs);
		} else {
			dmu_tx_hold_write(tx, ZTEST_DIROBJ, off, bs);
		}
	}

	txg_how = ztest_random(2) == 0 ? TXG_WAIT : TXG_NOWAIT;
	error = dmu_tx_assign(tx, txg_how);
	if (error) {
		if (error == ERESTART) {
			ASSERT(txg_how == TXG_NOWAIT);
			dmu_tx_wait(tx);
		} else {
			ztest_record_enospc("dmu write parallel");
		}
		dmu_tx_abort(tx);
		return;
	}
	txg = dmu_tx_get_txg(tx);

	lp = &ztest_shared->zs_sync_lock[b];
	(void) mutex_lock(lp);

	wbt->bt_objset = dmu_objset_id(os);
	wbt->bt_object = ZTEST_DIROBJ;
	wbt->bt_offset = off;
	wbt->bt_txg = txg;
	wbt->bt_thread = za->za_instance;
	wbt->bt_seq = ztest_shared->zs_seq[b]++;	/* protected by lp */

	/*
	 * Occasionally, write an all-zero block to test the behavior
	 * of blocks that compress into holes.
	 */
	if (off != -1ULL && ztest_random(8) == 0)
		bzero(wbt, btsize);

	if (off == -1ULL) {
		dmu_object_info_t *doi = &za->za_doi;
		char *dboff;

		VERIFY(dmu_bonus_hold(os, ZTEST_DIROBJ, FTAG, &db) == 0);
		za->za_dbuf = db;
		dmu_object_info_from_db(db, doi);
		ASSERT3U(doi->doi_bonus_size, <=, db->db_size);
		ASSERT3U(doi->doi_bonus_size, >=, btsize);
		ASSERT3U(doi->doi_bonus_size % btsize, ==, 0);
		dboff = (char *)db->db_data + doi->doi_bonus_size - btsize;
		bcopy(dboff, rbt, btsize);
		if (rbt->bt_objset != 0) {
			ASSERT3U(rbt->bt_objset, ==, wbt->bt_objset);
			ASSERT3U(rbt->bt_object, ==, wbt->bt_object);
			ASSERT3U(rbt->bt_offset, ==, wbt->bt_offset);
			ASSERT3U(rbt->bt_txg, <=, wbt->bt_txg);
		}
		if (ztest_random(10) == 0) {
			int newsize = (ztest_random(db->db_size /
			    btsize) + 1) * btsize;

			ASSERT3U(newsize, >=, btsize);
			ASSERT3U(newsize, <=, db->db_size);
			VERIFY3U(dmu_set_bonus(db, newsize, tx), ==, 0);
			dboff = (char *)db->db_data + newsize - btsize;
		}
		dmu_buf_will_dirty(db, tx);
		bcopy(wbt, dboff, btsize);
		dmu_buf_rele(db, FTAG);
		za->za_dbuf = NULL;
	} else if (do_free) {
		VERIFY(dmu_free_range(os, ZTEST_DIROBJ, off, bs, tx) == 0);
	} else {
		dmu_write(os, ZTEST_DIROBJ, off, btsize, wbt, tx);
	}

	(void) mutex_unlock(lp);

	if (ztest_random(1000) == 0)
		(void) poll(NULL, 0, 1); /* open dn_notxholds window */

	dmu_tx_commit(tx);

	if (ztest_random(10000) == 0)
		txg_wait_synced(dmu_objset_pool(os), txg);

	if (off == -1ULL || do_free)
		return;

	if (ztest_random(2) != 0)
		return;

	/*
	 * dmu_sync() the block we just wrote.
	 */
	(void) mutex_lock(lp);

	blkoff = P2ALIGN_TYPED(off, bs, uint64_t);
	error = dmu_buf_hold(os, ZTEST_DIROBJ, blkoff, FTAG, &db);
	za->za_dbuf = db;
	if (error) {
		dprintf("dmu_buf_hold(%s, %d, %llx) = %d\n",
		    osname, ZTEST_DIROBJ, blkoff, error);
		(void) mutex_unlock(lp);
		return;
	}
	blkoff = off - blkoff;
	error = dmu_sync(NULL, db, &blk, txg, NULL, NULL);
	dmu_buf_rele(db, FTAG);
	za->za_dbuf = NULL;

	(void) mutex_unlock(lp);

	if (error) {
		dprintf("dmu_sync(%s, %d, %llx) = %d\n",
		    osname, ZTEST_DIROBJ, off, error);
		return;
	}

	if (blk.blk_birth == 0)		/* concurrent free */
		return;

	txg_suspend(dmu_objset_pool(os));

	ASSERT(blk.blk_fill == 1);
	ASSERT3U(BP_GET_TYPE(&blk), ==, DMU_OT_UINT64_OTHER);
	ASSERT3U(BP_GET_LEVEL(&blk), ==, 0);
	ASSERT3U(BP_GET_LSIZE(&blk), ==, bs);

	/*
	 * Read the block that dmu_sync() returned to make sure its contents
	 * match what we wrote.  We do this while still txg_suspend()ed
	 * to ensure that the block can't be reused before we read it.
	 */
	zb.zb_objset = dmu_objset_id(os);
	zb.zb_object = ZTEST_DIROBJ;
	zb.zb_level = 0;
	zb.zb_blkid = off / bs;
	error = zio_wait(zio_read(NULL, za->za_spa, &blk, iobuf, bs,
	    NULL, NULL, ZIO_PRIORITY_SYNC_READ, ZIO_FLAG_MUSTSUCCEED, &zb));
	ASSERT3U(error, ==, 0);

	txg_resume(dmu_objset_pool(os));

	bcopy(&iobuf[blkoff], rbt, btsize);

	if (rbt->bt_objset == 0)		/* concurrent free */
		return;

	if (wbt->bt_objset == 0)		/* all-zero overwrite */
		return;

	ASSERT3U(rbt->bt_objset, ==, wbt->bt_objset);
	ASSERT3U(rbt->bt_object, ==, wbt->bt_object);
	ASSERT3U(rbt->bt_offset, ==, wbt->bt_offset);

	/*
	 * The semantic of dmu_sync() is that we always push the most recent
	 * version of the data, so in the face of concurrent updates we may
	 * see a newer version of the block.  That's OK.
	 */
	ASSERT3U(rbt->bt_txg, >=, wbt->bt_txg);
	if (rbt->bt_thread == wbt->bt_thread)
		ASSERT3U(rbt->bt_seq, ==, wbt->bt_seq);
	else
		ASSERT3U(rbt->bt_seq, >, wbt->bt_seq);
}

/*
 * Verify that zap_{create,destroy,add,remove,update} work as expected.
 */
#define	ZTEST_ZAP_MIN_INTS	1
#define	ZTEST_ZAP_MAX_INTS	4
#define	ZTEST_ZAP_MAX_PROPS	1000

void
ztest_zap(ztest_args_t *za)
{
	objset_t *os = za->za_os;
	uint64_t object;
	uint64_t txg, last_txg;
	uint64_t value[ZTEST_ZAP_MAX_INTS];
	uint64_t zl_ints, zl_intsize, prop;
	int i, ints;
	dmu_tx_t *tx;
	char propname[100], txgname[100];
	int error;
	char osname[MAXNAMELEN];
	char *hc[2] = { "s.acl.h", ".s.open.h.hyLZlg" };

	dmu_objset_name(os, osname);

	/*
	 * Create a new object if necessary, and record it in the directory.
	 */
	VERIFY(0 == dmu_read(os, ZTEST_DIROBJ, za->za_diroff,
	    sizeof (uint64_t), &object));

	if (object == 0) {
		tx = dmu_tx_create(os);
		dmu_tx_hold_write(tx, ZTEST_DIROBJ, za->za_diroff,
		    sizeof (uint64_t));
		dmu_tx_hold_zap(tx, DMU_NEW_OBJECT, TRUE, NULL);
		error = dmu_tx_assign(tx, TXG_WAIT);
		if (error) {
			ztest_record_enospc("create zap test obj");
			dmu_tx_abort(tx);
			return;
		}
		object = zap_create(os, DMU_OT_ZAP_OTHER, DMU_OT_NONE, 0, tx);
		if (error) {
			fatal(0, "zap_create('%s', %llu) = %d",
			    osname, object, error);
		}
		ASSERT(object != 0);
		dmu_write(os, ZTEST_DIROBJ, za->za_diroff,
		    sizeof (uint64_t), &object, tx);
		/*
		 * Generate a known hash collision, and verify that
		 * we can lookup and remove both entries.
		 */
		for (i = 0; i < 2; i++) {
			value[i] = i;
			error = zap_add(os, object, hc[i], sizeof (uint64_t),
			    1, &value[i], tx);
			ASSERT3U(error, ==, 0);
		}
		for (i = 0; i < 2; i++) {
			error = zap_add(os, object, hc[i], sizeof (uint64_t),
			    1, &value[i], tx);
			ASSERT3U(error, ==, EEXIST);
			error = zap_length(os, object, hc[i],
			    &zl_intsize, &zl_ints);
			ASSERT3U(error, ==, 0);
			ASSERT3U(zl_intsize, ==, sizeof (uint64_t));
			ASSERT3U(zl_ints, ==, 1);
		}
		for (i = 0; i < 2; i++) {
			error = zap_remove(os, object, hc[i], tx);
			ASSERT3U(error, ==, 0);
		}

		dmu_tx_commit(tx);
	}

	ints = MAX(ZTEST_ZAP_MIN_INTS, object % ZTEST_ZAP_MAX_INTS);

	prop = ztest_random(ZTEST_ZAP_MAX_PROPS);
	(void) sprintf(propname, "prop_%llu", (u_longlong_t)prop);
	(void) sprintf(txgname, "txg_%llu", (u_longlong_t)prop);
	bzero(value, sizeof (value));
	last_txg = 0;

	/*
	 * If these zap entries already exist, validate their contents.
	 */
	error = zap_length(os, object, txgname, &zl_intsize, &zl_ints);
	if (error == 0) {
		ASSERT3U(zl_intsize, ==, sizeof (uint64_t));
		ASSERT3U(zl_ints, ==, 1);

		VERIFY(zap_lookup(os, object, txgname, zl_intsize,
		    zl_ints, &last_txg) == 0);

		VERIFY(zap_length(os, object, propname, &zl_intsize,
		    &zl_ints) == 0);

		ASSERT3U(zl_intsize, ==, sizeof (uint64_t));
		ASSERT3U(zl_ints, ==, ints);

		VERIFY(zap_lookup(os, object, propname, zl_intsize,
		    zl_ints, value) == 0);

		for (i = 0; i < ints; i++) {
			ASSERT3U(value[i], ==, last_txg + object + i);
		}
	} else {
		ASSERT3U(error, ==, ENOENT);
	}

	/*
	 * Atomically update two entries in our zap object.
	 * The first is named txg_%llu, and contains the txg
	 * in which the property was last updated.  The second
	 * is named prop_%llu, and the nth element of its value
	 * should be txg + object + n.
	 */
	tx = dmu_tx_create(os);
	dmu_tx_hold_zap(tx, object, TRUE, NULL);
	error = dmu_tx_assign(tx, TXG_WAIT);
	if (error) {
		ztest_record_enospc("create zap entry");
		dmu_tx_abort(tx);
		return;
	}
	txg = dmu_tx_get_txg(tx);

	if (last_txg > txg)
		fatal(0, "zap future leak: old %llu new %llu", last_txg, txg);

	for (i = 0; i < ints; i++)
		value[i] = txg + object + i;

	error = zap_update(os, object, txgname, sizeof (uint64_t), 1, &txg, tx);
	if (error)
		fatal(0, "zap_update('%s', %llu, '%s') = %d",
		    osname, object, txgname, error);

	error = zap_update(os, object, propname, sizeof (uint64_t),
	    ints, value, tx);
	if (error)
		fatal(0, "zap_update('%s', %llu, '%s') = %d",
		    osname, object, propname, error);

	dmu_tx_commit(tx);

	/*
	 * Remove a random pair of entries.
	 */
	prop = ztest_random(ZTEST_ZAP_MAX_PROPS);
	(void) sprintf(propname, "prop_%llu", (u_longlong_t)prop);
	(void) sprintf(txgname, "txg_%llu", (u_longlong_t)prop);

	error = zap_length(os, object, txgname, &zl_intsize, &zl_ints);

	if (error == ENOENT)
		return;

	ASSERT3U(error, ==, 0);

	tx = dmu_tx_create(os);
	dmu_tx_hold_zap(tx, object, TRUE, NULL);
	error = dmu_tx_assign(tx, TXG_WAIT);
	if (error) {
		ztest_record_enospc("remove zap entry");
		dmu_tx_abort(tx);
		return;
	}
	error = zap_remove(os, object, txgname, tx);
	if (error)
		fatal(0, "zap_remove('%s', %llu, '%s') = %d",
		    osname, object, txgname, error);

	error = zap_remove(os, object, propname, tx);
	if (error)
		fatal(0, "zap_remove('%s', %llu, '%s') = %d",
		    osname, object, propname, error);

	dmu_tx_commit(tx);

	/*
	 * Once in a while, destroy the object.
	 */
	if (ztest_random(1000) != 0)
		return;

	tx = dmu_tx_create(os);
	dmu_tx_hold_write(tx, ZTEST_DIROBJ, za->za_diroff, sizeof (uint64_t));
	dmu_tx_hold_free(tx, object, 0, DMU_OBJECT_END);
	error = dmu_tx_assign(tx, TXG_WAIT);
	if (error) {
		ztest_record_enospc("destroy zap object");
		dmu_tx_abort(tx);
		return;
	}
	error = zap_destroy(os, object, tx);
	if (error)
		fatal(0, "zap_destroy('%s', %llu) = %d",
		    osname, object, error);
	object = 0;
	dmu_write(os, ZTEST_DIROBJ, za->za_diroff, sizeof (uint64_t),
	    &object, tx);
	dmu_tx_commit(tx);
}

void
ztest_zap_parallel(ztest_args_t *za)
{
	objset_t *os = za->za_os;
	uint64_t txg, object, count, wsize, wc, zl_wsize, zl_wc;
	dmu_tx_t *tx;
	int i, namelen, error;
	char name[20], string_value[20];
	void *data;

	/*
	 * Generate a random name of the form 'xxx.....' where each
	 * x is a random printable character and the dots are dots.
	 * There are 94 such characters, and the name length goes from
	 * 6 to 20, so there are 94^3 * 15 = 12,458,760 possible names.
	 */
	namelen = ztest_random(sizeof (name) - 5) + 5 + 1;

	for (i = 0; i < 3; i++)
		name[i] = '!' + ztest_random('~' - '!' + 1);
	for (; i < namelen - 1; i++)
		name[i] = '.';
	name[i] = '\0';

	if (ztest_random(2) == 0)
		object = ZTEST_MICROZAP_OBJ;
	else
		object = ZTEST_FATZAP_OBJ;

	if ((namelen & 1) || object == ZTEST_MICROZAP_OBJ) {
		wsize = sizeof (txg);
		wc = 1;
		data = &txg;
	} else {
		wsize = 1;
		wc = namelen;
		data = string_value;
	}

	count = -1ULL;
	VERIFY(zap_count(os, object, &count) == 0);
	ASSERT(count != -1ULL);

	/*
	 * Select an operation: length, lookup, add, update, remove.
	 */
	i = ztest_random(5);

	if (i >= 2) {
		tx = dmu_tx_create(os);
		dmu_tx_hold_zap(tx, object, TRUE, NULL);
		error = dmu_tx_assign(tx, TXG_WAIT);
		if (error) {
			ztest_record_enospc("zap parallel");
			dmu_tx_abort(tx);
			return;
		}
		txg = dmu_tx_get_txg(tx);
		bcopy(name, string_value, namelen);
	} else {
		tx = NULL;
		txg = 0;
		bzero(string_value, namelen);
	}

	switch (i) {

	case 0:
		error = zap_length(os, object, name, &zl_wsize, &zl_wc);
		if (error == 0) {
			ASSERT3U(wsize, ==, zl_wsize);
			ASSERT3U(wc, ==, zl_wc);
		} else {
			ASSERT3U(error, ==, ENOENT);
		}
		break;

	case 1:
		error = zap_lookup(os, object, name, wsize, wc, data);
		if (error == 0) {
			if (data == string_value &&
			    bcmp(name, data, namelen) != 0)
				fatal(0, "name '%s' != val '%s' len %d",
				    name, data, namelen);
		} else {
			ASSERT3U(error, ==, ENOENT);
		}
		break;

	case 2:
		error = zap_add(os, object, name, wsize, wc, data, tx);
		ASSERT(error == 0 || error == EEXIST);
		break;

	case 3:
		VERIFY(zap_update(os, object, name, wsize, wc, data, tx) == 0);
		break;

	case 4:
		error = zap_remove(os, object, name, tx);
		ASSERT(error == 0 || error == ENOENT);
		break;
	}

	if (tx != NULL)
		dmu_tx_commit(tx);
}

void
ztest_dsl_prop_get_set(ztest_args_t *za)
{
	objset_t *os = za->za_os;
	int i, inherit;
	uint64_t value;
	const char *prop, *valname;
	char setpoint[MAXPATHLEN];
	char osname[MAXNAMELEN];
	int error;

	(void) rw_rdlock(&ztest_shared->zs_name_lock);

	dmu_objset_name(os, osname);

	for (i = 0; i < 2; i++) {
		if (i == 0) {
			prop = "checksum";
			value = ztest_random_checksum();
			inherit = (value == ZIO_CHECKSUM_INHERIT);
		} else {
			prop = "compression";
			value = ztest_random_compress();
			inherit = (value == ZIO_COMPRESS_INHERIT);
		}

		error = dsl_prop_set(osname, prop, sizeof (value),
		    !inherit, &value);

		if (error == ENOSPC) {
			ztest_record_enospc("dsl_prop_set");
			break;
		}

		ASSERT3U(error, ==, 0);

		VERIFY3U(dsl_prop_get(osname, prop, sizeof (value),
		    1, &value, setpoint), ==, 0);

		if (i == 0)
			valname = zio_checksum_table[value].ci_name;
		else
			valname = zio_compress_table[value].ci_name;

		if (zopt_verbose >= 6) {
			(void) printf("%s %s = %s for '%s'\n",
			    osname, prop, valname, setpoint);
		}
	}

	(void) rw_unlock(&ztest_shared->zs_name_lock);
}

/*
 * Inject random faults into the on-disk data.
 */
void
ztest_fault_inject(ztest_args_t *za)
{
	int fd;
	uint64_t offset;
	uint64_t leaves = MAX(zopt_mirrors, 1) * zopt_raidz;
	uint64_t bad = 0x1990c0ffeedecade;
	uint64_t top, leaf;
	char path0[MAXPATHLEN];
	char pathrand[MAXPATHLEN];
	size_t fsize;
	spa_t *spa = za->za_spa;
	int bshift = SPA_MAXBLOCKSHIFT + 2;	/* don't scrog all labels */
	int iters = 1000;
	int maxfaults = zopt_maxfaults;
	vdev_t *vd0 = NULL;
	uint64_t guid0 = 0;

	ASSERT(leaves >= 1);

	/*
	 * We need SCL_STATE here because we're going to look at vd0->vdev_tsd.
	 */
	spa_config_enter(spa, SCL_STATE, FTAG, RW_READER);

	if (ztest_random(2) == 0) {
		/*
		 * Inject errors on a normal data device.
		 */
		top = ztest_random(spa->spa_root_vdev->vdev_children);
		leaf = ztest_random(leaves);

		/*
		 * Generate paths to the first leaf in this top-level vdev,
		 * and to the random leaf we selected.  We'll induce transient
		 * write failures and random online/offline activity on leaf 0,
		 * and we'll write random garbage to the randomly chosen leaf.
		 */
		(void) snprintf(path0, sizeof (path0), ztest_dev_template,
		    zopt_dir, zopt_pool, top * leaves + 0);
		(void) snprintf(pathrand, sizeof (pathrand), ztest_dev_template,
		    zopt_dir, zopt_pool, top * leaves + leaf);

		vd0 = vdev_lookup_by_path(spa->spa_root_vdev, path0);
		if (vd0 != NULL && maxfaults != 1) {
			/*
			 * Make vd0 explicitly claim to be unreadable,
			 * or unwriteable, or reach behind its back
			 * and close the underlying fd.  We can do this if
			 * maxfaults == 0 because we'll fail and reexecute,
			 * and we can do it if maxfaults >= 2 because we'll
			 * have enough redundancy.  If maxfaults == 1, the
			 * combination of this with injection of random data
			 * corruption below exceeds the pool's fault tolerance.
			 */
			vdev_file_t *vf = vd0->vdev_tsd;

			if (vf != NULL && ztest_random(3) == 0) {
				(void) close(vf->vf_vnode->v_fd);
				vf->vf_vnode->v_fd = -1;
			} else if (ztest_random(2) == 0) {
				vd0->vdev_cant_read = B_TRUE;
			} else {
				vd0->vdev_cant_write = B_TRUE;
			}
			guid0 = vd0->vdev_guid;
		}
	} else {
		/*
		 * Inject errors on an l2cache device.
		 */
		spa_aux_vdev_t *sav = &spa->spa_l2cache;

		if (sav->sav_count == 0) {
			spa_config_exit(spa, SCL_STATE, FTAG);
			return;
		}
		vd0 = sav->sav_vdevs[ztest_random(sav->sav_count)];
		guid0 = vd0->vdev_guid;
		(void) strcpy(path0, vd0->vdev_path);
		(void) strcpy(pathrand, vd0->vdev_path);

		leaf = 0;
		leaves = 1;
		maxfaults = INT_MAX;	/* no limit on cache devices */
	}

	dprintf("damaging %s and %s\n", path0, pathrand);

	spa_config_exit(spa, SCL_STATE, FTAG);

	if (maxfaults == 0)
		return;

	/*
	 * If we can tolerate two or more faults, randomly online/offline vd0.
	 */
	if (maxfaults >= 2 && guid0 != 0) {
		if (ztest_random(10) < 6)
			(void) vdev_offline(spa, guid0, B_TRUE);
		else
			(void) vdev_online(spa, guid0, B_FALSE, NULL);
	}

	/*
	 * We have at least single-fault tolerance, so inject data corruption.
	 */
	fd = open(pathrand, O_RDWR);

	if (fd == -1)	/* we hit a gap in the device namespace */
		return;

	fsize = lseek(fd, 0, SEEK_END);

	while (--iters != 0) {
		offset = ztest_random(fsize / (leaves << bshift)) *
		    (leaves << bshift) + (leaf << bshift) +
		    (ztest_random(1ULL << (bshift - 1)) & -8ULL);

		if (offset >= fsize)
			continue;

		if (zopt_verbose >= 6)
			(void) printf("injecting bad word into %s,"
			    " offset 0x%llx\n", pathrand, (u_longlong_t)offset);

		if (pwrite(fd, &bad, sizeof (bad), offset) != sizeof (bad))
			fatal(1, "can't inject bad word at 0x%llx in %s",
			    offset, pathrand);
	}

	(void) close(fd);
}

/*
 * Scrub the pool.
 */
void
ztest_scrub(ztest_args_t *za)
{
	spa_t *spa = za->za_spa;

	(void) spa_scrub(spa, POOL_SCRUB_EVERYTHING);
	(void) poll(NULL, 0, 1000); /* wait a second, then force a restart */
	(void) spa_scrub(spa, POOL_SCRUB_EVERYTHING);
}

/*
 * Rename the pool to a different name and then rename it back.
 */
void
ztest_spa_rename(ztest_args_t *za)
{
	char *oldname, *newname;
	int error;
	spa_t *spa;

	(void) rw_wrlock(&ztest_shared->zs_name_lock);

	oldname = za->za_pool;
	newname = umem_alloc(strlen(oldname) + 5, UMEM_NOFAIL);
	(void) strcpy(newname, oldname);
	(void) strcat(newname, "_tmp");

	/*
	 * Do the rename
	 */
	error = spa_rename(oldname, newname);
	if (error)
		fatal(0, "spa_rename('%s', '%s') = %d", oldname,
		    newname, error);

	/*
	 * Try to open it under the old name, which shouldn't exist
	 */
	error = spa_open(oldname, &spa, FTAG);
	if (error != ENOENT)
		fatal(0, "spa_open('%s') = %d", oldname, error);

	/*
	 * Open it under the new name and make sure it's still the same spa_t.
	 */
	error = spa_open(newname, &spa, FTAG);
	if (error != 0)
		fatal(0, "spa_open('%s') = %d", newname, error);

	ASSERT(spa == za->za_spa);
	spa_close(spa, FTAG);

	/*
	 * Rename it back to the original
	 */
	error = spa_rename(newname, oldname);
	if (error)
		fatal(0, "spa_rename('%s', '%s') = %d", newname,
		    oldname, error);

	/*
	 * Make sure it can still be opened
	 */
	error = spa_open(oldname, &spa, FTAG);
	if (error != 0)
		fatal(0, "spa_open('%s') = %d", oldname, error);

	ASSERT(spa == za->za_spa);
	spa_close(spa, FTAG);

	umem_free(newname, strlen(newname) + 1);

	(void) rw_unlock(&ztest_shared->zs_name_lock);
}


/*
 * Completely obliterate one disk.
 */
static void
ztest_obliterate_one_disk(uint64_t vdev)
{
	int fd;
	char dev_name[MAXPATHLEN], copy_name[MAXPATHLEN];
	size_t fsize;

	if (zopt_maxfaults < 2)
		return;

	(void) sprintf(dev_name, ztest_dev_template, zopt_dir, zopt_pool, vdev);
	(void) snprintf(copy_name, MAXPATHLEN, "%s.old", dev_name);

	fd = open(dev_name, O_RDWR);

	if (fd == -1)
		fatal(1, "can't open %s", dev_name);

	/*
	 * Determine the size.
	 */
	fsize = lseek(fd, 0, SEEK_END);

	(void) close(fd);

	/*
	 * Rename the old device to dev_name.old (useful for debugging).
	 */
	VERIFY(rename(dev_name, copy_name) == 0);

	/*
	 * Create a new one.
	 */
	VERIFY((fd = open(dev_name, O_RDWR | O_CREAT | O_TRUNC, 0666)) >= 0);
	VERIFY(ftruncate(fd, fsize) == 0);
	(void) close(fd);
}

static void
ztest_replace_one_disk(spa_t *spa, uint64_t vdev)
{
	char dev_name[MAXPATHLEN];
	nvlist_t *root;
	int error;
	uint64_t guid;
	vdev_t *vd;

	(void) sprintf(dev_name, ztest_dev_template, zopt_dir, zopt_pool, vdev);

	/*
	 * Build the nvlist describing dev_name.
	 */
	root = make_vdev_root(dev_name, NULL, 0, 0, 0, 0, 0, 1);

	spa_config_enter(spa, SCL_VDEV, FTAG, RW_READER);
	if ((vd = vdev_lookup_by_path(spa->spa_root_vdev, dev_name)) == NULL)
		guid = 0;
	else
		guid = vd->vdev_guid;
	spa_config_exit(spa, SCL_VDEV, FTAG);
	error = spa_vdev_attach(spa, guid, root, B_TRUE);
	if (error != 0 &&
	    error != EBUSY &&
	    error != ENOTSUP &&
	    error != ENODEV &&
	    error != EDOM)
		fatal(0, "spa_vdev_attach(in-place) = %d", error);

	nvlist_free(root);
}

static void
ztest_verify_blocks(char *pool)
{
	int status;
	char zdb[MAXPATHLEN + MAXNAMELEN + 20];
	char zbuf[1024];
	char *bin;
	char *ztest;
	char *isa;
	int isalen;
	FILE *fp;

	(void) realpath(getexecname(), zdb);

	/* zdb lives in /usr/sbin, while ztest lives in /usr/bin */
	bin = strstr(zdb, "/usr/bin/");
	ztest = strstr(bin, "/ztest");
	isa = bin + 8;
	isalen = ztest - isa;
	isa = strdup(isa);
	/* LINTED */
	(void) sprintf(bin,
	    "/usr/sbin%.*s/zdb -bc%s%s -U /tmp/zpool.cache %s",
	    isalen,
	    isa,
	    zopt_verbose >= 3 ? "s" : "",
	    zopt_verbose >= 4 ? "v" : "",
	    pool);
	free(isa);

	if (zopt_verbose >= 5)
		(void) printf("Executing %s\n", strstr(zdb, "zdb "));

	fp = popen(zdb, "r");

	while (fgets(zbuf, sizeof (zbuf), fp) != NULL)
		if (zopt_verbose >= 3)
			(void) printf("%s", zbuf);

	status = pclose(fp);

	if (status == 0)
		return;

	ztest_dump_core = 0;
	if (WIFEXITED(status))
		fatal(0, "'%s' exit code %d", zdb, WEXITSTATUS(status));
	else
		fatal(0, "'%s' died with signal %d", zdb, WTERMSIG(status));
}

static void
ztest_walk_pool_directory(char *header)
{
	spa_t *spa = NULL;

	if (zopt_verbose >= 6)
		(void) printf("%s\n", header);

	mutex_enter(&spa_namespace_lock);
	while ((spa = spa_next(spa)) != NULL)
		if (zopt_verbose >= 6)
			(void) printf("\t%s\n", spa_name(spa));
	mutex_exit(&spa_namespace_lock);
}

static void
ztest_spa_import_export(char *oldname, char *newname)
{
	nvlist_t *config;
	uint64_t pool_guid;
	spa_t *spa;
	int error;

	if (zopt_verbose >= 4) {
		(void) printf("import/export: old = %s, new = %s\n",
		    oldname, newname);
	}

	/*
	 * Clean up from previous runs.
	 */
	(void) spa_destroy(newname);

	/*
	 * Get the pool's configuration and guid.
	 */
	error = spa_open(oldname, &spa, FTAG);
	if (error)
		fatal(0, "spa_open('%s') = %d", oldname, error);

	pool_guid = spa_guid(spa);
	spa_close(spa, FTAG);

	ztest_walk_pool_directory("pools before export");

	/*
	 * Export it.
	 */
	error = spa_export(oldname, &config, B_FALSE);
	if (error)
		fatal(0, "spa_export('%s') = %d", oldname, error);

	ztest_walk_pool_directory("pools after export");

	/*
	 * Import it under the new name.
	 */
	error = spa_import(newname, config, NULL);
	if (error)
		fatal(0, "spa_import('%s') = %d", newname, error);

	ztest_walk_pool_directory("pools after import");

	/*
	 * Try to import it again -- should fail with EEXIST.
	 */
	error = spa_import(newname, config, NULL);
	if (error != EEXIST)
		fatal(0, "spa_import('%s') twice", newname);

	/*
	 * Try to import it under a different name -- should fail with EEXIST.
	 */
	error = spa_import(oldname, config, NULL);
	if (error != EEXIST)
		fatal(0, "spa_import('%s') under multiple names", newname);

	/*
	 * Verify that the pool is no longer visible under the old name.
	 */
	error = spa_open(oldname, &spa, FTAG);
	if (error != ENOENT)
		fatal(0, "spa_open('%s') = %d", newname, error);

	/*
	 * Verify that we can open and close the pool using the new name.
	 */
	error = spa_open(newname, &spa, FTAG);
	if (error)
		fatal(0, "spa_open('%s') = %d", newname, error);
	ASSERT(pool_guid == spa_guid(spa));
	spa_close(spa, FTAG);

	nvlist_free(config);
}

static void *
ztest_resume(void *arg)
{
	spa_t *spa = arg;

	while (!ztest_exiting) {
		(void) poll(NULL, 0, 1000);

		if (!spa_suspended(spa))
			continue;

		spa_vdev_state_enter(spa);
		vdev_clear(spa, NULL);
		(void) spa_vdev_state_exit(spa, NULL, 0);

		zio_resume(spa);
	}
	return (NULL);
}

static void *
ztest_thread(void *arg)
{
	ztest_args_t *za = arg;
	ztest_shared_t *zs = ztest_shared;
	hrtime_t now, functime;
	ztest_info_t *zi;
	int f, i;

	while ((now = gethrtime()) < za->za_stop) {
		/*
		 * See if it's time to force a crash.
		 */
		if (now > za->za_kill) {
			zs->zs_alloc = spa_get_alloc(za->za_spa);
			zs->zs_space = spa_get_space(za->za_spa);
			(void) kill(getpid(), SIGKILL);
		}

		/*
		 * Pick a random function.
		 */
		f = ztest_random(ZTEST_FUNCS);
		zi = &zs->zs_info[f];

		/*
		 * Decide whether to call it, based on the requested frequency.
		 */
		if (zi->zi_call_target == 0 ||
		    (double)zi->zi_call_total / zi->zi_call_target >
		    (double)(now - zs->zs_start_time) / (zopt_time * NANOSEC))
			continue;

		atomic_add_64(&zi->zi_calls, 1);
		atomic_add_64(&zi->zi_call_total, 1);

		za->za_diroff = (za->za_instance * ZTEST_FUNCS + f) *
		    ZTEST_DIRSIZE;
		za->za_diroff_shared = (1ULL << 63);

		for (i = 0; i < zi->zi_iters; i++)
			zi->zi_func(za);

		functime = gethrtime() - now;

		atomic_add_64(&zi->zi_call_time, functime);

		if (zopt_verbose >= 4) {
			Dl_info dli;
			(void) dladdr((void *)zi->zi_func, &dli);
			(void) printf("%6.2f sec in %s\n",
			    (double)functime / NANOSEC, dli.dli_sname);
		}

		/*
		 * If we're getting ENOSPC with some regularity, stop.
		 */
		if (zs->zs_enospc_count > 10)
			break;
	}

	return (NULL);
}

/*
 * Kick off threads to run tests on all datasets in parallel.
 */
static void
ztest_run(char *pool)
{
	int t, d, error;
	ztest_shared_t *zs = ztest_shared;
	ztest_args_t *za;
	spa_t *spa;
	char name[100];
	thread_t resume_tid;

	ztest_exiting = B_FALSE;

	(void) _mutex_init(&zs->zs_vdev_lock, USYNC_THREAD, NULL);
	(void) rwlock_init(&zs->zs_name_lock, USYNC_THREAD, NULL);

	for (t = 0; t < ZTEST_SYNC_LOCKS; t++)
		(void) _mutex_init(&zs->zs_sync_lock[t], USYNC_THREAD, NULL);

	/*
	 * Destroy one disk before we even start.
	 * It's mirrored, so everything should work just fine.
	 * This makes us exercise fault handling very early in spa_load().
	 */
	ztest_obliterate_one_disk(0);

	/*
	 * Verify that the sum of the sizes of all blocks in the pool
	 * equals the SPA's allocated space total.
	 */
	ztest_verify_blocks(pool);

	/*
	 * Kick off a replacement of the disk we just obliterated.
	 */
	kernel_init(FREAD | FWRITE);
	VERIFY(spa_open(pool, &spa, FTAG) == 0);
	ztest_replace_one_disk(spa, 0);
	if (zopt_verbose >= 5)
		show_pool_stats(spa);
	spa_close(spa, FTAG);
	kernel_fini();

	kernel_init(FREAD | FWRITE);

	/*
	 * Verify that we can export the pool and reimport it under a
	 * different name.
	 */
	if (ztest_random(2) == 0) {
		(void) snprintf(name, 100, "%s_import", pool);
		ztest_spa_import_export(pool, name);
		ztest_spa_import_export(name, pool);
	}

	/*
	 * Verify that we can loop over all pools.
	 */
	mutex_enter(&spa_namespace_lock);
	for (spa = spa_next(NULL); spa != NULL; spa = spa_next(spa)) {
		if (zopt_verbose > 3) {
			(void) printf("spa_next: found %s\n", spa_name(spa));
		}
	}
	mutex_exit(&spa_namespace_lock);

	/*
	 * Open our pool.
	 */
	VERIFY(spa_open(pool, &spa, FTAG) == 0);

	/*
	 * Create a thread to periodically resume suspended I/O.
	 */
	VERIFY(thr_create(0, 0, ztest_resume, spa, THR_BOUND,
	    &resume_tid) == 0);

	/*
	 * Verify that we can safely inquire about about any object,
	 * whether it's allocated or not.  To make it interesting,
	 * we probe a 5-wide window around each power of two.
	 * This hits all edge cases, including zero and the max.
	 */
	for (t = 0; t < 64; t++) {
		for (d = -5; d <= 5; d++) {
			error = dmu_object_info(spa->spa_meta_objset,
			    (1ULL << t) + d, NULL);
			ASSERT(error == 0 || error == ENOENT ||
			    error == EINVAL);
		}
	}

	/*
	 * Now kick off all the tests that run in parallel.
	 */
	zs->zs_enospc_count = 0;

	za = umem_zalloc(zopt_threads * sizeof (ztest_args_t), UMEM_NOFAIL);

	if (zopt_verbose >= 4)
		(void) printf("starting main threads...\n");

	za[0].za_start = gethrtime();
	za[0].za_stop = za[0].za_start + zopt_passtime * NANOSEC;
	za[0].za_stop = MIN(za[0].za_stop, zs->zs_stop_time);
	za[0].za_kill = za[0].za_stop;
	if (ztest_random(100) < zopt_killrate)
		za[0].za_kill -= ztest_random(zopt_passtime * NANOSEC);

	for (t = 0; t < zopt_threads; t++) {
		d = t % zopt_datasets;

		(void) strcpy(za[t].za_pool, pool);
		za[t].za_os = za[d].za_os;
		za[t].za_spa = spa;
		za[t].za_zilog = za[d].za_zilog;
		za[t].za_instance = t;
		za[t].za_random = ztest_random(-1ULL);
		za[t].za_start = za[0].za_start;
		za[t].za_stop = za[0].za_stop;
		za[t].za_kill = za[0].za_kill;

		if (t < zopt_datasets) {
			ztest_replay_t zr;
			int test_future = FALSE;
			(void) rw_rdlock(&ztest_shared->zs_name_lock);
			(void) snprintf(name, 100, "%s/%s_%d", pool, pool, d);
			error = dmu_objset_create(name, DMU_OST_OTHER, NULL, 0,
			    ztest_create_cb, NULL);
			if (error == EEXIST) {
				test_future = TRUE;
			} else if (error == ENOSPC) {
				zs->zs_enospc_count++;
				(void) rw_unlock(&ztest_shared->zs_name_lock);
				break;
			} else if (error != 0) {
				fatal(0, "dmu_objset_create(%s) = %d",
				    name, error);
			}
			error = dmu_objset_open(name, DMU_OST_OTHER,
			    DS_MODE_USER, &za[d].za_os);
			if (error)
				fatal(0, "dmu_objset_open('%s') = %d",
				    name, error);
			(void) rw_unlock(&ztest_shared->zs_name_lock);
			if (test_future)
				ztest_dmu_check_future_leak(&za[t]);
			zr.zr_os = za[d].za_os;
			zil_replay(zr.zr_os, &zr, &zr.zr_assign,
			    ztest_replay_vector, NULL);
			za[d].za_zilog = zil_open(za[d].za_os, NULL);
		}

		VERIFY(thr_create(0, 0, ztest_thread, &za[t], THR_BOUND,
		    &za[t].za_thread) == 0);
	}

	while (--t >= 0) {
		VERIFY(thr_join(za[t].za_thread, NULL, NULL) == 0);
		if (t < zopt_datasets) {
			zil_close(za[t].za_zilog);
			dmu_objset_close(za[t].za_os);
		}
	}

	if (zopt_verbose >= 3)
		show_pool_stats(spa);

	txg_wait_synced(spa_get_dsl(spa), 0);

	zs->zs_alloc = spa_get_alloc(spa);
	zs->zs_space = spa_get_space(spa);

	/*
	 * If we had out-of-space errors, destroy a random objset.
	 */
	if (zs->zs_enospc_count != 0) {
		(void) rw_rdlock(&ztest_shared->zs_name_lock);
		d = (int)ztest_random(zopt_datasets);
		(void) snprintf(name, 100, "%s/%s_%d", pool, pool, d);
		if (zopt_verbose >= 3)
			(void) printf("Destroying %s to free up space\n", name);
		(void) dmu_objset_find(name, ztest_destroy_cb, &za[d],
		    DS_FIND_SNAPSHOTS | DS_FIND_CHILDREN);
		(void) rw_unlock(&ztest_shared->zs_name_lock);
	}

	txg_wait_synced(spa_get_dsl(spa), 0);

	umem_free(za, zopt_threads * sizeof (ztest_args_t));

	/* Kill the resume thread */
	ztest_exiting = B_TRUE;
	VERIFY(thr_join(resume_tid, NULL, NULL) == 0);

	/*
	 * Right before closing the pool, kick off a bunch of async I/O;
	 * spa_close() should wait for it to complete.
	 */
	for (t = 1; t < 50; t++)
		dmu_prefetch(spa->spa_meta_objset, t, 0, 1 << 15);

	spa_close(spa, FTAG);

	kernel_fini();
}

void
print_time(hrtime_t t, char *timebuf)
{
	hrtime_t s = t / NANOSEC;
	hrtime_t m = s / 60;
	hrtime_t h = m / 60;
	hrtime_t d = h / 24;

	s -= m * 60;
	m -= h * 60;
	h -= d * 24;

	timebuf[0] = '\0';

	if (d)
		(void) sprintf(timebuf,
		    "%llud%02lluh%02llum%02llus", d, h, m, s);
	else if (h)
		(void) sprintf(timebuf, "%lluh%02llum%02llus", h, m, s);
	else if (m)
		(void) sprintf(timebuf, "%llum%02llus", m, s);
	else
		(void) sprintf(timebuf, "%llus", s);
}

/*
 * Create a storage pool with the given name and initial vdev size.
 * Then create the specified number of datasets in the pool.
 */
static void
ztest_init(char *pool)
{
	spa_t *spa;
	int error;
	nvlist_t *nvroot;

	kernel_init(FREAD | FWRITE);

	/*
	 * Create the storage pool.
	 */
	(void) spa_destroy(pool);
	ztest_shared->zs_vdev_primaries = 0;
	nvroot = make_vdev_root(NULL, NULL, zopt_vdev_size, 0,
	    0, zopt_raidz, zopt_mirrors, 1);
	error = spa_create(pool, nvroot, NULL, NULL, NULL);
	nvlist_free(nvroot);

	if (error)
		fatal(0, "spa_create() = %d", error);
	error = spa_open(pool, &spa, FTAG);
	if (error)
		fatal(0, "spa_open() = %d", error);

	if (zopt_verbose >= 3)
		show_pool_stats(spa);

	spa_close(spa, FTAG);

	kernel_fini();
}

int
main(int argc, char **argv)
{
	int kills = 0;
	int iters = 0;
	int i, f;
	ztest_shared_t *zs;
	ztest_info_t *zi;
	char timebuf[100];
	char numbuf[6];

	(void) setvbuf(stdout, NULL, _IOLBF, 0);

	/* Override location of zpool.cache */
	spa_config_path = "/tmp/zpool.cache";

	ztest_random_fd = open("/dev/urandom", O_RDONLY);

	process_options(argc, argv);

	argc -= optind;
	argv += optind;

	dprintf_setup(&argc, argv);

	/*
	 * Blow away any existing copy of zpool.cache
	 */
	if (zopt_init != 0)
		(void) remove("/tmp/zpool.cache");

	zs = ztest_shared = (void *)mmap(0,
	    P2ROUNDUP(sizeof (ztest_shared_t), getpagesize()),
	    PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANON, -1, 0);

	if (zopt_verbose >= 1) {
		(void) printf("%llu vdevs, %d datasets, %d threads,"
		    " %llu seconds...\n",
		    (u_longlong_t)zopt_vdevs, zopt_datasets, zopt_threads,
		    (u_longlong_t)zopt_time);
	}

	/*
	 * Create and initialize our storage pool.
	 */
	for (i = 1; i <= zopt_init; i++) {
		bzero(zs, sizeof (ztest_shared_t));
		if (zopt_verbose >= 3 && zopt_init != 1)
			(void) printf("ztest_init(), pass %d\n", i);
		ztest_init(zopt_pool);
	}

	/*
	 * Initialize the call targets for each function.
	 */
	for (f = 0; f < ZTEST_FUNCS; f++) {
		zi = &zs->zs_info[f];

		*zi = ztest_info[f];

		if (*zi->zi_interval == 0)
			zi->zi_call_target = UINT64_MAX;
		else
			zi->zi_call_target = zopt_time / *zi->zi_interval;
	}

	zs->zs_start_time = gethrtime();
	zs->zs_stop_time = zs->zs_start_time + zopt_time * NANOSEC;

	/*
	 * Run the tests in a loop.  These tests include fault injection
	 * to verify that self-healing data works, and forced crashes
	 * to verify that we never lose on-disk consistency.
	 */
	while (gethrtime() < zs->zs_stop_time) {
		int status;
		pid_t pid;
		char *tmp;

		/*
		 * Initialize the workload counters for each function.
		 */
		for (f = 0; f < ZTEST_FUNCS; f++) {
			zi = &zs->zs_info[f];
			zi->zi_calls = 0;
			zi->zi_call_time = 0;
		}

		pid = fork();

		if (pid == -1)
			fatal(1, "fork failed");

		if (pid == 0) {	/* child */
			struct rlimit rl = { 1024, 1024 };
			(void) setrlimit(RLIMIT_NOFILE, &rl);
			(void) enable_extended_FILE_stdio(-1, -1);
			ztest_run(zopt_pool);
			exit(0);
		}

		while (waitpid(pid, &status, 0) != pid)
			continue;

		if (WIFEXITED(status)) {
			if (WEXITSTATUS(status) != 0) {
				(void) fprintf(stderr,
				    "child exited with code %d\n",
				    WEXITSTATUS(status));
				exit(2);
			}
		} else if (WIFSIGNALED(status)) {
			if (WTERMSIG(status) != SIGKILL) {
				(void) fprintf(stderr,
				    "child died with signal %d\n",
				    WTERMSIG(status));
				exit(3);
			}
			kills++;
		} else {
			(void) fprintf(stderr, "something strange happened "
			    "to child\n");
			exit(4);
		}

		iters++;

		if (zopt_verbose >= 1) {
			hrtime_t now = gethrtime();

			now = MIN(now, zs->zs_stop_time);
			print_time(zs->zs_stop_time - now, timebuf);
			nicenum(zs->zs_space, numbuf);

			(void) printf("Pass %3d, %8s, %3llu ENOSPC, "
			    "%4.1f%% of %5s used, %3.0f%% done, %8s to go\n",
			    iters,
			    WIFEXITED(status) ? "Complete" : "SIGKILL",
			    (u_longlong_t)zs->zs_enospc_count,
			    100.0 * zs->zs_alloc / zs->zs_space,
			    numbuf,
			    100.0 * (now - zs->zs_start_time) /
			    (zopt_time * NANOSEC), timebuf);
		}

		if (zopt_verbose >= 2) {
			(void) printf("\nWorkload summary:\n\n");
			(void) printf("%7s %9s   %s\n",
			    "Calls", "Time", "Function");
			(void) printf("%7s %9s   %s\n",
			    "-----", "----", "--------");
			for (f = 0; f < ZTEST_FUNCS; f++) {
				Dl_info dli;

				zi = &zs->zs_info[f];
				print_time(zi->zi_call_time, timebuf);
				(void) dladdr((void *)zi->zi_func, &dli);
				(void) printf("%7llu %9s   %s\n",
				    (u_longlong_t)zi->zi_calls, timebuf,
				    dli.dli_sname);
			}
			(void) printf("\n");
		}

		/*
		 * It's possible that we killed a child during a rename test, in
		 * which case we'll have a 'ztest_tmp' pool lying around instead
		 * of 'ztest'.  Do a blind rename in case this happened.
		 */
		tmp = umem_alloc(strlen(zopt_pool) + 5, UMEM_NOFAIL);
		(void) strcpy(tmp, zopt_pool);
		(void) strcat(tmp, "_tmp");
		kernel_init(FREAD | FWRITE);
		(void) spa_rename(tmp, zopt_pool);
		kernel_fini();
		umem_free(tmp, strlen(tmp) + 1);
	}

	ztest_verify_blocks(zopt_pool);

	if (zopt_verbose >= 1) {
		(void) printf("%d killed, %d completed, %.0f%% kill rate\n",
		    kills, iters - kills, (100.0 * kills) / MAX(1, iters));
	}

	return (0);
}
