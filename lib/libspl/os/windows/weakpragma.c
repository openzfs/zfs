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

#include <sys/types.h>
#include <sys/dmu.h>
#include <sys/dbuf.h>


/* No #pragma weaks here! */
void
dmu_buf_add_ref(dmu_buf_t *db, void *tag)
{
	dbuf_add_ref((dmu_buf_impl_t *)db, tag);
}

boolean_t
dmu_buf_try_add_ref(dmu_buf_t *db, objset_t *os, uint64_t object,
    uint64_t blkid, void *tag)
{
	return (dbuf_try_add_ref(db, os, object, blkid, tag));
}
