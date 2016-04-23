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
// #include <sys/resource.h>
#include <sys/types.h>
#include <sys/zio.h>
#include <sys/debug.h>
#include <sys/zfs_debug.h>

#include <sys/vdev_raidz.h>
#include <sys/vdev_raidz_impl.h>

#define	ARR_LEN(x) (sizeof (x) / sizeof (x[0]))

extern const raidz_math_ops_t vdev_raidz_scalar_impl;
extern const raidz_math_ops_t vdev_raidz_sse_impl;
extern const raidz_math_ops_t vdev_raidz_avx2_impl;

const char *raidz_impl_names[] = {
	"original",
	"scalar",
	"sse",
	"avx2",
	NULL
};

const char *raidz_gen_name[] = {
	"gen_p", "gen_pq", "gen_pqr"
};
const char *raidz_rec_name[] = {
	"rec_p", "rec_q", "rec_r",
	"rec_pq", "rec_pr", "rec_qr", "rec_pqr"
};

/*
 * Select the math impl
 */
static enum math_impl_sel {
	IMPL_FASTEST	= -1,
	IMPL_ORIGINAL	= -2,
	IMPL_CYCLE	= -3
	/*
	 * between 0 and raidz_supp_maths_cnt values
	 * are index in raidz_supp_maths
	 */
} zfs_raidz_math_impl = IMPL_FASTEST;

/* Indicate that benchmark has been completed */
static boolean_t raidz_math_initialized = B_FALSE;

/* RAIDZ op that contain the fastest routines (can have mixed impl) */
static raidz_math_ops_t vdev_raidz_fastest_impl = {
	.name = "fastest",
	.is_supported = NULL
};

/* selected implementation and its lock */
static raidz_math_ops_t *vdev_raidz_used_impl = &vdev_raidz_fastest_impl;
static krwlock_t vdev_raidz_used_impl_lock;

/* All compiled in implementations */
const raidz_math_ops_t *raidz_all_maths[] = {
	&vdev_raidz_scalar_impl,
#if defined(__x86_64) && defined(HAVE_SSSE3)	/* only x86_64 for now */
	&vdev_raidz_sse_impl,
#endif
#if defined(__x86_64) && defined(HAVE_AVX2)	/* only x86_64 for now */
	&vdev_raidz_avx2_impl
#endif
};

/* Hold all impl available in runtime */
size_t raidz_supp_maths_cnt = 1;
raidz_math_ops_t *raidz_supp_maths[ARR_LEN(raidz_all_maths)] = {
	(raidz_math_ops_t *)&vdev_raidz_scalar_impl /* scalar is supported */
};

/* Holding kstats for all algos + original methods */
static raidz_math_ops_kstat_t raidz_math_kstats[ARR_LEN(raidz_all_maths)+1];

/* kstat for benchmarked algos */
static kstat_t *raidz_math_kstat = NULL;

/*
 * Selects the raidz operation for raidz_map
 * If rm_ops is set to NULL original raidz implementation will be used
 */
