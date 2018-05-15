#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>

/* backward compat in case it's not defined */
#ifndef O_TMPFILE
#define	O_TMPFILE	(020000000|O_DIRECTORY)
#endif

/*
 * DESCRIPTION:
 *	Check if the kernel support O_TMPFILE.
 */

int
main(int argc, char *argv[])
{
	int fd;
	struct stat buf;

	if (argc < 2) {
		fprintf(stderr, "Usage: %s dir\n", argv[0]);
		return (2);
	}
	if (stat(argv[1], &buf) < 0) {
		perror("stat");
		return (2);
	}
	if (!S_ISDIR(buf.st_mode)) {
		fprintf(stderr, "\"%s\" is not a directory\n", argv[1]);
		return (2);
	}

	fd = open(argv[1], O_TMPFILE | O_WRONLY, 0666);
	if (fd < 0) {
		if (errno == EISDIR) {
			fprintf(stderr,
			    "The kernel doesn't support O_TMPFILE\n");
			return (1);
		} else if (errno == EOPNOTSUPP) {
			fprintf(stderr,
			    "The filesystem doesn't support O_TMPFILE\n");
			return (2);
		}
		perror("open");
	} else {
		close(fd);
	}
	return (0);
}
