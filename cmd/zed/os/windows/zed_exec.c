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
#ifdef _WIN32
	HANDLE processHandle;
	DWORD processId;
#else
	pid_t pid;
#endif
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

	p1 = ((const struct launched_process_node *) x1)->processId;
	p2 = ((const struct launched_process_node *) x2)->processId;

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

// Compare function for qsort
int
compareEnvStrings(const void *a, const void *b)
{
	return (strcmp(*(const char **)a, *(const char **)b));
}

char *
env_get_merge_sort(char *unix[])
{
	// How many Unix entries?
	int unix_num = 0;
	int unix_strlen = 0;

	for (unix_num = 0; unix[unix_num]; unix_num++)
		unix_strlen += strlen(unix[unix_num]) + 1;

	// Get ENV (Windows style)
	LPCH envStrings = GetEnvironmentStrings();
	if (!envStrings)
		return (NULL);

	// How many Windows keys=value
	int win_num = 0;
	char *envPtr = envStrings;

	// Count the number of environment variables
	while (*envPtr) {
		win_num++;
		envPtr += strlen(envPtr) + 1; // Move to the next variable
	}
	int win_strlen = envPtr - envStrings;

	// Get me an array to hold all key=value
	char **envArray = (char **)malloc((unix_num + win_num + 1) *
	    sizeof (char *)); // +1 for NULL

	// Copy over Unix pointers
	int index = 0;

	for (int i = 0; i < unix_num; i++)
		envArray[index++] = unix[i];

	// Build Win entries
	envPtr = envStrings;

	// Count the number of environment variables
	while (*envPtr) {
		envArray[index++] = envPtr;
		envPtr += strlen(envPtr) + 1; // Move to the next variable
	}
	envArray[index] = NULL; // No index++, dont sort NULL

	// Sort the array of pointers
	qsort(envArray, index, sizeof (char *), compareEnvStrings);

	// Allocate output string
	char *output = malloc(unix_strlen + win_strlen + 1);
	if (!output) {
		FreeEnvironmentStrings(envStrings);
		return (NULL);
	}
	int output_index = 0;

	for (int i = 0; i < index; i++) {
		int len;
		len = strlen(envArray[i]);
		memcpy(&output[output_index],
		    envArray[i],
		    len);
		output_index += len;
		output[output_index++] = 0;
	}
	output[output_index++] = 0;

	free(envArray);
	FreeEnvironmentStrings(envStrings);
	return (output);
}

void
launch_process(const char *path, const char *prog, char *env[],
    struct launched_process_node *node)
{
	STARTUPINFO si;
	PROCESS_INFORMATION pi;
	ZeroMemory(&si, sizeof (si));
	si.cb = sizeof (si);
	ZeroMemory(&pi, sizeof (pi));
	char cmd[MAX_PATH];
	char *envBlock;

	fprintf(stderr, "Launching process '%s' in '%s'\r\n", prog, path);
	fflush(stderr);

	snprintf(cmd, sizeof (cmd),
	    "powershell.exe -ExecutionPolicy Bypass -File \"%s\"", path);
	/*
	 * ENV:
	 * Unix is an array of strings, will last pointer NULL.
	 * Win is a multi-string, with double 0 character at end.
	 * "HOME=/User/lundman\0SHELL=/bin/bash\0\0".
	 * We also need to copy *this* process ENV for CWDs, AND
	 * the env has to be *sorted* or it won't work.
	 */

	char *sortedMergedEnv = env_get_merge_sort(env);

	if (!CreateProcess(NULL, cmd, NULL, NULL, FALSE, 0, sortedMergedEnv,
	    NULL, &si, &pi)) {
		zed_log_msg(LOG_WARNING,
		    "Failed to create process \"%s\": %d %s",
		    prog, GetLastError(), strerror(GetLastError()));
		if (sortedMergedEnv)
			free(sortedMergedEnv);
		return;
	}
	if (sortedMergedEnv)
		free(sortedMergedEnv);

	node->processHandle = pi.hProcess;
	node->processId = pi.dwProcessId;
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

	/* parent process */
	node = calloc(1, sizeof (*node));

