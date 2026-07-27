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
    abd_t **dst, void **cbuf_handle, uint64_t *c_len,
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
