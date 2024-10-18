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
#include <sys/types.h>
#include <unistd.h>

/*
 * libspl_backtrace() must be safe to call from inside a signal hander. This
 * mostly means it must not allocate, and so we can't use things like printf.
 */

#if defined(HAVE_LIBUNWIND)
#define	UNW_LOCAL_ONLY
#include <libunwind.h>

static size_t
libspl_u64_to_hex_str(uint64_t v, size_t digits, char *buf, size_t buflen)
{
	static const char hexdigits[] = {
	    '0', '1', '2', '3', '4', '5', '6', '7',
	    '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'
	};

	size_t pos = 0;
	boolean_t want = (digits == 0);
	for (int i = 15; i >= 0; i--) {
		const uint64_t d = v >> (i * 4) & 0xf;
		if (!want && (d != 0 || digits > i))
			want = B_TRUE;
		if (want) {
			buf[pos++] = hexdigits[d];
			if (pos == buflen)
				break;
		}
	}
	return (pos);
}

void
libspl_backtrace(int fd)
{
	ssize_t ret __attribute__((unused));
	unw_context_t uc;
	unw_cursor_t cp;
	unw_word_t v;
	char buf[128];
	size_t n, c;

	unw_getcontext(&uc);

	unw_init_local(&cp, &uc);
	ret = write(fd, "Registers:\n", 11);
	c = 0;
	for (uint_t regnum = 0; regnum <= UNW_TDEP_LAST_REG; regnum++) {
		if (unw_get_reg(&cp, regnum, &v) < 0)
			continue;
		const char *name = unw_regname(regnum);
		for (n = 0; name[n] != '\0' && name[n] != '?'; n++) {}
		if (n == 0) {
			buf[0] = '?';
			n = libspl_u64_to_hex_str(regnum, 2,
			    &buf[1], sizeof (buf)-1) + 1;
			name = buf;
		}
		ret = write(fd, "      ", 5-MIN(n, 3));
		ret = write(fd, name, n);
		ret = write(fd, ": 0x", 4);
		n = libspl_u64_to_hex_str(v, 18, buf, sizeof (buf));
		ret = write(fd, buf, n);
		if (!(++c % 3))
			ret = write(fd, "\n", 1);
	}
	if (c % 3)
		ret = write(fd, "\n", 1);

	unw_init_local(&cp, &uc);
	ret = write(fd, "Call trace:\n", 12);
	while (unw_step(&cp) > 0) {
		unw_get_reg(&cp, UNW_REG_IP, &v);
		ret = write(fd, "  [0x", 5);
		n = libspl_u64_to_hex_str(v, 18, buf, sizeof (buf));
		ret = write(fd, buf, n);
		ret = write(fd, "] ", 2);
		unw_get_proc_name(&cp, buf, sizeof (buf), &v);
		for (n = 0; n < sizeof (buf) && buf[n] != '\0'; n++) {}
		ret = write(fd, buf, n);
		ret = write(fd, "+0x", 3);
		n = libspl_u64_to_hex_str(v, 2, buf, sizeof (buf));
		ret = write(fd, buf, n);
#ifdef HAVE_LIBUNWIND_ELF
		ret = write(fd, " (in ", 5);
		unw_get_elf_filename(&cp, buf, sizeof (buf), &v);
		for (n = 0; n < sizeof (buf) && buf[n] != '\0'; n++) {}
		ret = write(fd, buf, n);
		ret = write(fd, " +0x", 4);
		n = libspl_u64_to_hex_str(v, 2, buf, sizeof (buf));
		ret = write(fd, buf, n);
		ret = write(fd, ")", 1);
#endif
		ret = write(fd, "\n", 1);
	}
}
#elif defined(HAVE_BACKTRACE)
#include <execinfo.h>

void
libspl_backtrace(int fd)
{
	ssize_t ret __attribute__((unused));
	void *btptrs[64];
	size_t nptrs = backtrace(btptrs, 64);
	ret = write(fd, "Call trace:\n", 12);
	backtrace_symbols_fd(btptrs, nptrs, fd);
}
#else
#include <sys/debug.h>

void
libspl_backtrace(int fd __maybe_unused)
{
}
#endif
