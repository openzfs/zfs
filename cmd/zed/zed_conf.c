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
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <libgen.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include <unistd.h>
#include "zed.h"
#include "zed_conf.h"
#include "zed_file.h"
#include "zed_log.h"
#include "zed_strings.h"

/*
 * Return a new configuration with default values.
 */
struct zed_conf *
zed_conf_create(void)
{
	struct zed_conf *zcp;

	zcp = malloc(sizeof (*zcp));
	if (!zcp)
		goto nomem;

	memset(zcp, 0, sizeof (*zcp));

	zcp->syslog_facility = LOG_DAEMON;
	zcp->min_events = ZED_MIN_EVENTS;
	zcp->max_events = ZED_MAX_EVENTS;
	zcp->scripts = NULL;		/* created via zed_conf_scan_dir() */
	zcp->state_fd = -1;		/* opened via zed_conf_open_state() */
	zcp->zfs_hdl = NULL;		/* opened via zed_event_init() */
	zcp->zevent_fd = -1;		/* opened via zed_event_init() */

	if (!(zcp->conf_file = strdup(ZED_CONF_FILE)))
		goto nomem;

	if (!(zcp->pid_file = strdup(ZED_PID_FILE)))
		goto nomem;

	if (!(zcp->script_dir = strdup(ZED_SCRIPT_DIR)))
		goto nomem;

	if (!(zcp->state_file = strdup(ZED_STATE_FILE)))
		goto nomem;

	return (zcp);

nomem:
	zed_log_die("Failed to create conf: %s", strerror(errno));
	return (NULL);
}

/*
 * Destroy the configuration [zcp].
 *
 * Note: zfs_hdl & zevent_fd are destroyed via zed_event_fini().
 */
void
zed_conf_destroy(struct zed_conf *zcp)
{
	if (!zcp)
		return;

	if (zcp->state_fd >= 0) {
		if (close(zcp->state_fd) < 0)
			zed_log_msg(LOG_WARNING,
			    "Failed to close state file \"%s\": %s",
			    zcp->state_file, strerror(errno));
	}
	if (zcp->pid_file) {
		if ((unlink(zcp->pid_file) < 0) && (errno != ENOENT))
			zed_log_msg(LOG_WARNING,
			    "Failed to remove PID file \"%s\": %s",
			    zcp->pid_file, strerror(errno));
	}
	if (zcp->conf_file)
		free(zcp->conf_file);

	if (zcp->pid_file)
		free(zcp->pid_file);

	if (zcp->script_dir)
		free(zcp->script_dir);

	if (zcp->state_file)
		free(zcp->state_file);

	if (zcp->scripts)
		zed_strings_destroy(zcp->scripts);

	free(zcp);
}

/*
 * Display command-line help and exit.
 *
 * If [got_err] is 0, output to stdout and exit normally;
 * otherwise, output to stderr and exit with a failure status.
 */
static void
_zed_conf_display_help(const char *prog, int got_err)
{
	FILE *fp = got_err ? stderr : stdout;
	int w1 = 4;			/* width of leading whitespace */
	int w2 = 8;			/* width of L-justified option field */

	fprintf(fp, "Usage: %s [OPTION]...\n", (prog ? prog : "zed"));
	fprintf(fp, "\n");
	fprintf(fp, "%*c%*s %s\n", w1, 0x20, -w2, "-h",
	    "Display help.");
	fprintf(fp, "%*c%*s %s\n", w1, 0x20, -w2, "-L",
	    "Display license information.");
	fprintf(fp, "%*c%*s %s\n", w1, 0x20, -w2, "-V",
	    "Display version information.");
	fprintf(fp, "\n");
	fprintf(fp, "%*c%*s %s\n", w1, 0x20, -w2, "-v",
	    "Be verbose.");
	fprintf(fp, "%*c%*s %s\n", w1, 0x20, -w2, "-f",
	    "Force daemon to run.");
	fprintf(fp, "%*c%*s %s\n", w1, 0x20, -w2, "-F",
	    "Run daemon in the foreground.");
	fprintf(fp, "%*c%*s %s\n", w1, 0x20, -w2, "-M",
	    "Lock all pages in memory.");
	fprintf(fp, "%*c%*s %s\n", w1, 0x20, -w2, "-Z",
	    "Zero state file.");
	fprintf(fp, "\n");
#if 0
	fprintf(fp, "%*c%*s %s [%s]\n", w1, 0x20, -w2, "-c FILE",
	    "Read configuration from FILE.", ZED_CONF_FILE);
#endif
	fprintf(fp, "%*c%*s %s [%s]\n", w1, 0x20, -w2, "-d DIR",
	    "Read enabled scripts from DIR.", ZED_SCRIPT_DIR);
	fprintf(fp, "%*c%*s %s [%s]\n", w1, 0x20, -w2, "-p FILE",
	    "Write daemon's PID to FILE.", ZED_PID_FILE);
	fprintf(fp, "%*c%*s %s [%s]\n", w1, 0x20, -w2, "-s FILE",
	    "Write daemon's state to FILE.", ZED_STATE_FILE);
	fprintf(fp, "\n");

	exit(got_err ? EXIT_FAILURE : EXIT_SUCCESS);
}

