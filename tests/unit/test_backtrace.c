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

#include <sys/backtrace.h>
#include <sys/types.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "unit.h"

static MunitResult
test_backtrace_destination(const MunitParameter params[], void *data)
{
	(void) data;
#if defined(HAVE_LIBUNWIND) || defined(HAVE_BACKTRACE)
	FILE *capture = tmpfile();
	int saved_stderr = -1;
	boolean_t redirect = strcmp(munit_parameters_get(params, "destination"),
	    "stderr") == 0;
	boolean_t before = B_FALSE;
	boolean_t trace = B_FALSE;
	boolean_t after = B_FALSE;
	char line[1024];

	unit_true(capture != NULL);
	int fd = fileno(capture);
	if (redirect) {
		unit_ok(fflush(stderr));
		saved_stderr = dup(STDERR_FILENO);
		unit_true(saved_stderr >= 0);
		unit_true(dup2(fd, STDERR_FILENO) >= 0);
		fd = STDERR_FILENO;
	}

	ssize_t before_written = write(fd, "before trace\n", 13);
	libspl_backtrace(fd);
	ssize_t after_written = write(fd, "after trace\n", 12);

	if (redirect) {
		unit_true(dup2(saved_stderr, STDERR_FILENO) >= 0);
		unit_ok(close(saved_stderr));
	}

	rewind(capture);
	while (fgets(line, sizeof (line), capture) != NULL) {
		before |= strcmp(line, "before trace\n") == 0;
		trace |= strcmp(line, "Call trace:\n") == 0;
		after |= strcmp(line, "after trace\n") == 0;
	}
	int error = ferror(capture);
	unit_ok(fclose(capture));
	unit_ok(error);
	unit_eq(before_written, 13);
	unit_eq(after_written, 12);
	unit_true(before);
	unit_true(trace);
	unit_true(after);
	return (MUNIT_OK);
#else
	(void) params;
	munit_log(MUNIT_LOG_INFO, "No native backtrace backend available");
	return (MUNIT_SKIP);
#endif
}

static const MunitParameterEnum destination_params[] = {
	UNIT_PARAM("destination", "stderr", "custom"),
	{ NULL, NULL },
};

static const MunitTest backtrace_tests[] = {
	UNIT_TEST("destination", test_backtrace_destination,
	    destination_params),
	{ 0 },
};

static const MunitSuite backtrace_suite = {
	"backtrace.",
	backtrace_tests,
	NULL,
	1,
	MUNIT_SUITE_OPTION_NONE,
};

int
main(int argc, char **argv)
{
	return (munit_suite_main(&backtrace_suite, NULL, argc, argv));
}
