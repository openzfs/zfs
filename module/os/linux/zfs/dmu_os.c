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
 * Copyright (c) 2010, Oracle and/or its affiliates. All rights reserved.
 * Copyright (c) 2012, 2018 by Delphix. All rights reserved.
 * Copyright (c) 2019, loli10K <ezomori.nozomu@gmail.com>. All rights reserved.
 */

#include <sys/dmu.h>
#include <sys/dmu_impl.h>
#include <sys/dmu_recv.h>
#include <sys/dmu_tx.h>
#include <sys/dbuf.h>
#include <sys/dnode.h>
#include <sys/zfs_context.h>
#include <sys/dmu_objset.h>
#include <sys/dmu_traverse.h>
#include <sys/dsl_dataset.h>
#include <sys/dsl_dir.h>
#include <sys/dsl_pool.h>
#include <sys/dsl_synctask.h>
#include <sys/zfs_ioctl.h>
#include <sys/zap.h>
#include <sys/zio_checksum.h>
#include <sys/zfs_znode.h>

int
dmu_write_record(dmu_diffarg_t *da)
{
	ssize_t resid; /* have to get resid to get detailed errno */

	if (da->da_ddr.ddr_type == DDR_NONE) {
		da->da_err = 0;
		return (0);
	}

	da->da_err = vn_rdwr(UIO_WRITE, da->da_fp->f_vnode,
	    (caddr_t)&da->da_ddr,
	    sizeof (da->da_ddr), 0, UIO_SYSSPACE, FAPPEND,
	    RLIM64_INFINITY, CRED(), &resid);
	*da->da_offp += sizeof (da->da_ddr);
	return (da->da_err);
}

int
dmu_send_write(file_t *fp, char *buf, int len, ssize_t *resid)
{
	return (vn_rdwr(UIO_WRITE, fp->f_vnode, buf, len, 0, UIO_SYSSPACE,
	    FAPPEND, RLIM64_INFINITY, CRED(), resid));
}

int
dmu_restore_bytes(dmu_recv_cookie_t *drc, void *buf, int len,
    ssize_t *resid)
{
	return (vn_rdwr(UIO_READ, drc->drc_fp->f_vnode,
	    (char *)buf, len,
	    drc->drc_voff, UIO_SYSSPACE, FAPPEND,
	    RLIM64_INFINITY, CRED(), resid));
}
