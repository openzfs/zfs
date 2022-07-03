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
 * Copyright (c) 2021 Tino Reichardt <milky-zfs@mcmilk.de>
 */

#ifndef	_ZFS_CHKSUM_H
#define	_ZFS_CHKSUM_H

#ifdef  _KERNEL
#include <sys/types.h>
#else
#include <stdint.h>
#include <stdlib.h>
#endif

#ifdef	__cplusplus
extern "C" {
#endif

/* Benchmark the chksums of ZFS when the module is loading */
void chksum_init(void);
void chksum_fini(void);

#ifdef	__cplusplus
}
#endif

#endif	/* _ZFS_CHKSUM_H */
