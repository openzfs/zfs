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
#include <sys/sysctl.h>

#include <libzfs.h>
#include <libzfs_core.h>

#include "libzfs_impl.h"
#include "zfs_prop.h"
#include <libzutil.h>
#include <sys/zfs_sysfs.h>

#define	ZDIFF_SHARESDIR		"/.zfs/shares/"


int
zfs_ioctl(libzfs_handle_t *hdl, int request, zfs_cmd_t *zc)
{
	return (zfs_ioctl_fd(hdl->libzfs_fd, request, zc));
}

const char *
libzfs_error_init(int error)
{
	switch (error) {
	case ENXIO:
		return (dgettext(TEXT_DOMAIN, "The ZFS modules are not "
		    "loaded.\nTry running '/sbin/kextload zfs.kext' as root "
		    "to load them."));
	case ENOENT:
		return (dgettext(TEXT_DOMAIN, "/dev/zfs and /proc/self/mounts "
		    "are required.\nTry running 'udevadm trigger' and 'mount "
		    "-t proc proc /proc' as root."));
	case ENOEXEC:
		return (dgettext(TEXT_DOMAIN, "The ZFS modules cannot be "
		    "auto-loaded.\nTry running '/sbin/kextload zfs.kext' as "
		    "root to manually load them."));
	case EACCES:
		return (dgettext(TEXT_DOMAIN, "Permission denied the "
		    "ZFS utilities must be run as root."));
	default:
		return (dgettext(TEXT_DOMAIN, "Failed to initialize the "
		    "libzfs library."));
	}
}

static int
libzfs_module_loaded(const char *module)
{
	const char path_prefix[] = "/dev/";
	char path[256];

	memcpy(path, path_prefix, sizeof (path_prefix) - 1);
	strcpy(path + sizeof (path_prefix) - 1, module);

	return (access(path, F_OK) == 0);
}

/*
 * Verify the required ZFS_DEV device is available and optionally attempt
 * to load the ZFS modules.  Under normal circumstances the modules
 * should already have been loaded by some external mechanism.
 *
 * Environment variables:
 * - ZFS_MODULE_LOADING="YES|yes|ON|on" - Attempt to load modules.
 * - ZFS_MODULE_TIMEOUT="<seconds>"     - Seconds to wait for ZFS_DEV
 */
static int
libzfs_load_module_impl(const char *module)
{
	char *argv[4] = {"/sbin/kextload", (char *)module, (char *)0};
	char *load_str, *timeout_str;
	long timeout = 10; /* seconds */
	long busy_timeout = 10; /* milliseconds */
	int load = 0, fd;
	hrtime_t start;

	/* Optionally request module loading */
	if (!libzfs_module_loaded(module)) {
		load_str = getenv("ZFS_MODULE_LOADING");
		if (load_str) {
			if (!strncasecmp(load_str, "YES", strlen("YES")) ||
			    !strncasecmp(load_str, "ON", strlen("ON")))
				load = 1;
			else
				load = 0;
		}

		if (load) {
			if (libzfs_run_process("/sbin/kextload", argv, 0))
				return (ENOEXEC);
		}

		if (!libzfs_module_loaded(module))
			return (ENXIO);
	}

	/*
	 * Device creation by udev is asynchronous and waiting may be
	 * required.  Busy wait for 10ms and then fall back to polling every
	 * 10ms for the allowed timeout (default 10s, max 10m).  This is
	 * done to optimize for the common case where the device is
	 * immediately available and to avoid penalizing the possible
	 * case where udev is slow or unable to create the device.
	 */
	timeout_str = getenv("ZFS_MODULE_TIMEOUT");
	if (timeout_str) {
		timeout = strtol(timeout_str, NULL, 0);
		timeout = MAX(MIN(timeout, (10 * 60)), 0); /* 0 <= N <= 600 */
	}

	start = gethrtime();
	do {
		fd = open(ZFS_DEV, O_RDWR);
		if (fd >= 0) {
			(void) close(fd);
			return (0);
		} else if (errno != ENOENT) {
			return (errno);
		} else if (NSEC2MSEC(gethrtime() - start) < busy_timeout) {
			sched_yield();
		} else {
			usleep(10 * MILLISEC);
		}
	} while (NSEC2MSEC(gethrtime() - start) < (timeout * MILLISEC));

	return (ENOENT);
}

int
libzfs_load_module(void)
{
	return (libzfs_load_module_impl(ZFS_DRIVER));
}

int
find_shares_object(differ_info_t *di)
{
	return (0);
}

/*
 * Fill given version buffer with zfs kernel version read from ZFS_SYSFS_DIR
 * Returns 0 on success, and -1 on error (with errno set)
 */
int
zfs_version_kernel(char *version, int len)
{
	size_t rlen = len;

	if (sysctlbyname("zfs.kext_version",
	    version, &rlen, NULL, 0) == -1)
		return (-1);

	return (0);
}

