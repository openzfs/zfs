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

static size_t
zfs_slack_compress_buf(void *src, void *dst, size_t s_len, size_t d_len,
    int level)
{
	(void) level;

	ASSERT3U(s_len, >, 0);
	ASSERT0(P2PHASE(s_len, sizeof (uint64_t)));

	uint64_t *buf = (uint64_t *)src;

	int p = (s_len / sizeof (uint64_t)) - 1;
	for (; p >= 0; p--)
		if (buf[p] != 0)
			break;

	if (p < 0)
		return (s_len);

	size_t c_len = (p + 1) * sizeof (uint64_t);
	if (c_len > d_len)
		return (s_len);

	memcpy(dst, src, c_len);
	return (c_len);
}

static int
zfs_slack_decompress_buf(void *src, void *dst, size_t s_len, size_t d_len,
    int level)
{
	(void) level;
	ASSERT3U(d_len, >=, s_len);
	memcpy(dst, src, s_len);
	if (d_len > s_len)
		memset(dst+s_len, 0, d_len-s_len);
	return (0);
}

ZFS_COMPRESS_WRAP_DECL(zfs_slack_compress)
ZFS_DECOMPRESS_WRAP_DECL(zfs_slack_decompress)
