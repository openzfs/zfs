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
 * Copyright (c) 2021-2022 Tino Reichardt <milky-zfs@mcmilk.de>
 */

#include <sys/zfs_context.h>
#include <sys/blake3.h>

#include "blake3_impl.h"

/*
 * We need 1056 byte stack for blake3_compress_subtree_wide()
 * - we define this pragma to make gcc happy
 */
#if defined(__GNUC__)
#pragma GCC diagnostic ignored "-Wframe-larger-than="
#endif

/* internal used */
typedef struct {
	uint32_t input_cv[8];
	uint64_t counter;
	uint8_t block[BLAKE3_BLOCK_LEN];
	uint8_t block_len;
	uint8_t flags;
} output_t;

/* internal flags */
enum blake3_flags {
	CHUNK_START		= 1 << 0,
	CHUNK_END		= 1 << 1,
	PARENT			= 1 << 2,
	ROOT			= 1 << 3,
	KEYED_HASH		= 1 << 4,
	DERIVE_KEY_CONTEXT	= 1 << 5,
	DERIVE_KEY_MATERIAL	= 1 << 6,
};

/* internal start */
static void chunk_state_init(blake3_chunk_state_t *ctx,
    const uint32_t key[8], uint8_t flags)
{
	memcpy(ctx->cv, key, BLAKE3_KEY_LEN);
	ctx->chunk_counter = 0;
	memset(ctx->buf, 0, BLAKE3_BLOCK_LEN);
	ctx->buf_len = 0;
	ctx->blocks_compressed = 0;
	ctx->flags = flags;
}

static void chunk_state_reset(blake3_chunk_state_t *ctx,
    const uint32_t key[8], uint64_t chunk_counter)
{
	memcpy(ctx->cv, key, BLAKE3_KEY_LEN);
	ctx->chunk_counter = chunk_counter;
	ctx->blocks_compressed = 0;
	memset(ctx->buf, 0, BLAKE3_BLOCK_LEN);
	ctx->buf_len = 0;
}

static size_t chunk_state_len(const blake3_chunk_state_t *ctx)
{
	return (BLAKE3_BLOCK_LEN * (size_t)ctx->blocks_compressed) +
	    ((size_t)ctx->buf_len);
}

static size_t chunk_state_fill_buf(blake3_chunk_state_t *ctx,
    const uint8_t *input, size_t input_len)
{
	size_t take = BLAKE3_BLOCK_LEN - ((size_t)ctx->buf_len);
	if (take > input_len) {
		take = input_len;
	}
	uint8_t *dest = ctx->buf + ((size_t)ctx->buf_len);
	memcpy(dest, input, take);
	ctx->buf_len += (uint8_t)take;
	return (take);
}

static uint8_t chunk_state_maybe_start_flag(const blake3_chunk_state_t *ctx)
{
	if (ctx->blocks_compressed == 0) {
		return (CHUNK_START);
	} else {
		return (0);
	}
}

static output_t make_output(const uint32_t input_cv[8],
    const uint8_t *block, uint8_t block_len,
    uint64_t counter, uint8_t flags)
{
	output_t ret;
	memcpy(ret.input_cv, input_cv, 32);
	memcpy(ret.block, block, BLAKE3_BLOCK_LEN);
	ret.block_len = block_len;
	ret.counter = counter;
	ret.flags = flags;
	return (ret);
}

/*
 * Chaining values within a given chunk (specifically the compress_in_place
 * interface) are represented as words. This avoids unnecessary bytes<->words
 * conversion overhead in the portable implementation. However, the hash_many
 * interface handles both user input and parent node blocks, so it accepts
 * bytes. For that reason, chaining values in the CV stack are represented as
 * bytes.
 */
static void output_chaining_value(const blake3_ops_t *ops,
    const output_t *ctx, uint8_t cv[32])
{
	uint32_t cv_words[8];
	memcpy(cv_words, ctx->input_cv, 32);
	ops->compress_in_place(cv_words, ctx->block, ctx->block_len,
	    ctx->counter, ctx->flags);
	store_cv_words(cv, cv_words);
}

