/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License, Version 1.0 only
 * (the "License").  You may not use this file except in compliance
 * with the License.
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
 * Copyright 2005 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */
/*
 * Portions Copyright 2006 OmniTI, Inc.
 */

/* #pragma ident	"@(#)misc.c	1.6	05/06/08 SMI" */

#define _BUILDING_UMEM_MISC_C
#include "config.h"
/* #include "mtlib.h" */
#if HAVE_UNISTD_H
#include <unistd.h>
#endif
#if HAVE_DLFCN_H
#include <dlfcn.h>
#endif
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#if HAVE_SYS_MACHELF_H
#include <sys/machelf.h>
#endif

#include <umem_impl.h>
#include "misc.h"

#ifdef ECELERITY
#include "util.h"
#endif

#define	UMEM_ERRFD	2	/* goes to standard error */
#define	UMEM_MAX_ERROR_SIZE 4096 /* error messages are truncated to this */

/*
 * This is a circular buffer for holding error messages.
 * umem_error_enter appends to the buffer, adding "..." to the beginning
 * if data has been lost.
 */

#define	ERR_SIZE 8192		/* must be a power of 2 */

static mutex_t umem_error_lock = DEFAULTMUTEX;

static char umem_error_buffer[ERR_SIZE] = "";
static uint_t umem_error_begin = 0;
static uint_t umem_error_end = 0;

#define	WRITE_AND_INC(var, value) { \
	umem_error_buffer[(var)++] = (value); \
	var = P2PHASE((var), ERR_SIZE); \
}

static void
umem_log_enter(const char *error_str, int serious)
{
	int looped;
	char c;

	looped = 0;
#ifdef ECELERITY
	mem_printf(serious ? DCRITICAL : DINFO, "umem: %s", error_str);
#endif

	(void) mutex_lock(&umem_error_lock);

	while ((c = *error_str++) != '\0') {
		WRITE_AND_INC(umem_error_end, c);
		if (umem_error_end == umem_error_begin)
			looped = 1;
	}

	umem_error_buffer[umem_error_end] = 0;

	if (looped) {
		uint_t idx;
		umem_error_begin = P2PHASE(umem_error_end + 1, ERR_SIZE);

		idx = umem_error_begin;
		WRITE_AND_INC(idx, '.');
		WRITE_AND_INC(idx, '.');
		WRITE_AND_INC(idx, '.');
	}

	(void) mutex_unlock(&umem_error_lock);
}

void
umem_error_enter(const char *error_str)
{
#ifndef UMEM_STANDALONE
	if (umem_output && !issetugid())
		(void) write(UMEM_ERRFD, error_str, strlen(error_str));
#endif

	umem_log_enter(error_str, 1);
}

int
highbit(ulong_t i)
{
	register int h = 1;

	if (i == 0)
		return (0);
#ifdef _LP64
	if (i & 0xffffffff00000000ul) {
		h += 32; i >>= 32;
	}
#endif
	if (i & 0xffff0000) {
		h += 16; i >>= 16;
	}
	if (i & 0xff00) {
		h += 8; i >>= 8;
	}
	if (i & 0xf0) {
		h += 4; i >>= 4;
	}
	if (i & 0xc) {
		h += 2; i >>= 2;
	}
	if (i & 0x2) {
		h += 1;
	}
	return (h);
}

int
lowbit(ulong_t i)
{
	register int h = 1;

	if (i == 0)
		return (0);
#ifdef _LP64
	if (!(i & 0xffffffff)) {
		h += 32; i >>= 32;
	}
#endif
	if (!(i & 0xffff)) {
		h += 16; i >>= 16;
	}
	if (!(i & 0xff)) {
		h += 8; i >>= 8;
	}
	if (!(i & 0xf)) {
		h += 4; i >>= 4;
	}
	if (!(i & 0x3)) {
		h += 2; i >>= 2;
	}
	if (!(i & 0x1)) {
		h += 1;
	}
	return (h);
}

void
hrt2ts(hrtime_t hrt, timestruc_t *tsp)
{
	tsp->tv_sec = hrt / NANOSEC;
	tsp->tv_nsec = hrt % NANOSEC;
}

void
log_message(const char *format, ...)
{
	char buf[UMEM_MAX_ERROR_SIZE] = "";

	va_list va;

	va_start(va, format);
	(void) vsnprintf(buf, UMEM_MAX_ERROR_SIZE-1, format, va);
	va_end(va);

#ifndef UMEM_STANDALONE
	if (umem_output > 1)
		(void) write(UMEM_ERRFD, buf, strlen(buf));
#endif

	umem_log_enter(buf, 0);
}

#ifndef UMEM_STANDALONE
void
debug_printf(const char *format, ...)
{
	char buf[UMEM_MAX_ERROR_SIZE] = "";

	va_list va;

	va_start(va, format);
	(void) vsnprintf(buf, UMEM_MAX_ERROR_SIZE-1, format, va);
	va_end(va);

	(void) write(UMEM_ERRFD, buf, strlen(buf));
}
#endif

void
umem_vprintf(const char *format, va_list va)
{
	char buf[UMEM_MAX_ERROR_SIZE] = "";

	(void) vsnprintf(buf, UMEM_MAX_ERROR_SIZE-1, format, va);

	umem_error_enter(buf);
}

void
umem_printf(const char *format, ...)
{
	va_list va;

	va_start(va, format);
	umem_vprintf(format, va);
	va_end(va);
}

/*ARGSUSED*/
void
umem_printf_warn(void *ignored, const char *format, ...)
{
	va_list va;

	va_start(va, format);
	umem_vprintf(format, va);
	va_end(va);
}

/*
 * print_sym tries to print out the symbol and offset of a pointer
 */
int
print_sym(void *pointer)
{
#if HAVE_SYS_MACHELF_H
	int result;
	Dl_info sym_info;

	uintptr_t end = NULL;

	Sym *ext_info = NULL;

	result = dladdr1(pointer, &sym_info, (void **)&ext_info,
	    RTLD_DL_SYMENT);

	if (result != 0) {
		const char *endpath;

		end = (uintptr_t)sym_info.dli_saddr + ext_info->st_size;

		endpath = strrchr(sym_info.dli_fname, '/');
		if (endpath)
			endpath++;
		else
			endpath = sym_info.dli_fname;
		umem_printf("%s'", endpath);
	}

	if (result == 0 || (uintptr_t)pointer > end) {
		umem_printf("?? (0x%p)", pointer);
		return (0);
	} else {
		umem_printf("%s+0x%p", sym_info.dli_sname,
		    (char *)pointer - (char *)sym_info.dli_saddr);
		return (1);
	}
#else
	return 0;
#endif
}
