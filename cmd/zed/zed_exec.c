/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license from the top-level
 * OPENSOLARIS.LICENSE or <http://opensource.org/licenses/CDDL-1.0>.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each file
 * and include the License file from the top-level OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */

/*
 * Developed at Lawrence Livermore National Laboratory (LLNL-CODE-403049).
 * Copyright (C) 2013-2014 Lawrence Livermore National Security, LLC.
 */

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
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

	buf = malloc(buflen);
	if (!buf)
		return (NULL);

	pp = (char **) buf;
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
	return ((char **) buf);
}

/*
 * Fork a child process to handle event [eid].  The program [prog]
 * in directory [dir] is executed with the envionment [env].
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
		fd = open("/dev/null", O_RDWR);
		(void) dup2(fd, STDIN_FILENO);
		(void) dup2(fd, STDOUT_FILENO);
		(void) dup2(fd, STDERR_FILENO);
		(void) dup2(zfd, ZEVENT_FILENO);
		zed_file_close_from(ZEVENT_FILENO + 1);
		execle(path, prog, NULL, env);
		_exit(127);
	} else {
		zed_log_msg(LOG_INFO, "Invoking \"%s\" eid=%llu pid=%d",
		    prog, eid, pid);
		/* FIXME: Timeout rogue child processes with sigalarm? */
restart:
		wpid = waitpid(pid, &status, 0);
		if (wpid == (pid_t) -1) {
			if (errno == EINTR)
				goto restart;
			zed_log_msg(LOG_WARNING,
			    "Failed to wait for \"%s\" eid=%llu pid=%d",
			    prog, eid, pid);
		} else if (WIFEXITED(status)) {
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
	}
}

/*
 * Process the event [eid] by synchronously invoking all scripts with a
 * matching class prefix.
 *
 * Each executable in [scripts] from the directory [dir] is matched against
 * the event's [class], [subclass], and the "all" class (which matches
 * all events).  Every script with a matching class prefix is invoked.
 * The NAME=VALUE strings in [envs] will be passed to the script as
 * environment variables.
 *
 * The file descriptor [zfd] is the zevent_fd used to track the
 * current cursor location within the zevent nvlist.
 *
 * Return 0 on success, -1 on error.
 */
int
zed_exec_process(uint64_t eid, const char *class, const char *subclass,
    const char *dir, zed_strings_t *scripts, zed_strings_t *envs, int zfd)
{
	const char *class_strings[4];
	const char *allclass = "all";
	const char **csp;
	const char *s;
	char **e;
	int n;

	if (!dir || !scripts || !envs || zfd < 0)
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

	for (s = zed_strings_first(scripts); s; s = zed_strings_next(scripts)) {
		for (csp = class_strings; *csp; csp++) {
			n = strlen(*csp);
			if ((strncmp(s, *csp, n) == 0) && !isalpha(s[n]))
				_zed_exec_fork_child(eid, dir, s, e, zfd);
		}
	}
	free(e);
	return (0);
}