static void output_root_bytes(const blake3_ops_t *ops, const output_t *ctx,
    uint64_t seek, uint8_t *out, size_t out_len)
{
	uint64_t output_block_counter = seek / 64;
	size_t offset_within_block = seek % 64;
	uint8_t wide_buf[64];
	while (out_len > 0) {
		ops->compress_xof(ctx->input_cv, ctx->block, ctx->block_len,
		    output_block_counter, ctx->flags | ROOT, wide_buf);
		size_t available_bytes = 64 - offset_within_block;
		size_t memcpy_len;
		if (out_len > available_bytes) {
			memcpy_len = available_bytes;
		} else {
			memcpy_len = out_len;
		}
		memcpy(out, wide_buf + offset_within_block, memcpy_len);
		out += memcpy_len;
		out_len -= memcpy_len;
		output_block_counter += 1;
		offset_within_block = 0;
	}
}

static void chunk_state_update(const blake3_ops_t *ops,
    blake3_chunk_state_t *ctx, const uint8_t *input, size_t input_len)
{
	if (ctx->buf_len > 0) {
		size_t take = chunk_state_fill_buf(ctx, input, input_len);
		input += take;
		input_len -= take;
		if (input_len > 0) {
			ops->compress_in_place(ctx->cv, ctx->buf,
			    BLAKE3_BLOCK_LEN, ctx->chunk_counter,
			    ctx->flags|chunk_state_maybe_start_flag(ctx));
			ctx->blocks_compressed += 1;
			ctx->buf_len = 0;
			memset(ctx->buf, 0, BLAKE3_BLOCK_LEN);
		}
	}

	while (input_len > BLAKE3_BLOCK_LEN) {
		ops->compress_in_place(ctx->cv, input, BLAKE3_BLOCK_LEN,
		    ctx->chunk_counter,
		    ctx->flags|chunk_state_maybe_start_flag(ctx));
		ctx->blocks_compressed += 1;
		input += BLAKE3_BLOCK_LEN;
		input_len -= BLAKE3_BLOCK_LEN;
	}

	chunk_state_fill_buf(ctx, input, input_len);
}

static output_t chunk_state_output(const blake3_chunk_state_t *ctx)
{
	uint8_t block_flags =
	    ctx->flags | chunk_state_maybe_start_flag(ctx) | CHUNK_END;
	return (make_output(ctx->cv, ctx->buf, ctx->buf_len, ctx->chunk_counter,
	    block_flags));
}

static output_t parent_output(const uint8_t block[BLAKE3_BLOCK_LEN],
    const uint32_t key[8], uint8_t flags)
{
	return (make_output(key, block, BLAKE3_BLOCK_LEN, 0, flags | PARENT));
}

/*
 * Given some input larger than one chunk, return the number of bytes that
 * should go in the left subtree. This is the largest power-of-2 number of
 * chunks that leaves at least 1 byte for the right subtree.
 */
static size_t left_len(size_t content_len)
{
	/*
	 * Subtract 1 to reserve at least one byte for the right side.
	 * content_len
	 * should always be greater than BLAKE3_CHUNK_LEN.
	 */
	size_t full_chunks = (content_len - 1) / BLAKE3_CHUNK_LEN;
	return (round_down_to_power_of_2(full_chunks) * BLAKE3_CHUNK_LEN);
}

/*
 * Use SIMD parallelism to hash up to MAX_SIMD_DEGREE chunks at the same time
 * on a single thread. Write out the chunk chaining values and return the
 * number of chunks hashed. These chunks are never the root and never empty;
 * those cases use a different codepath.
 */
static size_t compress_chunks_parallel(const blake3_ops_t *ops,
    const uint8_t *input, size_t input_len, const uint32_t key[8],
    uint64_t chunk_counter, uint8_t flags, uint8_t *out)
{
	const uint8_t *chunks_array[MAX_SIMD_DEGREE];
	size_t input_position = 0;
	size_t chunks_array_len = 0;
	while (input_len - input_position >= BLAKE3_CHUNK_LEN) {
		chunks_array[chunks_array_len] = &input[input_position];
		input_position += BLAKE3_CHUNK_LEN;
		chunks_array_len += 1;
	}

	ops->hash_many(chunks_array, chunks_array_len, BLAKE3_CHUNK_LEN /
	    BLAKE3_BLOCK_LEN, key, chunk_counter, B_TRUE, flags, CHUNK_START,
	    CHUNK_END, out);

	/*
	 * Hash the remaining partial chunk, if there is one. Note that the
	 * empty chunk (meaning the empty message) is a different codepath.
	 */
	if (input_len > input_position) {
		uint64_t counter = chunk_counter + (uint64_t)chunks_array_len;
		blake3_chunk_state_t chunk_state;
		chunk_state_init(&chunk_state, key, flags);
		chunk_state.chunk_counter = counter;
		chunk_state_update(ops, &chunk_state, &input[input_position],
		    input_len - input_position);
		output_t output = chunk_state_output(&chunk_state);
		output_chaining_value(ops, &output, &out[chunks_array_len *
		    BLAKE3_OUT_LEN]);
		return (chunks_array_len + 1);
	} else {
		return (chunks_array_len);
	}
}

