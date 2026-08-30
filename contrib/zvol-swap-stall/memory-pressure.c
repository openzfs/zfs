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

#define	_GNU_SOURCE

#include <sys/mman.h>

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define	MIB		(1UL << 20)
#define	PAGE_BYTES	4096UL
#define	CHUNK_BYTES	(8UL * MIB)

static void
usage(const char *program)
{
	(void) fprintf(stderr,
	    "usage: %s MIB SECONDS OOM_SCORE_ADJ MARKER_DIR\n", program);
}

static int
create_marker(int directory, const char *name)
{
	int marker = openat(directory, name,
	    O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0644);

	if (marker < 0)
		return (-1);
	return (close(marker));
}

static int
parse_long(const char *text, long minimum, long maximum, long *result)
{
	char *end;
	long value;

	errno = 0;
	value = strtol(text, &end, 10);
	if (errno != 0 || end == text || *end != '\0' || value < minimum ||
	    value > maximum)
		return (-1);

	*result = value;
	return (0);
}

static int
set_oom_score_adj(long value)
{
	FILE *stream = fopen("/proc/self/oom_score_adj", "w");
	int error = 0;

	if (stream == NULL)
		return (-1);
	if (fprintf(stream, "%ld\n", value) < 0)
		error = -1;
	if (fclose(stream) != 0)
		error = -1;
	return (error);
}

static int
meminfo_mib(uint64_t *free_ram, uint64_t *free_swap)
{
	FILE *stream = fopen("/proc/meminfo", "r");
	char *line = NULL;
	size_t capacity = 0;
	unsigned long long value;
	bool found_ram = false;
	bool found_swap = false;

	if (stream == NULL)
		return (-1);
	while (getline(&line, &capacity, stream) >= 0) {
		if (sscanf(line, "MemFree: %llu kB", &value) == 1) {
			*free_ram = value / 1024;
			found_ram = true;
		} else if (sscanf(line, "SwapFree: %llu kB", &value) == 1) {
			*free_swap = value / 1024;
			found_swap = true;
		}
	}
	free(line);
	(void) fclose(stream);
	return (found_ram && found_swap ? 0 : -1);
}

static uint64_t
monotonic_msec(void)
{
	struct timespec now;

	if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
		perror("clock_gettime");
		exit(2);
	}
	return ((uint64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000);
}

int
main(int argc, char **argv)
{
	unsigned char *mapping;
	volatile unsigned char *memory;
	uint64_t free_ram = 0, free_swap = 0, started, deadline;
	size_t bytes, chunk_start, chunk_end, offset;
	unsigned int passes = 0;
	unsigned char sink = 0;
	int marker_directory;
	long mib, seconds, oom_score_adj;

	if (argc != 5 || parse_long(argv[1], 1, LONG_MAX, &mib) != 0 ||
	    parse_long(argv[2], 1, LONG_MAX, &seconds) != 0 ||
	    parse_long(argv[3], -1000, 1000, &oom_score_adj) != 0 ||
	    (uint64_t)mib > SIZE_MAX / MIB) {
		usage(argv[0]);
		return (2);
	}
	bytes = (size_t)mib * MIB;
	marker_directory = open(argv[4], O_RDONLY | O_DIRECTORY | O_CLOEXEC);
	if (marker_directory < 0) {
		perror("open marker directory");
		return (2);
	}

	if (set_oom_score_adj(oom_score_adj) != 0) {
		perror("write /proc/self/oom_score_adj");
		return (2);
	}
	mapping = mmap(NULL, bytes, PROT_READ | PROT_WRITE,
	    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (mapping == MAP_FAILED) {
		perror("mmap");
		return (2);
	}
	memory = mapping;
	if (create_marker(marker_directory, "pressure.started") != 0) {
		perror("create pressure.started");
		return (2);
	}

	(void) printf("pressure-start mib=%ld oom_score_adj=%ld\n", mib,
	    oom_score_adj);
	(void) fflush(stdout);
	started = monotonic_msec();
	for (chunk_start = 0; chunk_start < bytes;
	    chunk_start += CHUNK_BYTES) {
		chunk_end = chunk_start + CHUNK_BYTES;
		if (chunk_end < chunk_start || chunk_end > bytes)
			chunk_end = bytes;
		for (offset = chunk_start; offset < chunk_end;
		    offset += PAGE_BYTES)
			memory[offset] = 0xaa;
		if (meminfo_mib(&free_ram, &free_swap) != 0) {
			(void) fprintf(stderr, "cannot read /proc/meminfo\n");
			return (2);
		}
		(void) printf("touch-progress touched_mib=%zu elapsed_ms=%llu "
		    "free_ram_mib=%llu free_swap_mib=%llu\n", chunk_end / MIB,
		    (unsigned long long)(monotonic_msec() - started),
		    (unsigned long long)free_ram,
		    (unsigned long long)free_swap);
		(void) fflush(stdout);
	}
	(void) printf("pressure-ready mib=%ld oom_score_adj=%ld\n", mib,
	    oom_score_adj);
	(void) fflush(stdout);
	if (create_marker(marker_directory, "pressure.ready") != 0) {
		perror("create pressure.ready");
		return (2);
	}

	deadline = monotonic_msec() + (uint64_t)seconds * 1000;
	while (monotonic_msec() < deadline) {
		struct timespec delay = { .tv_sec = 1 };

		for (offset = 0; offset < bytes; offset += PAGE_BYTES * 64)
			sink ^= memory[offset];
		passes++;
		(void) printf("pressure-heartbeat passes=%u\n", passes);
		(void) fflush(stdout);
		while (nanosleep(&delay, &delay) != 0 && errno == EINTR)
			;
	}
	(void) sink;
	(void) munmap(mapping, bytes);
	if (create_marker(marker_directory, "pressure.done") != 0) {
		perror("create pressure.done");
		return (2);
	}
	(void) close(marker_directory);
	return (0);
}
