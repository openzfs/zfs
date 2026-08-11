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
 * Copyright (c) 2026, TrueNAS.
 */

#include <sys/zalgo.h>
#include <sys/spa_checksum.h>

/*
 * XXX userspace doesn't have kpreempt_disable(). however, we don't actually
 *     need to stop deschedules if we can detect and/or control for them.
 *     switching to CLOCK_THREAD_CPUTIME_ID instead of CLOCK_MONOTONIC (which
 *     gethrtime() is) is possibly enough, though need to check that it has
 *     ms granularity and not eg jiffy-quantised. extra credit, watch
 *     getrusage(RUSAGE_THREAD) -> ru_nivcsw for involuntary context switches;
 *     discard result and restart if we hit it. needs care; we don't want to
 *     spin forever in a hot environment.
 *       -- robn, 2026-08-11
 *
 *     implemented, needs cleanup, unclear if worth it but perhaps if only
 *     to suggest to reader that there's something here.
 *       -- robn, 2026-08-11
 *
 *     working well enough, but I worry it will retry indefinitely on a busy
 *     system. needs an upper limit on retry. also could try pinning the
 *     thread harder (SCHED_FIFO+PTHREAD_EXPLICIT_SCHED) but requires
 *     permissions and diminishing returns...
 *       -- robn, 2026-08-13
 */

static inline
uint64_t gethrtime_bench(void)
{
#ifdef _KERNEL
	return (gethrtime());
#else
	struct timespec ts;
	(void) clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts);
	return ((((uint64_t)ts.tv_sec) * NANOSEC) + ts.tv_nsec);
#endif
}

static inline
uint64_t getncs_bench(void)
{
#ifdef _KERNEL
	return (0);
#else
	struct rusage ru;
	getrusage(RUSAGE_THREAD, &ru);
	return (uint64_t)ru.ru_nivcsw;
#endif
}

static uint64_t
zalgo_bench_rounds(void (*fn)(void *), void *args)
{
	int rounds;

retry:
	rounds = 0;

	kpreempt_disable();
	uint64_t start_ncs = getncs_bench();

	uint64_t start = gethrtime_bench();
	do {
		fn(args);
		rounds++;
	} while (gethrtime_bench() - start < MSEC2NSEC(10));	/* 10ms */

	uint64_t end_ncs = getncs_bench();
	kpreempt_enable();

	if (start_ncs != end_ncs)
		/* preempted, retry */
		goto retry;

	return (rounds);
}

#define ZALGO_BENCH_DATA_LEN	(128*1024)

typedef struct {
	zalgo_checksum_hold_t *hold;
	void **ctxp;
	const uint8_t *data;
} zalgo_checksum_bench_args_t;

static void
zalgo_checksum_bench_cb(void *arg)
{
	zalgo_checksum_bench_args_t *cbarg = arg;
	zalgo_checksum_update(cbarg->hold, cbarg->ctxp, cbarg->data,
	    ZALGO_BENCH_DATA_LEN);
}

uint64_t
zalgo_checksum_bench(zalgo_checksum_hold_t *hold)
{
	uint8_t *data = vmem_alloc(ZALGO_BENCH_DATA_LEN, KM_SLEEP);
	if (data == NULL) {
		cmn_err(CE_NOTE, "%s: out of memory?", __FUNCTION__);
		return (0);
	}

	for (int i = 0; i < ZALGO_BENCH_DATA_LEN / sizeof (uint64_t); i++)
		((uint64_t *)data)[i] = (uintptr_t)(data+i);

	void *ctx;
	int err = zalgo_checksum_init(hold, &ctx);
	if (err != 0) {
		cmn_err(CE_NOTE, "%s: '%s' open failed; err=%d",
		    __FUNCTION__, zalgo_checksum_id(hold), err);
		vmem_free(data, ZALGO_BENCH_DATA_LEN);
		return (0);
	}

	zalgo_checksum_bench_args_t cbarg = {
		.hold = hold,
		.ctxp = &ctx,
		.data = data,
	};
	uint64_t rounds = zalgo_bench_rounds(zalgo_checksum_bench_cb, &cbarg);

	zio_cksum_t cksum;
	zalgo_checksum_final(hold, &ctx, &cksum);

	vmem_free(data, ZALGO_BENCH_DATA_LEN);

	return (rounds);
}

typedef struct {
	zalgo_cipher_hold_t *hold;
	void **ctxp;
	const uint8_t *plaintext;
	uint8_t *ciphertext;
	const uint8_t *iv;
} zalgo_cipher_bench_args_t;

static void
zalgo_cipher_bench_cb(void *arg)
{
	zalgo_cipher_bench_args_t *cbarg = arg;
	uint8_t mac[16];
	zalgo_cipher_encrypt(cbarg->hold, cbarg->ctxp, cbarg->plaintext,
	    cbarg->ciphertext, ZALGO_BENCH_DATA_LEN, cbarg->iv, NULL, 0, mac);
}

uint64_t
zalgo_cipher_bench(zalgo_cipher_hold_t *hold)
{
	uint8_t *plaintext = vmem_alloc(ZALGO_BENCH_DATA_LEN, KM_SLEEP);
	uint8_t *ciphertext = vmem_alloc(ZALGO_BENCH_DATA_LEN, KM_SLEEP);
	if (plaintext == NULL || ciphertext == NULL) {
		cmn_err(CE_NOTE, "%s: out of memory?", __FUNCTION__);
		if (plaintext != NULL)
			vmem_free(plaintext, ZALGO_BENCH_DATA_LEN);
		if (ciphertext != NULL)
			vmem_free(ciphertext, ZALGO_BENCH_DATA_LEN);
		return (0);
	}

	for (int i = 0; i < ZALGO_BENCH_DATA_LEN / sizeof (uint64_t); i++)
		((uint64_t *)plaintext)[i] = (uintptr_t)(plaintext+i);

	uint8_t key[32];
	for (int i = 0; i < sizeof (key); i++)
		key[i] = ((i & 0xf) << 4) | (i & 0xf);

	void *ctx;
	int err = zalgo_cipher_open(hold, &ctx, key, sizeof (key));
	if (err != 0) {
		cmn_err(CE_NOTE, "%s: '%s' open failed; err=%d",
		    __FUNCTION__, zalgo_cipher_id(hold), err);
		vmem_free(plaintext, ZALGO_BENCH_DATA_LEN);
		vmem_free(ciphertext, ZALGO_BENCH_DATA_LEN);
		return (0);
	}

	uint8_t iv[12];
	for (int i = 0; i < sizeof (iv); i++)
		iv[i] = 0x5a;

	zalgo_cipher_bench_args_t cbarg = {
		.hold = hold,
		.ctxp = &ctx,
		.plaintext = plaintext,
		.ciphertext = ciphertext,
		.iv = iv,
	};
	uint64_t rounds = zalgo_bench_rounds(zalgo_cipher_bench_cb, &cbarg);

	zalgo_cipher_close(hold, &ctx);

	vmem_free(plaintext, ZALGO_BENCH_DATA_LEN);
	vmem_free(ciphertext, ZALGO_BENCH_DATA_LEN);

	return (rounds);
}
