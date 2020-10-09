/*
 * This file is part of the ZFS Event Daemon (ZED).
 *
 * Developed at Lawrence Livermore National Laboratory (LLNL-CODE-403049).
 * Copyright (C) 2013-2014 Lawrence Livermore National Security, LLC.
 * Refer to the ZoL git commit log for authoritative copyright attribution.
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License Version 1.0 (CDDL-1.0).
 * You can obtain a copy of the license from the top-level file
 * "OPENSOLARIS.LICENSE" or at <http://opensource.org/licenses/CDDL-1.0>.
 * You may not use this file except in compliance with the license.
 */

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include "zed_exec.h"
#include "zed_file.h"
#include "zed_log.h"
#include "zed_strings.h"

#define	ZEVENT_FILENO	3

/*
 * Create an environment string array for passing to execve() using the
 * NAME=VALUE strings in container [zsp].
 * Return a newly-allocated environment, or NULL on error.
 */
static char **
_zed_exec_create_env(zed_strings_t *zsp)
{
	int num_ptrs;
	int buflen;
	char *buf;
	char **pp;
	char *p;
	const char *q;
	int i;
	int len;

	num_ptrs = zed_strings_count(zsp) + 1;
	buflen = num_ptrs * sizeof (char *);
	for (q = zed_strings_first(zsp); q; q = zed_strings_next(zsp))
		buflen += strlen(q) + 1;

	buf = calloc(1, buflen);
	if (!buf)
		return (NULL);

	pp = (char **)buf;
	p = buf + (num_ptrs * sizeof (char *));
	i = 0;
	for (q = zed_strings_first(zsp); q; q = zed_strings_next(zsp)) {
		pp[i] = p;
		len = strlen(q) + 1;
		memcpy(p, q, len);
		p += len;
		i++;
	}
	pp[i] = NULL;
	assert(buf + buflen == p);
	return ((char **)buf);
}

/*
 * Fork a child process to handle event [eid].  The program [prog]
 * in directory [dir] is executed with the environment [env].
 *
 * The file descriptor [zfd] is the zevent_fd used to track the
 * current cursor location within the zevent nvlist.
 */
static void
_zed_exec_fork_child(uint64_t eid, const char *dir, const char *prog,
    char *env[], int zfd)
{
	char path[PATH_MAX];
	int n;
	pid_t pid;
	int fd;
	pid_t wpid;
	int status;

	assert(dir != NULL);
	assert(prog != NULL);
	assert(env != NULL);
	assert(zfd >= 0);

	n = snprintf(path, sizeof (path), "%s/%s", dir, prog);
	if ((n < 0) || (n >= sizeof (path))) {
		zed_log_msg(LOG_WARNING,
		    "Failed to fork \"%s\" for eid=%llu: %s",
		    prog, eid, strerror(ENAMETOOLONG));
		return;
	}
	pid = fork();
	if (pid < 0) {
		zed_log_msg(LOG_WARNING,
		    "Failed to fork \"%s\" for eid=%llu: %s",
		    prog, eid, strerror(errno));
		return;
	} else if (pid == 0) {
		(void) umask(022);
		if ((fd = open("/dev/null", O_RDWR)) != -1) {
			(void) dup2(fd, STDIN_FILENO);
			(void) dup2(fd, STDOUT_FILENO);
			(void) dup2(fd, STDERR_FILENO);
		}
		(void) dup2(zfd, ZEVENT_FILENO);
		zed_file_close_from(ZEVENT_FILENO + 1);
		execle(path, prog, NULL, env);
		_exit(127);
	}

	/* parent process */

	zed_log_msg(LOG_INFO, "Invoking \"%s\" eid=%llu pid=%d",
	    prog, eid, pid);

	/* FIXME: Timeout rogue child processes with sigalarm? */

	/*
	 * Wait for child process using WNOHANG to limit
	 * the time spent waiting to 10 seconds (10,000ms).
	 */
	for (n = 0; n < 1000; n++) {
		wpid = waitpid(pid, &status, WNOHANG);
		if (wpid == (pid_t)-1) {
			if (errno == EINTR)
				continue;
			zed_log_msg(LOG_WARNING,
			    "Failed to wait for \"%s\" eid=%llu pid=%d",
			    prog, eid, pid);
			break;
		} else if (wpid == 0) {
			struct timespec t;

			/* child still running */
			t.tv_sec = 0;
			t.tv_nsec = 10000000;	/* 10ms */
			(void) nanosleep(&t, NULL);
			continue;
		}

		if (WIFEXITED(status)) {
			zed_log_msg(LOG_INFO,
			    "Finished \"%s\" eid=%llu pid=%d exit=%d",
			    prog, eid, pid, WEXITSTATUS(status));
		} else if (WIFSIGNALED(status)) {
			zed_log_msg(LOG_INFO,
			    "Finished \"%s\" eid=%llu pid=%d sig=%d/%s",
			    prog, eid, pid, WTERMSIG(status),
			    strsignal(WTERMSIG(status)));
		} else {
			zed_log_msg(LOG_INFO,
			    "Finished \"%s\" eid=%llu pid=%d status=0x%X",
			    prog, eid, (unsigned int) status);
		}
		break;
	}

	/*
	 * kill child process after 10 seconds
	 */
	if (wpid == 0) {
		zed_log_msg(LOG_WARNING, "Killing hung \"%s\" pid=%d",
		    prog, pid);
		(void) kill(pid, SIGKILL);
	}
}

/*
 * Process the event [eid] by synchronously invoking all zedlets with a
 * matching class prefix.
 *
 * Each executable in [zedlets] from the directory [dir] is matched against
 * the event's [class], [subclass], and the "all" class (which matches
 * all events).  Every zedlet with a matching class prefix is invoked.
 * The NAME=VALUE strings in [envs] will be passed to the zedlet as
 * environment variables.
 *
 * The file descriptor [zfd] is the zevent_fd used to track the
 * current cursor location within the zevent nvlist.
 *
 * Return 0 on success, -1 on error.
 */
int
zed_exec_process(uint64_t eid, const char *class, const char *subclass,
    const char *dir, zed_strings_t *zedlets, zed_strings_t *envs, int zfd)
{
	const char *class_strings[4];
	const char *allclass = "all";
	const char **csp;
	const char *z;
	char **e;
	int n;

	if (!dir || !zedlets || !envs || zfd < 0)
		return (-1);

	csp = class_strings;

	if (class)
		*csp++ = class;

	if (subclass)
		*csp++ = subclass;

	if (allclass)
		*csp++ = allclass;

	*csp = NULL;

	e = _zed_exec_create_env(envs);

	for (z = zed_strings_first(zedlets); z; z = zed_strings_next(zedlets)) {
		for (csp = class_strings; *csp; csp++) {
			n = strlen(*csp);
			if ((strncmp(z, *csp, n) == 0) && !isalpha(z[n]))
				_zed_exec_fork_child(eid, dir, z, e, zfd);
		}
	}
	free(e);
	return (0);
}
