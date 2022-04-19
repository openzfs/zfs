/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */

/*
 * Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

/*
 * Copyright (c) 2013 by Delphix. All rights reserved.
 */


#include <sys/types.h>
#include <sys/stat.h>
#ifndef __FreeBSD__
#include <sys/xattr.h>
#endif
#include <utime.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <libgen.h>
#include <string.h>

#define	ST_ATIME 0
#define	ST_CTIME 1
#define	ST_MTIME 2

#define	ALL_MODE (mode_t)(S_IRWXU|S_IRWXG|S_IRWXO)

typedef struct timetest {
	int	type;
	const char	*name;
	int	(*func)(const char *pfile);
} timetest_t;

static char tfile[BUFSIZ] = { 0 };

/*
 * DESCRIPTION:
 * 	Verify time will be changed correctly after each operation.
 *
 * STRATEGY:
 *	1. Define time test array.
 *	2. Loop through each item in this array.
 *	3. Verify the time is changed after each operation.
 *
 */

static int
get_file_time(const char *pfile, int what, time_t *ptr)
{
	struct stat stat_buf;

	if (pfile == NULL || ptr == NULL) {
		return (-1);
	}

	if (stat(pfile, &stat_buf) == -1) {
		return (-1);
	}

	switch (what) {
		case ST_ATIME:
			*ptr = stat_buf.st_atime;
			return (0);
		case ST_CTIME:
			*ptr = stat_buf.st_ctime;
			return (0);
		case ST_MTIME:
			*ptr = stat_buf.st_mtime;
			return (0);
		default:
			return (-1);
	}
}

static ssize_t
get_dirnamelen(const char *path)
{
	const char *end = strrchr(path, '/');
	return (end ? end - path : -1);
}

static int
do_read(const char *pfile)
{
	int fd, ret = 0;
	char buf[BUFSIZ] = { 0 };

	if (pfile == NULL) {
		return (-1);
	}

	if ((fd = open(pfile, O_RDONLY, ALL_MODE)) == -1) {
		return (-1);
	}
	if (read(fd, buf, sizeof (buf)) == -1) {
		(void) fprintf(stderr, "read(%d, buf, %zd) failed with errno "
		    "%d\n", fd, sizeof (buf), errno);
		(void) close(fd);
		return (1);
	}
	(void) close(fd);

	return (ret);
}

static int
do_write(const char *pfile)
{
	int fd, ret = 0;
	char buf[BUFSIZ] = "call function do_write()";

	if (pfile == NULL) {
		return (-1);
	}

	if ((fd = open(pfile, O_WRONLY, ALL_MODE)) == -1) {
		return (-1);
	}
	if (write(fd, buf, strlen(buf)) == -1) {
		(void) fprintf(stderr, "write(%d, buf, %d) failed with errno "
		    "%d\n", fd, (int)strlen(buf), errno);
		(void) close(fd);
		return (1);
	}
	(void) close(fd);

	return (ret);
}

static int
do_link(const char *pfile)
{
	int ret = 0;
	char link_file[BUFSIZ + 16] = { 0 };

	if (pfile == NULL) {
		return (-1);
	}

	/*
	 * Figure out source file directory name, and create
	 * the link file in the same directory.
	 */
	(void) snprintf(link_file, sizeof (link_file),
	    "%.*s/%s", (int)get_dirnamelen(pfile), pfile, "link_file");

	if (link(pfile, link_file) == -1) {
		(void) fprintf(stderr, "link(%s, %s) failed with errno %d\n",
		    pfile, link_file, errno);
		return (1);
	}

	(void) unlink(link_file);

	return (ret);
}

static int
do_creat(const char *pfile)
{
	int fd, ret = 0;

	if (pfile == NULL) {
		return (-1);
	}

	if ((fd = creat(pfile, ALL_MODE)) == -1) {
		(void) fprintf(stderr, "creat(%s, ALL_MODE) failed with errno "
		    "%d\n", pfile, errno);
		return (1);
	}
	(void) close(fd);

	return (ret);
}

static int
do_utime(const char *pfile)
{
	int ret = 0;

	if (pfile == NULL) {
		return (-1);
	}

	/*
	 * Times of the file are set to the current time
	 */
	if (utime(pfile, NULL) == -1) {
		(void) fprintf(stderr, "utime(%s, NULL) failed with errno "
		    "%d\n", pfile, errno);
		return (1);
	}

	return (ret);
}