/*
 * Use SIMD parallelism to hash up to MAX_SIMD_DEGREE parents at the same time
 * on a single thread. Write out the parent chaining values and return the
 * number of parents hashed. (If there's an odd input chaining value left over,
 * return it as an additional output.) These parents are never the root and
 * never empty; those cases use a different codepath.
 */
static size_t compress_parents_parallel(const blake3_ops_t *ops,
    const uint8_t *child_chaining_values, size_t num_chaining_values,
    const uint32_t key[8], uint8_t flags, uint8_t *out)
{
	const uint8_t *parents_array[MAX_SIMD_DEGREE_OR_2];
	size_t parents_array_len = 0;

	while (num_chaining_values - (2 * parents_array_len) >= 2) {
		parents_array[parents_array_len] = &child_chaining_values[2 *
		    parents_array_len * BLAKE3_OUT_LEN];
		parents_array_len += 1;
	}

	ops->hash_many(parents_array, parents_array_len, 1, key, 0, B_FALSE,
	    flags | PARENT, 0, 0, out);

	/* If there's an odd child left over, it becomes an output. */
	if (num_chaining_values > 2 * parents_array_len) {
		memcpy(&out[parents_array_len * BLAKE3_OUT_LEN],
		    &child_chaining_values[2 * parents_array_len *
		    BLAKE3_OUT_LEN], BLAKE3_OUT_LEN);
		return (parents_array_len + 1);
	} else {
		return (parents_array_len);
	}
}

/*
 * The wide helper function returns (writes out) an array of chaining values
 * and returns the length of that array. The number of chaining values returned
 * is the dyanmically detected SIMD degree, at most MAX_SIMD_DEGREE. Or fewer,
 * if the input is shorter than that many chunks. The reason for maintaining a
 * wide array of chaining values going back up the tree, is to allow the
 * implementation to hash as many parents in parallel as possible.
 *
 * As a special case when the SIMD degree is 1, this function will still return
 * at least 2 outputs. This guarantees that this function doesn't perform the
 * root compression. (If it did, it would use the wrong flags, and also we
 * wouldn't be able to implement exendable ouput.) Note that this function is
 * not used when the whole input is only 1 chunk long; that's a different
 * codepath.
 *
 * Why not just have the caller split the input on the first update(), instead
 * of implementing this special rule? Because we don't want to limit SIMD or
 * multi-threading parallelism for that update().
 */