static int
execvPe(const char *name, const char *path, char * const *argv,
    char * const *envp)
{
	const char **memp;
	size_t cnt, lp, ln;
	int eacces, save_errno;
	char *cur, buf[MAXPATHLEN];
	const char *p, *bp;
	struct stat sb;

	eacces = 0;

	/* If it's an absolute or relative path name, it's easy. */
	if (strchr(name, '/')) {
		bp = name;
		cur = NULL;
		goto retry;
	}
	bp = buf;

	/* If it's an empty path name, fail in the usual POSIX way. */
	if (*name == '\0') {
		errno = ENOENT;
		return (-1);
	}

	cur = alloca(strlen(path) + 1);
	if (cur == NULL) {
		errno = ENOMEM;
		return (-1);
	}
	strcpy(cur, path);
	while ((p = strsep(&cur, ":")) != NULL) {
		/*
		 * It's a SHELL path -- double, leading and trailing colons
		 * mean the current directory.
		 */
		if (*p == '\0') {
			p = ".";
			lp = 1;
		} else
			lp = strlen(p);
		ln = strlen(name);

		/*
		 * If the path is too long complain.  This is a possible
		 * security issue; given a way to make the path too long
		 * the user may execute the wrong program.
		 */
		if (lp + ln + 2 > sizeof (buf)) {
			(void) write(STDERR_FILENO, "execvP: ", 8);
			(void) write(STDERR_FILENO, p, lp);
			(void) write(STDERR_FILENO, ": path too long\n",
			    16);
			continue;
		}
		bcopy(p, buf, lp);
		buf[lp] = '/';
		bcopy(name, buf + lp + 1, ln);
		buf[lp + ln + 1] = '\0';

retry:
		(void) execve(bp, argv, envp);
		switch (errno) {
		case E2BIG:
			goto done;
		case ELOOP:
		case ENAMETOOLONG:
		case ENOENT:
			break;
		case ENOEXEC:
			for (cnt = 0; argv[cnt]; ++cnt)
				;
			memp = alloca((cnt + 2) * sizeof (char *));
			if (memp == NULL) {
				goto done;
			}
			memp[0] = "sh";
			memp[1] = bp;
			bcopy(argv + 1, memp + 2, cnt * sizeof (char *));
			execve(_PATH_BSHELL, __DECONST(char **, memp),
			    envp);
			goto done;
		case ENOMEM:
			goto done;
		case ENOTDIR:
			break;
		case ETXTBSY:
			/*
			 * We used to retry here, but sh(1) doesn't.
			 */
			goto done;
		default:
			/*
			 * EACCES may be for an inaccessible directory or
			 * a non-executable file.  Call stat() to decide
			 * which.  This also handles ambiguities for EFAULT
			 * and EIO, and undocumented errors like ESTALE.
			 * We hope that the race for a stat() is unimportant.
			 */
			save_errno = errno;
			if (stat(bp, &sb) != 0)
				break;
			if (save_errno == EACCES) {
				eacces = 1;
				continue;
			}
			errno = save_errno;
			goto done;
		}
	}
	if (eacces)
		errno = EACCES;
	else
		errno = ENOENT;
done:
	return (-1);
}

int
execvpe(const char *name, char * const argv[], char * const envp[])
{
	const char *path;

	/* Get the path we're searching. */
	if ((path = getenv("PATH")) == NULL)
		path = _PATH_DEFPATH;

	return (execvPe(name, path, argv, envp));
}

#include <objc/objc.h>
#include <objc/runtime.h>
#include <objc/message.h>

extern void libzfs_refresh_finder(char *);

/*
 * To tell Finder to refresh is relatively easy from Obj-C, but as this
 * would be the only function to use Obj-C (and only .m), the following code:
 * void libzfs_refresh_finder(char *mountpoint)
 * {
 *    [[NSWorkspace sharedWorkspace] noteFileSystemChanged:[NSString
 *         stringWithUTF8String:mountpoint]];
 * }
 * Has been converted to C to keep autoconf simpler. If in future we have
 * more Obj-C source files, then we should re-address this.
 */
void
libzfs_refresh_finder(char *path)
{
	Class NSWorkspace = objc_getClass("NSWorkspace");
	Class NSString = objc_getClass("NSString");
	SEL stringWithUTF8String = sel_registerName("stringWithUTF8String:");
	SEL sharedWorkspace = sel_registerName("sharedWorkspace");
	SEL noteFileSystemChanged = sel_registerName("noteFileSystemChanged:");
	id ns_path = ((id(*)(Class, SEL, char *))objc_msgSend)(NSString,
	    stringWithUTF8String, path);
	id workspace = ((id(*)(Class, SEL))objc_msgSend)(NSWorkspace,
	    sharedWorkspace);
	((id(*)(id, SEL, id))objc_msgSend)(workspace, noteFileSystemChanged,
	    ns_path);
}

void
zfs_rollback_os(zfs_handle_t *zhp)
{
	char sourceloc[ZFS_MAX_DATASET_NAME_LEN];
	char mountpoint[ZFS_MAXPROPLEN];
	zprop_source_t sourcetype;

	if (zfs_prop_valid_for_type(ZFS_PROP_MOUNTPOINT, zhp->zfs_type,
	    B_FALSE)) {
		if (zfs_prop_get(zhp, ZFS_PROP_MOUNTPOINT,
		    mountpoint, sizeof (mountpoint),
		    &sourcetype, sourceloc, sizeof (sourceloc), B_FALSE) == 0)
			libzfs_refresh_finder(mountpoint);
	}
}

