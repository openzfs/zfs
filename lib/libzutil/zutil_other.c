#include <errno.h>
#include <fcntl.h>
#include <libintl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <unistd.h>
#include <math.h>
#include <sys/stat.h>
#include <sys/mnttab.h>
#include <sys/mntent.h>
#include <sys/types.h>
#include <sys/wait.h>

#include <libzfs.h>
#include <libzfs_core.h>

/*
 * Read lines from an open file descriptor and store them in an array of
 * strings until EOF.  lines[] will be allocated and populated with all the
 * lines read.  All newlines are replaced with NULL terminators for
 * convenience.  lines[] must be freed after use with libzfs_free_str_array().
 *
 * Returns the number of lines read.
 */
static int
libzfs_read_stdout_from_fd(int fd, char **lines[])
{

	FILE *fp;
	int lines_cnt = 0;
	size_t len = 0;
	char *line = NULL;
	char **tmp_lines = NULL, **tmp;
	char *nl = NULL;
	int rc;

	fp = fdopen(fd, "r");
	if (fp == NULL)
		return (0);
	while (1) {
		rc = getline(&line, &len, fp);
		if (rc == -1)
			break;

		tmp = realloc(tmp_lines, sizeof (*tmp_lines) * (lines_cnt + 1));
		if (tmp == NULL) {
			/* Return the lines we were able to process */
			break;
		}
		tmp_lines = tmp;

		/* Terminate newlines */
		if ((nl = strchr(line, '\n')) != NULL)
			*nl = '\0';
		tmp_lines[lines_cnt] = line;
		lines_cnt++;
		line = NULL;
	}
	fclose(fp);
	*lines = tmp_lines;
	return (lines_cnt);
}

static int
libzfs_run_process_impl(const char *path, char *argv[], char *env[], int flags,
    char **lines[], int *lines_cnt)
{
	pid_t pid;
	int error, devnull_fd;
	int link[2];

	/*
	 * Setup a pipe between our child and parent process if we're
	 * reading stdout.
	 */
	if ((lines != NULL) && pipe(link) == -1)
		return (-ESTRPIPE);

	pid = vfork();
	if (pid == 0) {
		/* Child process */
		devnull_fd = open("/dev/null", O_WRONLY);

		if (devnull_fd < 0)
			_exit(-1);

		if (!(flags & STDOUT_VERBOSE) && (lines == NULL))
			(void) dup2(devnull_fd, STDOUT_FILENO);
		else if (lines != NULL) {
			/* Save the output to lines[] */
			dup2(link[1], STDOUT_FILENO);
			close(link[0]);
			close(link[1]);
		}

		if (!(flags & STDERR_VERBOSE))
			(void) dup2(devnull_fd, STDERR_FILENO);

		close(devnull_fd);

		if (flags & NO_DEFAULT_PATH) {
			if (env == NULL)
				execv(path, argv);
			else
				execve(path, argv, env);
		} else {
			if (env == NULL)
				execvp(path, argv);
			else
				execvpe(path, argv, env);
		}

		_exit(-1);
	} else if (pid > 0) {
		/* Parent process */
		int status;

		while ((error = waitpid(pid, &status, 0)) == -1 &&
		    errno == EINTR) { }
		if (error < 0 || !WIFEXITED(status))
			return (-1);

		if (lines != NULL) {
			close(link[1]);
			*lines_cnt = libzfs_read_stdout_from_fd(link[0], lines);
		}
		return (WEXITSTATUS(status));
	}

	return (-1);
}

int
libzfs_run_process(const char *path, char *argv[], int flags)
{
	return (libzfs_run_process_impl(path, argv, NULL, flags, NULL, NULL));
}

/*
 * Run a command and store its stdout lines in an array of strings (lines[]).
 * lines[] is allocated and populated for you, and the number of lines is set in
 * lines_cnt.  lines[] must be freed after use with libzfs_free_str_array().
 * All newlines (\n) in lines[] are terminated for convenience.
 */
int
libzfs_run_process_get_stdout(const char *path, char *argv[], char *env[],
    char **lines[], int *lines_cnt)
{
	return (libzfs_run_process_impl(path, argv, env, 0, lines, lines_cnt));
}

/*
 * Same as libzfs_run_process_get_stdout(), but run without $PATH set.  This
 * means that *path needs to be the full path to the executable.
 */
int
libzfs_run_process_get_stdout_nopath(const char *path, char *argv[],
    char *env[], char **lines[], int *lines_cnt)
{
	return (libzfs_run_process_impl(path, argv, env, NO_DEFAULT_PATH,
	    lines, lines_cnt));
}
