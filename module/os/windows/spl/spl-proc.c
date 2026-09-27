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

#include <sys/systeminfo.h>
#include <sys/kstat.h>
#include <spl-debug.h>

/*
 * "p0" is the first process/kernel in illumos/solaris - it is only used as
 * an address to know if we are first process or not. It needs no allocated
 * space, just "an address". It should be a "proc_t *".
 */

struct _KPROCESS {
    void *something;
};

proc_t p0 = {0};