/*
 * Display license information to stdout and exit.
 */
static void
_zed_conf_display_license(void)
{
	const char **pp;
	const char *text[] = {
	    "The ZFS Event Daemon (ZED) is distributed under the terms of the",
	    "  Common Development and Distribution License (CDDL-1.0)",
	    "  <http://opensource.org/licenses/CDDL-1.0>.",
	    "Developed at Lawrence Livermore National Laboratory"
	    " (LLNL-CODE-403049).",
	    "Copyright (C) 2013-2014"
	    " Lawrence Livermore National Security, LLC.",
	    "",
	    NULL
	};

	for (pp = text; *pp; pp++)
		printf("%s\n", *pp);

	exit(EXIT_SUCCESS);
}

/*
 * Display version information to stdout and exit.
 */
static void
_zed_conf_display_version(void)
{
	printf("%s-%s-%s\n",
	    ZFS_META_NAME, ZFS_META_VERSION, ZFS_META_RELEASE);

	exit(EXIT_SUCCESS);
}

/*
 * Copy the [path] string to the [resultp] ptr.
 * If [path] is not an absolute path, prefix it with the current working dir.
 * If [resultp] is non-null, free its existing string before assignment.
 */
static void
_zed_conf_parse_path(char **resultp, const char *path)
{
	char buf[PATH_MAX];

	assert(resultp != NULL);
	assert(path != NULL);

	if (*resultp)
		free(*resultp);

	if (path[0] == '/') {
		*resultp = strdup(path);
	} else if (!getcwd(buf, sizeof (buf))) {
		zed_log_die("Failed to get current working dir: %s",
		    strerror(errno));
	} else if (strlcat(buf, "/", sizeof (buf)) >= sizeof (buf)) {
		zed_log_die("Failed to copy path: %s", strerror(ENAMETOOLONG));
	} else if (strlcat(buf, path, sizeof (buf)) >= sizeof (buf)) {
		zed_log_die("Failed to copy path: %s", strerror(ENAMETOOLONG));
	} else {
		*resultp = strdup(buf);
	}
	if (!*resultp)
		zed_log_die("Failed to copy path: %s", strerror(ENOMEM));
}

/*
 * Parse the command-line options into the configuration [zcp].
 */
void
zed_conf_parse_opts(struct zed_conf *zcp, int argc, char **argv)
{
	const char * const opts = ":hLVc:d:p:s:vfFMZ";
	int opt;

	if (!zcp || !argv || !argv[0])
		zed_log_die("Failed to parse options: Internal error");

	opterr = 0;			/* suppress default getopt err msgs */

	while ((opt = getopt(argc, argv, opts)) != -1) {
		switch (opt) {
		case 'h':
			_zed_conf_display_help(argv[0], EXIT_SUCCESS);
			break;
		case 'L':
			_zed_conf_display_license();
			break;
		case 'V':
			_zed_conf_display_version();
			break;
		case 'c':
			_zed_conf_parse_path(&zcp->conf_file, optarg);
			break;
		case 'd':
			_zed_conf_parse_path(&zcp->script_dir, optarg);
			break;
		case 'p':
			_zed_conf_parse_path(&zcp->pid_file, optarg);
			break;
		case 's':
			_zed_conf_parse_path(&zcp->state_file, optarg);
			break;
		case 'v':
			zcp->do_verbose = 1;
			break;
		case 'f':
			zcp->do_force = 1;
			break;
		case 'F':
			zcp->do_foreground = 1;
			break;
		case 'M':
			zcp->do_memlock = 1;
			break;
		case 'Z':
			zcp->do_zero = 1;
			break;
		case '?':
		default:
			if (optopt == '?')
				_zed_conf_display_help(argv[0], EXIT_SUCCESS);

			fprintf(stderr, "%s: %s '-%c'\n\n", argv[0],
			    "Invalid option", optopt);
			_zed_conf_display_help(argv[0], EXIT_FAILURE);
			break;
		}
	}
}

