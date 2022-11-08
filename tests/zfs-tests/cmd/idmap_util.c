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

#ifndef _GNU_SOURCE
#define	_GNU_SOURCE
#endif

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <linux/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/mount.h>
#include <fcntl.h>
#include <errno.h>
#include <sched.h>
#include <syscall.h>
#include <sys/socket.h>

#include <sys/list.h>

#ifndef UINT_MAX
#define	UINT_MAX	4294967295U
#endif

#ifndef __NR_Linux
#if defined __alpha__
#define	__NR_Linux 110
#elif defined _MIPS_SIM
#if _MIPS_SIM == _MIPS_SIM_ABI32
#define	__NR_Linux 4000
#endif
#if _MIPS_SIM == _MIPS_SIM_NABI32
#define	__NR_Linux 6000
#endif
#if _MIPS_SIM == _MIPS_SIM_ABI64
#define	__NR_Linux 5000
#endif
#elif defined __ia64__
#define	__NR_Linux 1024
#else
#define	__NR_Linux 0
#endif
#endif

#ifndef __NR_mount_setattr
#define	__NR_mount_setattr (442 + __NR_Linux)
#endif

#ifndef __NR_open_tree
#define	__NR_open_tree (428 + __NR_Linux)
#endif

#ifndef __NR_move_mount
#define	__NR_move_mount (429 + __NR_Linux)
#endif

#ifndef MNT_DETACH
#define	MNT_DETACH 2
#endif

#ifndef MOVE_MOUNT_F_EMPTY_PATH
#define	MOVE_MOUNT_F_EMPTY_PATH 0x00000004
#endif

#ifndef MOUNT_ATTR_IDMAP
#define	MOUNT_ATTR_IDMAP 0x00100000
#endif

#ifndef OPEN_TREE_CLONE
#define	OPEN_TREE_CLONE 1
#endif

#ifndef OPEN_TREE_CLOEXEC
#define	OPEN_TREE_CLOEXEC O_CLOEXEC
#endif

#ifndef AT_RECURSIVE
#define	AT_RECURSIVE 0x8000
#endif

typedef struct {
	__u64 attr_set;
	__u64 attr_clr;
	__u64 propagation;
	__u64 userns_fd;
} mount_attr_t;

static inline int
sys_mount_setattr(int dfd, const char *path, unsigned int flags,
    mount_attr_t *attr, size_t size)
{
	return (syscall(__NR_mount_setattr, dfd, path, flags, attr, size));
}

static inline int
sys_open_tree(int dfd, const char *filename, unsigned int flags)
{
	return (syscall(__NR_open_tree, dfd, filename, flags));
}

static inline int sys_move_mount(int from_dfd, const char *from_pathname,
    int to_dfd, const char *to_pathname, unsigned int flags)
{
	return (syscall(__NR_move_mount, from_dfd, from_pathname, to_dfd,
	    to_pathname, flags));
}

typedef enum idmap_type_t {
	TYPE_UID,
	TYPE_GID,
	TYPE_BOTH
} idmap_type_t;

struct idmap_entry {
	__u32 first;
	__u32 lower_first;
	__u32 count;
	idmap_type_t type;
	list_node_t node;
};

static void
log_msg(const char *msg, ...)
{
	va_list ap;

	va_start(ap, msg);
	vfprintf(stderr, msg, ap);
	fputc('\n', stderr);
	va_end(ap);
}

