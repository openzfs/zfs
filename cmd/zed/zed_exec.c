// SPDX-License-Identifier: CDDL-1.0
/*
 * This file is part of the ZFS Event Daemon (ZED).
 *
 * Developed at Lawrence Livermore National Laboratory (LLNL-CODE-403049).
 * Copyright (C) 2013-2014 Lawrence Livermore National Security, LLC.
 * Refer to the OpenZFS git commit log for authoritative copyright attribution.
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
#include <stddef.h>
#include <sys/avl.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <pthread.h>
#include <signal.h>

#include "zed_exec.h"
#include "zed_log.h"
#include "zed_strings.h"

#define	ZEVENT_FILENO	3

struct launched_process_node {
	avl_node_t node;
	pid_t pid;
	uint64_t eid;
	char *name;
};

static int
_launched_process_node_compare(const void *x1, const void *x2)
{
	pid_t p1;
	pid_t p2;

	assert(x1 != NULL);
	assert(x2 != NULL);

	p1 = ((const struct launched_process_node *) x1)->pid;
	p2 = ((const struct launched_process_node *) x2)->pid;

	if (p1 < p2)
		return (-1);
	else if (p1 == p2)
		return (0);
	else
		return (1);
}

static pthread_t _reap_children_tid = (pthread_t)-1;
static volatile boolean_t _reap_children_stop;
static avl_tree_t _launched_processes;
static pthread_mutex_t _launched_processes_lock = PTHREAD_MUTEX_INITIALIZER;
static int16_t _launched_processes_limit;

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
    char *env[], int zfd, boolean_t in_foreground)
{
	char path[PATH_MAX];
	int n;
	pid_t pid;
	int fd;
	struct launched_process_node *node;
	sigset_t mask;
	struct timespec launch_timeout =
		{ .tv_sec = 0, .tv_nsec = 200 * 1000 * 1000, };

	assert(dir != NULL);
	assert(prog != NULL);
	assert(env != NULL);
	assert(zfd >= 0);

	while (__atomic_load_n(&_launched_processes_limit,
	    __ATOMIC_SEQ_CST) <= 0)
		(void) nanosleep(&launch_timeout, NULL);

	n = snprintf(path, sizeof (path), "%s/%s", dir, prog);
	if ((n < 0) || (n >= sizeof (path))) {
		zed_log_msg(LOG_WARNING,
		    "Failed to fork \"%s\" for eid=%llu: %s",
		    prog, eid, strerror(ENAMETOOLONG));
		return;
	}
	(void) pthread_mutex_lock(&_launched_processes_lock);
	pid = fork();
	if (pid < 0) {
		(void) pthread_mutex_unlock(&_launched_processes_lock);
		zed_log_msg(LOG_WARNING,
		    "Failed to fork \"%s\" for eid=%llu: %s",
		    prog, eid, strerror(errno));
		return;
	} else if (pid == 0) {
		(void) sigemptyset(&mask);
		(void) sigprocmask(SIG_SETMASK, &mask, NULL);

		(void) umask(022);
		if (in_foreground && /* we're already devnulled if daemonised */
		    (fd = open("/dev/null", O_RDWR | O_CLOEXEC)) != -1) {
			(void) dup2(fd, STDIN_FILENO);
			(void) dup2(fd, STDOUT_FILENO);
			(void) dup2(fd, STDERR_FILENO);
		}
		(void) dup2(zfd, ZEVENT_FILENO);
		execle(path, prog, NULL, env);
		_exit(127);
	}

	/* parent process */

	node = calloc(1, sizeof (*node));
	if (node) {
		node->pid = pid;
		node->eid = eid;
		node->name = strdup(prog);
		if (node->name == NULL) {
			perror("strdup");
			exit(EXIT_FAILURE);
		}

		avl_add(&_launched_processes, node);
	}
	(void) pthread_mutex_unlock(&_launched_processes_lock);

	__atomic_sub_fetch(&_launched_processes_limit, 1, __ATOMIC_SEQ_CST);
	zed_log_msg(LOG_INFO, "Invoking \"%s\" eid=%llu pid=%d",
	    prog, eid, pid);
}

static void
_nop(int sig)
{
	(void) sig;
}

