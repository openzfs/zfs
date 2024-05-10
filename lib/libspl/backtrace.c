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
 * Copyright (c) 2024, Rob Norris <robn@despairlabs.com>
 * Copyright (c) 2024, Klara Inc.
 */

#include <sys/backtrace.h>
#include <stdio.h>

#if defined(HAVE_LIBUNWIND)
#define	UNW_LOCAL_ONLY
#include <libunwind.h>

void
libspl_backtrace(void)
{
	unw_context_t uc;
	unw_cursor_t cp;
	unw_word_t ip, off;
	char funcname[128];
#ifdef HAVE_LIBUNWIND_ELF
	char objname[128];
	unw_word_t objoff;
#endif

	fprintf(stderr, "Call trace:\n");
	unw_getcontext(&uc);
	unw_init_local(&cp, &uc);
	while (unw_step(&cp) > 0) {
		unw_get_reg(&cp, UNW_REG_IP, &ip);
		unw_get_proc_name(&cp, funcname, sizeof (funcname), &off);
#ifdef HAVE_LIBUNWIND_ELF
		unw_get_elf_filename(&cp, objname, sizeof (objname), &objoff);
		fprintf(stderr, "  [0x%08lx] %s+0x%2lx (in %s +0x%2lx)\n",
		    ip, funcname, off, objname, objoff);
#else
		fprintf(stderr, "  [0x%08lx] %s+0x%2lx\n", ip, funcname, off);
#endif
	}
}
#elif defined(HAVE_BACKTRACE)
#include <execinfo.h>

void
libspl_backtrace(void)
{
	void *btptrs[100];
	size_t nptrs = backtrace(btptrs, 100);
	char **bt = backtrace_symbols(btptrs, nptrs);
	fprintf(stderr, "Call trace:\n");
	for (size_t i = 0; i < nptrs; i++)
		fprintf(stderr, "  %s\n", bt[i]);
	free(bt);
}
#else
void
libspl_backtrace(void)
{
}
#endif

