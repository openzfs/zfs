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
#include <sys/debug.h>
#include <unistd.h>

/*
 * Output helpers. libspl_backtrace() must not block, must be thread-safe and
 * must be safe to call from a signal handler. At least, that means not having
 * printf, so we end up having to call write() directly on the fd. That's
 * awkward, as we always have to pass through a length, and some systems will
 * complain if we don't consume the return. So we have some macros to make
 * things a little more palatable.
 */
#define	spl_bt_write_n(fd, s, n) \
	do { ssize_t r __maybe_unused = write(fd, s, n); } while (0)
#define	spl_bt_write(fd, s)		spl_bt_write_n(fd, s, sizeof (s))

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
	unw_context_t uc;
	unw_cursor_t cp;
	unw_word_t v;
	char buf[128];
	size_t n, c;

	unw_getcontext(&uc);

	unw_init_local(&cp, &uc);
	spl_bt_write(fd, "Registers:\n");
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
		spl_bt_write_n(fd, "      ", 5-MIN(n, 3));
		spl_bt_write_n(fd, name, n);
		spl_bt_write(fd, ": 0x");
		n = libspl_u64_to_hex_str(v, 18, buf, sizeof (buf));
		spl_bt_write_n(fd, buf, n);
		if (!(++c % 3))
			spl_bt_write(fd, "\n");
	}
	if (c % 3)
		spl_bt_write(fd, "\n");

	unw_init_local(&cp, &uc);
	spl_bt_write(fd, "Call trace:\n");
	while (unw_step(&cp) > 0) {
		unw_get_reg(&cp, UNW_REG_IP, &v);
		spl_bt_write(fd, "  [0x");
		n = libspl_u64_to_hex_str(v, 18, buf, sizeof (buf));
		spl_bt_write_n(fd, buf, n);
		spl_bt_write(fd, "] ");
		unw_get_proc_name(&cp, buf, sizeof (buf), &v);
		for (n = 0; n < sizeof (buf) && buf[n] != '\0'; n++) {}
		spl_bt_write_n(fd, buf, n);
		spl_bt_write(fd, "+0x");
		n = libspl_u64_to_hex_str(v, 2, buf, sizeof (buf));
		spl_bt_write_n(fd, buf, n);
#ifdef HAVE_LIBUNWIND_ELF
		spl_bt_write(fd, " (in ");
		unw_get_elf_filename(&cp, buf, sizeof (buf), &v);
		for (n = 0; n < sizeof (buf) && buf[n] != '\0'; n++) {}
		spl_bt_write_n(fd, buf, n);
		spl_bt_write(fd, " +0x");
		n = libspl_u64_to_hex_str(v, 2, buf, sizeof (buf));
		spl_bt_write_n(fd, buf, n);
		spl_bt_write(fd, ")");
#endif
		spl_bt_write(fd, "\n");
	}
}
#elif defined(HAVE_BACKTRACE)
#include <execinfo.h>

void
libspl_backtrace(int fd)
{
	void *btptrs[64];
	size_t nptrs = backtrace(btptrs, 64);
	spl_bt_write(fd, "Call trace:\n");
	backtrace_symbols_fd(btptrs, nptrs, fd);
}
#else
void
libspl_backtrace(int fd __maybe_unused)
{
}
#endif