	if (node) {
		// fills in node
		launch_process(path, prog, env, node);

		node->eid = eid;
		node->name = strdup(prog);
		if (node->name == NULL) {
			perror("strdup");
			exit(EXIT_FAILURE); // eeeewwww
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

#define	MAX_CHILD_PROCESSES 64
	struct launched_process_node *node, *finishedNode;
	DWORD waitResult, exitCode;
	HANDLE handles[MAX_CHILD_PROCESSES];
	struct launched_process_node *nodes[MAX_CHILD_PROCESSES];
	size_t handleCount;

	while (!_reap_children_stop) {
		// Build the list of handles
		handleCount = 0;
		pthread_mutex_lock(&_launched_processes_lock);
		for (node = avl_first(&_launched_processes); node != NULL;
		    node = AVL_NEXT(&_launched_processes, node)) {
			if (handleCount < MAX_CHILD_PROCESSES) {
				handles[handleCount] = node->processHandle;
				nodes[handleCount] = node;
				handleCount++;
			} else {
				// Log warning if too many child processes
				zed_log_msg(LOG_WARNING,
				    "Too many child processes to track");
				break;
			}
		}
		pthread_mutex_unlock(&_launched_processes_lock);

		if (handleCount == 0) {
			Sleep(1000);  // No child processes to wait for
			continue;
		}

		// Wait for any process to finish
		waitResult = WaitForMultipleObjects((DWORD)handleCount,
		    handles, FALSE, INFINITE);
		if (waitResult >= WAIT_OBJECT_0 && waitResult <
		    WAIT_OBJECT_0 + handleCount) {
			size_t index = waitResult - WAIT_OBJECT_0;
			finishedNode = nodes[index];

			// Get process exit code
			if (GetExitCodeProcess(finishedNode->processHandle,
			    &exitCode)) {
				zed_log_msg(LOG_INFO,
				    "Process finished: %s, exitCode=%lu",
				    finishedNode->name, exitCode);
			}

			// Remove node from AVL tree
			pthread_mutex_lock(&_launched_processes_lock);
			avl_remove(&_launched_processes, finishedNode);
			pthread_mutex_unlock(&_launched_processes_lock);
			__atomic_add_fetch(&_launched_processes_limit, 1,
			    __ATOMIC_SEQ_CST);

			// Cleanup
			CloseHandle(finishedNode->processHandle);
			free(finishedNode->name);
			free(finishedNode);
		} else if (waitResult == WAIT_FAILED) {
			zed_log_msg(LOG_ERR,
			    "WaitForMultipleObjects failed: %lu",
			    GetLastError());
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

static struct zed_conf *_zcp = NULL;
void main_loop(struct zed_conf *);

static SERVICE_STATUS ServiceStatus;
static SERVICE_STATUS_HANDLE hStatus;

// Service Control Handler
static void WINAPI
ServiceControlHandler(DWORD controlCode)
{
	switch (controlCode) {
	case SERVICE_CONTROL_STOP:
		ServiceStatus.dwCurrentState = SERVICE_STOP_PENDING;
		// Perform cleanup
		ServiceStatus.dwCurrentState = SERVICE_STOPPED;
		SetServiceStatus(hStatus, &ServiceStatus);
		break;
	default:
		break;
	}
}

static void WINAPI
ServiceMain(DWORD argc, LPTSTR *argv)
{

	ServiceStatus.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
	ServiceStatus.dwCurrentState = SERVICE_START_PENDING;
	ServiceStatus.dwControlsAccepted = SERVICE_ACCEPT_STOP;
	hStatus = RegisterServiceCtrlHandler(TEXT("OpenZFS_zed"),
	    ServiceControlHandler);
	if (!hStatus)
		return;

	ServiceStatus.dwCurrentState = SERVICE_RUNNING;
	SetServiceStatus(hStatus, &ServiceStatus);

	// Service logic here, e.g., initializing zed's main functionality
	// while (ServiceStatus.dwCurrentState == SERVICE_RUNNING) {
	main_loop(_zcp);
	// }
}

void
win_run_loop(struct zed_conf *zcp)
{
	SERVICE_TABLE_ENTRY ServiceTable[] = {
	    { TEXT("OpenZFS_zed"), (LPSERVICE_MAIN_FUNCTION) ServiceMain },
	    { NULL, NULL }
	};

	/* Weirdly, Windows has no way to pass a void * */
	_zcp = zcp;

	/* Blocks until Service is stopped */
	if (!StartServiceCtrlDispatcher(ServiceTable)) {
		fprintf(stderr, "error %d\r\n", GetLastError()); fflush(stderr);
	}

	exit(EXIT_SUCCESS);
}
