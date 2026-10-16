// SPDX-License-Identifier: CDDL-1.0
/*
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this
 * source.  A copy of the CDDL is also available via the Internet at
 * https://opensource.org/license/CDDL-1.0.
 */

/*
 * Copyright (c) 2026 by George Melikov.
 */

/*
 * Hold a source-page fault during a large O_APPEND write, then append a
 * marker from another thread.  The marker must follow the whole first
 * write, not land in the range that the first writer later retries.
 *
 * usage: mmap_write_append_race <file>
 */

#ifndef _GNU_SOURCE
#define	_GNU_SOURCE
#endif

#ifdef __linux__

#include <sys/eventfd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <linux/userfaultfd.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define	WRITE_SIZE	(64 * 1024 * 1024)
#define	FAULT_OFFSET	(32 * 1024 * 1024)
#define	APPEND_WAIT_MS	5000
#define	FAULT_WAIT_MS	30000

struct write_args {
	int		fd;
	const void	*buffer;
	size_t		size;
	ssize_t		written;
	off_t		offset;
	int		error;
	int		event_fd;
};

static void *
write_thread(void *arg)
{
	struct write_args *args = arg;

	args->written = write(args->fd, args->buffer, args->size);
	args->error = args->written < 0 ? errno : 0;
	if (args->written >= 0) {
		args->offset = lseek(args->fd, 0, SEEK_CUR);
		if (args->offset < 0)
			args->error = errno;
	}
	if (args->event_fd >= 0) {
		uint64_t value = 1;
		if (write(args->event_fd, &value, sizeof (value)) !=
		    sizeof (value) && args->error == 0)
			args->error = errno;
	}

	return (NULL);
}

static int
resolve_page(int uffd, void *page, const void *data, size_t page_size)
{
	struct uffdio_copy copy = {
		.dst = (unsigned long)page,
		.src = (unsigned long)data,
		.len = page_size,
	};

	if (ioctl(uffd, UFFDIO_COPY, &copy) != 0)
		return (errno);

	return (0);
}