static size_t blake3_compress_subtree_wide(const blake3_ops_t *ops,
    const uint8_t *input, size_t input_len, const uint32_t key[8],
    uint64_t chunk_counter, uint8_t flags, uint8_t *out)
{
	/*
	 * Note that the single chunk case does *not* bump the SIMD degree up
	 * to 2 when it is 1. If this implementation adds multi-threading in
	 * the future, this gives us the option of multi-threading even the
	 * 2-chunk case, which can help performance on smaller platforms.
	 */
	if (input_len <= (size_t)(ops->degree * BLAKE3_CHUNK_LEN)) {
		return (compress_chunks_parallel(ops, input, input_len, key,
		    chunk_counter, flags, out));
	}


	/*
	 * With more than simd_degree chunks, we need to recurse. Start by
	 * dividing the input into left and right subtrees. (Note that this is
	 * only optimal as long as the SIMD degree is a power of 2. If we ever
	 * get a SIMD degree of 3 or something, we'll need a more complicated
	 * strategy.)
	 */
	size_t left_input_len = left_len(input_len);
	size_t right_input_len = input_len - left_input_len;
	const uint8_t *right_input = &input[left_input_len];
	uint64_t right_chunk_counter = chunk_counter +
	    (uint64_t)(left_input_len / BLAKE3_CHUNK_LEN);

	/*
	 * Make space for the child outputs. Here we use MAX_SIMD_DEGREE_OR_2
	 * to account for the special case of returning 2 outputs when the
	 * SIMD degree is 1.
	 */
	uint8_t cv_array[2 * MAX_SIMD_DEGREE_OR_2 * BLAKE3_OUT_LEN];
	size_t degree = ops->degree;
	if (left_input_len > BLAKE3_CHUNK_LEN && degree == 1) {

		/*
		 * The special case: We always use a degree of at least two,
		 * to make sure there are two outputs. Except, as noted above,
		 * at the chunk level, where we allow degree=1. (Note that the
		 * 1-chunk-input case is a different codepath.)
		 */
		degree = 2;
	}
	uint8_t *right_cvs = &cv_array[degree * BLAKE3_OUT_LEN];

	/*
	 * Recurse! If this implementation adds multi-threading support in the
	 * future, this is where it will go.
	 */
	size_t left_n = blake3_compress_subtree_wide(ops, input, left_input_len,
	    key, chunk_counter, flags, cv_array);
	size_t right_n = blake3_compress_subtree_wide(ops, right_input,
	    right_input_len, key, right_chunk_counter, flags, right_cvs);

	/*
	 * The special case again. If simd_degree=1, then we'll have left_n=1
	 * and right_n=1. Rather than compressing them into a single output,
	 * return them directly, to make sure we always have at least two
	 * outputs.
	 */
	if (left_n == 1) {
		memcpy(out, cv_array, 2 * BLAKE3_OUT_LEN);
		return (2);
	}

	/* Otherwise, do one layer of parent node compression. */
	size_t num_chaining_values = left_n + right_n;
	return compress_parents_parallel(ops, cv_array,
	    num_chaining_values, key, flags, out);
}

/*
 * Hash a subtree with compress_subtree_wide(), and then condense the resulting
 * list of chaining values down to a single parent node. Don't compress that
 * last parent node, however. Instead, return its message bytes (the
 * concatenated chaining values of its children). This is necessary when the
 * first call to update() supplies a complete subtree, because the topmost
 * parent node of that subtree could end up being the root. It's also necessary
 * for extended output in the general case.
 *
 * As with compress_subtree_wide(), this function is not used on inputs of 1
 * chunk or less. That's a different codepath.
 */
static void compress_subtree_to_parent_node(const blake3_ops_t *ops,
    const uint8_t *input, size_t input_len, const uint32_t key[8],
    uint64_t chunk_counter, uint8_t flags, uint8_t out[2 * BLAKE3_OUT_LEN])
{
	uint8_t cv_array[MAX_SIMD_DEGREE_OR_2 * BLAKE3_OUT_LEN];
	size_t num_cvs = blake3_compress_subtree_wide(ops, input, input_len,
	    key, chunk_counter, flags, cv_array);

	/*
	 * If MAX_SIMD_DEGREE is greater than 2 and there's enough input,
	 * compress_subtree_wide() returns more than 2 chaining values. Condense
	 * them into 2 by forming parent nodes repeatedly.
	 */
	uint8_t out_array[MAX_SIMD_DEGREE_OR_2 * BLAKE3_OUT_LEN / 2];
	while (num_cvs > 2) {
		num_cvs = compress_parents_parallel(ops, cv_array, num_cvs, key,
		    flags, out_array);
		memcpy(cv_array, out_array, num_cvs * BLAKE3_OUT_LEN);
	}
	memcpy(out, cv_array, 2 * BLAKE3_OUT_LEN);
}

static void hasher_init_base(BLAKE3_CTX *ctx, const uint32_t key[8],
    uint8_t flags)
{
	memcpy(ctx->key, key, BLAKE3_KEY_LEN);
	chunk_state_init(&ctx->chunk, key, flags);
	ctx->cv_stack_len = 0;
	ctx->ops = blake3_impl_get_ops();
}

