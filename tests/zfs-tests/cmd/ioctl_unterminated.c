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
 * Pass strings that are not NUL-terminated to the kernel in fixed-size
 * string fields, to check that the kernel terminates them itself.
 *
 * ioctl_unterminated fru POOL GUID
 *	ZFS_IOC_VDEV_SETFRU with zc_value filled to the end with 'f' and no
 *	NUL.  zc_string, the field that follows zc_value, holds a marker
 *	string, so a kernel that reads past the end of zc_value picks the
 *	marker up as part of the FRU.  Prints sizeof (zc_value), which
 *	depends on the platform's MAXPATHLEN.
 *
 * ioctl_unterminated recv SNAPSHOT < STREAM
 *	A resumable receive of STREAM (read from stdin, a native stream with
 *	no compound header) into SNAPSHOT, with drr_toname of the BEGIN
 *	record filled to the end with 'n' and no NUL.
 */

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <libzfs_core.h>
#include <sys/zfs_ioctl.h>

#define	ZC_STRING_MARKER	"zc_string_marker"

static void
usage(void)
{
	(void) fprintf(stderr,
	    "usage: ioctl_unterminated fru POOL GUID\n"
	    "       ioctl_unterminated recv SNAPSHOT < STREAM\n");
	exit(2);
}

static int
do_fru(const char *pool, const char *guidstr)
{
	zfs_cmd_t zc;
	char *end;
	int fd, err;

	memset(&zc, 0, sizeof (zc));
	(void) strlcpy(zc.zc_name, pool, sizeof (zc.zc_name));
	memset(zc.zc_value, 'f', sizeof (zc.zc_value));
	(void) strlcpy(zc.zc_string, ZC_STRING_MARKER, sizeof (zc.zc_string));
	errno = 0;
	zc.zc_guid = strtoull(guidstr, &end, 0);
	if (errno != 0 || *end != '\0' || zc.zc_guid == 0) {
		(void) fprintf(stderr, "invalid guid: %s\n", guidstr);
		return (2);
	}

	if ((fd = open(ZFS_DEV, O_RDWR)) < 0) {
		(void) fprintf(stderr, "open(%s): %s\n", ZFS_DEV,
		    strerror(errno));
		return (2);
	}
	err = lzc_ioctl_fd(fd, ZFS_IOC_VDEV_SETFRU, &zc);
	if (err != 0)
		err = errno;
	(void) close(fd);
	if (err != 0) {
		(void) fprintf(stderr, "ZFS_IOC_VDEV_SETFRU: %s\n",
		    strerror(err));
		return (1);
	}
	(void) printf("%zu\n", sizeof (zc.zc_value));
	return (0);
}

/* Read up to len bytes; returns the number read, short only at EOF. */
static size_t
read_full(int fd, void *buf, size_t len)
{
	size_t off = 0;
	ssize_t n;

	while (off < len) {
		n = read(fd, (char *)buf + off, len - off);
		if (n < 0 && errno == EINTR)
			continue;
		if (n <= 0)
			break;
		off += n;
	}
	return (off);
}

/* Copy up to len bytes of payload; returns 0 at EOF. */
static int
copy_payload(int in, FILE *out, uint64_t len)
{
	char buf[65536];
	size_t n, want;

	while (len > 0) {
		want = len < sizeof (buf) ? len : sizeof (buf);
		n = read_full(in, buf, want);
		if (n > 0 && fwrite(buf, 1, n, out) != n)
			return (-1);
		if (n < want)
			return (0);
		len -= n;
	}
	return (1);
}

/*
 * Changing drr_toname changes the stream's running checksum, so zero
 * the checksum of every record after the BEGIN record (a zero checksum
 * is not verified), and pass the result to the receive.
 */
static int
do_recv(const char *snapname)
{
	dmu_replay_record_t drr, rec;
	uint64_t len;
	size_t n;
	FILE *out;
	int err;

	if (read_full(STDIN_FILENO, &drr, sizeof (drr)) != sizeof (drr) ||
	    drr.drr_type != DRR_BEGIN ||
	    drr.drr_u.drr_begin.drr_magic != DMU_BACKUP_MAGIC) {
		(void) fprintf(stderr, "no native DRR_BEGIN record\n");
		return (2);
	}
	memset(drr.drr_u.drr_begin.drr_toname, 'n',
	    sizeof (drr.drr_u.drr_begin.drr_toname));

	if ((out = tmpfile()) == NULL) {
		(void) fprintf(stderr, "tmpfile: %s\n", strerror(errno));
		return (2);
	}
	err = copy_payload(STDIN_FILENO, out, drr.drr_payloadlen);
	while (err == 1) {
		n = read_full(STDIN_FILENO, &rec, sizeof (rec));
		if (n < sizeof (rec)) {
			if (n > 0 && fwrite(&rec, 1, n, out) != n)
				err = -1;
			break;
		}
		memset(&rec.drr_u.drr_checksum.drr_checksum, 0,
		    sizeof (rec.drr_u.drr_checksum.drr_checksum));
		if (fwrite(&rec, 1, sizeof (rec), out) != sizeof (rec)) {
			err = -1;
			break;
		}
		switch (rec.drr_type) {
		case DRR_OBJECT:
			len = DRR_OBJECT_PAYLOAD_SIZE(&rec.drr_u.drr_object);
			break;
		case DRR_WRITE:
			len = DRR_WRITE_PAYLOAD_SIZE(&rec.drr_u.drr_write);
			break;
		case DRR_SPILL:
			len = DRR_SPILL_PAYLOAD_SIZE(&rec.drr_u.drr_spill);
			break;
		case DRR_WRITE_EMBEDDED:
			len = rec.drr_u.drr_write_embedded.drr_psize;
			len = P2ROUNDUP(len, 8);
			break;
		default:
			len = 0;
			break;
		}
		err = copy_payload(STDIN_FILENO, out, len);
	}
	if (err < 0 || fflush(out) != 0 || fseek(out, 0, SEEK_SET) != 0) {
		(void) fprintf(stderr, "writing the stream: %s\n",
		    strerror(errno));
		(void) fclose(out);
		return (2);
	}

	err = lzc_receive_with_header(snapname, NULL, NULL, B_FALSE, B_TRUE,
	    B_FALSE, fileno(out), &drr);
	(void) fclose(out);
	if (err == ZFS_ERR_STREAM_TRUNCATED) {
		(void) printf("receive: stream truncated\n");
		return (1);
	} else if (err != 0) {
		(void) printf("receive: error %d\n", err);
		return (1);
	}
	return (0);
}

int
main(int argc, char *argv[])
{
	int ret = 2;

	if (argc < 2)
		usage();

	if (libzfs_core_init() != 0) {
		(void) fprintf(stderr, "libzfs_core_init: %s\n",
		    strerror(errno));
		return (2);
	}

	if (strcmp(argv[1], "fru") == 0 && argc == 4)
		ret = do_fru(argv[2], argv[3]);
	else if (strcmp(argv[1], "recv") == 0 && argc == 3)
		ret = do_recv(argv[2]);
	else
		usage();

	libzfs_core_fini();
	return (ret);
}
