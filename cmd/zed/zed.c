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

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include "zed.h"
#include "zed_conf.h"
#include "zed_event.h"
#include "zed_file.h"
#include "zed_log.h"

static volatile sig_atomic_t _got_exit = 0;
static volatile sig_atomic_t _got_hup = 0;

/*
 * Signal handler for SIGINT & SIGTERM.
 */
static void
_exit_handler(int signum)
{
	_got_exit = 1;
}

/*
 * Signal handler for SIGHUP.
 */
static void
_hup_handler(int signum)
{
	_got_hup = 1;
}

/*
 * Register signal handlers.
 */
static void
_setup_sig_handlers(void)
{
	struct sigaction sa;

	if (sigemptyset(&sa.sa_mask) < 0)
		zed_log_die("Failed to initialize sigset");

	sa.sa_flags = SA_RESTART;
	sa.sa_handler = SIG_IGN;

	if (sigaction(SIGPIPE, &sa, NULL) < 0)
		zed_log_die("Failed to ignore SIGPIPE");

	sa.sa_handler = _exit_handler;
	if (sigaction(SIGINT, &sa, NULL) < 0)
		zed_log_die("Failed to register SIGINT handler");

	if (sigaction(SIGTERM, &sa, NULL) < 0)
		zed_log_die("Failed to register SIGTERM handler");

	sa.sa_handler = _hup_handler;
	if (sigaction(SIGHUP, &sa, NULL) < 0)
		zed_log_die("Failed to register SIGHUP handler");
}

/*
 * Lock all current and future pages in the virtual memory address space.
 *   Access to locked pages will never be delayed by a page fault.
 * EAGAIN is tested up to max_tries in case this is a transient error.
 */
static void
_lock_memory(void)
{
#if HAVE_MLOCKALL
	int i = 0;
	const int max_tries = 10;

	for (i = 0; i < max_tries; i++) {
		if (mlockall(MCL_CURRENT | MCL_FUTURE) == 0) {
			zed_log_msg(LOG_INFO, "Locked all pages in memory");
			return;
		}
		if (errno != EAGAIN)
			break;
	}
	zed_log_die("Failed to lock memory pages: %s", strerror(errno));

#else /* HAVE_MLOCKALL */
	zed_log_die("Failed to lock memory pages: mlockall() not supported");
#endif /* HAVE_MLOCKALL */
}

/*
 * Transform the process into a daemon.
 */
static void
_become_daemon(void)
{
	pid_t pid;
	int fd;

	pid = fork();
	if (pid < 0) {
		zed_log_die("Failed to create child process: %s",
		    strerror(errno));
	} else if (pid > 0) {
		_exit(EXIT_SUCCESS);
	}
	if (setsid() < 0)
		zed_log_die("Failed to create new session: %s",
		    strerror(errno));

	pid = fork();
	if (pid < 0) {
		zed_log_die("Failed to create grandchild process: %s",
		    strerror(errno));
	} else if (pid > 0) {
		_exit(EXIT_SUCCESS);
	}
	fd = open("/dev/null", O_RDWR);

	if (fd < 0)
		zed_log_die("Failed to open /dev/null: %s", strerror(errno));

	if (dup2(fd, STDIN_FILENO) < 0)
		zed_log_die("Failed to dup /dev/null onto stdin: %s",
		    strerror(errno));

	if (dup2(fd, STDOUT_FILENO) < 0)
		zed_log_die("Failed to dup /dev/null onto stdout: %s",
		    strerror(errno));

	if (dup2(fd, STDERR_FILENO) < 0)
		zed_log_die("Failed to dup /dev/null onto stderr: %s",
		    strerror(errno));

	if (close(fd) < 0)
		zed_log_die("Failed to close /dev/null: %s", strerror(errno));
}

/*
 * ZFS Event Daemon (ZED).
 */
int
main(int argc, char *argv[])
{
	struct zed_conf *zcp;
	uint64_t saved_eid;
	int64_t saved_etime[2];

	zed_log_init(argv[0]);
	zed_log_stderr_open(LOG_NOTICE);
	zcp = zed_conf_create();
	zed_conf_parse_opts(zcp, argc, argv);
	if (zcp->do_verbose)
		zed_log_stderr_open(LOG_INFO);

	if (geteuid() != 0)
		zed_log_die("Must be run as root");

	(void) umask(0);

	_setup_sig_handlers();

	zed_conf_parse_file(zcp);

	zed_file_close_from(STDERR_FILENO + 1);

	if (chdir("/") < 0)
		zed_log_die("Failed to change to root directory");

	if (zed_conf_scan_dir(zcp) < 0)
		exit(EXIT_FAILURE);

	if (zcp->do_memlock)
		_lock_memory();

	if (!zcp->do_foreground) {
		_become_daemon();
		zed_log_syslog_open(LOG_DAEMON);
		zed_log_stderr_close();
	}
	zed_log_msg(LOG_NOTICE,
	    "ZFS Event Daemon %s-%s", ZFS_META_VERSION, ZFS_META_RELEASE);

	(void) zed_conf_write_pid(zcp);

	if (zed_conf_open_state(zcp) < 0)
		exit(EXIT_FAILURE);

	if (zed_conf_read_state(zcp, &saved_eid, saved_etime) < 0)
		exit(EXIT_FAILURE);

	zed_event_init(zcp);
	zed_event_seek(zcp, saved_eid, saved_etime);

	while (!_got_exit) {
		if (_got_hup) {
			_got_hup = 0;
			(void) zed_conf_scan_dir(zcp);
		}
		zed_event_service(zcp);
	}
	zed_log_msg(LOG_NOTICE, "Exiting");
	zed_event_fini(zcp);
	zed_conf_destroy(zcp);
	zed_log_fini();
	exit(EXIT_SUCCESS);
}