/*
 * As described in hasher_push_cv() below, we do "lazy merging", delaying
 * merges until right before the next CV is about to be added. This is
 * different from the reference implementation. Another difference is that we
 * aren't always merging 1 chunk at a time. Instead, each CV might represent
 * any power-of-two number of chunks, as long as the smaller-above-larger
 * stack order is maintained. Instead of the "count the trailing 0-bits"
 * algorithm described in the spec, we use a "count the total number of
 * 1-bits" variant that doesn't require us to retain the subtree size of the
 * CV on top of the stack. The principle is the same: each CV that should
 * remain in the stack is represented by a 1-bit in the total number of chunks
 * (or bytes) so far.
 */
static void hasher_merge_cv_stack(BLAKE3_CTX *ctx, uint64_t total_len)
{
	size_t post_merge_stack_len = (size_t)popcnt(total_len);
	while (ctx->cv_stack_len > post_merge_stack_len) {
		uint8_t *parent_node =
		    &ctx->cv_stack[(ctx->cv_stack_len - 2) * BLAKE3_OUT_LEN];
		output_t output =
		    parent_output(parent_node, ctx->key, ctx->chunk.flags);
		output_chaining_value(ctx->ops, &output, parent_node);
		ctx->cv_stack_len -= 1;
	}
}

/*
 * In reference_impl.rs, we merge the new CV with existing CVs from the stack
 * before pushing it. We can do that because we know more input is coming, so
 * we know none of the merges are root.
 *
 * This setting is different. We want to feed as much input as possible to
 * compress_subtree_wide(), without setting aside anything for the chunk_state.
 * If the user gives us 64 KiB, we want to parallelize over all 64 KiB at once
 * as a single subtree, if at all possible.
 *
 * This leads to two problems:
 * 1) This 64 KiB input might be the only call that ever gets made to update.
 *    In this case, the root node of the 64 KiB subtree would be the root node
 *    of the whole tree, and it would need to be ROOT finalized. We can't
 *    compress it until we know.
 * 2) This 64 KiB input might complete a larger tree, whose root node is
 *    similarly going to be the the root of the whole tree. For example, maybe
 *    we have 196 KiB (that is, 128 + 64) hashed so far. We can't compress the
 *    node at the root of the 256 KiB subtree until we know how to finalize it.
 *
 * The second problem is solved with "lazy merging". That is, when we're about
 * to add a CV to the stack, we don't merge it with anything first, as the
 * reference impl does. Instead we do merges using the *previous* CV that was
 * added, which is sitting on top of the stack, and we put the new CV
 * (unmerged) on top of the stack afterwards. This guarantees that we never
 * merge the root node until finalize().
 *
 * Solving the first problem requires an additional tool,
 * compress_subtree_to_parent_node(). That function always returns the top
 * *two* chaining values of the subtree it's compressing. We then do lazy
 * merging with each of them separately, so that the second CV will always
 * remain unmerged. (That also helps us support extendable output when we're
 * hashing an input all-at-once.)
 */
static void hasher_push_cv(BLAKE3_CTX *ctx, uint8_t new_cv[BLAKE3_OUT_LEN],
    uint64_t chunk_counter)
{
	hasher_merge_cv_stack(ctx, chunk_counter);
	memcpy(&ctx->cv_stack[ctx->cv_stack_len * BLAKE3_OUT_LEN], new_cv,
	    BLAKE3_OUT_LEN);
	ctx->cv_stack_len += 1;
}

void
Blake3_Init(BLAKE3_CTX *ctx)
{
	hasher_init_base(ctx, BLAKE3_IV, 0);
}

void
Blake3_InitKeyed(BLAKE3_CTX *ctx, const uint8_t key[BLAKE3_KEY_LEN])
{
	uint32_t key_words[8];
	load_key_words(key, key_words);
	hasher_init_base(ctx, key_words, KEYED_HASH);
}

