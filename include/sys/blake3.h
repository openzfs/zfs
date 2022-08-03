/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or https://opensource.org/licenses/CDDL-1.0.
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
 * Based on BLAKE3 v1.3.1, https://github.com/BLAKE3-team/BLAKE3
 * Copyright (c) 2019-2020 Samuel Neves and Jack O'Connor
 * Copyright (c) 2021 Tino Reichardt <milky-zfs@mcmilk.de>
 */

#ifndef BLAKE3_H
#define	BLAKE3_H

#ifdef  _KERNEL
#include <sys/types.h>
#else
#include <stdint.h>
#include <stdlib.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define	BLAKE3_KEY_LEN		32
#define	BLAKE3_OUT_LEN		32
#define	BLAKE3_MAX_DEPTH	54
#define	BLAKE3_BLOCK_LEN	64
#define	BLAKE3_CHUNK_LEN	1024

/*
 * This struct is a private implementation detail.
 * It has to be here because it's part of BLAKE3_CTX below.
 */
typedef struct {
	uint32_t cv[8];
	uint64_t chunk_counter;
	uint8_t buf[BLAKE3_BLOCK_LEN];
	uint8_t buf_len;
	uint8_t blocks_compressed;
	uint8_t flags;
} blake3_chunk_state_t;

typedef struct {
	uint32_t key[8];
	blake3_chunk_state_t chunk;
	uint8_t cv_stack_len;

	/*
	 * The stack size is MAX_DEPTH + 1 because we do lazy merging. For
	 * example, with 7 chunks, we have 3 entries in the stack. Adding an
	 * 8th chunk requires a 4th entry, rather than merging everything down
	 * to 1, because we don't know whether more input is coming. This is
	 * different from how the reference implementation does things.
	 */
	uint8_t cv_stack[(BLAKE3_MAX_DEPTH + 1) * BLAKE3_OUT_LEN];

	/* const blake3_ops_t *ops */
	const void *ops;
} BLAKE3_CTX;

/* init the context for hash operation */
void Blake3_Init(BLAKE3_CTX *ctx);

/* init the context for a MAC and/or tree hash operation */
void Blake3_InitKeyed(BLAKE3_CTX *ctx, const uint8_t key[BLAKE3_KEY_LEN]);

/* process the input bytes */
void Blake3_Update(BLAKE3_CTX *ctx, const void *input, size_t input_len);

/* finalize the hash computation and output the result */
void Blake3_Final(const BLAKE3_CTX *ctx, uint8_t *out);

/* finalize the hash computation and output the result */
void Blake3_FinalSeek(const BLAKE3_CTX *ctx, uint64_t seek, uint8_t *out,
    size_t out_len);

/* these are pre-allocated contexts */
extern void **blake3_per_cpu_ctx;
extern void blake3_per_cpu_ctx_init(void);
extern void blake3_per_cpu_ctx_fini(void);

/* get count of supported implementations */
extern uint32_t blake3_impl_getcnt(void);

/* get id of selected implementation */
extern uint32_t blake3_impl_getid(void);

/* get name of selected implementation */
extern const char *blake3_impl_getname(void);

/* setup id as fastest implementation */
extern void blake3_impl_set_fastest(uint32_t id);

/* set implementation by id */
extern void blake3_impl_setid(uint32_t id);

/* set implementation by name */
extern int blake3_impl_setname(const char *name);

#ifdef __cplusplus
}
#endif

#endif	/* BLAKE3_H */