/*
 * Parse the configuration file into the configuration [zcp].
 *
 * FIXME: Not yet implemented.
 */
void
zed_conf_parse_file(struct zed_conf *zcp)
{
	if (!zcp)
		zed_log_die("Failed to parse config: %s", strerror(EINVAL));
}

/*
 * Scan the [zcp] script_dir for files to exec based on the event class.
 * Files must be executable by user, but not writable by group or other.
 * Dotfiles are ignored.
 *
 * Return 0 on success with an updated set of scripts,
 * or -1 on error with errno set.
 *
 * FIXME: Check if script_dir and all parent dirs are secure.
 */
int
zed_conf_scan_dir(struct zed_conf *zcp)
{
	zed_strings_t *scripts;
	DIR *dirp;
	struct dirent *direntp;
	char pathname[PATH_MAX];
	struct stat st;
	int n;

	if (!zcp) {
		errno = EINVAL;
		zed_log_msg(LOG_ERR, "Failed to scan script dir: %s",
		    strerror(errno));
		return (-1);
	}
	scripts = zed_strings_create();
	if (!scripts) {
		errno = ENOMEM;
		zed_log_msg(LOG_WARNING, "Failed to scan dir \"%s\": %s",
		    zcp->script_dir, strerror(errno));
		return (-1);
	}
	dirp = opendir(zcp->script_dir);
	if (!dirp) {
		int errno_bak = errno;
		zed_log_msg(LOG_WARNING, "Failed to open dir \"%s\": %s",
		    zcp->script_dir, strerror(errno));
		zed_strings_destroy(scripts);
		errno = errno_bak;
		return (-1);
	}
	while ((direntp = readdir(dirp))) {
		if (direntp->d_name[0] == '.')
			continue;

		n = snprintf(pathname, sizeof (pathname),
		    "%s/%s", zcp->script_dir, direntp->d_name);
		if ((n < 0) || (n >= sizeof (pathname))) {
			zed_log_msg(LOG_WARNING, "Failed to stat \"%s\": %s",
			    direntp->d_name, strerror(ENAMETOOLONG));
			continue;
		}
		if (stat(pathname, &st) < 0) {
			zed_log_msg(LOG_WARNING, "Failed to stat \"%s\": %s",
			    pathname, strerror(errno));
			continue;
		}
		if (!S_ISREG(st.st_mode)) {
			zed_log_msg(LOG_INFO,
			    "Ignoring \"%s\": not a regular file",
			    direntp->d_name);
			continue;
		}
		if ((st.st_uid != 0) && !zcp->do_force) {
			zed_log_msg(LOG_NOTICE,
			    "Ignoring \"%s\": not owned by root",
			    direntp->d_name);
			continue;
		}
		if (!(st.st_mode & S_IXUSR)) {
			zed_log_msg(LOG_INFO,
			    "Ignoring \"%s\": not executable by user",
			    direntp->d_name);
			continue;
		}
		if ((st.st_mode & S_IWGRP) & !zcp->do_force) {
			zed_log_msg(LOG_NOTICE,
			    "Ignoring \"%s\": writable by group",
			    direntp->d_name);
			continue;
		}
		if ((st.st_mode & S_IWOTH) & !zcp->do_force) {
			zed_log_msg(LOG_NOTICE,
			    "Ignoring \"%s\": writable by other",
			    direntp->d_name);
			continue;
		}
		if (zed_strings_add(scripts, direntp->d_name) < 0) {
			zed_log_msg(LOG_WARNING,
			    "Failed to register \"%s\": %s",
			    direntp->d_name, strerror(errno));
			continue;
		}
		if (zcp->do_verbose)
			zed_log_msg(LOG_INFO,
			    "Registered script \"%s\"", direntp->d_name);
	}
	if (closedir(dirp) < 0) {
		int errno_bak = errno;
		zed_log_msg(LOG_WARNING, "Failed to close dir \"%s\": %s",
		    zcp->script_dir, strerror(errno));
		zed_strings_destroy(scripts);
		errno = errno_bak;
		return (-1);
	}
	if (zcp->scripts)
		zed_strings_destroy(zcp->scripts);

	zcp->scripts = scripts;
	return (0);
}

