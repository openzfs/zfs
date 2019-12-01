/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License, Version 1.0 only
 * (the "License").  You may not use this file except in compliance
 * with the License.
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
 * Copyright (c) 2016-2018, Klara Systems Inc. All rights reserved.
 * Copyright (c) 2016-2018, Allan Jude. All rights reserved.
 * Copyright (c) 2018-2019, Sebastian Gottschall. All rights reserved.
 * Copyright (c) 2019, Michael Niewöhner. All rights reserved.
 */

#ifndef	_ZFS_ZSTD_H
#define	_ZFS_ZSTD_H

#ifdef	__cplusplus
extern "C" {
#endif

/* (de)init for user space / kernel emulation */
int zstd_init(void);
void zstd_fini(void);

size_t zstd_compress(void *s_start, void *d_start, size_t s_len, size_t d_len,
    int n);
int zstd_get_level(void *s_start, size_t s_len, uint8_t *level);
int zstd_decompress_level(void *s_start, void *d_start, size_t s_len,
    size_t d_len, uint8_t *level);
int zstd_decompress(void *s_start, void *d_start, size_t s_len, size_t d_len,
    int n);

#ifdef	__cplusplus
}
#endif

#endif /* _ZFS_ZSTD_H */
