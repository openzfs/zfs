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
