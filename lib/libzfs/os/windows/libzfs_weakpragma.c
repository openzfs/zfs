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
 * Copyright (c) 2017 Jorgen Lundman <lundman@lundman.net>
 */

#include <sys/types.h>
#include <sys/dmu.h>
#include <sys/dbuf.h>

/* No #pragma weaks here! */
void
dmu_buf_add_ref(dmu_buf_t *db, const void *tag)
{
	dbuf_add_ref((dmu_buf_impl_t *)db, tag);
}

boolean_t
dmu_buf_try_add_ref(dmu_buf_t *db, objset_t *os, uint64_t object,
    uint64_t blkid, const void *tag)
{
	return (dbuf_try_add_ref(db, os, object, blkid, tag));
}
