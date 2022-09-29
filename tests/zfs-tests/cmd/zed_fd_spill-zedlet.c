/*
 * Permission to use, copy, modify, and/or distribute this software for
 * any purpose with or without fee is hereby granted.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN
 * AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT
 * OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

int main(void) {
	if (fork()) {
		int err;
		wait(&err);
		return (err);
	}

	char buf[64];
	sprintf(buf, "/tmp/zts-zed_fd_spill-logdir/%d", getppid());
	int fd = creat(buf, 0644);
	if (fd == -1) {
		(void) fprintf(stderr, "creat(%s) failed: %s\n", buf,
		    strerror(errno));
		exit(EXIT_FAILURE);
	}

	if (dup2(fd, STDOUT_FILENO) == -1) {
		close(fd);
		(void) fprintf(stderr, "dup2(%s, STDOUT_FILENO) failed: %s\n",
		    buf, strerror(errno));
		exit(EXIT_FAILURE);
	}

	snprintf(buf, sizeof (buf), "/proc/%d/fd", getppid());
	execlp("ls", "ls", buf, NULL);
	_exit(127);
}
