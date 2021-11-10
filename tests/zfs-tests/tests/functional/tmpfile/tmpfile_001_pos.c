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
	int i;
	srand(time(NULL));
	for (i = 0; i < len; i++) {
		buf[i] = (char)rand();
	}
}

int
main(int argc, char *argv[])
{
	int i, fd;
	char buf1[BSZ], buf2[BSZ] = {};
	char *penv[] = {"TESTDIR"};

	(void) fprintf(stdout, "Verify O_TMPFILE is working properly.\n");

	/*
	 * Get the environment variable values.
	 */
	for (i = 0; i < sizeof (penv) / sizeof (char *); i++) {
		if ((penv[i] = getenv(penv[i])) == NULL) {
			(void) fprintf(stderr, "getenv(penv[%d])\n", i);
			exit(1);
		}
	}

	fill_random(buf1, BSZ);

	fd = open(penv[0], O_RDWR|O_TMPFILE, 0666);
	if (fd < 0) {
		perror("open");
		exit(2);
	}

	if (write(fd, buf1, BSZ) < 0) {
		perror("write");
		close(fd);
		exit(3);
	}

	if (pread(fd, buf2, BSZ, 0) < 0) {
		perror("pread");
		close(fd);
		exit(4);
	}

	if (memcmp(buf1, buf2, BSZ) != 0) {
		fprintf(stderr, "data corrupted\n");
		close(fd);
		exit(5);
	}

	memset(buf2, 0, BSZ);

	if (fsetxattr(fd, "user.test", buf1, BSZ, 0) < 0) {
		perror("fsetxattr");
		close(fd);
		exit(6);
	}

	if (fgetxattr(fd, "user.test", buf2, BSZ) < 0) {
		perror("fgetxattr");
		close(fd);
		exit(7);
	}

	if (memcmp(buf1, buf2, BSZ) != 0) {
		fprintf(stderr, "xattr corrupted\n");
		close(fd);
		exit(8);
	}

	close(fd);

	return (0);
}