static void
Blake3_Update2(BLAKE3_CTX *ctx, const void *input, size_t input_len)
{
	/*
	 * Explicitly checking for zero avoids causing UB by passing a null
	 * pointer to memcpy. This comes up in practice with things like:
	 *   std::vector<uint8_t> v;
	 *   blake3_hasher_update(&hasher, v.data(), v.size());
	 */
	if (input_len == 0) {
		return;
	}

	const uint8_t *input_bytes = (const uint8_t *)input;

	/*
	 * If we have some partial chunk bytes in the internal chunk_state, we
	 * need to finish that chunk first.
	 */
	if (chunk_state_len(&ctx->chunk) > 0) {
		size_t take = BLAKE3_CHUNK_LEN - chunk_state_len(&ctx->chunk);
		if (take > input_len) {
			take = input_len;
		}
		chunk_state_update(ctx->ops, &ctx->chunk, input_bytes, take);
		input_bytes += take;
		input_len -= take;
		/*
		 * If we've filled the current chunk and there's more coming,
		 * finalize this chunk and proceed. In this case we know it's
		 * not the root.
		 */
		if (input_len > 0) {
			output_t output = chunk_state_output(&ctx->chunk);
			uint8_t chunk_cv[32];
			output_chaining_value(ctx->ops, &output, chunk_cv);
			hasher_push_cv(ctx, chunk_cv, ctx->chunk.chunk_counter);
			chunk_state_reset(&ctx->chunk, ctx->key,
			    ctx->chunk.chunk_counter + 1);
		} else {
			return;
		}
	}

	/*
	 * Now the chunk_state is clear, and we have more input. If there's
	 * more than a single chunk (so, definitely not the root chunk), hash
	 * the largest whole subtree we can, with the full benefits of SIMD
	 * (and maybe in the future, multi-threading) parallelism. Two
	 * restrictions:
	 * - The subtree has to be a power-of-2 number of chunks. Only
	 *   subtrees along the right edge can be incomplete, and we don't know
	 *   where the right edge is going to be until we get to finalize().
	 * - The subtree must evenly divide the total number of chunks up
	 *   until this point (if total is not 0). If the current incomplete
	 *   subtree is only waiting for 1 more chunk, we can't hash a subtree
	 *   of 4 chunks. We have to complete the current subtree first.
	 * Because we might need to break up the input to form powers of 2, or
	 * to evenly divide what we already have, this part runs in a loop.
	 */
	while (input_len > BLAKE3_CHUNK_LEN) {
		size_t subtree_len = round_down_to_power_of_2(input_len);
		uint64_t count_so_far =
		    ctx->chunk.chunk_counter * BLAKE3_CHUNK_LEN;
		/*
		 * Shrink the subtree_len until it evenly divides the count so
		 * far. We know that subtree_len itself is a power of 2, so we
		 * can use a bitmasking trick instead of an actual remainder
		 * operation. (Note that if the caller consistently passes
		 * power-of-2 inputs of the same size, as is hopefully
		 * typical, this loop condition will always fail, and
		 * subtree_len will always be the full length of the input.)
		 *
		 * An aside: We don't have to shrink subtree_len quite this
		 * much. For example, if count_so_far is 1, we could pass 2
		 * chunks to compress_subtree_to_parent_node. Since we'll get
		 * 2 CVs back, we'll still get the right answer in the end,
		 * and we might get to use 2-way SIMD parallelism. The problem
		 * with this optimization, is that it gets us stuck always
		 * hashing 2 chunks. The total number of chunks will remain
		 * odd, and we'll never graduate to higher degrees of
		 * parallelism. See
		 * https://github.com/BLAKE3-team/BLAKE3/issues/69.
		 */
		while ((((uint64_t)(subtree_len - 1)) & count_so_far) != 0) {
			subtree_len /= 2;
		}
		/*
		 * The shrunken subtree_len might now be 1 chunk long. If so,
		 * hash that one chunk by itself. Otherwise, compress the
		 * subtree into a pair of CVs.
		 */
		uint64_t subtree_chunks = subtree_len / BLAKE3_CHUNK_LEN;
		if (subtree_len <= BLAKE3_CHUNK_LEN) {
			blake3_chunk_state_t chunk_state;
			chunk_state_init(&chunk_state, ctx->key,
			    ctx->chunk.flags);
			chunk_state.chunk_counter = ctx->chunk.chunk_counter;
			chunk_state_update(ctx->ops, &chunk_state, input_bytes,
			    subtree_len);
			output_t output = chunk_state_output(&chunk_state);
			uint8_t cv[BLAKE3_OUT_LEN];
			output_chaining_value(ctx->ops, &output, cv);
			hasher_push_cv(ctx, cv, chunk_state.chunk_counter);
		} else {
			/*
			 * This is the high-performance happy path, though
			 * getting here depends on the caller giving us a long
			 * enough input.
			 */
			uint8_t cv_pair[2 * BLAKE3_OUT_LEN];
			compress_subtree_to_parent_node(ctx->ops, input_bytes,
			    subtree_len, ctx->key, ctx-> chunk.chunk_counter,
			    ctx->chunk.flags, cv_pair);
			hasher_push_cv(ctx, cv_pair, ctx->chunk.chunk_counter);
			hasher_push_cv(ctx, &cv_pair[BLAKE3_OUT_LEN],
			    ctx->chunk.chunk_counter + (subtree_chunks / 2));
		}
		ctx->chunk.chunk_counter += subtree_chunks;
		input_bytes += subtree_len;
		input_len -= subtree_len;
	}

	/*
	 * If there's any remaining input less than a full chunk, add it to
	 * the chunk state. In that case, also do a final merge loop to make
	 * sure the subtree stack doesn't contain any unmerged pairs. The
	 * remaining input means we know these merges are non-root. This merge
	 * loop isn't strictly necessary here, because hasher_push_chunk_cv
	 * already does its own merge loop, but it simplifies
	 * blake3_hasher_finalize below.
	 */
	if (input_len > 0) {
		chunk_state_update(ctx->ops, &ctx->chunk, input_bytes,
		    input_len);
		hasher_merge_cv_stack(ctx, ctx->chunk.chunk_counter);
	}
}