/*
 * Write the PID file specified in [zcp].
 * Return 0 on success, -1 on error.
 *
 * This must be called after fork()ing to become a daemon (so the correct PID
 * is recorded), but before daemonization is complete and the parent process
 * exits (for synchronization with systemd).
 *
 * FIXME: Only update the PID file after verifying the PID previously stored
 * in the PID file no longer exists or belongs to a foreign process
 * in order to ensure the daemon cannot be started more than once.
 * (This check is currently done by zed_conf_open_state().)
 */
int
zed_conf_write_pid(struct zed_conf *zcp)
{
	char dirbuf[PATH_MAX];
	mode_t dirmode = S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH;
	int n;
	char *p;
	mode_t mask;
	FILE *fp;

	if (!zcp || !zcp->pid_file) {
		errno = EINVAL;
		zed_log_msg(LOG_ERR, "Failed to write PID file: %s",
		    strerror(errno));
		return (-1);
	}
	n = strlcpy(dirbuf, zcp->pid_file, sizeof (dirbuf));
	if (n >= sizeof (dirbuf)) {
		errno = ENAMETOOLONG;
		zed_log_msg(LOG_WARNING, "Failed to write PID file: %s",
		    strerror(errno));
		return (-1);
	}
	p = strrchr(dirbuf, '/');
	if (p)
		*p = '\0';

	if ((mkdirp(dirbuf, dirmode) < 0) && (errno != EEXIST)) {
		zed_log_msg(LOG_WARNING,
		    "Failed to create directory \"%s\": %s",
		    dirbuf, strerror(errno));
		return (-1);
	}
	(void) unlink(zcp->pid_file);

	mask = umask(0);
	umask(mask | 022);
	fp = fopen(zcp->pid_file, "w");
	umask(mask);

	if (!fp) {
		zed_log_msg(LOG_WARNING, "Failed to open PID file \"%s\": %s",
		    zcp->pid_file, strerror(errno));
	} else if (fprintf(fp, "%d\n", (int) getpid()) == EOF) {
		zed_log_msg(LOG_WARNING, "Failed to write PID file \"%s\": %s",
		    zcp->pid_file, strerror(errno));
	} else if (fclose(fp) == EOF) {
		zed_log_msg(LOG_WARNING, "Failed to close PID file \"%s\": %s",
		    zcp->pid_file, strerror(errno));
	} else {
		return (0);
	}
	(void) unlink(zcp->pid_file);
	return (-1);
}

/*
 * Open and lock the [zcp] state_file.
 * Return 0 on success, -1 on error.
 *
 * FIXME: If state_file exists, verify ownership & permissions.
 * FIXME: Move lock to pid_file instead.
 */
int
zed_conf_open_state(struct zed_conf *zcp)
{
	char dirbuf[PATH_MAX];
	mode_t dirmode = S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH;
	int n;
	char *p;
	int rv;

	if (!zcp || !zcp->state_file) {
		errno = EINVAL;
		zed_log_msg(LOG_ERR, "Failed to open state file: %s",
		    strerror(errno));
		return (-1);
	}
	n = strlcpy(dirbuf, zcp->state_file, sizeof (dirbuf));
	if (n >= sizeof (dirbuf)) {
		errno = ENAMETOOLONG;
		zed_log_msg(LOG_WARNING, "Failed to open state file: %s",
		    strerror(errno));
		return (-1);
	}
	p = strrchr(dirbuf, '/');
	if (p)
		*p = '\0';

	if ((mkdirp(dirbuf, dirmode) < 0) && (errno != EEXIST)) {
		zed_log_msg(LOG_WARNING,
		    "Failed to create directory \"%s\": %s",
		    dirbuf, strerror(errno));
		return (-1);
	}
	if (zcp->state_fd >= 0) {
		if (close(zcp->state_fd) < 0) {
			zed_log_msg(LOG_WARNING,
			    "Failed to close state file \"%s\": %s",
			    zcp->state_file, strerror(errno));
			return (-1);
		}
	}
	if (zcp->do_zero)
		(void) unlink(zcp->state_file);

	zcp->state_fd = open(zcp->state_file,
	    (O_RDWR | O_CREAT), (S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH));
	if (zcp->state_fd < 0) {
		zed_log_msg(LOG_WARNING, "Failed to open state file \"%s\": %s",
		    zcp->state_file, strerror(errno));
		return (-1);
	}
	rv = zed_file_lock(zcp->state_fd);
	if (rv < 0) {
		zed_log_msg(LOG_WARNING, "Failed to lock state file \"%s\": %s",
		    zcp->state_file, strerror(errno));
		return (-1);
	}
	if (rv > 0) {
		pid_t pid = zed_file_is_locked(zcp->state_fd);
		if (pid < 0) {
			zed_log_msg(LOG_WARNING,
			    "Failed to test lock on state file \"%s\"",
			    zcp->state_file);
		} else if (pid > 0) {
			zed_log_msg(LOG_WARNING,
			    "Found PID %d bound to state file \"%s\"",
			    pid, zcp->state_file);
		} else {
			zed_log_msg(LOG_WARNING,
			    "Inconsistent lock state on state file \"%s\"",
			    zcp->state_file);
		}
		return (-1);
	}
	return (0);
}

