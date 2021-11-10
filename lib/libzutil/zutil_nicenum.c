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

/*
 * Copyright (c) 2005, 2010, Oracle and/or its affiliates. All rights reserved.
 */

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <libzutil.h>
#include <string.h>

/*
 * Return B_TRUE if "str" is a number string, B_FALSE otherwise.
 * Works for integer and floating point numbers.
 */
boolean_t
zfs_isnumber(const char *str)
{
	if (!*str)
		return (B_FALSE);

	for (; *str; str++)
		if (!(isdigit(*str) || (*str == '.')))
			return (B_FALSE);

	/*
	 * Numbers should not end with a period ("." ".." or "5." are
	 * not valid)
	 */
	if (str[strlen(str) - 1] == '.') {
		return (B_FALSE);
	}

	return (B_TRUE);
}

/*
 * Convert a number to an appropriately human-readable output.
 */
void
zfs_nicenum_format(uint64_t num, char *buf, size_t buflen,
    enum zfs_nicenum_format format)
{
	uint64_t n = num;
	int index = 0;
	const char *u;
	const char *units[3][7] = {
	    [ZFS_NICENUM_1024] = {"", "K", "M", "G", "T", "P", "E"},
	    [ZFS_NICENUM_BYTES] = {"B", "K", "M", "G", "T", "P", "E"},
	    [ZFS_NICENUM_TIME] = {"ns", "us", "ms", "s", "?", "?", "?"}
	};

	const int units_len[] = {[ZFS_NICENUM_1024] = 6,
	    [ZFS_NICENUM_BYTES] = 6,
	    [ZFS_NICENUM_TIME] = 4};

	const int k_unit[] = {	[ZFS_NICENUM_1024] = 1024,
	    [ZFS_NICENUM_BYTES] = 1024,
	    [ZFS_NICENUM_TIME] = 1000};

	double val;

	if (format == ZFS_NICENUM_RAW) {
		snprintf(buf, buflen, "%llu", (u_longlong_t)num);
		return;
	} else if (format == ZFS_NICENUM_RAWTIME && num > 0) {
		snprintf(buf, buflen, "%llu", (u_longlong_t)num);
		return;
	} else if (format == ZFS_NICENUM_RAWTIME && num == 0) {
		snprintf(buf, buflen, "%s", "-");
		return;
	}

	while (n >= k_unit[format] && index < units_len[format]) {
		n /= k_unit[format];
		index++;
	}

	u = units[format][index];

	/* Don't print zero latencies since they're invalid */
	if ((format == ZFS_NICENUM_TIME) && (num == 0)) {
		(void) snprintf(buf, buflen, "-");
	} else if ((index == 0) || ((num %
	    (uint64_t)powl(k_unit[format], index)) == 0)) {
		/*
		 * If this is an even multiple of the base, always display
		 * without any decimal precision.
		 */
		(void) snprintf(buf, buflen, "%llu%s", (u_longlong_t)n, u);

	} else {
		/*
		 * We want to choose a precision that reflects the best choice
		 * for fitting in 5 characters.  This can get rather tricky when
		 * we have numbers that are very close to an order of magnitude.
		 * For example, when displaying 10239 (which is really 9.999K),
		 * we want only a single place of precision for 10.0K.  We could
		 * develop some complex heuristics for this, but it's much
		 * easier just to try each combination in turn.
		 */
		int i;
		for (i = 2; i >= 0; i--) {
			val = (double)num /
			    (uint64_t)powl(k_unit[format], index);

			/*
			 * Don't print floating point values for time.  Note,
			 * we use floor() instead of round() here, since
			 * round can result in undesirable results.  For
			 * example, if "num" is in the range of
			 * 999500-999999, it will print out "1000us".  This
			 * doesn't happen if we use floor().
			 */
			if (format == ZFS_NICENUM_TIME) {
				if (snprintf(buf, buflen, "%d%s",
				    (unsigned int) floor(val), u) <= 5)
					break;

			} else {
				if (snprintf(buf, buflen, "%.*f%s", i,
				    val, u) <= 5)
					break;
			}
		}
	}
}

/*
 * Convert a number to an appropriately human-readable output.
 */
void
zfs_nicenum(uint64_t num, char *buf, size_t buflen)
{
	zfs_nicenum_format(num, buf, buflen, ZFS_NICENUM_1024);
}

/*
 * Convert a time to an appropriately human-readable output.
 * @num:	Time in nanoseconds
 */
void
zfs_nicetime(uint64_t num, char *buf, size_t buflen)
{
	zfs_nicenum_format(num, buf, buflen, ZFS_NICENUM_TIME);
}

/*
 * Print out a raw number with correct column spacing
 */
void
zfs_niceraw(uint64_t num, char *buf, size_t buflen)
{
	zfs_nicenum_format(num, buf, buflen, ZFS_NICENUM_RAW);
}

/*
 * Convert a number of bytes to an appropriately human-readable output.
 */
void
zfs_nicebytes(uint64_t num, char *buf, size_t buflen)
{
	zfs_nicenum_format(num, buf, buflen, ZFS_NICENUM_BYTES);
}
