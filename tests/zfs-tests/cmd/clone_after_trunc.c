// SPDX-License-Identifier: CDDL-1.0

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>

#if defined(_GNU_SOURCE) && defined(__linux__)
_Static_assert(sizeof (loff_t) == sizeof (off_t),
	"loff_t and off_t must be the same size");
#endif

ssize_t
copy_file_range(int, off_t *, int, off_t *, size_t, unsigned int)
    __attribute__((weak));

#define	FILE_SIZE	(1024 * 1024)
#define	RECORD_SIZE	(128 * 1024)
#define	NUM_THREADS	64

const char *dir;
volatile int failed;

static void *
run_test(void *arg)
{
	int thread_id = (int)(long)arg;

	char src_path[PATH_MAX], dst_path[PATH_MAX];
	snprintf(src_path, PATH_MAX, "%s/src-%d", dir, thread_id);
	snprintf(dst_path, PATH_MAX, "%s/dst-%d", dir, thread_id);

	unsigned char *write_buf = malloc(FILE_SIZE);
	unsigned char *read_buf = malloc(FILE_SIZE);

	// Write out expected data.
	memset(write_buf, 0xAA, FILE_SIZE);
	int src = open(src_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (write(src, write_buf, FILE_SIZE) != FILE_SIZE)
		perror("write");
	close(src);

	// Create destination file so we exercise O_TRUNC.
	int dst = open(dst_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (write(dst, write_buf, FILE_SIZE) != FILE_SIZE)
		perror("write");
	fsync(dst);
	close(dst);

	// Open file with O_TRUNC and perform copy.
	src = open(src_path, O_RDONLY);
	dst = open(dst_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);

	off_t off_in = 0, off_out = 0;
	ssize_t ret =
	    copy_file_range(src, &off_in, dst, &off_out, FILE_SIZE, 0);
	if (ret != FILE_SIZE)
		perror("copy_file_range");
	close(src);
	close(dst);

	// Read back
	dst = open(dst_path, O_RDONLY);
	if (read(dst, read_buf, FILE_SIZE) != FILE_SIZE)
		perror("read");
	close(dst);

	// Bug check
	if (memcmp(write_buf, read_buf, FILE_SIZE) != 0) {
		failed = 1;
		fprintf(stderr, "[%d]: FAIL\n", thread_id);

		int all_zeros = 1;
		for (int i = 0; i < RECORD_SIZE; i++) {
			if (read_buf[i] != 0) {
				all_zeros = 0;
				break;
			}
		}

		if (all_zeros) {
			fprintf(stderr, "[%d]: ALL ZERO\n", thread_id);
		}
	}

	unlink(src_path);
	unlink(dst_path);
	free(write_buf);
	free(read_buf);
	return (NULL);
}

int
main(int argc, const char **argv)
{
	if (argc < 2) {
		fprintf(stderr, "usage: %s <dir>\n", argv[0]);
		return (1);
	}
	dir = argv[1];

	pthread_t threads[NUM_THREADS];

	for (int i = 0; i < NUM_THREADS; i++) {
		pthread_create(&threads[i], NULL, run_test, (void *)(long)i);
	}
	for (int i = 0; i < NUM_THREADS; i++) {
		pthread_join(threads[i], NULL);
	}

	return (failed);
}