/*
 * Read the opened [zcp] state_file to obtain the eid & etime of the last event
 * processed.  Write the state from the last event to the [eidp] & [etime] args
 * passed by reference.  Note that etime[] is an array of size 2.
 * Return 0 on success, -1 on error.
 */
int
zed_conf_read_state(struct zed_conf *zcp, uint64_t *eidp, int64_t etime[])
{
	ssize_t len;
	struct iovec iov[3];
	ssize_t n;

	if (!zcp || !eidp || !etime) {
		errno = EINVAL;
		zed_log_msg(LOG_ERR,
		    "Failed to read state file: %s", strerror(errno));
		return (-1);
	}
	if (lseek(zcp->state_fd, 0, SEEK_SET) == (off_t) -1) {
		zed_log_msg(LOG_WARNING,
		    "Failed to reposition state file offset: %s",
		    strerror(errno));
		return (-1);
	}
	len = 0;
	iov[0].iov_base = eidp;
	len += iov[0].iov_len = sizeof (*eidp);
	iov[1].iov_base = &etime[0];
	len += iov[1].iov_len = sizeof (etime[0]);
	iov[2].iov_base = &etime[1];
	len += iov[2].iov_len = sizeof (etime[1]);

	n = readv(zcp->state_fd, iov, 3);
	if (n == 0) {
		*eidp = 0;
	} else if (n < 0) {
		zed_log_msg(LOG_WARNING,
		    "Failed to read state file \"%s\": %s",
		    zcp->state_file, strerror(errno));
		return (-1);
	} else if (n != len) {
		errno = EIO;
		zed_log_msg(LOG_WARNING,
		    "Failed to read state file \"%s\": Read %d of %d bytes",
		    zcp->state_file, n, len);
		return (-1);
	}
	return (0);
}

/*
 * Write the [eid] & [etime] of the last processed event to the opened
 * [zcp] state_file.  Note that etime[] is an array of size 2.
 * Return 0 on success, -1 on error.
 */
int
zed_conf_write_state(struct zed_conf *zcp, uint64_t eid, int64_t etime[])
{
	ssize_t len;
	struct iovec iov[3];
	ssize_t n;

	if (!zcp) {
		errno = EINVAL;
		zed_log_msg(LOG_ERR,
		    "Failed to write state file: %s", strerror(errno));
		return (-1);
	}
	if (lseek(zcp->state_fd, 0, SEEK_SET) == (off_t) -1) {
		zed_log_msg(LOG_WARNING,
		    "Failed to reposition state file offset: %s",
		    strerror(errno));
		return (-1);
	}
	len = 0;
	iov[0].iov_base = &eid;
	len += iov[0].iov_len = sizeof (eid);
	iov[1].iov_base = &etime[0];
	len += iov[1].iov_len = sizeof (etime[0]);
	iov[2].iov_base = &etime[1];
	len += iov[2].iov_len = sizeof (etime[1]);

	n = writev(zcp->state_fd, iov, 3);
	if (n < 0) {
		zed_log_msg(LOG_WARNING,
		    "Failed to write state file \"%s\": %s",
		    zcp->state_file, strerror(errno));
		return (-1);
	}
	if (n != len) {
		errno = EIO;
		zed_log_msg(LOG_WARNING,
		    "Failed to write state file \"%s\": Wrote %d of %d bytes",
		    zcp->state_file, n, len);
		return (-1);
	}
	if (fdatasync(zcp->state_fd) < 0) {
		zed_log_msg(LOG_WARNING,
		    "Failed to sync state file \"%s\": %s",
		    zcp->state_file, strerror(errno));
		return (-1);
	}
	return (0);
}
