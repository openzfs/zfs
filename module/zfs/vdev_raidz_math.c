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
 * Copyright (C) 2016 Gvozden Nešković. All rights reserved.
 */

#include <sys/zfs_context.h>
#include <sys/types.h>
#include <sys/zio.h>
#include <sys/debug.h>
#include <sys/zfs_debug.h>

#include <sys/vdev_raidz.h>
#include <sys/vdev_raidz_impl.h>

/* All compiled in implementations */
const raidz_impl_ops_t *raidz_all_maths[] = {
	&vdev_raidz_scalar_impl,
#if defined(__x86_64) && defined(HAVE_SSE2)	/* only x86_64 for now */
	&vdev_raidz_sse2_impl,
#endif
#if defined(__x86_64) && defined(HAVE_SSSE3)	/* only x86_64 for now */
	&vdev_raidz_ssse3_impl,
#endif
#if defined(__x86_64) && defined(HAVE_AVX2)	/* only x86_64 for now */
	&vdev_raidz_avx2_impl
#endif
};

/* Indicate that benchmark has been completed */
static boolean_t raidz_math_initialized = B_FALSE;

/* Select raidz implementation */
static enum vdev_raidz_impl_sel {
	IMPL_FASTEST	= -1,
	IMPL_ORIGINAL	= -2,
	IMPL_CYCLE	= -3,
	IMPL_SCALAR	=  0,
} zfs_vdev_raidz_impl = IMPL_SCALAR;

/* selected implementation and its lock */
static krwlock_t vdev_raidz_impl_lock;
static raidz_impl_ops_t *vdev_raidz_used_impl =
	(raidz_impl_ops_t *) &vdev_raidz_scalar_impl;
static boolean_t vdev_raidz_impl_user_set = B_FALSE;

/* RAIDZ op that contain the fastest routines */
static raidz_impl_ops_t vdev_raidz_fastest_impl = {
	.name = "fastest"
};

/* Hold all supported implementations */
size_t raidz_supp_impl_cnt = 1;
raidz_impl_ops_t *raidz_supp_impl[ARRAY_SIZE(raidz_all_maths) + 1] = {
	(raidz_impl_ops_t *) &vdev_raidz_scalar_impl, /* scalar is supported */
	NULL
};

/*
 * kstats values for supported impl & original methods
 * Values represent per disk throughput of 8 disk+parity raidz vdev (Bps)
 */
static raidz_impl_kstat_t raidz_impl_kstats[ARRAY_SIZE(raidz_all_maths) + 1];

/* kstat for benchmarked implementations */
static kstat_t *raidz_math_kstat = NULL;

/*
 * Selects the raidz operation for raidz_map
 * If rm_ops is set to NULL original raidz implementation will be used
 */
void
vdev_raidz_math_get_ops(raidz_map_t *rm)
{
	rw_enter(&vdev_raidz_impl_lock, RW_READER);

	rm->rm_ops = vdev_raidz_used_impl;

#if !defined(_KERNEL)
	if (zfs_vdev_raidz_impl == IMPL_CYCLE) {
		static size_t cycle_impl_idx = 0;
		size_t idx;
		/*
		 * Cycle through all supported new implementations, and
		 * when idx == raidz_supp_impl_cnt, use the original
		 */
		idx = (++cycle_impl_idx) % (raidz_supp_impl_cnt + 1);
		rm->rm_ops = raidz_supp_impl[idx];
	}
#endif

	rw_exit(&vdev_raidz_impl_lock);
}

/*
 * Select parity generation method for raidz_map
 */
void
vdev_raidz_math_generate(raidz_map_t *rm)
{
	raidz_gen_f gen_parity = NULL;

	switch (raidz_parity(rm)) {
		case 1:
			gen_parity = rm->rm_ops->gen[RAIDZ_GEN_P];
			break;
		case 2:
			gen_parity = rm->rm_ops->gen[RAIDZ_GEN_PQ];
			break;
		case 3:
			gen_parity = rm->rm_ops->gen[RAIDZ_GEN_PQR];
			break;
		default:
			gen_parity = NULL;
			cmn_err(CE_PANIC, "invalid RAID-Z configuration %d",
				raidz_parity(rm));
			break;
	}

	ASSERT(gen_parity != NULL);

	gen_parity(rm);
}