struct pipe2file {
	int from;
	int to;
};
typedef struct pipe2file pipe2file_t;

#include <poll.h>

static void *
pipe_io_relay(void *arg)
{
	pipe2file_t *p2f = (pipe2file_t *)arg;
	int readfd, writefd;
	unsigned char *buffer;
	unsigned char space[1024];
	int size = 1024 * 1024;
	int red, sent;
	uint64_t total = 0;

	readfd = p2f->from;
	writefd = p2f->to;
	free(p2f);
	p2f = NULL;

	buffer = malloc(size);
	if (buffer == NULL) {
		buffer = space;
		size = sizeof (space);
	}

	fprintf(stderr, "%s: thread up: read(%d) write(%d)\r\n", __func__,
	    readfd, writefd);

	for (;;) {

		red = read(readfd, buffer, size);
		// fprintf(stderr, "%s: read(%d): %d (errno %d)\r\n", __func__,
		//	readfd, red, errno);
		if (red == 0)
			break;
		if (red < 0 && errno != EWOULDBLOCK)
			break;
		sent = write(writefd, buffer, red);
		// fprintf(stderr, "%s: write(%d): %d (errno %d)\r\n", __func__,
		//	writefd, sent, errno);
		if (sent < 0)
			break;
		total += red;
	}

	/*
	 * It seems unlikely that this code is ever reached, as the process
	 * calls exit() when done, and this thread is terminated.
	 */

	fprintf(stderr, "loop exit\r\n");

	close(readfd);
	close(writefd);

	if (buffer != space)
		free(buffer);

	fprintf(stderr, "%s: thread done: %llu bytes\r\n", __func__, total);
	return (NULL);
}

/*
 * XNU only lets us do IO on vnodes, not pipes, so create a Unix
 * Domain socket, open it to get a vnode for the kernel, and spawn
 * thread to relay IO.
 */
void
libzfs_macos_wrapfd(int *srcfd, boolean_t send)
{
	char template[100];
	int readfd = -1;
	int writefd = -1;
	int error;
	struct stat sb;
	pipe2file_t *p2f = NULL;

	fprintf(stderr, "%s: checking if we need pipe wrap\r\n", __func__);

	// Check if it is a pipe
	error = fstat(*srcfd, &sb);

	if (error != 0)
		return;

	if (!S_ISFIFO(sb.st_mode))
		return;

	p2f = (pipe2file_t *)malloc(sizeof (pipe2file_t));
	if (p2f == NULL)
		return;

	fprintf(stderr, "%s: is pipe: work on fd %d\r\n", __func__, *srcfd);

	snprintf(template, sizeof (template), "/tmp/.zfs.pipe.XXXXXX");

	mktemp(template);

	mkfifo(template, 0600);

	readfd = open(template, O_RDONLY | O_NONBLOCK);

	fprintf(stderr, "%s: readfd %d (%d)\r\n", __func__, readfd, error);

	writefd = open(template, O_WRONLY | O_NONBLOCK);

	fprintf(stderr, "%s: writefd %d (%d)\r\n", __func__, writefd, error);

	// set it to delete
	unlink(template);

	// Check delayed so unlink() is always called.
	if (readfd < 0)
		goto out;
	if (writefd < 0)
		goto out;

	/* Open needs NONBLOCK, so switch back to BLOCK */
	int flags;
	flags = fcntl(readfd, F_GETFL);
	flags &= ~O_NONBLOCK;
	fcntl(readfd, F_SETFL, flags);
	flags = fcntl(writefd, F_GETFL);
	flags &= ~O_NONBLOCK;
	fcntl(writefd, F_SETFL, flags);

	// create IO thread

	// Send, kernel was to be given *srcfd - to write to.
	// Instead we give it writefd.
	// thread then uses read(readfd) -> write(*srcfd)
	if (send) {
		p2f->from = readfd;
		p2f->to = *srcfd;
	} else {
		p2f->from = *srcfd;
		p2f->to = writefd;
	}
	fprintf(stderr, "%s: spawning thread\r\n", __func__);

	// pthread kills all threads on exit, and pipe_io_relay may not
	// have fully completed.
	error = fork();
	if (error == 0) {

		// Close the fd we don't need
		if (send)
			close(writefd);
		else
			close(readfd);

		setsid();
		pipe_io_relay(p2f);
		_exit(0);
	}

	if (error < 0)
		goto out;

	// Return open(file) fd to kernel only after all error cases
	if (send) {
		*srcfd = writefd;
		close(readfd);
	} else {
		*srcfd = readfd;
		close(writefd);
	}
	return;

out:
	if (p2f != NULL)
		free(p2f);

	if (readfd >= 0)
		close(readfd);

	if (writefd >= 0)
		close(writefd);
}

void
libzfs_set_pipe_max(int infd)
{
	/* macOS automatically resizes */
}
