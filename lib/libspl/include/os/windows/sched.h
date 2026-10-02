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

#ifndef _SPL_SCHED_H
#define	_SPL_SCHED_H


struct sched_param {
	int32_t  sched_priority;
	int32_t  sched_curpriority;
	union {
		int32_t  reserved[8];
		struct {
			int32_t  __ss_low_priority;
			int32_t  __ss_max_repl;
			struct timespec __ss_repl_period;
			struct timespec __ss_init_budget;
		} __ss;
	} __ss_un;
};

extern int sched_yield(void);

#endif