#define	log_errno(msg, args...) \
	do { \
		log_msg("%s:%d:%s: [%m] " msg, __FILE__, __LINE__,\
		    __FUNCTION__, ##args); \
	} while (0)

/*
 * Parse the idmapping in the following format
 * and add to the list:
 *
 *   u:nsid_first:hostid_first:count
 *   g:nsid_first:hostid_first:count
 *   b:nsid_first:hostid_first:count
 *
 * The delimiter can be : or space character.
 *
 * Return:
 *   0      if success
 *   ENOMEM if out of memory
 *   EINVAL if wrong arg or input
 */
static int
parse_idmap_entry(list_t *head, char *input)
{
	char *token, *savedptr = NULL;
	struct idmap_entry *entry;
	unsigned long ul;
	char *delimiter = (char *)": ";
	char c;

	if (!input || !head)
		return (EINVAL);
	entry = malloc(sizeof (*entry));
	if (!entry)
		return (ENOMEM);

	token = strtok_r(input, delimiter, &savedptr);
	if (token)
		c = token[0];
	if (!token || (c != 'b' && c != 'u' && c != 'g'))
		goto errout;
	entry->type = (c == 'b') ? TYPE_BOTH :
	    ((c == 'u') ? TYPE_UID : TYPE_GID);

	token = strtok_r(NULL, delimiter, &savedptr);
	if (!token)
		goto errout;
	ul = strtoul(token, NULL, 10);
	if (ul > UINT_MAX || errno != 0)
		goto errout;
	entry->first = (__u32)ul;

	token = strtok_r(NULL, delimiter, &savedptr);
	if (!token)
		goto errout;
	ul = strtoul(token, NULL, 10);
	if (ul > UINT_MAX || errno != 0)
		goto errout;
	entry->lower_first = (__u32)ul;

	token = strtok_r(NULL, delimiter, &savedptr);
	if (!token)
		goto errout;
	ul = strtoul(token, NULL, 10);
	if (ul > UINT_MAX || errno != 0)
		goto errout;
	entry->count = (__u32)ul;

	list_insert_tail(head, entry);

	return (0);

errout:
	free(entry);
	return (EINVAL);
}

/*
 * Release all the entries in the list
 */
static void
free_idmap(list_t *head)
{
	struct idmap_entry *entry;

	while ((entry = list_remove_head(head)) != NULL)
		free(entry);
	/* list_destroy() to be done by the caller */
}

/*
 * Write all bytes in the buffer to fd
 */
static ssize_t
write_buf(int fd, const char *buf, size_t buf_size)
{
	ssize_t written, total_written = 0;
	size_t remaining = buf_size;
	char *position = (char *)buf;

	for (;;) {
		written = write(fd, position, remaining);
		if (written < 0 && errno == EINTR)
			continue;
		if (written < 0) {
			log_errno("write");
			return (written);
		}
		total_written += written;
		if (total_written == buf_size)
			break;
		remaining -= written;
		position += written;
	}

	return (total_written);
}

/*
 * Read data from file into buffer
 */
static ssize_t
read_buf(int fd, char *buf, size_t buf_size)
{
	int ret;
	for (;;) {
		ret = read(fd, buf, buf_size);
		if (ret < 0 && errno == EINTR)
			continue;
		break;
	}
	if (ret < 0)
		log_errno("read");
	return (ret);
}

/*
 * Write idmap of the given type in the buffer to the
 * process' uid_map or gid_map proc file.
 *
 * Return:
 *   0     if success
 *   errno if there's any error
 */
static int
write_idmap(pid_t pid, char *buf, size_t buf_size, idmap_type_t type)
{
	char path[PATH_MAX];
	int fd = -EBADF;
	int ret;

	(void) snprintf(path, sizeof (path), "/proc/%d/%cid_map",
	    pid, type == TYPE_UID ? 'u' : 'g');
	fd = open(path, O_WRONLY | O_CLOEXEC);
	if (fd < 0) {
		ret = errno;
		log_errno("open(%s)", path);
		goto out;
	}
	ret = write_buf(fd, buf, buf_size);
	if (ret < 0)
		ret = errno;
	else
		ret = 0;
out:
	if (fd >= 0)
		close(fd);
	return (ret);
}

/*
 * Write idmap info in the list to the process
 * user namespace, i.e. its /proc/<pid>/uid_map
 * and /proc/<pid>/gid_map file.
 *
 * Return:
 *   0     if success
 *   errno if it fails
 */
static int
write_pid_idmaps(pid_t pid, list_t *head)
{
	char *buf_uids, *buf_gids;
	char *curr_bufu, *curr_bufg;
	/* max 4k to be allowed for each map */
	int size_buf_uids = 4096, size_buf_gids = 4096;
	struct idmap_entry *entry;
	int uid_filled, gid_filled;
	int ret = 0;
	int has_uids = 0, has_gids = 0;
	size_t buf_size;

	buf_uids = malloc(size_buf_uids);
	if (!buf_uids)
		return (ENOMEM);
	buf_gids = malloc(size_buf_gids);
	if (!buf_gids) {
		free(buf_uids);
		return (ENOMEM);
	}
	curr_bufu = buf_uids;
	curr_bufg = buf_gids;

	for (entry = list_head(head); entry; entry = list_next(head, entry)) {
		if (entry->type == TYPE_UID || entry->type == TYPE_BOTH) {
			uid_filled = snprintf(curr_bufu, size_buf_uids,
			    "%u %u %u\n", entry->first, entry->lower_first,
			    entry->count);
			if (uid_filled <= 0 || uid_filled >= size_buf_uids) {
				ret = E2BIG;
				goto out;
			}
			curr_bufu += uid_filled;
			size_buf_uids -= uid_filled;
			has_uids = 1;
		}
		if (entry->type == TYPE_GID || entry->type == TYPE_BOTH) {
			gid_filled = snprintf(curr_bufg, size_buf_gids,
			    "%u %u %u\n", entry->first, entry->lower_first,
			    entry->count);
			if (gid_filled <= 0 || gid_filled >= size_buf_gids) {
				ret = E2BIG;
				goto out;
			}
			curr_bufg += gid_filled;
			size_buf_gids -= gid_filled;
			has_gids = 1;
		}
	}
	if (has_uids) {
		buf_size = curr_bufu - buf_uids;
		ret = write_idmap(pid, buf_uids, buf_size, TYPE_UID);
		if (ret)
			goto out;
	}
	if (has_gids) {
		buf_size = curr_bufg - buf_gids;
		ret = write_idmap(pid, buf_gids, buf_size, TYPE_GID);
	}

out:
	free(buf_uids);
	free(buf_gids);
	return (ret);
}

/*
 * Wait for the child process to exit
 * and reap it.
 *
 * Return:
 *   process exit code if available
 */
static int
wait_for_pid(pid_t pid)
{
	int status;
	int ret;

	for (;;) {
		ret = waitpid(pid, &status, 0);
		if (ret < 0) {
			if (errno == EINTR)
				continue;
			return (EXIT_FAILURE);
		}
		break;
	}
	if (!WIFEXITED(status))
		return (EXIT_FAILURE);
	return (WEXITSTATUS(status));
}

/*
 * Get the file descriptor of the process user namespace
 * given its pid.
 *
 * Return:
 *   fd  if success
 *   -1  if it fails
 */
static int
userns_fd_from_pid(pid_t pid)
{
	int fd;
	char path[PATH_MAX];

	(void) snprintf(path, sizeof (path), "/proc/%d/ns/user", pid);
	fd = open(path, O_RDONLY | O_CLOEXEC);
	if (fd < 0)
		log_errno("open(%s)", path);
	return (fd);
}

/*
 * Get the user namespace file descriptor given a list
 * of idmap info.
 *
 * Return:
 *   fd     if success
 *   -errno if it fails
 */
static int
userns_fd_from_idmap(list_t *head)
{
	pid_t pid;
	int ret, fd;
	int fds[2];
	char c;
	int saved_errno = 0;

	/* socketpair for bidirectional communication */
	ret = socketpair(AF_LOCAL, SOCK_STREAM | SOCK_CLOEXEC, 0, fds);
	if (ret) {
		log_errno("socketpair");
		return (-errno);
	}

	pid = fork();
	if (pid < 0) {
		log_errno("fork");
		fd = -errno;
		goto out;
	}

	if (pid == 0) {
		/* child process */
		ret = unshare(CLONE_NEWUSER);
		if (ret == 0) {
			/* notify the parent of success */
			ret = write_buf(fds[1], "1", 1);
			if (ret < 0)
				saved_errno = errno;
			else {
				/*
				 * Until the parent has written to idmap,
				 * we cannot exit, otherwise the defunct
				 * process is owned by the real root, writing
				 * to its idmap ends up with EPERM in the
				 * context of a user ns
				 */
				ret = read_buf(fds[1], &c, 1);
				if (ret < 0)
					saved_errno = errno;
			}
		} else {
			saved_errno = errno;
			log_errno("unshare");
			ret = write_buf(fds[1], "0", 1);
			if (ret < 0)
				saved_errno = errno;
		}
		exit(saved_errno);
	}

	/* parent process */
	ret = read_buf(fds[0], &c, 1);
	if (ret == 1 && c == '1') {
		ret = write_pid_idmaps(pid, head);
		if (!ret) {
			fd = userns_fd_from_pid(pid);
			if (fd < 0)
				fd = -errno;
		} else {
			fd = -ret;
		}
		/* Let child know it can exit */
		(void) write_buf(fds[0], "1", 1);
	} else {
		fd = -EBADF;
	}
	(void) wait_for_pid(pid);
out:
	close(fds[0]);
	close(fds[1]);
	return (fd);
}

/*
 * Check if the operating system supports idmapped mount on the
 * given path or not.
 *
 * Return:
 *   true  if supported
 *   false if not supported
 */
static bool
is_idmap_supported(char *path)
{
	list_t head;
	int ret;
	int tree_fd = -EBADF, path_fd = -EBADF;
	mount_attr_t attr = {
	    .attr_set	= MOUNT_ATTR_IDMAP,
	    .userns_fd  = -EBADF,
	};

	/* strtok_r() won't be happy with a const string */
	/* To check if idmapped mount can be done in a user ns, map 0 to 0 */
	char *input = strdup("b:0:0:1");

	if (!input) {
		errno = ENOMEM;
		log_errno("strdup");
		return (false);
	}

	list_create(&head, sizeof (struct idmap_entry),
	    offsetof(struct idmap_entry, node));
	ret = parse_idmap_entry(&head, input);
	if (ret) {
		errno = ret;
		log_errno("parse_idmap_entry(%s)", input);
		goto out1;
	}
	ret = userns_fd_from_idmap(&head);
	if (ret < 0)
		goto out1;
	attr.userns_fd = ret;
	ret = openat(-EBADF, path, O_DIRECTORY | O_CLOEXEC);
	if (ret < 0) {
		log_errno("openat(%s)", path);
		goto out;
	}
	path_fd = ret;
	ret = sys_open_tree(path_fd, "", AT_EMPTY_PATH | AT_NO_AUTOMOUNT |
	    AT_SYMLINK_NOFOLLOW | OPEN_TREE_CLOEXEC | OPEN_TREE_CLONE);
	if (ret < 0) {
		log_errno("sys_open_tree");
		goto out;
	}
	tree_fd = ret;
	ret = sys_mount_setattr(tree_fd, "", AT_EMPTY_PATH, &attr,
	    sizeof (attr));
	if (ret < 0) {
		log_errno("sys_mount_setattr");
	}
out:
	close(attr.userns_fd);
out1:
	free_idmap(&head);
	list_destroy(&head);
	if (tree_fd >= 0)
		close(tree_fd);
	if (path_fd >= 0)
		close(path_fd);
	free(input);
	return (ret == 0);
}

/*
 * Check if the given path is a mount point or not.
 *
 * Return:
 *   true  if it is
 *   false otherwise
 */
static bool
is_mountpoint(char *path)
{
	char *parent;
	struct stat st_me, st_parent;
	bool ret;

	parent = malloc(strlen(path)+4);
	if (!parent) {
		errno = ENOMEM;
		log_errno("malloc");
		return (false);
	}
	strcat(strcpy(parent, path), "/..");
	if (lstat(path, &st_me) != 0 ||
	    lstat(parent, &st_parent) != 0)
		ret = false;
	else
		if (st_me.st_dev != st_parent.st_dev ||
		    st_me.st_ino == st_parent.st_ino)
			ret = true;
		else
			ret = false;
	free(parent);
	return (ret);
}

/*
 * Remount the source on the new target folder with the given
 * list of idmap info. If target is NULL, the source will be
 * unmounted and then remounted if it is a mountpoint, otherwise
 * no unmount is done, the source is simply idmap remounted.
 *
 * Return:
 *   0      if success
 *   -errno otherwise
 */
static int
do_idmap_mount(list_t *idmap, char *source, char *target, int flags)
{
	int ret;
	int tree_fd = -EBADF, source_fd = -EBADF;
	mount_attr_t attr = {
	    .attr_set   = MOUNT_ATTR_IDMAP,
	    .userns_fd  = -EBADF,
	};

	ret = userns_fd_from_idmap(idmap);
	if (ret < 0)
		goto out1;
	attr.userns_fd = ret;
	ret = openat(-EBADF, source, O_DIRECTORY | O_CLOEXEC);
	if (ret < 0) {
		ret = -errno;
		log_errno("openat(%s)", source);
		goto out;
	}
	source_fd = ret;
	ret = sys_open_tree(source_fd, "", AT_EMPTY_PATH | AT_NO_AUTOMOUNT |
	    AT_SYMLINK_NOFOLLOW | OPEN_TREE_CLOEXEC | OPEN_TREE_CLONE | flags);
	if (ret < 0) {
		ret = -errno;
		log_errno("sys_open_tree");
		goto out;
	}
	tree_fd = ret;
	ret = sys_mount_setattr(tree_fd, "", AT_EMPTY_PATH | flags, &attr,
	    sizeof (attr));
	if (ret < 0) {
		ret = -errno;
		log_errno("sys_mount_setattr");
		goto out;
	}
	if (target == NULL && is_mountpoint(source)) {
		ret = umount2(source, MNT_DETACH);
		if (ret < 0) {
			ret = -errno;
			log_errno("umount2(%s)", source);
			goto out;
		}
	}
	ret = sys_move_mount(tree_fd, "", -EBADF, target == NULL ?
	    source : target, MOVE_MOUNT_F_EMPTY_PATH);
	if (ret < 0) {
		ret = -errno;
		log_errno("sys_move_mount(%s)", target == NULL ?
		    source : target);
	}
out:
	close(attr.userns_fd);
out1:
	if (tree_fd >= 0)
		close(tree_fd);
	if (source_fd >= 0)
		close(source_fd);
	return (ret);
}

static void
print_usage(char *argv[])
{
	fprintf(stderr, "Usage: %s [-r] [-c] [-m <idmap1>] [-m <idmap2>]" \
	    " ... [<source>] [<target>]\n", argv[0]);
	fprintf(stderr, "\n");
	fprintf(stderr, "  -r Recursively do idmapped mount.\n");
	fprintf(stderr, "\n");
	fprintf(stderr, "  -c Checks if idmapped mount is supported " \
	    "on the <source> by the operating system or not.\n");
	fprintf(stderr, "\n");
	fprintf(stderr, "  -m <idmap> to specify the idmap info, " \
	    "in the following format:\n");
	fprintf(stderr, "     <id_type>:<nsid_first>:<hostid_first>:<count>\n");
	fprintf(stderr, "\n");
	fprintf(stderr, "  <id_type> can be either of 'b', 'u', and 'g'.\n");
	fprintf(stderr, "\n");
	fprintf(stderr, "The <source> folder will be mounted at <target> " \
	    "with the provided idmap information.\nIf no <target> is " \
	    "specified, and <source> is a mount point, " \
	    "then <source> will be unmounted and then remounted.\n");
}

int
main(int argc, char *argv[])
{
	int opt;
	list_t idmap_head;
	int check_supported = 0;
	int ret = EXIT_SUCCESS;
	char *source = NULL, *target = NULL;
	int flags = 0;

	list_create(&idmap_head, sizeof (struct idmap_entry),
	    offsetof(struct idmap_entry, node));

	while ((opt = getopt(argc, argv, "rcm:")) != -1) {
		switch (opt) {
		case 'r':
			flags |= AT_RECURSIVE;
			break;
		case 'c':
			check_supported = 1;
			break;
		case 'm':
			ret = parse_idmap_entry(&idmap_head, optarg);
			if (ret) {
				errno = ret;
				log_errno("parse_idmap_entry(%s)", optarg);
				ret = EXIT_FAILURE;
				goto out;
			}
			break;
		default:
			print_usage(argv);
			exit(EXIT_FAILURE);
		}
	}

	if (check_supported == 0 && list_is_empty(&idmap_head))	{
		print_usage(argv);
		ret = EXIT_FAILURE;
		goto out;
	}

	if (optind >= argc) {
		fprintf(stderr, "Expected to have <source>, <target>.\n");
		print_usage(argv);
		ret = EXIT_FAILURE;
		goto out;
	}

	source = argv[optind];
	if (optind < (argc - 1)) {
		target = argv[optind + 1];
	}

	if (check_supported) {
		free_idmap(&idmap_head);
		list_destroy(&idmap_head);
		if (is_idmap_supported(source)) {
			printf("idmapped mount is supported on [%s].\n",
			    source);
			return (EXIT_SUCCESS);
		} else {
			printf("idmapped mount is NOT supported.\n");
			return (EXIT_FAILURE);
		}
	}

	ret = do_idmap_mount(&idmap_head, source, target, flags);
	if (ret)
		ret = EXIT_FAILURE;
out:
	free_idmap(&idmap_head);
	list_destroy(&idmap_head);

	exit(ret);
}
