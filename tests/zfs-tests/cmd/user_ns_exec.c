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

#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <limits.h>
#include <sys/types.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <sched.h>

#define	EXECSHELL	"/bin/sh"
#define	UIDMAP		"0 100000 65536"

static int
child_main(int argc, char *argv[], int sync_pipe)
{
	char sync_buf;
	char cmds[BUFSIZ] = { 0 };
	char sep[] = " ";
	int i, len;

	if (unshare(CLONE_NEWUSER | CLONE_NEWNS) != 0) {
		perror("unshare");
		return (1);
	}

	/* tell parent we entered the new namespace */
	if (write(sync_pipe, "1", 1) != 1) {
		perror("write");
		return (1);
	}

	/* wait for parent to setup the uid mapping */
	if (read(sync_pipe, &sync_buf, 1) != 1) {
		(void) fprintf(stderr, "user namespace setup failed\n");
		return (1);
	}

	close(sync_pipe);

	if (setuid(0) != 0) {
		perror("setuid");
		return (1);
	}
	if (setgid(0) != 0) {
		perror("setgid");
		return (1);
	}

	len = 0;
	for (i = 1; i < argc; i++) {
		(void) snprintf(cmds+len, sizeof (cmds)-len,
		    "%s%s", argv[i], sep);
		len += strlen(argv[i]) + strlen(sep);
	}

	if (execl(EXECSHELL, "sh",  "-c", cmds, (char *)NULL) != 0) {
		perror("execl: " EXECSHELL);
		return (1);
	}

	return (0);
}

static int
set_idmap(pid_t pid, const char *file)
{
	int result = 0;
	int mapfd;
	char path[PATH_MAX];

	(void) snprintf(path, sizeof (path), "/proc/%d/%s", (int)pid, file);

	mapfd = open(path, O_WRONLY);
	if (mapfd < 0) {
		perror("open");
		return (errno);
	}

	if (write(mapfd, UIDMAP, sizeof (UIDMAP)-1) != sizeof (UIDMAP)-1) {
		perror("write");
		result = (errno);
	}

	close(mapfd);

	return (result);
}

int
main(int argc, char *argv[])
{
	char sync_buf;
	int result, wstatus;
	int syncfd[2];
	pid_t child;

	if (argc < 2 || strlen(argv[1]) == 0) {
		(void) printf("\tUsage: %s <commands> ...\n", argv[0]);
		return (1);
	}

	if (socketpair(AF_UNIX, SOCK_STREAM, 0, syncfd) != 0) {
		perror("socketpair");
		return (1);
	}

	child = fork();
	if (child == (pid_t)-1) {
		perror("fork");
		return (1);
	}

	if (child == 0) {
		close(syncfd[0]);
		return (child_main(argc, argv, syncfd[1]));
	}

	close(syncfd[1]);

	result = 0;
	/* wait for the child to have unshared its namespaces */
	if (read(syncfd[0], &sync_buf, 1) != 1) {
		perror("read");
		kill(child, SIGKILL);
		result = 1;
		goto reap;
	}

	/* write uid mapping */
	if (set_idmap(child, "uid_map") != 0 ||
	    set_idmap(child, "gid_map") != 0) {
		result = 1;
		kill(child, SIGKILL);
		goto reap;
	}

	/* tell the child to proceed */
	if (write(syncfd[0], "1", 1) != 1) {
		perror("write");
		kill(child, SIGKILL);
		result = 1;
		goto reap;
	}
	close(syncfd[0]);

reap:
	while (waitpid(child, &wstatus, 0) != child)
		kill(child, SIGKILL);
	if (result == 0)
		result = WEXITSTATUS(wstatus);

	return (result);
}
