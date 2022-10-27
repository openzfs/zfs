#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <err.h>

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

static void
run(const char *op)
{
	int ret;
	char buf[50];
	sprintf(buf, "sudo -E zpool %s $TESTPOOL", op);
	if ((ret = system(buf)) != 0) {
		if (ret == -1)
			err(4, "system \"zpool %s\"", op);
		else
			errx(4, "zpool %s exited %d\n",
			    op, WEXITSTATUS(ret));
	}
}

int
main(void)
{
	int i, fd;
	char spath[1024], dpath[1024];
	const char *penv[] = {"TESTDIR", "TESTFILE0"};

	(void) fprintf(stdout, "Verify O_TMPFILE file can be linked.\n");

	/*
	 * Get the environment variable values.
	 */
	for (i = 0; i < ARRAY_SIZE(penv); i++)
		if ((penv[i] = getenv(penv[i])) == NULL)
			errx(1, "getenv(penv[%d])", i);

	fd = open(penv[0], O_RDWR|O_TMPFILE, 0666);
	if (fd < 0)
		err(2, "open(%s)", penv[0]);

	snprintf(spath, 1024, "/proc/self/fd/%d", fd);
	snprintf(dpath, 1024, "%s/%s", penv[0], penv[1]);
	if (linkat(AT_FDCWD, spath, AT_FDCWD, dpath, AT_SYMLINK_FOLLOW) < 0)
		err(3, "linkat");

	run("freeze");

	close(fd);

	run("export");
	run("import");

	if (unlink(dpath) == -1) {
		perror("unlink");
		exit(5);
	}

	return (0);
}
