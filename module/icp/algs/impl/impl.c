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

/*
 * Based on aes_impl.c
 */

#include <sys/simd.h>
#include <impl/impl.h>

static const struct {
	const char *name;
	uint32_t sel;
} alg_impl_opts[] = {
	{ "cycle", ALG_IMPL_CYCLE },
	{ "fastest", ALG_IMPL_FASTEST },
};

#define	BENCHMARK_NS MSEC2NSEC(2)

#if defined(_KERNEL)

/*
 * Print benchmark stat headers.
 *
 * @param[out] buf
 * @param      size of buffer
 */
static int
impl_kstat_headers(char *buf, size_t size)
{
	ssize_t off = 0;

	off += snprintf(buf + off, size, "%-17s", "implementation");
	off += snprintf(buf + off, size - off, "%-15s\n", "bytes/second");

	return (0);
}

static int
impl_kstat_data(char *buf, size_t size, void *data)
{
	const alg_impl_ops_bandwidth_t *bw = data;

	ssize_t off = 0;

	off += snprintf(buf + off, size - off, "%-17s", bw->ops->name);
	off += snprintf(buf + off, size - off, "%-15llu\n",
	    (u_longlong_t)bw->bandwidth);

	return (0);
}

/*
 * Get the next kstat line data.
 *
 * @param[in,out] ksp
 * @param         n
 *
 * @retval NULL end of data
 * @return alg_impl_ops_bandwidth_t* from conf->supported
 */
static void *
impl_kstat_addr(kstat_t *ksp, loff_t n)
{
	alg_impl_conf_t *conf = ksp->ks_private;

	if (n == 0) {
		return (&conf->bandwidth_fastest);
	}

	if (n <= conf->supported_n) {
		return (void *)(conf->bandwidth + (n - 1));
	}

	return (NULL);
}
#endif

void
alg_impl_init(alg_impl_conf_t *conf)
{
	const alg_impl_ops_t *curr_impl, *fast_impl = conf->generic;
	size_t i, c;
	uint32_t max_priority = 0;

	/* Copy from available to supported */
	for (i = 0, c = 0; i < conf->available_n; i++) {
		curr_impl = conf->available[i];

		if (curr_impl->is_supported()) {
			conf->supported[c] = curr_impl;
			/* Ops for kstat, even if there is no benchmark. */
			conf->bandwidth[c].ops = curr_impl;
			c++;

			/* Keep track of fastest */
			if (curr_impl->priority > max_priority) {
				max_priority = curr_impl->priority;
				fast_impl = curr_impl;
			}
		}
	}
	conf->supported_n = c;

	/* Run benchmark. */
	if (conf->benchmark && kfpu_allowed()) {
		/* Reset fastest assumption. */
		uint64_t max_bw = 0;
		fast_impl = conf->generic;

		/* Allocate memory for benchmarking. */
		size_t buffer_size = conf->benchmark_buffer_size;
		uint8_t *buffer = vmem_alloc(buffer_size, KM_SLEEP);

		/* Warmup. */
		for (i = 0; i < conf->benchmark_buffer_size; i++) {
			buffer[i] = i % 255;
		}

		/* Loop over supported implementations. */
		for (i = 0; i < conf->supported_n; i++) {
			const alg_impl_ops_t *ops = conf->supported[i];
			uint64_t run_bw;
			uint64_t run_count = 0;
			uint64_t start, run_time_ns;

			kpreempt_disable();
			start = gethrtime();
			do {
				for (c = 0; c < 32; c++, run_count++) {
					conf->benchmark(
					    ops, buffer, buffer_size);
				}
				run_time_ns = gethrtime() - start;
			} while (run_time_ns < BENCHMARK_NS);
			kpreempt_enable();

			run_bw = buffer_size * run_count * NANOSEC;
			run_bw /= run_time_ns;	/* B/s */

			conf->bandwidth[i].bandwidth = run_bw;

			/* Keep track of fastest. */
			if (run_bw > max_bw) {
				max_bw = run_bw;
				fast_impl = ops;
			}
		}

		/* Save fastest result. */
		conf->bandwidth_fastest.bandwidth = max_bw;

		/* Free memory. */
		vmem_free(buffer, buffer_size);
	}

	/* Copy the fastest implementation. */
	memcpy(&conf->fastest, fast_impl, sizeof (*fast_impl));
	strlcpy(conf->fastest.name, "fastest", ALG_IMPL_NAME_MAX);
	conf->bandwidth_fastest.ops = &conf->fastest;

#if defined(_KERNEL)
	if (conf->benchmark) {
		/*
		 * Create name for kstat_create.
		 * It will be copied to ks_name.
		 */
		char kstat_name[KSTAT_STRLEN];
		snprintf(kstat_name, sizeof (kstat_name),
		    "%s_bench", conf->name);

		/* Create kstat. */
		conf->benchmark_kstat = kstat_create(
		    "zfs", 0, kstat_name, "misc",
		    KSTAT_TYPE_RAW, 0, KSTAT_FLAG_VIRTUAL);

		/* Install kstat. */
		if (conf->benchmark_kstat != NULL) {
			conf->benchmark_kstat->ks_data = NULL;
			conf->benchmark_kstat->ks_ndata = UINT32_MAX;
			conf->benchmark_kstat->ks_private = conf;
			kstat_set_raw_ops(conf->benchmark_kstat,
			    impl_kstat_headers,
			    impl_kstat_data,
			    impl_kstat_addr);
			kstat_install(conf->benchmark_kstat);
		}
	}
#endif

	/*
	 * Finish initialization. At this time, user_sel_impl can only be one of
	 * alg_impl_opts, because initialized is not yet set, so
	 * alg_impl_set would have returned EINVAL for any other value.
	 */
	conf->cycle_impl_idx = 0;
	atomic_swap_32(&conf->icp_alg_impl, conf->user_sel_impl);
	conf->initialized = B_TRUE;
}