static raidz_rec_f
_reconstruct_fun_raidz1(raidz_map_t *rm, const int *parity_valid,
	const int nbaddata)
{
	if (nbaddata == 1 && parity_valid[CODE_P]) {
		return (rm->rm_ops->rec[RAIDZ_REC_P]);
	}
	return ((raidz_rec_f) NULL);
}

static raidz_rec_f
_reconstruct_fun_raidz2(raidz_map_t *rm, const int *parity_valid,
	const int nbaddata)
{
	if (nbaddata == 1) {
		if (parity_valid[CODE_P]) {
			return (rm->rm_ops->rec[RAIDZ_REC_P]);
		} else if (parity_valid[CODE_Q]) {
			return (rm->rm_ops->rec[RAIDZ_REC_Q]);
		}
	} else if (nbaddata == 2 &&
		parity_valid[CODE_P] && parity_valid[CODE_Q]) {
		return (rm->rm_ops->rec[RAIDZ_REC_PQ]);
	}
	return ((raidz_rec_f) NULL);
}

static raidz_rec_f
_reconstruct_fun_raidz3(raidz_map_t *rm, const int *parity_valid,
	const int nbaddata)
{
	if (nbaddata == 1) {
		if (parity_valid[CODE_P]) {
			return (rm->rm_ops->rec[RAIDZ_REC_P]);
		} else if (parity_valid[CODE_Q]) {
			return (rm->rm_ops->rec[RAIDZ_REC_Q]);
		} else if (parity_valid[CODE_R]) {
			return (rm->rm_ops->rec[RAIDZ_REC_R]);
		}
	} else if (nbaddata == 2) {
		if (parity_valid[CODE_P] && parity_valid[CODE_Q]) {
			return (rm->rm_ops->rec[RAIDZ_REC_PQ]);
		} else if (parity_valid[CODE_P] && parity_valid[CODE_R]) {
			return (rm->rm_ops->rec[RAIDZ_REC_PR]);
		} else if (parity_valid[CODE_Q] && parity_valid[CODE_R]) {
			return (rm->rm_ops->rec[RAIDZ_REC_QR]);
		}
	} else if (nbaddata == 3 &&
		parity_valid[CODE_P] && parity_valid[CODE_Q] &&
		parity_valid[CODE_R]) {
		return (rm->rm_ops->rec[RAIDZ_REC_PQR]);
	}
	return ((raidz_rec_f) NULL);
}

/*
 * Select data reconstruction method for raidz_map
 * @parity_valid - Parity validity flag
 * @dt           - Failed data index array
 * @nbaddata     - Number of failed data columns
 */
int
vdev_raidz_math_reconstruct(raidz_map_t *rm, const int *parity_valid,
	const int *dt, const int nbaddata)
{
	raidz_rec_f rec_data = NULL;

	switch (raidz_parity(rm)) {
		case 1:
			rec_data = _reconstruct_fun_raidz1(rm, parity_valid,
			    nbaddata);
			break;
		case 2:
			rec_data = _reconstruct_fun_raidz2(rm, parity_valid,
			    nbaddata);
			break;
		case 3:
			rec_data = _reconstruct_fun_raidz3(rm, parity_valid,
			    nbaddata);
			break;
		default:
			cmn_err(CE_PANIC, "invalid RAID-Z configuration %d",
			    raidz_parity(rm));
			break;
	}

	ASSERT(rec_data != NULL);

	return (rec_data(rm, dt));
}

const char *raidz_gen_name[] = {
	"gen_p", "gen_pq", "gen_pqr"
};
const char *raidz_rec_name[] = {
	"rec_p", "rec_q", "rec_r",
	"rec_pq", "rec_pr", "rec_qr", "rec_pqr"
};

