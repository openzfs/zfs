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
 * Copyright 2021 Jan Kasiak. All rights reserved.
 */

#ifndef	_ALG_IMPL_H
#define	_ALG_IMPL_H

/*
 * Common code for managing algorithm implementations.
 */

#ifdef	__cplusplus
extern "C" {
#endif

#include <sys/zfs_context.h>

/* A function that tests whether method will work. */
typedef boolean_t (*alg_impl_will_work_f)(void);

/* Maximum length of algorithm implementation name (including NULL). */
#define	ALG_IMPL_NAME_MAX (16)

/* Algorithm implementation operations and name. */
typedef struct alg_impl_ops {
	void *ctx; /* Algorithm implementation context. */

	alg_impl_will_work_f is_supported;

	/* Algorithm priorty. Higher is faster. Used if benchmark not set. */
	uint64_t priority;

	/* Name of implementation variant. */
	char name[ALG_IMPL_NAME_MAX];
} alg_impl_ops_t;

/* Fastest algorithm implementation. */
#define	ALG_IMPL_FASTEST (UINT32_MAX)

/* Cycle to next implementation. */
#define	ALG_IMPL_CYCLE (UINT32_MAX-1)

/*
 * A function that benchmarks a supported imlpementation.
 *
 * @param[in] ops
 * @param[in] buffer
 * @param     buffer_n size in bytes
 */
typedef void (*alg_impl_benchmark_f)(
	const alg_impl_ops_t *ops, void *buffer, size_t buffer_n);

/*
 * Algorithm implementation and benchmark bandwidth result.
 */
typedef struct {
	const alg_impl_ops_t *ops;
	uint64_t bandwidth;
} alg_impl_ops_bandwidth_t;

/*
 * Algorithm implementation configuration.
 */
typedef struct alg_impl_conf {
	const char *name; /* Name of algorithm. */

	const alg_impl_ops_t *const *available; /* Available implementations. */
	uint32_t available_n; /* Length of available. */

	const alg_impl_ops_t **supported; /* Supported implementations. */
	uint32_t supported_n; /* Length of supported, <= available_n. */

	uint32_t cycle_impl_idx; /* Index in case of "cycle" implementation. */

	/*
	 * Index into supported or one of ALG_IMPL_CYCLE or ALG_IMPL_FASTEST.
	 */
	uint32_t icp_alg_impl;

	/*
	 * User desired value of icp_alg_impl, before initialization finishes.
	 * Must be one of ALG_IMPL_FASTEST or ALG_IMPL_CYCLE.
	 */
	uint32_t user_sel_impl;

	alg_impl_benchmark_f benchmark; /* Benchmark function. */
	size_t benchmark_buffer_size;   /* Size of buffer for benchmarking. */
	alg_impl_ops_bandwidth_t *bandwidth; /* Benchmark results. */
	alg_impl_ops_bandwidth_t bandwidth_fastest; /* Fastest result. */

	alg_impl_ops_t fastest; /* Fastest implementation. */
	const alg_impl_ops_t *generic; /* Fallback generic implementation. */

	boolean_t initialized; /* Is configuration initilized. */

#if defined(_KERNEL)
	kstat_t *benchmark_kstat;
#endif
} alg_impl_conf_t;

/*
 * Delcare a alg_impl_ops_t struct.
 *
 * @param name_s     name of algorithm, example: "sha256"
 * @param avail      array of available implementations
 * @param supp       array of size size avail, initialized by alg_impl_init
 * @param gen        generic fallback implementations
 * @param bench      benchmark function (optional)
 * @param bench_size size of buffer to use for benchmark
 * @param bw         array of size avail, initialized by alg_impl_init
 */
#define	ALG_IMPL_CONF_DECL(name_s, avail, supp, gen, bench, bench_size, bw) \
	{ \
		.name = name_s, \
		.available = avail, \
		.available_n = ARRAY_SIZE(avail), \
		.supported = (const alg_impl_ops_t **)(supp), \
		.supported_n = 0, \
		.cycle_impl_idx = 0, \
		.icp_alg_impl = ALG_IMPL_FASTEST, \
		.user_sel_impl = ALG_IMPL_FASTEST, \
		.benchmark = bench, \
		.benchmark_buffer_size = bench_size, \
		.bandwidth = bw, \
		.bandwidth_fastest = {0}, \
		.fastest = {0}, \
		.generic = &(gen), \
		.initialized = 0, \
	}

/*
 * Initialize conf.
 *
 * The following must be already set:
 * - available
 * - available_n
 * - generic
 * - supported - points to an empty array of size available_n
 * - user_sel_impl
 *
 * @param[in,out] conf
 */
void alg_impl_init(alg_impl_conf_t *conf);

/*
 * Fini conf.
 *
 * @param[in,out] conf
 */
void alg_impl_fini(alg_impl_conf_t *conf);

/*
 * Get the selected and supported implementations.
 *
 * Uses sprintf and writes at most: N * (ALG_IMPL_NAME_MAX + 2) bytes to
 * buffer, where N is ARRAY_SIZE(alg_impl_opts) + conf->supported_n
 *
 * @param[in]     conf
 * @param[in,out] buffer
 *
 * @return number of bytes written
 */
int alg_impl_get(alg_impl_conf_t *conf, char *buffer);

/*
 * Set the desired implementation to use.
 *
 * @param[in,out] conf
 * @param[in]     val one of supported[i].name
 *
 * @retval 0 success
 * @retval -EINVAL unknown or unsupported implementation
 */
int alg_impl_set(alg_impl_conf_t *conf, const char *val);

/*
 * Get the implementation ops to use.
 *
 * - If !kfpu_allowed, then returns conf->generic
 * - If alg_impl_set was set to "cycle", then returns the next implementation.
 * - If alg_impl_set was set to any of supported[i].name, then returns that
 *   implementation.
 *
 * @param[in,out] conf
 *
 * @return one of conf->generic, conf->fastest, or conf->supported
 */
const alg_impl_ops_t *alg_impl_get_ops(alg_impl_conf_t *conf);

/*
 * Helper function that always returns true.
 *
 * @retval B_TRUE
 */
boolean_t alg_impl_will_always_work(void);

#ifdef	__cplusplus
}
#endif

#endif /* _ALG_IMPL_H */
