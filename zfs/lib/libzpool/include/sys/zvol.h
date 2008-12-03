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
 * Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef	_SYS_ZVOL_H
#define	_SYS_ZVOL_H

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#include <sys/zfs_context.h>

#ifdef	__cplusplus
extern "C" {
#endif

#define	ZVOL_OBJ		1ULL
#define	ZVOL_ZAP_OBJ		2ULL

#ifdef _KERNEL
extern int zvol_check_volsize(uint64_t volsize, uint64_t blocksize);
extern int zvol_check_volblocksize(uint64_t volblocksize);
extern int zvol_get_stats(objset_t *os, nvlist_t *nv);
extern void zvol_create_cb(objset_t *os, void *arg, cred_t *cr, dmu_tx_t *tx);
extern int zvol_create_minor(const char *, major_t);
extern int zvol_remove_minor(const char *);
extern int zvol_set_volsize(const char *, major_t, uint64_t);
extern int zvol_set_volblocksize(const char *, uint64_t);

extern int zvol_open(dev_t *devp, int flag, int otyp, cred_t *cr);
extern int zvol_dump(dev_t dev, caddr_t addr, daddr_t offset, int nblocks);
extern int zvol_close(dev_t dev, int flag, int otyp, cred_t *cr);
extern int zvol_strategy(buf_t *bp);
extern int zvol_read(dev_t dev, uio_t *uiop, cred_t *cr);
extern int zvol_write(dev_t dev, uio_t *uiop, cred_t *cr);
extern int zvol_aread(dev_t dev, struct aio_req *aio, cred_t *cr);
extern int zvol_awrite(dev_t dev, struct aio_req *aio, cred_t *cr);
extern int zvol_ioctl(dev_t dev, int cmd, intptr_t arg, int flag, cred_t *cr,
    int *rvalp);
extern int zvol_busy(void);
extern void zvol_init(void);
extern void zvol_fini(void);
#endif

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_ZVOL_H */
