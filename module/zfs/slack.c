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
 * Copyright (c) 2024, Klara, Inc.
 */

#include <sys/zio_compress.h>
#include <sys/types.h>

/*
 * Slack compression simply searches for the last non-zero byte in the buffer,
 * and sets the position as the size of the "compressed" data.
 */

typedef struct {
	size_t chunk_off;	/* rolling offset to start of current chunk */
	size_t nzero_off;	/* offset+1 of last non-zero byte */
} slack_cb_args_t;

static int
zfs_slack_compress_cb(void *data, size_t len, void *priv)
{
	slack_cb_args_t *args = (slack_cb_args_t *) priv;
	uint64_t *buf = (uint64_t *) data;

	ASSERT0(P2PHASE(len, sizeof (uint64_t)));

	args->chunk_off -= len;

	int p = (len / sizeof (uint64_t)) - 1;
	for (; p >= 0; p--)
		if (buf[p] != 0)
			break;

	if (p >= 0) {
		args->nzero_off = args->chunk_off +
		    ((p + 1) * sizeof (uint64_t));
		return (1);
	}

	return (0);
}

size_t
zfs_slack_compress(abd_t *src, abd_t *dst, size_t s_len, size_t d_len,
    int level)
{
	(void) level;
	(void) dst;
	(void) d_len;

	ASSERT0P(dst);
	ASSERT3U(s_len, >, 0);
	ASSERT0(P2PHASE(s_len, sizeof (uint64_t)));

	/* [chunk start offset, last !0 offset ] */
	slack_cb_args_t args = {
		.chunk_off = s_len,
		.nzero_off = 0,
	};
	abd_iterate_func_flags(src, 0, s_len, zfs_slack_compress_cb, &args,
	    ABD_ITER_REVERSE);

	if (args.nzero_off > d_len)
		return (s_len);

	return (args.nzero_off);
}

int
zfs_slack_decompress(abd_t *src, abd_t *dst, size_t s_len, size_t d_len,
    int level)
{
	(void) level;
	ASSERT3U(d_len, >=, s_len);
	abd_copy(dst, src, s_len);
	if (d_len > s_len)
		abd_zero_off(dst, s_len, d_len-s_len);
	return (0);
}