void
vdev_raidz_math_set_ops(raidz_map_t *rm)
{
	static uint32_t cycle_impl_idx = 0;
	uint32_t idx;
	/*
	 * Set raidz implementation
	 */
	rw_enter(&vdev_raidz_used_impl_lock, RW_READER);

	rm->rm_ops = vdev_raidz_used_impl;

	if (zfs_raidz_math_impl == IMPL_CYCLE) {
		/*
		 * Cycle through all supprted new inmplementations, and
		 * when idx == raidz_supp_maths_cnt, use the original
		 */
		idx = atomic_add_32_nv(&cycle_impl_idx, 1);
		idx = idx % (raidz_supp_maths_cnt+1);

		if (idx == raidz_supp_maths_cnt) {
			rm->rm_ops = NULL;
		} else {
			rm->rm_ops = raidz_supp_maths[idx];
		}
	}

	rw_exit(&vdev_raidz_used_impl_lock);
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

static void
init_raidz_kstat(raidz_math_ops_kstat_t *rs, const char *name)
{
	int i;
	const size_t impl_name_len = strnlen(name, KSTAT_STRLEN);
	const size_t op_name_max = (KSTAT_STRLEN-2) > impl_name_len ?
		KSTAT_STRLEN - impl_name_len - 2 : 0;

	for (i = 0; i < RAIDZ_GEN_NUM; i++) {
		strncpy(rs->gen_kstat[i].name, name, impl_name_len);
		strncpy(rs->gen_kstat[i].name + impl_name_len, "_", 1);
		strncpy(rs->gen_kstat[i].name + impl_name_len + 1,
			raidz_gen_name[i], op_name_max);

		rs->gen_kstat[i].data_type = KSTAT_DATA_UINT64;
		rs->gen_kstat[i].value.ui64  = 0;
	}

	for (i = 0; i < RAIDZ_REC_NUM; i++) {
		strncpy(rs->rec_kstat[i].name, name, impl_name_len);
		strncpy(rs->rec_kstat[i].name + impl_name_len, "_", 1);
		strncpy(rs->rec_kstat[i].name + impl_name_len + 1,
			raidz_rec_name[i], op_name_max);

		rs->rec_kstat[i].data_type = KSTAT_DATA_UINT64;
		rs->rec_kstat[i].value.ui64  = 0;
	}
}


/* Number of pages for benchmark: 2^BENCH_SIZE_ORD */
#define	BENCH_D_COLS	(8)
#define	BENCH_COLS	(3 + BENCH_D_COLS)
#define	BENCH_COL_SIZE	(512)
#define	BENCH_ZIO_SIZE	(BENCH_COL_SIZE * BENCH_D_COLS)
#define	BENCH_NS	(1ULL*1000ULL*1000ULL) /* 50ms */

#define	BENCH_RES_FMT	"RAIDZ_math: [%-7s] %-8s %5ld.%.3ld MB/s\n"
#define	BENCH_SKIP_FMT	"RAIDZ_math: [%-7s] is not supported.\n"

#if defined(_KERNEL)
#define	bench_print(x...)	cmn_err(CE_CONT, x)
#else
#define	bench_print(x...)	do { } while (0)
#endif

void
vdev_raidz_math_init(void)
{
	const int impl_num = ARR_LEN(raidz_all_maths);
	const int tgtidx[3] = {3, 4, 5};
	raidz_math_ops_t const *curr_impl;
	unsigned long long run_count, best_run_count, speed;
	hrtime_t t_start;
	zio_t *bench_zio = NULL;
	raidz_map_t *bench_rm = NULL;
	uint64_t bench_parity;
	int i, c, fn, impl;
	int bench_rec_orig[7][3] = {
		{1, 2, 3}, {0, 2, 3},
		{0, 1, 3}, {2, 3, 4},
		{1, 3, 4}, {0, 3, 4},
		{3, 4, 5}
	};

	/* init & vdev_raidz_used_impl_lock */
	rw_init(&vdev_raidz_used_impl_lock, NULL, RW_DEFAULT, NULL);

#if !defined(_KERNEL)
	/*
	 * It is possible that this routine is called multiple times
	 * inside user-space test. Check if we already initialized the
	 * fastest ops and skip the benchmark.
	 */
	if (raidz_math_initialized) {
		goto init_done;
	}
#endif

	/* init scalar multiplication lookup table */
	raidz_init_scalar_mul_lt();

	/* move supported maths into raidz_supp_maths */
	for (i = 0, c = 0; i < impl_num; i++) {
		curr_impl = raidz_all_maths[i];

		/* init kstat */
		init_raidz_kstat(raidz_math_kstats + i, curr_impl->name);

		if (curr_impl->is_supported()) {
			raidz_supp_maths[c++] = (raidz_math_ops_t *) curr_impl;
		} else {
			bench_print(BENCH_SKIP_FMT, curr_impl->name);
			continue;
		}
	}
	raidz_supp_maths_cnt = c;	/* number of supported algos */

	/* init  kstat for original algo  routines */
	init_raidz_kstat(&(raidz_math_kstats[impl_num]), "original");

	/* Fake an zio and run the benchmark on it */
	bench_zio = kmem_alloc(sizeof (zio_t), KM_SLEEP);
	VERIFY(bench_zio);
	bzero(bench_zio, sizeof (zio_t));

	bench_zio->io_offset = 0;
	bench_zio->io_size = BENCH_ZIO_SIZE; /* only data columns */
	bench_zio->io_data = abd_alloc_scatter(BENCH_ZIO_SIZE);
	if (!bench_zio->io_data) {
		cmn_err(CE_WARN, "RAIDZ: Can not allocate benchmark memory!");
		/* use last supported math as the fastest */
		cmn_err(CE_WARN, "RAIDZ: Using [%s] implementation",
			raidz_supp_maths[raidz_supp_maths_cnt-1]->name);
		memcpy(&vdev_raidz_fastest_impl,
			raidz_supp_maths[raidz_supp_maths_cnt-1],
			sizeof (raidz_math_ops_t));

		kmem_free(bench_zio, sizeof (zio_t));
		return;
	}

	/*
	 * Benchmark new RAIDZ implementations
	 */

	/* Find the fastest gen functions */
	for (fn = 0; fn < RAIDZ_GEN_NUM; fn++) {
		bench_parity = fn+1;
		best_run_count = 0;

		/* New raidz_map is needed for rach generate_p/q/r */
		bench_rm = vdev_raidz_map_alloc(bench_zio, 9,
		    BENCH_D_COLS+bench_parity, bench_parity);

		for (impl = 0; impl < impl_num; impl++) {
			curr_impl = raidz_all_maths[impl];

			if (!curr_impl->is_supported()) {
				continue;
			}

			kpreempt_disable();

			run_count = 0;
			t_start = gethrtime();

			do {
				for (i = 0; i < 10; i++, run_count++) {
					curr_impl->gen[fn](bench_rm);
				}
			} while (gethrtime() < (t_start + BENCH_NS));

			speed = run_count * (bench_zio->io_size >> 10);
			speed /= (BENCH_D_COLS+bench_parity);
			speed *= 1000000000ULL;
			speed /= (gethrtime() - t_start);

			kpreempt_enable();

			raidz_math_kstats[impl].gen_kstat[fn].value.ui64 =
				speed;
			bench_print(BENCH_RES_FMT, raidz_gen_name[fn],
				curr_impl->name, speed/1000, speed%1000);

			if (run_count > best_run_count) {
				best_run_count = run_count;
				vdev_raidz_fastest_impl.gen[fn] =
					curr_impl->gen[fn];
			}
		}
		vdev_raidz_map_free(bench_rm);
	}

	/* Find the fastest rec functions */
	bench_rm = vdev_raidz_map_alloc(bench_zio, 9, BENCH_COLS, PARITY_PQR);

	for (fn = 0; fn < RAIDZ_REC_NUM; fn++) {
		best_run_count = 0;

		for (impl = 0; impl < impl_num; impl++) {
			curr_impl = raidz_all_maths[impl];

			if (!curr_impl->is_supported()) {
				continue;
			}

			run_count = 0;

			kpreempt_disable();
			t_start = gethrtime();

			do {
				for (i = 0; i < 100; i++, run_count++)
					curr_impl->rec[fn](bench_rm, tgtidx);
			} while (gethrtime() < (t_start + BENCH_NS));

			speed = run_count * (bench_zio->io_size >> 10);
			speed /= (BENCH_COLS);
			speed *= 1000000000ULL;
			speed /= (gethrtime() - t_start);

			kpreempt_enable();

			raidz_math_kstats[impl].rec_kstat[fn].value.ui64 =
				speed;
			bench_print(BENCH_RES_FMT, raidz_rec_name[fn],
				curr_impl->name, speed/1000, speed%1000);

			if (run_count > best_run_count) {
				best_run_count = run_count;
				vdev_raidz_fastest_impl.rec[fn] =
					curr_impl->rec[fn];
			}
		}
	}
	vdev_raidz_map_free(bench_rm);

	/*
	 * Benchmark original RAIDZ implementations
	 */

	/* Benchmark generate functions */

	for (fn = 0; fn < RAIDZ_GEN_NUM; fn++) {
		bench_parity = fn+1;
		bench_rm = vdev_raidz_map_alloc(bench_zio, 9,
			BENCH_D_COLS+bench_parity, bench_parity);
		bench_rm->rm_ops = NULL; /* force original math */

		kpreempt_disable();

		run_count = 0;
		t_start = gethrtime();

		do {
			for (i = 0; i < 10; i++, run_count++) {
				vdev_raidz_generate_parity(bench_rm);
			}
		} while (gethrtime() < (t_start + BENCH_NS));

		speed = run_count * (bench_zio->io_size >> 10);
		speed /= (BENCH_D_COLS+bench_parity);
		speed *= 1000000000ULL;
		speed /= (gethrtime() - t_start);

		kpreempt_enable();

		raidz_math_kstats[impl_num].gen_kstat[fn].value.ui64 = speed;
		bench_print(BENCH_RES_FMT, raidz_gen_name[fn], "original",
			speed/1000, speed%1000);
		vdev_raidz_map_free(bench_rm);
	}

	/* Benchmark reconstruction functions */
	bench_rm = vdev_raidz_map_alloc(bench_zio, 9, BENCH_COLS, PARITY_PQR);
	bench_rm->rm_ops = NULL; /* force original math */

	for (fn = 0; fn < RAIDZ_REC_NUM; fn++) {

		kpreempt_disable();

		run_count = 0;
		t_start = gethrtime();

		do {
			for (i = 0; i < 10; i++, run_count++) {
				vdev_raidz_reconstruct(bench_rm,
				    bench_rec_orig[fn], PARITY_PQR);
			}
		} while (gethrtime() < (t_start + BENCH_NS));

		speed = run_count * (bench_zio->io_size >> 10);
		speed /= (BENCH_COLS);
		speed *= 1000000000ULL;
		speed /= (gethrtime() - t_start);

		kpreempt_enable();

		raidz_math_kstats[impl_num].rec_kstat[fn].value.ui64 = speed;
		bench_print(BENCH_RES_FMT, raidz_rec_name[fn], "original",
			speed/1000, speed%1000);
	}
	vdev_raidz_map_free(bench_rm);

	/* Mark fastest ops as completed (use function from scalar math) */
	raidz_math_initialized = B_TRUE;
	vdev_raidz_fastest_impl.is_supported =
		raidz_supp_maths[0]->is_supported;


	/* cleanup the bench zio */
	abd_free(bench_zio->io_data, BENCH_ZIO_SIZE);
	kmem_free(bench_zio, sizeof (zio_t));

	/* install kstats for all algos */
	raidz_math_kstat = kstat_create("zfs", 0, "vdev_raidz_bench",
		"misc", KSTAT_TYPE_NAMED,
		sizeof (raidz_math_ops_kstat_t) / sizeof (kstat_named_t) *
		(impl_num+1), KSTAT_FLAG_VIRTUAL);
	if (raidz_math_kstat != NULL) {
		raidz_math_kstat->ks_data = raidz_math_kstats;
		kstat_install(raidz_math_kstat);
	}

#if !defined(_KERNEL)
init_done:
#endif

	/*
	 * For testing only!!!
	 * Use 'cycle' math selection method from userspace.
	 */
#if !defined(_KERNEL)
	VERIFY0(zfs_raidz_math_impl_set("cycle", NULL));
#endif
}

void
vdev_raidz_math_fini(void)
{
	if (raidz_math_kstat != NULL) {
		kstat_delete(raidz_math_kstat);
		raidz_math_kstat = NULL;
	}

	rw_destroy(&vdev_raidz_used_impl_lock);
}


static const char * math_impl_opt_names[] = {
	"fastest",
	"original",
	"cycle",
	"scalar",
	"sse",
	"avx2",
};

int
zfs_raidz_math_impl_set(const char *val, struct kernel_param *kp)
{
	size_t idx, i;
	raidz_math_ops_t *impl;
	const size_t val_len = strlen(val);
	boolean_t use_locks = raidz_math_initialized;
	int err = 0;

	for (idx = 0; idx < ARR_LEN(math_impl_opt_names); idx++) {
		if (0 == strncmp(val, math_impl_opt_names[idx], val_len)) {
			i = idx;
			break;
		}
	}
	if (idx >= ARR_LEN(math_impl_opt_names))
		return (-EINVAL);

	if (use_locks)
		rw_enter(&vdev_raidz_used_impl_lock, RW_WRITER);

	impl = vdev_raidz_used_impl;

	if (idx == 0) {
		impl = &vdev_raidz_fastest_impl;
		zfs_raidz_math_impl = IMPL_FASTEST;
	} else if (idx == 1) {
		impl = NULL;
		zfs_raidz_math_impl = IMPL_ORIGINAL;
	} else if (idx == 2) {
		impl = NULL;
		zfs_raidz_math_impl = IMPL_CYCLE;
	} else {
		for (idx = 0; idx < raidz_supp_maths_cnt; idx++) {
			if (0 == strncmp(raidz_supp_maths[idx]->name,
			    val, val_len)) {
				impl = raidz_supp_maths[idx];
				zfs_raidz_math_impl = idx;
				break;
			}
		}
		if (idx >= raidz_supp_maths_cnt) {
			err = -EINVAL;
		}
	}

	vdev_raidz_used_impl = impl;

	if (use_locks)
		rw_exit(&vdev_raidz_used_impl_lock);

	return (err);
}

#if defined(_KERNEL) && defined(HAVE_SPL)
static int
zfs_raidz_math_impl_get(char *buffer, struct kernel_param *kp)
{
	int i, cnt = 0;

	cnt += sprintf(buffer + cnt,
	    (zfs_raidz_math_impl == IMPL_FASTEST) ? "[%s] " : "%s ",
	    "fastest");
	cnt += sprintf(buffer + cnt,
	    (zfs_raidz_math_impl == IMPL_ORIGINAL) ? "[%s] " : "%s ",
	    "original");
	cnt += sprintf(buffer + cnt,
	    (zfs_raidz_math_impl == IMPL_CYCLE) ? "[%s] " : "%s ",
	    "cycle");

	for (i = 0; i < raidz_supp_maths_cnt; i++) {
		cnt += sprintf(buffer + cnt,
		    (zfs_raidz_math_impl == i) ? "[%s] " : "%s ",
		    raidz_supp_maths[i]->name);
	}

	return (cnt);
}

module_param_call(zfs_raidz_math_impl, zfs_raidz_math_impl_set,
	zfs_raidz_math_impl_get, NULL, 0644);
MODULE_PARM_DESC(zfs_raidz_math_impl, "Select raidz math to use");
#endif
