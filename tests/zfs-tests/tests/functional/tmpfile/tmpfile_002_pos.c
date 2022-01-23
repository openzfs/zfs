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
 *	Verify we can link tmpfile.
 *
 * STRATEGY:
 *	1. open(2) with O_TMPFILE.
 *	2. linkat(2).
 *	3. freeze the pool, export and re-import the pool.
 *	3. stat(2) the path to verify it has been created.
 *
 */

int
main(void)
{
	int i, fd, ret;
	char spath[1024], dpath[1024];
	char *penv[] = {"TESTDIR", "TESTFILE0"};
	struct stat sbuf;

	(void) fprintf(stdout, "Verify O_TMPFILE file can be linked.\n");

	/*
	 * Get the environment variable values.
	 */
	for (i = 0; i < sizeof (penv) / sizeof (char *); i++) {
		if ((penv[i] = getenv(penv[i])) == NULL) {
			(void) fprintf(stderr, "getenv(penv[%d])\n", i);
			exit(1);
		}
	}

	fd = open(penv[0], O_RDWR|O_TMPFILE, 0666);
	if (fd < 0) {
		perror("open");
		exit(2);
	}

	snprintf(spath, 1024, "/proc/self/fd/%d", fd);
	snprintf(dpath, 1024, "%s/%s", penv[0], penv[1]);
	if (linkat(AT_FDCWD, spath, AT_FDCWD, dpath, AT_SYMLINK_FOLLOW) < 0) {
		perror("linkat");
		close(fd);
		exit(3);
	}

	if ((ret = system("sudo zpool freeze $TESTPOOL"))) {
		if (ret == -1)
			perror("system \"zpool freeze\"");
		else
			fprintf(stderr, "zpool freeze exits with %d\n",
			    WEXITSTATUS(ret));
		exit(4);
	}

	close(fd);

	if ((ret = system("sudo zpool export $TESTPOOL"))) {
		if (ret == -1)
			perror("system \"zpool export\"");
		else
			fprintf(stderr, "zpool export exits with %d\n",
			    WEXITSTATUS(ret));
		exit(4);
	}

	if ((ret = system("sudo zpool import $TESTPOOL"))) {
		if (ret == -1)
			perror("system \"zpool import\"");
		else
			fprintf(stderr, "zpool import exits with %d\n",
			    WEXITSTATUS(ret));
		exit(4);
	}

	if (stat(dpath, &sbuf) < 0) {
		perror("stat");
		unlink(dpath);
		exit(5);
	}
	unlink(dpath);

	return (0);
}
