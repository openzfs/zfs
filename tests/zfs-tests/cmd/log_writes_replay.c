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
 * Reader and replayer for Linux dm-log-writes logs.
 *
 * dm-log-writes records every write to a device, in the order the device
 * made it durable: writes are appended when a FLUSH covering them completes
 * (FUA writes immediately), and user "mark" messages are appended in order.
 * Replaying the log up to a FLUSH or FUA entry onto a copy of the device's
 * initial contents reproduces a state the device could hold after a power
 * loss at that point.  The format is described in the kernel's
 * Documentation/admin-guide/device-mapper/log-writes.rst and
 * drivers/md/dm-log-writes.c: a super block, then for each entry one
 * sector holding the entry header (and a mark's text), followed by the
 * entry's data.  Sizes are in units of the logged device's sector size.
 *
 * Usage:
 *   log_writes_replay list <log>
 *	Print "<index> <flags> <sector> <nr_sectors> <zil> [mark]" per entry,
 *	where <zil> counts the ZFS intent log blocks the write starts (found by
 *	the checksum magic of their leading zil_chain_t), in either byte order.
 *   log_writes_replay mark <log> <name>
 *	Print the index of the first mark entry called <name>.
 *   log_writes_replay points <log> <start-mark> <end-mark>
 *	Print the index of every FLUSH or FUA entry after <start-mark> and
 *	before <end-mark>, then the index of <end-mark>.
 *   log_writes_replay replay <log> <image> <first> <last> [skip...]
 *	Apply entries <first>..<last> (inclusive) to <image>: writes are
 *	copied, discards are punched out (the image must start zeroed).
 *	Entries listed as <skip> are left out, modelling writes a device lost
 *	from its volatile cache before a FLUSH covering them completed.
 */

#include <endian.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define	LOG_FLUSH_FLAG		(1 << 0)
#define	LOG_FUA_FLAG		(1 << 1)
#define	LOG_DISCARD_FLAG	(1 << 2)
#define	LOG_MARK_FLAG		(1 << 3)
#define	LOG_METADATA_FLAG	(1 << 4)

#define	WRITE_LOG_VERSION	1ULL

/* zio_eck_t magic, at this offset of a ZILOG2 block's leading zil_chain_t */
#define	ZEC_MAGIC		0x210da7ab10c7a11ULL
#define	ZIL_CHAIN_MAGIC_OFF	144
#define	WRITE_LOG_MAGIC		0x6a736677736872ULL

struct log_write_super {
	uint64_t magic;
	uint64_t version;
	uint64_t nr_entries;
	uint32_t sectorsize;
} __attribute__((packed));

struct log_write_entry {
	uint64_t sector;
	uint64_t nr_sectors;
	uint64_t flags;
	uint64_t data_len;
} __attribute__((packed));

typedef struct entry {
	uint64_t index;
	uint64_t sector;
	uint64_t nr_sectors;
	uint64_t flags;
	off_t data_off;		/* offset of the data in the log */
	char mark[256];
} entry_t;

typedef struct log {
	int fd;
	uint32_t sectorsize;
	uint64_t nr_entries;
	uint64_t next;		/* index of the next entry to read */
	off_t off;		/* log offset of the next entry */
	off_t size;		/* size of the log */
	char *sector;		/* one sector buffer */
} log_t;

static void
fail(const char *fmt, const char *arg)
{
	(void) fprintf(stderr, "log_writes_replay: ");
	(void) fprintf(stderr, fmt, arg);
	(void) fprintf(stderr, "%s%s\n", errno ? ": " : "",
	    errno ? strerror(errno) : "");
	exit(2);
}

static void
read_exact(int fd, void *buf, size_t len, off_t off)
{
	while (len > 0) {
		ssize_t n = pread(fd, buf, len, off);
		if (n <= 0) {
			if (n == 0)
				errno = 0;
			fail("%s", "short read from log");
		}
		buf = (char *)buf + n;
		len -= n;
		off += n;
	}
}

static void
log_open(log_t *lg, const char *path)
{
	struct log_write_super super;
	struct stat st;

	lg->fd = open(path, O_RDONLY);
	if (lg->fd < 0 || fstat(lg->fd, &st) != 0)
		fail("cannot open %s", path);
	lg->size = st.st_size;
	read_exact(lg->fd, &super, sizeof (super), 0);
	errno = 0;
	if (le64toh(super.magic) != WRITE_LOG_MAGIC)
		fail("%s is not a dm-log-writes log", path);
	if (le64toh(super.version) != WRITE_LOG_VERSION)
		fail("%s has an unsupported log version", path);
	lg->sectorsize = le32toh(super.sectorsize);
	if (lg->sectorsize < sizeof (struct log_write_entry) ||
	    (lg->sectorsize & (lg->sectorsize - 1)) != 0)
		fail("%s has an invalid sector size", path);
	lg->nr_entries = le64toh(super.nr_entries);
	lg->next = 0;
	lg->off = lg->sectorsize;
	lg->sector = malloc(lg->sectorsize);
	if (lg->sector == NULL)
		fail("%s", "out of memory");
}

/* Returns 0 at the end of the log. */
static int
log_next(log_t *lg, entry_t *e)
{
	struct log_write_entry raw;
	uint64_t data_len;

	if (lg->next >= lg->nr_entries)
		return (0);
	read_exact(lg->fd, lg->sector, lg->sectorsize, lg->off);
	(void) memcpy(&raw, lg->sector, sizeof (raw));
	e->index = lg->next;
	e->sector = le64toh(raw.sector);
	e->nr_sectors = le64toh(raw.nr_sectors);
	e->flags = le64toh(raw.flags);
	data_len = le64toh(raw.data_len);
	e->mark[0] = '\0';
	if (e->flags & LOG_MARK_FLAG) {
		size_t max = lg->sectorsize - sizeof (raw);
		if (data_len > max)
			data_len = max;
		if (data_len >= sizeof (e->mark))
			data_len = sizeof (e->mark) - 1;
		(void) memcpy(e->mark, lg->sector + sizeof (raw), data_len);
		e->mark[data_len] = '\0';
	}
	lg->off += lg->sectorsize;
	e->data_off = lg->off;
	if (!(e->flags & LOG_DISCARD_FLAG))
		lg->off += (off_t)e->nr_sectors * lg->sectorsize;
	if (lg->off > lg->size) {
		errno = 0;
		fail("%s", "log is truncated");
	}
	lg->next++;
	return (1);
}

static uint64_t
find_mark(log_t *lg, const char *name)
{
	entry_t e;

	while (log_next(lg, &e))
		if ((e.flags & LOG_MARK_FLAG) && strcmp(e.mark, name) == 0)
			return (e.index);
	errno = 0;
	fail("mark %s not found", name);
	return (0);
}

/*
 * Number of intent log blocks that a write entry's data starts; blocks are
 * aligned to the vdev's sector size, at most the log's.
 */
static uint64_t
zil_blocks(log_t *lg, const entry_t *e)
{
	uint64_t len = e->nr_sectors * lg->sectorsize, n = 0, m;

	if (e->flags & (LOG_MARK_FLAG | LOG_DISCARD_FLAG))
		return (0);
	for (uint64_t off = 0; off + ZIL_CHAIN_MAGIC_OFF + sizeof (m) <= len;
	    off += lg->sectorsize) {
		read_exact(lg->fd, &m, sizeof (m),
		    e->data_off + off + ZIL_CHAIN_MAGIC_OFF);
		if (m == ZEC_MAGIC || m == __builtin_bswap64(ZEC_MAGIC))
			n++;
	}
	return (n);
}

static int
skipped(uint64_t index, int nskip, char **skip)
{
	for (int i = 0; i < nskip; i++)
		if (strtoull(skip[i], NULL, 10) == index)
			return (1);
	return (0);
}

static void
replay(log_t *lg, const char *image, uint64_t first, uint64_t last,
    int nskip, char **skip)
{
	size_t bufsize = 1024 * 1024;
	char *buf = malloc(bufsize);
	entry_t e;
	int fd;

	if (buf == NULL)
		fail("%s", "out of memory");
	fd = open(image, O_WRONLY);
	if (fd < 0)
		fail("cannot open %s", image);
	while (log_next(lg, &e) && e.index <= last) {
		off_t dst = (off_t)e.sector * lg->sectorsize;
		off_t len = (off_t)e.nr_sectors * lg->sectorsize;

		if (e.index < first || (e.flags & LOG_MARK_FLAG) || len == 0 ||
		    skipped(e.index, nskip, skip))
			continue;
		if (e.flags & LOG_DISCARD_FLAG) {
			if (fallocate(fd, FALLOC_FL_PUNCH_HOLE |
			    FALLOC_FL_KEEP_SIZE, dst, len) != 0)
				fail("cannot discard in %s", image);
			continue;
		}
		for (off_t done = 0; done < len; ) {
			size_t n = len - done > (off_t)bufsize ? bufsize :
			    (size_t)(len - done);
			read_exact(lg->fd, buf, n, e.data_off + done);
			if (pwrite(fd, buf, n, dst + done) != (ssize_t)n)
				fail("cannot write %s", image);
			done += n;
		}
	}
	if (fsync(fd) != 0 || close(fd) != 0)
		fail("cannot sync %s", image);
	free(buf);
}

int
main(int argc, char **argv)
{
	log_t lg;
	entry_t e;

	if (argc < 3) {
		(void) fprintf(stderr, "usage: %s list <log>\n"
		    "       %s mark <log> <name>\n"
		    "       %s points <log> <start-mark> <end-mark>\n"
		    "       %s replay <log> <image> <first> <last> [skip...]\n",
		    argv[0], argv[0], argv[0], argv[0]);
		return (2);
	}
	log_open(&lg, argv[2]);

	if (strcmp(argv[1], "list") == 0) {
		while (log_next(&lg, &e))
			(void) printf("%llu 0x%llx %llu %llu %llu %s\n",
			    (unsigned long long)e.index,
			    (unsigned long long)e.flags,
			    (unsigned long long)e.sector,
			    (unsigned long long)e.nr_sectors,
			    (unsigned long long)zil_blocks(&lg, &e), e.mark);
	} else if (strcmp(argv[1], "mark") == 0 && argc == 4) {
		(void) printf("%llu\n",
		    (unsigned long long)find_mark(&lg, argv[3]));
	} else if (strcmp(argv[1], "points") == 0 && argc == 5) {
		(void) find_mark(&lg, argv[3]);
		for (;;) {
			if (!log_next(&lg, &e)) {
				errno = 0;
				fail("mark %s not found", argv[4]);
			}
			if ((e.flags & LOG_MARK_FLAG) &&
			    strcmp(e.mark, argv[4]) == 0)
				break;
			if (e.flags & (LOG_FLUSH_FLAG | LOG_FUA_FLAG))
				(void) printf("%llu\n",
				    (unsigned long long)e.index);
		}
		(void) printf("%llu\n", (unsigned long long)e.index);
	} else if (strcmp(argv[1], "replay") == 0 && argc >= 6) {
		replay(&lg, argv[3], strtoull(argv[4], NULL, 10),
		    strtoull(argv[5], NULL, 10), argc - 6, argv + 6);
	} else {
		errno = 0;
		fail("invalid command %s", argv[1]);
	}
	return (0);
}
