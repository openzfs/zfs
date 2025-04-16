// SPDX-License-Identifier: CDDL-1.0
/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or https://opensource.org/licenses/CDDL-1.0.
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
 * Copyright 2016 Lawrence Livermore National Security, LLC.
 */

/*
 * An extended attribute (xattr) correctness test.  This program creates
 * N files and sets M attrs on them of size S.  Optionally is will verify
 * a pattern stored in the xattr.
 */
#include <stdlib.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <getopt.h>
#include <fcntl.h>
#include <time.h>
#include <unistd.h>
#include <sys/xattr.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <linux/limits.h>

#define	ERROR(fmt, ...)                                                 \
	fprintf(stderr, "xattrtest: %s:%d: %s: " fmt "\n",              \
		__FILE__, __LINE__,      				\
		__func__, ## __VA_ARGS__);

static const char shortopts[] = "hvycdn:f:x:s:p:t:e:rRko:";
static const struct option longopts[] = {
	{ "help",		no_argument,		0,	'h' },
	{ "verbose",		no_argument,		0,	'v' },
	{ "verify",		no_argument,		0,	'y' },
	{ "nth",		required_argument,	0,	'n' },
	{ "files",		required_argument,	0,	'f' },
	{ "xattrs",		required_argument,	0,	'x' },
	{ "size",		required_argument,	0,	's' },
	{ "path",		required_argument,	0,	'p' },
	{ "synccaches", 	no_argument,		0,	'c' },
	{ "dropcaches",		no_argument,		0,	'd' },
	{ "script",		required_argument,	0,	't' },
	{ "seed",		required_argument,	0,	'e' },
	{ "random",		no_argument,		0,	'r' },
	{ "randomvalue",	no_argument,		0,	'R' },
	{ "keep",		no_argument,		0,	'k' },
	{ "only",		required_argument,	0,	'o' },
	{ 0,			0,			0,	0   }
};

enum phases {
	PHASE_ALL = 0,
	PHASE_CREATE,
	PHASE_SETXATTR,
	PHASE_GETXATTR,
	PHASE_UNLINK,
	PHASE_INVAL
};

static int verbose = 0;
static int verify = 0;
static int synccaches = 0;
static int dropcaches = 0;
static int nth = 0;
static int files = 1000;
static int xattrs = 1;
static int size = 6;
static int size_is_random = 0;
static int value_is_random = 0;
static int keep_files = 0;
static int phase = PHASE_ALL;
static const char *path = "/tmp/xattrtest";
static const char *script = "/bin/true";
static char xattrbytes[XATTR_SIZE_MAX];

static int
usage(char *argv0)
{
	fprintf(stderr,
	    "usage: %s [-hvycdrRk] [-n <nth>] [-f <files>] [-x <xattrs>]\n"
	    "       [-s <bytes>] [-p <path>] [-t <script> ] [-o <phase>]\n",
	    argv0);

	fprintf(stderr,
	    "  --help        -h           This help\n"
	    "  --verbose     -v           Increase verbosity\n"
	    "  --verify      -y           Verify xattr contents\n"
	    "  --nth         -n <nth>     Print every nth file\n"
	    "  --files       -f <files>   Set xattrs on N files\n"
	    "  --xattrs      -x <xattrs>  Set N xattrs on each file\n"
	    "  --size        -s <bytes>   Set N bytes per xattr\n"
	    "  --path        -p <path>    Path to files\n"
	    "  --synccaches  -c           Sync caches between phases\n"
	    "  --dropcaches  -d           Drop caches between phases\n"
	    "  --script      -t <script>  Exec script between phases\n"
	    "  --seed        -e <seed>    Random seed value\n"
	    "  --random      -r           Randomly sized xattrs [16-size]\n"
	    "  --randomvalue -R           Random xattr values\n"
	    "  --keep        -k           Don't unlink files\n"
	    "  --only        -o <num>     Only run phase N\n"
	    "                             0=all, 1=create, 2=setxattr,\n"
	    "                             3=getxattr, 4=unlink\n\n");

	return (1);
}

static int
parse_args(int argc, char **argv)
{
	long seed = time(NULL);
	int c;
	int rc = 0;

	while ((c = getopt_long(argc, argv, shortopts, longopts, NULL)) != -1) {
		switch (c) {
		case 'h':
			return (usage(argv[0]));
		case 'v':
			verbose++;
			break;
		case 'y':
			verify = 1;
			break;
		case 'n':
			nth = strtol(optarg, NULL, 0);
			break;
		case 'f':
			files = strtol(optarg, NULL, 0);
			break;
		case 'x':
			xattrs = strtol(optarg, NULL, 0);
			break;
		case 's':
			size = strtol(optarg, NULL, 0);
			if (size > XATTR_SIZE_MAX) {
				fprintf(stderr, "Error: the -s value may not "
				    "be greater than %d\n", XATTR_SIZE_MAX);
				rc = 1;
			}
			break;
		case 'p':
			path = optarg;
			break;
		case 'c':
			synccaches = 1;
			break;
		case 'd':
			dropcaches = 1;
			break;
		case 't':
			script = optarg;
			break;
		case 'e':
			seed = strtol(optarg, NULL, 0);
			break;
		case 'r':
			size_is_random = 1;
			break;
		case 'R':
			value_is_random = 1;
			break;
		case 'k':
			keep_files = 1;
			break;
		case 'o':
			phase = strtol(optarg, NULL, 0);
			if (phase <= PHASE_ALL || phase >= PHASE_INVAL) {
				fprintf(stderr, "Error: the -o value must be "
				    "greater than %d and less than %d\n",
				    PHASE_ALL, PHASE_INVAL);
				rc = 1;
			}
			break;
		default:
			rc = 1;
			break;
		}
	}

	if (rc != 0)
		return (rc);

	srandom(seed);

	if (verbose) {
		fprintf(stdout, "verbose:          %d\n", verbose);
		fprintf(stdout, "verify:           %d\n", verify);
		fprintf(stdout, "nth:              %d\n", nth);
		fprintf(stdout, "files:            %d\n", files);
		fprintf(stdout, "xattrs:           %d\n", xattrs);
		fprintf(stdout, "size:             %d\n", size);
		fprintf(stdout, "path:             %s\n", path);
		fprintf(stdout, "synccaches:       %d\n", synccaches);
		fprintf(stdout, "dropcaches:       %d\n", dropcaches);
		fprintf(stdout, "script:           %s\n", script);
		fprintf(stdout, "seed:             %ld\n", seed);
		fprintf(stdout, "random size:      %d\n", size_is_random);
		fprintf(stdout, "random value:     %d\n", value_is_random);
		fprintf(stdout, "keep:             %d\n", keep_files);
		fprintf(stdout, "only:             %d\n", phase);
		fprintf(stdout, "%s", "\n");
	}

	return (rc);
}

static int
drop_caches(void)
{
	char file[] = "/proc/sys/vm/drop_caches";
	int fd, rc;

	fd = open(file, O_WRONLY);
	if (fd == -1) {
		ERROR("Error %d: open(\"%s\", O_WRONLY)\n", errno, file);
		return (errno);
	}

	rc = write(fd, "3", 1);
	if ((rc == -1) || (rc != 1)) {
		ERROR("Error %d: write(%d, \"3\", 1)\n", errno, fd);
		(void) close(fd);
		return (errno);
	}

	rc = close(fd);
	if (rc == -1) {
		ERROR("Error %d: close(%d)\n", errno, fd);
		return (errno);
	}

	return (0);
}

static int
run_process(const char *path, char *argv[])
{
	pid_t pid;
	int rc, devnull_fd;

	pid = fork();
	if (pid == 0) {
		devnull_fd = open("/dev/null", O_WRONLY);

		if (devnull_fd < 0)
			_exit(-1);

		(void) dup2(devnull_fd, STDOUT_FILENO);
		(void) dup2(devnull_fd, STDERR_FILENO);
		close(devnull_fd);

		(void) execvp(path, argv);
		_exit(-1);
	} else if (pid > 0) {
		int status;

		while ((rc = waitpid(pid, &status, 0)) == -1 &&
		    errno == EINTR) { }

		if (rc < 0 || !WIFEXITED(status))
			return (-1);

		return (WEXITSTATUS(status));
	}

	return (-1);
}

static int
post_hook(const char *phase)
{
	char *argv[3] = { (char *)script, (char *)phase, NULL };
	int rc;

	if (synccaches)
		sync();

	if (dropcaches) {
		rc = drop_caches();
		if (rc)
			return (rc);
	}

	rc = run_process(script, argv);
	if (rc)
		return (rc);

	return (0);
}

#define	USEC_PER_SEC	1000000

static void
timeval_normalize(struct timeval *tv, time_t sec, suseconds_t usec)
{
	while (usec >= USEC_PER_SEC) {
		usec -= USEC_PER_SEC;
		sec++;
	}

	while (usec < 0) {
		usec += USEC_PER_SEC;
		sec--;
	}

	tv->tv_sec = sec;
	tv->tv_usec = usec;
}

static void
timeval_sub(struct timeval *delta, struct timeval *tv1, struct timeval *tv2)
{
	timeval_normalize(delta,
	    tv1->tv_sec - tv2->tv_sec,
	    tv1->tv_usec - tv2->tv_usec);
}

static double
timeval_sub_seconds(struct timeval *tv1, struct timeval *tv2)
{
	struct timeval delta;

	timeval_sub(&delta, tv1, tv2);
	return ((double)delta.tv_usec / USEC_PER_SEC + delta.tv_sec);
}

static int
create_files(void)
{
	int i, rc;
	char *file = NULL;
	struct timeval start, stop;
	double seconds;
	size_t fsize;

	fsize = PATH_MAX;
	file = malloc(fsize);
	if (file == NULL) {
		rc = ENOMEM;
		ERROR("Error %d: malloc(%d) bytes for file name\n", rc,
		    PATH_MAX);
		goto out;
	}

	(void) gettimeofday(&start, NULL);

	for (i = 1; i <= files; i++) {
		if (snprintf(file, fsize, "%s/file-%d", path, i) >= fsize) {
			rc = EINVAL;
			ERROR("Error %d: path too long\n", rc);
			goto out;
		}

		if (nth && ((i % nth) == 0))
			fprintf(stdout, "create: %s\n", file);

		rc = unlink(file);
		if ((rc == -1) && (errno != ENOENT)) {
			ERROR("Error %d: unlink(%s)\n", errno, file);
			rc = errno;
			goto out;
		}

		rc = open(file, O_CREAT, 0644);
		if (rc == -1) {
			ERROR("Error %d: open(%s, O_CREATE, 0644)\n",
			    errno, file);
			rc = errno;
			goto out;
		}

		rc = close(rc);
		if (rc == -1) {
			ERROR("Error %d: close(%d)\n", errno, rc);
			rc = errno;
			goto out;
		}
	}

	(void) gettimeofday(&stop, NULL);
	seconds = timeval_sub_seconds(&stop, &start);
	fprintf(stdout, "create:   %f seconds %f creates/second\n",
	    seconds, files / seconds);

	rc = post_hook("post");
out:
	if (file)
		free(file);

	return (rc);
}

static int
get_random_bytes(char *buf, size_t bytes)
{
	int rand;
	ssize_t bytes_read = 0;

	rand = open("/dev/urandom", O_RDONLY);

	if (rand < 0)
		return (rand);

	while (bytes_read < bytes) {
		ssize_t rc = read(rand, buf + bytes_read, bytes - bytes_read);
		if (rc < 0)
			break;
		bytes_read += rc;
	}

	(void) close(rand);

	return (bytes_read);
}

static int
setxattrs(void)
{
	int i, j, rnd_size = size, shift, rc = 0;
	char name[XATTR_NAME_MAX];
	char *value = NULL;
	char *file = NULL;
	struct timeval start, stop;
	double seconds;
	size_t fsize;

	value = malloc(XATTR_SIZE_MAX);
	if (value == NULL) {
		rc = ENOMEM;
		ERROR("Error %d: malloc(%d) bytes for xattr value\n", rc,
		    XATTR_SIZE_MAX);
		goto out;
	}

	fsize = PATH_MAX;
	file = malloc(fsize);
	if (file == NULL) {
		rc = ENOMEM;
		ERROR("Error %d: malloc(%d) bytes for file name\n", rc,
		    PATH_MAX);
		goto out;
	}

	(void) gettimeofday(&start, NULL);

	for (i = 1; i <= files; i++) {
		if (snprintf(file, fsize, "%s/file-%d", path, i) >= fsize) {
			rc = EINVAL;
			ERROR("Error %d: path too long\n", rc);
			goto out;
		}

		if (nth && ((i % nth) == 0))
			fprintf(stdout, "setxattr: %s\n", file);

		for (j = 1; j <= xattrs; j++) {
			if (size_is_random)
				rnd_size = (random() % (size - 16)) + 16;

			(void) sprintf(name, "user.%d", j);
			shift = sprintf(value, "size=%d ", rnd_size);
			memcpy(value + shift, xattrbytes,
			    sizeof (xattrbytes) - shift);

			rc = lsetxattr(file, name, value, rnd_size, 0);
			if (rc == -1) {
				ERROR("Error %d: lsetxattr(%s, %s, ..., %d)\n",
				    errno, file, name, rnd_size);
				goto out;
			}
		}
	}

	(void) gettimeofday(&stop, NULL);
	seconds = timeval_sub_seconds(&stop, &start);
	fprintf(stdout, "setxattr: %f seconds %f setxattrs/second\n",
	    seconds, (files * xattrs) / seconds);

	rc = post_hook("post");
out:
	if (file)
		free(file);

	if (value)
		free(value);

	return (rc);
}

static int
getxattrs(void)
{
	int i, j, rnd_size, shift, rc = 0;
	char name[XATTR_NAME_MAX];
	char *verify_value = NULL;
	const char *verify_string;
	char *value = NULL;
	const char *value_string;
	char *file = NULL;
	struct timeval start, stop;
	double seconds;
	size_t fsize;

	verify_value = malloc(XATTR_SIZE_MAX);
	if (verify_value == NULL) {
		rc = ENOMEM;
		ERROR("Error %d: malloc(%d) bytes for xattr verify\n", rc,
		    XATTR_SIZE_MAX);
		goto out;
	}

	value = malloc(XATTR_SIZE_MAX);
	if (value == NULL) {
		rc = ENOMEM;
		ERROR("Error %d: malloc(%d) bytes for xattr value\n", rc,
		    XATTR_SIZE_MAX);
		goto out;
	}

	verify_string = value_is_random ? "<random>" : verify_value;
	value_string = value_is_random ? "<random>" : value;

	fsize = PATH_MAX;
	file = malloc(fsize);

	if (file == NULL) {
		rc = ENOMEM;
		ERROR("Error %d: malloc(%d) bytes for file name\n", rc,
		    PATH_MAX);
		goto out;
	}

	(void) gettimeofday(&start, NULL);

	for (i = 1; i <= files; i++) {
		if (snprintf(file, fsize, "%s/file-%d", path, i) >= fsize) {
			rc = EINVAL;
			ERROR("Error %d: path too long\n", rc);
			goto out;
		}

		if (nth && ((i % nth) == 0))
			fprintf(stdout, "getxattr: %s\n", file);

		for (j = 1; j <= xattrs; j++) {
			(void) sprintf(name, "user.%d", j);

			rc = lgetxattr(file, name, value, XATTR_SIZE_MAX);
			if (rc == -1) {
				ERROR("Error %d: lgetxattr(%s, %s, ..., %d)\n",
				    errno, file, name, XATTR_SIZE_MAX);
				goto out;
			}

			if (!verify)
				continue;

			sscanf(value, "size=%d [a-z]", &rnd_size);
			shift = sprintf(verify_value, "size=%d ",
			    rnd_size);
			memcpy(verify_value + shift, xattrbytes,
			    sizeof (xattrbytes) - shift);

			if (rnd_size != rc ||
			    memcmp(verify_value, value, rnd_size)) {
				ERROR("Error %d: verify failed\n "
				    "verify: %s\n value:  %s\n", EINVAL,
				    verify_string, value_string);
				rc = 1;
				goto out;
			}
		}
	}

	(void) gettimeofday(&stop, NULL);
	seconds = timeval_sub_seconds(&stop, &start);
	fprintf(stdout, "getxattr: %f seconds %f getxattrs/second\n",
	    seconds, (files * xattrs) / seconds);

	rc = post_hook("post");
out:
	if (file)
		free(file);

	if (value)
		free(value);

	if (verify_value)
		free(verify_value);

	return (rc);
}

static int
unlink_files(void)
{
	int i, rc;
	char *file = NULL;
	struct timeval start, stop;
	double seconds;
	size_t fsize;

	fsize = PATH_MAX;
	file = malloc(fsize);
	if (file == NULL) {
		rc = ENOMEM;
		ERROR("Error %d: malloc(%d) bytes for file name\n",
		    rc, PATH_MAX);
		goto out;
	}

	(void) gettimeofday(&start, NULL);

	for (i = 1; i <= files; i++) {
		if (snprintf(file, fsize, "%s/file-%d", path, i) >= fsize) {
			rc = EINVAL;
			ERROR("Error %d: path too long\n", rc);
			goto out;
		}

		if (nth && ((i % nth) == 0))
			fprintf(stdout, "unlink: %s\n", file);

		rc = unlink(file);
		if ((rc == -1) && (errno != ENOENT)) {
			ERROR("Error %d: unlink(%s)\n", errno, file);
			free(file);
			return (errno);
		}
	}

	(void) gettimeofday(&stop, NULL);
	seconds = timeval_sub_seconds(&stop, &start);
	fprintf(stdout, "unlink:   %f seconds %f unlinks/second\n",
	    seconds, files / seconds);

	rc = post_hook("post");
out:
	if (file)
		free(file);

	return (rc);
}

int
main(int argc, char **argv)
{
	int rc;

	rc = parse_args(argc, argv);
	if (rc)
		return (rc);

	if (value_is_random) {
		size_t rndsz = sizeof (xattrbytes);

		rc = get_random_bytes(xattrbytes, rndsz);
		if (rc < rndsz) {
			ERROR("Error %d: get_random_bytes() wanted %zd "
			    "got %d\n", errno, rndsz, rc);
			return (rc);
		}
	} else {
		memset(xattrbytes, 'x', sizeof (xattrbytes));
	}

	if (phase == PHASE_ALL || phase == PHASE_CREATE) {
		rc = create_files();
		if (rc)
			return (rc);
	}

	if (phase == PHASE_ALL || phase == PHASE_SETXATTR) {
		rc = setxattrs();
		if (rc)
			return (rc);
	}

	if (phase == PHASE_ALL || phase == PHASE_GETXATTR) {
		rc = getxattrs();
		if (rc)
			return (rc);
	}

	if (!keep_files && (phase == PHASE_ALL || phase == PHASE_UNLINK)) {
		rc = unlink_files();
		if (rc)
			return (rc);
	}

	return (0);
}
