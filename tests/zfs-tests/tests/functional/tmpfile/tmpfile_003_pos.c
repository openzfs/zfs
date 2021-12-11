#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>

/* backward compat in case it's not defined */
#ifndef O_TMPFILE
#define	O_TMPFILE	(020000000|O_DIRECTORY)
#endif

/*
 * DESCRIPTION:
 *	Verify O_EXCL tmpfile cannot be linked.
 *
 * STRATEGY:
 *	1. open(2) with O_TMPFILE|O_EXCL.
 *	2. linkat(2).
 *	3. stat(2) the path to verify it wasn't created.
 *
 */

int
main(void)
{
	int i, fd;
	char spath[1024], dpath[1024];
	char *penv[] = {"TESTDIR", "TESTFILE0"};
	struct stat sbuf;

	(void) fprintf(stdout, "Verify O_EXCL tmpfile cannot be linked.\n");

	/*
	 * Get the environment variable values.
	 */
	for (i = 0; i < sizeof (penv) / sizeof (char *); i++) {
		if ((penv[i] = getenv(penv[i])) == NULL) {
			(void) fprintf(stderr, "getenv(penv[%d])\n", i);
			exit(1);
		}
	}

	fd = open(penv[0], O_RDWR|O_TMPFILE|O_EXCL, 0666);
	if (fd < 0) {
		perror("open");
		exit(2);
	}

	snprintf(spath, 1024, "/proc/self/fd/%d", fd);
	snprintf(dpath, 1024, "%s/%s", penv[0], penv[1]);
	if (linkat(AT_FDCWD, spath, AT_FDCWD, dpath, AT_SYMLINK_FOLLOW) == 0) {
		fprintf(stderr, "linkat returns successfully\n");
		close(fd);
		exit(3);
	}

	if (stat(dpath, &sbuf) == 0) {
		fprintf(stderr, "stat returns successfully\n");
		close(fd);
		exit(4);
	}
	close(fd);

	return (0);
}