static void
init_raidz_kstat(raidz_impl_kstat_t *rs, const char *name)
{
	int i;
	const size_t impl_name_len = strnlen(name, KSTAT_STRLEN);
	const size_t op_name_max = (KSTAT_STRLEN - 2) > impl_name_len ?
		KSTAT_STRLEN - impl_name_len - 2 : 0;

	for (i = 0; i < RAIDZ_GEN_NUM; i++) {
		strncpy(rs->gen[i].name, name, impl_name_len);
		strncpy(rs->gen[i].name + impl_name_len, "_", 1);
		strncpy(rs->gen[i].name + impl_name_len + 1,
			raidz_gen_name[i], op_name_max);

		rs->gen[i].data_type = KSTAT_DATA_UINT64;
		rs->gen[i].value.ui64  = 0;
	}

	for (i = 0; i < RAIDZ_REC_NUM; i++) {
		strncpy(rs->rec[i].name, name, impl_name_len);
		strncpy(rs->rec[i].name + impl_name_len, "_", 1);
		strncpy(rs->rec[i].name + impl_name_len + 1,
			raidz_rec_name[i], op_name_max);

		rs->rec[i].data_type = KSTAT_DATA_UINT64;
		rs->rec[i].value.ui64  = 0;
	}
}

#define	BENCH_D_COLS	(8ULL)
#define	BENCH_COLS	(BENCH_D_COLS + PARITY_PQR)
#define	BENCH_ZIO_SIZE	(1ULL << SPA_OLD_MAXBLOCKSHIFT)	/* 128 kiB */
#define	BENCH_NS	MSEC2NSEC(25)			/* 25ms */

typedef void (*benchmark_fn)(raidz_map_t *rm, const int fn);

static void
benchmark_gen_impl(raidz_map_t *rm, const int fn)
{
	(void) fn;
	vdev_raidz_generate_parity(rm);
}

static void
benchmark_rec_impl(raidz_map_t *rm, const int fn)
{
	static const int rec_tgt[7][3] = {
		{1, 2, 3},	/* rec_p:   bad QR & D[0]	*/
		{0, 2, 3},	/* rec_q:   bad PR & D[0]	*/
		{0, 1, 3},	/* rec_r:   bad PQ & D[0]	*/
		{2, 3, 4},	/* rec_pq:  bad R  & D[0][1]	*/
		{1, 3, 4},	/* rec_pr:  bad Q  & D[0][1]	*/
		{0, 3, 4},	/* rec_qr:  bad P  & D[0][1]	*/
		{3, 4, 5}	/* rec_pqr: bad    & D[0][1][2] */
	};

	vdev_raidz_reconstruct(rm, rec_tgt[fn], 3);
}

/*
 * Benchmarking of all supported implementations (raidz_supp_impl_cnt)
 * is performed by setting the rm_ops pointer and calling the top level
 * generate/reconstruct methods of bench_rm.
 */
static void
benchmark_raidz_impl(raidz_map_t *bench_rm, const int fn, benchmark_fn bench_fn)
{
	uint64_t run_cnt, speed, best_speed = 0;
	hrtime_t t_start, t_diff;
	raidz_impl_ops_t *curr_impl;
	int impl, i;

	/*
	 * Use the sentinel (NULL) from the end of raidz_supp_impl_cnt
	 * to run "original" implementation (bench_rm->rm_ops = NULL)
	 */
	for (impl = 0; impl <= raidz_supp_impl_cnt; impl++) {
		/* set an implementation to benchmark */
		curr_impl = raidz_supp_impl[impl];
		bench_rm->rm_ops = curr_impl;

		run_cnt = 0;
		t_start = gethrtime();

		do {
			for (i = 0; i < 25; i++, run_cnt++)
				bench_fn(bench_rm, fn);

			t_diff = gethrtime() - t_start;
		} while (t_diff < BENCH_NS);

		speed = run_cnt * BENCH_ZIO_SIZE * NANOSEC;
		speed /= (t_diff * BENCH_COLS);

		if (bench_fn == benchmark_gen_impl)
			raidz_impl_kstats[impl].gen[fn].value.ui64 = speed;
		else
			raidz_impl_kstats[impl].rec[fn].value.ui64 = speed;

		/* if curr_impl==NULL the original impl is benchmarked */
		if (curr_impl != NULL && speed > best_speed) {
			best_speed = speed;

			if (bench_fn == benchmark_gen_impl)
				vdev_raidz_fastest_impl.gen[fn] =
				    curr_impl->gen[fn];
			else
				vdev_raidz_fastest_impl.rec[fn] =
				    curr_impl->rec[fn];
		}
	}
}