static int
do_chmod(const char *pfile)
{
	int ret = 0;

	if (pfile == NULL) {
		return (-1);
	}

	if (chmod(pfile, ALL_MODE) == -1) {
		(void) fprintf(stderr, "chmod(%s, ALL_MODE) failed with "
		    "errno %d\n", pfile, errno);
		return (1);
	}

	return (ret);
}

static int
do_chown(const char *pfile)
{
	int ret = 0;

	if (pfile == NULL) {
		return (-1);
	}

	if (chown(pfile, getuid(), getgid()) == -1) {
		(void) fprintf(stderr, "chown(%s, %d, %d) failed with errno "
		    "%d\n", pfile, (int)getuid(), (int)getgid(), errno);
		return (1);
	}

	return (ret);
}

#ifndef __FreeBSD__
static int
do_xattr(const char *pfile)
{
	int ret = 0;
	const char *value = "user.value";

	if (pfile == NULL) {
		return (-1);
	}

	if (setxattr(pfile, "user.x", value, strlen(value), 0) == -1) {
		(void) fprintf(stderr, "setxattr(%s, %d, %d) failed with errno "
		    "%d\n", pfile, (int)getuid(), (int)getgid(), errno);
		return (1);
	}
	return (ret);
}
#endif

static void
cleanup(void)
{
	if ((strlen(tfile) != 0) && (access(tfile, F_OK) == 0)) {
		(void) unlink(tfile);
	}
}

static timetest_t timetest_table[] = {
	{ ST_ATIME,	"st_atime",	do_read		},
	{ ST_ATIME,	"st_atime",	do_utime	},
	{ ST_MTIME,	"st_mtime",	do_creat	},
	{ ST_MTIME,	"st_mtime",	do_write	},
	{ ST_MTIME,	"st_mtime",	do_utime	},
	{ ST_CTIME,	"st_ctime",	do_creat	},
	{ ST_CTIME,	"st_ctime",	do_write	},
	{ ST_CTIME,	"st_ctime",	do_chmod	},
	{ ST_CTIME,	"st_ctime",	do_chown 	},
	{ ST_CTIME,	"st_ctime",	do_link		},
	{ ST_CTIME,	"st_ctime",	do_utime	},
#ifndef __FreeBSD__
	{ ST_CTIME,	"st_ctime",	do_xattr	},
#endif
};

#define	NCOMMAND (sizeof (timetest_table) / sizeof (timetest_table[0]))

int
main(void)
{
	int i, ret, fd;
	const char *penv[] = {"TESTDIR", "TESTFILE0"};

	(void) atexit(cleanup);

	/*
	 * Get the environment variable values.
	 */
	for (i = 0; i < sizeof (penv) / sizeof (char *); i++) {
		if ((penv[i] = getenv(penv[i])) == NULL) {
			(void) fprintf(stderr, "getenv(penv[%d])\n", i);
			return (1);
		}
	}
	(void) snprintf(tfile, sizeof (tfile), "%s/%s", penv[0], penv[1]);

	/*
	 * If the test file exists, remove it first.
	 */
	if (access(tfile, F_OK) == 0) {
		(void) unlink(tfile);
	}
	ret = 0;
	if ((fd = open(tfile, O_WRONLY | O_CREAT | O_TRUNC, ALL_MODE)) == -1) {
		(void) fprintf(stderr, "open(%s) failed: %d\n", tfile, errno);
		return (1);
	}
	(void) close(fd);

	for (i = 0; i < NCOMMAND; i++) {
		time_t t1, t2;

		/*
		 * Get original time before operating.
		 */
		ret = get_file_time(tfile, timetest_table[i].type, &t1);
		if (ret != 0) {
			(void) fprintf(stderr, "get_file_time(%s %d) = %d\n",
			    tfile, timetest_table[i].type, ret);
			return (1);
		}

		/*
		 * Sleep 2 seconds, then invoke command on given file
		 */
		(void) sleep(2);
		timetest_table[i].func(tfile);

		/*
		 * Get time after operating.
		 */
		ret = get_file_time(tfile, timetest_table[i].type, &t2);
		if (ret != 0) {
			(void) fprintf(stderr, "get_file_time(%s %d) = %d\n",
			    tfile, timetest_table[i].type, ret);
			return (1);
		}

		if (t1 == t2) {
			(void) fprintf(stderr, "%s: t1(%ld) == t2(%ld)\n",
			    timetest_table[i].name, (long)t1, (long)t2);
			return (1);
		} else {
			(void) fprintf(stderr, "%s: t1(%ld) != t2(%ld)\n",
			    timetest_table[i].name, (long)t1, (long)t2);
		}
	}

	return (0);
}
