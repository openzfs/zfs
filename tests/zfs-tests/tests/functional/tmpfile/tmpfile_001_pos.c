#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/xattr.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <time.h>
#include <err.h>

/* backward compat in case it's not defined */
#ifndef O_TMPFILE
#define	O_TMPFILE	(020000000|O_DIRECTORY)
#endif

/*
 * DESCRIPTION:
 *	Verify we can create tmpfile.
 *
 * STRATEGY:
 *	1. open(2) with O_TMPFILE.
 *	2. write(2) random data to it, then read(2) and compare.
 *	3. fsetxattr(2) random data, then fgetxattr(2) and compare.
 *	4. Verify the above operations run successfully.
 *
 */

#define	BSZ 64

static void
fill_random(char *buf, int len)
{
	srand(time(NULL));
	for (int i = 0; i < len; i++)
		buf[i] = (char)(rand() % 0xFF);
}

int
main(void)
{
	char buf1[BSZ], buf2[BSZ] = {0};

	(void) fprintf(stdout, "Verify O_TMPFILE is working properly.\n");

	const char *testdir = getenv("TESTDIR");
	if (testdir == NULL)
		errx(1, "getenv(\"TESTDIR\")");

	fill_random(buf1, BSZ);

	int fd = open(testdir, O_RDWR|O_TMPFILE, 0666);
	if (fd < 0)
		err(2, "open(%s)", testdir);

	if (write(fd, buf1, BSZ) < 0)
		err(3, "write");

	if (pread(fd, buf2, BSZ, 0) < 0)
		err(4, "pread");

	if (memcmp(buf1, buf2, BSZ) != 0)
		errx(5, "data corrupted");

	memset(buf2, 0, BSZ);

	if (fsetxattr(fd, "user.test", buf1, BSZ, 0) < 0)
		err(6, "pread");

	if (fgetxattr(fd, "user.test", buf2, BSZ) < 0)
		err(7, "fgetxattr");

	if (memcmp(buf1, buf2, BSZ) != 0)
		errx(8, "xattr corrupted\n");

	return (0);
}