void
vdev_raidz_math_init(void)
{
	raidz_impl_ops_t *curr_impl;
	zio_t *bench_zio = NULL;
	raidz_map_t *bench_rm = NULL;
	uint64_t bench_parity;
	int i, c, fn;

	/* init & vdev_raidz_impl_lock */
	rw_init(&vdev_raidz_impl_lock, NULL, RW_DEFAULT, NULL);

	/* move supported impl into raidz_supp_impl */
	for (i = 0, c = 0; i < ARRAY_SIZE(raidz_all_maths); i++) {
		curr_impl = (raidz_impl_ops_t *) raidz_all_maths[i];

		/* initialize impl */
		if (curr_impl->init)
			curr_impl->init();

		if (curr_impl->is_supported()) {
			/* init kstat */
			init_raidz_kstat(&raidz_impl_kstats[c],
			    curr_impl->name);
			raidz_supp_impl[c++] = (raidz_impl_ops_t *) curr_impl;
		}
	}
	raidz_supp_impl_cnt = c;	/* number of supported impl */
	raidz_supp_impl[c] = NULL;	/* sentinel */

	/* init kstat for original routines */
	init_raidz_kstat(&(raidz_impl_kstats[raidz_supp_impl_cnt]), "original");

#if !defined(_KERNEL)
	/*
	 * Skip benchmarking and use last implementation as fastest
	 */
	memcpy(&vdev_raidz_fastest_impl, raidz_supp_impl[raidz_supp_impl_cnt-1],
	    sizeof (vdev_raidz_fastest_impl));

	vdev_raidz_fastest_impl.name = "fastest";

	raidz_math_initialized = B_TRUE;

	/* Use 'cycle' math selection method for userspace */
	VERIFY0(vdev_raidz_impl_set("cycle"));
	return;
#endif

	/* Fake an zio and run the benchmark on it */
	bench_zio = kmem_zalloc(sizeof (zio_t), KM_SLEEP);
	bench_zio->io_offset = 0;
	bench_zio->io_size = BENCH_ZIO_SIZE; /* only data columns */
	bench_zio->io_data = zio_data_buf_alloc(BENCH_ZIO_SIZE);
	VERIFY(bench_zio->io_data);

	/* Benchmark parity generation methods */
	for (fn = 0; fn < RAIDZ_GEN_NUM; fn++) {
		bench_parity = fn + 1;
		/* New raidz_map is needed for each generate_p/q/r */
		bench_rm = vdev_raidz_map_alloc(bench_zio, 9,
		    BENCH_D_COLS + bench_parity, bench_parity);

		benchmark_raidz_impl(bench_rm, fn, benchmark_gen_impl);

		vdev_raidz_map_free(bench_rm);
	}

	/* Benchmark data reconstruction methods */
	bench_rm = vdev_raidz_map_alloc(bench_zio, 9, BENCH_COLS, PARITY_PQR);

	for (fn = 0; fn < RAIDZ_REC_NUM; fn++)
		benchmark_raidz_impl(bench_rm, fn, benchmark_rec_impl);

	vdev_raidz_map_free(bench_rm);

	/* cleanup the bench zio */
	zio_data_buf_free(bench_zio->io_data, BENCH_ZIO_SIZE);
	kmem_free(bench_zio, sizeof (zio_t));

	/* install kstats for all impl */
	raidz_math_kstat = kstat_create("zfs", 0, "vdev_raidz_bench",
		"misc", KSTAT_TYPE_NAMED,
		sizeof (raidz_impl_kstat_t) / sizeof (kstat_named_t) *
		(raidz_supp_impl_cnt + 1), KSTAT_FLAG_VIRTUAL);

	if (raidz_math_kstat != NULL) {
		raidz_math_kstat->ks_data = raidz_impl_kstats;
		kstat_install(raidz_math_kstat);
	}

	/* Finish initialization */
	raidz_math_initialized = B_TRUE;
	if (!vdev_raidz_impl_user_set)
		VERIFY0(vdev_raidz_impl_set("fastest"));
}