int
main(int argc, char *argv[])
{
	if (argc != 2) {
		(void) fprintf(stderr, "usage: %s <file>\n", argv[0]);
		return (2);
	}

	long page_size_long = sysconf(_SC_PAGESIZE);
	if (page_size_long <= 0) {
		(void) fprintf(stderr, "invalid page size\n");
		return (1);
	}
	size_t page_size = (size_t)page_size_long;
	if (FAULT_OFFSET % page_size != 0 || WRITE_SIZE % page_size != 0) {
		(void) fprintf(stderr,
		    "page size does not align with test range\n");
		return (1);
	}

#ifndef SYS_userfaultfd
	(void) fprintf(stderr,
	    "userfaultfd is unavailable on this architecture\n");
	return (77);
#else
	int uffd = syscall(SYS_userfaultfd, O_CLOEXEC | O_NONBLOCK);
	if (uffd < 0) {
		if (errno == ENOSYS || errno == EPERM || errno == EACCES) {
			(void) fprintf(stderr,
			    "userfaultfd is unavailable: %s\n",
			    strerror(errno));
			return (77);
		}
		perror("userfaultfd");
		return (1);
	}
#endif

	int result = 1;
	int registered = 0;
	int fd = -1;
	int append_fd = -1;
	int event_fd = -1;
	void *source = MAP_FAILED;
	void *fault_page = NULL;
	void *page_data = NULL;
	pthread_t first_thread;
	pthread_t append_thread;
	int first_started = 0;
	int append_started = 0;
	int append_finished = 0;
	struct write_args first = { 0 };
	struct write_args append = { 0 };

	struct uffdio_api api = {
		.api = UFFD_API,
	};
	if (ioctl(uffd, UFFDIO_API, &api) != 0) {
		if (errno == ENOSYS || errno == EPERM || errno == EACCES) {
			(void) fprintf(stderr,
			    "userfaultfd API is unavailable: %s\n",
			    strerror(errno));
			result = 77;
		} else {
			perror("UFFDIO_API");
		}
		goto out;
	}

	source = mmap(NULL, WRITE_SIZE, PROT_READ | PROT_WRITE,
	    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (source == MAP_FAILED) {
		perror("mmap");
		goto out;
	}
	(void) memset(source, 0xa5, WRITE_SIZE);
	fault_page = (char *)source + FAULT_OFFSET;

	if (posix_memalign(&page_data, page_size, page_size) != 0) {
		(void) fprintf(stderr, "cannot allocate fault page\n");
		goto out;
	}
	(void) memset(page_data, 0xa5, page_size);

	struct uffdio_register registration = {
		.range.start = (unsigned long)fault_page,
		.range.len = page_size,
		.mode = UFFDIO_REGISTER_MODE_MISSING,
	};
	if (ioctl(uffd, UFFDIO_REGISTER, &registration) != 0) {
		if (errno == ENOSYS || errno == EPERM || errno == EACCES ||
		    errno == EINVAL) {
			(void) fprintf(stderr,
			    "userfaultfd registration unavailable: %s\n",
			    strerror(errno));
			result = 77;
		} else {
			perror("UFFDIO_REGISTER");
		}
		goto out;
	}
	registered = 1;

	if (madvise(fault_page, page_size, MADV_DONTNEED) != 0) {
		perror("madvise");
		goto out;
	}

	fd = open(argv[1], O_RDWR | O_CREAT | O_TRUNC | O_APPEND, 0666);
	if (fd < 0) {
		perror(argv[1]);
		goto out;
	}
	append_fd = open(argv[1], O_WRONLY | O_APPEND);
	if (append_fd < 0) {
		perror(argv[1]);
		goto out;
	}
	event_fd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
	if (event_fd < 0) {
		perror("eventfd");
		goto out;
	}

	char marker = 0x5a;
	first.fd = fd;
	first.buffer = source;
	first.size = WRITE_SIZE;
	first.event_fd = -1;
	append.fd = append_fd;
	append.buffer = &marker;
	append.size = sizeof (marker);
	append.event_fd = event_fd;

	int error = pthread_create(&first_thread, NULL, write_thread, &first);
	if (error != 0) {
		(void) fprintf(stderr, "pthread_create: %s\n", strerror(error));
		goto out;
	}
	first_started = 1;

	struct pollfd fault_poll = {
		.fd = uffd,
		.events = POLLIN,
	};
	int polled;
	do {
		polled = poll(&fault_poll, 1, FAULT_WAIT_MS);
	} while (polled < 0 && errno == EINTR);
	if (polled != 1) {
		if (polled == 0)
			(void) fprintf(stderr,
			    "timed out waiting for source fault\n");
		else
			perror("poll userfaultfd");
		goto unblock;
	}

	struct uffd_msg message;
	ssize_t bytes = read(uffd, &message, sizeof (message));
	if (bytes != sizeof (message) ||
	    message.event != UFFD_EVENT_PAGEFAULT ||
	    (message.arg.pagefault.address & ~(page_size - 1)) !=
	    (unsigned long)fault_page) {
		(void) fprintf(stderr, "unexpected userfaultfd event\n");
		goto unblock;
	}

	error = pthread_create(&append_thread, NULL, write_thread, &append);
	if (error != 0) {
		(void) fprintf(stderr, "pthread_create: %s\n", strerror(error));
		goto unblock;
	}
	append_started = 1;

	struct pollfd append_poll = {
		.fd = event_fd,
		.events = POLLIN,
	};
	do {
		polled = poll(&append_poll, 1, APPEND_WAIT_MS);
	} while (polled < 0 && errno == EINTR);
	if (polled == 1) {
		uint64_t value;
		if (read(event_fd, &value, sizeof (value)) != sizeof (value)) {
			perror("read eventfd");
			goto unblock;
		}
		append_finished = 1;
	} else if (polled < 0) {
		perror("poll append writer");
		goto unblock;
	}

unblock:
	error = resolve_page(uffd, fault_page, page_data, page_size);
	if (error != 0) {
		(void) fprintf(stderr, "UFFDIO_COPY: %s\n", strerror(error));
		(void) close(uffd);
		uffd = -1;
		goto join;
	}

join:
	if (first_started) {
		error = pthread_join(first_thread, NULL);
		if (error != 0) {
			(void) fprintf(stderr, "pthread_join: %s\n",
			    strerror(error));
			goto out;
		}
		first_started = 0;
	}
	if (append_started) {
		error = pthread_join(append_thread, NULL);
		if (error != 0) {
			(void) fprintf(stderr, "pthread_join: %s\n",
			    strerror(error));
			goto out;
		}
		append_started = 0;
	}

	if (!append_finished) {
		(void) fprintf(stderr, "competing append did not finish before "
		    "source was unblocked\n");
		goto out;
	}
	if (first.error != 0 || first.written != WRITE_SIZE) {
		(void) fprintf(stderr, "first write returned %zd: %s\n",
		    first.written, strerror(first.error));
		goto out;
	}
	if (append.error != 0 || append.written != sizeof (marker)) {
		(void) fprintf(stderr, "competing append returned %zd: %s\n",
		    append.written, strerror(append.error));
		goto out;
	}

	struct stat st;
	if (fstat(fd, &st) != 0) {
		perror("fstat");
		goto out;
	}

	char gap_byte;
	if (pread(fd, &gap_byte, 1, FAULT_OFFSET) != 1) {
		perror("pread append gap");
		goto out;
	}
	int markers = gap_byte == marker;
	if (st.st_size > WRITE_SIZE) {
		char end_byte;
		if (pread(fd, &end_byte, 1, WRITE_SIZE) != 1) {
			perror("pread end of file");
			goto out;
		}
		markers += end_byte == marker;
	}
	if (markers != 1) {
		(void) fprintf(stderr,
		    "expected one append marker at offset %d or %d, found %d\n",
		    FAULT_OFFSET, WRITE_SIZE, markers);
		goto out;
	}
	off_t expected_offset = gap_byte == marker ? WRITE_SIZE + 1 :
	    WRITE_SIZE;
	if (first.offset != expected_offset) {
		(void) fprintf(stderr,
		    "expected first writer offset %lld, got %lld\n",
		    (long long)expected_offset, (long long)first.offset);
		goto out;
	}
	if (st.st_size != WRITE_SIZE + 1) {
		(void) fprintf(stderr, "expected file size %d, got %lld\n",
		    WRITE_SIZE + 1, (long long)st.st_size);
		goto out;
	}

	result = 0;

out:
	if (first_started)
		(void) pthread_join(first_thread, NULL);
	if (append_started)
		(void) pthread_join(append_thread, NULL);
	if (registered && uffd >= 0) {
		struct uffdio_range range = {
			.start = (unsigned long)fault_page,
			.len = page_size,
		};
		(void) ioctl(uffd, UFFDIO_UNREGISTER, &range);
	}
	if (event_fd >= 0)
		(void) close(event_fd);
	if (append_fd >= 0)
		(void) close(append_fd);
	if (fd >= 0)
		(void) close(fd);
	if (uffd >= 0)
		(void) close(uffd);
	if (page_data != NULL)
		free(page_data);
	if (source != MAP_FAILED)
		(void) munmap(source, WRITE_SIZE);

	return (result);
}

#else

#include <stdio.h>

int
main(void)
{
	(void) fprintf(stderr, "mmap_write_append_race requires Linux\n");
	return (77);
}

#endif