static void *
_reap_children(void *arg)
{
	(void) arg;
	struct launched_process_node node, *pnode;
	pid_t pid;
	int status;
	struct rusage usage;
	struct sigaction sa = {};

	(void) sigfillset(&sa.sa_mask);
	(void) sigdelset(&sa.sa_mask, SIGCHLD);
	(void) pthread_sigmask(SIG_SETMASK, &sa.sa_mask, NULL);

	(void) sigemptyset(&sa.sa_mask);
	sa.sa_handler = _nop;
	sa.sa_flags = SA_NOCLDSTOP;
	(void) sigaction(SIGCHLD, &sa, NULL);

	for (_reap_children_stop = B_FALSE; !_reap_children_stop; ) {
		(void) pthread_mutex_lock(&_launched_processes_lock);
		pid = wait4(0, &status, WNOHANG, &usage);

		if (pid == 0 || pid == (pid_t)-1) {
			(void) pthread_mutex_unlock(&_launched_processes_lock);
			if (pid == 0 || errno == ECHILD)
				pause();
			else if (errno != EINTR)
				zed_log_msg(LOG_WARNING,
				    "Failed to wait for children: %s",
				    strerror(errno));
		} else {
			memset(&node, 0, sizeof (node));
			node.pid = pid;
			pnode = avl_find(&_launched_processes, &node, NULL);
			if (pnode) {
				memcpy(&node, pnode, sizeof (node));

				avl_remove(&_launched_processes, pnode);
				free(pnode);
			}
			(void) pthread_mutex_unlock(&_launched_processes_lock);
			__atomic_add_fetch(&_launched_processes_limit, 1,
			    __ATOMIC_SEQ_CST);

			usage.ru_utime.tv_sec += usage.ru_stime.tv_sec;
			usage.ru_utime.tv_usec += usage.ru_stime.tv_usec;
			usage.ru_utime.tv_sec +=
			    usage.ru_utime.tv_usec / (1000 * 1000);
			usage.ru_utime.tv_usec %= 1000 * 1000;

			if (WIFEXITED(status)) {
				zed_log_msg(LOG_INFO,
				    "Finished \"%s\" eid=%llu pid=%d "
				    "time=%llu.%06us exit=%d",
				    node.name, node.eid, pid,
				    (unsigned long long) usage.ru_utime.tv_sec,
				    (unsigned int) usage.ru_utime.tv_usec,
				    WEXITSTATUS(status));
			} else if (WIFSIGNALED(status)) {
				zed_log_msg(LOG_INFO,
				    "Finished \"%s\" eid=%llu pid=%d "
				    "time=%llu.%06us sig=%d/%s",
				    node.name, node.eid, pid,
				    (unsigned long long) usage.ru_utime.tv_sec,
				    (unsigned int) usage.ru_utime.tv_usec,
				    WTERMSIG(status),
				    strsignal(WTERMSIG(status)));
			} else {
				zed_log_msg(LOG_INFO,
				    "Finished \"%s\" eid=%llu pid=%d "
				    "time=%llu.%06us status=0x%X",
				    node.name, node.eid, pid,
				    (unsigned long long) usage.ru_utime.tv_sec,
				    (unsigned int) usage.ru_utime.tv_usec,
				    (unsigned int) status);
			}

			free(node.name);
		}
	}

	return (NULL);
}

void
zed_exec_fini(void)
{
	struct launched_process_node *node;
	void *ck = NULL;

	if (_reap_children_tid == (pthread_t)-1)
		return;

	_reap_children_stop = B_TRUE;
	(void) pthread_kill(_reap_children_tid, SIGCHLD);
	(void) pthread_join(_reap_children_tid, NULL);

	while ((node = avl_destroy_nodes(&_launched_processes, &ck)) != NULL) {
		free(node->name);
		free(node);
	}
	avl_destroy(&_launched_processes);

	(void) pthread_mutex_destroy(&_launched_processes_lock);
	(void) pthread_mutex_init(&_launched_processes_lock, NULL);

	_reap_children_tid = (pthread_t)-1;
}

/*
 * Process the event [eid] by synchronously invoking all zedlets with a
 * matching class prefix.
 *
 * Each executable in [zcp->zedlets] from the directory [zcp->zedlet_dir]
 * is matched against the event's [class], [subclass], and the "all" class
 * (which matches all events).
 * Every zedlet with a matching class prefix is invoked.
 * The NAME=VALUE strings in [envs] will be passed to the zedlet as
 * environment variables.
 *
 * The file descriptor [zcp->zevent_fd] is the zevent_fd used to track the
 * current cursor location within the zevent nvlist.
 *
 * Return 0 on success, -1 on error.
 */
int
zed_exec_process(uint64_t eid, const char *class, const char *subclass,
    struct zed_conf *zcp, zed_strings_t *envs)
{
	const char *class_strings[4];
	const char *allclass = "all";
	const char **csp;
	const char *z;
	char **e;
	int n;

	if (!zcp->zedlet_dir || !zcp->zedlets || !envs || zcp->zevent_fd < 0)
		return (-1);

	if (_reap_children_tid == (pthread_t)-1) {
		_launched_processes_limit = zcp->max_jobs;

		if (pthread_create(&_reap_children_tid, NULL,
		    _reap_children, NULL) != 0)
			return (-1);
		pthread_setname_np(_reap_children_tid, "reap ZEDLETs");

		avl_create(&_launched_processes, _launched_process_node_compare,
		    sizeof (struct launched_process_node),
		    offsetof(struct launched_process_node, node));
	}

	csp = class_strings;

	if (class)
		*csp++ = class;

	if (subclass)
		*csp++ = subclass;

	if (allclass)
		*csp++ = allclass;

	*csp = NULL;

	e = _zed_exec_create_env(envs);

	for (z = zed_strings_first(zcp->zedlets); z;
	    z = zed_strings_next(zcp->zedlets)) {
		for (csp = class_strings; *csp; csp++) {
			n = strlen(*csp);
			if ((strncmp(z, *csp, n) == 0) && !isalpha(z[n]))
				_zed_exec_fork_child(eid, zcp->zedlet_dir,
				    z, e, zcp->zevent_fd, zcp->do_foreground);
		}
	}
	free(e);
	return (0);
}
