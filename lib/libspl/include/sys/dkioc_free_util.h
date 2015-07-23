/*
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this
 * source.  A copy of the CDDL is also available via the Internet at
 * http://www.illumos.org/license/CDDL.
 */

/*
 * Copyright 2015 Nexenta Inc.  All rights reserved.
 */

#ifndef _SYS_DKIOC_FREE_UTIL_H
#define	_SYS_DKIOC_FREE_UTIL_H

#include <sys/dkio.h>

#ifdef	__cplusplus
extern "C" {
#endif

static inline void dfl_free(dkioc_free_list_t *dfl) {
	vmem_free(dfl, DFL_SZ(dfl->dfl_num_exts));
}

#ifdef	__cplusplus
}
#endif

#endif /* _SYS_DKIOC_FREE_UTIL_H */
