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
#include <sys/debug.h>

struct proc {
	void *nothing;
};

struct proc p0 = {0};


int
issig(void)
{
	return (thread_issignal(current_proc(), current_thread(),
	    THREADMASK));
}