void
Blake3_Update(BLAKE3_CTX *ctx, const void *input, size_t todo)
{
	size_t done = 0;
	const uint8_t *data = input;
	const size_t block_max = 1024 * 64;

	/* max feed buffer to leave the stack size small */
	while (todo != 0) {
		size_t block = (todo >= block_max) ? block_max : todo;
		Blake3_Update2(ctx, data + done, block);
		done += block;
		todo -= block;
	}
}

void
Blake3_Final(const BLAKE3_CTX *ctx, uint8_t *out)
{
	Blake3_FinalSeek(ctx, 0, out, BLAKE3_OUT_LEN);
}

void
Blake3_FinalSeek(const BLAKE3_CTX *ctx, uint64_t seek, uint8_t *out,
    size_t out_len)
{
	/*
	 * Explicitly checking for zero avoids causing UB by passing a null
	 * pointer to memcpy. This comes up in practice with things like:
	 *   std::vector<uint8_t> v;
	 *   blake3_hasher_finalize(&hasher, v.data(), v.size());
	 */
	if (out_len == 0) {
		return;
	}
	/* If the subtree stack is empty, then the current chunk is the root. */
	if (ctx->cv_stack_len == 0) {
		output_t output = chunk_state_output(&ctx->chunk);
		output_root_bytes(ctx->ops, &output, seek, out, out_len);
		return;
	}
	/*
	 * If there are any bytes in the chunk state, finalize that chunk and
	 * do a roll-up merge between that chunk hash and every subtree in the
	 * stack. In this case, the extra merge loop at the end of
	 * blake3_hasher_update guarantees that none of the subtrees in the
	 * stack need to be merged with each other first. Otherwise, if there
	 * are no bytes in the chunk state, then the top of the stack is a
	 * chunk hash, and we start the merge from that.
	 */
	output_t output;
	size_t cvs_remaining;
	if (chunk_state_len(&ctx->chunk) > 0) {
		cvs_remaining = ctx->cv_stack_len;
		output = chunk_state_output(&ctx->chunk);
	} else {
		/* There are always at least 2 CVs in the stack in this case. */
		cvs_remaining = ctx->cv_stack_len - 2;
		output = parent_output(&ctx->cv_stack[cvs_remaining * 32],
		    ctx->key, ctx->chunk.flags);
	}
	while (cvs_remaining > 0) {
		cvs_remaining -= 1;
		uint8_t parent_block[BLAKE3_BLOCK_LEN];
		memcpy(parent_block, &ctx->cv_stack[cvs_remaining * 32], 32);
		output_chaining_value(ctx->ops, &output, &parent_block[32]);
		output = parent_output(parent_block, ctx->key,
		    ctx->chunk.flags);
	}
	output_root_bytes(ctx->ops, &output, seek, out, out_len);
}
