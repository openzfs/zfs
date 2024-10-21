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

/*
 * Convert `v` to ASCII hex characters. The bottom `n` nybbles (4-bits ie one
 * hex digit) will be written, up to `buflen`. The buffer will not be
 * null-terminated. Returns the number of digits written.
 */
static size_t
spl_bt_u64_to_hex_str(uint64_t v, size_t n, char *buf, size_t buflen)
{
	static const char hexdigits[] = {
	    '0', '1', '2', '3', '4', '5', '6', '7',
	    '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'
	};

	size_t pos = 0;
	boolean_t want = (n == 0);
	for (int i = 15; i >= 0; i--) {
		const uint64_t d = v >> (i * 4) & 0xf;
		if (!want && (d != 0 || n > i))
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
	size_t n;
	int err;

	/* Snapshot the current frame and state. */
	unw_getcontext(&uc);

	/*
	 * TODO: walk back to the frame that tripped the assertion / the place
	 *       where the signal was recieved.
	 */

	/*
	 * Register dump. We're going to loop over all the registers in the
	 * top frame, and show them, with names, in a nice three-column
	 * layout, which keeps us within 80 columns.
	 */
	spl_bt_write(fd, "Registers:\n");

	/* Initialise a frame cursor, starting at the current frame */
	unw_init_local(&cp, &uc);

	/*
	 * libunwind's list of possible registers for this architecture is an
	 * enum, unw_regnum_t. UNW_TDEP_LAST_REG is the highest-numbered
	 * register in that list, however, not all register numbers in this
	 * range are defined by the architecture, and not all defined registers
	 * will be present on every implementation of that architecture.
	 * Moreover, libunwind provides nice names for most, but not all
	 * registers, but these are hardcoded; a name being available does not
	 * mean that register is available.
	 *
	 * So, we have to pull this all together here. We try to get the value
	 * of every possible register. If we get a value for it, then the
	 * register must exist, and so we get its name. If libunwind has no
	 * name for it, we synthesize something. These cases should be rare,
	 * and they're usually for uninteresting or niche registers, so it
	 * shouldn't really matter. We can see the value, and that's the main
	 * thing.
	 */
	uint_t cols = 0;
	for (uint_t regnum = 0; regnum <= UNW_TDEP_LAST_REG; regnum++) {
		/*
		 * Get the value. Any error probably means the register
		 * doesn't exist, and we skip it.
		 */
		if (unw_get_reg(&cp, regnum, &v) < 0)
			continue;

		/*
		 * Register name. If libunwind doesn't have a name for it,
		 * it will return "???". As a shortcut, we just treat '?'
		 * is an alternate end-of-string character.
		 */
		const char *name = unw_regname(regnum);
		for (n = 0; name[n] != '\0' && name[n] != '?'; n++) {}
		if (n == 0) {
			/*
			 * No valid name, so make one of the form "?xx", where
			 * "xx" is the two-char hex of libunwind's register
			 * number.
			 */
			buf[0] = '?';
			n = spl_bt_u64_to_hex_str(regnum, 2,
			    &buf[1], sizeof (buf)-1) + 1;
			name = buf;
		}

		/*
		 * Two spaces of padding before each column, plus extra
		 * spaces to align register names shorter than three chars.
		 */
		spl_bt_write_n(fd, "      ", 5-MIN(n, 3));

		/* Register name and column punctuation */
		spl_bt_write_n(fd, name, n);
		spl_bt_write(fd, ": 0x");

		/*
		 * Convert register value (from unw_get_reg()) to hex. We're
		 * assuming that all registers are 64-bits wide, which is
		 * probably fine for any general-purpose registers on any
		 * machine currently in use. A more generic way would be to
		 * look at the width of unw_word_t, but that would also
		 * complicate the column code a bit. This is fine.
		 */
		n = spl_bt_u64_to_hex_str(v, 16, buf, sizeof (buf));
		spl_bt_write_n(fd, buf, n);

		/* Every third column, emit a newline */
		if (!(++cols % 3))
			spl_bt_write(fd, "\n");
	}

	/* If we finished before the third column, emit a newline. */
	if (cols % 3)
		spl_bt_write(fd, "\n");

	/* Now the main event, the backtrace. */
	spl_bt_write(fd, "Call trace:\n");

	/* Reset the cursor to the top again. */
	unw_init_local(&cp, &uc);

	do {
		/*
		 * Getting the IP should never fail; libunwind handles it
		 * specially, because its used a lot internally. Still, no
		 * point being silly about it, as the last thing we want is
		 * our crash handler to crash. So if it ever does fail, we'll
		 * show an error line, but keep going to the next frame.
		 */
		if (unw_get_reg(&cp, UNW_REG_IP, &v) < 0) {
			spl_bt_write(fd, "  [couldn't get IP register; "
			    "corrupt frame?]");
			continue;
		}

		/* IP & punctuation */
		n = spl_bt_u64_to_hex_str(v, 16, buf, sizeof (buf));
		spl_bt_write(fd, "  [0x");
		spl_bt_write_n(fd, buf, n);
		spl_bt_write(fd, "] ");

		/*
		 * Function ("procedure") name for the current frame. `v`
		 * receives the offset from the named function to the IP, which
		 * we show as a "+offset" suffix.
		 *
		 * If libunwind can't determine the name, we just show "???"
		 * instead. We've already displayed the IP above; that will
		 * have to do.
		 *
		 * unw_get_proc_name() will return ENOMEM if the buffer is too
		 * small, instead truncating the name. So we treat that as a
		 * success and use whatever is in the buffer.
		 */
		err = unw_get_proc_name(&cp, buf, sizeof (buf), &v);
		if (err == 0 || err == -UNW_ENOMEM) {
			for (n = 0; n < sizeof (buf) && buf[n] != '\0'; n++) {}
			spl_bt_write_n(fd, buf, n);

			/* Offset from proc name */
			spl_bt_write(fd, "+0x");
			n = spl_bt_u64_to_hex_str(v, 2, buf, sizeof (buf));
			spl_bt_write_n(fd, buf, n);
		} else
			spl_bt_write(fd, "???");

#ifdef HAVE_LIBUNWIND_ELF
		/*
		 * Newer libunwind has unw_get_elf_filename(), which gets
		 * the name of the ELF object that the frame was executing in.
		 * Like `unw_get_proc_name()`, `v` recieves the offset within
		 * the file, and UNW_ENOMEM indicates that a truncate filename
		 * was left in the buffer.
		 */
		err = unw_get_elf_filename(&cp, buf, sizeof (buf), &v);
		if (err == 0 || err == -UNW_ENOMEM) {
			for (n = 0; n < sizeof (buf) && buf[n] != '\0'; n++) {}
			spl_bt_write(fd, " (in ");
			spl_bt_write_n(fd, buf, n);

			/* Offset within file */
			spl_bt_write(fd, " +0x");
			n = spl_bt_u64_to_hex_str(v, 2, buf, sizeof (buf));
			spl_bt_write_n(fd, buf, n);
			spl_bt_write(fd, ")");
		}
#endif
		spl_bt_write(fd, "\n");
	} while (unw_step(&cp) > 0);
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