void
alg_impl_fini(alg_impl_conf_t *conf)
{
#if defined(_KERNEL)
	if (conf->benchmark_kstat != NULL) {
		kstat_delete(conf->benchmark_kstat);
		conf->benchmark_kstat = NULL;
	}
#endif
}

int
alg_impl_get(alg_impl_conf_t *conf, char *buffer)
{
	int cnt = 0;
	size_t i = 0;
	const char *fmt = NULL;
	const uint32_t impl = atomic_load_32(&conf->icp_alg_impl);

	ASSERT(conf->initialized);

	/* First check mandatory options. */
	for (i = 0; i < ARRAY_SIZE(alg_impl_opts); i++) {
		fmt = (impl == alg_impl_opts[i].sel) ? "[%s] " : "%s ";
		cnt += sprintf(buffer + cnt, fmt, alg_impl_opts[i].name);
	}

	/* Then check all supported. */
	for (i = 0; i < conf->supported_n; i++) {
		fmt = (i == impl) ? "[%s] " : "%s ";
		cnt += sprintf(buffer + cnt, fmt, conf->supported[i]->name);
	}

	return (cnt);
}

int
alg_impl_set(alg_impl_conf_t *conf, const char *val)
{
	int err = -EINVAL;
	char req_name[ALG_IMPL_NAME_MAX];
	uint32_t impl = atomic_load_32(&conf->user_sel_impl);
	size_t i;

	/* Sanitize input. */
	i = strnlen(val, ALG_IMPL_NAME_MAX);
	if (i == 0 || i >= ALG_IMPL_NAME_MAX) {
		return (err);
	}

	/* Copy and NULL terminate. */
	strlcpy(req_name, val, ALG_IMPL_NAME_MAX);
	while (i > 0 && isspace(req_name[i-1])) {
		i--;
	}
	req_name[i] = '\0';

	/* First check mandatory options. */
	for (i = 0; i < ARRAY_SIZE(alg_impl_opts); i++) {
		if (strcmp(req_name, alg_impl_opts[i].name) == 0) {
			impl = alg_impl_opts[i].sel;
			err = 0;
			break;
		}
	}

	/* Then check all supported impl if init() was already called. */
	if (err != 0 && conf->initialized) {
		for (i = 0; i < conf->supported_n; i++) {
			if (strcmp(req_name, conf->supported[i]->name) == 0) {
				impl = i;
				err = 0;
				break;
			}
		}
	}

	/* If success, then set depending on configuration state. */
	if (err == 0) {
		if (conf->initialized) {
			atomic_swap_32(&conf->icp_alg_impl, impl);
		} else {
			atomic_swap_32(&conf->user_sel_impl, impl);
		}
	}

	return (err);
}

const alg_impl_ops_t *
alg_impl_get_ops(alg_impl_conf_t *conf)
{
	const alg_impl_ops_t *ops = conf->generic;

	/* Check if we are not allowed to use faster versions. */
	if (!kfpu_allowed()) {
		return (ops);
	}

	const uint32_t impl = atomic_load_32(&conf->icp_alg_impl);

	switch (impl) {
	case ALG_IMPL_FASTEST:
		ASSERT(conf->initialized);
		ops = &conf->fastest;
		break;
	case ALG_IMPL_CYCLE:
		/* Cycle through supported implementations */
		ASSERT(conf->initialized);
		ASSERT3U(conf->supported_n, >, 0);

		size_t idx = (++conf->cycle_impl_idx) % conf->supported_n;
		ops = conf->supported[idx];
		break;
	default:
		/* Use a specific implementation. */
		ASSERT3U(impl, <, conf->supported_n);
		ASSERT3U(conf->supported_n, >, 0);
		if (impl < conf->supported_n) {
			ops = conf->supported[impl];
		}
		break;
	}

	ASSERT3P(ops, !=, NULL);

	return (ops);
}

boolean_t
alg_impl_will_always_work(void)
{
	return (B_TRUE);
}
