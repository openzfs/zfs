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
 * Access to locked pages will never be delayed by a page fault.
 *
 * EAGAIN is tested up to max_tries in case this is a transient error.
 *
 * Note that memory locks are not inherited by a child created via fork()
 * and are automatically removed during an execve().  As such, this must
 * be called after the daemon fork()s (when running in the background).
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
 * Start daemonization of the process including the double fork().
 *
 * The parent process will block here until _finish_daemonize() is called
 * (in the grandchild process), at which point the parent process will exit.
 * This prevents the parent process from exiting until initialization is
 * complete.
 */
static void
_start_daemonize(void)
{
	pid_t pid;
	struct sigaction sa;

	/* Create pipe for communicating with child during daemonization. */
	zed_log_pipe_open();

	/* Background process and ensure child is not process group leader. */
	pid = fork();
	if (pid < 0) {
		zed_log_die("Failed to create child process: %s",
		    strerror(errno));
	} else if (pid > 0) {

		/* Close writes since parent will only read from pipe. */
		zed_log_pipe_close_writes();

		/* Wait for notification that daemonization is complete. */
		zed_log_pipe_wait();

		zed_log_pipe_close_reads();
		_exit(EXIT_SUCCESS);
	}

	/* Close reads since child will only write to pipe. */
	zed_log_pipe_close_reads();

	/* Create independent session and detach from terminal. */
	if (setsid() < 0)
		zed_log_die("Failed to create new session: %s",
		    strerror(errno));

	/* Prevent child from terminating on HUP when session leader exits. */
	if (sigemptyset(&sa.sa_mask) < 0)
		zed_log_die("Failed to initialize sigset");

	sa.sa_flags = 0;
	sa.sa_handler = SIG_IGN;

	if (sigaction(SIGHUP, &sa, NULL) < 0)
		zed_log_die("Failed to ignore SIGHUP");

	/* Ensure process cannot re-acquire terminal. */
	pid = fork();
	if (pid < 0) {
		zed_log_die("Failed to create grandchild process: %s",
		    strerror(errno));
	} else if (pid > 0) {
		_exit(EXIT_SUCCESS);
	}
}

/*
 * Finish daemonization of the process by closing stdin/stdout/stderr.
 *
 * This must be called at the end of initialization after all external
 * communication channels are established and accessible.
 */
static void
_finish_daemonize(void)
{
	int devnull;

	/* Preserve fd 0/1/2, but discard data to/from stdin/stdout/stderr. */
	devnull = open("/dev/null", O_RDWR);
	if (devnull < 0)
		zed_log_die("Failed to open /dev/null: %s", strerror(errno));

	if (dup2(devnull, STDIN_FILENO) < 0)
		zed_log_die("Failed to dup /dev/null onto stdin: %s",
		    strerror(errno));

	if (dup2(devnull, STDOUT_FILENO) < 0)
		zed_log_die("Failed to dup /dev/null onto stdout: %s",
		    strerror(errno));

	if (dup2(devnull, STDERR_FILENO) < 0)
		zed_log_die("Failed to dup /dev/null onto stderr: %s",
		    strerror(errno));

	if ((devnull > STDERR_FILENO) && (close(devnull) < 0))
		zed_log_die("Failed to close /dev/null: %s", strerror(errno));

	/* Notify parent that daemonization is complete. */
	zed_log_pipe_close_writes();
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

	zed_conf_parse_file(zcp);

	zed_file_close_from(STDERR_FILENO + 1);

	(void) umask(0);

	if (chdir("/") < 0)
		zed_log_die("Failed to change to root directory");

	if (zed_conf_scan_dir(zcp) < 0)
		exit(EXIT_FAILURE);

	if (!zcp->do_foreground) {
		_start_daemonize();
		zed_log_syslog_open(LOG_DAEMON);
	}
	_setup_sig_handlers();

	if (zcp->do_memlock)
		_lock_memory();

	if ((zed_conf_write_pid(zcp) < 0) && (!zcp->do_force))
		exit(EXIT_FAILURE);

	if (!zcp->do_foreground)
		_finish_daemonize();

	zed_log_msg(LOG_NOTICE,
	    "ZFS Event Daemon %s-%s (PID %d)",
	    ZFS_META_VERSION, ZFS_META_RELEASE, (int)getpid());

	if (zed_conf_open_state(zcp) < 0)
		exit(EXIT_FAILURE);

	if (zed_conf_read_state(zcp, &saved_eid, saved_etime) < 0)
		exit(EXIT_FAILURE);

idle:
	/*
	 * If -I is specified, attempt to open /dev/zfs repeatedly until
	 * successful.
	 */
	do {
		if (!zed_event_init(zcp))
			break;
		/* Wait for some time and try again. tunable? */
		sleep(30);
	} while (!_got_exit && zcp->do_idle);

	if (_got_exit)
		goto out;

	zed_event_seek(zcp, saved_eid, saved_etime);

	while (!_got_exit) {
		int rv;
		if (_got_hup) {
			_got_hup = 0;
			(void) zed_conf_scan_dir(zcp);
		}
		rv = zed_event_service(zcp);

		/* ENODEV: When kernel module is unloaded (osx) */
		if (rv == ENODEV)
			break;
	}

	zed_log_msg(LOG_NOTICE, "Exiting");
	zed_event_fini(zcp);

	if (zcp->do_idle && !_got_exit)
		goto idle;

out:
	zed_conf_destroy(zcp);
	zed_log_fini();
	exit(EXIT_SUCCESS);
}
