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

#include <sys/resource.h>
#include <sys/wait.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "unit.h"

#ifndef MUNIT_NO_FORK
static int
wait_for_child(pid_t pid)
{
	int status;
	pid_t waited;
	do {
		waited = waitpid(pid, &status, 0);
	} while (waited == -1 && errno == EINTR);
	munit_assert_int(waited, ==, pid);
	return (status);
}

static void
assert_diagnostic(FILE *output, const char *message)
{
	munit_assert_int(fseek(output, 0, SEEK_SET), ==, 0);
	char line[256];
	int found = 0;
	while (fgets(line, sizeof (line), output) != NULL) {
		if (strstr(line, message) != NULL)
			found = 1;
	}
	munit_assert_int(ferror(output), ==, 0);
	munit_assert_true(found);
}

static void
late_abort(void)
{
	(void) fprintf(stderr, "late child failure\n");
	abort();
}

static void
late_exit(void)
{
	(void) fprintf(stderr, "late child failure\n");
	_Exit(77);
}

static MunitResult
callback(const MunitParameter params[], void *data)
{
	(void) params;
	const char *mode = data;

	if (strcmp(mode, "body-abort") == 0)
		abort();
	if (strstr(mode, "late-abort") != NULL) {
		if (atexit(late_abort) != 0)
			return (MUNIT_ERROR);
	} else if (strcmp(mode, "late-exit") == 0) {
		if (atexit(late_exit) != 0)
			return (MUNIT_ERROR);
	}
	if (strstr(mode, "fail") != NULL)
		return (MUNIT_FAIL);
	if (strstr(mode, "error") != NULL)
		return (MUNIT_ERROR);
	return (MUNIT_OK);
}
#endif

static MunitResult
test_completion(const MunitParameter params[], void *data)
{
	(void) data;
#ifdef MUNIT_NO_FORK
	(void) params;
	return (MUNIT_SKIP);
#else
	const char *mode = munit_parameters_get(params, "completion");
	int todo = strcmp(mode, "success") != 0;
	int expected = strcmp(mode, "body-abort") == 0 ||
	    strcmp(mode, "fail") == 0 || !todo ? EXIT_SUCCESS : EXIT_FAILURE;
	FILE *output = tmpfile();
	munit_assert_not_null(output);
	munit_assert_int(fflush(NULL), ==, 0);
	pid_t pid = fork();
	munit_assert_int(pid, >=, 0);
	if (pid == 0) {
		struct rlimit limit = { 0, 0 };
		if (setrlimit(RLIMIT_CORE, &limit) != 0 ||
		    dup2(fileno(output), STDOUT_FILENO) == -1 ||
		    dup2(fileno(output), STDERR_FILENO) == -1)
			_Exit(99);
		MunitTest tests[] = {
			{ "callback", callback, NULL, NULL,
			    todo ? MUNIT_TEST_OPTION_TODO :
			    MUNIT_TEST_OPTION_NONE, NULL },
			{ 0 }
		};
		MunitSuite suite = {
			"inner.", tests, NULL, 1, MUNIT_SUITE_OPTION_NONE
		};
		char *args[] = { (char *)"test_munit", NULL };
		exit(munit_suite_main(&suite, (void *)mode, 1, args));
	}
	int status = wait_for_child(pid);
	munit_assert_true(WIFEXITED(status));
	munit_assert_int(WEXITSTATUS(status), ==, expected);
	if (strstr(mode, "late-") != NULL)
		assert_diagnostic(output, "late child failure");
	munit_assert_int(fclose(output), ==, 0);
	return (MUNIT_OK);
#endif
}

#ifndef MUNIT_NO_FORK
static MunitResult
output_failure(const MunitParameter params[], void *data)
{
	(void) params;
	int fd = *(int *)data;
	char byte = '\0';
	ssize_t len;
	if (write(STDOUT_FILENO, &byte, 1) != 1)
		return (MUNIT_ERROR);
	do {
		len = read(fd, &byte, 1);
	} while (len == -1 && errno == EINTR);
	if (len != 1)
		return (MUNIT_ERROR);
	(void) fprintf(stderr, "original test failure\n");
	return (MUNIT_FAIL);
}
#endif

static MunitResult
test_stderr_after_stdout_failure(const MunitParameter params[], void *data)
{
	(void) params, (void) data;
#ifdef MUNIT_NO_FORK
	return (MUNIT_SKIP);
#else
	int out[2];
	int release[2];
	munit_assert_int(pipe(out), ==, 0);
	munit_assert_int(pipe(release), ==, 0);
	FILE *error = tmpfile();
	munit_assert_not_null(error);
	munit_assert_int(fflush(NULL), ==, 0);
	pid_t pid = fork();
	munit_assert_int(pid, >=, 0);
	if (pid == 0) {
		if (close(out[0]) != 0 || close(release[1]) != 0 ||
		    dup2(out[1], STDOUT_FILENO) == -1 ||
		    close(out[1]) != 0 ||
		    dup2(fileno(error), STDERR_FILENO) == -1 ||
		    signal(SIGPIPE, SIG_IGN) == SIG_ERR)
			_Exit(99);
		MunitTest inner_tests[] = {
			{ "failure", output_failure, NULL, NULL,
			    MUNIT_TEST_OPTION_NONE, NULL },
			{ 0 }
		};
		MunitSuite inner_suite = {
			"inner.", inner_tests, NULL, 1, MUNIT_SUITE_OPTION_NONE
		};
		char *args[] = { (char *)"test_munit", NULL };
		exit(munit_suite_main(&inner_suite, &release[0], 1, args));
	}
	munit_assert_int(close(out[1]), ==, 0);
	munit_assert_int(close(release[0]), ==, 0);
	/* Wait for the callback before breaking the runner's output pipe. */
	char byte;
	ssize_t len;
	do {
		len = read(out[0], &byte, 1);
		if (len == -1 && errno == EINTR)
			continue;
		munit_assert_int(len, ==, 1);
	} while (len != 1 || byte != '\0');
	munit_assert_int(close(out[0]), ==, 0);
	munit_assert_int(write(release[1], "x", 1), ==, 1);
	munit_assert_int(close(release[1]), ==, 0);
	int status = wait_for_child(pid);
	munit_assert_true(WIFEXITED(status));
	munit_assert_int(WEXITSTATUS(status), ==, EXIT_FAILURE);
	assert_diagnostic(error, "original test failure");
	munit_assert_int(fclose(error), ==, 0);
	return (MUNIT_OK);
#endif
}

static MunitParameterEnum completion_params[] = {
	UNIT_PARAM("completion",
	    "success", "todo-success", "fail", "body-abort", "late-abort",
	    "fail-late-abort", "error-late-abort", "late-exit"),
	{ NULL, NULL }
};

static MunitTest tests[] = {
	{ "completion", test_completion, NULL, NULL, MUNIT_TEST_OPTION_NONE,
	    completion_params },
	{ "stderr_after_stdout_failure", test_stderr_after_stdout_failure,
	    NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL },
	{ 0 }
};

static MunitSuite suite = {
	"munit.", tests, NULL, 1, MUNIT_SUITE_OPTION_NONE
};

int
main(int argc, char *argv[])
{
	return (munit_suite_main(&suite, NULL, argc, argv));
}
