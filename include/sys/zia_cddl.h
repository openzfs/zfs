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

#ifndef _ZIA_CDDL_H
#define	_ZIA_CDDL_H

#include <sys/abd.h>
#include <sys/zio.h>
#include <sys/zio_compress.h>

#ifdef ZIA
#include <dpusm/user_api.h>
int
zia_compress_impl(const dpusm_uf_t *dpusm, zia_props_t *props,
    enum zio_compress c, abd_t *src, size_t s_len,
    void **cbuf_handle, uint64_t *c_len,
    uint8_t level, boolean_t *local_offload);

int
zia_raidz_rec_impl(const dpusm_uf_t *dpusm,
    raidz_row_t *rr, int *t, int nt);

#ifdef _KERNEL
void
zia_disk_write_completion(void *zio_ptr, int error);

void
zia_disk_flush_completion(void *zio_ptr, int error);
#endif /* _KERNEL */

#endif /* ZIA */

#endif /* _ZIA_CDDL_H */
