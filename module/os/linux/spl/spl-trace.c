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
 * Each DTRACE_PROBE must define its trace point in one (and only one)
 * source file, so this dummy file exists for that purpose.
 */

#include <sys/taskq.h>

#define	CREATE_TRACE_POINTS
#include <sys/trace.h>
#include <sys/trace_taskq.h>
