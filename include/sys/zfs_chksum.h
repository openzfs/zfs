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