void
vdev_raidz_math_fini(void)
{
	raidz_impl_ops_t const *curr_impl;
	int i;

	if (raidz_math_kstat != NULL) {
		kstat_delete(raidz_math_kstat);
		raidz_math_kstat = NULL;
	}

	rw_destroy(&vdev_raidz_impl_lock);

	/* fini impl */
	for (i = 0; i < ARRAY_SIZE(raidz_all_maths); i++) {
		curr_impl = raidz_all_maths[i];

		if (curr_impl->fini)
			curr_impl->fini();
	}
}

static const
struct {
	char *name;
	raidz_impl_ops_t *impl;
	enum vdev_raidz_impl_sel sel;
} math_impl_opts[] = {
		{ "fastest",  &vdev_raidz_fastest_impl, IMPL_FASTEST },
		{ "original", NULL, IMPL_ORIGINAL },
#if !defined(_KERNEL)
		{ "cycle",    NULL, IMPL_CYCLE },
#endif
};

/*
 * Function sets desired raidz implementation.
 * If called after module_init(), vdev_raidz_impl_lock must be held for writing.
 *
 * @val		Name of raidz implementation to use
 * @param	Unused.
 */
static int
zfs_vdev_raidz_impl_set(const char *val, struct kernel_param *kp)
{
	size_t i;

	/* Check mandatory options */
	for (i = 0; i < ARRAY_SIZE(math_impl_opts); i++) {
		if (strcmp(val, math_impl_opts[i].name) == 0) {
			zfs_vdev_raidz_impl = math_impl_opts[i].sel;
			vdev_raidz_used_impl = math_impl_opts[i].impl;
			vdev_raidz_impl_user_set = B_TRUE;
			return (0);
		}
	}

	/* check all supported implementations */
	for (i = 0; i < raidz_supp_impl_cnt; i++) {
		if (strcmp(val, raidz_supp_impl[i]->name) == 0) {
			zfs_vdev_raidz_impl = i;
			vdev_raidz_used_impl = raidz_supp_impl[i];
			vdev_raidz_impl_user_set = B_TRUE;
			return (0);
		}
	}

	return (-EINVAL);
}

int
vdev_raidz_impl_set(const char *val)
{
	int err;

	ASSERT(raidz_math_initialized);

	rw_enter(&vdev_raidz_impl_lock, RW_WRITER);
	err = zfs_vdev_raidz_impl_set(val, NULL);
	rw_exit(&vdev_raidz_impl_lock);
	return (err);
}

#if defined(_KERNEL) && defined(HAVE_SPL)
static int
zfs_vdev_raidz_impl_get(char *buffer, struct kernel_param *kp)
{
	int i, cnt = 0;
	char *fmt;

	ASSERT(raidz_math_initialized);

	rw_enter(&vdev_raidz_impl_lock, RW_READER);

	/* list mandatory options */
	for (i = 0; i < ARRAY_SIZE(math_impl_opts); i++) {
		if (math_impl_opts[i].sel == zfs_vdev_raidz_impl)
			fmt = "[%s] ";
		else
			fmt = "%s ";

		cnt += sprintf(buffer + cnt, fmt, math_impl_opts[i].name);
	}

	/* list all supported implementations */
	for (i = 0; i < raidz_supp_impl_cnt; i++) {
		fmt = (i == zfs_vdev_raidz_impl) ? "[%s] " : "%s ";
		cnt += sprintf(buffer + cnt, fmt, raidz_supp_impl[i]->name);
	}

	rw_exit(&vdev_raidz_impl_lock);

	return (cnt);
}

module_param_call(zfs_vdev_raidz_impl, zfs_vdev_raidz_impl_set,
	zfs_vdev_raidz_impl_get, NULL, 0644);
MODULE_PARM_DESC(zfs_vdev_raidz_impl, "Select raidz implementation.");
#endif
